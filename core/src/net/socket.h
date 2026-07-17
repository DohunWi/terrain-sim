#pragma once
  
class Socket {
public:
    explicit Socket(int fd);
    ~Socket();

    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
    Socket(Socket&& other) noexcept; 
    Socket& operator=(Socket&& other) noexcept;
    
    int get() const;

private:
    int fd_;
};