#include "echo_server.h"

#include "SocketException.h"
#include "TCPServerSocket.h"
#include "TCPSocket.h"
#include "socket_utils.h"

#include <iostream>

const unsigned int RCVBUFSIZE = 32;  // Size of receive buffer

TCPServerSocket *GLOBAL_servSock;

void HandleTCPClient(TCPSocket *sock);  // TCP client handling function

void stop_echo_server()
{
    GLOBAL_servSock->shutdown();
}

int run_echo_server()
{
    unsigned short echoServPort = 5674;

    try {
        TCPServerSocket servSock(echoServPort);  // Server Socket object
        GLOBAL_servSock = &servSock;

        //        for (;;) {   // Run forever
        HandleTCPClient(servSock.accept());  // Wait for a client to connect
                                             //        }
    } catch (SocketException &e) {
        std::cerr << e.what() << std::endl;
        //        exit(1);
    }

    return 0;
}

// TCP client handling function
void HandleTCPClient(TCPSocket *sock)
{
    ////    cout << "Handling client ";
    //    try {
    ////        cout << sock->getForeignAddress() << ":";
    //    } catch (SocketException &e) {
    //        cerr << "Unable to get remote address" << endl;
    //    }
    //    try {
    //        cout << sock->getForeignPort();
    //    } catch (SocketException &e) {
    //        cerr << "Unable to get remote port" << endl;
    //    }
    //    cout << endl;

    // Send received string and receive again until the end of transmission
    char echoBuffer[RCVBUFSIZE];
    int recvMsgSize;
    while ((recvMsgSize = sock->recv(echoBuffer, RCVBUFSIZE)) > 0) {  // Zero means
        // end of transmission
        // Echo message back to client
        sock->send(echoBuffer, recvMsgSize);
    }
    delete sock;
}