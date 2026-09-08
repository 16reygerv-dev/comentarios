#pragma once

#include <QString>

struct SocialComment {
    QString id;
    QString platform; // "youtube" | "facebook"
    QString author;
    QString message;
    QString avatarUrl;
};

struct OverlayStyle {
    QString fontFamily = "Inter";
    QString textColor = "#FFFFFF";
    QString nameColor = "#FFFFFF";
    QString backgroundColor = "#111318";
    int backgroundOpacity = 90; // 0..100
    int durationSeconds = 8;
    QString animation = "fade"; // none | fade | slide
    QString sizePreset = "normal"; // compact | normal | large
    bool showAvatar = true;
    bool showLogo = true;
};
