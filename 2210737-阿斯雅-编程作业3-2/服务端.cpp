#include <iostream>
#include <cstdlib>
#include <cstring>
#include <WinSock2.h>
#include <ws2tcpip.h>
#include <windows.h> // 用于控制台颜色
#include <cstdio>
#include <vector>
#include <algorithm> // 用于排序和查找
#pragma comment(lib, "ws2_32.lib")   // socket库
#define _CRT_SECURE_NO_WARNINGS 
#define LOSS_PROBABILITY 0.1 // 丢包概率（10%）
#define SIZE 1024          // 消息最大长度
using namespace std;

// 全局变量初始化
int nextAckNum = -1;        // 期望的下一个序列号
int highestSeqNum = -1;     // 当前接收到的最高序列号
FILE* p;                    // 文件指针，用于接收文件
SOCKET serverSocket;        // 服务器端 socket
SOCKADDR_IN serverAddr;     // 服务器地址
SOCKADDR_IN sourceAddr;     // 客户端地址
int addrlen = sizeof(SOCKADDR); // 地址长度

bool con = true; // 连接状态

// 数据包结构体定义
struct data {
    int ack, syn, fin;
    unsigned short checksum;
    int seqnum, acknum;
    int dataLen;
    char data[SIZE];
} receiveData, sendData;

int datalen = sizeof(sendData); // 数据包长度


// 计算校验和的函数
unsigned short checksum(void* data, int len) {
    unsigned short* ptr = (unsigned short*)data;
    unsigned int sum = 0;
    unsigned short result;

    // 计算校验和的过程
    while (len > 1) {
        sum += *ptr++;
        if (sum & 0x10000) {
            sum = (sum & 0xFFFF) + 1; // 如果有溢出，则将溢出部分加回
        }
        len -= 2;
    }

    // 处理奇数个字节的情况
    if (len) {
        sum += *(unsigned char*)ptr;
        if (sum & 0x10000) {
            sum = (sum & 0xFFFF) + 1;
        }
    }

    // 反码求和
    return sum;
}

// 校验接收到的数据
bool checkReceivedData(const struct data& receivedData) {
    // 计算接收到数据的校验和
    unsigned short calculatedChecksum = checksum((void*)&receivedData, sizeof(receivedData));

    // 检查接收到的数据校验和是否与传送的校验和一致
    if (calculatedChecksum == unsigned short(65535)) {
        cout << "检验正确:seqnum=" << receivedData.seqnum << endl;
        return true;  // 校验和正确
    }
    else {
        cout << "校验错误" << endl;
        return false; // 校验和错误
    }
}

//三次握手建立连接
void Handshake_connection() {
    // 接收客户端的 SYN 数据包
    memset(&receiveData, 0, sizeof(receiveData));
    recvfrom(serverSocket, (char*)&receiveData, datalen, 0,
        (SOCKADDR*)&sourceAddr, &addrlen);

    // 检查 SYN 标志是否正确
    if (receiveData.syn != 1) {
        cout << "连接错误：未接收到有效的 SYN 数据包" << endl;
        return;
    }

    // 发送 SYN-ACK 数据包
    memset(&sendData, 0, sizeof(sendData));
    sendData.ack = 1;
    sendData.syn = 1;
    sendData.acknum = 1; // ACK 确认号
    sendto(serverSocket, (char*)&sendData, datalen, 0,
        (SOCKADDR*)&sourceAddr, addrlen);

    // 接收客户端的 ACK 数据包
    memset(&receiveData, 0, sizeof(receiveData));
    recvfrom(serverSocket, (char*)&receiveData, datalen, 0,
        (SOCKADDR*)&sourceAddr, &addrlen);

    // 检查 ACK 和 ACK 确认号是否正确
    if (!(receiveData.ack == 1 && receiveData.acknum == 1)) {
        cout << "连接错误：未接收到有效的 ACK 数据包" << endl;
        return;
    }

    // 握手成功，更新连接状态
    cout << "连接成功!" << endl;
    nextAckNum = -1;
    con = true;

    return;
}

//四次挥手断开连接
void wave_disconnect() {
    // 第一次挥手：客户端已发送 FIN 包，服务端确认
    cout << "第一次挥手成功" << endl;

    // 第二次挥手：服务端发送 ACK 确认包
    memset(&sendData, 0, sizeof(sendData));
    sendData.ack = 1;
    sendData.acknum = receiveData.seqnum + 1; // 对客户端的 FIN 进行确认
    sendto(serverSocket, (char*)&sendData, datalen, 0,
        (SOCKADDR*)&sourceAddr, addrlen);
    cout << "第二次挥手成功" << endl;

    // 第三次挥手：服务端发送 FIN 包
    memset(&sendData, 0, sizeof(sendData));
    sendData.fin = 1;
    sendData.seqnum = 1; // 服务端当前序列号
    sendto(serverSocket, (char*)&sendData, datalen, 0,
        (SOCKADDR*)&sourceAddr, addrlen);
    cout << "第三次挥手成功" << endl;

    // 第四次挥手：等待客户端发送的 ACK 包
    memset(&receiveData, 0, sizeof(receiveData));
    recvfrom(serverSocket, (char*)&receiveData, sizeof(receiveData), 0,
        (SOCKADDR*)&sourceAddr, &addrlen);

    // 检查客户端的 ACK 包内容
    if (receiveData.ack == 1) {
        cout << "第四次挥手成功，关闭连接" << endl;
    }

    else {
        cerr << "强制关闭：ACK 不匹配" << endl;
        return;
    }


    return;
}

void SetConsoleColor(int color) {
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    SetConsoleTextAttribute(hConsole, color);
}

//接收文件名
void receive_filename() {
    char fileName[50] = "";
    char filePath[100] = "receive/";

    memset(&receiveData, 0, sizeof(receiveData));
    recvfrom(serverSocket, (char*)&receiveData, sizeof(receiveData), 0,
        (SOCKADDR*)&sourceAddr, &addrlen);

    strcpy(fileName, receiveData.data);
    nextAckNum++; // 更新期望序列号
    highestSeqNum = 0;

    sprintf(filePath + strlen(filePath), "%s", fileName);
    p = fopen(filePath, "wb+");

    if (p) {
        cout << "已准备接收文件: " << fileName << ", 存储路径: " << filePath << endl;
    }
    else {
        cerr << "文件打开失败: " << filePath << endl;
    }

    // 发送文件名的 ACK
    memset(&sendData, 0, sizeof(sendData));
    sendData.ack = 1;
    sendData.acknum = nextAckNum;
    sendto(serverSocket, (char*)&sendData, sizeof(sendData), 0,
        (SOCKADDR*)&sourceAddr, addrlen);
}

//处理接收到的数据包
void process_received_data() {
    if (checkReceivedData(receiveData)) {
        if (receiveData.seqnum == nextAckNum) {
            // 按序接收的数据，直接写入文件
            fwrite(receiveData.data, receiveData.dataLen, 1, p);
            cout << "接收到按序数据包，序列号: " << receiveData.seqnum << ", 数据大小: "
                << receiveData.dataLen << " 字节" << endl;

            // 更新期望的下一个序列号
            nextAckNum++;

            // 发送累积 ACK，表示到 `nextAckNum - 1` 的数据均已收到
            memset(&sendData, 0, sizeof(sendData));
            sendData.ack = 1;
            //cout << nextAckNum;
            sendData.acknum = nextAckNum;
            sendto(serverSocket, (char*)&sendData, sizeof(sendData), 0, (SOCKADDR*)&sourceAddr, addrlen);
            cout << "发送累积确认: ACK=" << sendData.acknum << endl;
        }
        else if (receiveData.seqnum > nextAckNum) {
            // 乱序到达的数据包
            cout << "接收到乱序数据包，序列号: " << receiveData.seqnum << ", 抛弃" << endl;
            sendto(serverSocket, (char*)&sendData, sizeof(sendData), 0, (SOCKADDR*)&sourceAddr, addrlen);
        }
        else {
            ;
        }
    }
    else {
        //等待超时重传
    }
    
}

//主函数
int main() {
    // 初始化 Winsock
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    SetConsoleColor(10);
    cout << "Socket 初始化完成\n" << endl;
    SetConsoleColor(7);

    // 创建服务器端 Socket
    serverSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(8000);
    inet_pton(AF_INET, "127.0.0.1", &serverAddr.sin_addr);
    bind(serverSocket, (SOCKADDR*)&serverAddr, sizeof(serverAddr));

    SetConsoleColor(10);
    cout << "服务器端绑定完成，等待客户端连接...\n" << endl;
    SetConsoleColor(7);

    //建立连接
    Handshake_connection();

    //如果成功连接
    if (con) {
        receive_filename();
        while (true) {
            memset(&receiveData, 0, sizeof(receiveData));
            recvfrom(serverSocket, (char*)&receiveData, sizeof(receiveData), 0,
                (SOCKADDR*)&sourceAddr, &addrlen);

            if (receiveData.fin == 1) {
                cout << "接收到 FIN 数据包，传输完成" << endl;
                fclose(p);
                break;
            }

            // 处理数据包
            process_received_data();
            
        }
        //断开连接
        wave_disconnect();
       
    }

    
    //清理资源
    closesocket(serverSocket);
    WSACleanup();
    system("pause");
    return 0;
}
