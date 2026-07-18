#pragma once
#include "socket.h"
#include <cstddef>


bool sendAll(const Socket& sock, const void* buf, size_t len);
bool recvAll(const Socket& sock, void* buf, size_t len);