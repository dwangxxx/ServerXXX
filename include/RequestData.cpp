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
    if (timer.lock())
    {
        auto myTimer = timer.lock();
        myTimer->clearReq();
        timer.reset();
    }
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
    keepAlive = false;
    // weak_ptr.lock()获取一个shared_ptr指针
    /* 
    if (timer.lock())
    {
        // timer置空
        timer.reset();
    } */
}

// 将当前RequestData对象与Timer分离
void RequestData::separateTimer()
{
    if (timer.lock())
    {
        auto myTimer(timer.lock());
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
    // 如果未出错
    if (!error)
    {
        if (events != 0)
        {
            // 新增时间信息
            int timeout = 2000;
            if (keepAlive)
                timeout = 5 * 60 * 1000;
            isRead = false;
            isWrite = false;
            // 新增一个Timer定时器
            Epoll::addTimer(shared_from_this(), timeout);
            if ((events & EPOLLIN) && (events & EPOLLOUT))
            {
                events = __uint32_t(0);
                events |= EPOLLOUT;
            }
            events |= (EPOLLET | EPOLLONESHOT);
            __uint32_t events_ = events;
            events = 0;
            // 修改fd的监听事件
            if (Epoll::epollMod(fd, shared_from_this(), events_) < 0)
            {
                std::cout << "Epoll::epollMod error" << std::endl;
            }
        }
        else if (keepAlive)
        {
            events |= (EPOLLIN | EPOLLET | EPOLLONESHOT);
            int timeout = 5 * 60 * 1000;
            isRead = false;
            isWrite = false;
            // 新增一个Timer定时器
            Epoll::addTimer(shared_from_this(), timeout);
            __uint32_t events_ = events;
            events = 0;
            // 修改fd的监听事件
            if (Epoll::epollMod(fd, shared_from_this(), events_) < 0)
            {
                std::cout << "Epoll::epollMod error" << std::endl;
            }
        }
    }
}

// 解析HTTP请求行
int RequestData::parseURI()
{
    std::string &str = inBuf;
    // 读到完整的请求行再开始解析请求
    int pos = str.find('\r', readPos);
    if (pos < 0)
    {
        return PARSE_URI_AGAIN;
    }
    // 去掉请求行所占的空间，节省空间
    std::string requestLine = str.substr(0, pos);
    if (str.size() > pos + 1)
        str = str.substr(pos + 1);
    else 
        str.clear();
    // Method
    pos = requestLine.find("GET");
    if (pos < 0)
    {
        pos = requestLine.find("POST");
        if (pos < 0)
            return PARSE_URI_ERROR;
        else
            method = METHOD_POST;
    }
    else
        method = METHOD_GET;
    pos = requestLine.find("/", pos);
    if (pos < 0)
        return PARSE_URI_ERROR;
    else
    {
        int _pos = requestLine.find(' ', pos);
        if (_pos < 0)
            return PARSE_URI_ERROR;
        else
        {
            if (_pos - pos > 1)
            {
                fileName = requestLine.substr(pos + 1, _pos - pos - 1);
                int __pos = fileName.find('?');
                if (__pos >= 0)
                {
                    fileName = fileName.substr(0, __pos);
                }
            }
                
            else
                fileName = "index.html";
        }
        pos = _pos;
    }
    pos = requestLine.find("/", pos);
    if (pos < 0)
        return PARSE_URI_ERROR;
    else
    {
        if (requestLine.size() - pos <= 3)
            return PARSE_URI_ERROR;
        else
        {
            // HTTP版本
            std::string ver = requestLine.substr(pos + 1, 3);
            if (ver == "1.0")
                version = HTTP_10;
            else if (ver == "1.1")
                version = HTTP_11;
            else
                return PARSE_URI_ERROR;
        }
    }
    return PARSE_URI_SUCCESS;
}

// 解析HTTP请求头部
int RequestData::parseHeader()
{
    std::string &str = inBuf;
    int key_start = -1, key_end = -1, value_start = -1, value_end = -1;
    int readLineBegin = 0;
    bool notFinish = true;
    for (int i = 0; i < str.size() && notFinish; ++i)
    {
        switch(hState)
        {
            case hStart:
            {
                if (str[i] == '\n' || str[i] == '\r')
                    break;
                hState = hKey;
                key_start = i;
                readLineBegin = i;
                break;
            }
            case hKey:
            {
                if (str[i] == ':')
                {
                    key_end = i;
                    if (key_end - key_start <= 0)
                        return PARSE_HEADER_ERROR;
                    hState = hColon;
                }
                else if (str[i] == '\n' || str[i] == '\r')
                    return PARSE_HEADER_ERROR;
                break;  
            }
            case hColon:
            {
                if (str[i] == ' ')
                {
                    hState = hSpaceAfterColon;
                }
                else
                    return PARSE_HEADER_ERROR;
                break;  
            }
            case hSpaceAfterColon:
            {
                hState = hValue;
                value_start = i;
                break;  
            }
            case hValue:
            {
                if (str[i] == '\r')
                {
                    hState = hCR;
                    value_end = i;
                    if (value_end - value_start <= 0)
                        return PARSE_HEADER_ERROR;
                }
                else if (i - value_start > 255)
                    return PARSE_HEADER_ERROR;
                break;  
            }
            case hCR:
            {
                if (str[i] == '\n')
                {
                    hState = hLF;
                    std::string key(str.begin() + key_start, str.begin() + key_end);
                    std::string value(str.begin() + value_start, str.begin() + value_end);
                    headers[key] = value;
                    readLineBegin = i;
                }
                else
                    return PARSE_HEADER_ERROR;
                break;  
            }
            case hLF:
            {
                if (str[i] == '\r')
                {
                    hState = hEndCR;
                }
                else
                {
                    key_start = i;
                    hState = hKey;
                }
                break;
            }
            case hEndCR:
            {
                if (str[i] == '\n')
                {
                    hState = hEndLF;
                }
                else
                    return PARSE_HEADER_ERROR;
                break;
            }
            case hEndLF:
            {
                notFinish = false;
                key_start = i;
                readLineBegin = i;
                break;
            }
        }
    }
    if (hState == hEndLF)
    {
        str = str.substr(readLineBegin);
        return PARSE_HEADER_SUCCESS;
    }
    str = str.substr(readLineBegin);
    return PARSE_HEADER_AGAIN;
}

// 解析HTTP请求数据
int RequestData::parseRequest()
{
    if (method == METHOD_POST)
    {
        std::string header;
        // 响应行
        header += std::string("HTTP/1.1 200 OK\r\n");
        // 响应头部数据
        if(headers.find("Connection") != headers.end() && headers["Connection"] == "keep-alive")
        {
            keepAlive = true;
            header += std::string("Connection: keep-alive\r\n") + "Keep-Alive: timeout=" + std::to_string(5 * 60 * 1000) + "\r\n";
        }
        int length = stoi(headers["Content-length"]);
        std::vector<char> data(inBuf.begin(), inBuf.begin() + length);
        std::cout << " data.size()=" << data.size() << std::endl;
        Mat src = imdecode(data, CV_LOAD_IMAGE_ANYDEPTH|CV_LOAD_IMAGE_ANYCOLOR);
        imwrite("receive.bmp", src);
        std::cout << "1" << std::endl;
        Mat res = stitch(src);
        std::cout << "2" << std::endl;
        std::vector<uchar> data_encode;
        imencode(".png", res, data_encode);
        std::cout << "3" << std::endl;
        header += std::string("Content-length: ") + std::to_string(data_encode.size()) + "\r\n\r\n";
        std::cout << "4" << std::endl;
        // 响应报文数据
        outBuf += header + std::string(data_encode.begin(), data_encode.end());
        std::cout << "5" << std::endl;
        inBuf = inBuf.substr(length);
        return ANALYSIS_SUCCESS;
    }
    else if (method == METHOD_GET)
    {
        std::string header;
        // 响应行
        header += "HTTP/1.1 200 OK\r\n";
        // 响应头部数据
        if(headers.find("Connection") != headers.end() && headers["Connection"] == "keep-alive")
        {
            keepAlive = true;
            header += std::string("Connection: keep-alive\r\n") + "Keep-Alive: timeout=" + std::to_string(5 * 60 * 1000) + "\r\n";
        }
        int dot_pos = fileName.find('.');
        std::string fileType;
        if (dot_pos < 0) 
            fileType = MimeType::getMime("default");
        else
            fileType = MimeType::getMime(fileName.substr(dot_pos));
        struct stat sbuf;
        if (stat(fileName.c_str(), &sbuf) < 0)
        {
            header.clear();
            handleError(fd, 404, "Not Found!");
            return ANALYSIS_ERROR;
        }
        header += "Content-type: " + fileType + "\r\n";
        header += "Content-length: " + std::to_string(sbuf.st_size) + "\r\n";
        // 头部结束
        header += "\r\n";
        outBuf += header;
        int src_fd = open(fileName.c_str(), O_RDONLY, 0);
        char *src_addr = static_cast<char*>(mmap(NULL, sbuf.st_size, PROT_READ, MAP_PRIVATE, src_fd, 0));
        close(src_fd);
        // 源数据
        outBuf += src_addr;
        munmap(src_addr, sbuf.st_size);
        return ANALYSIS_SUCCESS;
    }
    else
        return ANALYSIS_ERROR;
}

void RequestData::handleError(int fd, int err_no, std::string msg)
{
    msg = " " + msg;
    char send_buf[MAX_BUF_SIZE];
    std::string body_buf, header_buf;
    body_buf += "<html><title>Something Error!!!</title>";
    body_buf += "<body bgcolor=\"ffffff\">";
    body_buf += std::to_string(err_no) + msg;
    body_buf += "<hr><em> LinYa's Web Server</em>\n</body></html>";

    header_buf += "HTTP/1.1 " + std::to_string(err_no) + msg + "\r\n";
    header_buf += "Content-type: text/html\r\n";
    header_buf += "Connection: close\r\n";
    header_buf += "Content-length: " + std::to_string(body_buf.size()) + "\r\n";
    header_buf += "\r\n";
    // 错误处理不考虑writen不完的情况
    sprintf(send_buf, "%s", header_buf.c_str());
    writen(fd, send_buf, strlen(send_buf));
    sprintf(send_buf, "%s", body_buf.c_str());
    writen(fd, send_buf, strlen(send_buf));
}

void RequestData::disableWR()
{
    isRead = false;
    isWrite = false;
}

void RequestData::enableRead()
{
    isRead = true;
}

void RequestData::enableWrite()
{
    isWrite = true;
}

bool RequestData::isCanRead()
{
    return isRead;
}

bool RequestData::isCanWrite()
{
    return isWrite;
}