#include <iostream>
#include <WinSock2.h>
#include <ws2tcpip.h>
#include <cstring>
#include <windows.h>  // 用于 Sleep 函数
#pragma comment(lib, "ws2_32.lib")   // socket库
using namespace std;

#define SIZE 4096
SOCKET clientSocket; // 定义客户端socket
SOCKADDR_IN servAddr; // 定义服务器地址
#define _WINSOCK_DEPRECATED_NO_WARNINGS // 解决inet_pton错误

// ANSI escape codes for color formatting
#define RESET   "\033[0m"
#define BOLD    "\033[1m"
#define RED     "\033[31m"
#define GREEN   "\033[32m"
#define YELLOW  "\033[33m"
#define BLUE    "\033[34m"
#define MAGENTA "\033[35m"
#define CYAN    "\033[36m"
#define WHITE   "\033[37m"

// 显示欢迎信息并美化
void printWelcome() {
    cout << BOLD << GREEN;
    cout << "======================================" << endl;
    cout << "       欢迎来到" << CYAN << " ASY" << RESET << GREEN << " 聊天室!      " << endl;
    cout << "======================================" << endl;
    cout << "  发送消息或输入 " << YELLOW << "END" << GREEN << " 退出房间   " << endl;
    cout << "--------------------------------------" << endl;
    cout << RESET;
}

// 接收消息并美化输出
DWORD WINAPI recvThread() {
    while (true) {
        char buffer[SIZE] = {};
        if (recv(clientSocket, buffer, sizeof(buffer), 0) > 0) {
            if (strcmp(buffer, "房间已满，已经进入排队等待") == 0) {
                cout << RED << buffer << RESET << endl;
                
                
            }
            else{ cout << CYAN << "[收到消息]: " << RESET << buffer << endl; }
            
        }
        else if (recv(clientSocket, buffer, sizeof(buffer), 0) < 0) {
            cout << RED << "[错误]: 失去连接!\n" << RESET << endl;
            break;
        }
    }
    Sleep(100); // 延时100ms
    return 0;
}

int main() {
    // 初始化winsock
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    // 初始化客户端socket
    clientSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    // 初始化服务端地址
    servAddr.sin_family = AF_INET;           // 地址类型IPV4
    servAddr.sin_port = htons(8000);         // 转换成网络字节序
    inet_pton(AF_INET, "127.0.0.1", &servAddr.sin_addr); //IP地址

    // 连接服务器
    connect(clientSocket, (SOCKADDR*)&servAddr, sizeof(SOCKADDR));
    printWelcome();

    // 创建线程接收消息
    CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)recvThread, NULL, 0, 0);

    // 发送消息
    char Mes[SIZE] = {};
    while (true) {
        
        cin.getline(Mes, sizeof(Mes));
        if (strcmp(Mes, "END") == 0) {
            break;
        }
        send(clientSocket, Mes, sizeof(Mes), 0);  // 发送消息
    }

    // 关闭socket
    closesocket(clientSocket);
    WSACleanup();
    cout << RED << "已退出聊天室，再见！" << RESET << endl;
    return 0;
}
