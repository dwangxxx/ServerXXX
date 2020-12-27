#include "Timer.h"
#include "Epoll.h"
#include <unordered_map>
#include <string>
#include <sys/time.h>
#include <unistd.h>
#include <deque>
#include <queue>
#include <iostream>

using namespace std;

// Timer定时器构造函数
Timer::Timer(reqPtr request_data_, int timeout)
    : deleted(false), request_data(request_data_)
{
    struct timeval now;
    gettimeofday(&now, NULL);
    // 以毫秒计时
    expired_time = ((now.tv_sec * 1000) + (now.tv_usec / 1000)) + timeout;
}

// Timer定时器析构函数, 将文件描述符从epollfd中删除
Timer::~Timer()
{
    if (request_data)
    {
        // 将request_data指向的文件描述符从epoll中删去
        Epoll::epollDel(request_data->getFd());
    }
}

// 更新Timer超时时间
void Timer::update(int timeout)
{
    struct timeval now;
    gettimeofday(&now, NULL);
    expired_time = ((now.tv_sec * 1000) + (now.tv_usec / 1000)) + timeout;
}

// 判断当前定时器是否失效
bool Timer::isValid()
{
    struct timeval now;
    gettimeofday(&now, NULL);
    size_t tmp = ((now.tv_sec * 1000) + (now.tv_usec / 1000));
    // 未到超时时间，有效
    if (tmp < expired_time)
    {
        return true;
    }
    // 超过超时时间，无效
    else 
    {
        this->setDeleted();
        return false;   
    }
}

// 清除RequestData
void Timer::clearReq()
{
    request_data->reset();
    this->setDeleted();
}

// 设置删除, 可能超时时间未到就需要延时删除
void Timer::setDeleted()
{
    deleted = true;
}

bool Timer::isDeleted() const
{
    return deleted;
}

size_t Timer::getExpTime() const
{
    return expired_time;
}

// 定时器管理器相关函数
TimerManager::TimerManager() {}

TimerManager::~TimerManager() {}

// 往定时器堆中添加定时器
void TimerManager::addTimer(reqPtr request_data_, int timeout)
{
    TimerPtr new_timer(new Timer(request_data_, timeout));
    {
        // RAII技术，定义新的作用域，作用域结束，对象自动销毁
        MutexLockGuard locker(lock);
        TimerQueue.push(new_timer);
    }
    request_data_->linkTimer(new_timer);
}

void TimerManager::addTimer(TimerPtr timer_) {}

/*
对于被置为deleted的时间节点，会延迟到它(1)超时 或 (2)它前面的节点都被删除时，它才会被删除。
一个点被置为deleted,它最迟会在TIMER_TIME_OUT时间后被删除。
这样做有两个好处：
(1) 第一个好处是不需要遍历优先队列，省时。
(2) 第二个好处是给超时时间一个容忍的时间，就是设定的超时时间是删除的下限(并不是一到超时时间就立即删除)，如果监听的请求在超时后的下一次请求中又一次出现了，
就不用再重新申请RequestData节点了，这样可以继续重复利用前面的RequestData，减少了一次delete和一次new的时间。
*/
// 心搏函数，每次定时事件到了之后就调用这个函数来处理
void TimerManager::eventHandler()
{
    // 首先加锁
    MutexLockGuard locker(lock);
    while (!TimerQueue.empty())
    {
        TimerPtr timer_top = TimerQueue.top();
        if (timer_top->isDeleted())
        {
            TimerQueue.pop();
        }
        else if (timer_top->isValid() == false)
        {
            TimerQueue.pop();
        }
        else 
            break;
    }
}