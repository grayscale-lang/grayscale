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

#define GRAY_NET_HOST_BUFFER_SIZE         256
#define GRAY_NET_PORT_BUFFER_SIZE         16
#define GRAY_NET_MAX_RECEIVE_BUFFER_SIZE     1048576
#define GRAY_NET_LISTEN_BACKLOG   128


GraySocket gray_net_dial(GrayArena *arena, GrayString host, int64_t port) {
    (void)arena;
    gray_net_startup();
    GraySocket sock = {-1};

    char host_buffer[GRAY_NET_HOST_BUFFER_SIZE];
    gray_cstr(host, host_buffer, sizeof(host_buffer));

    /* Resolve hostname */
    struct addrinfo hints, *address_results;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    char port_text[GRAY_NET_PORT_BUFFER_SIZE];
    snprintf(port_text, sizeof(port_text), "%d", (int)port);

    if (getaddrinfo(host_buffer, port_text, &hints, &address_results) != 0) {
        return sock;
    }

    int file_descriptor = socket(address_results->ai_family, address_results->ai_socktype, address_results->ai_protocol);
    if (file_descriptor < 0) {
        freeaddrinfo(address_results);
        return sock;
    }

    if (connect(file_descriptor, address_results->ai_addr, address_results->ai_addrlen) != 0) {
        gray_sock_close(file_descriptor);
        freeaddrinfo(address_results);
        return sock;
    }

    freeaddrinfo(address_results);
    sock.file_descriptor = file_descriptor;
    return sock;
}

void gray_net_close(GraySocket sock) {
    if (sock.file_descriptor >= 0) {
        gray_sock_close(sock.file_descriptor);
    }
}

int64_t gray_net_send(GraySocket sock, GrayString data) {
    if (sock.file_descriptor < 0 || !data.data) return -1;
    int64_t sent = (int64_t)send(sock.file_descriptor, data.data, (int)data.len, 0);
    return (int64_t)sent;
}

GrayString gray_net_recv(GrayArena *arena, GraySocket sock, int64_t maximum_bytes) {
    if (sock.file_descriptor < 0 || maximum_bytes <= 0) return (GrayString){"", 0};

    size_t buffer_size = (size_t)maximum_bytes;
    if (buffer_size > GRAY_NET_MAX_RECEIVE_BUFFER_SIZE) buffer_size = GRAY_NET_MAX_RECEIVE_BUFFER_SIZE; /* cap at 1MB */
    char *buffer = gray_arena_alloc_uninitialized(arena, buffer_size + 1);

    int64_t bytes_received = (int64_t)recv(sock.file_descriptor, buffer, (int)buffer_size, 0);
    if (bytes_received <= 0) return (GrayString){"", 0};

    buffer[bytes_received] = '\0';
    return (GrayString){buffer, (int32_t)bytes_received};
}

GraySocket gray_net_listen(GrayArena *arena, int64_t port) {
    (void)arena;
    gray_net_startup();
    GraySocket sock = {-1};

    int file_descriptor = socket(AF_INET, SOCK_STREAM, 0);
    if (file_descriptor < 0) return sock;

    /* Allow port reuse */
    int option = 1;
    setsockopt(file_descriptor, SOL_SOCKET, SO_REUSEADDR, (const char *)&option, sizeof(option));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons((uint16_t)port);

    if (bind(file_descriptor, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        gray_sock_close(file_descriptor);
        return sock;
    }

    if (listen(file_descriptor, GRAY_NET_LISTEN_BACKLOG) != 0) {
        gray_sock_close(file_descriptor);
        return sock;
    }

    sock.file_descriptor = file_descriptor;
    return sock;
}

GraySocket gray_net_accept(GrayArena *arena, GraySocket listener) {
    (void)arena;
    GraySocket sock = {-1};
    if (listener.file_descriptor < 0) return sock;

    struct sockaddr_in client_addr;
    gray_socklen_t client_length = sizeof(client_addr);
    int file_descriptor = accept(listener.file_descriptor, (struct sockaddr *)&client_addr, &client_length);
    if (file_descriptor < 0) return sock;

    sock.file_descriptor = file_descriptor;
    return sock;
}

void gray_net_set_timeout(GraySocket sock, int64_t milliseconds) {
    if (sock.file_descriptor < 0) return;
    GRAY_SOCK_TIMEOUT_TYPE timeout_value;
    gray_sock_timeout_value(milliseconds, &timeout_value);
    setsockopt(sock.file_descriptor, SOL_SOCKET, SO_RCVTIMEO, gray_sock_timeout_arg(timeout_value), sizeof(timeout_value));
    setsockopt(sock.file_descriptor, SOL_SOCKET, SO_SNDTIMEO, gray_sock_timeout_arg(timeout_value), sizeof(timeout_value));
}

GrayString gray_net_resolve(GrayArena *arena, GrayString hostname) {
    gray_net_startup();
    char host_buffer[GRAY_NET_HOST_BUFFER_SIZE];
    gray_cstr(hostname, host_buffer, sizeof(host_buffer));

    struct addrinfo hints, *address_results;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;

    if (getaddrinfo(host_buffer, NULL, &hints, &address_results) != 0) {
        return (GrayString){"", 0};
    }

    struct sockaddr_in *addr = (struct sockaddr_in *)address_results->ai_addr;
    char ip_buffer[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &addr->sin_addr, ip_buffer, sizeof(ip_buffer));

    GrayString result = gray_string_new(arena, ip_buffer, (int32_t)strlen(ip_buffer));
    freeaddrinfo(address_results);
    return result;
}

/* _result variants */

GrayResult_socket gray_net_dial_result(GrayArena *arena, GrayString host, int64_t port) {
    GrayResult_socket result;
    result.v0 = gray_net_dial(arena, host, port);
    if (result.v0.file_descriptor < 0) {
        result.v1 = gray_error_new(arena, gray_errno_code(errno), gray_string_format(arena, "cannot connect to '%.*s:%lld'",
            host.len, host.data, (long long)port));
    } else {
        result.v1 = NULL;
    }
    return result;
}

GraySocket gray_net_listen_host(GrayArena *arena, GrayString host, int64_t port) {
    (void)arena;
    gray_net_startup();
    GraySocket sock = {-1};

    int file_descriptor = socket(AF_INET, SOCK_STREAM, 0);
    if (file_descriptor < 0) return sock;

    int option = 1;
    setsockopt(file_descriptor, SOL_SOCKET, SO_REUSEADDR, (const char *)&option, sizeof(option));

    char host_buffer[GRAY_NET_HOST_BUFFER_SIZE];
    gray_cstr(host, host_buffer, sizeof(host_buffer));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);

    if (inet_pton(AF_INET, host_buffer, &addr.sin_addr) != 1) {
        gray_sock_close(file_descriptor);
        return sock;
    }

    if (bind(file_descriptor, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        gray_sock_close(file_descriptor);
        return sock;
    }

    if (listen(file_descriptor, GRAY_NET_LISTEN_BACKLOG) != 0) {
        gray_sock_close(file_descriptor);
        return sock;
    }

    sock.file_descriptor = file_descriptor;
    return sock;
}

GrayResult_socket gray_net_listen_result(GrayArena *arena, int64_t port) {
    GrayResult_socket result;
    result.v0 = gray_net_listen(arena, port);
    if (result.v0.file_descriptor < 0) {
        result.v1 = gray_error_new(arena, gray_errno_code(errno), gray_string_format(arena, "cannot listen on port %lld",
            (long long)port));
    } else {
        result.v1 = NULL;
    }
    return result;
}

GrayResult_socket gray_net_listen_host_result(GrayArena *arena, GrayString host, int64_t port) {
    GrayResult_socket result;
    result.v0 = gray_net_listen_host(arena, host, port);
    if (result.v0.file_descriptor < 0) {
        result.v1 = gray_error_new(arena, gray_errno_code(errno), gray_string_format(arena, "cannot listen on %.*s:%lld",
            host.len, host.data, (long long)port));
    } else {
        result.v1 = NULL;
    }
    return result;
}

GrayResult_socket gray_net_accept_result(GrayArena *arena, GraySocket listener) {
    GrayResult_socket result;
    result.v0 = gray_net_accept(arena, listener);
    if (result.v0.file_descriptor < 0) {
        result.v1 = gray_error_new(arena, gray_errno_code(errno), gray_string_format(arena, "accept failed on fd %d", listener.file_descriptor));
    } else {
        result.v1 = NULL;
    }
    return result;
}

GrayResult_i64 gray_net_send_result(GrayArena *arena, GraySocket sock, GrayString data) {
    GrayResult_i64 result;
    result.v0 = gray_net_send(sock, data);
    if (result.v0 < 0) {
        result.v1 = gray_error_new(arena, gray_errno_code(errno), gray_string_format(arena, "send failed on fd %d", sock.file_descriptor));
    } else {
        result.v1 = NULL;
    }
    return result;
}

GrayResult_string gray_net_recv_result(GrayArena *arena, GraySocket sock, int64_t maximum_bytes) {
    GrayResult_string result;
    result.v0 = gray_net_recv(arena, sock, maximum_bytes);
    if (result.v0.len == 0 && sock.file_descriptor >= 0) {
        result.v1 = gray_error_new(arena, GRAY_ERR_Closed, gray_string_format(arena, "recv returned no data on fd %d", sock.file_descriptor));
    } else {
        result.v1 = NULL;
    }
    return result;
}

GrayResult_string gray_net_resolve_result(GrayArena *arena, GrayString hostname) {
    GrayResult_string result;
    result.v0 = gray_net_resolve(arena, hostname);
    if (result.v0.len == 0) {
        result.v1 = gray_error_new(arena, GRAY_ERR_NotFound, gray_string_format(arena, "cannot resolve '%.*s'",
            hostname.len, hostname.data));
    } else {
        result.v1 = NULL;
    }
    return result;
}
