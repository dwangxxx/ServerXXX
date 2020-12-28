#pragma once

#include "RequestData.h"
#include "nocopyable.h"
#include "MutexLock.h"
#include <unistd.h>
#include <memory>
#include <queue>
#include <deque>

class RequestData;

class Timer
{
    typedef std::shared_ptr<RequestData> reqPtr;
private:
    bool deleted;
    size_t expired_time;
    reqPtr request_data;
public:
    Timer(reqPtr request_data_, int timeout);
    ~Timer();
    void update(int timeout);
    bool isValid();
    void clearReq();
    void setDeleted();
    bool isDeleted() const;
    size_t getExpTime() const;
};

struct timerCmp
{
    bool operator()(std::shared_ptr<Timer> &lhs, std::shared_ptr<Timer> &rhs) const
    {
        return lhs->getExpTime() > rhs->getExpTime();
    }
};

class TimerManager
{
    typedef std::shared_ptr<RequestData> reqPtr;
    typedef std::shared_ptr<Timer> timerPtr;
private:
    std::priority_queue<timerPtr, std::deque<timerPtr>, timerCmp> timerQueue;
    MutexLock lock;
public:
    TimerManager();
    ~TimerManager();
    void addTimer(reqPtr request_data_, int timeout);
    void addTimer(timerPtr timer_);
    void handleEvent();
};