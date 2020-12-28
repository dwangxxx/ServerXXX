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

// 解析URI
const int STATE_PARSE_URI = 1;
// 解析头部
const int STATE_PARSE_HEADERS = 2;
// 解析请求数据(post命令)
const int STATE_RECV_BODY = 3;
// 正在分析请求(get)
const int STATE_ANALYSIS = 4;
// 解析结束
const int STATE_FINISH = 5;

const int MAX_BUF_SIZE = 4096;

// 重复请求的次数
const int AGAIN_MAX_TIMES = 200;

const int PARSE_URI_AGAIN = -1;
const int PARSE_URI_ERROR = -2;
const int PARSE_URI_SUCCESS = 0;

const int PARSE_HEADER_AGAIN = -1;
const int PARSE_HEADER_ERROR = -2;
const int PARSE_HEADER_SUCCESS = 0;

const int ANALYSIS_ERROR = -2;
const int ANALYSIS_SUCCESS = 0;

const int METHOD_POST = 1;
const int METHOD_GET = 2;
const int HTTP_10 = 1;
const int HTTP_11 = 2;

const int EPOLL_WAIT_TIME = 500;

class MimeType
{
private:
    static void init();
    static std::unordered_map<std::string, std::string> mime;
    MimeType();
    MimeType(const MimeType &m);

public:
    static std::string getMime(const std::string &suffix);

private:
    static pthread_once_t once_control;
};

enum HeadersState
{
    hStart = 0,
    hKey,
    hColon,
    hSpacesAfterColon,
    hValue,
    hCR,
    hLF,
    hEndCR,
    hEndLF
};

class Timer;

class RequestData : public std::enable_shared_from_this<RequestData>
{
private:
    std::string path;
    int fd;
    int epollfd;

    std::string inBuf;
    std::string outBuf;
    __uint32_t events;
    bool error;

    // http方法
    int method;
    // http版本
    int HTTPversion;
    std::string fileName;
    int readPos;
    int state;
    int hState;
    bool isFinish;
    bool keepAlive;
    std::unordered_map<std::string, std::string> headers;
    std::weak_ptr<Timer> timer;

    bool isAbleRead;
    bool isAbleWrite;

private:
    int parseURI();
    int parseHeaders();
    int parseRequest();

    Mat stitch(Mat &src)
    {
        return src;
    }

public:

    RequestData();
    RequestData(int epollfd_, int fd_, std::string path_);
    ~RequestData();
    void linkTimer(std::shared_ptr<Timer> timer_);
    void reset();
    void seperateTimer();
    int getFd();
    void setFd(int _fd);
    void handleRead();
    void handleWrite();
    void handleError(int fd, int err_num, std::string msg);
    void handleConn();

    void disableWR();

    void enableRead();

    void enableWrite();

    bool isCanRead();

    bool isCanWrite();
};

