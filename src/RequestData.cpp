#include "RequestData.h"
#include "Timer.h"
#include "util.h"
#include "Epoll.h"
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <queue>
#include <memory>
#include <cstdlib>
#include <opencv/cv.h>
#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/opencv.hpp>

using namespace cv;

// 保证一次一个线程处理
pthread_once_t MimeType::once_control = PTHREAD_ONCE_INIT;
// 用来存储文件的格式
std::unordered_map<std::string, std::string> MimeType::mime;

/*
Mime文件格式，用来传送除了文本之外的数据
MIME意为多功能Internet邮件扩展，它设计的最初目的是为了在发送电子邮件时附加多媒体数据，
让邮件客户程序能根据其类型进行处理。然而当它被HTTP协议支持之后，它的意义就更为显著了。
它使得HTTP传输的不仅是普通的文本，而变得丰富多彩。
*/
void MimeType::init()
{
    mime[".html"] = "text/html";
    mime[".avi"] = "video/xmsvideo";
    mime[".bmp"] = "image/bmp";
    mime[".c"] = "text/plain";
    mime[".doc"] = "application/msword";
    mime[".gif"] = "image/gif";
    mime[".gz"] = "application/x-gzip";
    mime[".htm"] = "text/html";
    mime[".ico"] = "application/x-ico";
    mime[".jpg"] = "image/jpeg";
    mime[".png"] = "image/png";
    mime[".txt"] = "text/plain";
    mime[".mp3"] = "audio/mp3";
    mime["default"] = "text/html";
}

// 返回对应的文件格式
std::string MimeType::getMime(const std::string &suffix)
{
    pthread_once(&once_control, MimeType::init);
    if (mime.find(suffix) == mime.end())
        return mime["default"];
    else return mime[suffix];
}

// RequestData构造函数
RequestData::RequestData():
    readPos(0),
    state(STATE_PARSE_URI),
    hState(hStart),
    keepAlive(false),
    isRead(true),
    isWrite(false),
    events(0),
    error(false)
{
    std::cout << "Initializing RequestData based on default data" << std::endl;
}

RequestData::RequestData(int epollfd_, int fd_, std::string path_):
    readPos(0),
    state(STATE_PARSE_URI),
    hState(hStart),
    keepAlive(false),
    isRead(true),
    isWrite(false),
    events(0),
    error(false),
    path(path_),
    fd(fd_),
    epollfd(epollfd_)
{
    std::cout << "Initializing RequestData based on specifical parameters." << std::endl;
}

// 析构函数
RequestData::~RequestData()
{
    std::cout << "Destroy RequestData()" << std::endl;
    close(fd);
}

void RequestData::linkTimer(std::shared_ptr<Timer> timer_)
{
    timer = timer_;
}

int RequestData::getFd()
{
    return fd;
}

// 设置文件描述符
void RequestData::setFd(int fd_)
{
    fd = fd_;
}

// 重置当前RequestData对象
void RequestData::reset()
{
    inBuf.clear();
    fileName.clear();
    path.clear();
    readPos = 0;
    state = STATE_PARSE_URI;
    hState = hStart;
    headers.clear();
    // weak_ptr.lock()获取一个shared_ptr指针
    if (timer.lock())
    {
        // 重置timer
        std::shared_ptr<Timer> myTimer(timer.lock());
        myTimer->clearReq();
        timer.reset();
    }
}

void RequestData::separateTimer()
{
    if (timer.lock())
    {
        std::shared_ptr<Timer> myTimer(timer.lock());
        // 首先清除myTimer的RequestData
        myTimer->clearReq();
        // 清除timer指针
        timer.reset();
    }
}

// 处理读请求
void RequestData::handleRead()
{
    do 
    {
        int nRead = readn(fd, inBuf);
        if (nRead < 0)
        {
            perror("read error 1");
            error = true;
            handleError(fd, 400, "Bad Request");
            break;
        }
        else if (nRead = 0)
        {
            /*
            有读请求但是读不到数据，可能是Request Aborted
            或者是来自网络的数据没有到达
            最可能是对端已经关闭，统一按照对端已经关闭处理
            */
           error = true;
           break;
        }

        // 如果正在解析请求行字段
        if (state == STATE_PARSE_URI)
        {
            int flag = this->parseURI();
            if (flag == PARSE_URI_AGAIN)
                break;
            else if(flag == PARSE_URI_ERROR)
            {
                perror("PARSE_URI_ERROR 2");
                error = true;
                handleError(fd, 400, "Bad Request");
                break;
            }
            // 请求行解析完毕，解析请求头部
            else
                state = STATE_PARSE_HEADERS;
        }

        // 正在解析请求头部
        if (state == STATE_PARSE_HEADERS)
        {
            int flag = this->parseHeader();
            if (flag == PARSE_HEADER_AGAIN)
                break;
            else if(flag == PARSE_HEADER_ERROR)
            {
                perror("PARSE_HEADER_ERROR 3");
                error = true;
                handleError(fd, 400, "Bad Request");
                break;
            }
            // post方法
            if (method == METHOD_POST)
            {
                // post方法, 准备解析请求数据
                state = STATE_RECV_BODY;
            }
            else 
            {
                // get方法
                state = STATE_ANALYSIS;
            }
        }

        // 如果是post方法，解析请求数据
        if (state == STATE_RECV_BODY)
        {
            int contentLength = -1;
            if (headers.find("Content-length") != headers.end())
            {
                contentLength = stoi(headers["Content-length"]);
            }
            // 缺少Content-length首部字段
            else
            {
                error = true;
                handleError(fd, 400, "Bad Request: lack of argument (Content-length)");
                break;
            }
            if (inBuf.size() < contentLength)
                break;
            state = STATE_ANALYSIS;
        }

        if (state == STATE_ANALYSIS)
        {
            int flag = this->parseRequest();
            if (flag == ANALYSIS_SUCCESS)
            {
                state = STATE_FINISH;
                break;
            }
            else 
            {
                error = true;
                break;
            }
        }
    } while (false);

    // 如果出现错误
    if (!error)
    {
        if (outBuf.size() > 0)
            events |= EPOLLOUT;
        if (state == STATE_FINISH)
        {
            std::cout << "keepAlive = " << keepAlive << std::endl;
            if (keepAlive)
            {
                this->reset();
                events |= EPOLLIN;
            }
            else 
                return;
        }
        else 
            events |= EPOLLIN;
    }
}

// 处理当前连接的写请求
void RequestData::handleWrite()
{
    if (!error)
    {
        if (writen(fd, outBuf) < 0)
        {
            perror("writen");
            events = 0;
            error = true;
        }
        else if (outBuf.size() > 0)
            events |= EPOLLOUT;
    }
}

// 处理当前连接的连接相关问题
void RequestData::handleConn()
{
    if (!error)
    {
        if (events != 0)
        {
            int timeout = 2000;
            if (keepAlive)
                timeout = 5 * 60 * 1000;
            isRead = false;
            isWrite = false;
            Epoll::addTimer(shared_from_this(), timeout);
            if ((events & EPOLLIN) && (events & EPOLLOUT))
            {
                events = __uint32_t(0);
                events |= EPOLLOUT;
            }
            
        }
    }
}