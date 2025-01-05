#include <iostream>
#include <WinSock2.h>
#include <ws2tcpip.h>
#include <cstring>
#include <windows.h>
#include <cstdio>
#include <thread>
#include <condition_variable>
#include <atomic>
#include <vector>
#include <unordered_map>
#include <future>
#include <map>

#define _CRT_SECURE_NO_WARNINGS 
#define SIZE 1024
#define TIMEOUT 200
#pragma comment(lib, "ws2_32.lib")

// 全局变量
std::mutex sendl;               // 发送队列的锁
std::mutex timeout;             // 超时锁
std::condition_variable sendCv; // 发送条件变量
using namespace std;

// 套接字及相关参数
SOCKET clientSocket;
SOCKADDR_IN servAddr;
int addrlen = sizeof(SOCKADDR);
int nextseqno = 0;              // 下一个发送的数据包序列号
int datanum = 0;                // 数据包数量
int base = 0;                   // 滑动窗口的起始序列号
int timeoutid = -1;             // 超时的数据包序列号
bool stoptimer = false;         // 是否停止定时器
int coun = 0;                   // 丢包计数器
bool con = false;               // 是否成功连接
FILE* p;                        // 文件指针

// 数据包结构体
struct data {
    int ack, syn, fin;               // 标志位
    unsigned short checksum;         // 校验和
    int seqnum, acknum;              // 序列号、确认号
    int dataLen;                     // 数据长度
    char data[SIZE];                 // 数据内容
} receiveData, sendData, npacket;

vector<struct data> window;         // 发送窗口（存储要发送的数据包）
int win_size = 0;                   // 滑动窗口大小
std::map<int, std::chrono::steady_clock::time_point> packetTimers;  // 包的定时器


// 判断是否可以发送数据包
bool cansend() {
    if (nextseqno - base < win_size) {
        return true;
    }
    else {
        return false;
    }
}

// 传输文件名
void trans_filename() {
    char fileName[50] = "";
    printf("请输入文件名:\n");
    scanf("%s", fileName);

    if (!(p = fopen(fileName, "rb+"))) {
        printf("错误：无法打开文件\n");
        return;
    }

    memset(&sendData, 0, sizeof(sendData));
    strcpy(sendData.data, fileName);
    sendData.seqnum = nextseqno;
    sendData.dataLen = strlen(fileName);

    sendto(clientSocket, (char*)&sendData, sizeof(sendData), 0, (SOCKADDR*)&servAddr, sizeof(SOCKADDR));

    memset(&receiveData, 0, sizeof(receiveData));
    recvfrom(clientSocket, (char*)&receiveData, sizeof(receiveData), 0, (SOCKADDR*)&servAddr, &addrlen);

    nextseqno++;
    cout << "成功打开文件！";
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
    con = true;

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

//接收线程
void receiveack() {
    while (true) {
        memset(&receiveData, 0, sizeof(sendData));
        recvfrom(clientSocket, (char*)&receiveData, sizeof(receiveData), 0,
            (SOCKADDR*)&servAddr, &addrlen);
        if (receiveData.ack == 1 && receiveData.acknum > base) {
            sendl.lock();
            base= receiveData.acknum;
            cout << "收到累积 ACK：acknum=" << receiveData.acknum << endl;
            if (base >= datanum) {
                sendl.unlock();
                stoptimer = true;
                return;
            }
            else {
                for (int i = 0; i < base; i++) {
                    if (packetTimers.find(i) != packetTimers.end()) {
                        packetTimers.erase(i);
                    }
                }
               
            }

            sendl.unlock();
            sendCv.notify_one();
            timeout.lock();
            timeoutid = -1;
            timeout.unlock();

        }
        else {
            cout << "收到重复累积 ACK：acknum=" << receiveData.acknum << endl;
            timeout.lock();
            timeoutid = base;
            timeout.unlock();
        }
    }
}

//重传线程
void handelresend() {
    while (!stoptimer) {
        if (timeoutid != -1) {
            timeout.lock();
            auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration_cast<std::chrono::milliseconds>(now - packetTimers[timeoutid]).count() > TIMEOUT) {
                for (int i = timeoutid; i < timeoutid + win_size; i++) {
                    if (i < datanum) {
                        cout << "重传 seqnum=" << window[i].seqnum << endl;
                        sendto(clientSocket, (char*)&window[i], sizeof(sendData), 0,
                            (SOCKADDR*)&servAddr, sizeof(SOCKADDR));
                        packetTimers[i] = std::chrono::steady_clock::now();


                    }

                }
            }
            timeout.unlock();
        }
        Sleep(100);
    }
    return;
}

int main() {

    long long sum_byte = 0;
    srand(time(nullptr)); // 随机数种子
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    clientSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

    servAddr.sin_family = AF_INET;
    servAddr.sin_port = htons(8000);
    inet_pton(AF_INET, "127.0.0.1", &servAddr.sin_addr);

    Handshake_connection();
    if (con) {
        clock_t start_time = clock();

        cout << "请输入窗口大小：" << endl;
        cin >> win_size;
        trans_filename();

        fseek(p, 0, SEEK_END);
        long long fileLen = ftell(p);
        fseek(p, 0, SEEK_SET);

        printf("开始传输文件, 文件大小为 %lld 字节\n", fileLen);
        sum_byte += fileLen;
        nextseqno = 0;
        while (fileLen > 0) {
            memset(&npacket, 0, sizeof(npacket));
            fread(npacket.data, 1, min(SIZE, fileLen), p);
            npacket.dataLen = min(SIZE, fileLen);
            npacket.seqnum = nextseqno;
            nextseqno++;
            fileLen -= npacket.dataLen;
            window.push_back(npacket);
            datanum++;
        }

        nextseqno = 0;
        thread receiveThread(receiveack);
        thread timeThread(handelresend);

        while (nextseqno < datanum) {
            unique_lock<std::mutex> lock(sendl);
            a
            // 等待可以发送
            sendCv.wait(lock, [] { return cansend(); });
            struct data& currentPacket = window[nextseqno];
            setChecksum(&currentPacket);
            packetTimers[nextseqno] = std::chrono::steady_clock::now();  // 为发送分组设置定时器起始时间
            if (coun == 50) {
                coun = 0;
                cout << "丢包 seqnum=" << currentPacket.seqnum << endl;
            }
            else {
                coun++;
                cout << "发送 seqnum=" << currentPacket.seqnum << endl;
                sendto(clientSocket, (char*)&currentPacket, sizeof(sendData), 0,
                    (SOCKADDR*)&servAddr, sizeof(SOCKADDR));

            }

            nextseqno++;
            if (nextseqno >= datanum) {
                break;
            }

        }
        receiveThread.join();
        timeThread.join();
        stoptimer = true;
        memset(&sendData, 0, sizeof(sendData));
        sendData.fin = 1;
        sendData.seqnum = nextseqno;
        sendto(clientSocket, (char*)&sendData, sizeof(sendData), 0, (SOCKADDR*)&servAddr, sizeof(SOCKADDR));
        wave_disconnect();

        clock_t end_time = clock();

        // 计算总耗时
        double elapsed_time = (double)(end_time - start_time) / CLOCKS_PER_SEC;

        // 打印统计信息
        cout << "文件传输完成！" << endl;
        cout << "总传输字节数：" << sum_byte << " 字节" << endl;
        cout << "总耗时：" << elapsed_time << " 秒" << endl;
        cout << "平均吞吐率：" << sum_byte / elapsed_time << " 字节/秒" << endl;

    }


    closesocket(clientSocket);
    WSACleanup();

    system("pause");
    return 0;

}