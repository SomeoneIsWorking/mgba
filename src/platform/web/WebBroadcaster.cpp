/* UDP broadcaster implementation */
#include "WebBroadcaster.h"
#include "StreamingCommon.h"
#include <QDateTime>
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkInterface>

using namespace QGBA;

WebBroadcaster::WebBroadcaster(QObject* parent)
    : QObject(parent)
    , m_socket(nullptr)
    , m_timer(new QTimer(this))
    , m_udpPort(0)
    , m_tcpPort(0)
    , m_intervalMs(2000)
    , m_broadcastAddr(QHostAddress::Broadcast) {
	connect(m_timer, &QTimer::timeout, this, &WebBroadcaster::onTimer);
}

WebBroadcaster::~WebBroadcaster() {
	stop();
}

void WebBroadcaster::setServerInfo(const QString& host, int udpPort, int tcpPort) {
	m_host = host;
	m_udpPort = udpPort;
	m_tcpPort = tcpPort;
}

void WebBroadcaster::setIntervalMs(int ms) {
	m_intervalMs = ms;
	if (m_timer)
		m_timer->setInterval(ms);
}

bool WebBroadcaster::start() {
	if (m_socket)
		return true;

	m_socket = new QUdpSocket(this);
	// Try to enable broadcast on the socket
	bool ok = m_socket->bind(QHostAddress::AnyIPv4, 0, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint);
	if (!ok) {
		qDebug() << "WebBroadcaster: failed to bind UDP socket" << m_socket->errorString();
		delete m_socket;
		m_socket = nullptr;
		return false;
	}

	m_timer->start(m_intervalMs);
	qDebug() << "WebBroadcaster: started broadcasting every" << m_intervalMs << "ms";
	return true;
}

void WebBroadcaster::stop() {
	if (m_timer && m_timer->isActive())
		m_timer->stop();
	if (m_socket) {
		m_socket->close();
		delete m_socket;
		m_socket = nullptr;
	}
}

void WebBroadcaster::onTimer() {
	if (!m_socket)
		return;

	// Send compact binary discovery packet to avoid JSON parsing.
	// Packet layout (network byte order):
	//  uint32_t magic = 'MGBA'
	//  uint8_t version = 1
	//  char host[64] (NUL-terminated)
	//  uint16_t wsPort
	//  uint16_t httpPort
	//  uint16_t udpPort
	//  uint16_t tcpPort
	//  uint64_t timestamp_ms

	mgba::DiscoveryPacket pkt {};
	pkt.magic = htonl(mgba::DISCOVERY_MAGIC);
	pkt.version = 1;
	memset(pkt.host, 0, sizeof(pkt.host));
	// leave host blank so clients can use the source IP from recvfrom
	(void)m_host;
	pkt.udpPort = htons((uint16_t) m_udpPort);
	pkt.tcpPort = htons((uint16_t) m_tcpPort);
	uint64_t ts = (uint64_t) QDateTime::currentMSecsSinceEpoch();
	pkt.timestamp_ms = mgba::hton64(ts);

	QByteArray data(reinterpret_cast<const char*>(&pkt), sizeof(pkt));
	quint16 port = 43889;
	qint64 written = m_socket->writeDatagram(data, m_broadcastAddr, port);
	if (written < 0) {
		qDebug() << "WebBroadcaster: writeDatagram failed:" << m_socket->errorString();
	}
}
