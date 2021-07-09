#ifndef QPID_DISPATCH_SOCKET_UTILS_H
#define QPID_DISPATCH_SOCKET_UTILS_H

#include <netinet/in.h>

#include <cstring>
#include <string>

// Saw warning to not use `= {}` to initialize socket structs; it supposedly does not work right
//  due to casting and c-style polymorphism there. It's working just fine for me, though.
template <class T>
inline void zero(T &value)
{
    memset(&value, 0, sizeof(value));
}

void fillSockAddr(const std::string &address, unsigned short port, sockaddr_in &addr);

#endif  // QPID_DISPATCH_SOCKET_UTILS_H