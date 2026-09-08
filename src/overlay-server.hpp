#pragma once

#include "comment-model.hpp"

#include <QJsonObject>
#include <QTcpServer>

class QTcpSocket;

class OverlayServer : public QObject {
public:
    explicit OverlayServer(QObject *parent = nullptr);

    bool start();
    void stop();
    quint16 port() const { return server.serverPort(); }
    QString overlayUrl() const;

    void setStyle(const OverlayStyle &newStyle);
    void showComment(const SocialComment &comment, bool pinned);
    void clear();

private:
    void handleConnection();
    void handleRequest(QTcpSocket *socket);
    QByteArray renderStateJson() const;
    QByteArray overlayHtml() const;
    void writeHttp(QTcpSocket *socket, const QByteArray &contentType, const QByteArray &body, int status = 200);

    QTcpServer server;
    OverlayStyle style;
    SocialComment current;
    bool visible = false;
    bool pinned = false;
    quint64 version = 0;
};
