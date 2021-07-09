#ifndef QPID_DISPATCH_TCPSOCKET_H
#define QPID_DISPATCH_TCPSOCKET_H

#include "Socket.h"
class TCPSocket : public Socket
{
   private:
    friend class TCPServerSocket;
    explicit TCPSocket(int newConnSD);

   public:
    TCPSocket();
    TCPSocket(const std::string& remoteAddress, unsigned short remotePort);
    TCPSocket(TCPSocket&& socket) noexcept : TCPSocket(socket.mFileDescriptor)
    {
        socket.mFileDescriptor = -1;
    };
};

#endif  // QPID_DISPATCH_TCPSOCKET_H
