#pragma once

#include <atomic>
#include <functional>
#include <thread>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")
#else
#include <arpa/inet.h>
#include <cstring>
#include <iostream>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>


#define SOCKET int
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
#define ZeroMemory(p, size) memset(p, 0, size)
#define closesocket close
#endif

class NetworkServer {
public:
  NetworkServer();
  ~NetworkServer();

  // Start listening on the specified port
  bool Start(int port,
             std::function<void(const unsigned char *, int)> dataCallback);

  // Stop the server
  void Stop();

private:
  void ServerLoop();
  void ClientHandler(SOCKET clientSocket);

  SOCKET listenSocket;
  std::thread serverThread;
  std::atomic<bool> running;
  std::function<void(const unsigned char *, int)> onDataReceived;
};
