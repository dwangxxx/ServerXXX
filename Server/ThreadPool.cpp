#include "ThreadPool.h"
#include "RequestData.h"

pthread_mutex_t ThreadPool::lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t ThreadPool::cond = PTHREAD_COND_INITIALIZER;
std::vector<pthread_t> ThreadPool::threads;
std::vector<ThreadTask> ThreadPool::taskQueue;
int ThreadPool::thread_count = 0;
int ThreadPool::queue_size = 0;
int ThreadPool::head = 0;
int ThreadPool::tail = 0;
int ThreadPool::count = 0;
int ThreadPool::shutdown = 0;
int ThreadPool::started = 0;

// 创建线程池
int ThreadPool::ThreadPoolCreate(int thread_count_, int queue_size_)
{
    bool error = false;
    do
    {
        if (thread_count_ <= 0 || thread_count_ > MAX_THREADS_SIZE || queue_size_ <= 0 || queue_size_ > MAX_QUEUE_SIZE)
        {
            thread_count_ = 4;
            queue_size_ = 1024;
        }

        thread_count = 0;
        queue_size = queue_size_;
        head = tail = count = 0;
        shutdown = started = 0;

        // 初始化threads和taskQueue
        threads.resize(thread_count_);
        taskQueue.resize(queue_size_);

        // 从工作线程开始启动
        for (int i = 0; i < thread_count_; ++i)
        {
            if (pthread_create(&threads[i], NULL, ThreadPoolRun, (void*)(0)) != 0)
            {
                return -1;
            }
            ++thread_count;
            ++started;
        }
    } while (false);

    if (error)
    {
        return -1;
    }
    return 0;
}

void Handler(std::shared_ptr<void> req)
{
    // 智能指针指向RequestData
    std::shared_ptr<RequestData> request = std::static_pointer_cast<RequestData>(req);
    // 处理读请求
    if (request->isCanWrite())
        request->handleWrite();
    // 处理写请求
    else if (request->isCanRead())
        request->handleRead();
    // 处理当前连接
    request->handleConn();
}

// 往任务队列中加入任务, 参数是需要处理的函数和相应的函数实参
int ThreadPool::ThreadPoolAdd(std::shared_ptr<void> args, std::function<void(std::shared_ptr<void>)> func)
{
    int next = 0;
    int error = 0;
    if (pthread_mutex_lock(&lock) != 0)
        // 锁相关的错误
        return THREADPOOL_LOCK_FAILURE;
    do 
    {
        next = (tail + 1) % queue_size;
        // 如果队列满了
        if (count == queue_size)
        {
            error = THREADPOOL_QUEUE_FULL;
            break;
        }
        // 线程池已经关闭
        if (shutdown)
        {
            error = THREADPOOL_SHUTDOWN;
            break;
        }
        // 将任务加入到任务队列中
        // 需要执行的函数
        taskQueue[tail].func = func;
        // 函数参数
        taskQueue[tail].args = args;
        tail = next;
        // 任务队列的个数
        ++count;

        // 唤醒正在等待任务的线程
        if (pthread_cond_signal(&cond) != 0)
        {
            error = THREADPOOL_LOCK_FAILURE;
            break;
        }
    } while(false);

    if (pthread_mutex_unlock(&lock) != 0)
        error = THREADPOOL_LOCK_FAILURE;
    
    return error;
}

// 销毁线程池, 在其中指定销毁线程的类型(immediate或者graceful)
int ThreadPool::ThreadPoolDestroy(ShutDownOption option)
{
    printf("Destroy Thread Pool!\n");
    int i = 0;
    int error = 0;

    if (pthread_mutex_lock(&lock) != 0)
    {
        return THREADPOOL_LOCK_FAILURE;
    }
    do 
    {
        if (shutdown)
        {
            error = THREADPOOL_SHUTDOWN;
            break;
        }
        // 指定shutdown的类型, 指示工作线程应该退出
        shutdown = option;

        if (pthread_cond_broadcast(&cond) != 0 || pthread_mutex_unlock(&lock) != 0)
        {
            error = THREADPOOL_LOCK_FAILURE;
            break;
        }

        for (i = 0; i < thread_count; ++i)
        {
            // 主线程等待工作线程退出
            if (pthread_join(threads[i], NULL) != 0)
                // 线程出现错误
                error = THREADPOOL_THREAD_FAILURE;
        }
    } while (false);

    return error;
}

// 释放线程创建的一些资源(锁等)
int ThreadPool::ThreadPoolFree()
{
    if (started > 0)
        return -1;
    
    pthread_mutex_lock(&lock);
    pthread_mutex_destroy(&lock);
    pthread_cond_destroy(&cond);
    return 0;
}

// 每个线程的运行入口
void *ThreadPool::ThreadPoolRun(void *args)
{
    // 工作线程的工作就是不断的从工作队列中取出任务
    while (true)
    {
        ThreadTask task;
        // 首先加锁
        pthread_mutex_lock(&lock);
        // 如果任务队列没有任务，并且没有设置shutdown
        while (count == 0 && !shutdown)
        {
            // 等待任务队列唤醒线程, 使用条件变量
            pthread_cond_wait(&cond, &lock);
        }
        // 应该优雅的退出，如果任务队列还有任务，则处理完任务之后再退出线程
        // 如果是直接结束线程，或者优雅结束线程并且任务队列个数为0，则结束线程
        if (shutdown == immediate_shutdown || 
                (shutdown == graceful_shutdown && count == 0))
        {
            break;
        }
        task.func = taskQueue[head].func;
        task.args = taskQueue[head].args;
        taskQueue[head].func = NULL;
        // 智能指针释放
        taskQueue[head].args.reset();
        head = (head + 1) % queue_size;
        --count;
        pthread_mutex_unlock(&lock);
        // 执行任务
        (task.func)(task.args);
    }

    // 退出一个线程
    --started;
    pthread_mutex_unlock(&lock);
    printf("This thread is finished!\n");
    pthread_exit(NULL);
    return (NULL);
}