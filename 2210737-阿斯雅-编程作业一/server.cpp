#include <iostream>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <thread>
#include <list>
#include <queue> // 包含队列头文件
#include <WinSock2.h>
#include <windows.h> // 用于控制台颜色
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")   // socket库

//保存客户信息结构体
struct client_info {
    SOCKADDR_IN clientAddr;
    SOCKET clientSocket;
};

using namespace std;

#define SIZE 4096 //消息最大长度
#define MaxClient 3 //最多在线用户

SOCKET serverSocket;         // 服务器端socket
SOCKADDR_IN serverAddr;      // 定义服务器地址
SOCKADDR_IN clientAddr;      // 定义客户端地址
list<client_info> clientList; //管理在线用户
queue<client_info> waitingQueue;  // 管理超员用户

int addrlen = sizeof(SOCKADDR); //结构体长度

// 设置控制台文本颜色
void SetConsoleColor(int color) {
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    SetConsoleTextAttribute(hConsole, color);
}


DWORD WINAPI handlerequest(LPVOID lpParameter) // 线程函数
{
    int receByt = 0;        // 接收到的字节数
    char RecvBuf[SIZE];     // 接收缓冲区
    char SendBuf[SIZE];     // 发送缓冲区
    client_info* clientInfo = (client_info*)lpParameter;
    SOCKET ClientSocket = clientInfo->clientSocket;

    // 循环接收信息
    while (true) {
        Sleep(100); // 延时100ms
        receByt = recv(ClientSocket, RecvBuf, sizeof(RecvBuf), 0); // 接收信息
        if (receByt > 0) { // 接收成功

            auto time_now = chrono::system_clock::to_time_t(chrono::system_clock::now());
            tm localTime;
            localtime_s(&localTime, &time_now);  // 使用 localtime_s 替代 localtime
            char timeStr[50];
            strftime(timeStr, sizeof(timeStr), "%Y-%m-%d--%H:%M:%S", &localTime); // 格式化时间

            SetConsoleColor(14); // 设置黄色字体

            cout << "客户 " << ClientSocket << " : " << RecvBuf << " --" << timeStr << endl;
            SetConsoleColor(7); // 恢复为默认颜色

            sprintf_s(SendBuf, sizeof(SendBuf), "%d: %s --%s ", ClientSocket, RecvBuf, timeStr); // 格式化发送信息
            
            // 将消息同步到所有聊天窗口
            for (auto& client : clientList) { 
                send(client.clientSocket, SendBuf, sizeof(SendBuf), 0);
            }
        }
        else { // 接收失败
            if (WSAGetLastError() == 10054) { // 客户端主动关闭连接
                auto time_now = chrono::system_clock::to_time_t(chrono::system_clock::now());
                tm localTime;
                localtime_s(&localTime, &time_now);  // 使用 localtime_s 替代 localtime
                char timeStr[50];
                strftime(timeStr, sizeof(timeStr), "%Y-%m-%d--%H:%M:%S", &localTime); // 格式化时间

                SetConsoleColor(12); // 设置红色字体
                cout << "客户 " << ClientSocket << " 已退出! 时间: " << timeStr << endl;
                SetConsoleColor(7); // 恢复默认颜色

                // 广播客户退出的信息
                char exitMsg[SIZE];
                sprintf_s(exitMsg, sizeof(exitMsg), "客户 %d 已退出房间,当前客户数量为 %d", ClientSocket, clientList.size()-1);
                for (auto& client : clientList) {
                    if (client.clientSocket != ClientSocket) { // 不发送给已经断开的客户端
                        send(client.clientSocket, exitMsg, sizeof(exitMsg), 0);
                    }
                }
                
                clientList.remove_if([ClientSocket](const client_info& client) {
                    return client.clientSocket == ClientSocket;
                    });
                closesocket(ClientSocket);

                SetConsoleColor(11); // 设置蓝色字体
                cout << "现在的用户数量为: " << clientList.size() << endl;
                SetConsoleColor(7); // 恢复默认颜色

                // 检查等待队列
                if (!waitingQueue.empty()) {
                    client_info nextClient = waitingQueue.front();
                    waitingQueue.pop();  // 取出队列中的下一个客户端

                    client_info newClient;
                    newClient.clientSocket = nextClient.clientSocket;
                    newClient.clientAddr = nextClient.clientAddr; 
                    clientList.push_back(newClient);

                    char enterMsg[SIZE];
                    sprintf_s(enterMsg, sizeof(enterMsg), "排队客户 %d 已进入房间，当前客户数量为 %d", nextClient.clientSocket, clientList.size());
                    for (auto& client : clientList) {
                        send(client.clientSocket, enterMsg, sizeof(enterMsg), 0);
                    }
                    cout << enterMsg << endl;

                    HANDLE Thread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)handlerequest, (LPVOID)&clientList.back(), 0, NULL);
                    CloseHandle(Thread);
                }

                return 0;
            }
            else {
                SetConsoleColor(12); // 设置红色字体
                cout << "接受失败: " << WSAGetLastError() << endl;
                SetConsoleColor(7); // 恢复默认颜色
                break;
            }
        }
    }
    return 0;
}

void main() {
    // 初始化winsock
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    SetConsoleColor(10); // 设置绿色字体
    cout << "socket初始化完成\n" << endl;
    SetConsoleColor(7); // 恢复默认颜色

    // 初始化服务端socket
    serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    // 初始化服务器端地址
    serverAddr.sin_family = AF_INET;         // 地址类型IPV4
    serverAddr.sin_port = htons(8000);       // 端口号
    inet_pton(AF_INET, "127.0.0.1", &serverAddr.sin_addr);

    SetConsoleColor(10); // 设置绿色字体
    cout << "服务器端地址初始化完成\n" << endl;
    SetConsoleColor(7); // 恢复默认颜色

    // 绑定服务器地址和服务器socket
    bind(serverSocket, (LPSOCKADDR)&serverAddr, sizeof(serverAddr));

    SetConsoleColor(10); // 设置绿色字体
    cout << "服务器端绑定完成\n" << endl;
    SetConsoleColor(7); // 恢复默认颜色

    // 监听
    listen(serverSocket, MaxClient);
    cout << "服务器开始监听，等待客户端连接...\n" << endl;

    // 接受消息
    while (true) {
        SOCKET client = accept(serverSocket, (sockaddr*)&clientAddr, &addrlen); // 接收客户端请求
        client_info newClient;
        newClient.clientSocket = client;
        newClient.clientAddr = clientAddr;
        if (clientList.size() < MaxClient) {
            // 广播新客户端进入的信息
            char enterMsg[SIZE];
            clientList.push_back(newClient); // 添加到客户端列表
            
            sprintf_s(enterMsg, sizeof(enterMsg), "客户 %d 已加入房间，当前客户数量为 %d", client, clientList.size());
            for (auto& client : clientList) {
                send(client.clientSocket, enterMsg, sizeof(enterMsg), 0);
            }
            cout << enterMsg << endl;
            HANDLE Thread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)handlerequest, (LPVOID)&clientList.back(), 0, NULL); // 创建线程
            CloseHandle(Thread);
        }
        else {
            
            char SendBuf[SIZE] = { "房间已满，已经进入排队等待" }; // 发送缓冲区
            SetConsoleColor(14); // 设置黄色字体
           
            SetConsoleColor(7); // 恢复默认颜色
            send(client, SendBuf, sizeof(SendBuf), 0);
           
            // 将超员的客户端放入等待队列
           
            cout << "房间已满，客户 " << client << " 加入排队队列" << endl;
            SetConsoleColor(7); // 恢复默认颜色
            waitingQueue.push(newClient);
        }
    }
    closesocket(serverSocket);
    WSACleanup();
}
