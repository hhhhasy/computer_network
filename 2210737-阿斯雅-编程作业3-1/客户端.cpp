// 包含必要的头文件，用于实现Socket通信、线程同步、文件操作等功能
#include <iostream>       // 输入输出流
#include <WinSock2.h>     // Windows Sockets 2 API
#include <ws2tcpip.h>     // 扩展的Windows Sockets功能（如IPv6支持）
#include <cstring>        // 字符串操作函数
#include <windows.h>      // Windows操作系统API
#include <cstdio>         // C风格输入输出函数
#include <thread>         // C++多线程支持
#include <condition_variable> // 条件变量，用于线程同步
#include <atomic>         // 原子操作

#pragma comment(lib, "ws2_32.lib") // 链接Windows Sockets 2库，必需的Socket库

using namespace std; // 使用标准命名空间

// 关闭不安全函数的警告，适用于VS编译器
#define _CRT_SECURE_NO_WARNINGS 

// 定义常量
#define SIZE 20480             // 数据缓冲区大小
#define TIMEOUT 500            // 超时时间设置为500ms
#define MAX_RETRIES 10         // 最大重传次数
#define LOSS_PROBABILITY 0.1   // 模拟丢包概率为10%

// 全局变量定义
int filecount = 0; // 文件计数器，用于记录发送的文件数量

SOCKET clientSocket;      // 客户端Socket句柄，用于通信
SOCKADDR_IN servAddr;     // 服务器地址结构

int addrlen = sizeof(SOCKADDR); // 地址长度
int nextseqno = 0;             // 下一个发送的序列号
bool conect = false;           // 连接状态标志

FILE* p; // 文件指针，用于操作文件

// 原子变量和同步工具
atomic<bool> isAckReceived(false);  // 原子变量，标志是否收到ACK
condition_variable cv;             // 条件变量，用于线程间通知和同步
mutex cvMutex;                     // 互斥锁，保护条件变量访问
bool stopTimer = false;            // 定时器线程退出标志
HANDLE mutexHandle;  // Windows互斥量句柄，用于多线程保护
bool isack = false; // ACK标志，用于表示当前是否收到确认包

//数据包结构体
struct data {
    int ack, syn, fin;
    unsigned short checksum;
    int seqnum, acknum;
    int dataLen;
    char data[SIZE];
} receiveData, sendData;

//传输文件名
void trans_filename() {
    char fileName[50] = "";
    printf("请输入文件名:\n");
    scanf("%s", fileName);

    // 尝试打开文件
    if (!(p = fopen(fileName, "rb+"))) {
        printf("错误：无法打开文件\n");
        return;
    }

    // 清空发送数据结构并设置文件名
    memset(&sendData, 0, sizeof(sendData));
    strcpy(sendData.data, fileName);
    sendData.seqnum = nextseqno; // 设置序列号
    sendData.dataLen = strlen(fileName); // 设置文件名长度

    // 发送文件名
    sendto(clientSocket, (char*)&sendData, sizeof(sendData), 0, (SOCKADDR*)&servAddr, sizeof(SOCKADDR));

    // 接收服务器的响应
    memset(&receiveData, 0, sizeof(receiveData));
    recvfrom(clientSocket, (char*)&receiveData, sizeof(receiveData), 0, (SOCKADDR*)&servAddr, &addrlen);

    // 更新序列号
    nextseqno++;
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

    result = (unsigned short)~sum; // 反码求和
    return result;
}

// 发送数据之前设置校验和
void setChecksum(struct data* sendData) {
    sendData->checksum = 0;  // 先清空校验和字段
    sendData->checksum = checksum((void*)sendData, sizeof(*sendData));  // 计算并设置校验和
}

// 模拟丢包的发送函数
void send_data_with_loss_simulation() {
    // 模拟丢包，根据丢包概率决定是否丢包
    float randomValue = (float)rand() / RAND_MAX;
    if (randomValue < LOSS_PROBABILITY) {
        cout << "模拟丢包：seqnum=" << sendData.seqnum << endl;
        return; // 丢包，不发送
    }
    // 如果没有丢包，则正常发送数据
    sendto(clientSocket, (char*)&sendData, sizeof(sendData), 0, (SOCKADDR*)&servAddr, sizeof(SOCKADDR));
    cout << "发送数据包：seqnum=" << sendData.seqnum << endl;
}

// 定时器线程函数
void timerThreadFunc() {
    unique_lock<mutex> lock(cvMutex);
    while (!stopTimer) {
        // 等待 ACK 或超时
        if (cv.wait_for(lock, chrono::milliseconds(TIMEOUT), [] { return isAckReceived.load(); })) {
            break; // 收到 ACK，退出定时器
        }
        // 超时，重新发送数据
        cout << "超时，重新发送数据，序列号 seqnum=" << sendData.seqnum << endl;
        sendto(clientSocket, (char*)&sendData, sizeof(sendData), 0, (SOCKADDR*)&servAddr, sizeof(SOCKADDR));
    }
}

// 握手建立连接
void Handshake_connection() {
    // 第一次握手：客户端发送 SYN 包
    memset(&sendData, 0, sizeof(sendData));
    sendData.syn = 1;
    sendto(clientSocket, (char*)&sendData, sizeof(sendData), 0, (SOCKADDR*)&servAddr, sizeof(SOCKADDR));

    // 第二次握手：接收服务器返回的 SYN+ACK
    memset(&receiveData, 0, sizeof(receiveData));
    recvfrom(clientSocket, (char*)&receiveData, sizeof(receiveData), 0, (SOCKADDR*)&servAddr, &addrlen);
    if (!(receiveData.ack == 1 && receiveData.syn == 1 && receiveData.acknum == 1)) {
        cout << "连接错误：未收到正确的 SYN+ACK 包" << endl;
        return;
    }

    // 第三次握手：客户端发送 ACK
    memset(&sendData, 0, sizeof(sendData));
    sendData.ack = 1;
    sendData.acknum = 1;
    sendData.seqnum = 1;
    nextseqno = 1;
    sendto(clientSocket, (char*)&sendData, sizeof(sendData), 0, (SOCKADDR*)&servAddr, sizeof(SOCKADDR));

    cout << "连接成功！" << endl;
    conect = true;

    // 发送文件数量
    char file_count[50] = "";
    printf("请输入文件个数：\n");
    scanf("%s", file_count);

    memset(&sendData, 0, sizeof(sendData));
    strcpy(sendData.data, file_count);
    sendData.seqnum = nextseqno;
    sendData.dataLen = strlen(file_count);
    sendto(clientSocket, (char*)&sendData, sizeof(sendData), 0, (SOCKADDR*)&servAddr, sizeof(SOCKADDR));

    // 接收服务器确认
    memset(&receiveData, 0, sizeof(receiveData));
    recvfrom(clientSocket, (char*)&receiveData, sizeof(receiveData), 0, (SOCKADDR*)&servAddr, &addrlen);
    nextseqno++;

    filecount = int(file_count[0] - '0');
}

//挥手断开连接
void wave_disconnect() {
    // 第一次挥手：客户端发送 FIN 包
    //memset(&sendData, 0, sizeof(sendData));
    //sendData.fin = 1;
    //sendData.seqnum = 1; // 客户端当前序列号
    //sendto(clientSocket, (char*)&sendData, sizeof(sendData), 0, (SOCKADDR*)&servAddr, sizeof(SOCKADDR));
    cout << "第一次挥手：客户端发送 FIN 包" << endl;

    // 第二次挥手：等待服务器返回 ACK
    memset(&receiveData, 0, sizeof(receiveData));
    recvfrom(clientSocket, (char*)&receiveData, sizeof(receiveData), 0, (SOCKADDR*)&servAddr, &addrlen);
    if (receiveData.ack == 1) {
        cout << "第二次挥手：收到服务器的 ACK，acknum=" << receiveData.acknum << endl;
    }
    else {
        cerr << "连接错误：未收到服务器正确的 ACK 包" << endl;
        return;
    }

    // 第三次挥手：等待服务器发送 FIN
    memset(&receiveData, 0, sizeof(receiveData));
    recvfrom(clientSocket, (char*)&receiveData, sizeof(receiveData), 0, (SOCKADDR*)&servAddr, &addrlen);
    if (receiveData.fin == 1) {
        cout << "第三次挥手：收到服务器的 FIN，seqnum=" << receiveData.seqnum << endl;
    }
    else {
        cerr << "连接错误：未收到服务器的 FIN 包" << endl;
        return;
    }

    // 第四次挥手：客户端发送 ACK
    memset(&sendData, 0, sizeof(sendData));
    sendData.ack = 1;
    sendData.acknum = receiveData.seqnum + 1; // 对服务器的 FIN 进行确认
    sendto(clientSocket, (char*)&sendData, sizeof(sendData), 0, (SOCKADDR*)&servAddr, sizeof(SOCKADDR));
    cout << "第四次挥手：客户端发送 ACK" << endl;
    cout << "连接已成功关闭！" << endl;
    
}

// 发送数据并启动定时器
void send_tcp() {
    isAckReceived.store(false); // 初始化 ACK 标志
    setChecksum(&sendData); // 设置校验和
    send_data_with_loss_simulation(); // 模拟数据发送（包含丢包情况）

    stopTimer = false;
    thread timerThread(timerThreadFunc); // 启动定时器线程

    while (true) {
        // 接收来自服务器的 ACK 数据
        recvfrom(clientSocket, (char*)&receiveData, sizeof(receiveData), 0, (SOCKADDR*)&servAddr, &addrlen);

        if (receiveData.ack == 1 && receiveData.acknum == sendData.seqnum + 1) {
            cout << "收到 ACK，确认号 acknum=" << receiveData.acknum << endl;
            isAckReceived.store(true); // 更新 ACK 状态
            cv.notify_one(); // 通知定时器线程
            break;
        }
    }
    // 停止定时器线程
    stopTimer = true;
    cv.notify_one();  // 唤醒被阻塞的定时器线程
    timerThread.join(); // 等待定时器线程结束
}

// 主函数
int main() {
    long long sum_byte = 0;

    // 初始化 Winsock
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    // 初始化客户端 socket
    clientSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

    // 初始化服务端地址
    servAddr.sin_family = AF_INET;            // 地址类型 IPV4
    servAddr.sin_port = htons(8000);          // 转换成网络字节序
    inet_pton(AF_INET, "127.0.0.1", &servAddr.sin_addr);  // 设置服务端 IP 地址

    // 获取起始时间
    clock_t start_time = clock();

    // 握手建立连接
    Handshake_connection();

    // 文件传输过程
    for (int i = 0; i < filecount; i++) {
        trans_filename(); // 传输文件名

        // 文件大小处理
        fseek(p, 0, SEEK_END);
        long long fileLen = ftell(p); // 获取文件长度
        fseek(p, 0, SEEK_SET);

        printf("开始传输文件, 文件大小为 %lld 字节\n", fileLen);
        sum_byte += fileLen;

        // 传输文件内容
        while (fileLen > 0) {
            memset(&sendData, 0, sizeof(sendData));
            fread(&sendData.data, 1, min(SIZE, fileLen), p);  // 读取文件内容到发送数据
            sendData.dataLen = min(SIZE, fileLen);  // 数据长度
            sendData.seqnum = nextseqno;  // 设置序列号
            send_tcp(); // 发送数据并处理超时重传

            nextseqno++;  // 更新序列号
            fileLen -= sendData.dataLen; // 更新剩余文件长度
        }

        // 输出文件传输完成信息
        cout << "第 " << i + 1 << " 个文件传输完成" << endl;

        fclose(p);  // 关闭文件

        // 发送文件结束标志 (FIN)
        memset(&sendData, 0, sizeof(sendData));
        sendData.fin = 1;
        sendData.seqnum = nextseqno;
        sendto(clientSocket, (char*)&sendData, sizeof(sendData), 0, (SOCKADDR*)&servAddr, sizeof(SOCKADDR));
    }

    // 如果连接成功，进行断开连接
    if (conect) {
        wave_disconnect();  // 断开连接

        // 获取结束时间
        clock_t end_time = clock();

        // 计算总耗时
        double elapsed_time = (double)(end_time - start_time) / CLOCKS_PER_SEC;

        // 打印统计信息
        cout << "文件传输完成！" << endl;
        cout << "总传输字节数：" << sum_byte << " 字节" << endl;
        cout << "总耗时：" << elapsed_time << " 秒" << endl;
        cout << "平均吞吐率：" << sum_byte / elapsed_time << " 字节/秒" << endl;
    }

    // 释放资源
    closesocket(clientSocket);
    WSACleanup();

    system("pause");
    return 0;
}

