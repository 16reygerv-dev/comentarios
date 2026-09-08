#include "oauth-manager.hpp"

#include <QCryptographicHash>
#include <QDesktopServices>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRandomGenerator>
#include <QTcpSocket>
#include <QUrl>
#include <QUrlQuery>

namespace {
constexpr quint16 kFacebookOAuthPort = 18765;
constexpr auto kGraphVersion = "v26.0";

QString platformName(OAuthManager::Flow flow)
{
    switch (flow) {
    case OAuthManager::Flow::Google:
        return QStringLiteral("youtube");
    case OAuthManager::Flow::Facebook:
        return QStringLiteral("facebook");
    default:
        return QStringLiteral("oauth");
    }
}
}

OAuthManager::OAuthManager(QObject *parent) : QObject(parent)
{
    QObject::connect(&loopback, &QTcpServer::newConnection, this, [this]() { handleNewConnection(); });
}

OAuthManager::~OAuthManager()
{
    cancel();
}

void OAuthManager::configureGoogle(const QString &clientId, const QString &clientSecret)
{
    googleClientId = clientId.trimmed();
    googleClientSecret = clientSecret.trimmed();
}

QString OAuthManager::normalizedBaseUrl(const QString &value)
{
    QString result = value.trimmed();
    while (result.endsWith('/'))
        result.chop(1);
    const QUrl parsed(result);
    if (!parsed.isValid() || (parsed.scheme() != "https" && parsed.scheme() != "http"))
        return {};
    return result;
}

void OAuthManager::configureFacebook(const QString &appId, const QString &appSecret, const QString &brokerBaseUrl)
{
    facebookAppId = appId.trimmed();
    facebookAppSecret = appSecret.trimmed();
    facebookBrokerUrl = normalizedBaseUrl(brokerBaseUrl);
}

void OAuthManager::emitStatus(const QString &platform, const QString &message, bool ok)
{
    if (onStatus)
        onStatus(platform, message, ok);
}

QString OAuthManager::randomUrlSafe(int byteCount)
{
    QByteArray bytes;
    bytes.resize(byteCount);
    for (int i = 0; i < byteCount; ++i)
        bytes[i] = static_cast<char>(QRandomGenerator::system()->generate() & 0xFF);
    return QString::fromLatin1(bytes.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

QString OAuthManager::pkceChallenge(const QString &verifier)
{
    const QByteArray digest = QCryptographicHash::hash(verifier.toUtf8(), QCryptographicHash::Sha256);
    return QString::fromLatin1(digest.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

QByteArray OAuthManager::formBody(const QList<QPair<QString, QString>> &items)
{
    QUrlQuery query;
    for (const auto &item : items)
        query.addQueryItem(item.first, item.second);
    return query.toString(QUrl::FullyEncoded).toUtf8();
}

bool OAuthManager::startLoopbackServer(Flow requestedFlow)
{
    cancel();
    flow = requestedFlow;
    state = randomUrlSafe(24);

    quint16 requestedPort = 0;
    if (requestedFlow == Flow::Facebook)
        requestedPort = kFacebookOAuthPort;

    if (!loopback.listen(QHostAddress::LocalHost, requestedPort)) {
        emitStatus(platformName(requestedFlow),
                   requestedFlow == Flow::Facebook
                       ? QString("No pude abrir http://127.0.0.1:%1 para OAuth. Cierra otra app que use ese puerto e inténtalo de nuevo.").arg(kFacebookOAuthPort)
                       : QStringLiteral("No pude abrir el receptor local de OAuth."),
                   false);
        flow = Flow::None;
        return false;
    }

    const quint16 port = loopback.serverPort();
    if (requestedFlow == Flow::Google)
        googleRedirect = QString("http://127.0.0.1:%1").arg(port);
    else
        facebookRedirect = QString("http://127.0.0.1:%1/oauth/callback/facebook").arg(port);
    return true;
}

bool OAuthManager::startGoogleLogin()
{
    if (googleClientId.isEmpty()) {
        emitStatus("youtube", "Configura el Client ID OAuth de Google una sola vez.", false);
        return false;
    }
    if (!startLoopbackServer(Flow::Google))
        return false;

    codeVerifier = randomUrlSafe(64);
    QUrl url("https://accounts.google.com/o/oauth2/v2/auth");
    QUrlQuery q;
    q.addQueryItem("client_id", googleClientId);
    q.addQueryItem("redirect_uri", googleRedirect);
    q.addQueryItem("response_type", "code");
    q.addQueryItem("scope", "https://www.googleapis.com/auth/youtube.readonly");
    q.addQueryItem("access_type", "offline");
    q.addQueryItem("prompt", "consent");
    q.addQueryItem("state", state);
    q.addQueryItem("code_challenge", pkceChallenge(codeVerifier));
    q.addQueryItem("code_challenge_method", "S256");
    url.setQuery(q);

    emitStatus("youtube", "Abriendo Google para autorizar…", true);
    if (!QDesktopServices::openUrl(url)) {
        emitStatus("youtube", "No pude abrir el navegador para Google OAuth.", false);
        cancel();
        return false;
    }
    return true;
}

bool OAuthManager::startFacebookLogin()
{
    const bool useBroker = !facebookBrokerUrl.isEmpty();
    if (!useBroker && (facebookAppId.isEmpty() || facebookAppSecret.isEmpty())) {
        emitStatus("facebook", "Configura el broker OAuth o App ID + App Secret de Meta.", false);
        return false;
    }
    if (!startLoopbackServer(Flow::Facebook))
        return false;
    activeFacebookBrokerFlow = useBroker;

    QUrl url;
    if (activeFacebookBrokerFlow) {
        url = QUrl(facebookBrokerUrl + "/facebook/start");
        QUrlQuery q;
        q.addQueryItem("state", state);
        q.addQueryItem("return_uri", facebookRedirect);
        url.setQuery(q);
        emitStatus("facebook", "Abriendo Facebook mediante el broker seguro…", true);
    } else {
        url = QUrl(QString("https://www.facebook.com/%1/dialog/oauth").arg(kGraphVersion));
        QUrlQuery q;
        q.addQueryItem("client_id", facebookAppId);
        q.addQueryItem("redirect_uri", facebookRedirect);
        q.addQueryItem("response_type", "code");
        q.addQueryItem("state", state);
        q.addQueryItem("scope", "pages_show_list,pages_read_engagement,pages_read_user_content");
        url.setQuery(q);
        emitStatus("facebook", "Abriendo Facebook para autorizar…", true);
    }

    if (!QDesktopServices::openUrl(url)) {
        emitStatus("facebook", "No pude abrir el navegador para Facebook OAuth.", false);
        cancel();
        return false;
    }
    return true;
}

void OAuthManager::handleNewConnection()
{
    while (loopback.hasPendingConnections()) {
        QTcpSocket *socket = loopback.nextPendingConnection();
        if (!socket)
            continue;

        QObject::connect(socket, &QTcpSocket::readyRead, socket, [this, socket]() {
            const QByteArray request = socket->readAll();
            const QList<QByteArray> lines = request.split('\n');
            if (lines.isEmpty())
                return;

            const QList<QByteArray> first = lines.first().trimmed().split(' ');
            if (first.size() < 2) {
                sendBrowserResponse(socket, false, "Solicitud OAuth inválida.");
                return;
            }

            const QString target = QString::fromUtf8(first.at(1));
            QUrl callbackUrl(QStringLiteral("http://127.0.0.1") + target);
            QUrlQuery query(callbackUrl);
            const QString returnedState = query.queryItemValue("state");
            const QString code = query.queryItemValue("code");
            const QString brokerCode = query.queryItemValue("broker_code");
            const QString error = query.queryItemValue("error");
            const QString errorDescription = query.queryItemValue("error_description");

            if (returnedState.isEmpty() || returnedState != state) {
                sendBrowserResponse(socket, false, "Estado OAuth inválido. Vuelve a OBS e inténtalo otra vez.");
                emitStatus(platformName(flow), "OAuth rechazado por validación de seguridad (state).", false);
                cancel();
                return;
            }

            if (!error.isEmpty()) {
                const QString message = errorDescription.isEmpty() ? error : errorDescription;
                sendBrowserResponse(socket, false, message);
                emitStatus(platformName(flow), "Autorización cancelada: " + message, false);
                cancel();
                return;
            }

            const Flow completedFlow = flow;
            const bool brokerFlow = activeFacebookBrokerFlow;
            const QString completedState = state;

            if (completedFlow == Flow::Facebook && brokerFlow) {
                if (brokerCode.isEmpty()) {
                    sendBrowserResponse(socket, false, "El broker no devolvió un código de sesión.");
                    emitStatus("facebook", "Respuesta inválida del broker OAuth.", false);
                    cancel();
                    return;
                }
                sendBrowserResponse(socket, true, "Autorización recibida. Puedes cerrar esta pestaña y volver a OBS.");
                loopback.close();
                flow = Flow::None;
                exchangeFacebookBrokerCode(brokerCode, completedState);
                return;
            }

            if (code.isEmpty()) {
                sendBrowserResponse(socket, false, "No se recibió código de autorización.");
                emitStatus(platformName(flow), "No se recibió código OAuth.", false);
                cancel();
                return;
            }

            sendBrowserResponse(socket, true, "Autorización recibida. Puedes cerrar esta pestaña y volver a OBS.");
            loopback.close();
            flow = Flow::None;

            if (completedFlow == Flow::Google)
                exchangeGoogleCode(code);
            else if (completedFlow == Flow::Facebook)
                exchangeFacebookCode(code);
        });
    }
}

void OAuthManager::sendBrowserResponse(QTcpSocket *socket, bool ok, const QString &message)
{
    if (!socket)
        return;
    const QString accent = ok ? "#39d98a" : "#ff6b6b";
    const QString title = ok ? "Conectado" : "No se pudo conectar";
    const QString html = QString(
        "<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>Social Comments</title></head><body style='font-family:Segoe UI,Arial;background:#0f1115;color:#fff;display:grid;place-items:center;height:100vh;margin:0'>"
        "<div style='max-width:520px;padding:32px;border-radius:18px;background:#171a21;box-shadow:0 18px 60px rgba(0,0,0,.35)'>"
        "<div style='font-size:14px;color:%1;font-weight:700;margin-bottom:10px'>SOCIAL COMMENTS FOR OBS</div>"
        "<h1 style='font-size:28px;margin:0 0 12px'>%2</h1><p style='line-height:1.55;color:#c9ced8'>%3</p>"
        "</div></body></html>").arg(accent, title, message.toHtmlEscaped());
    const QByteArray body = html.toUtf8();
    QByteArray response = "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nConnection: close\r\nContent-Length: ";
    response += QByteArray::number(body.size());
    response += "\r\n\r\n";
    response += body;
    socket->write(response);
    socket->flush();
    socket->disconnectFromHost();
}

void OAuthManager::exchangeGoogleCode(const QString &code)
{
    QNetworkRequest request(QUrl("https://oauth2.googleapis.com/token"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");

    QList<QPair<QString, QString>> items = {
        {"code", code},
        {"client_id", googleClientId},
        {"redirect_uri", googleRedirect},
        {"grant_type", "authorization_code"},
        {"code_verifier", codeVerifier},
    };
    if (!googleClientSecret.isEmpty())
        items.append({"client_secret", googleClientSecret});

    QNetworkReply *reply = network.post(request, formBody(items));
    QObject::connect(reply, &QNetworkReply::finished, reply, [this, reply]() {
        const QByteArray bytes = reply->readAll();
        const auto error = reply->error();
        const QJsonObject root = QJsonDocument::fromJson(bytes).object();
        reply->deleteLater();

        if (error != QNetworkReply::NoError || root.contains("error")) {
            QString detail = root.value("error_description").toString();
            if (detail.isEmpty())
                detail = root.value("error").toString("No se pudo intercambiar el código OAuth de Google.");
            emitStatus("youtube", detail, false);
            return;
        }

        const QString access = root.value("access_token").toString();
        const QString refresh = root.value("refresh_token").toString();
        const int expires = root.value("expires_in").toInt(3600);
        if (access.isEmpty()) {
            emitStatus("youtube", "Google no devolvió un access token.", false);
            return;
        }

        emitStatus("youtube", "Cuenta de YouTube autorizada. Buscando directo activo…", true);
        if (onGoogleToken)
            onGoogleToken(access, refresh, expires);
    });
}

void OAuthManager::refreshGoogleAccessToken(const QString &refreshToken)
{
    if (googleClientId.isEmpty() || refreshToken.isEmpty())
        return;

    QNetworkRequest request(QUrl("https://oauth2.googleapis.com/token"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    QList<QPair<QString, QString>> items = {
        {"client_id", googleClientId},
        {"refresh_token", refreshToken},
        {"grant_type", "refresh_token"},
    };
    if (!googleClientSecret.isEmpty())
        items.append({"client_secret", googleClientSecret});

    QNetworkReply *reply = network.post(request, formBody(items));
    QObject::connect(reply, &QNetworkReply::finished, reply, [this, reply, refreshToken]() {
        const QByteArray bytes = reply->readAll();
        const auto error = reply->error();
        const QJsonObject root = QJsonDocument::fromJson(bytes).object();
        reply->deleteLater();

        if (error != QNetworkReply::NoError || root.contains("error")) {
            emitStatus("youtube", "La sesión de YouTube caducó. Pulsa Conectar YouTube otra vez.", false);
            return;
        }

        const QString access = root.value("access_token").toString();
        const int expires = root.value("expires_in").toInt(3600);
        if (!access.isEmpty() && onGoogleToken)
            onGoogleToken(access, refreshToken, expires);
    });
}

void OAuthManager::exchangeFacebookCode(const QString &code)
{
    QUrl url(QString("https://graph.facebook.com/%1/oauth/access_token").arg(kGraphVersion));
    QUrlQuery q;
    q.addQueryItem("client_id", facebookAppId);
    q.addQueryItem("redirect_uri", facebookRedirect);
    q.addQueryItem("client_secret", facebookAppSecret);
    q.addQueryItem("code", code);
    url.setQuery(q);

    QNetworkReply *reply = network.get(QNetworkRequest(url));
    QObject::connect(reply, &QNetworkReply::finished, reply, [this, reply]() {
        const QByteArray bytes = reply->readAll();
        const auto error = reply->error();
        const QJsonObject root = QJsonDocument::fromJson(bytes).object();
        reply->deleteLater();

        if (error != QNetworkReply::NoError || root.contains("error")) {
            QString detail = root.value("error").toObject().value("message").toString();
            if (detail.isEmpty())
                detail = "No se pudo obtener el token de Facebook.";
            emitStatus("facebook", detail, false);
            return;
        }

        const QString shortToken = root.value("access_token").toString();
        if (shortToken.isEmpty()) {
            emitStatus("facebook", "Facebook no devolvió un access token.", false);
            return;
        }
        exchangeFacebookLongLivedToken(shortToken);
    });
}

void OAuthManager::exchangeFacebookLongLivedToken(const QString &shortToken)
{
    QUrl url(QString("https://graph.facebook.com/%1/oauth/access_token").arg(kGraphVersion));
    QUrlQuery q;
    q.addQueryItem("grant_type", "fb_exchange_token");
    q.addQueryItem("client_id", facebookAppId);
    q.addQueryItem("client_secret", facebookAppSecret);
    q.addQueryItem("fb_exchange_token", shortToken);
    url.setQuery(q);

    QNetworkReply *reply = network.get(QNetworkRequest(url));
    QObject::connect(reply, &QNetworkReply::finished, reply, [this, reply, shortToken]() {
        const QByteArray bytes = reply->readAll();
        const QJsonObject root = QJsonDocument::fromJson(bytes).object();
        reply->deleteLater();
        const QString longToken = root.value("access_token").toString();
        const QString usableToken = longToken.isEmpty() ? shortToken : longToken;

        emitStatus("facebook", "Cuenta autorizada. Cargando tus Páginas…", true);
        if (onFacebookToken)
            onFacebookToken(usableToken);
        fetchFacebookPages(usableToken);
    });
}

void OAuthManager::exchangeFacebookBrokerCode(const QString &brokerCode, const QString &returnedState)
{
    if (facebookBrokerUrl.isEmpty()) {
        emitStatus("facebook", "El broker OAuth no está configurado.", false);
        return;
    }

    QNetworkRequest request(QUrl(facebookBrokerUrl + "/facebook/exchange"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    const QJsonObject payload{{"code", brokerCode}, {"state", returnedState}};
    QNetworkReply *reply = network.post(request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    QObject::connect(reply, &QNetworkReply::finished, reply, [this, reply]() {
        const QByteArray bytes = reply->readAll();
        const auto error = reply->error();
        const QJsonObject root = QJsonDocument::fromJson(bytes).object();
        reply->deleteLater();

        const QString token = root.value("access_token").toString();
        if (error != QNetworkReply::NoError || token.isEmpty()) {
            QString detail = root.value("error_description").toString();
            if (detail.isEmpty())
                detail = root.value("error").toString("No se pudo completar el login mediante el broker.");
            emitStatus("facebook", detail, false);
            return;
        }

        emitStatus("facebook", "Cuenta autorizada. Cargando tus Páginas…", true);
        if (onFacebookToken)
            onFacebookToken(token);
        fetchFacebookPages(token);
    });
}

void OAuthManager::fetchFacebookPages(const QString &userAccessToken)
{
    if (userAccessToken.isEmpty()) {
        emitStatus("facebook", "No hay sesión de Facebook guardada.", false);
        return;
    }

    QUrl url(QString("https://graph.facebook.com/%1/me/accounts").arg(kGraphVersion));
    QUrlQuery q;
    q.addQueryItem("fields", "id,name,access_token,tasks");
    q.addQueryItem("limit", "100");
    q.addQueryItem("access_token", userAccessToken);
    url.setQuery(q);

    QNetworkReply *reply = network.get(QNetworkRequest(url));
    QObject::connect(reply, &QNetworkReply::finished, reply, [this, reply]() {
        const QByteArray bytes = reply->readAll();
        const auto error = reply->error();
        const QJsonObject root = QJsonDocument::fromJson(bytes).object();
        reply->deleteLater();

        if (error != QNetworkReply::NoError || root.contains("error")) {
            QString detail = root.value("error").toObject().value("message").toString();
            if (detail.isEmpty())
                detail = "No pude cargar las Páginas administradas.";
            emitStatus("facebook", detail, false);
            return;
        }

        QVector<OAuthFacebookPage> pages;
        const QJsonArray data = root.value("data").toArray();
        pages.reserve(data.size());
        for (const QJsonValue &value : data) {
            const QJsonObject obj = value.toObject();
            OAuthFacebookPage page;
            page.id = obj.value("id").toString();
            page.name = obj.value("name").toString();
            page.accessToken = obj.value("access_token").toString();
            if (!page.id.isEmpty() && !page.accessToken.isEmpty())
                pages.push_back(page);
        }

        if (pages.isEmpty()) {
            emitStatus("facebook", "La cuenta no devolvió Páginas accesibles. Revisa permisos y acceso de la app.", false);
            return;
        }
        emitStatus("facebook", QString("%1 Página(s) disponible(s).").arg(pages.size()), true);
        if (onFacebookPages)
            onFacebookPages(pages);
    });
}

void OAuthManager::fetchFacebookLives(const QString &pageId, const QString &pageAccessToken)
{
    if (pageId.isEmpty() || pageAccessToken.isEmpty())
        return;

    QUrl url(QString("https://graph.facebook.com/%1/%2/live_videos").arg(kGraphVersion, pageId));
    QUrlQuery q;
    q.addQueryItem("fields", "id,title,status,creation_time,permalink_url");
    q.addQueryItem("broadcast_status", "LIVE");
    q.addQueryItem("limit", "25");
    q.addQueryItem("access_token", pageAccessToken);
    url.setQuery(q);

    emitStatus("facebook", "Buscando Live activo en la Página…", true);
    QNetworkReply *reply = network.get(QNetworkRequest(url));
    QObject::connect(reply, &QNetworkReply::finished, reply, [this, reply]() {
        const QByteArray bytes = reply->readAll();
        const auto error = reply->error();
        const QJsonObject root = QJsonDocument::fromJson(bytes).object();
        reply->deleteLater();

        if (error != QNetworkReply::NoError || root.contains("error")) {
            QString detail = root.value("error").toObject().value("message").toString();
            if (detail.isEmpty())
                detail = "No pude consultar los Live de la Página.";
            emitStatus("facebook", detail, false);
            return;
        }

        QVector<OAuthFacebookLive> lives;
        const QJsonArray data = root.value("data").toArray();
        for (const QJsonValue &value : data) {
            const QJsonObject obj = value.toObject();
            OAuthFacebookLive live;
            live.id = obj.value("id").toString();
            live.title = obj.value("title").toString();
            live.status = obj.value("status").toString();
            live.permalink = obj.value("permalink_url").toString();
            if (!live.id.isEmpty())
                lives.push_back(live);
        }

        if (lives.isEmpty())
            emitStatus("facebook", "No encontré un Live activo en esa Página.", false);
        else
            emitStatus("facebook", QString("Live activo encontrado (%1).").arg(lives.size()), true);
        if (onFacebookLives)
            onFacebookLives(lives);
    });
}

void OAuthManager::revokeGoogleToken(const QString &token)
{
    if (token.isEmpty()) {
        if (onRevoke)
            onRevoke("youtube", false, "No hay token de Google para revocar.");
        return;
    }

    QNetworkRequest request(QUrl("https://oauth2.googleapis.com/revoke"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    QNetworkReply *reply = network.post(request, formBody({{"token", token}}));
    QObject::connect(reply, &QNetworkReply::finished, reply, [this, reply]() {
        const bool ok = reply->error() == QNetworkReply::NoError && reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200;
        const QString detail = ok ? QStringLiteral("Acceso de Google revocado.")
                                  : QStringLiteral("Google no confirmó la revocación. Puedes volver a intentarlo.");
        reply->deleteLater();
        if (onRevoke)
            onRevoke("youtube", ok, detail);
    });
}

void OAuthManager::revokeFacebookPermissions(const QString &userAccessToken)
{
    if (userAccessToken.isEmpty()) {
        if (onRevoke)
            onRevoke("facebook", false, "No hay token de Facebook para revocar.");
        return;
    }

    QNetworkRequest request(QUrl(QString("https://graph.facebook.com/%1/me/permissions").arg(kGraphVersion)));
    request.setRawHeader("Authorization", QByteArray("Bearer ") + userAccessToken.toUtf8());
    QNetworkReply *reply = network.sendCustomRequest(request, "DELETE");
    QObject::connect(reply, &QNetworkReply::finished, reply, [this, reply]() {
        const QByteArray bytes = reply->readAll();
        const QJsonObject root = QJsonDocument::fromJson(bytes).object();
        const bool successField = root.value("success").toBool(false);
        const bool ok = reply->error() == QNetworkReply::NoError && successField;
        QString detail = ok ? QStringLiteral("Permisos de Facebook revocados.")
                            : root.value("error").toObject().value("message").toString();
        if (detail.isEmpty())
            detail = "Facebook no confirmó la revocación. Puedes quitar la app desde la configuración de tu cuenta.";
        reply->deleteLater();
        if (onRevoke)
            onRevoke("facebook", ok, detail);
    });
}

void OAuthManager::cancel()
{
    if (loopback.isListening())
        loopback.close();
    flow = Flow::None;
    state.clear();
    codeVerifier.clear();
    activeFacebookBrokerFlow = false;
}
