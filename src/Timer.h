#pragma once
#include "RequestData.h"
#include "nocopyable.h"
#include "MutexLock.h"
#include <unistd.h>
#include <memory>
#include <queue>
#include <deque>

class RequestData;

// 一个定时器类
class Timer
{
    typedef std::shared_ptr<RequestData> reqPtr;

private:
    // 是否删除
    bool deleted;
    // 超时时间
    size_t expired_time;
    // 指向一个RequestData对象
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

// 定义仿函数，用于Timer的比较
struct TimerCmp
{
    bool operator()(std::shared_ptr<Timer> &lhs, std::shared_ptr<Timer> &rhs) const
    {
        // 实现小跟堆，所以需要返回大于比较的结果
        return lhs->getExpTime() > rhs->getExpTime();
    }
};

// 定时器管理
class TimerManager
{
    typedef std::shared_ptr<RequestData> reqPtr;
    typedef std::shared_ptr<Timer> TimerPtr;

private:
    // 使用priority_queue作为小根堆定时器实现
    std::priority_queue<TimerPtr, std::deque<TimerPtr>, TimerCmp> TimerQueue;
    // 添加定时器的时候需要上锁
    MutexLock lock;

public:
    TimerManager();
    ~TimerManager();
    void addTimer(reqPtr request_data_, int timeout);
    void addTimer(TimerPtr timer_);
    void eventHandler();
};