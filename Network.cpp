#include "Network.h"
#include <iostream>
#include <string>
#include <vector>


NetworkServer::NetworkServer() : listenSocket(INVALID_SOCKET), running(false) {
#ifdef _WIN32
  WSADATA wsaData;
  WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
}

NetworkServer::~NetworkServer() {
  Stop();
#ifdef _WIN32
  WSACleanup();
#endif
}

bool NetworkServer::Start(
    int port, std::function<void(const unsigned char *, int)> dataCallback) {
  onDataReceived = dataCallback;

  struct addrinfo *result = NULL;
  struct addrinfo hints;

  ZeroMemory(&hints, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;
  hints.ai_flags = AI_PASSIVE;

  std::string portStr = std::to_string(port);

  if (getaddrinfo(NULL, portStr.c_str(), &hints, &result) != 0) {
    return false;
  }

  listenSocket =
      socket(result->ai_family, result->ai_socktype, result->ai_protocol);
  if (listenSocket == INVALID_SOCKET) {
    freeaddrinfo(result);
    return false;
  }

  if (bind(listenSocket, result->ai_addr, (int)result->ai_addrlen) ==
      SOCKET_ERROR) {
    freeaddrinfo(result);
    closesocket(listenSocket);
    listenSocket = INVALID_SOCKET;
    return false;
  }

  freeaddrinfo(result);

  if (listen(listenSocket, SOMAXCONN) == SOCKET_ERROR) {
    closesocket(listenSocket);
    listenSocket = INVALID_SOCKET;
    return false;
  }

  running = true;
  serverThread = std::thread(&NetworkServer::ServerLoop, this);
  return true;
}

void NetworkServer::Stop() {
  running = false;
  if (listenSocket != INVALID_SOCKET) {
    closesocket(listenSocket);
    listenSocket = INVALID_SOCKET;
  }
  if (serverThread.joinable()) {
    serverThread.join();
  }
}

void NetworkServer::ServerLoop() {
  while (running) {
    SOCKET clientSocket = accept(listenSocket, NULL, NULL);
    if (clientSocket == INVALID_SOCKET) {
      if (running) {
        // Error or interrupted
      }
      continue;
    }

    // Handle client in a detached thread or block here?
    // For a simple simulator, blocking here is risky if the client hangs.
    // Let's spawn a thread.
    std::thread clientThread(&NetworkServer::ClientHandler, this, clientSocket);
    clientThread.detach();
  }
}

void NetworkServer::ClientHandler(SOCKET clientSocket) {
  std::vector<unsigned char> buffer(4096);
  while (running) {
    int bytesReceived =
        recv(clientSocket, (char *)buffer.data(), (int)buffer.size(), 0);
    if (bytesReceived > 0) {
      if (onDataReceived) {
        onDataReceived(buffer.data(), bytesReceived);
      }
    } else if (bytesReceived == 0) {
      // Connection closed
      break;
    } else {
      // Error
      break;
    }
  }
  closesocket(clientSocket);
}
