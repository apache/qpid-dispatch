#include "UDPSocket.h"

#include "SocketException.h"
#include "socket_utils.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <string>

UDPSocket::UDPSocket() : Socket(SOCK_DGRAM, IPPROTO_UDP)
{
    setBroadcast();
}

UDPSocket::UDPSocket(unsigned short localPort) : Socket(SOCK_DGRAM, IPPROTO_UDP)
{
    setLocalPort(localPort);
    setBroadcast();
}

UDPSocket::UDPSocket(const std::string &localAddress, unsigned short localPort) : Socket(SOCK_DGRAM, IPPROTO_UDP)
{
    setLocalAddressAndPort(localAddress, localPort);
    setBroadcast();
}

void UDPSocket::setBroadcast()
{
    int broadcastPermission = 1;
    setsockopt(mFileDescriptor, SOL_SOCKET, SO_BROADCAST, &broadcastPermission, sizeof(broadcastPermission));
}

void UDPSocket::disconnect()
{
    sockaddr_in nullAddr = {};
    nullAddr.sin_family  = AF_UNSPEC;

    // Try to disconnect
    if (::connect(mFileDescriptor, reinterpret_cast<const sockaddr *>(&nullAddr), sizeof(nullAddr)) < 0) {
        if (errno != EAFNOSUPPORT) {
            throw SocketException("Disconnect failed (connect())", true);
        }
    }
}

void UDPSocket::sendTo(const void *buffer, int bufferLen, const std::string &remoteAddress, unsigned short remotePort)
{
    sockaddr_in destAddr;
    fillSockAddr(remoteAddress, remotePort, destAddr);

    // Write out the whole buffer as a single message.
    if (sendto(mFileDescriptor, buffer, bufferLen, 0, reinterpret_cast<const sockaddr *>(&destAddr), sizeof(destAddr)) != bufferLen) {
        throw SocketException("Send failed (sendto())", true);
    }
}

int UDPSocket::recvFrom(void *buffer, int bufferLen, std::string &sourceAddress, unsigned short &sourcePort)
{
    sockaddr_in clntAddr;
    socklen_t addrLen = sizeof(clntAddr);
    int rtn = recvfrom(mFileDescriptor, buffer, bufferLen, 0, reinterpret_cast<sockaddr *>(&clntAddr), &addrLen);
    if (rtn < 0) {
        throw SocketException("Receive failed (recvfrom())", true);
    }
    sourceAddress = inet_ntoa(clntAddr.sin_addr);
    sourcePort    = ntohs(clntAddr.sin_port);

    return rtn;
}

void UDPSocket::setMulticastTTL(unsigned char multicastTTL)
{
    if (setsockopt(mFileDescriptor, IPPROTO_IP, IP_MULTICAST_TTL, (void *) &multicastTTL, sizeof(multicastTTL)) < 0) {
        throw SocketException("Multicast TTL set failed (setsockopt())", true);
    }
}

void UDPSocket::joinGroup(const std::string &multicastGroup)
{
    struct ip_mreq multicastRequest;

    multicastRequest.imr_multiaddr.s_addr = inet_addr(multicastGroup.c_str());
    multicastRequest.imr_interface.s_addr = htonl(INADDR_ANY);
    if (setsockopt(mFileDescriptor, IPPROTO_IP, IP_ADD_MEMBERSHIP, &multicastRequest, sizeof(multicastRequest)) < 0) {
        throw SocketException("Multicast group join failed (setsockopt())", true);
    }
}

void UDPSocket::leaveGroup(const std::string &multicastGroup)
{
    struct ip_mreq multicastRequest;

    multicastRequest.imr_multiaddr.s_addr = inet_addr(multicastGroup.c_str());
    multicastRequest.imr_interface.s_addr = htonl(INADDR_ANY);
    if (setsockopt(mFileDescriptor, IPPROTO_IP, IP_DROP_MEMBERSHIP, &multicastRequest, sizeof(multicastRequest)) < 0) {
        throw SocketException("Multicast group leave failed (setsockopt())", true);
    }
}