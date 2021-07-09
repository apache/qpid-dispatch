//
// Created by jdanek on 7/8/21.
//

#ifndef QPID_DISPATCH_UDPSOCKET_H
#define QPID_DISPATCH_UDPSOCKET_H

#include "Socket.h"

#include <string>
class UDPSocket : public Socket
{
   public:
    UDPSocket();
    UDPSocket(unsigned short localPort);
    UDPSocket(const std::string &localAddress, unsigned short localPort);
    void disconnect();
    void sendTo(const void *buffer, int bufferLen, const std::string &remoteAddress, unsigned short remotePort);
    int recvFrom(void *buffer, int bufferLen, std::string &sourceAddress, unsigned short &sourcePort);
    void setMulticastTTL(unsigned char multicastTTL);
    void joinGroup(const std::string &multicastGroup);
    void leaveGroup(const std::string &multicastGroup);

   private:
    void setBroadcast();
};

#endif  // QPID_DISPATCH_UDPSOCKET_H
