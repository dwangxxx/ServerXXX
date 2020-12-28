#include "RequestData.h"
#include "util.h"
#include "Epoll.h"
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <queue>
#include <cstdlib>
#include <opencv/cv.h>
#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/opencv.hpp>
using namespace cv;
#include <iostream>
using namespace std;

pthread_once_t MimeType::once_control = PTHREAD_ONCE_INIT;
std::unordered_map<std::string, std::string> MimeType::mime;


void MimeType::init()
{
    mime[".html"] = "text/html";
    mime[".avi"] = "video/x-msvideo";
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

std::string MimeType::getMime(const std::string &suffix)
{
    pthread_once(&once_control, MimeType::init);
    if (mime.find(suffix) == mime.end())
        return mime["default"];
    else
        return mime[suffix];
}

RequestData::RequestData(): 
    readPos(0), 
    state(STATE_PARSE_URI), 
    hState(hStart), 
    keepAlive(false), 
    isAbleRead(true),
    isAbleWrite(false),
    events(0),
    error(false)
{
    cout << "RequestData constructor()" << endl;
}

RequestData::RequestData(int _epollfd, int _fd, std::string _path):
    readPos(0), 
    state(STATE_PARSE_URI), 
    hState(hStart), 
    keepAlive(false), 
    path(_path), 
    fd(_fd), 
    epollfd(_epollfd),
    isAbleRead(true),
    isAbleWrite(false),
    events(0),
    error(false)
{
    cout << "RequestData constructor()" << endl;
}

RequestData::~RequestData()
{
    cout << "~RequestData()" << endl;
    close(fd);
}

void RequestData::linkTimer(shared_ptr<Timer> timer_)
{
    // 使用了weak_ptr
    timer = timer_;
}

int RequestData::getFd()
{
    return fd;
}
void RequestData::setFd(int _fd)
{
    fd = _fd;
}

void RequestData::reset()
{
    inBuf.clear();
    fileName.clear();
    path.clear();
    readPos = 0;
    state = STATE_PARSE_URI;
    hState = hStart;
    headers.clear();
    if (timer.lock())
    {
        shared_ptr<Timer> my_timer(timer.lock());
        my_timer->clearReq();
        timer.reset();
    }
}

void RequestData::seperateTimer()
{
    //cout << "seperateTimer" << endl;
    if (timer.lock())
    {
        shared_ptr<Timer> my_timer(timer.lock());
        my_timer->clearReq();
        timer.reset();
    }
}

void RequestData::handleRead()
{
    do
    {
        int read_num = readn(fd, inBuf);
        if (read_num < 0)
        {
            perror("1");
            error = true;
            handleError(fd, 400, "Bad Request");
            break;
        }
        else if (read_num == 0)
        {
            // 有请求出现但是读不到数据，可能是Request Aborted，或者来自网络的数据没有达到等原因
            // 最可能是对端已经关闭了，统一按照对端已经关闭处理
            error = true;
            break; 
        }

        if (state == STATE_PARSE_URI)
        {
            int flag = this->parseURI();
            if (flag == PARSE_URI_AGAIN)
                break;
            else if (flag == PARSE_URI_ERROR)
            {
                perror("2");
                error = true;
                handleError(fd, 400, "Bad Request");
                break;
            }
            else
                state = STATE_PARSE_HEADERS;
        }
        if (state == STATE_PARSE_HEADERS)
        {
            int flag = this->parseHeaders();
            if (flag == PARSE_HEADER_AGAIN)
                break;
            else if (flag == PARSE_HEADER_ERROR)
            {
                perror("3");
                error = true;
                handleError(fd, 400, "Bad Request");
                break;
            }
            if(method == METHOD_POST)
            {
                // POST方法准备
                state = STATE_RECV_BODY;
            }
            else 
            {
                state = STATE_ANALYSIS;
            }
        }
        if (state == STATE_RECV_BODY)
        {
            int content_length = -1;
            if (headers.find("Content-length") != headers.end())
            {
                content_length = stoi(headers["Content-length"]);
            }
            else
            {
                error = true;
                handleError(fd, 400, "Bad Request: Lack of argument (Content-length)");
                break;
            }
            if (inBuf.size() < content_length)
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

    if (!error)
    {
        if (outBuf.size() > 0)
            events |= EPOLLOUT;
        if (state == STATE_FINISH)
        {
            cout << "keepAlive=" << keepAlive << endl;
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

void RequestData::handleConn()
{
    if (!error)
    {
        if (events != 0)
        {
            // 一定要先加时间信息，否则可能会出现double free错误。
            // 新增时间信息
            int timeout = 2000;
            if (keepAlive)
                timeout = 5 * 60 * 1000;
            isAbleRead = false;
            isAbleWrite = false;
            Epoll::addTimer(shared_from_this(), timeout);
            if ((events & EPOLLIN) && (events & EPOLLOUT))
            {
                events = __uint32_t(0);
                events |= EPOLLOUT;
            }
            events |= (EPOLLET | EPOLLONESHOT);
            __uint32_t _events = events;
            events = 0;
            if (Epoll::epollMod(fd, shared_from_this(), _events) < 0)
            {
                printf("Epoll::epoll_mod error\n");
            }
        }
        else if (keepAlive)
        {
            events |= (EPOLLIN | EPOLLET | EPOLLONESHOT);
            int timeout = 5 * 60 * 1000;
            isAbleRead = false;
            isAbleWrite = false;
            Epoll::addTimer(shared_from_this(), timeout);
            __uint32_t _events = events;
            events = 0;
            if (Epoll::epollAdd(fd, shared_from_this(), _events) < 0)
            {
                printf("Epoll::epoll_mod error\n");
            }
        }
    }
}

// 解析请求URI
int RequestData::parseURI()
{
    string &str = inBuf;
    // 读到完整的请求行再开始解析请求
    int pos = str.find('\r', readPos);
    if (pos < 0)
    {
        return PARSE_URI_AGAIN;
    }
    // 去掉请求行所占的空间，节省空间
    string request_line = str.substr(0, pos);
    if (str.size() > pos + 1)
        str = str.substr(pos + 1);
    else 
        str.clear();
    // Method
    pos = request_line.find("GET");
    if (pos < 0)
    {
        pos = request_line.find("POST");
        if (pos < 0)
            return PARSE_URI_ERROR;
        else
            method = METHOD_POST;
    }
    else
        method = METHOD_GET;
    //printf("method = %d\n", method);
    // filename
    pos = request_line.find("/", pos);
    if (pos < 0)
        return PARSE_URI_ERROR;
    else
    {
        int _pos = request_line.find(' ', pos);
        if (_pos < 0)
            return PARSE_URI_ERROR;
        else
        {
            if (_pos - pos > 1)
            {
                fileName = request_line.substr(pos + 1, _pos - pos - 1);
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
    // HTTP 版本号
    pos = request_line.find("/", pos);
    if (pos < 0)
        return PARSE_URI_ERROR;
    else
    {
        if (request_line.size() - pos <= 3)
            return PARSE_URI_ERROR;
        else
        {
            string ver = request_line.substr(pos + 1, 3);
            if (ver == "1.0")
                HTTPversion = HTTP_10;
            else if (ver == "1.1")
                HTTPversion = HTTP_11;
            else
                return PARSE_URI_ERROR;
        }
    }
    return PARSE_URI_SUCCESS;
}

// 解析请求头部
int RequestData::parseHeaders()
{
    string &str = inBuf;
    int key_start = -1, key_end = -1, value_start = -1, value_end = -1;
    int now_read_line_begin = 0;
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
                now_read_line_begin = i;
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
                    hState = hSpacesAfterColon;
                }
                else
                    return PARSE_HEADER_ERROR;
                break;  
            }
            case hSpacesAfterColon:
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
                    string key(str.begin() + key_start, str.begin() + key_end);
                    string value(str.begin() + value_start, str.begin() + value_end);
                    headers[key] = value;
                    now_read_line_begin = i;
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
                now_read_line_begin = i;
                break;
            }
        }
    }
    if (hState == hEndLF)
    {
        str = str.substr(now_read_line_begin);
        return PARSE_HEADER_SUCCESS;
    }
    str = str.substr(now_read_line_begin);
    return PARSE_HEADER_AGAIN;
}

// 解析请求
int RequestData::parseRequest()
{
    // POST请求
    if (method == METHOD_POST)
    {
        //get inBuffer
        string header;
        header += string("HTTP/1.1 200 OK\r\n");
        if(headers.find("Connection") != headers.end() && headers["Connection"] == "keep-alive")
        {
            keepAlive = true;
            header += string("Connection: keep-alive\r\n") + "Keep-Alive: timeout=" + to_string(5 * 60 * 1000) + "\r\n";
        }
        int length = stoi(headers["Content-length"]);
        vector<char> data(inBuf.begin(), inBuf.begin() + length);
        cout << " data.size()=" << data.size() << endl;
        Mat src = imdecode(data, CV_LOAD_IMAGE_ANYDEPTH|CV_LOAD_IMAGE_ANYCOLOR);
        imwrite("receive.bmp", src);
        cout << "1" << endl;
        Mat res = stitch(src);
        cout << "2" << endl;
        vector<uchar> data_encode;
        imencode(".png", res, data_encode);
        cout << "3" << endl;
        header += string("Content-length: ") + to_string(data_encode.size()) + "\r\n\r\n";
        cout << "4" << endl;
        outBuf += header + string(data_encode.begin(), data_encode.end());
        cout << "5" << endl;
        inBuf = inBuf.substr(length);
        return ANALYSIS_SUCCESS;
    }
    // GET请求
    else if (method == METHOD_GET)
    {
        string header;
        header += "HTTP/1.1 200 OK\r\n";
        if(headers.find("Connection") != headers.end() && headers["Connection"] == "keep-alive")
        {
            keepAlive = true;
            header += string("Connection: keep-alive\r\n") + "Keep-Alive: timeout=" + to_string(5 * 60 * 1000) + "\r\n";
        }
        int dot_pos = fileName.find('.');
        string filetype;
        if (dot_pos < 0) 
            filetype = MimeType::getMime("default");
        else
            filetype = MimeType::getMime(fileName.substr(dot_pos));
        struct stat sbuf;
        if (stat(fileName.c_str(), &sbuf) < 0)
        {
            header.clear();
            handleError(fd, 404, "Not Found!");
            return ANALYSIS_ERROR;
        }
        header += "Content-type: " + filetype + "\r\n";
        header += "Content-length: " + to_string(sbuf.st_size) + "\r\n";
        // 头部结束
        header += "\r\n";
        outBuf += header;
        int src_fd = open(fileName.c_str(), O_RDONLY, 0);
        char *src_addr = static_cast<char*>(mmap(NULL, sbuf.st_size, PROT_READ, MAP_PRIVATE, src_fd, 0));
        close(src_fd);
        outBuf += src_addr;
        munmap(src_addr, sbuf.st_size);
        return ANALYSIS_SUCCESS;
    }
    else
        return ANALYSIS_ERROR;
}

void RequestData::handleError(int fd, int err_num, string short_msg)
{
    short_msg = " " + short_msg;
    char send_buff[MAX_BUF_SIZE];
    string body_buff, header_buff;
    body_buff += "<html><title>哎~出错了</title>";
    body_buff += "<body bgcolor=\"ffffff\">";
    body_buff += to_string(err_num) + short_msg;
    body_buff += "<hr><em> LinYa's Web Server</em>\n</body></html>";

    header_buff += "HTTP/1.1 " + to_string(err_num) + short_msg + "\r\n";
    header_buff += "Content-type: text/html\r\n";
    header_buff += "Connection: close\r\n";
    header_buff += "Content-length: " + to_string(body_buff.size()) + "\r\n";
    header_buff += "\r\n";
    // 错误处理不考虑writen不完的情况
    sprintf(send_buff, "%s", header_buff.c_str());
    writen(fd, send_buff, strlen(send_buff));
    sprintf(send_buff, "%s", body_buff.c_str());
    writen(fd, send_buff, strlen(send_buff));
}


void RequestData::disableWR()
{
    isAbleRead = false;
    isAbleWrite = false;
}
void RequestData::enableRead()
{
    isAbleRead = true;
}
void RequestData::enableWrite()
{
    isAbleWrite = true;
}
bool RequestData::isCanRead()
{
    return isAbleRead;
}
bool RequestData::isCanWrite()
{
    return isAbleWrite;
}