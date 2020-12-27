/*
本文件用来封装条件变量condition
*/
#pragma once
#include "nocopyable.h"
#include "MutexLock.h"
#include <pthread.h>

class Condition : nocopyable
{
public:
    explicit Condition(MutexLock &mutex_) : mutex(mutex_)
    {
        pthread_cond_init(&cond, nullptr);
    }
    ~Condition()
    {
        pthread_cond_destroy(&cond);
    }
    void wait()
    {
        pthread_cond_wait(&cond, mutex.get());
    }
    // 唤醒一个等待线程
    void wake()
    {
        pthread_cond_signal(&cond);
    }
    // 唤醒所有等待线程
    void wakeAll()
    {
        pthread_cond_broadcast(&cond);
    }

private:
    MutexLock &mutex;
    pthread_cond_t cond;
};