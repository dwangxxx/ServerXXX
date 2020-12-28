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

Timer::Timer(reqPtr _request_data, int timeout): 
    deleted(false), 
    request_data(_request_data)
{
    struct timeval now;
    gettimeofday(&now, NULL);
    // 以毫秒计
    expired_time = ((now.tv_sec * 1000) + (now.tv_usec / 1000)) + timeout;
}

Timer::~Timer()
{
    if (request_data)
    {
        Epoll::epollDel(request_data->getFd());
    }
}

void Timer::update(int timeout)
{
    struct timeval now;
    gettimeofday(&now, NULL);
    expired_time = ((now.tv_sec * 1000) + (now.tv_usec / 1000)) + timeout;
}

bool Timer::isValid()
{
    struct timeval now;
    gettimeofday(&now, NULL);
    size_t temp = ((now.tv_sec * 1000) + (now.tv_usec / 1000));
    if (temp < expired_time)
    {
        return true;
    }
    else
    {
        this->setDeleted();
        return false;
    }
}

void Timer::clearReq()
{
    request_data.reset();
    this->setDeleted();
}

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

TimerManager::TimerManager()
{
}

TimerManager::~TimerManager()
{
}

void TimerManager::addTimer(reqPtr request_data_, int timeout)
{
    timerPtr newTimer(new Timer(request_data_, timeout));
    {
        MutexLockGuard locker(lock);
        timerQueue.push(newTimer);
    }
    request_data_->linkTimer(newTimer);
}

void TimerManager::addTimer(timerPtr timer_node) {}

/* 
对于被置为deleted的时间节点，会延迟到它(1)超时 或 (2)它前面的节点都被删除时，它才会被删除。
一个点被置为deleted,它最迟会在TIMER_TIME_OUT时间后被删除。
这样做有两个好处：
(1) 第一个好处是不需要遍历优先队列，省时。
(2) 第二个好处是给超时时间一个容忍的时间，就是设定的超时时间是删除的下限(并不是一到超时时间就立即删除)，如果监听的请求在超时后的下一次请求中又一次出现了，
就不用再重新申请RequestData节点了，这样可以继续重复利用前面的RequestData，减少了一次delete和一次new的时间。
*/

void TimerManager::handleEvent()
{
    MutexLockGuard locker(lock);
    while (!timerQueue.empty())
    {
        timerPtr ptimer_now = timerQueue.top();
        if (ptimer_now->isDeleted())
        {
            timerQueue.pop();
        }
        else if (ptimer_now->isValid() == false)
        {
            timerQueue.pop();
        }
        else
        {
            break;
        }
    }
}