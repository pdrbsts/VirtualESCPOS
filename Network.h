#pragma once

#include <thread>
#include <functional>
#include <atomic>
#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment (lib, "Ws2_32.lib")

class NetworkServer {
public:
    NetworkServer();
    ~NetworkServer();

    // Start listening on the specified port
    bool Start(int port, std::function<void(const unsigned char*, int)> dataCallback);

    // Stop the server
    void Stop();

private:
    void ServerLoop();
    void ClientHandler(SOCKET clientSocket);

    SOCKET listenSocket;
    std::thread serverThread;
    std::atomic<bool> running;
    std::function<void(const unsigned char*, int)> onDataReceived;
};
