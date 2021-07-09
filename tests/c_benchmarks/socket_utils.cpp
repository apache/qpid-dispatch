#include "socket_utils.h"

#include "SocketException.h"

#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>

void fillSockAddr(const std::string &address, unsigned short port, sockaddr_in &addr)
{
    zero(addr);

    hostent *host = gethostbyname(address.c_str());
    if (host == nullptr) {
        throw SocketException("Failed to resolve name (gethostbyname())");
    }

    addr.sin_port        = htons(port);
    addr.sin_family      = host->h_addrtype;
    if (host->h_addrtype == AF_INET) {
        auto sin_addr = reinterpret_cast<struct in_addr *>(host->h_addr_list[0]);
        addr.sin_addr.s_addr = sin_addr->s_addr;
    } else if (host->h_addrtype == AF_INET6) {
        // auto sin_addr = reinterpret_cast<struct in6_addr *>(host->h_addr_list[0]);
    } else {
        throw SocketException("Name was not resolved to IPv4 (gethostbyname())");
    }
}
