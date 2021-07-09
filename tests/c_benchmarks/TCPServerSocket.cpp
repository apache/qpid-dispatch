#include "TCPServerSocket.h"

#include "SocketException.h"

#include <netinet/in.h>
#include <sys/socket.h>

#include <string>

TCPServerSocket::TCPServerSocket(unsigned short localPort, int queueLen) : Socket(SOCK_STREAM, IPPROTO_TCP)
{
    setLocalPort(localPort);
    setListen(queueLen);
}

TCPServerSocket::TCPServerSocket(const std::string &localAddress, unsigned short localPort, int queueLen)
    : Socket(SOCK_STREAM, IPPROTO_TCP)
{
    setLocalAddressAndPort(localAddress, localPort);
    setListen(queueLen);
}

TCPSocket *TCPServerSocket::accept()
{
    int newConnSD = ::accept(mFileDescriptor, nullptr, nullptr);
    if (newConnSD < 0) {
        throw SocketException("Accept failed (accept())", true);
    }

    return new TCPSocket(newConnSD);
}

void TCPServerSocket::shutdown()
{
    ::shutdown(this->mFileDescriptor, ::SHUT_RD);
}

void TCPServerSocket::setListen(int queueLen)
{
    if (listen(mFileDescriptor, queueLen) < 0) {
        throw SocketException("Set listening socket failed (listen())", true);
    }
}