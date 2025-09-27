#include "LocalClientDetector.h"
#include <QNetworkInterface>
#include <QHostInfo>
#include <QHostAddress>
#include <QDebug>

bool LocalClientDetector::isLocal(const QUrl& url, QString* reason) {
    QString host = url.host();
    if (host.isEmpty()) {
        if (reason) *reason = QLatin1String("empty host (treated as local)");
        return true;
    }
    if (host == QLatin1String("localhost") || host == QLatin1String("127.0.0.1") || host == QLatin1String("::1")) {
        if (reason) *reason = QLatin1String("loopback address");
        return true;
    }

    QHostAddress target;
    if (target.setAddress(host)) {
        // Direct IP parse succeeded — compare against local addresses
        const auto localAddrs = QNetworkInterface::allAddresses();
        for (const QHostAddress& a : localAddrs) {
            if (a == target) {
                if (reason) *reason = QString::fromLatin1("IP matches local interface: %1").arg(a.toString());
                return true;
            }
            // IPv4-mapped IPv6 -> handle the mapping case
            if (a.protocol() == QAbstractSocket::IPv6Protocol && a.toIPv4Address() != 0 && target.protocol() == QAbstractSocket::IPv4Protocol) {
                if (QHostAddress(a.toIPv4Address()) == target) {
                    if (reason) *reason = QString::fromLatin1("IPv4-mapped IPv6 matches local interface: %1").arg(a.toString());
                    return true;
                }
            }
        }
        if (reason) *reason = QString::fromLatin1("IP does not match any local interface: %1").arg(host);
        return false;
    }

    // Not an IP — resolve hostname synchronously and compare its addresses
    QHostInfo info = QHostInfo::fromName(host);
    if (info.error() != QHostInfo::NoError) {
        if (reason) *reason = QString::fromLatin1("hostname resolution failed: %1").arg(info.errorString());
        return false;
    }
    const auto resolved = info.addresses();
    if (resolved.isEmpty()) {
        if (reason) *reason = QLatin1String("hostname resolved to no addresses");
        return false;
    }

    const auto localAddrs = QNetworkInterface::allAddresses();
    for (const QHostAddress& r : resolved) {
        for (const QHostAddress& l : localAddrs) {
            if (r == l) {
                if (reason) *reason = QString::fromLatin1("hostname resolves to local address %1").arg(r.toString());
                return true;
            }
            if (l.protocol() == QAbstractSocket::IPv6Protocol && l.toIPv4Address() != 0 && r.protocol() == QAbstractSocket::IPv4Protocol) {
                if (QHostAddress(l.toIPv4Address()) == r) {
                    if (reason) *reason = QString::fromLatin1("hostname resolves to IPv4 that matches IPv6-mapped local %1").arg(l.toString());
                    return true;
                }
            }
        }
    }
    if (reason) *reason = QString::fromLatin1("hostname resolves to non-local addresses");
    return false;
}
