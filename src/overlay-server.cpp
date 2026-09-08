#include "overlay-server.hpp"

#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpSocket>

OverlayServer::OverlayServer(QObject *parent) : QObject(parent)
{
    QObject::connect(&server, &QTcpServer::newConnection, this, [this]() { handleConnection(); });
}

bool OverlayServer::start()
{
    if (server.isListening())
        return true;

    for (quint16 candidate = 43991; candidate < 44020; ++candidate) {
        if (server.listen(QHostAddress::LocalHost, candidate))
            return true;
    }
    return false;
}

void OverlayServer::stop()
{
    server.close();
}

QString OverlayServer::overlayUrl() const
{
    return QString("http://127.0.0.1:%1/overlay").arg(server.serverPort());
}

void OverlayServer::setStyle(const OverlayStyle &newStyle)
{
    style = newStyle;
    ++version;
}

void OverlayServer::showComment(const SocialComment &comment, bool pin)
{
    current = comment;
    visible = true;
    pinned = pin;
    ++version;
}

void OverlayServer::clear()
{
    visible = false;
    pinned = false;
    ++version;
}

void OverlayServer::handleConnection()
{
    while (server.hasPendingConnections()) {
        QTcpSocket *socket = server.nextPendingConnection();
        if (!socket)
            continue;

        QObject::connect(socket, &QTcpSocket::readyRead, socket, [this, socket]() { handleRequest(socket); });
        QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    }
}

void OverlayServer::handleRequest(QTcpSocket *socket)
{
    if (!socket->canReadLine())
        return;

    const QByteArray request = socket->readAll();

    const QByteArray firstLine = request.left(request.indexOf("\r\n"));
    const QList<QByteArray> parts = firstLine.split(' ');
    if (parts.size() < 2) {
        writeHttp(socket, "text/plain; charset=utf-8", "Bad Request", 400);
        return;
    }

    const QByteArray path = parts.at(1).split('?').first();
    if (path == "/overlay" || path == "/") {
        writeHttp(socket, "text/html; charset=utf-8", overlayHtml());
    } else if (path == "/api/state") {
        writeHttp(socket, "application/json; charset=utf-8", renderStateJson());
    } else {
        writeHttp(socket, "text/plain; charset=utf-8", "Not Found", 404);
    }
}

QByteArray OverlayServer::renderStateJson() const
{
    QJsonObject comment;
    comment["id"] = current.id;
    comment["platform"] = current.platform;
    comment["author"] = current.author;
    comment["message"] = current.message;
    comment["avatarUrl"] = current.avatarUrl;

    QJsonObject styleObj;
    styleObj["fontFamily"] = style.fontFamily;
    styleObj["textColor"] = style.textColor;
    styleObj["nameColor"] = style.nameColor;
    styleObj["backgroundColor"] = style.backgroundColor;
    styleObj["backgroundOpacity"] = style.backgroundOpacity;
    styleObj["durationSeconds"] = style.durationSeconds;
    styleObj["animation"] = style.animation;
    styleObj["sizePreset"] = style.sizePreset;
    styleObj["showAvatar"] = style.showAvatar;
    styleObj["showLogo"] = style.showLogo;

    QJsonObject root;
    root["version"] = static_cast<qint64>(version);
    root["visible"] = visible;
    root["pinned"] = pinned;
    root["comment"] = comment;
    root["style"] = styleObj;

    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

void OverlayServer::writeHttp(QTcpSocket *socket, const QByteArray &contentType, const QByteArray &body, int status)
{
    QByteArray statusText = "OK";
    if (status == 404)
        statusText = "Not Found";
    else if (status == 400)
        statusText = "Bad Request";

    QByteArray response;
    response += "HTTP/1.1 " + QByteArray::number(status) + " " + statusText + "\r\n";
    response += "Content-Type: " + contentType + "\r\n";
    response += "Cache-Control: no-store, no-cache, must-revalidate\r\n";
    response += "Access-Control-Allow-Origin: *\r\n";
    response += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    response += "Connection: close\r\n\r\n";
    response += body;
    socket->write(response);
    socket->disconnectFromHost();
}

QByteArray OverlayServer::overlayHtml() const
{
    static const char *html = R"HTML(<!doctype html>
<html lang="es">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<style>
html,body{width:100%;height:100%;margin:0;background:transparent;overflow:hidden;font-family:Inter,Arial,sans-serif}
*{box-sizing:border-box}
#wrap{width:100%;height:100%;display:flex;align-items:flex-end;padding:16px}
#card{width:min(900px,100%);min-height:150px;border-radius:22px;padding:20px 24px;display:grid;grid-template-columns:auto 1fr;gap:14px 16px;align-items:center;box-shadow:0 14px 40px rgba(0,0,0,.22);transform-origin:left bottom;transition:opacity .24s ease,transform .28s cubic-bezier(.2,.8,.2,1)}
#card.hidden{opacity:0;transform:translateY(28px) scale(.985);pointer-events:none}
#card.fade.hidden{transform:none}
#card.none{transition:none}
#card.none.hidden{transform:none}
#identity{display:flex;align-items:center;gap:10px;min-width:0}
#avatar{width:46px;height:46px;border-radius:999px;object-fit:cover;background:rgba(255,255,255,.12);display:none}
#avatar.initials{display:flex;align-items:center;justify-content:center;color:white;font-weight:700;font-size:16px}
#meta{display:flex;align-items:center;gap:10px;min-width:0}
#logo{width:28px;height:28px;display:flex;align-items:center;justify-content:center;flex:0 0 28px}
#logo svg{display:block;width:28px;height:28px}
#author{font-size:20px;font-weight:750;line-height:1.15;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
#message{grid-column:2;font-size:30px;font-weight:560;line-height:1.18;letter-spacing:-.02em;word-break:break-word;display:-webkit-box;-webkit-box-orient:vertical;-webkit-line-clamp:4;overflow:hidden}
#pin{grid-column:2;font-size:12px;font-weight:700;letter-spacing:.08em;text-transform:uppercase;opacity:.72;display:none;margin-top:-4px}
#card.size-compact{min-height:122px;border-radius:18px;padding:16px 20px;gap:10px 13px}
#card.size-compact #avatar{width:38px;height:38px}
#card.size-compact #author{font-size:17px}
#card.size-compact #message{font-size:24px}
#card.size-large{min-height:182px;border-radius:26px;padding:24px 28px;gap:16px 18px}
#card.size-large #avatar{width:54px;height:54px}
#card.size-large #author{font-size:23px}
#card.size-large #message{font-size:38px;line-height:1.14}
@media(max-width:600px){#card{padding:16px 18px;border-radius:18px;grid-template-columns:auto 1fr}#message{font-size:24px}#author{font-size:17px}}
</style>
</head>
<body>
<div id="wrap"><div id="card" class="hidden slide">
  <div id="identity"><div id="avatar"></div></div>
  <div id="meta"><div id="logo"></div><div id="author"></div></div>
  <div id="message"></div><div id="pin">DESTACADO</div>
</div></div>
<script>
const card=document.getElementById('card'),avatar=document.getElementById('avatar'),logo=document.getElementById('logo'),author=document.getElementById('author'),message=document.getElementById('message'),pin=document.getElementById('pin');
let lastVersion=-1;
function ytLogo(){return '<svg viewBox="0 0 28 28" aria-hidden="true"><rect x="1" y="5" width="26" height="18" rx="5" fill="#ff0033"/><path d="M11.5 9.5 19 14l-7.5 4.5z" fill="#fff"/></svg>'}
function fbLogo(){return '<svg viewBox="0 0 28 28" aria-hidden="true"><circle cx="14" cy="14" r="13" fill="#1877F2"/><path d="M16.2 27V16.9h3.4l.5-4h-3.9v-2.6c0-1.15.32-1.94 1.98-1.94h2.12V4.78c-.37-.05-1.62-.16-3.08-.16-3.05 0-5.14 1.86-5.14 5.28v3h-3.45v4h3.45V27z" fill="#fff"/></svg>'}
function hexRgba(hex,a){let h=(hex||'#111318').replace('#','');if(h.length===3)h=h.split('').map(x=>x+x).join('');const n=parseInt(h,16);return `rgba(${(n>>16)&255},${(n>>8)&255},${n&255},${Math.max(0,Math.min(100,a))/100})`}
function initials(name){return (name||'?').trim().split(/\s+/).slice(0,2).map(x=>x[0]||'').join('').toUpperCase()}
function apply(data){const s=data.style||{},c=data.comment||{};card.classList.remove('none','fade','slide','size-compact','size-normal','size-large');card.classList.add(s.animation||'fade');card.classList.add('size-'+(s.sizePreset||'normal'));card.style.fontFamily=`${s.fontFamily||'Inter'}, Arial, sans-serif`;card.style.background=hexRgba(s.backgroundColor,s.backgroundOpacity);message.style.color=s.textColor||'#fff';author.style.color=s.nameColor||'#fff';author.textContent=c.author||'';message.textContent=c.message||'';logo.innerHTML=s.showLogo?(c.platform==='facebook'?fbLogo():ytLogo()):'';logo.style.display=s.showLogo?'flex':'none';pin.style.display=data.pinned?'block':'none';
  avatar.innerHTML='';avatar.removeAttribute('style');avatar.className='';
  if(s.showAvatar){const u=c.avatarUrl||'';if(/^https?:\/\//i.test(u)){avatar.style.display='block';avatar.style.backgroundImage=`url("${u.replace(/"/g,'')}")`;avatar.style.backgroundSize='cover';avatar.style.backgroundPosition='center';}else{avatar.className='initials';avatar.style.display='flex';avatar.textContent=initials(c.author)}}else avatar.style.display='none';
  if(data.visible)requestAnimationFrame(()=>card.classList.remove('hidden'));else card.classList.add('hidden');
}
async function tick(){try{const r=await fetch('/api/state?t='+Date.now(),{cache:'no-store'});const d=await r.json();if(d.version!==lastVersion){lastVersion=d.version;apply(d)}}catch(e){}finally{setTimeout(tick,180)}}
tick();
</script></body></html>)HTML";
    return QByteArray(html);
}
