#include "socket.h"
#include <unistd.h>       // close


Socket::Socket(int fd): fd_(fd){}
Socket::~Socket() {if(fd_!=-1) close(fd_);}

Socket::Socket(Socket&& other) noexcept : fd_(other.fd_) {
    other.fd_ = -1;   // 원본은 이제 아무것도 안 가진 상태로
}

Socket& Socket::operator=(Socket&& other) noexcept{
    if (fd_ != -1) close(fd_);   // 내가 이미 뭔가 갖고 있었으면 먼저 정리
    fd_ = other.fd_;
    other.fd_ = -1;
    return *this;
}

int Socket::get() const{return fd_;}