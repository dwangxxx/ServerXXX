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
    static const int MAXFDS = 1000;
    static epoll_event *events;
    static reqPtr requests[MAXFDS];
    static int epoll_fd;
    static const std::string path;

    static TimerManager timer_manager;
public:
    static int epollInit(int max_events, int listen_num);
    static int epollAdd(int fd, reqPtr request_, __uint32_t events);
    static int epollMod(int fd, reqPtr request_, __uint32_t events);
    static int epollDel(int fd, __uint32_t events = (EPOLLIN | EPOLLET | EPOLLONESHOT));
    static void epollWait(int listen_fd, int max_events, int timeout);
    static void acceptConn(int listen_fd, int epoll_fd, const std::string path_);
    static std::vector<reqPtr> getEvents(int listen_fd, int events_num, const std::string path_);

    static void addTimer(reqPtr request_data_, int timeout);
};