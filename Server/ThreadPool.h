#pragma once
#include <pthread.h>
#include <functional>
#include <memory>
#include <vector>

// 线程池无效
const int THREADPOOL_INVALID = -1;
// 获取锁失败
const int THREADPOOL_LOCK_FAILURE = -2;
// 任务队列已满
const int THREADPOOL_QUEUE_FULL = -3;
// 线程池shutdown
const int THREADPOOL_SHUTDOWN = -4;
// 线程池线程出现错误
const int THREADPOOL_THREAD_FAILURE = -5;
// 优雅结束线程
const int THREADPOOL_GRACEFUL = 1;

// 线程池的线程个数
const int MAX_THREADS_SIZE = 1024;
// 任务队列最大数
const int MAX_QUEUE_SIZE = 65535;

// 结束子线程的方式
typedef enum
{
    immediate_shutdown = 1,
    graceful_shutdown = 2
} ShutDownOption;

// 线程池任务定义
struct ThreadTask
{
    // 使用智能指针
    std::function<void(std::shared_ptr<void>)> func;
    std::shared_ptr<void> args;
};

void Handler(std::shared_ptr<void> req);

class ThreadPool
{
// 单例模式
private:
    static pthread_mutex_t lock;
    static pthread_cond_t cond;
    
    // 线程结构体, 用来保存线程实体
    static std::vector<pthread_t> threads;
    // 任务队列
    static std::vector<ThreadTask> taskQueue;
    static int thread_count;
    static int queue_size;
    // 指向队列的头结点
    static int head;
    // 指向队列的尾节点
    static int tail;
    static int count;
    // 记录结束子线程的方式
    static int shutdown;
    // 已经启动的线程数
    static int started;

public:
    // 创建线程池
    static int ThreadPoolCreate(int thread_count_, int queue_size_);
    // 添加任务
    static int ThreadPoolAdd(std::shared_ptr<void> args, std::function<void(std::shared_ptr<void>)> func = Handler);
    // 销毁线程池(主线程调用，设置shutdown参数，工作线程根据shutdown的参数进行自身的退出和运行)
    static int ThreadPoolDestroy(ShutDownOption option = graceful_shutdown);
    // 释放线程池的资源
    static int ThreadPoolFree();
    // 线程的入口函数
    static void *ThreadPoolRun(void *args);
};