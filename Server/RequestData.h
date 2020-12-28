#pragma once

#include "Timer.h"
#include <string>
#include <unordered_map>
#include <memory>
#include <sys/epoll.h>
#include <opencv/cv.h>
#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/opencv.hpp>

using namespace cv;

/*
HTTP报文字段：
方法 空格 URL 空格 版本 回车符 换行符  (请求行)
首部:域值 回车符 换行符
多个首部与域值
首部:域值 回车符 换行符 (请求头部)
回车符 换行符
实体 (请求数据)
*/

// 正在解析请求行
const int STATE_PARSE_URI = 1;
// 正在解析请求头
const int STATE_PARSE_HEADERS = 2;
// 正在解析请求数据
const int STATE_RECV_BODY = 3;
const int STATE_ANALYSIS = 4;
// 解析完毕
const int STATE_FINISH = 5;
const int MAX_BUF_SIZE = 4096;

/*
如果有请求但是读不到数据：
可能是request aborted
或者来自网络的数据没有达到等原因，对这样的请求尝试超过一定次数就抛弃
*/
const int AGAIN_MAX_TIMES = 200;

const int PARSE_URI_AGAIN = -1;
const int PARSE_URI_ERROR = -2;
const int PARSE_URI_SUCCESS = 0;

const int PARSE_HEADER_AGAIN = -1;
const int PARSE_HEADER_ERROR = -2;
const int PARSE_HEADER_SUCCESS = 0;

const int ANALYSIS_ERROR = -2;
const int ANALYSIS_SUCCESS = 0;

// post方法
const int METHOD_POST = 1;
// get方法
const int METHOD_GET = 2;
// HTTP 1.0
const int HTTP_10 = 1;
// HTTP 1.1
const int HTTP_11 = 2;
// epoll等待时间
const int EPOLL_WAIT_TIME = 500;

class MimeType
{
private:
    static void init();
    static std::unordered_map<std::string, std::string> mime;
    MimeType();
    MimeType(const MimeType &mime_);

public:
    static std::string getMime(const std::string &suffix);

private:
    static pthread_once_t once_control;
};

// header状态
enum HeaderState
{
    hStart = 0, // 首部
    hKey,   // 域值
    hColon, // 引号
    hSpaceAfterColon,  // 引号后面的空格
    hValue,  // 域值
    hCR,    // 回车符
    hLF,    // 换行符
    hEndCR, // 最后的回车符
    hEndLF, // 最后的换行符
};

class Timer;

// 为每一个连接创建一个RequestData
class RequestData : public std::enable_shared_from_this<RequestData>
{
private:
    std::string path;
    // 当前请求的文件描述符
    int fd;
    int epollfd;

    // 读缓冲
    std::string inBuf;
    // 写缓冲
    std::string outBuf;
    // 监听事件集
    __uint32_t events;
    bool error;

    // 请求方法
    int method;
    // HTTP版本
    int version;
    std::string fileName;
    // 当前读的位置
    int readPos;
    // 当前状态
    int state;
    // 当前头部状态
    int hState;
    // 是否结束
    bool isFinish;
    // 是否有保活标志
    bool keepAlive;
    // 用于保存首部字段
    std::unordered_map<std::string, std::string> headers;
    // 当前请求指向的定时器(Timer与RequestData互相引用，因此需要使用weak_ptr解决循环引用问题)
    std::weak_ptr<Timer> timer;
    // 是否可读写标志
    bool isRead;
    bool isWrite;

private:
    // 解析URI的函数
    int parseURI();
    // 解析头部的函数
    int parseHeader();
    // 解析请求数据的函数
    int parseRequest();

    Mat stitch(Mat &src)
    {
        return src;
    }

public:
    RequestData();
    RequestData(int epollfd_, int fd_, std::string path_);
    ~RequestData();

    // 将当前对象与其相应的定时器连接
    void linkTimer(std::shared_ptr<Timer> timer_);
    void reset();
    void separateTimer();
    int getFd();
    void setFd(int fd_);
    // 处理读写和错误，以及连接
    void handleRead();
    void handleWrite();
    void handleError(int fd, int err_no, std::string msg);
    void handleConn();
    // 关闭读写通道
    void disableWR();
    void enableRead();
    void enableWrite();

    // 查看是否可读写
    bool isCanRead();
    bool isCanWrite();
};