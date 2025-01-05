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
#define _CRT_SECURE_NO_WARNINGS
#define LOSS_PROBABILITY 0.1 // 丢包概率（10%）
#define SIZE 20480 //消息最大长度
using namespace std;
int filecount = 0;
bool con = false;
FILE* p;
SOCKET serverSocket;         // 服务器端socket
SOCKADDR_IN serverAddr;      // 定义服务器地址
SOCKADDR_IN sourceAddr;      // 定义客户端地址

int nextAckNum = 0;

struct data {
    int ack, syn, fin;
    unsigned short checksum;
    int seqnum, acknum;
    int dataLen;
    char data[SIZE];
} receiveData, sendData;

int datalen = sizeof(sendData);

int addrlen = sizeof(SOCKADDR); //结构体长度

// 设置控制台文本颜色
void SetConsoleColor(int color) {
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    SetConsoleTextAttribute(hConsole, color);
}

// 模拟丢包的发送函数
void send_data_with_loss_simulation() {
    // 模拟丢包，根据丢包概率决定是否丢包
    float randomValue = (float)rand() / RAND_MAX;
    if (randomValue < LOSS_PROBABILITY) {
        cout << "随机丢包: acknum=" << sendData.acknum << endl;
        return; // 丢包，不发送
    }
    // 如果没有丢包，则正常发送数据
    sendto(serverSocket, (char*)&sendData, datalen, 0, (SOCKADDR*)&sourceAddr, addrlen);
    cout << "发送数据包：acknum="<< sendData.acknum<< endl;
}

//接受文件名
void receive_filename() {
    // 初始化文件名和文件路径的缓冲区
    char fileName[50] = "";
    char filePath[50] = "";

    // 接收文件名数据包
    memset(&receiveData, 0, sizeof(receiveData));
    recvfrom(serverSocket, (char*)&receiveData, datalen, 0,
        (SOCKADDR*)&sourceAddr, &addrlen);

    // 将接收到的数据解析为文件名
    strcpy(fileName, receiveData.data);

    // 增加 ACK 确认号并发送确认包
    nextAckNum++;
    memset(&sendData, 0, sizeof(sendData));
    sendData.ack = 1;
    sendData.syn = 1;
    sendData.acknum = nextAckNum;
    sendData.seqnum = 1;
    sendto(serverSocket, (char*)&sendData, datalen, 0,
        (SOCKADDR*)&sourceAddr, addrlen);

    // 构造文件存储路径并打开文件
    sprintf(filePath, "receive\\%s", fileName);
    p = fopen(filePath, "wb+"); // 以二进制写入方式打开文件

    // 调试输出信息
    if (p) {
        cout << "已准备接收文件: " << fileName << ", 存储路径: " << filePath << endl;
    }
    else {
        cerr << "文件打开失败: " << filePath << endl;
    }
}


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
    if (calculatedChecksum == unsigned short(65535)){
        cout << "检验正确:seqnum="<< receivedData.seqnum << endl;
        return true;  // 校验和正确
    }
    else {
        cout << "校验错误"<<endl;
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
    nextAckNum = 1;
    con = true;

    // 接收文件数量
    char file_count[50] = "";
    memset(&receiveData, 0, sizeof(receiveData));
    recvfrom(serverSocket, (char*)&receiveData, datalen, 0,
        (SOCKADDR*)&sourceAddr, &addrlen);
    strcpy(file_count, receiveData.data);

    // 发送确认 ACK 数据包
    nextAckNum++;
    memset(&sendData, 0, sizeof(sendData));
    sendData.ack = 1;
    sendData.syn = 1;
    sendData.acknum = nextAckNum;
    sendData.seqnum = 1;
    sendto(serverSocket, (char*)&sendData, datalen, 0,
        (SOCKADDR*)&sourceAddr, addrlen);

    // 将文件数量从字符转为整数
    filecount = int(file_count[0] - '0'); // 假设文件数量为个位数
    cout << "将收到 " << filecount << " 个文件" << endl;

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
    if (receiveData.ack==1) {
        cout << "第四次挥手成功，关闭连接" << endl;
    }
    
    else {
        cerr << "强制关闭：ACK 不匹配" << endl;
        return;
    }
   

    return;
}


void main() {
    // 初始化 Winsock
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    // 设置绿色字体并提示 Socket 初始化完成
    SetConsoleColor(10);
    cout << "Socket 初始化完成\n" << endl;
    SetConsoleColor(7); // 恢复默认颜色

    // 初始化服务端 Socket
    serverSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

    // 初始化服务器端地址
    serverAddr.sin_family = AF_INET;         // 地址类型：IPv4
    serverAddr.sin_port = htons(8000);       // 设置端口号，转换为网络字节序
    inet_pton(AF_INET, "127.0.0.1", &serverAddr.sin_addr); // 设置 IP 地址

    // 设置绿色字体并提示服务器地址初始化完成
    SetConsoleColor(10);
    cout << "服务器端地址初始化完成\n" << endl;
    SetConsoleColor(7); // 恢复默认颜色

    // 绑定服务器地址和 Socket
    bind(serverSocket, (LPSOCKADDR)&serverAddr, sizeof(serverAddr));

    // 设置绿色字体并提示绑定完成
    SetConsoleColor(10);
    cout << "服务器端绑定完成\n" << endl;
    SetConsoleColor(7); // 恢复默认颜色

    int count = 0; // 文件接收计数器

    // 执行握手连接
    Handshake_connection();

    // 文件接收循环
    for (int i = 0; i < filecount; i++) {
        receive_filename(); // 接收文件名
        while (1) {
            // 清空接收数据结构并接收数据包
            memset(&receiveData, 0, sizeof(receiveData));
            recvfrom(serverSocket, (char*)&receiveData, datalen, 0,
                (SOCKADDR*)&sourceAddr, &addrlen);

            // 如果接收到结束标志（FIN），关闭文件并退出循环
            if (receiveData.fin == 1) {
                fclose(p);
                break;
            }

            // 检查接收数据是否有效
            if (checkReceivedData(receiveData)) {
                // 如果接收到期望的序列号
                if (receiveData.seqnum == nextAckNum) {
                    nextAckNum++; // 更新期望序列号
                    cout << "接收到数据大小：" << receiveData.dataLen << " 字节" << endl;

                    // 将数据写入文件
                    fwrite(receiveData.data, receiveData.dataLen, 1, p);

                    // 准备 ACK 数据包
                    memset(&sendData, 0, sizeof(sendData));
                    sendData.seqnum = 1;
                    sendData.ack = 1;
                    sendData.acknum = nextAckNum;

                    // 发送 ACK，模拟丢包
                    send_data_with_loss_simulation();
                }
                else {
                    // 如果收到重复数据包，重新发送 ACK
                    sendto(serverSocket, (char*)&sendData, datalen, 0,
                        (SOCKADDR*)&sourceAddr, addrlen);
                    cout << "发送重复 ACK：acknum=" << sendData.acknum << endl;
                }
            }
        }
    }

    // 如果连接状态为已连接，则执行断开连接操作
    if (con) {
        wave_disconnect();
    }

    // 关闭 Socket 并清理 Winsock 资源
    closesocket(serverSocket);
    WSACleanup();

    system("pause"); // 暂停程序，等待用户操作
}

    
   

