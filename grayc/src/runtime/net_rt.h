/*
 * net_rt.h — Socket portability layer for the networking stdlib modules.
 *
 * Include this instead of <sys/socket.h> and friends. It maps the BSD socket
 * surface the modules use onto Winsock2, so net.c, server.c, and http.c stay
 * written against one API.
 *
 * Author:  Aristomedes (@Aristomedes)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

#ifndef GRAY_NET_RT_H
#define GRAY_NET_RT_H

#include "platform_rt.h"

#if GRAY_RUNTIME_WINDOWS

/* winsock2.h must precede windows.h, which win32.h pulls in. */
#include <winsock2.h>
#include <ws2tcpip.h>
#include "win32.h"

typedef int gray_socklen_t;

#define gray_sock_close closesocket

/* Winsock's SO_RCVTIMEO and SO_SNDTIMEO take a DWORD count of milliseconds,
 * not the struct timeval that POSIX expects. Passing a timeval here silently
 * configures a nonsense timeout. */
#define GRAY_SOCK_TIMEOUT_TYPE DWORD
#define gray_sock_timeout_value(milliseconds, out) (*(out) = (DWORD)(milliseconds))
#define gray_sock_timeout_arg(timeout) ((const char *)&(timeout))

/* Winsock needs process-wide initialization before any socket call, and every
 * public entry point in these modules may be the first one reached. */
static inline void gray_net_startup(void) {
    static volatile LONG started = 0;
    if (InterlockedCompareExchange(&started, 1, 0) == 0) {
        WSADATA winsock_data;
        WSAStartup(MAKEWORD(2, 2), &winsock_data);
    }
}

#else /* POSIX */

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

typedef socklen_t gray_socklen_t;

#define gray_sock_close close

#define GRAY_SOCK_TIMEOUT_TYPE struct timeval
#define gray_sock_timeout_value(milliseconds, out)                                                           \
    do {                                                                                           \
        (out)->tv_sec = (milliseconds) / 1000;                                                               \
        (out)->tv_usec = ((milliseconds) % 1000) * 1000;                                                     \
    } while (0)
#define gray_sock_timeout_arg(timeout) (&(timeout))

static inline void gray_net_startup(void) {}

#endif

#endif
