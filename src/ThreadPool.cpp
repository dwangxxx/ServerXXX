#include "ThreadPool.h"


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

int ThreadPool::ThreadPoolCreate(int _thread_count, int _queue_size)
{
    bool err = false;
    do
    {
        if(_thread_count <= 0 || _thread_count > MAX_THREADS_NUM || _queue_size <= 0 || _queue_size > MAX_QUEUE) 
        {
            _thread_count = 4;
            _queue_size = 1024;
        }
    
        thread_count = 0;
        queue_size = _queue_size;
        head = tail = count = 0;
        shutdown = started = 0;

        threads.resize(_thread_count);
        taskQueue.resize(_queue_size);
    
        /* Start worker threads */
        for(int i = 0; i < _thread_count; ++i) 
        {
            if(pthread_create(&threads[i], NULL, threadRun, (void*)(0)) != 0) 
            {
                return -1;
            }
            ++thread_count;
            ++started;
        }
    } while(false);
    
    if (err) 
    {
        return -1;
    }
    return 0;
}

void Handler(std::shared_ptr<void> req)
{
    std::shared_ptr<RequestData> request = std::static_pointer_cast<RequestData>(req);
    if (request->isCanWrite())
        request->handleWrite();
    else if (request->isCanRead())
        request->handleRead();
    request->handleConn();
}

int ThreadPool::ThreadPoolAdd(std::shared_ptr<void> args, std::function<void(std::shared_ptr<void>)> fun)
{
    int next, err = 0;
    if(pthread_mutex_lock(&lock) != 0)
        return THREADPOOL_LOCK_FAILURE;
    do 
    {
        next = (tail + 1) % queue_size;
        // 队列满
        if(count == queue_size) 
        {
            err = THREADPOOL_QUEUE_FULL;
            break;
        }
        // 已关闭
        if(shutdown)
        {
            err = THREADPOOL_SHUTDOWN;
            break;
        }
        taskQueue[tail].fun = fun;
        taskQueue[tail].args = args;
        tail = next;
        ++count;
        
        /* pthread_cond_broadcast */
        // 唤醒等待任务的线程
        if(pthread_cond_signal(&cond) != 0) 
        {
            err = THREADPOOL_LOCK_FAILURE;
            break;
        }
    } while(false);

    if(pthread_mutex_unlock(&lock) != 0)
        err = THREADPOOL_LOCK_FAILURE;
    return err;
}


int ThreadPool::ThreadPoolDestroy(ShutDownOption shutdown_option)
{
    printf("Thread pool destroy !\n");
    int i, err = 0;

    if(pthread_mutex_lock(&lock) != 0) 
    {
        return THREADPOOL_LOCK_FAILURE;
    }
    do 
    {
        if(shutdown) {
            err = THREADPOOL_SHUTDOWN;
            break;
        }

        // 将shutdown参数设置为true
        shutdown = shutdown_option;

        // 唤醒所有等待条件变量的线程
        if((pthread_cond_broadcast(&cond) != 0) ||
           (pthread_mutex_unlock(&lock) != 0)) {
            err = THREADPOOL_LOCK_FAILURE;
            break;
        }

        for(i = 0; i < thread_count; ++i)
        {
            if(pthread_join(threads[i], NULL) != 0)
            {
                err = THREADPOOL_THREAD_FAILURE;
            }
        }
    } while(false);

    if(!err) 
    {
        ThreadPoolFree();
    }
    return err;
}

// 销毁线程池的资源
int ThreadPool::ThreadPoolFree()
{
    if(started > 0)
        return -1;
    pthread_mutex_lock(&lock);
    pthread_mutex_destroy(&lock);
    pthread_cond_destroy(&cond);
    return 0;
}

// 线程入口函数
void *ThreadPool::threadRun(void *args)
{
    while (true)
    {
        ThreadTask task;
        pthread_mutex_lock(&lock);
        while((count == 0) && (!shutdown)) 
        {
            pthread_cond_wait(&cond, &lock);
        }
        if((shutdown == immediate_shutdown) ||
           ((shutdown == graceful_shutdown) && (count == 0)))
        {
            break;
        }
        task.fun = taskQueue[head].fun;
        task.args = taskQueue[head].args;
        taskQueue[head].fun = NULL;
        taskQueue[head].args.reset();
        head = (head + 1) % queue_size;
        --count;
        pthread_mutex_unlock(&lock);
        (task.fun)(task.args);
    }
    --started;
    pthread_mutex_unlock(&lock);
    printf("This threadpool thread finishs!\n");
    pthread_exit(NULL);
    return(NULL);
}