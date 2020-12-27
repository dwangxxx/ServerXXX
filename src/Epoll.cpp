#include "Epoll.h"
#include "ThreadPool.h"
#include "util.h"
#include <sys/epoll.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <queue>
#include <deque>
#include <iostream>
#include <arpa/inet.h>

using namespace std;

int TIMER_TIME_OUT = 500;

// 定义
epoll_event *Epoll::events;
Epoll::reqPtr Epoll::requests[MAX_FD_SIZE];
int Epoll::epoll_fd = 0;
const std::string Epoll::path = "/";
TimerManager Epoll::timer_manager;

// epoll初始化
int Epoll::epollInit(int max_events, int max_listen)
{
    epoll_fd = epoll_create(max_listen + 1);
    if (epoll_fd = -1)
        return -1;
    events = new epoll_event[max_events];
    return 0;
}

// 注册新描述符 epoll_ctl
int Epoll::epollAdd(int fd, reqPtr request, __uint32_t events)
{
    struct epoll_event event;
    event.data.fd = fd;
    event.events = events;
    requests[fd] = request;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &event) < 0)
    {
        perror("epoll add error");
        return -1;
    }
    return 0;
}

// 修改描述符状态 epoll_ctl
int Epoll::epollMod(int fd, reqPtr request, __uint32_t events)
{
    struct epoll_event event;
    event.data.fd = fd;
    event.events = events;
    // 更新数组
    requests[fd] = request;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &event) < 0)
    {
        perror("epoll mod error");
        return -1;
    }
    return 0;
}

// 将fd从epoll中删除 epoll_ctl
int Epoll::epollDel(int fd, __uint32_t events)
{
    struct epoll_event event;
    event.data.fd = fd;
    event.events = events;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, &event) < 0)
    {
        perror("epoll del error");
        return -1;
    }
    return 0;
}

// 等待活跃事件 epoll_wait
void Epoll::epollWait(int listen_fd, int max_events,int timeout)
{
    int event_num = epoll_wait(epoll_fd, events, max_events, timeout);
    if (event_num < 0)
        perror("epoll wait error");
    // 获取文件描述符事件
    std::vector<reqPtr> req_data = getEvents(listen_fd, event_num, path);
    // 将事件放到任务队列中
    if (req_data.size() > 0)
    {
        for (auto &req : req_data)
        {
            if (ThreadPool::ThreadPoolAdd(req) < 0)
            {
                // 如果线程池满了或者关闭了，则抛弃本次监听到的请求
                break;
            }
        }
    }

    timer_manager.eventHandler();
}

// 接受连接 accept
void Epoll::acceptConn(int listen_fd, int epoll_fd, const std::string path)
{
    struct sockaddr_in client_addr;
    memset(&client_addr, 0, sizeof(client_addr));
    socklen_t clientaddr_len = sizeof(client_addr);
    int accept_fd = 0;
    // 一次接受所有的连接
    while (accept_fd = accept(listen_fd, (struct sockaddr*) &client_addr, &clientaddr_len) > 0)
    {
        cout << inet_ntoa(client_addr.sin_addr) << endl;
        cout << ntohs(client_addr.sin_port) << endl;
        // TCP的保活机制默认是关闭的
        // 如果超过了最大文件描述符个数，则直接将其关闭
        if (accept_fd >= MAX_FD_SIZE)
        {
            close(accept_fd);
            continue;
        }

        // 设为非阻塞模式
        int ret = setNonBlocking(accept_fd);
        if (ret < 0)
        {
            perror("set non block failed!");
            return;
        }

        reqPtr req_info(new RequestData(epoll_fd, accept_fd, path));

        // 文件描述符可读，边沿触发(EPOLLONESHOT)模式，保证一个socket连接在任意时刻只能被一个线程锁处理
        __uint32_t epoll_event_ = EPOLLIN | EPOLLET | EPOLLONESHOT;
        // 添加文件描述符监听
        Epoll::epollAdd(accept_fd, req_info, epoll_event_);
        // 添加定时器
        timer_manager.addTimer(req_info, TIMER_TIME_OUT);
    }
}

// 分发处理函数, 获取事件 
vector<shared_ptr<RequestData>> Epoll::getEvents(int listen_fd, int events_num, const std::string path)
{
    vector<reqPtr> req_data;
    for (int i = 0; i < events_num; ++i)
    {
        // 获取有事件产生的描述符
        int fd = events[i].data.fd;

        // 监听描述符
        if (fd == listen_fd)
        {
            // 接受连接
            acceptConn(listen_fd, epoll_fd, path);
        }
        else if (fd < 3)
        {
            cout << "fd < 3 error\n" << endl;
            break;
        }
        else 
        {
            // 排除错误事件
            if ((events[i].events & EPOLLERR) || (events[i].events & EPOLLHUP))
            {
                cout << "error event\n" << endl;
                if (requests[fd])
                    requests[fd]->separateTimer();
                requests[fd]->reset();
                continue;
            }

            // 将请求队列加入到线程池
            reqPtr cur_req = requests[fd];
            if (cur_req)
            {
                // 设置可读可写
                if ((events[i].events & EPOLLIN) || (events[i].events & EPOLLPRI))
                    cur_req->enableRead();
                else 
                    cur_req->enableWrite();
                
                cur_req->separateTimer();
                // 添加到任务队列中
                req_data.push_back(cur_req);
                requests[fd].reset();
            }
            else 
            {
                cout << "cur_req is invalid" << endl;
            }
        }
    }
    return req_data;
}

// 添加定时事件 addTimer
void Epoll::addTimer(shared_ptr<RequestData> request_data, int timeout)
{
    timer_manager.addTimer(request_data, timeout);
}