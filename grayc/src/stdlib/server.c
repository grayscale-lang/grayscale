/*
 * server.c — Implementation of the server stdlib module.
 * HTTP server with routing, path parameters, CORS support, and
 * thread-per-connection concurrency using POSIX sockets.
 *
 * Author:  Marshall A Burns (@SchoolyB)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

#include "server.h"
#include "net.h"
#include "../runtime/atomic.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "../runtime/net_rt.h"
#include <pthread.h>

#define GRAY_SERVER_BUF_SIZE          65536
#define GRAY_SERVER_REQUEST_ARENA     (64 * 1024)
#define GRAY_SERVER_CORS_BUF          512
#define GRAY_ROUTER_INITIAL_CAP       16
#define GRAY_HTTP_METHOD_BUF          16
#define GRAY_HTTP_PATH_BUF_SERVER     2048
#define GRAY_SERVER_MAX_CONNECTIONS   1024
#define GRAY_SERVER_READ_TIMEOUT_MS   30000

static int32_t active_connections = 0;

static const char *http_reason_phrase(int status) {
    switch (status) {
        case 200: return "OK";
        case 201: return "Created";
        case 204: return "No Content";
        case 301: return "Moved Permanently";
        case 302: return "Found";
        case 303: return "See Other";
        case 304: return "Not Modified";
        case 307: return "Temporary Redirect";
        case 308: return "Permanent Redirect";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 409: return "Conflict";
        case 410: return "Gone";
        case 422: return "Unprocessable Entity";
        case 429: return "Too Many Requests";
        case 500: return "Internal Server Error";
        case 501: return "Not Implemented";
        case 502: return "Bad Gateway";
        case 503: return "Service Unavailable";
        default:  return "Unknown";
    }
}

/* Global arena for server allocations */
static GrayArena *server_arena = NULL;

static GrayArena *get_server_arena(void) {
    if (!server_arena) {
        server_arena = gray_arena_create(GRAY_DEFAULT_ARENA_SIZE);
    }
    return server_arena;
}

GrayRouter gray_server_router(void) {
    GrayRouter r;
    r.count = 0;
    r.capacity = GRAY_ROUTER_INITIAL_CAP;
    r.routes = malloc(sizeof(GrayRoute) * r.capacity);
    r.cors_origin = NULL;
    r.middlewares = NULL;
    r.mw_count = 0;
    r.mw_capacity = 0;
    return r;
}

void gray_server_route(GrayRouter *r, GrayString method, GrayString pattern,
                     GrayResponse (*handler)(GrayRequest)) {
    if (r->count >= r->capacity) {
        r->capacity *= 2;
        void *tmp = realloc(r->routes, sizeof(GrayRoute) * r->capacity);
        if (!tmp) {
            fprintf(stderr, "gray: out of memory\n");
            exit(1);
        }
        r->routes = tmp;
    }
    GrayRoute *route = &r->routes[r->count++];

    /* Null-terminate method and pattern */
    GrayArena *arena = get_server_arena();
    char *method_copy = gray_arena_alloc(arena, method.len + 1);
    memcpy(method_copy, method.data, method.len);
    method_copy[method.len] = '\0';
    route->method = method_copy;

    char *pattern_copy = gray_arena_alloc(arena, pattern.len + 1);
    memcpy(pattern_copy, pattern.data, pattern.len);
    pattern_copy[pattern.len] = '\0';
    route->pattern = pattern_copy;

    route->handler = handler;
}

void gray_server_cors(GrayRouter *r, GrayString origin) {
    for (int32_t i = 0; i < origin.len; i++) {
        if (origin.data[i] == '\r' || origin.data[i] == '\n') {
            gray_panic_code("P0101", "server.cors: origin contains CR or LF — HTTP header injection is not allowed");
        }
    }
    GrayArena *arena = get_server_arena();
    char *origin_copy = gray_arena_alloc(arena, origin.len + 1);
    memcpy(origin_copy, origin.data, origin.len);
    origin_copy[origin.len] = '\0';
    r->cors_origin = origin_copy;
}

void gray_server_use(GrayRouter *r, GrayMiddleware fn) {
    if (r->mw_count >= r->mw_capacity) {
        r->mw_capacity = r->mw_capacity == 0 ? 8 : r->mw_capacity * 2;
        r->middlewares = realloc(r->middlewares, sizeof(GrayMiddleware) * r->mw_capacity);
    }
    r->middlewares[r->mw_count++] = fn;
}

/* Check if a route pattern matches a path, extracting params */
static bool match_route(const char *pattern, const char *path,
                        GrayArena *arena, GrayMap *params) {
    const char *pattern_ptr = pattern;
    const char *path_ptr = path;

    while (*pattern_ptr && *path_ptr) {
        if (*pattern_ptr == ':') {
            /* Path parameter — extract name and value */
            pattern_ptr++; /* skip : */
            const char *name_start = pattern_ptr;
            while (*pattern_ptr && *pattern_ptr != '/') pattern_ptr++;
            int32_t name_len = (int32_t)(pattern_ptr - name_start);

            const char *val_start = path_ptr;
            while (*path_ptr && *path_ptr != '/') path_ptr++;
            int32_t val_len = (int32_t)(path_ptr - val_start);

            GrayString key = gray_string_new(arena, name_start, name_len);
            GrayString val = gray_string_new(arena, val_start, val_len);
            GRAY_MAP_SET(arena, params, &key, &val);
        } else {
            if (*pattern_ptr != *path_ptr) return false;
            pattern_ptr++;
            path_ptr++;
        }
    }

    /* Both must be fully consumed (or both at trailing /) */
    if (*pattern_ptr == '\0' && *path_ptr == '\0') return true;
    if (*pattern_ptr == '\0' && *path_ptr == '/' && *(path_ptr+1) == '\0') return true;
    if (*path_ptr == '\0' && *pattern_ptr == '/' && *(pattern_ptr+1) == '\0') return true;
    return false;
}

/* Parse HTTP request from raw data */
static bool parse_request(GrayArena *arena, const char *data, int data_len,
                          GrayRequest *req) {
    if (data_len < 10) return false;

    /* Parse request line: METHOD /path HTTP/1.1 */
    const char *first_space = memchr(data, ' ', data_len);
    if (!first_space) return false;
    req->method = gray_string_new(arena, data, (int32_t)(first_space - data));

    const char *path_start = first_space + 1;
    const char *second_space = memchr(path_start, ' ', data_len - (path_start - data));
    if (!second_space) return false;

    /* Split path and query string */
    const char *qmark = memchr(path_start, '?', second_space - path_start);
    if (qmark) {
        req->path = gray_string_new(arena, path_start, (int32_t)(qmark - path_start));
        /* Parse query params */
        const char *query_string = qmark + 1;
        int32_t query_string_length = (int32_t)(second_space - query_string);
        /* Simple key=value&key2=value2 parser */
        const char *cursor = query_string;
        const char *end = query_string + query_string_length;
        while (cursor < end) {
            const char *eq = memchr(cursor, '=', end - cursor);
            if (!eq) break;
            const char *amp = memchr(eq, '&', end - eq);
            if (!amp) amp = end;

            GrayString key = gray_string_new(arena, cursor, (int32_t)(eq - cursor));
            GrayString val = gray_string_new(arena, eq + 1, (int32_t)(amp - eq - 1));
            GRAY_MAP_SET(arena, &req->query, &key, &val);

            cursor = amp + 1;
        }
    } else {
        req->path = gray_string_new(arena, path_start, (int32_t)(second_space - path_start));
    }

    /* Parse headers */
    const char *header_start = strstr(data, "\r\n");
    const char *body_separator = strstr(data, "\r\n\r\n");
    if (header_start) header_start += 2;

    while (header_start && header_start < body_separator) {
        const char *end_of_line = strstr(header_start, "\r\n");
        if (!end_of_line) break;

        const char *colon = memchr(header_start, ':', end_of_line - header_start);
        if (colon) {
            int32_t key_length = (int32_t)(colon - header_start);
            const char *vstart = colon + 1;
            while (*vstart == ' ') vstart++;
            int32_t value_length = (int32_t)(end_of_line - vstart);

            GrayString key = gray_string_new(arena, header_start, key_length);
            GrayString val = gray_string_new(arena, vstart, value_length);
            GRAY_MAP_SET(arena, &req->headers, &key, &val);
        }
        header_start = end_of_line + 2;
    }

    /* Body */
    if (body_separator) {
        const char *body_start = body_separator + 4;
        int32_t body_length = (int32_t)(data_len - (body_start - data));
        if (body_length > 0) {
            req->body = gray_string_new(arena, body_start, body_length);
        }
    }

    return true;
}

/* Connection handler — runs in its own thread */
typedef struct {
    int client_fd;
    GrayRouter *router;
} ConnCtx;

static void *cleanup_connection(ConnCtx *ctx, GrayArena *arena) {
    gray_sock_close(ctx->client_fd);
    free(ctx);
    gray_arena_destroy(arena, __FILE__, __LINE__);
    free(arena);
    gray_atomic_sub32(&active_connections, 1);
    return NULL;
}

static void *handle_connection(void *arg) {
    ConnCtx *ctx = (ConnCtx *)arg;
    GrayArena *arena = gray_arena_create(GRAY_SERVER_REQUEST_ARENA); /* 64KB per request */

    /* Apply read timeout so slow or idle connections don't hold threads indefinitely */
    GRAY_SOCK_TIMEOUT_TYPE tv;
    gray_sock_timeout_value(GRAY_SERVER_READ_TIMEOUT_MS, &tv);
    setsockopt(ctx->client_fd, SOL_SOCKET, SO_RCVTIMEO, gray_sock_timeout_arg(tv), sizeof(tv));

    /* Receive request data */
    char buf[GRAY_SERVER_BUF_SIZE];
    int64_t n = (int64_t)recv(ctx->client_fd, buf, (int)sizeof(buf) - 1, 0);
    if (n <= 0) {
        return cleanup_connection(ctx, arena);
    }
    buf[n] = '\0';

    /* Parse request */
    GrayRequest req;
    req.body = (GrayString){"", 0};
    req.query = gray_map_new(arena, sizeof(GrayString), sizeof(GrayString), 8);
    req.headers = gray_map_new(arena, sizeof(GrayString), sizeof(GrayString), 16);
    req.params = gray_map_new(arena, sizeof(GrayString), sizeof(GrayString), 8);

    GrayResponse resp;
    resp.status = 404;
    resp.body = gray_string_new(arena, "Not Found", sizeof("Not Found") - 1);
    resp.content_type = gray_string_new(arena, "text/plain", sizeof("text/plain") - 1);

    if (parse_request(arena, buf, (int)n, &req)) {
        /* Reject oversized method or path before copying into fixed stack buffers */
        if (req.method.len >= GRAY_HTTP_METHOD_BUF || req.path.len >= GRAY_HTTP_PATH_BUF_SERVER) {
            const char *err = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
            send(ctx->client_fd, err, strlen(err), 0);
            return cleanup_connection(ctx, arena);
        }

        /* Find matching route */
        char method_buf[GRAY_HTTP_METHOD_BUF], path_buf[GRAY_HTTP_PATH_BUF_SERVER];
        memcpy(method_buf, req.method.data, req.method.len);
        method_buf[req.method.len] = '\0';
        memcpy(path_buf, req.path.data, req.path.len);
        path_buf[req.path.len] = '\0';

        for (int i = 0; i < ctx->router->count; i++) {
            GrayRoute *route = &ctx->router->routes[i];
            if (strcmp(route->method, method_buf) == 0 &&
                match_route(route->pattern, path_buf, arena, &req.params)) {
                resp = route->handler(req);
                break;
            }
        }

        /* Run middleware */
        for (int i = 0; i < ctx->router->mw_count; i++) {
            ctx->router->middlewares[i](&req, &resp);
        }
    }

    /* Build HTTP response with optional CORS headers */
    char cors_hdrs[GRAY_SERVER_CORS_BUF];
    cors_hdrs[0] = '\0';
    if (ctx->router->cors_origin) {
        snprintf(cors_hdrs, sizeof(cors_hdrs),
            "Access-Control-Allow-Origin: %s\r\n"
            "Access-Control-Allow-Methods: GET, POST, PUT, DELETE, PATCH, OPTIONS\r\n"
            "Access-Control-Allow-Headers: Content-Type, Authorization\r\n",
            ctx->router->cors_origin);
    }

    char response_buffer[GRAY_SERVER_BUF_SIZE];
    int response_length;
    int status = (int)resp.status;
    bool is_redirect = (status >= 300 && status < 400 && resp.body.len > 0);

    if (is_redirect) {
        response_length = snprintf(response_buffer, sizeof(response_buffer),
            "HTTP/1.1 %d %s\r\n"
            "Location: %.*s\r\n"
            "Content-Length: 0\r\n"
            "%s"
            "Connection: close\r\n"
            "\r\n",
            status, http_reason_phrase(status),
            (int)resp.body.len, resp.body.data,
            cors_hdrs);
    } else {
        response_length = snprintf(response_buffer, sizeof(response_buffer),
            "HTTP/1.1 %d %s\r\n"
            "Content-Type: %.*s\r\n"
            "Content-Length: %d\r\n"
            "%s"
            "Connection: close\r\n"
            "\r\n"
            "%.*s",
            status, http_reason_phrase(status),
            (int)resp.content_type.len, resp.content_type.data,
            (int)resp.body.len,
            cors_hdrs,
            (int)resp.body.len, resp.body.data);
    }

    size_t send_len = (response_length > 0 && (size_t)response_length < sizeof(response_buffer))
        ? (size_t)response_length : sizeof(response_buffer) - 1;
    send(ctx->client_fd, response_buffer, send_len, 0);
    return cleanup_connection(ctx, arena);
}

void gray_server_listen_host(int64_t port, GrayString host, GrayRouter *r) {
    GrayArena *arena = get_server_arena();
    GraySocket listener = gray_net_listen_host(arena, host, port);
    if (listener.fd < 0) {
        fprintf(stderr, "server: failed to listen on %.*s:%d\n", host.len, host.data, (int)port);
        return;
    }

    printf("Grayscale server listening on %.*s:%d\n", host.len, host.data, (int)port);
    fflush(stdout);

    while (1) {
        GraySocket client = gray_net_accept(arena, listener);
        if (client.fd < 0) continue;

        int32_t prev = gray_atomic_add32(&active_connections, 1);
        if (prev >= GRAY_SERVER_MAX_CONNECTIONS) {
            gray_atomic_sub32(&active_connections, 1);
            const char *r503 = "HTTP/1.1 503 Service Unavailable\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
            send(client.fd, r503, strlen(r503), 0);
            gray_sock_close(client.fd);
            continue;
        }

        ConnCtx *ctx = malloc(sizeof(ConnCtx));
        if (!ctx) {
            gray_atomic_sub32(&active_connections, 1);
            gray_sock_close(client.fd);
            continue;
        }
        ctx->client_fd = client.fd;
        ctx->router = r;

        pthread_t thread;
        if (pthread_create(&thread, NULL, handle_connection, ctx) != 0) {
            gray_atomic_sub32(&active_connections, 1);
            fprintf(stderr, "server: failed to create thread\n");
            free(ctx);
            gray_sock_close(client.fd);
            continue;
        }
        pthread_detach(thread);
    }
}

void gray_server_listen(int64_t port, GrayRouter *r) {
    gray_server_listen_host(port, gray_string_lit("0.0.0.0"), r);
}

/* Response builders */
GrayResponse gray_server_text(int64_t status, GrayString body) {
    return (GrayResponse){status, body, gray_string_lit("text/plain")};
}

GrayResponse gray_server_json(int64_t status, GrayString body) {
    return (GrayResponse){status, body, gray_string_lit("application/json")};
}

GrayResponse gray_server_html(int64_t status, GrayString body) {
    return (GrayResponse){status, body, gray_string_lit("text/html")};
}

GrayResponse gray_server_redirect(int64_t status, GrayString location) {
    /* For redirect, body contains Location header value */
    return (GrayResponse){status, location, gray_string_lit("text/plain")};
}
