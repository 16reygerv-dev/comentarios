#pragma once

#include "comment-model.hpp"
#include "oauth-manager.hpp"
#include "overlay-server.hpp"
#include "social-api.hpp"

#include <QColor>
#include <QHash>
#include <QQueue>
#include <QTimer>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSlider;
class QSpinBox;

class SocialCommentsDock : public QWidget {
public:
    explicit SocialCommentsDock(QWidget *parent = nullptr);
    ~SocialCommentsDock() override;

private:
    void buildUi();
    void wireOAuth();
    void loadSettings();
    void saveSettings(bool includeSecrets = true);
    void restoreSessions();
    void configureOAuthFromUi();
    void logoutYouTubeLocal();
    void logoutFacebookLocal();

    void applyStyle();
    void applyPreset(const QString &presetKey);
    void markStyleCustom();
    void setStylePanelVisible(bool visible);
    void setConnectionAdvancedVisible(bool visible);
    void updateColorButton(QPushButton *button, const QColor &color);
    QColor chooseColor(const QColor &current, QPushButton *button);

    void handleFacebookPages(const QVector<OAuthFacebookPage> &pages);
    void handleFacebookLives(const QVector<OAuthFacebookLive> &lives);
    void useSelectedFacebookPage();
    void useSelectedFacebookLive();

    void onIncomingComment(const SocialComment &comment);
    void addCommentRow(const SocialComment &comment);
    void showNow(const SocialComment &comment, bool pinned);
    void enqueueAuto(const SocialComment &comment);
    void processAutoQueue();
    void clearOverlay(bool continueQueue = true);
    void addOverlayToCurrentScene();
    void previewTest();

    OverlayServer overlay;
    SocialApiClient api;
    OAuthManager oauth;
    QQueue<SocialComment> autoQueue;
    QTimer autoDisplayTimer;
    QTimer googleRefreshTimer;
    bool pinnedActive = false;
    bool suppressStyleSignals = false;
    bool suppressFacebookSelection = false;

    QString googleRefreshToken;
    QString facebookUserToken;
    QHash<QString, QString> facebookPageTokens;

    QPushButton *ytConnect = nullptr;
    QLabel *ytStatus = nullptr;
    QLineEdit *ytVideo = nullptr;

    QPushButton *fbConnect = nullptr;
    QLabel *fbStatus = nullptr;
    QComboBox *fbPage = nullptr;
    QComboBox *fbLive = nullptr;

    QPushButton *accountToggle = nullptr;
    QGroupBox *accountGroup = nullptr;
    QPushButton *ytLogout = nullptr;
    QPushButton *ytRevoke = nullptr;
    QPushButton *fbLogout = nullptr;
    QPushButton *fbRevoke = nullptr;

    QPushButton *connectionAdvancedToggle = nullptr;
    QGroupBox *connectionAdvancedGroup = nullptr;
    QLineEdit *googleClientId = nullptr;
    QLineEdit *googleClientSecret = nullptr;
    QLineEdit *facebookAppId = nullptr;
    QLineEdit *facebookAppSecret = nullptr;
    QLineEdit *facebookBrokerUrl = nullptr;

    QLineEdit *ytApiKey = nullptr;
    QPushButton *ytManualConnect = nullptr;
    QLineEdit *fbToken = nullptr;
    QLineEdit *fbVideo = nullptr;
    QPushButton *fbManualConnect = nullptr;

    QCheckBox *rememberCredentials = nullptr;
    QLabel *credentialStatus = nullptr;

    QComboBox *mode = nullptr;
    QListWidget *commentList = nullptr;

    QPushButton *styleToggle = nullptr;
    QGroupBox *styleGroup = nullptr;
    QComboBox *preset = nullptr;
    QComboBox *fontFamily = nullptr;
    QComboBox *sizePreset = nullptr;
    QPushButton *textColorButton = nullptr;
    QPushButton *nameColorButton = nullptr;
    QPushButton *backgroundColorButton = nullptr;
    QSlider *backgroundOpacity = nullptr;
    QSpinBox *duration = nullptr;
    QComboBox *animation = nullptr;
    QCheckBox *showAvatar = nullptr;
    QCheckBox *showLogo = nullptr;
    QLabel *serverStatus = nullptr;

    QColor textColor = QColor("#FFFFFF");
    QColor nameColor = QColor("#FFFFFF");
    QColor backgroundColor = QColor("#111318");
};
