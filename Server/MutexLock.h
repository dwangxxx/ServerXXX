/*
本文件用于实现互斥锁的封装
并使用了RAII技术来实现锁的自动释放
*/

#pragma once
#include "nocopyable.h"
#include <pthread.h>
#include <cstdio>

// 封装pthread_mutex_t
class MutexLock : nocopyable
{
public:
    MutexLock()
    {
        pthread_mutex_init(&mutex, nullptr);
    }
    ~MutexLock()
    {
        pthread_mutex_lock(&mutex);
        pthread_mutex_destroy(&mutex);
    }
    void lock()
    {
        pthread_mutex_lock(&mutex);
    }
    void unlock()
    {
        pthread_mutex_unlock(&mutex);
    }
    pthread_mutex_t *get()
    {
        return &mutex;
    }

private:
    pthread_mutex_t mutex;
    // 将Condition类设置为当前类的友元类
    friend class Condition;
};

// 使用RAII技术，使得锁能够自动释放
class MutexLockGuard : nocopyable
{
public:
    // 构造对象即加锁
    explicit MutexLockGuard(MutexLock &mutex_) : mutex(mutex_)
    {
        mutex.lock();
    }
    // 销毁对象即释放锁
    ~MutexLockGuard()
    {
        mutex.unlock();
    }
private:
    MutexLock &mutex;
};