#include "include/RequestData.h"
#include "include/Epoll.h"
#include "include/ThreadPool.h"
#include "include/util.h"
#include <sys/epoll.h>
#include <queue>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <cstdlib>
#include <iostream>
#include <vector>
#include <unistd.h>
#include <memory>

using namespace std;

// epoll最大事件数
static const int MAX_EVENTS = 5000;
// 最大监听数量
static const int MAX_LISTEN = 1024;
// 线程池线程数量
const int THREADPOOL_THREAD_NUM = 4;
// 任务队列任务数量
const int QUEUE_SIZE = 65535;

const int PORT = 8888;
const int ASK_STATIC_FILE = 1;
const int ASK_IMAGE_STITCH = 2;

const int TIMER_TIME_OUT = 500;

// 创建监听端口并绑定
int socketBindLiten(int port)
{
    // 检查port值，取正确区间范围
    if (port < 1024 || port > 65535)
        return -1;

    // 创建socket(IPv4 + TCP)，返回监听描述符
    int listen_fd = 0;
    if((listen_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
        return -1;

    // 消除bind时"Address already in use"错误
    int optval = 1;
    if(setsockopt(listen_fd, SOL_SOCKET,  SO_REUSEADDR, &optval, sizeof(optval)) == -1)
        return -1;

    // 设置服务器IP和Port，和监听描述副绑定
    struct sockaddr_in server_addr;
    bzero((char*)&server_addr, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons((unsigned short)port);
    if(bind(listen_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1)
        return -1;

    // 开始监听，最大等待队列长为LISTENQ
    if(listen(listen_fd, MAX_LISTEN) == -1)
        return -1;

    // 无效监听描述符
    if(listen_fd == -1)
    {
        close(listen_fd);
        return -1;
    }
    return listen_fd;
}


int main()
{
    #ifndef _PTHREADS
        printf("_PTHREADS is not defined !\n");
    #endif
    handleSigpipe();
    // epollfd初始化
    if (Epoll::epollInit(MAX_EVENTS, MAX_LISTEN) < 0)
    {
        perror("epollInit failed");
        return 1;
    }
    // 创建线程池
    if (ThreadPool::ThreadPoolCreate(THREADPOOL_THREAD_NUM, QUEUE_SIZE) < 0)
    {
        printf("ThreadPool create failed\n");
        return 1;
    }
    // 创建监听文件描述符并且绑定地址
    int listen_fd = socketBindLiten(PORT);
    if (listen_fd < 0) 
    {
        perror("socket bind failed");
        return 1;
    }
    // 设置socket非阻塞
    if (setNonBlocking(listen_fd) < 0)
    {
        perror("set socket non block failed");
        return 1;
    }
    shared_ptr<RequestData> request(new RequestData());
    request->setFd(listen_fd);
    // 首先将listen_fd添加到epoll描述符中
    if (Epoll::epollAdd(listen_fd, request, EPOLLIN | EPOLLET) < 0)
    {
        perror("epoll add error");
        return 1;
    }
    
    // 主线程监听文件描述符, 并将任务添加到任务队列中
    while (true)
    {
        Epoll::epollWait(listen_fd, MAX_EVENTS, -1);
    }
    return 0;
}