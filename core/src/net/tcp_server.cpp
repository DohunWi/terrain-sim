#include "socket.h"
#include "tcp_server.h"
#include <iostream>
#include <sys/socket.h>   // socket, bind, listen, accept
#include <netinet/in.h>   // sockaddr_in, htons, INADDR_ANY
#include <unistd.h>       // close
#include <cstring>   

Socket makeListenSocket(int port){
    // step 1: make socket
    Socket listener = Socket(socket(AF_INET,SOCK_STREAM,0));
    
    // step 2: bind address
    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;
    bind(listener.get(), (struct sockaddr*)&addr, sizeof(addr));
    // step 3 : listen to connect
    listen(listener.get(), 5);

    return listener;
}

Socket acceptConnection(const Socket& listener){
    // step 4 : accept
    sockaddr_in accept_addr;
    memset(&accept_addr, 0, sizeof(accept_addr));
    socklen_t sl = sizeof(accept_addr);
    Socket accept_sock = Socket(accept(listener.get(), (struct sockaddr*)&accept_addr, &sl));
    
    return accept_sock;
}