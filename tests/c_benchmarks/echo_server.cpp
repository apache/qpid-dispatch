#include "PracticalSocket.h"  // For Socket, ServerSocket, and SocketException
#include <iostream>           // For cerr and cout
#include <cstdlib>            // For atoi()

#include "echo_server.h"

using namespace std;

const unsigned int RCVBUFSIZE = 32;    // Size of receive buffer

TCPServerSocket *GLOBAL_servSock;

void HandleTCPClient(TCPSocket *sock); // TCP client handling function

void stop_echo_server() {
    GLOBAL_servSock->shutdown();
}

int run_echo_server() {
    unsigned short echoServPort = 5674;

    try {
        TCPServerSocket servSock(echoServPort);     // Server Socket object
        GLOBAL_servSock = &servSock;

//        for (;;) {   // Run forever
            HandleTCPClient(servSock.accept());       // Wait for a client to connect
//        }
    } catch (SocketException &e) {
        cerr << e.what() << endl;
//        exit(1);
    }

    return 0;
}

// TCP client handling function
void HandleTCPClient(TCPSocket *sock) {
////    cout << "Handling client ";
//    try {
////        cout << sock->getForeignAddress() << ":";
//    } catch (SocketException &e) {
//        cerr << "Unable to get foreign address" << endl;
//    }
//    try {
//        cout << sock->getForeignPort();
//    } catch (SocketException &e) {
//        cerr << "Unable to get foreign port" << endl;
//    }
//    cout << endl;

    // Send received string and receive again until the end of transmission
    char echoBuffer[RCVBUFSIZE];
    int recvMsgSize;
    while ((recvMsgSize = sock->recv(echoBuffer, RCVBUFSIZE)) > 0) { // Zero means
        // end of transmission
        // Echo message back to client
        sock->send(echoBuffer, recvMsgSize);
    }
    delete sock;
}