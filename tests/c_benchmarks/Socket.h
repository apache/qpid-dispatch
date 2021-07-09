

#ifndef QPID_DISPATCH_SOCKET_H
#define QPID_DISPATCH_SOCKET_H

#include <string>
class Socket
{
   public:
    ~Socket();
    std::string getLocalAddress() const;
    unsigned short getLocalPort();
    void setLocalPort(unsigned short localPort);
    void setLocalAddressAndPort(const std::string &localAddress, unsigned short localPort = 0);
    static unsigned short resolveService(const std::string &service, const std::string &protocol = "tcp");
    Socket(const Socket &&sock) noexcept : mFileDescriptor(sock.mFileDescriptor)
    {
    }

   private:
    Socket(const Socket &sock);
    void operator=(const Socket &sock);

   protected:
    int mFileDescriptor;
    Socket(int type, int protocol) noexcept(false);
    explicit Socket(int fd);

   public:
    void connect(const std::string &remoteAddress, unsigned short remotePort) noexcept(false);
    void send(const void *buffer, int bufferLen) noexcept(false);
    int recv(void *buffer, int bufferLen) noexcept(false);
    std::string getForeignAddress() noexcept(false);
    unsigned short getForeignPort() noexcept(false);
};

#endif  // QPID_DISPATCH_SOCKET_H
