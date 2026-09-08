#include "social-api.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QUrl>
#include <QUrlQuery>
#include <QVector>

namespace {
constexpr auto kGraphVersion = "v26.0";
}

SocialApiClient::SocialApiClient(QObject *parent) : QObject(parent)
{
    ytTimer.setSingleShot(true);
    QObject::connect(&ytTimer, &QTimer::timeout, this, [this]() { pollYouTube(); });

    fbTimer.setInterval(2000);
    QObject::connect(&fbTimer, &QTimer::timeout, this, [this]() { pollFacebook(); });
}

void SocialApiClient::emitStatus(const QString &platform, const QString &message, bool ok)
{
    if (onStatus)
        onStatus(platform, message, ok);
}

QString SocialApiClient::extractYouTubeVideoId(const QString &input) const
{
    const QString trimmed = input.trimmed();
    QRegularExpression plain("^[A-Za-z0-9_-]{11}$");
    if (plain.match(trimmed).hasMatch())
        return trimmed;

    QUrl url(trimmed);
    if (!url.isValid())
        return {};

    const QString host = url.host().toLower();
    if (host == "youtu.be")
        return url.path().split('/', Qt::SkipEmptyParts).value(0);

    if (host.contains("youtube.com")) {
        QUrlQuery query(url);
        const QString v = query.queryItemValue("v");
        if (!v.isEmpty())
            return v;

        const QStringList segments = url.path().split('/', Qt::SkipEmptyParts);
        if (segments.size() >= 2 &&
            (segments.at(0) == "live" || segments.at(0) == "shorts" || segments.at(0) == "embed"))
            return segments.at(1);
    }
    return {};
}

QString SocialApiClient::extractFacebookVideoId(const QString &input) const
{
    const QString trimmed = input.trimmed();
    if (QRegularExpression("^\\d{5,}$").match(trimmed).hasMatch())
        return trimmed;

    QRegularExpression re("(?:videos|live|watch)/(?:[^/]+/)?(\\d{5,})");
    const QRegularExpressionMatch match = re.match(trimmed);
    if (match.hasMatch())
        return match.captured(1);

    QRegularExpression anyDigits("(\\d{8,})");
    const QRegularExpressionMatch fallback = anyDigits.match(trimmed);
    return fallback.hasMatch() ? fallback.captured(1) : QString();
}

QNetworkRequest SocialApiClient::youtubeRequest(const QUrl &url) const
{
    QNetworkRequest request(url);
    if (ytOAuth && !ytAccessToken.isEmpty())
        request.setRawHeader("Authorization", "Bearer " + ytAccessToken.toUtf8());
    return request;
}

void SocialApiClient::connectYouTubeOAuth(const QString &accessToken, const QString &videoOrUrl)
{
    disconnectYouTube();
    ytOAuth = true;
    ytAccessToken = accessToken.trimmed();
    ytVideoId = videoOrUrl.trimmed().isEmpty() ? QString() : extractYouTubeVideoId(videoOrUrl);

    if (ytAccessToken.isEmpty()) {
        emitStatus("youtube", "No hay access token OAuth de YouTube.", false);
        return;
    }
    if (!videoOrUrl.trimmed().isEmpty() && ytVideoId.isEmpty()) {
        emitStatus("youtube", "La URL/ID de YouTube no es válida.", false);
        return;
    }

    ytEnabled = true;
    emitStatus("youtube", "Buscando transmisión activa…", true);
    if (ytVideoId.isEmpty())
        discoverYouTubeLive();
    else
        resolveYouTubeLiveChat();
}

void SocialApiClient::updateYouTubeOAuthToken(const QString &accessToken)
{
    ytAccessToken = accessToken.trimmed();
}

void SocialApiClient::connectYouTubeApiKey(const QString &apiKey, const QString &videoOrUrl)
{
    disconnectYouTube();
    ytOAuth = false;
    ytApiKey = apiKey.trimmed();
    ytVideoId = extractYouTubeVideoId(videoOrUrl);

    if (ytApiKey.isEmpty() || ytVideoId.isEmpty()) {
        emitStatus("youtube", "Falta API key o URL/ID válido del directo.", false);
        return;
    }

    ytEnabled = true;
    emitStatus("youtube", "Conectando con API key…", true);
    resolveYouTubeLiveChat();
}

void SocialApiClient::disconnectYouTube()
{
    ytEnabled = false;
    ytPrimed = false;
    ytTimer.stop();
    ytOAuth = false;
    ytApiKey.clear();
    ytAccessToken.clear();
    ytVideoId.clear();
    ytLiveChatId.clear();
    ytPageToken.clear();
    ytSeen.clear();
}

void SocialApiClient::discoverYouTubeLive()
{
    if (!ytEnabled || !ytOAuth)
        return;

    QUrl url("https://www.googleapis.com/youtube/v3/liveBroadcasts");
    QUrlQuery q;
    q.addQueryItem("part", "id,snippet,status");
    q.addQueryItem("broadcastStatus", "active");
    q.addQueryItem("mine", "true");
    q.addQueryItem("maxResults", "10");
    url.setQuery(q);

    QNetworkReply *reply = network.get(youtubeRequest(url));
    QObject::connect(reply, &QNetworkReply::finished, reply, [this, reply]() {
        const QByteArray bytes = reply->readAll();
        const auto error = reply->error();
        const int http = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        reply->deleteLater();
        if (!ytEnabled)
            return;

        const QJsonObject root = QJsonDocument::fromJson(bytes).object();
        if (error != QNetworkReply::NoError || root.contains("error")) {
            if (http == 401)
                emitStatus("youtube", "La sesión OAuth de YouTube caducó. Reconecta la cuenta.", false);
            else
                emitStatus("youtube", "No pude consultar tus transmisiones activas.", false);
            return;
        }

        const QJsonArray items = root.value("items").toArray();
        if (items.isEmpty()) {
            emitStatus("youtube", "Cuenta conectada · esperando una transmisión activa…", true);
            ytTimer.start(10000);
            return;
        }

        for (const QJsonValue &value : items) {
            const QJsonObject obj = value.toObject();
            const QJsonObject snippet = obj.value("snippet").toObject();
            const QString chatId = snippet.value("liveChatId").toString();
            if (chatId.isEmpty())
                continue;

            ytVideoId = obj.value("id").toString();
            ytLiveChatId = chatId;
            const QString title = snippet.value("title").toString();
            emitStatus("youtube", title.isEmpty() ? "Conectado al chat activo." : "Conectado · " + title, true);
            pollYouTube();
            return;
        }

        emitStatus("youtube", "Directo detectado, esperando que el chat esté disponible…", true);
        ytTimer.start(10000);
    });
}

void SocialApiClient::resolveYouTubeLiveChat()
{
    if (!ytEnabled || ytVideoId.isEmpty())
        return;

    QUrl url("https://www.googleapis.com/youtube/v3/videos");
    QUrlQuery q;
    q.addQueryItem("part", "liveStreamingDetails,snippet");
    q.addQueryItem("id", ytVideoId);
    if (!ytOAuth)
        q.addQueryItem("key", ytApiKey);
    url.setQuery(q);

    QNetworkReply *reply = network.get(youtubeRequest(url));
    QObject::connect(reply, &QNetworkReply::finished, reply, [this, reply]() {
        const QByteArray bytes = reply->readAll();
        const auto error = reply->error();
        const int http = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        reply->deleteLater();
        if (!ytEnabled)
            return;

        if (error != QNetworkReply::NoError) {
            emitStatus("youtube", http == 401 ? "La sesión de YouTube caducó." : "No se pudo consultar el directo de YouTube.", false);
            return;
        }

        const QJsonObject root = QJsonDocument::fromJson(bytes).object();
        const QJsonArray items = root.value("items").toArray();
        if (items.isEmpty()) {
            emitStatus("youtube", "No encontré ese video en YouTube.", false);
            return;
        }

        const QJsonObject item = items.first().toObject();
        const QJsonObject details = item.value("liveStreamingDetails").toObject();
        ytLiveChatId = details.value("activeLiveChatId").toString();
        if (ytLiveChatId.isEmpty()) {
            emitStatus("youtube", "El video no tiene un chat en vivo activo.", false);
            return;
        }

        const QString title = item.value("snippet").toObject().value("title").toString();
        emitStatus("youtube", title.isEmpty() ? "Conectado al chat en vivo." : "Conectado · " + title, true);
        pollYouTube();
    });
}

void SocialApiClient::pollYouTube()
{
    if (!ytEnabled)
        return;
    if (ytLiveChatId.isEmpty()) {
        if (ytOAuth)
            discoverYouTubeLive();
        return;
    }

    QUrl url("https://www.googleapis.com/youtube/v3/liveChat/messages");
    QUrlQuery q;
    q.addQueryItem("part", "snippet,authorDetails");
    q.addQueryItem("liveChatId", ytLiveChatId);
    q.addQueryItem("maxResults", "200");
    q.addQueryItem("profileImageSize", "64");
    if (!ytOAuth)
        q.addQueryItem("key", ytApiKey);
    if (!ytPageToken.isEmpty())
        q.addQueryItem("pageToken", ytPageToken);
    url.setQuery(q);

    QNetworkReply *reply = network.get(youtubeRequest(url));
    QObject::connect(reply, &QNetworkReply::finished, reply, [this, reply]() {
        const QByteArray bytes = reply->readAll();
        const auto error = reply->error();
        const int http = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        reply->deleteLater();
        if (!ytEnabled)
            return;

        const QJsonObject root = QJsonDocument::fromJson(bytes).object();
        if (error != QNetworkReply::NoError || root.contains("error")) {
            if (http == 401)
                emitStatus("youtube", "La sesión OAuth de YouTube caducó. Reconecta la cuenta.", false);
            else
                emitStatus("youtube", "Error leyendo comentarios. Reintentaré.", false);
            ytTimer.start(5000);
            return;
        }

        const QJsonArray items = root.value("items").toArray();
        const bool firstBatch = !ytPrimed;

        for (const QJsonValue &value : items) {
            const QJsonObject obj = value.toObject();
            const QString id = obj.value("id").toString();
            if (id.isEmpty() || ytSeen.contains(id))
                continue;
            ytSeen.insert(id);

            const QJsonObject snippet = obj.value("snippet").toObject();
            const bool hasDisplay = snippet.value("hasDisplayContent").toBool(false);
            const QString type = snippet.value("type").toString();
            const bool supported = type == "textMessageEvent" || type == "superChatEvent" ||
                                   type == "memberMilestoneChatEvent" || type == "giftEvent";
            if (!hasDisplay || !supported)
                continue;

            if (firstBatch)
                continue; // Sin historial viejo al conectar.

            const QJsonObject authorDetails = obj.value("authorDetails").toObject();
            SocialComment c;
            c.id = id;
            c.platform = "youtube";
            c.author = authorDetails.value("displayName").toString("YouTube");
            c.message = snippet.value("displayMessage").toString();
            c.avatarUrl = authorDetails.value("profileImageUrl").toString();
            if (!c.message.isEmpty() && onComment)
                onComment(c);
        }

        ytPrimed = true;
        ytPageToken = root.value("nextPageToken").toString();
        int waitMs = root.value("pollingIntervalMillis").toInt(2500);
        waitMs = qBound(1000, waitMs, 15000);
        ytTimer.start(waitMs);
    });
}

void SocialApiClient::connectFacebook(const QString &pageAccessToken, const QString &liveVideoIdOrUrl)
{
    disconnectFacebook();
    fbToken = pageAccessToken.trimmed();
    fbVideoId = extractFacebookVideoId(liveVideoIdOrUrl);

    if (fbToken.isEmpty() || fbVideoId.isEmpty()) {
        emitStatus("facebook", "Falta token de Página o ID válido del Live.", false);
        return;
    }

    fbEnabled = true;
    emitStatus("facebook", "Conectando al Live…", true);
    pollFacebook();
    fbTimer.start();
}

void SocialApiClient::connectFacebookPage(const QString &pageAccessToken, const QString &pageId)
{
    disconnectFacebook();
    fbToken = pageAccessToken.trimmed();
    fbPageId = pageId.trimmed();
    if (fbToken.isEmpty() || fbPageId.isEmpty()) {
        emitStatus("facebook", "No hay Página/token para detectar el Live.", false);
        return;
    }

    fbEnabled = true;
    emitStatus("facebook", "Buscando Live activo…", true);
    discoverFacebookLive();
}

void SocialApiClient::disconnectFacebook()
{
    fbEnabled = false;
    fbPrimed = false;
    fbTimer.stop();
    fbToken.clear();
    fbPageId.clear();
    fbVideoId.clear();
    fbSeen.clear();
}

void SocialApiClient::discoverFacebookLive()
{
    if (!fbEnabled || fbPageId.isEmpty() || fbToken.isEmpty())
        return;

    QUrl url(QString("https://graph.facebook.com/%1/%2/live_videos").arg(kGraphVersion, fbPageId));
    QUrlQuery q;
    q.addQueryItem("fields", "id,title,status,creation_time,permalink_url");
    q.addQueryItem("broadcast_status", "LIVE");
    q.addQueryItem("limit", "25");
    q.addQueryItem("access_token", fbToken);
    url.setQuery(q);

    QNetworkReply *reply = network.get(QNetworkRequest(url));
    QObject::connect(reply, &QNetworkReply::finished, reply, [this, reply]() {
        const QByteArray bytes = reply->readAll();
        const auto error = reply->error();
        const QJsonObject root = QJsonDocument::fromJson(bytes).object();
        reply->deleteLater();
        if (!fbEnabled)
            return;

        if (error != QNetworkReply::NoError || root.contains("error")) {
            QString detail = root.value("error").toObject().value("message").toString();
            if (detail.isEmpty())
                detail = "No pude consultar el Live activo de Facebook.";
            emitStatus("facebook", detail, false);
            return;
        }

        const QJsonArray data = root.value("data").toArray();
        if (data.isEmpty()) {
            emitStatus("facebook", "No encontré un Live activo en esta Página.", false);
            return;
        }

        const QJsonObject selected = data.first().toObject();
        fbVideoId = selected.value("id").toString();
        const QString title = selected.value("title").toString();
        if (fbVideoId.isEmpty()) {
            emitStatus("facebook", "Facebook devolvió un Live sin ID utilizable.", false);
            return;
        }

        emitStatus("facebook", title.isEmpty() ? "Conectado al Live activo." : "Conectado · " + title, true);
        pollFacebook();
        fbTimer.start();
    });
}

void SocialApiClient::pollFacebook()
{
    if (!fbEnabled || fbVideoId.isEmpty())
        return;

    QUrl url(QString("https://graph.facebook.com/%1/%2/comments").arg(kGraphVersion, fbVideoId));
    QUrlQuery q;
    q.addQueryItem("fields", "id,message,created_time,from{name,id}");
    q.addQueryItem("filter", "stream");
    q.addQueryItem("live_filter", "no_filter");
    q.addQueryItem("order", "reverse_chronological");
    q.addQueryItem("limit", "100");
    q.addQueryItem("access_token", fbToken);
    url.setQuery(q);

    QNetworkReply *reply = network.get(QNetworkRequest(url));
    QObject::connect(reply, &QNetworkReply::finished, reply, [this, reply]() {
        const QByteArray bytes = reply->readAll();
        const auto error = reply->error();
        reply->deleteLater();
        if (!fbEnabled)
            return;

        const QJsonObject root = QJsonDocument::fromJson(bytes).object();
        if (error != QNetworkReply::NoError || root.contains("error")) {
            QString detail = root.value("error").toObject().value("message").toString();
            if (detail.isEmpty())
                detail = "No se pudieron leer comentarios.";
            emitStatus("facebook", detail, false);
            return;
        }

        const QJsonArray data = root.value("data").toArray();
        const bool firstBatch = !fbPrimed;
        QVector<SocialComment> incoming;

        for (const QJsonValue &value : data) {
            const QJsonObject obj = value.toObject();
            const QString id = obj.value("id").toString();
            if (id.isEmpty() || fbSeen.contains(id))
                continue;
            fbSeen.insert(id);

            if (firstBatch)
                continue;

            SocialComment c;
            c.id = id;
            c.platform = "facebook";
            c.author = obj.value("from").toObject().value("name").toString("Facebook");
            c.message = obj.value("message").toString();
            if (!c.message.isEmpty())
                incoming.push_back(c);
        }

        // API recomendada en reverse chronological; entregar a OBS en orden natural.
        for (auto it = incoming.crbegin(); it != incoming.crend(); ++it) {
            if (onComment)
                onComment(*it);
        }

        fbPrimed = true;
        emitStatus("facebook", "Conectado al Live.", true);
    });
}
