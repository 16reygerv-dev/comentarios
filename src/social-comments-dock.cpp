#include "social-comments-dock.hpp"
#include "secret-store.hpp"

#include <obs.h>
#include <obs-frontend-api.h>

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>

#ifndef SOCIAL_COMMENTS_GOOGLE_CLIENT_ID
#define SOCIAL_COMMENTS_GOOGLE_CLIENT_ID ""
#endif
#ifndef SOCIAL_COMMENTS_FACEBOOK_BROKER_URL
#define SOCIAL_COMMENTS_FACEBOOK_BROKER_URL ""
#endif


namespace {
QString badgeFor(const QString &platform)
{
    return platform == "facebook" ? "Facebook" : "YouTube";
}

QString dotColor(const QString &platform)
{
    return platform == "facebook" ? "#1877F2" : "#FF0033";
}

QString preferredFont(const QComboBox *combo)
{
    if (!combo)
        return QStringLiteral("Arial");
    const int inter = combo->findText("Inter", Qt::MatchFixedString);
    if (inter >= 0)
        return combo->itemText(inter);
    const int segoe = combo->findText("Segoe UI", Qt::MatchFixedString);
    if (segoe >= 0)
        return combo->itemText(segoe);
    return combo->currentText();
}

QString compiledGoogleClientId()
{
    return QString::fromUtf8(SOCIAL_COMMENTS_GOOGLE_CLIENT_ID).trimmed();
}

QString compiledFacebookBrokerUrl()
{
    return QString::fromUtf8(SOCIAL_COMMENTS_FACEBOOK_BROKER_URL).trimmed();
}
}

SocialCommentsDock::SocialCommentsDock(QWidget *parent)
    : QWidget(parent), api(this), oauth(this), overlay(this)
{
    setObjectName("SocialCommentsDockWidget");
    buildUi();
    wireOAuth();
    loadSettings();

    const bool serverOk = overlay.start();
    serverStatus->setText(serverOk ? QString("Overlay listo · %1").arg(overlay.overlayUrl())
                                   : "No se pudo iniciar el overlay local");
    serverStatus->setStyleSheet(serverOk ? "color:#7bd88f" : "color:#ff6b6b");

    api.setCommentCallback([this](const SocialComment &comment) { onIncomingComment(comment); });
    api.setStatusCallback([this](const QString &platform, const QString &message, bool ok) {
        QLabel *label = platform == "youtube" ? ytStatus : fbStatus;
        label->setText(message);
        label->setStyleSheet(ok ? "color:#7bd88f" : "color:#ff8c8c");
        if (platform == "youtube" && api.youtubeConnected())
            ytConnect->setText("Desconectar YouTube");
        if (platform == "facebook" && api.facebookConnected())
            fbConnect->setText("Desconectar Facebook");
    });

    autoDisplayTimer.setSingleShot(true);
    QObject::connect(&autoDisplayTimer, &QTimer::timeout, this, [this]() {
        if (!pinnedActive)
            clearOverlay(true);
    });

    googleRefreshTimer.setSingleShot(true);
    QObject::connect(&googleRefreshTimer, &QTimer::timeout, this, [this]() {
        if (!googleRefreshToken.isEmpty()) {
            configureOAuthFromUi();
            oauth.refreshGoogleAccessToken(googleRefreshToken);
        }
    });

    applyStyle();
    restoreSessions();
}

SocialCommentsDock::~SocialCommentsDock()
{
    saveSettings(true);
    oauth.cancel();
    overlay.stop();
}

void SocialCommentsDock::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(10);

    auto *title = new QLabel("Social Comments · v0.4");
    QFont titleFont = title->font();
    titleFont.setPointSize(titleFont.pointSize() + 3);
    titleFont.setBold(true);
    title->setFont(titleFont);
    root->addWidget(title);

    auto *subtitle = new QLabel("Facebook + YouTube · conecta, elige comentario y muestra. Sin configuración diaria.");
    subtitle->setStyleSheet("color:#9aa0aa");
    subtitle->setWordWrap(true);
    root->addWidget(subtitle);

    auto *connections = new QGroupBox("Conexiones");
    auto *connectionsLayout = new QVBoxLayout(connections);
    connectionsLayout->setContentsMargins(10, 10, 10, 10);
    connectionsLayout->setSpacing(10);

    auto *ytTitle = new QLabel("● YouTube");
    ytTitle->setStyleSheet("font-weight:700;color:#FF0033");
    connectionsLayout->addWidget(ytTitle);

    auto *ytMain = new QHBoxLayout();
    ytConnect = new QPushButton("Conectar YouTube");
    ytMain->addWidget(ytConnect, 1);
    connectionsLayout->addLayout(ytMain);
    ytStatus = new QLabel("Desconectado · detecta automáticamente tu directo activo");
    ytStatus->setWordWrap(true);
    connectionsLayout->addWidget(ytStatus);

    auto *fbTitle = new QLabel("● Facebook");
    fbTitle->setStyleSheet("font-weight:700;color:#1877F2;margin-top:4px");
    connectionsLayout->addWidget(fbTitle);

    auto *fbMain = new QHBoxLayout();
    fbConnect = new QPushButton("Conectar Facebook");
    fbMain->addWidget(fbConnect, 1);
    connectionsLayout->addLayout(fbMain);

    fbPage = new QComboBox();
    fbPage->setPlaceholderText("Página de Facebook");
    fbPage->setEnabled(false);
    connectionsLayout->addWidget(fbPage);

    fbLive = new QComboBox();
    fbLive->setPlaceholderText("Live activo");
    fbLive->setEnabled(false);
    connectionsLayout->addWidget(fbLive);

    fbStatus = new QLabel("Desconectado · autoriza y elige tu Página");
    fbStatus->setWordWrap(true);
    connectionsLayout->addWidget(fbStatus);

    accountToggle = new QPushButton("Cuenta y privacidad ▾");
    accountToggle->setCheckable(true);
    connectionsLayout->addWidget(accountToggle);

    accountGroup = new QGroupBox("Sesiones");
    auto *accountLayout = new QFormLayout(accountGroup);
    auto *ytAccountActions = new QHBoxLayout();
    ytLogout = new QPushButton("Cerrar sesión");
    ytRevoke = new QPushButton("Revocar acceso");
    ytAccountActions->addWidget(ytLogout);
    ytAccountActions->addWidget(ytRevoke);
    auto *fbAccountActions = new QHBoxLayout();
    fbLogout = new QPushButton("Cerrar sesión");
    fbRevoke = new QPushButton("Revocar acceso");
    fbAccountActions->addWidget(fbLogout);
    fbAccountActions->addWidget(fbRevoke);
    accountLayout->addRow("YouTube", ytAccountActions);
    accountLayout->addRow("Facebook", fbAccountActions);
    auto *privacyNote = new QLabel("Cerrar sesión borra la sesión de este PC. Revocar acceso también retira el permiso en el proveedor.");
    privacyNote->setWordWrap(true);
    privacyNote->setStyleSheet("font-size:11px;color:#8b919a");
    accountLayout->addRow(privacyNote);
    accountGroup->setVisible(false);
    connectionsLayout->addWidget(accountGroup);

    connectionAdvancedToggle = new QPushButton("Configuración avanzada ▾");
    connectionAdvancedToggle->setCheckable(true);
    connectionsLayout->addWidget(connectionAdvancedToggle);

    connectionAdvancedGroup = new QGroupBox("OAuth / modo técnico");
    auto *advanced = new QVBoxLayout(connectionAdvancedGroup);

    auto *oauthNote = new QLabel(
        "En builds comerciales, Google Client ID y la URL del broker pueden venir preconfigurados. "
        "El usuario final solo pulsa Conectar. App Secret de Meta se conserva únicamente para el modo técnico directo.");
    oauthNote->setWordWrap(true);
    oauthNote->setStyleSheet("font-size:11px;color:#8b919a");
    advanced->addWidget(oauthNote);

    auto *oauthForm = new QFormLayout();
    googleClientId = new QLineEdit();
    googleClientId->setPlaceholderText("Client ID de app de escritorio");
    googleClientSecret = new QLineEdit();
    googleClientSecret->setEchoMode(QLineEdit::Password);
    googleClientSecret->setPlaceholderText("Client Secret (si Google lo emitió)");
    facebookAppId = new QLineEdit();
    facebookAppId->setPlaceholderText("Meta App ID");
    facebookAppSecret = new QLineEdit();
    facebookAppSecret->setEchoMode(QLineEdit::Password);
    facebookAppSecret->setPlaceholderText("Meta App Secret (solo modo técnico)");
    facebookBrokerUrl = new QLineEdit();
    facebookBrokerUrl->setPlaceholderText("https://oauth.tudominio.com");
    oauthForm->addRow("Google Client ID", googleClientId);
    oauthForm->addRow("Google Secret", googleClientSecret);
    oauthForm->addRow("Meta Broker URL", facebookBrokerUrl);
    oauthForm->addRow("Meta App ID", facebookAppId);
    oauthForm->addRow("Meta App Secret", facebookAppSecret);
    advanced->addLayout(oauthForm);

    auto *redirectInfo = new QLabel(
        "Callback local del plugin: http://127.0.0.1:18765/oauth/callback/facebook\n"
        "Con broker, Meta vuelve al HTTPS del broker y este entrega un código de un solo uso al callback local.");
    redirectInfo->setTextInteractionFlags(Qt::TextSelectableByMouse);
    redirectInfo->setWordWrap(true);
    redirectInfo->setStyleSheet("font-size:11px;color:#9aa0aa");
    advanced->addWidget(redirectInfo);

    auto *fallback = new QGroupBox("Respaldo manual");
    auto *fallbackForm = new QFormLayout(fallback);
    ytVideo = new QLineEdit();
    ytVideo->setPlaceholderText("Opcional: URL/ID de YouTube; vacío = detectar activo");
    ytApiKey = new QLineEdit();
    ytApiKey->setEchoMode(QLineEdit::Password);
    ytApiKey->setPlaceholderText("API key de YouTube (solo respaldo)");
    ytManualConnect = new QPushButton("Conectar YouTube con API key");
    fbToken = new QLineEdit();
    fbToken->setEchoMode(QLineEdit::Password);
    fbToken->setPlaceholderText("Page Access Token");
    fbVideo = new QLineEdit();
    fbVideo->setPlaceholderText("ID o URL del Facebook Live");
    fbManualConnect = new QPushButton("Conectar Facebook manualmente");
    fallbackForm->addRow("YouTube directo", ytVideo);
    fallbackForm->addRow("YouTube API key", ytApiKey);
    fallbackForm->addRow(ytManualConnect);
    fallbackForm->addRow("Facebook token", fbToken);
    fallbackForm->addRow("Facebook Live", fbVideo);
    fallbackForm->addRow(fbManualConnect);
    advanced->addWidget(fallback);

    rememberCredentials = new QCheckBox("Recordar sesión y credenciales de forma segura en Windows");
    rememberCredentials->setChecked(SecretStore::available());
    rememberCredentials->setEnabled(SecretStore::available());
    advanced->addWidget(rememberCredentials);

    credentialStatus = new QLabel();
    credentialStatus->setWordWrap(true);
    credentialStatus->setStyleSheet("font-size:11px;color:#8b919a");
    credentialStatus->setText(SecretStore::available()
                                  ? "Tokens y secretos se guardan en Windows Credential Manager."
                                  : "El guardado seguro de credenciales está disponible en Windows.");
    advanced->addWidget(credentialStatus);

    connectionsLayout->addWidget(connectionAdvancedGroup);
    root->addWidget(connections);
    setConnectionAdvancedVisible(false);

    auto *modeRow = new QHBoxLayout();
    modeRow->addWidget(new QLabel("Modo"));
    mode = new QComboBox();
    mode->addItem("Manual · tú eliges qué sale", "manual");
    mode->addItem("Automático · salen por orden", "automatic");
    modeRow->addWidget(mode, 1);
    root->addLayout(modeRow);

    commentList = new QListWidget();
    commentList->setMinimumHeight(210);
    commentList->setAlternatingRowColors(false);
    root->addWidget(commentList, 1);

    auto *actions = new QHBoxLayout();
    auto *addOverlay = new QPushButton("＋ Añadir overlay");
    auto *preview = new QPushButton("Vista previa");
    auto *hide = new QPushButton("Ocultar");
    addOverlay->setToolTip("Crea o reutiliza la Browser Source en la escena actual");
    actions->addWidget(addOverlay, 1);
    actions->addWidget(preview);
    actions->addWidget(hide);
    root->addLayout(actions);

    styleToggle = new QPushButton("Ajustes visuales ▾");
    styleToggle->setCheckable(true);
    styleToggle->setChecked(false);
    root->addWidget(styleToggle);

    styleGroup = new QGroupBox("Estilo del comentario");
    auto *styleLayout = new QFormLayout(styleGroup);

    preset = new QComboBox();
    preset->addItem("Dark Clean", "dark");
    preset->addItem("Minimal", "minimal");
    preset->addItem("Light Clean", "light");
    preset->addItem("Personalizado", "custom");

    fontFamily = new QComboBox();
    const QStringList fonts = QFontDatabase::families();
    for (const QString &preferred : {QString("Inter"), QString("Segoe UI"), QString("Poppins"),
                                     QString("Montserrat"), QString("Roboto"), QString("Arial")}) {
        if (fonts.contains(preferred, Qt::CaseInsensitive) &&
            fontFamily->findText(preferred, Qt::MatchFixedString) < 0)
            fontFamily->addItem(preferred);
    }
    if (fontFamily->count() == 0)
        fontFamily->addItem("Arial");

    sizePreset = new QComboBox();
    sizePreset->addItem("Compacto", "compact");
    sizePreset->addItem("Normal", "normal");
    sizePreset->addItem("Grande", "large");
    sizePreset->setCurrentIndex(1);

    textColorButton = new QPushButton();
    nameColorButton = new QPushButton();
    backgroundColorButton = new QPushButton();

    backgroundOpacity = new QSlider(Qt::Horizontal);
    backgroundOpacity->setRange(0, 100);
    backgroundOpacity->setValue(90);

    duration = new QSpinBox();
    duration->setRange(2, 30);
    duration->setValue(8);
    duration->setSuffix(" s");

    animation = new QComboBox();
    animation->addItem("Fade", "fade");
    animation->addItem("Slide", "slide");
    animation->addItem("Sin animación", "none");

    showAvatar = new QCheckBox("Mostrar foto / iniciales");
    showAvatar->setChecked(true);
    showLogo = new QCheckBox("Mostrar logo de plataforma");
    showLogo->setChecked(true);

    styleLayout->addRow("Preset", preset);
    styleLayout->addRow("Tipografía", fontFamily);
    styleLayout->addRow("Tamaño", sizePreset);
    styleLayout->addRow("Texto", textColorButton);
    styleLayout->addRow("Nombre", nameColorButton);
    styleLayout->addRow("Fondo", backgroundColorButton);
    styleLayout->addRow("Opacidad", backgroundOpacity);
    styleLayout->addRow("Duración", duration);
    styleLayout->addRow("Animación", animation);
    styleLayout->addRow(showAvatar);
    styleLayout->addRow(showLogo);
    root->addWidget(styleGroup);
    setStylePanelVisible(false);

    serverStatus = new QLabel();
    serverStatus->setWordWrap(true);
    serverStatus->setStyleSheet("font-size:11px;color:#8b919a");
    root->addWidget(serverStatus);

    QObject::connect(ytConnect, &QPushButton::clicked, this, [this]() {
        if (api.youtubeConnected()) {
            api.disconnectYouTube();
            googleRefreshTimer.stop();
            ytConnect->setText("Conectar YouTube");
            ytStatus->setText("Desconectado");
            ytStatus->setStyleSheet(QString());
            return;
        }
        configureOAuthFromUi();
        if (googleClientId->text().trimmed().isEmpty()) {
            setConnectionAdvancedVisible(true);
            ytStatus->setText("Configura Google Client ID una sola vez y vuelve a pulsar Conectar.");
            ytStatus->setStyleSheet("color:#ffcc66");
            return;
        }
        oauth.startGoogleLogin();
        saveSettings(true);
    });

    QObject::connect(fbConnect, &QPushButton::clicked, this, [this]() {
        if (api.facebookConnected()) {
            api.disconnectFacebook();
            fbConnect->setText("Conectar Facebook");
            fbStatus->setText("Desconectado");
            fbStatus->setStyleSheet(QString());
            return;
        }
        configureOAuthFromUi();
        if (fbPage->count() > 0) {
            useSelectedFacebookPage();
        } else if (facebookBrokerUrl->text().trimmed().isEmpty() &&
                   (facebookAppId->text().trimmed().isEmpty() || facebookAppSecret->text().isEmpty())) {
            setConnectionAdvancedVisible(true);
            fbStatus->setText("Configura la URL del broker o Meta App ID + App Secret en modo técnico.");
            fbStatus->setStyleSheet("color:#ffcc66");
        } else {
            // Si la sesión guardada caducó, este botón siempre permite reautorizar.
            oauth.startFacebookLogin();
        }
        saveSettings(true);
    });

    QObject::connect(fbPage, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
        if (!suppressFacebookSelection)
            useSelectedFacebookPage();
    });
    QObject::connect(fbLive, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
        if (!suppressFacebookSelection)
            useSelectedFacebookLive();
    });

    QObject::connect(accountToggle, &QPushButton::toggled, this, [this](bool visible) {
        accountGroup->setVisible(visible);
        accountToggle->setText(visible ? "Cuenta y privacidad ▴" : "Cuenta y privacidad ▾");
    });
    QObject::connect(ytLogout, &QPushButton::clicked, this, [this]() { logoutYouTubeLocal(); });
    QObject::connect(fbLogout, &QPushButton::clicked, this, [this]() { logoutFacebookLocal(); });
    QObject::connect(ytRevoke, &QPushButton::clicked, this, [this]() {
        if (googleRefreshToken.isEmpty()) {
            ytStatus->setText("No hay token guardado para revocar; se cerrará la sesión local.");
            logoutYouTubeLocal();
            return;
        }
        ytStatus->setText("Revocando acceso de Google…");
        oauth.revokeGoogleToken(googleRefreshToken);
    });
    QObject::connect(fbRevoke, &QPushButton::clicked, this, [this]() {
        if (facebookUserToken.isEmpty()) {
            fbStatus->setText("No hay token guardado para revocar; se cerrará la sesión local.");
            logoutFacebookLocal();
            return;
        }
        fbStatus->setText("Revocando permisos de Facebook…");
        oauth.revokeFacebookPermissions(facebookUserToken);
    });

    QObject::connect(connectionAdvancedToggle, &QPushButton::toggled, this,
                     [this](bool visible) { setConnectionAdvancedVisible(visible); });
    QObject::connect(rememberCredentials, &QCheckBox::toggled, this, [this](bool) { saveSettings(true); });

    QObject::connect(ytManualConnect, &QPushButton::clicked, this, [this]() {
        api.connectYouTubeApiKey(ytApiKey->text(), ytVideo->text());
        if (api.youtubeConnected())
            ytConnect->setText("Desconectar YouTube");
        saveSettings(true);
    });
    QObject::connect(fbManualConnect, &QPushButton::clicked, this, [this]() {
        api.connectFacebook(fbToken->text(), fbVideo->text());
        if (api.facebookConnected())
            fbConnect->setText("Desconectar Facebook");
        saveSettings(true);
    });

    QObject::connect(styleToggle, &QPushButton::toggled, this, [this](bool visible) { setStylePanelVisible(visible); });
    QObject::connect(preset, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
        if (!suppressStyleSignals)
            applyPreset(preset->currentData().toString());
    });

    QObject::connect(textColorButton, &QPushButton::clicked, this, [this]() {
        const QColor selected = chooseColor(textColor, textColorButton);
        if (selected != textColor) {
            textColor = selected;
            markStyleCustom();
            applyStyle();
        }
    });
    QObject::connect(nameColorButton, &QPushButton::clicked, this, [this]() {
        const QColor selected = chooseColor(nameColor, nameColorButton);
        if (selected != nameColor) {
            nameColor = selected;
            markStyleCustom();
            applyStyle();
        }
    });
    QObject::connect(backgroundColorButton, &QPushButton::clicked, this, [this]() {
        const QColor selected = chooseColor(backgroundColor, backgroundColorButton);
        if (selected != backgroundColor) {
            backgroundColor = selected;
            markStyleCustom();
            applyStyle();
        }
    });

    QObject::connect(fontFamily, &QComboBox::currentTextChanged, this, [this](const QString &) {
        if (!suppressStyleSignals) { markStyleCustom(); applyStyle(); }
    });
    QObject::connect(sizePreset, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
        if (!suppressStyleSignals) { markStyleCustom(); applyStyle(); }
    });
    QObject::connect(backgroundOpacity, &QSlider::valueChanged, this, [this](int) {
        if (!suppressStyleSignals) { markStyleCustom(); applyStyle(); }
    });
    QObject::connect(duration, qOverload<int>(&QSpinBox::valueChanged), this, [this](int) {
        if (!suppressStyleSignals) { markStyleCustom(); applyStyle(); }
    });
    QObject::connect(animation, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
        if (!suppressStyleSignals) { markStyleCustom(); applyStyle(); }
    });
    QObject::connect(showAvatar, &QCheckBox::toggled, this, [this](bool) {
        if (!suppressStyleSignals) { markStyleCustom(); applyStyle(); }
    });
    QObject::connect(showLogo, &QCheckBox::toggled, this, [this](bool) {
        if (!suppressStyleSignals) { markStyleCustom(); applyStyle(); }
    });

    QObject::connect(mode, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
        if (mode->currentData().toString() == "manual")
            autoQueue.clear();
        saveSettings(false);
    });
    QObject::connect(addOverlay, &QPushButton::clicked, this, [this]() { addOverlayToCurrentScene(); });
    QObject::connect(preview, &QPushButton::clicked, this, [this]() { previewTest(); });
    QObject::connect(hide, &QPushButton::clicked, this, [this]() { clearOverlay(false); });
}

void SocialCommentsDock::wireOAuth()
{
    oauth.setStatusCallback([this](const QString &platform, const QString &message, bool ok) {
        QLabel *label = platform == "youtube" ? ytStatus : fbStatus;
        label->setText(message);
        label->setStyleSheet(ok ? "color:#7bd88f" : "color:#ff8c8c");
    });

    oauth.setGoogleTokenCallback([this](const QString &accessToken, const QString &refreshToken, int expiresInSeconds) {
        if (!refreshToken.isEmpty())
            googleRefreshToken = refreshToken;
        if (rememberCredentials->isChecked() && SecretStore::available() && !googleRefreshToken.isEmpty())
            SecretStore::save("google-refresh-token", googleRefreshToken);

        if (api.youtubeConnected())
            api.updateYouTubeOAuthToken(accessToken);
        else
            api.connectYouTubeOAuth(accessToken, ytVideo->text());
        ytConnect->setText("Desconectar YouTube");

        const int refreshMs = qMax(60, expiresInSeconds - 300) * 1000;
        googleRefreshTimer.start(refreshMs);
    });

    oauth.setFacebookTokenCallback([this](const QString &token) {
        facebookUserToken = token;
        if (rememberCredentials->isChecked() && SecretStore::available())
            SecretStore::save("facebook-user-token", facebookUserToken);
    });

    oauth.setFacebookPagesCallback([this](const QVector<OAuthFacebookPage> &pages) { handleFacebookPages(pages); });
    oauth.setFacebookLivesCallback([this](const QVector<OAuthFacebookLive> &lives) { handleFacebookLives(lives); });
    oauth.setRevokeCallback([this](const QString &platform, bool ok, const QString &message) {
        if (platform == "youtube") {
            if (ok)
                logoutYouTubeLocal();
            ytStatus->setText(message);
            ytStatus->setStyleSheet(ok ? "color:#7bd88f" : "color:#ff8c8c");
        } else if (platform == "facebook") {
            if (ok)
                logoutFacebookLocal();
            fbStatus->setText(message);
            fbStatus->setStyleSheet(ok ? "color:#7bd88f" : "color:#ff8c8c");
        }
    });
}

void SocialCommentsDock::configureOAuthFromUi()
{
    oauth.configureGoogle(googleClientId->text(), googleClientSecret->text());
    oauth.configureFacebook(facebookAppId->text(), facebookAppSecret->text(), facebookBrokerUrl->text());
}

void SocialCommentsDock::logoutYouTubeLocal()
{
    api.disconnectYouTube();
    googleRefreshTimer.stop();
    googleRefreshToken.clear();
    SecretStore::remove("google-refresh-token");
    ytConnect->setText("Conectar YouTube");
    ytStatus->setText("Sesión local de YouTube cerrada.");
    ytStatus->setStyleSheet(QString());
}

void SocialCommentsDock::logoutFacebookLocal()
{
    api.disconnectFacebook();
    facebookUserToken.clear();
    facebookPageTokens.clear();
    SecretStore::remove("facebook-user-token");
    suppressFacebookSelection = true;
    fbPage->clear();
    fbLive->clear();
    fbPage->setEnabled(false);
    fbLive->setEnabled(false);
    suppressFacebookSelection = false;
    QSettings settings("SocialCommentsOBS", "SocialComments");
    settings.remove("facebook/pageId");
    settings.remove("facebook/liveId");
    fbConnect->setText("Conectar Facebook");
    fbStatus->setText("Sesión local de Facebook cerrada.");
    fbStatus->setStyleSheet(QString());
}

void SocialCommentsDock::restoreSessions()
{
    configureOAuthFromUi();
    if (!rememberCredentials->isChecked() || !SecretStore::available())
        return;

    googleRefreshToken = SecretStore::load("google-refresh-token");
    facebookUserToken = SecretStore::load("facebook-user-token");

    if (!googleRefreshToken.isEmpty() && !googleClientId->text().trimmed().isEmpty()) {
        ytStatus->setText("Restaurando sesión de YouTube…");
        oauth.refreshGoogleAccessToken(googleRefreshToken);
    }
    if (!facebookUserToken.isEmpty()) {
        fbStatus->setText("Restaurando sesión de Facebook…");
        oauth.fetchFacebookPages(facebookUserToken);
    }
}

void SocialCommentsDock::handleFacebookPages(const QVector<OAuthFacebookPage> &pages)
{
    suppressFacebookSelection = true;
    facebookPageTokens.clear();
    fbPage->clear();
    QSettings settings("SocialCommentsOBS", "SocialComments");
    const QString preferredPage = settings.value("facebook/pageId").toString();
    int preferredIndex = -1;

    for (const OAuthFacebookPage &page : pages) {
        facebookPageTokens.insert(page.id, page.accessToken);
        fbPage->addItem(page.name.isEmpty() ? page.id : page.name, page.id);
        if (page.id == preferredPage)
            preferredIndex = fbPage->count() - 1;
    }
    fbPage->setEnabled(fbPage->count() > 0);
    if (preferredIndex >= 0)
        fbPage->setCurrentIndex(preferredIndex);
    else if (fbPage->count() > 0)
        fbPage->setCurrentIndex(0);
    suppressFacebookSelection = false;

    if (fbPage->count() > 0)
        useSelectedFacebookPage();
}

void SocialCommentsDock::handleFacebookLives(const QVector<OAuthFacebookLive> &lives)
{
    suppressFacebookSelection = true;
    fbLive->clear();
    QSettings settings("SocialCommentsOBS", "SocialComments");
    const QString preferredLive = settings.value("facebook/liveId").toString();
    int preferredIndex = -1;

    for (const OAuthFacebookLive &live : lives) {
        QString label = live.title.trimmed();
        if (label.isEmpty())
            label = QString("Live %1").arg(live.id);
        fbLive->addItem(label, live.id);
        if (live.id == preferredLive)
            preferredIndex = fbLive->count() - 1;
    }
    fbLive->setEnabled(fbLive->count() > 0);
    if (preferredIndex >= 0)
        fbLive->setCurrentIndex(preferredIndex);
    else if (fbLive->count() > 0)
        fbLive->setCurrentIndex(0);
    suppressFacebookSelection = false;

    if (fbLive->count() > 0)
        useSelectedFacebookLive();
}

void SocialCommentsDock::useSelectedFacebookPage()
{
    const QString pageId = fbPage->currentData().toString();
    const QString token = facebookPageTokens.value(pageId);
    if (pageId.isEmpty() || token.isEmpty())
        return;

    QSettings settings("SocialCommentsOBS", "SocialComments");
    settings.setValue("facebook/pageId", pageId);
    fbLive->clear();
    fbLive->setEnabled(false);
    configureOAuthFromUi();
    oauth.fetchFacebookLives(pageId, token);
}

void SocialCommentsDock::useSelectedFacebookLive()
{
    const QString pageId = fbPage->currentData().toString();
    const QString token = facebookPageTokens.value(pageId);
    const QString liveId = fbLive->currentData().toString();
    if (token.isEmpty() || liveId.isEmpty())
        return;

    QSettings settings("SocialCommentsOBS", "SocialComments");
    settings.setValue("facebook/liveId", liveId);
    api.connectFacebook(token, liveId);
    fbConnect->setText("Desconectar Facebook");
}

void SocialCommentsDock::loadSettings()
{
    QSettings s("SocialCommentsOBS", "SocialComments");
    suppressStyleSignals = true;

    ytVideo->setText(s.value("youtube/video").toString());
    fbVideo->setText(s.value("facebook/video").toString());
    QString googleId = s.value("oauth/googleClientId").toString();
    if (googleId.trimmed().isEmpty())
        googleId = compiledGoogleClientId();
    googleClientId->setText(googleId);

    QString brokerUrl = s.value("oauth/facebookBrokerUrl").toString();
    if (brokerUrl.trimmed().isEmpty())
        brokerUrl = compiledFacebookBrokerUrl();
    facebookBrokerUrl->setText(brokerUrl);
    facebookAppId->setText(s.value("oauth/facebookAppId").toString());

    const bool remember = SecretStore::available() && s.value("security/rememberCredentials", true).toBool();
    rememberCredentials->setChecked(remember);
    if (remember) {
        googleClientSecret->setText(SecretStore::load("google-client-secret"));
        facebookAppSecret->setText(SecretStore::load("facebook-app-secret"));
        ytApiKey->setText(SecretStore::load("youtube-api-key"));
        fbToken->setText(SecretStore::load("facebook-page-token-manual"));
    }

    const QString modeValue = s.value("ui/mode", "manual").toString();
    mode->setCurrentIndex(qMax(0, mode->findData(modeValue)));

    const QString presetValue = s.value("style/preset", "dark").toString();
    preset->setCurrentIndex(qMax(0, preset->findData(presetValue)));

    const QString font = s.value("style/font", preferredFont(fontFamily)).toString();
    const int fontIndex = fontFamily->findText(font, Qt::MatchFixedString);
    if (fontIndex >= 0)
        fontFamily->setCurrentIndex(fontIndex);

    textColor = QColor(s.value("style/textColor", "#FFFFFF").toString());
    nameColor = QColor(s.value("style/nameColor", "#FFFFFF").toString());
    backgroundColor = QColor(s.value("style/backgroundColor", "#111318").toString());
    backgroundOpacity->setValue(s.value("style/backgroundOpacity", 90).toInt());
    duration->setValue(s.value("style/duration", 8).toInt());

    animation->setCurrentIndex(qMax(0, animation->findData(s.value("style/animation", "fade").toString())));
    sizePreset->setCurrentIndex(qMax(0, sizePreset->findData(s.value("style/size", "normal").toString())));
    showAvatar->setChecked(s.value("style/showAvatar", true).toBool());
    showLogo->setChecked(s.value("style/showLogo", true).toBool());

    updateColorButton(textColorButton, textColor);
    updateColorButton(nameColorButton, nameColor);
    updateColorButton(backgroundColorButton, backgroundColor);
    suppressStyleSignals = false;
}

void SocialCommentsDock::saveSettings(bool includeSecrets)
{
    QSettings s("SocialCommentsOBS", "SocialComments");
    s.setValue("youtube/video", ytVideo->text());
    s.setValue("facebook/video", fbVideo->text());
    s.setValue("oauth/googleClientId", googleClientId->text().trimmed());
    s.setValue("oauth/facebookBrokerUrl", facebookBrokerUrl->text().trimmed());
    s.setValue("oauth/facebookAppId", facebookAppId->text().trimmed());
    s.setValue("ui/mode", mode->currentData().toString());
    s.setValue("security/rememberCredentials", rememberCredentials->isChecked());
    s.setValue("style/preset", preset->currentData().toString());
    s.setValue("style/font", fontFamily->currentText());
    s.setValue("style/size", sizePreset->currentData().toString());
    s.setValue("style/textColor", textColor.name());
    s.setValue("style/nameColor", nameColor.name());
    s.setValue("style/backgroundColor", backgroundColor.name());
    s.setValue("style/backgroundOpacity", backgroundOpacity->value());
    s.setValue("style/duration", duration->value());
    s.setValue("style/animation", animation->currentData().toString());
    s.setValue("style/showAvatar", showAvatar->isChecked());
    s.setValue("style/showLogo", showLogo->isChecked());

    if (!includeSecrets || !SecretStore::available())
        return;

    if (rememberCredentials->isChecked()) {
        bool ok = true;
        ok &= googleClientSecret->text().isEmpty() ? SecretStore::remove("google-client-secret")
                                                   : SecretStore::save("google-client-secret", googleClientSecret->text());
        ok &= facebookAppSecret->text().isEmpty() ? SecretStore::remove("facebook-app-secret")
                                                  : SecretStore::save("facebook-app-secret", facebookAppSecret->text());
        ok &= ytApiKey->text().isEmpty() ? SecretStore::remove("youtube-api-key")
                                         : SecretStore::save("youtube-api-key", ytApiKey->text());
        ok &= fbToken->text().isEmpty() ? SecretStore::remove("facebook-page-token-manual")
                                        : SecretStore::save("facebook-page-token-manual", fbToken->text());
        if (!googleRefreshToken.isEmpty())
            ok &= SecretStore::save("google-refresh-token", googleRefreshToken);
        if (!facebookUserToken.isEmpty())
            ok &= SecretStore::save("facebook-user-token", facebookUserToken);
        credentialStatus->setText(ok ? "Sesiones y secretos guardados en Windows Credential Manager."
                                     : "No pude guardar alguna credencial en Windows.");
    } else {
        for (const QString &key : {QString("google-client-secret"), QString("facebook-app-secret"),
                                   QString("youtube-api-key"), QString("facebook-page-token-manual"),
                                   QString("google-refresh-token"), QString("facebook-user-token")})
            SecretStore::remove(key);
        credentialStatus->setText("Las sesiones no se guardarán al cerrar OBS.");
    }
}

void SocialCommentsDock::applyPreset(const QString &presetKey)
{
    if (presetKey == "custom") {
        applyStyle();
        return;
    }

    suppressStyleSignals = true;
    const QString font = preferredFont(fontFamily);
    const int fontIndex = fontFamily->findText(font, Qt::MatchFixedString);
    if (fontIndex >= 0)
        fontFamily->setCurrentIndex(fontIndex);

    if (presetKey == "light") {
        textColor = QColor("#202124");
        nameColor = QColor("#111318");
        backgroundColor = QColor("#FFFFFF");
        backgroundOpacity->setValue(94);
        animation->setCurrentIndex(qMax(0, animation->findData("fade")));
        sizePreset->setCurrentIndex(qMax(0, sizePreset->findData("normal")));
        showAvatar->setChecked(true);
        showLogo->setChecked(true);
    } else if (presetKey == "minimal") {
        textColor = QColor("#FFFFFF");
        nameColor = QColor("#FFFFFF");
        backgroundColor = QColor("#0E1014");
        backgroundOpacity->setValue(68);
        animation->setCurrentIndex(qMax(0, animation->findData("fade")));
        sizePreset->setCurrentIndex(qMax(0, sizePreset->findData("compact")));
        showAvatar->setChecked(false);
        showLogo->setChecked(true);
    } else {
        textColor = QColor("#FFFFFF");
        nameColor = QColor("#FFFFFF");
        backgroundColor = QColor("#111318");
        backgroundOpacity->setValue(90);
        animation->setCurrentIndex(qMax(0, animation->findData("fade")));
        sizePreset->setCurrentIndex(qMax(0, sizePreset->findData("normal")));
        showAvatar->setChecked(true);
        showLogo->setChecked(true);
    }

    updateColorButton(textColorButton, textColor);
    updateColorButton(nameColorButton, nameColor);
    updateColorButton(backgroundColorButton, backgroundColor);
    suppressStyleSignals = false;
    applyStyle();
}

void SocialCommentsDock::markStyleCustom()
{
    if (suppressStyleSignals || preset->currentData().toString() == "custom")
        return;
    suppressStyleSignals = true;
    const int customIndex = preset->findData("custom");
    if (customIndex >= 0)
        preset->setCurrentIndex(customIndex);
    suppressStyleSignals = false;
}

void SocialCommentsDock::setStylePanelVisible(bool visible)
{
    if (styleGroup)
        styleGroup->setVisible(visible);
    if (styleToggle) {
        styleToggle->setChecked(visible);
        styleToggle->setText(visible ? "Ajustes visuales ▴" : "Ajustes visuales ▾");
    }
}

void SocialCommentsDock::setConnectionAdvancedVisible(bool visible)
{
    if (connectionAdvancedGroup)
        connectionAdvancedGroup->setVisible(visible);
    if (connectionAdvancedToggle) {
        connectionAdvancedToggle->setChecked(visible);
        connectionAdvancedToggle->setText(visible ? "Configuración avanzada ▴" : "Configuración avanzada ▾");
    }
}

void SocialCommentsDock::applyStyle()
{
    OverlayStyle s;
    s.fontFamily = fontFamily->currentText();
    s.textColor = textColor.name();
    s.nameColor = nameColor.name();
    s.backgroundColor = backgroundColor.name();
    s.backgroundOpacity = backgroundOpacity->value();
    s.durationSeconds = duration->value();
    s.animation = animation->currentData().toString();
    s.sizePreset = sizePreset->currentData().toString();
    s.showAvatar = showAvatar->isChecked();
    s.showLogo = showLogo->isChecked();
    overlay.setStyle(s);
    updateColorButton(textColorButton, textColor);
    updateColorButton(nameColorButton, nameColor);
    updateColorButton(backgroundColorButton, backgroundColor);
    saveSettings(false);
}

void SocialCommentsDock::updateColorButton(QPushButton *button, const QColor &color)
{
    if (!button)
        return;
    button->setText(color.name().toUpper());
    const QString foreground = color.lightness() < 135 ? "#FFFFFF" : "#111111";
    button->setStyleSheet(QString("QPushButton{background:%1;color:%2;border:1px solid rgba(127,127,127,.4);padding:5px 9px;border-radius:6px}")
                              .arg(color.name(), foreground));
}

QColor SocialCommentsDock::chooseColor(const QColor &current, QPushButton *button)
{
    const QColor selected = QColorDialog::getColor(current, this, "Elegir color");
    if (!selected.isValid())
        return current;
    updateColorButton(button, selected);
    return selected;
}

void SocialCommentsDock::onIncomingComment(const SocialComment &comment)
{
    addCommentRow(comment);
    if (mode->currentData().toString() == "automatic")
        enqueueAuto(comment);
}

void SocialCommentsDock::addCommentRow(const SocialComment &comment)
{
    auto *item = new QListWidgetItem(commentList);
    auto *row = new QWidget();
    auto *layout = new QVBoxLayout(row);
    layout->setContentsMargins(7, 6, 7, 6);
    layout->setSpacing(4);

    auto *top = new QHBoxLayout();
    auto *badge = new QLabel(QString("● %1").arg(badgeFor(comment.platform)));
    badge->setStyleSheet(QString("font-weight:700;color:%1").arg(dotColor(comment.platform)));
    auto *author = new QLabel(comment.author);
    QFont f = author->font();
    f.setBold(true);
    author->setFont(f);
    top->addWidget(badge);
    top->addWidget(author, 1);
    layout->addLayout(top);

    auto *msg = new QLabel(comment.message);
    msg->setWordWrap(true);
    layout->addWidget(msg);

    auto *buttons = new QHBoxLayout();
    auto *show = new QPushButton("Mostrar");
    auto *pin = new QPushButton("★ Destacar");
    buttons->addStretch(1);
    buttons->addWidget(show);
    buttons->addWidget(pin);
    layout->addLayout(buttons);

    QObject::connect(show, &QPushButton::clicked, row, [this, comment]() { showNow(comment, false); });
    QObject::connect(pin, &QPushButton::clicked, row, [this, comment]() { showNow(comment, true); });

    item->setSizeHint(row->sizeHint());
    commentList->setItemWidget(item, row);
    commentList->scrollToBottom();

    while (commentList->count() > 150)
        delete commentList->takeItem(0);
}

void SocialCommentsDock::showNow(const SocialComment &comment, bool pin)
{
    autoDisplayTimer.stop();
    pinnedActive = pin;
    overlay.showComment(comment, pin);
    if (!pin)
        autoDisplayTimer.start(duration->value() * 1000);
}

void SocialCommentsDock::enqueueAuto(const SocialComment &comment)
{
    autoQueue.enqueue(comment);
    while (autoQueue.size() > 50)
        autoQueue.dequeue();
    processAutoQueue();
}

void SocialCommentsDock::processAutoQueue()
{
    if (pinnedActive || autoDisplayTimer.isActive() || autoQueue.isEmpty())
        return;
    const SocialComment next = autoQueue.dequeue();
    showNow(next, false);
}

void SocialCommentsDock::clearOverlay(bool continueQueue)
{
    autoDisplayTimer.stop();
    pinnedActive = false;
    overlay.clear();
    if (!continueQueue)
        autoQueue.clear();
    else
        QTimer::singleShot(250, this, [this]() { processAutoQueue(); });
}

void SocialCommentsDock::previewTest()
{
    static bool flip = false;
    flip = !flip;
    SocialComment c;
    c.id = "preview";
    c.platform = flip ? "youtube" : "facebook";
    c.author = flip ? "María López" : "Carlos Rodríguez";
    c.message = flip ? "¡Saludos! El comentario se ve perfecto 👋" : "Excelente transmisión, se ve muy limpio 🔥";
    showNow(c, false);
}

void SocialCommentsDock::addOverlayToCurrentScene()
{
    if (overlay.port() == 0) {
        QMessageBox::warning(this, "Social Comments", "El servidor local del overlay no está activo.");
        return;
    }

    constexpr const char *sourceName = "Social Comments Overlay";
    obs_source_t *source = obs_get_source_by_name(sourceName);
    if (!source) {
        obs_data_t *settings = obs_data_create();
        obs_data_set_bool(settings, "is_local_file", false);
        obs_data_set_string(settings, "url", overlay.overlayUrl().toUtf8().constData());
        obs_data_set_int(settings, "width", 960);
        obs_data_set_int(settings, "height", 340);
        obs_data_set_int(settings, "fps", 30);
        obs_data_set_bool(settings, "fps_custom", false);
        obs_data_set_bool(settings, "shutdown", false);
        obs_data_set_bool(settings, "restart_when_active", false);
        obs_data_set_bool(settings, "reroute_audio", false);
        obs_data_set_string(settings, "css", "body{background:rgba(0,0,0,0);margin:0;overflow:hidden;}");
        source = obs_source_create("browser_source", sourceName, settings, nullptr);
        obs_data_release(settings);
    } else {
        obs_data_t *settings = obs_source_get_settings(source);
        obs_data_set_string(settings, "url", overlay.overlayUrl().toUtf8().constData());
        obs_source_update(source, settings);
        obs_data_release(settings);
    }

    if (!source) {
        QMessageBox::warning(this, "Social Comments", "No pude crear la fuente Browser. Verifica que OBS Browser esté instalado.");
        return;
    }

    obs_source_t *sceneSource = obs_frontend_get_current_scene();
    if (!sceneSource) {
        obs_source_release(source);
        QMessageBox::warning(this, "Social Comments", "No hay una escena activa.");
        return;
    }

    obs_scene_t *scene = obs_scene_from_source(sceneSource);
    if (!scene) {
        obs_source_release(sceneSource);
        obs_source_release(source);
        QMessageBox::warning(this, "Social Comments", "La fuente actual no es una escena válida.");
        return;
    }

    if (!obs_scene_find_source(scene, sourceName))
        obs_scene_add(scene, source);

    obs_source_release(sceneSource);
    obs_source_release(source);
    QMessageBox::information(this, "Social Comments", "Overlay añadido. Muévelo y redimensiónalo como cualquier fuente de OBS.");
}
