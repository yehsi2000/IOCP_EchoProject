#include <WS2tcpip.h>
#include <WinSock2.h>
#include <process.h>
#include <tchar.h>
#include <windows.h>
#include <windowsx.h>

#include <iostream>
#include <cstdio>
#include <cstdlib>

#include "resource.h"

#define MAX_LOADSTRING 100
#define MAX_BUFFER 1024
#define SERVER_PORT 27015
#define SERVER_PORT_STR "27015"

HWND MainDlg;
HANDLE handleIOCP;
TCHAR messageBuffer[MAX_BUFFER];
TCHAR recvbuf[MAX_BUFFER];
SOCKET connectSocket = INVALID_SOCKET;
WSABUF dataBuf;
HANDLE pworkerHandle;
struct addrinfo *result = NULL, *ptr = NULL, hints;
TCHAR addressBuffer[MAX_BUFFER];
char convertedAddress[MAX_BUFFER];

void Listen(void *args) {
  int res;
  while (true) {
    res = recv(connectSocket, (char *)recvbuf, MAX_BUFFER, 0);
    if (res > 0) {
      printf("Bytes received: %d\n", res);
      Edit_SetText(GetDlgItem(MainDlg, IDC_RECEIVED), messageBuffer);
    } else if (res == 0) {
      printf("Connection closed\n");
      break;
    } else {
      printf("recv failed: %d\n", WSAGetLastError());
      break;
    }
  }
}

BOOL Dlg_OnInitDialog(HWND hwnd, HWND hwndFocus, LPARAM lParam) {
  SendMessage(
      hwnd, WM_SETICON, ICON_BIG,
      (LPARAM)LoadIcon((HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE),
                       MAKEINTRESOURCE(IDI_IOCPECHOCLIENT)));
  SendMessage(
      hwnd, WM_SETICON, ICON_SMALL,
      (LPARAM)LoadIcon((HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE),
                       MAKEINTRESOURCE(IDI_IOCPECHOCLIENT)));

  Edit_SetText(GetDlgItem(hwnd, IDC_ADDRESS), TEXT("127.0.0.1"));
  Edit_SetText(GetDlgItem(hwnd, IDC_SENDEDIT), TEXT("send text"));

  MainDlg = hwnd;

  return (TRUE);
}

void Dlg_OnCommand(HWND hwnd, int id, HWND hwndCtl, UINT codeNotify) {
  int res;
  switch (id) {
    case IDC_CONNECT:
      if (connectSocket != INVALID_SOCKET) {
        MessageBox(hwnd, TEXT("Already connected."), TEXT("Info"), MB_OK);
        return;
      }
      
      Edit_GetText(GetDlgItem(hwnd, IDC_ADDRESS), addressBuffer,
                   _countof(addressBuffer));
      WideCharToMultiByte(CP_ACP, 0, addressBuffer, -1, convertedAddress,
                          _countof(convertedAddress), NULL, NULL);
      res = getaddrinfo(convertedAddress, SERVER_PORT_STR, &hints, &result);
      if (res != 0) {
        printf("getaddrinfo failed: %d\n", res);
        return;
      }

      ptr = result;

      connectSocket =
          socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);

      if (connectSocket == INVALID_SOCKET) {
        std::cerr << "Error at socket():" << WSAGetLastError() << std::endl;
        freeaddrinfo(result);
        return;
      }

      // Connect to server.
      res = connect(connectSocket, ptr->ai_addr, (int)ptr->ai_addrlen);
      if (res == SOCKET_ERROR) {
        closesocket(connectSocket);
        connectSocket = INVALID_SOCKET;
      } else {
        std::cout << "Connected to server" << std::endl;
        pworkerHandle = (HANDLE)_beginthread(Listen, 0, NULL);
      }

      freeaddrinfo(result);
      result = NULL;

      if (connectSocket == INVALID_SOCKET) {
        std::cerr << "Unable to connect to server" << std::endl;
        return;
      }
      break;

    case IDC_SUBMIT:
      Edit_GetText(GetDlgItem(hwnd, IDC_SENDEDIT), messageBuffer,
                   _countof(messageBuffer));
      WideCharToMultiByte(CP_ACP, 0, messageBuffer, -1, convertedAddress,
                          _countof(convertedAddress), NULL, NULL);
      res =
          send(connectSocket, convertedAddress, _countof(convertedAddress), 0);
      if (res == SOCKET_ERROR) {
        printf("send failed: %d\n", WSAGetLastError());
        closesocket(connectSocket);
        connectSocket = INVALID_SOCKET;
        return;
      }

      _tprintf(TEXT("Sent : %s\n"), messageBuffer);

      break;
  }
}



INT_PTR WINAPI Dlg_Proc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
  switch (uMsg) {
    case WM_INITDIALOG:
      return SetDlgMsgResult(
          hwnd, uMsg,
          HANDLE_WM_INITDIALOG(hwnd, wParam, lParam, Dlg_OnInitDialog));
    case WM_COMMAND:
      return SetDlgMsgResult(
          hwnd, uMsg, HANDLE_WM_COMMAND(hwnd, wParam, lParam, Dlg_OnCommand));
    case WM_CLOSE:
      if (pworkerHandle != NULL) {
        if (connectSocket != INVALID_SOCKET) {
          closesocket(connectSocket);
          connectSocket = INVALID_SOCKET;
        }

        WaitForSingleObject(pworkerHandle, 3000);

        CloseHandle(pworkerHandle);
        pworkerHandle = NULL;
      }
      EndDialog(hwnd, 0);
      return TRUE;
  }

  return FALSE;
}

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
                      _In_opt_ HINSTANCE hPrevInstance, _In_ LPWSTR lpCmdLine,
                      _In_ int nCmdShow) {
  UNREFERENCED_PARAMETER(hPrevInstance);
  UNREFERENCED_PARAMETER(lpCmdLine);
  WSADATA wsaData;
  int res = WSAStartup(MAKEWORD(2, 2), &wsaData);
  if (res != 0) {
    std::cerr << "Can't Initialize winsock" << std::endl;
    return -1;
  }

  ZeroMemory(&hints, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;

  DialogBox(hInstance, MAKEINTRESOURCE(IDD_DIALOG), NULL, Dlg_Proc);
  closesocket(connectSocket);
  WSACleanup();
  return 0;
}
