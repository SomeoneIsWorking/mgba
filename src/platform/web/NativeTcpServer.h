#pragma once

#include <QTcpServer>

class NativeTcpServer : public QTcpServer {
    Q_OBJECT
public:
    explicit NativeTcpServer(QObject* parent = nullptr);
protected:
    void incomingConnection(qintptr socketDescriptor) override;
signals:
    void haveDescriptor(qintptr descriptor);
};
