/* Simple UDP broadcaster that periodically multicasts/broadcasts a small JSON
 * packet announcing the presence of the mGBA Web server on the local network.
 * This is intentionally lightweight and runs on a QTimer; it does not attempt
 * to discover network interfaces or traverse subnets. Receivers (web clients)
 * can listen for these UDP packets to auto-fill the server address.
 */
#pragma once

#include <QObject>
#include <QUdpSocket>
#include <QTimer>
#include <QHostAddress>

namespace QGBA {

class WebBroadcaster : public QObject {
    Q_OBJECT
public:
    explicit WebBroadcaster(QObject* parent = nullptr);
    ~WebBroadcaster();

    // Configure the announcement payload and period
    void setServerInfo(const QString& host, int udpPort = 0, int tcpPort = 0);
    void setIntervalMs(int ms);
    bool start();
    void stop();

private slots:
    void onTimer();

private:
    QUdpSocket* m_socket;
    QTimer* m_timer;
    QString m_host;
    int m_udpPort;
    int m_tcpPort;
    int m_intervalMs;
    QHostAddress m_broadcastAddr;
};

}
