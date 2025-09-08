#include <WS2tcpip.h>
#include <WinSock2.h>
#include <process.h>
#include <stdio.h>
#include <tchar.h>
#include <locale.h>
#include <windows.h>

#include <cmath>
#include <cstdlib>


#define MAX_LOADSTRING 100
#define MAX_BUFFER 1024
#define SERVER_PORT 27015
#define SERVER_PORT_CHAR "27015"

int _tmain(int argc, TCHAR *argv[]) {
  _wsetlocale(LC_ALL, L"Korean");
  SetConsoleOutputCP(CP_UTF8); 
  WSADATA wsaData;
  int res = WSAStartup(MAKEWORD(2, 2), &wsaData);
  if (res != 0) {
    _ftprintf(stderr, TEXT("Can't Initialize winsock\n"));
    return -1;
  }
  char recvBuffer[MAX_BUFFER];
  TCHAR messageBuffer[MAX_BUFFER];
  SOCKET listenSocket;
  SOCKADDR_IN clientAddr;
  int addrLen = sizeof(SOCKADDR_IN);
  SOCKET clientSocket;
 

  SOCKADDR_IN serverAddr;
  serverAddr.sin_family = PF_INET;
  serverAddr.sin_port = htons(SERVER_PORT);
  serverAddr.sin_addr.S_un.S_addr = htonl(INADDR_ANY);

  struct addrinfo *result = NULL;
  struct addrinfo hints;

  ZeroMemory(&hints, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;
  hints.ai_flags = AI_PASSIVE;

  res = getaddrinfo(NULL, SERVER_PORT_CHAR, &hints, &result);
  if (res != 0) {
    _ftprintf(stderr, TEXT("getaddrinfo failed with error: %d\n"), res);
    WSACleanup();
    return 1;
  }

  listenSocket =
      socket(result->ai_family, result->ai_socktype, result->ai_protocol);
  if (listenSocket == INVALID_SOCKET) {
    _tprintf(TEXT("socket failed with error: %ld\n"), WSAGetLastError());
    freeaddrinfo(result);
    WSACleanup();
    return 1;
  }

  res = bind(listenSocket, (struct sockaddr *)&serverAddr, sizeof(SOCKADDR_IN));
  if (res == SOCKET_ERROR) {
    _ftprintf(stderr, TEXT("Fail to bind socket\n"));
    closesocket(listenSocket);
    WSACleanup();
    return -1;
  }

  res = listen(listenSocket, 5);
  if (res == SOCKET_ERROR) {
    _tprintf(TEXT("Fail to listen\n"));
    closesocket(listenSocket);
    WSACleanup();
    return -1;
  }
  _tprintf(TEXT("Server Start\n"));
  clientSocket = accept(listenSocket, (SOCKADDR *)&clientAddr, &addrLen);

  if (clientSocket == INVALID_SOCKET) {
    _ftprintf(stderr, TEXT("fail to accept\n"));
    return -1;
  } else {
    _tprintf(TEXT("Client connected\n"));
  }

  while (true) {
    int sendres;
    int res =
        recv(clientSocket, recvBuffer, sizeof(recvBuffer), 0);
    if (res) {
      /*ZeroMemory(&messageBuffer, sizeof(messageBuffer));*/
      int nLen = MultiByteToWideChar(CP_ACP, 0, recvBuffer, -1,
                                     NULL, NULL); 
      MultiByteToWideChar(CP_ACP, 0, recvBuffer, -1,
                          messageBuffer, nLen);
      _tprintf(TEXT("Receive: %s\n"), messageBuffer);
      sendres = send(clientSocket, recvBuffer, res, 0);
      if (sendres == SOCKET_ERROR) {
        _ftprintf(stderr, TEXT("fail to send\n"));
        break;
      } else {
        _tprintf(TEXT("Sent: %s\n"), messageBuffer);
      }
    } else {
      _ftprintf(stderr, TEXT("fail to receive\n"));
      break;
    }
  }

  closesocket(listenSocket);
  closesocket(clientSocket);
  WSACleanup();
  return 0;
}
