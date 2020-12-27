#pragma once
#include <cstdlib>
#include <string>

ssize_t readn(int fd, void *buf, size_t n);
ssize_t readn(int fd, std::string &inBuf);
ssize_t writen(int fd, void *buf, size_t n);
ssize_t writen(int fd, std::string &outBuf);
void handleSigpipe();
int setNonBlocking(int fd);