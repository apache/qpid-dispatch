#include "TCPSocket.h"

#include <netinet/in.h>
#include <sys/socket.h>

TCPSocket::TCPSocket() : Socket(SOCK_STREAM, IPPROTO_TCP)
{
}

TCPSocket::TCPSocket(const std::string &remoteAddress, unsigned short remotePort) : Socket(SOCK_STREAM, IPPROTO_TCP)
{
    connect(remoteAddress, remotePort);
}

TCPSocket::TCPSocket(int newConnSD) : Socket(newConnSD)
{
}
