#include "util.h"
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <string.h>

const int MAX_BUF_SIZE = 4096;

ssize_t readn(int fd, void *buf, size_t n)
{
    size_t nLeft = n;
    ssize_t nRead = 0;
    ssize_t sumRead = 0;
    char *ptr = (char*)buf;
    while (nLeft > 0)
    {
        if ((nRead = read(fd, ptr, nLeft)) < 0)
        {
            if (errno == EINTR)
                nRead = 0;
            else if (errno == EAGAIN)
            {
                return sumRead;
            }
            else
            {
                return -1;
            }  
        }
        else if (nRead == 0)
            break;
        sumRead += nRead;
        nLeft -= nRead;
        ptr += nRead;
    }
    return sumRead;
}

ssize_t readn(int fd, std::string &inBuf)
{
    ssize_t nRead = 0;
    ssize_t sumRead = 0;
    while (true)
    {
        char buf[MAX_BUF_SIZE];
        if ((nRead = read(fd, buf, MAX_BUF_SIZE)) < 0)
        {
            if (errno == EINTR)
                continue;
            else if (errno == EAGAIN)
            {
                
                return sumRead;
            }  
            else
            {
                perror("read error");
                return -1;
            }
        }
        else if (nRead == 0)
            break;
        sumRead += nRead;
        inBuf += std::string(buf, buf + nRead);
    }
    return sumRead;
}

ssize_t writen(int fd, void *buf, size_t n)
{
    size_t nLeft = n;
    ssize_t nWrite = 0;
    ssize_t sumWrite = 0;
    char *ptr = (char*)buf;
    while (nLeft > 0)
    {
        if ((nWrite = write(fd, ptr, nLeft)) <= 0)
        {
            if (nWrite < 0)
            {
                if (errno == EINTR)
                {
                    nWrite = 0;
                    continue;
                }
                else if (errno == EAGAIN)
                {
                    return sumWrite;
                }
                else
                    return -1;
            }
        }
        sumWrite += nWrite;
        nLeft -= nWrite;
        ptr += nWrite;
    }
    return sumWrite;
}
ssize_t writen(int fd, std::string &outBuf)
{
    size_t nLeft = outBuf.size();
    ssize_t nWrite = 0;
    ssize_t sumWrite = 0;
    const char *ptr = outBuf.c_str();
    while (nLeft > 0)
    {
        if ((nWrite = write(fd, ptr, nLeft)) <= 0)
        {
            if (nWrite < 0)
            {
                if (errno == EINTR)
                {
                    nWrite = 0;
                    continue;
                }
                else if (errno == EAGAIN)
                    break;
                else
                    return -1;
            }
        }
        sumWrite += nWrite;
        nLeft -= nWrite;
        ptr += nWrite;
    }
    if (sumWrite == outBuf.size())
        outBuf.clear();
    else
        outBuf = outBuf.substr(sumWrite);
    return sumWrite;
}

// 处理SIGPIPE信号，设置为忽略，防止服务器异常终止
void handleSigpipe()
{
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = SIG_IGN;
    sa.sa_flags = 0;
    if(sigaction(SIGPIPE, &sa, NULL))
        return;
}

// 设置非阻塞IO
int setNonBlocking(int fd)
{
    int flag = fcntl(fd, F_GETFL, 0);
    if(flag == -1)
        return -1;

    flag |= O_NONBLOCK;
    if(fcntl(fd, F_SETFL, flag) == -1)
        return -1;
    return 0;
}