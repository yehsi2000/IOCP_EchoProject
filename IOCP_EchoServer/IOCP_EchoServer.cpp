#include <WS2tcpip.h>
#include <WinSock2.h>
#include <locale.h>
#include <process.h>
#include <tchar.h>
#include <windows.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <list>
#include <vector>

constexpr DWORD MAX_BUFFER = 1024;
constexpr USHORT SERVER_PORT = 27015;

enum class IO_OPERATION { RECEIVE, SEND };

struct SOCKET_OVERLAPPED {
  WSAOVERLAPPED overlapped;
  IO_OPERATION operationType;
  CHAR messageBuffer[MAX_BUFFER];
  WSABUF dataBuf;
  DWORD bytesRecv = 0;
  DWORD bytesSent = 0;
  SOCKET_OVERLAPPED(IO_OPERATION operationType)
      : overlapped(),
        operationType(operationType),
        messageBuffer(),
        dataBuf{MAX_BUFFER, messageBuffer} {}
};

struct ClientInfo {
  SOCKET socket;
  SOCKET_OVERLAPPED *pSendOverlapped;
  SOCKET_OVERLAPPED *pRecvOverlapped;

  DWORD refCount;

  ClientInfo(SOCKET s)
      : socket(s),
        refCount(1),
        pSendOverlapped(nullptr),
        pRecvOverlapped(nullptr) {}

  void AddRef() { InterlockedIncrement(&refCount); }

  void Release() {
    if (InterlockedDecrement(&refCount) == 0) {
      closesocket(this->socket);
      delete pRecvOverlapped;
      delete pSendOverlapped;
      delete this;
    }
  }
};

HANDLE hIOCP;
std::vector<HANDLE> threadPool;

void WINAPI IOCPWorkerThread(LPVOID p) {
  DWORD recvByteCnt{0};
  DWORD dwFlags{0};
  LPOVERLAPPED pOverlapped = nullptr;
  ClientInfo *pClientInfo = nullptr;
  while (true) {
    BOOL res = GetQueuedCompletionStatus(
        hIOCP, &recvByteCnt, (ULONG_PTR *)&pClientInfo, &pOverlapped, INFINITE);

    SOCKET_OVERLAPPED *pSocketOverlapped =
        CONTAINING_RECORD(pOverlapped, SOCKET_OVERLAPPED, overlapped);

    if (res == 0) {
      _ftprintf(stderr, TEXT("GetQueuedCompletionStatus failed: %d\n"),
                GetLastError());
      pClientInfo->Release();
      continue;
    }

    if (pClientInfo == 0 && pOverlapped == 0) {
      // Shutdown signal
      break;
    }

    if (recvByteCnt == 0) {
      _ftprintf(stderr, TEXT("Client disconnected\n"));
      pClientInfo->Release();
      continue;
    } else {
      if (pSocketOverlapped->operationType == IO_OPERATION::RECEIVE) {
        TCHAR wideBuffer[MAX_BUFFER];
        int nLen = MultiByteToWideChar(
            CP_ACP, 0, pSocketOverlapped->messageBuffer, -1, nullptr, 0);
        MultiByteToWideChar(CP_ACP, 0, pSocketOverlapped->messageBuffer, -1,
                            wideBuffer, nLen);
        _tprintf(TEXT("Bytes received: %d : %s\n"), recvByteCnt, wideBuffer);

        memcpy(pClientInfo->pSendOverlapped->messageBuffer,
               pClientInfo->pRecvOverlapped->messageBuffer, recvByteCnt);

        pClientInfo->AddRef();

        res =
            WSASend(pClientInfo->socket, &pClientInfo->pSendOverlapped->dataBuf,
                    1, &pClientInfo->pSendOverlapped->bytesSent, 0,
                    (LPOVERLAPPED)pClientInfo->pSendOverlapped, nullptr);

        if (res == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING) {
          _ftprintf(stderr, TEXT("WSASend failed: %d\n"), WSAGetLastError());
        }

        ZeroMemory(&pSocketOverlapped->overlapped, sizeof(WSAOVERLAPPED));
        pSocketOverlapped->dataBuf.len = MAX_BUFFER;
        pSocketOverlapped->dataBuf.buf = pSocketOverlapped->messageBuffer;
        pSocketOverlapped->bytesRecv = 0;
        pSocketOverlapped->operationType = IO_OPERATION::RECEIVE;

        ZeroMemory(pSocketOverlapped->messageBuffer, MAX_BUFFER);

        pClientInfo->AddRef();

        res = WSARecv(pClientInfo->socket, &pSocketOverlapped->dataBuf, 1,
                      &recvByteCnt, &dwFlags, &pSocketOverlapped->overlapped,
                      nullptr);

        if (res == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING) {
          _ftprintf(stderr, TEXT("WSARecv failed: %d\n"), WSAGetLastError());
        }
      } else {
        if (pClientInfo->pSendOverlapped == pSocketOverlapped) {
          _tprintf(TEXT("Bytes sent: %d\n"), pSocketOverlapped->bytesSent);
        }
        pClientInfo->Release();
      }
    }
  }
}

bool CreateThreadPool() {
  SYSTEM_INFO sysInfo;
  GetSystemInfo(&sysInfo);
  DWORD nThreadCnt = sysInfo.dwNumberOfProcessors * 2;

  threadPool = std::vector<HANDLE>(nThreadCnt);

  for (size_t i = 0; i < nThreadCnt; i++) {
    threadPool[i] = (HANDLE)_beginthread(IOCPWorkerThread, 0, nullptr);
    if (threadPool[i] == nullptr) {
      return false;
    }
  }

  return true;
}

int _tmain(int argc, TCHAR *argv[]) {
  _wsetlocale(LC_ALL, L"");
  SetConsoleOutputCP(CP_UTF8);

  WSADATA wsaData;
  int res = WSAStartup(MAKEWORD(2, 2), &wsaData);

  if (res != 0) {
    _ftprintf(stderr, TEXT("Can't Initialize winsock\n"));
    return -1;
  }

  SOCKET listenSocket;

  listenSocket =
      WSASocket(AF_INET, SOCK_STREAM, 0, nullptr, 0, WSA_FLAG_OVERLAPPED);
  if (listenSocket == INVALID_SOCKET) {
    _tprintf(TEXT("socket failed with error: %ld\n"), WSAGetLastError());
    return -1;
  }

  SOCKADDR_IN serverAddr{};
  serverAddr.sin_family = PF_INET;
  serverAddr.sin_port = htons(SERVER_PORT);
  serverAddr.sin_addr.S_un.S_addr = htonl(INADDR_ANY);

  res = bind(listenSocket, (sockaddr *)&serverAddr, sizeof(SOCKADDR_IN));
  if (res == SOCKET_ERROR) {
    _ftprintf(stderr, TEXT("Fail to bind socket\n"));
    closesocket(listenSocket);
    WSACleanup();
    return -1;
  }

  res = listen(listenSocket, SOMAXCONN);
  if (res == SOCKET_ERROR) {
    _tprintf(TEXT("Fail to listen\n"));
    closesocket(listenSocket);
    WSACleanup();
    return -1;
  }

  hIOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
  if (hIOCP == nullptr) {
    _ftprintf(stderr, TEXT("CreateIoCompletionPort failed: %d\n"),
              GetLastError());
    closesocket(listenSocket);
    WSACleanup();
    return -1;
  }

  if (!CreateThreadPool()) {
    _ftprintf(stderr, TEXT("CreateThreadPool failed\n"));
    CloseHandle(hIOCP);
    closesocket(listenSocket);
    WSACleanup();
    return -1;
  }

  _tprintf(TEXT("Server Start\n"));

  while (true) {
    SOCKADDR_IN clientAddr;
    int addrLen = sizeof(SOCKADDR_IN);

    SOCKET clientSocket =
        WSAAccept(listenSocket, (sockaddr *)&clientAddr, &addrLen, nullptr, 0);

    if (clientSocket == INVALID_SOCKET) {
      _ftprintf(stderr, TEXT("fail to accept\n"));
      continue;
    }
    _tprintf(TEXT("Client connected\n"));

    ClientInfo *pClientInfo = new ClientInfo(clientSocket);

    pClientInfo->pRecvOverlapped = new SOCKET_OVERLAPPED(IO_OPERATION::RECEIVE);
    pClientInfo->pSendOverlapped = new SOCKET_OVERLAPPED(IO_OPERATION::SEND);

    CreateIoCompletionPort((HANDLE)clientSocket, hIOCP, (ULONG_PTR)pClientInfo,
                           0);

    DWORD dwFlags = 0;

    pClientInfo->AddRef();
    int res = WSARecv(clientSocket, &pClientInfo->pRecvOverlapped->dataBuf, 1,
                      &pClientInfo->pRecvOverlapped->bytesRecv, &dwFlags,
                      &pClientInfo->pRecvOverlapped->overlapped, nullptr);

    if (res == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING) {
      _ftprintf(stderr, TEXT("WSARecv failed: %d\n"), WSAGetLastError());
      pClientInfo->Release();
      continue;
    }
  }

  closesocket(listenSocket);

  for (size_t i = 0; i < threadPool.size(); ++i)
    PostQueuedCompletionStatus(hIOCP, 0, 0, NULL);

  WaitForMultipleObjects(static_cast<DWORD>(threadPool.size()),
                         threadPool.data(), TRUE, INFINITE);

  for (size_t i = 0; i < threadPool.size(); ++i) CloseHandle(threadPool[i]);

  CloseHandle(hIOCP);

  WSACleanup();
  return 0;
}
