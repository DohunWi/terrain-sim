#include <sys/socket.h>
#include <cerrno>
#include "io.h"

bool sendAll(const Socket& sock, const void* buf, size_t len){
    size_t done = 0;
    while (done < len){
        ssize_t result = send(sock.get(), (char*)buf+done, len-done, 0);
        if (result > 0){
            done += result;
        }
        else if(result ==0){
            return false;
        }
        else{
            if(errno == EINTR){
                continue;
            }
            else{
                return false;    
            }
        }
    }
    return true;
}
bool recvAll(const Socket& sock, void* buf, size_t len){
    size_t done = 0;
    while (done < len){
        ssize_t result = recv(sock.get(), (char*)buf+done, len-done, 0);
        if (result > 0){
            done += result;
        }
        else if(result == 0){
            return false;
        }
        else{
            if(errno == EINTR){
                continue;
            }
            else{
                return false;    
            }
        }
    }
    return true;
}