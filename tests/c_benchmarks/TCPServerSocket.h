

#ifndef QPID_DISPATCH_TCPSERVERSOCKET_H
#define QPID_DISPATCH_TCPSERVERSOCKET_H

#include "Socket.h"
#include "TCPSocket.h"

class TCPServerSocket : public Socket
{
   private:
    void setListen(int queueLen);

   public:
    TCPServerSocket(unsigned short localPort, int queueLen = 10);
    TCPServerSocket(const std::string &localAddress, unsigned short localPort, int queueLen = 10);
    TCPSocket *accept();
    void shutdown();
};

#endif  // QPID_DISPATCH_TCPSERVERSOCKET_H
