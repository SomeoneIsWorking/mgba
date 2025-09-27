#include "NativeTcpServer.h"

NativeTcpServer::NativeTcpServer(QObject* parent)
    : QTcpServer(parent) {
}

void NativeTcpServer::incomingConnection(qintptr socketDescriptor) {
    emit haveDescriptor(socketDescriptor);
}
