#pragma once

#include "comment-model.hpp"

#include <QNetworkAccessManager>
#include <QSet>
#include <QTimer>
#include <functional>

class SocialApiClient : public QObject {
public:
    using CommentCallback = std::function<void(const SocialComment &)>;
    using StatusCallback = std::function<void(const QString &platform, const QString &message, bool ok)>;

    explicit SocialApiClient(QObject *parent = nullptr);

    void setCommentCallback(CommentCallback callback) { onComment = std::move(callback); }
    void setStatusCallback(StatusCallback callback) { onStatus = std::move(callback); }

    // v0.4 primary path: OAuth access token. If videoOrUrl is empty, the active
    // broadcast owned by the authenticated YouTube account is detected automatically.
    void connectYouTubeOAuth(const QString &accessToken, const QString &videoOrUrl = {});
    void updateYouTubeOAuthToken(const QString &accessToken);

    // v0.2 fallback path kept for troubleshooting/public streams.
    void connectYouTubeApiKey(const QString &apiKey, const QString &videoOrUrl);
    void disconnectYouTube();

    // If no Live ID is supplied, use connectFacebookPage() to auto-detect it.
    void connectFacebook(const QString &pageAccessToken, const QString &liveVideoIdOrUrl);
    void connectFacebookPage(const QString &pageAccessToken, const QString &pageId);
    void disconnectFacebook();

    bool youtubeConnected() const { return ytEnabled; }
    bool facebookConnected() const { return fbEnabled; }

private:
    QString extractYouTubeVideoId(const QString &input) const;
    QString extractFacebookVideoId(const QString &input) const;

    QNetworkRequest youtubeRequest(const QUrl &url) const;
    void discoverYouTubeLive();
    void resolveYouTubeLiveChat();
    void pollYouTube();

    void discoverFacebookLive();
    void pollFacebook();
    void emitStatus(const QString &platform, const QString &message, bool ok);

    QNetworkAccessManager network;
    QTimer ytTimer;
    QTimer fbTimer;

    CommentCallback onComment;
    StatusCallback onStatus;

    bool ytEnabled = false;
    bool ytPrimed = false;
    bool ytOAuth = false;
    QString ytApiKey;
    QString ytAccessToken;
    QString ytVideoId;
    QString ytLiveChatId;
    QString ytPageToken;
    QSet<QString> ytSeen;

    bool fbEnabled = false;
    bool fbPrimed = false;
    QString fbToken;
    QString fbPageId;
    QString fbVideoId;
    QSet<QString> fbSeen;
};
