# ServerXXX
A tiny web server based on reactor mode.

# 运行示例

```shell
// 启动服务器
cd src
./myserver

// 运行测试
cd WebBench
./test.sh
```

# 模型结构如下

* 基于**Reactor模式**实现，主线程负责监听事件，将事件放入工作队列中，工作线程负责从工作队列中取出任务来完成相应的IO

* 使用epoll边沿触发EPOLLET + EPOLLONESHOT + 非阻塞IO, EPOLLONESHOT保证在同一时间同一个连接只由一个线程进行处理
* 使用线程池避免线程频繁创建和销毁带来的开销
* 实现了一个任务队列task_queue，应用**条件变量**来触发通知线程新任务的到来
* 实现了一个小根堆的定时器及时剔除超时请求，使用了STL的优先队列priority_queue来管理定时器
* 支持HTTP的get、post请求，目前支持短连接
* 主线程和工作线程分配：
    * 主线程负责等待epoll中的事件，并把新到来的事件放入任务队列，在每次循环的结束(epollWait函数中)剔除超时请求和被置为删除的时间结点
    * 工作线程阻塞在**条件变量**的等待中，新任务到来后，某一工作线程会被唤醒，执行具体的IO操作和计算任务，如果需要继续监听，会添加到epoll中 
* 锁的使用：
    * 一是任务队列的添加和取操作，都需要加锁，并配合条件变量
    * 二是定时器结点的添加和删除，需要加锁，主线程和工作线程都要操作定时器队列
* 锁的设计上，使用了**RAII锁机制**，定义一个类来管理锁，使锁能够自动释放
* 动态内存的管理，使用了**智能指针，包括shared_ptr，以及为了解决循环引用问题，使用了weak_ptr(RequestData和Timer类互相引用)**
* 任务队列中的任务结构使用了C++11中的**function**来包装任务函数  
* 对互斥锁以及条件变量进行了封装，更加面向对象

# 测试分析

* 使用工具Webbench，开启500客户端进程，时间为60s

# 测试结果

* 短连接测试(500个客户端同时发送请求)

![np_keepalive](https://github.com/dwangxxx/ServerXXX/blob/main/test_result/test_nokeep.png)