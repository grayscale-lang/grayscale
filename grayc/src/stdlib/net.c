/*
 * net.c — Implementation of the net stdlib module.
 * Low-level TCP and UDP networking using POSIX sockets, providing
 * connect, listen, accept, send, and receive operations.
 *
 * Author:  Marshall A Burns (@SchoolyB)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

#include "net.h"
#include <string.h>
#include <stdio.h>
#include "../runtime/net_rt.h"
#include <errno.h>

#define GRAY_NET_HOST_BUF         256
#define GRAY_NET_PORT_BUF         16
#define GRAY_NET_MAX_RECV_BUF     1048576
#define GRAY_NET_LISTEN_BACKLOG   128


GraySocket gray_net_dial(GrayArena *arena, GrayString host, int64_t port) {
    (void)arena;
    gray_net_startup();
    GraySocket sock = {-1};

    char host_buf[GRAY_NET_HOST_BUF];
    gray_cstr(host, host_buf, sizeof(host_buf));

    /* Resolve hostname */
    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    char port_str[GRAY_NET_PORT_BUF];
    snprintf(port_str, sizeof(port_str), "%d", (int)port);

    if (getaddrinfo(host_buf, port_str, &hints, &res) != 0) {
        return sock;
    }

    int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0) {
        freeaddrinfo(res);
        return sock;
    }

    if (connect(fd, res->ai_addr, res->ai_addrlen) != 0) {
        gray_sock_close(fd);
        freeaddrinfo(res);
        return sock;
    }

    freeaddrinfo(res);
    sock.fd = fd;
    return sock;
}

void gray_net_close(GraySocket sock) {
    if (sock.fd >= 0) {
        gray_sock_close(sock.fd);
    }
}

int64_t gray_net_send(GraySocket sock, GrayString data) {
    if (sock.fd < 0 || !data.data) return -1;
    int64_t sent = (int64_t)send(sock.fd, data.data, (int)data.len, 0);
    return (int64_t)sent;
}

GrayString gray_net_recv(GrayArena *arena, GraySocket sock, int64_t max_bytes) {
    if (sock.fd < 0 || max_bytes <= 0) return (GrayString){"", 0};

    size_t buffer_size = (size_t)max_bytes;
    if (buffer_size > GRAY_NET_MAX_RECV_BUF) buffer_size = GRAY_NET_MAX_RECV_BUF; /* cap at 1MB */
    char *buf = gray_arena_alloc(arena, buffer_size);

    int64_t n = (int64_t)recv(sock.fd, buf, (int)buffer_size, 0);
    if (n <= 0) return (GrayString){"", 0};

    return (GrayString){buf, (int32_t)n};
}

GraySocket gray_net_listen(GrayArena *arena, int64_t port) {
    (void)arena;
    gray_net_startup();
    GraySocket sock = {-1};

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return sock;

    /* Allow port reuse */
    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons((uint16_t)port);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        gray_sock_close(fd);
        return sock;
    }

    if (listen(fd, GRAY_NET_LISTEN_BACKLOG) != 0) {
        gray_sock_close(fd);
        return sock;
    }

    sock.fd = fd;
    return sock;
}

GraySocket gray_net_accept(GrayArena *arena, GraySocket listener) {
    (void)arena;
    GraySocket sock = {-1};
    if (listener.fd < 0) return sock;

    struct sockaddr_in client_addr;
    gray_socklen_t client_len = sizeof(client_addr);
    int fd = accept(listener.fd, (struct sockaddr *)&client_addr, &client_len);
    if (fd < 0) return sock;

    sock.fd = fd;
    return sock;
}

void gray_net_set_timeout(GraySocket sock, int64_t milliseconds) {
    if (sock.fd < 0) return;
    GRAY_SOCK_TIMEOUT_TYPE tv;
    gray_sock_timeout_value(milliseconds, &tv);
    setsockopt(sock.fd, SOL_SOCKET, SO_RCVTIMEO, gray_sock_timeout_arg(tv), sizeof(tv));
    setsockopt(sock.fd, SOL_SOCKET, SO_SNDTIMEO, gray_sock_timeout_arg(tv), sizeof(tv));
}

GrayString gray_net_resolve(GrayArena *arena, GrayString hostname) {
    gray_net_startup();
    char host_buf[GRAY_NET_HOST_BUF];
    gray_cstr(hostname, host_buf, sizeof(host_buf));

    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;

    if (getaddrinfo(host_buf, NULL, &hints, &res) != 0) {
        return (GrayString){"", 0};
    }

    struct sockaddr_in *addr = (struct sockaddr_in *)res->ai_addr;
    char ip_buf[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &addr->sin_addr, ip_buf, sizeof(ip_buf));

    GrayString result = gray_string_new(arena, ip_buf, (int32_t)strlen(ip_buf));
    freeaddrinfo(res);
    return result;
}

/* _result variants */

GrayResult_socket gray_net_dial_result(GrayArena *arena, GrayString host, int64_t port) {
    GrayResult_socket r;
    r.v0 = gray_net_dial(arena, host, port);
    if (r.v0.fd < 0) {
        r.v1 = gray_error_new(arena, gray_string_format(arena, "cannot connect to '%.*s:%lld'",
            host.len, host.data, (long long)port));
    } else {
        r.v1 = NULL;
    }
    return r;
}

GraySocket gray_net_listen_host(GrayArena *arena, GrayString host, int64_t port) {
    (void)arena;
    gray_net_startup();
    GraySocket sock = {-1};

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return sock;

    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof(opt));

    char host_buf[GRAY_NET_HOST_BUF];
    gray_cstr(host, host_buf, sizeof(host_buf));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);

    if (inet_pton(AF_INET, host_buf, &addr.sin_addr) != 1) {
        gray_sock_close(fd);
        return sock;
    }

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        gray_sock_close(fd);
        return sock;
    }

    if (listen(fd, GRAY_NET_LISTEN_BACKLOG) != 0) {
        gray_sock_close(fd);
        return sock;
    }

    sock.fd = fd;
    return sock;
}

GrayResult_socket gray_net_listen_result(GrayArena *arena, int64_t port) {
    GrayResult_socket r;
    r.v0 = gray_net_listen(arena, port);
    if (r.v0.fd < 0) {
        r.v1 = gray_error_new(arena, gray_string_format(arena, "cannot listen on port %lld",
            (long long)port));
    } else {
        r.v1 = NULL;
    }
    return r;
}

GrayResult_socket gray_net_listen_host_result(GrayArena *arena, GrayString host, int64_t port) {
    GrayResult_socket r;
    r.v0 = gray_net_listen_host(arena, host, port);
    if (r.v0.fd < 0) {
        r.v1 = gray_error_new(arena, gray_string_format(arena, "cannot listen on %.*s:%lld",
            host.len, host.data, (long long)port));
    } else {
        r.v1 = NULL;
    }
    return r;
}

GrayResult_socket gray_net_accept_result(GrayArena *arena, GraySocket listener) {
    GrayResult_socket r;
    r.v0 = gray_net_accept(arena, listener);
    if (r.v0.fd < 0) {
        r.v1 = gray_error_new(arena, gray_string_format(arena, "accept failed on fd %d", listener.fd));
    } else {
        r.v1 = NULL;
    }
    return r;
}

GrayResult_int gray_net_send_result(GrayArena *arena, GraySocket sock, GrayString data) {
    GrayResult_int r;
    r.v0 = gray_net_send(sock, data);
    if (r.v0 < 0) {
        r.v1 = gray_error_new(arena, gray_string_format(arena, "send failed on fd %d", sock.fd));
    } else {
        r.v1 = NULL;
    }
    return r;
}

GrayResult_string gray_net_recv_result(GrayArena *arena, GraySocket sock, int64_t max_bytes) {
    GrayResult_string r;
    r.v0 = gray_net_recv(arena, sock, max_bytes);
    if (r.v0.len == 0 && sock.fd >= 0) {
        r.v1 = gray_error_new(arena, gray_string_format(arena, "recv returned no data on fd %d", sock.fd));
    } else {
        r.v1 = NULL;
    }
    return r;
}

GrayResult_string gray_net_resolve_result(GrayArena *arena, GrayString hostname) {
    GrayResult_string r;
    r.v0 = gray_net_resolve(arena, hostname);
    if (r.v0.len == 0) {
        r.v1 = gray_error_new(arena, gray_string_format(arena, "cannot resolve '%.*s'",
            hostname.len, hostname.data));
    } else {
        r.v1 = NULL;
    }
    return r;
}
