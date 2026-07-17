#pragma once
#include "socket.h"

Socket makeListenSocket(int port);
Socket acceptConnection(const Socket& listener);