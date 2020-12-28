#pragma once
#include "RequestData.h"
//#include "condition.hpp"
#include <pthread.h>
#include <functional>
#include <memory>
#include <vector>

const int THREADPOOL_INVALID = -1;
const int THREADPOOL_LOCK_FAILURE = -2;
const int THREADPOOL_QUEUE_FULL = -3;
const int THREADPOOL_SHUTDOWN = -4;
const int THREADPOOL_THREAD_FAILURE = -5;
const int THREADPOOL_GRACEFUL = 1;

const int MAX_THREADS_NUM = 1024;
const int MAX_QUEUE = 65535;

typedef enum
{
    immediate_shutdown = 1,
    graceful_shutdown  = 2
} ShutDownOption;

struct ThreadTask
{
    std::function<void(std::shared_ptr<void>)> fun;
    std::shared_ptr<void> args;
};

void Handler(std::shared_ptr<void> req);

class ThreadPool
{
private:
    static pthread_mutex_t lock;
    static pthread_cond_t cond;

    static std::vector<pthread_t> threads;
    static std::vector<ThreadTask> taskQueue;
    static int thread_count;
    static int queue_size;
    static int head;
    static int tail;
    static int count;
    static int shutdown;
    static int started;
public:
    static int ThreadPoolCreate(int thread_count_, int queue_size_);
    static int ThreadPoolAdd(std::shared_ptr<void> args, std::function<void(std::shared_ptr<void>)> fun = Handler);
    static int ThreadPoolDestroy(ShutDownOption shutdown_option = graceful_shutdown);
    static int ThreadPoolFree();
    static void *threadRun(void *args);
};