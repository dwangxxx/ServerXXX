#pragma once

#include "RequestData.h"
#include "Timer.h"
#include <vector>
#include <unordered_map>
#include <sys/epoll.h>
#include <memory>

class Epoll
{
public:
    typedef std::shared_ptr<RequestData> reqPtr;

private:
    static const int MAX_FD_SIZE = 1024;
    // 用来接收最大监听事件
    static epoll_event *events;
    // 用来存储指向RequestData的指针，使用文件描述符fd索引，节省时间
    static reqPtr requests[MAX_FD_SIZE];
    static int epoll_fd;
    static const std::string path;
    // 定时器管理器，用来管理连接定时
    static TimerManager timer_manager;

public:
    static int epollInit(int max_events, int max_listen);
    static int epollAdd(int fd, reqPtr request, __uint32_t events);
    static int epollMod(int fd, reqPtr request, __uint32_t events);
    static int epollDel(int fd, __uint32_t events = (EPOLLIN | EPOLLET | EPOLLONESHOT));
    static void epollWait(int listen_fd, int max_events, int timeout);
    static void acceptConn(int listen_fd, int epoll_fd, const std::string path);
    static std::vector<reqPtr> getEvents(int listen_fd, int events_num, const std::string path);

    static void addTimer(reqPtr request, int timeout);
};