#pragma once

#include <QList>
#include <QNetworkAccessManager>
#include <QPair>
#include <QString>
#include <QTcpServer>
#include <QVector>
#include <functional>

struct OAuthFacebookPage {
    QString id;
    QString name;
    QString accessToken;
};

struct OAuthFacebookLive {
    QString id;
    QString title;
    QString status;
    QString permalink;
};

class QTcpSocket;

class OAuthManager : public QObject {
public:
    enum class Flow { None, Google, Facebook };

    using StatusCallback = std::function<void(const QString &platform, const QString &message, bool ok)>;
    using GoogleTokenCallback = std::function<void(const QString &accessToken,
                                                   const QString &refreshToken,
                                                   int expiresInSeconds)>;
    using FacebookTokenCallback = std::function<void(const QString &userAccessToken)>;
    using FacebookPagesCallback = std::function<void(const QVector<OAuthFacebookPage> &pages)>;
    using FacebookLivesCallback = std::function<void(const QVector<OAuthFacebookLive> &lives)>;
    using RevokeCallback = std::function<void(const QString &platform, bool ok, const QString &message)>;

    explicit OAuthManager(QObject *parent = nullptr);
    ~OAuthManager() override;

    void setStatusCallback(StatusCallback callback) { onStatus = std::move(callback); }
    void setGoogleTokenCallback(GoogleTokenCallback callback) { onGoogleToken = std::move(callback); }
    void setFacebookTokenCallback(FacebookTokenCallback callback) { onFacebookToken = std::move(callback); }
    void setFacebookPagesCallback(FacebookPagesCallback callback) { onFacebookPages = std::move(callback); }
    void setFacebookLivesCallback(FacebookLivesCallback callback) { onFacebookLives = std::move(callback); }
    void setRevokeCallback(RevokeCallback callback) { onRevoke = std::move(callback); }

    void configureGoogle(const QString &clientId, const QString &clientSecret = {});
    void configureFacebook(const QString &appId, const QString &appSecret, const QString &brokerBaseUrl = {});

    bool startGoogleLogin();
    bool startFacebookLogin();
    void refreshGoogleAccessToken(const QString &refreshToken);

    void fetchFacebookPages(const QString &userAccessToken);
    void fetchFacebookLives(const QString &pageId, const QString &pageAccessToken);

    void revokeGoogleToken(const QString &token);
    void revokeFacebookPermissions(const QString &userAccessToken);

    void cancel();

    QString googleRedirectUri() const { return googleRedirect; }
    QString facebookRedirectUri() const { return facebookRedirect; }
    bool usingFacebookBroker() const { return !facebookBrokerUrl.isEmpty(); }

private:
    bool startLoopbackServer(Flow requestedFlow);
    void handleNewConnection();
    void exchangeGoogleCode(const QString &code);
    void exchangeFacebookCode(const QString &code);
    void exchangeFacebookLongLivedToken(const QString &shortToken);
    void exchangeFacebookBrokerCode(const QString &brokerCode, const QString &returnedState);
    void sendBrowserResponse(QTcpSocket *socket, bool ok, const QString &message);
    void emitStatus(const QString &platform, const QString &message, bool ok);

    static QString randomUrlSafe(int byteCount);
    static QString pkceChallenge(const QString &verifier);
    static QByteArray formBody(const QList<QPair<QString, QString>> &items);
    static QString normalizedBaseUrl(const QString &value);

    QNetworkAccessManager network;
    QTcpServer loopback;
    Flow flow = Flow::None;

    QString state;
    QString codeVerifier;
    QString googleRedirect;
    QString facebookRedirect;

    QString googleClientId;
    QString googleClientSecret;
    QString facebookAppId;
    QString facebookAppSecret;
    QString facebookBrokerUrl;
    bool activeFacebookBrokerFlow = false;

    StatusCallback onStatus;
    GoogleTokenCallback onGoogleToken;
    FacebookTokenCallback onFacebookToken;
    FacebookPagesCallback onFacebookPages;
    FacebookLivesCallback onFacebookLives;
    RevokeCallback onRevoke;
};
