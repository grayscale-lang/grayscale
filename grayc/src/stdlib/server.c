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

#define GRAY_SERVER_BUFFER_SIZE          65536
#define GRAY_SERVER_REQUEST_ARENA     (64 * 1024)
#define GRAY_SERVER_CORS_BUFFER_SIZE          512
#define GRAY_ROUTER_INITIAL_CAPACITY       16
#define GRAY_HTTP_METHOD_BUFFER_SIZE          16
#define GRAY_HTTP_PATH_BUFFER_SIZE_SERVER     2048
#define GRAY_SERVER_MAX_CONNECTIONS   1024
#define GRAY_SERVER_READ_TIMEOUT_MILLISECONDS   30000

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
    GrayRouter router;
    router.count = 0;
    router.capacity = GRAY_ROUTER_INITIAL_CAPACITY;
    router.routes = malloc(sizeof(GrayRoute) * router.capacity);
    router.cors_origin = NULL;
    router.middlewares = NULL;
    router.middleware_count = 0;
    router.middleware_capacity = 0;
    return router;
}

void gray_server_route(GrayRouter *router, GrayString method, GrayString pattern,
                     GrayResponse (*handler)(GrayRequest)) {
    if (router->count >= router->capacity) {
        router->capacity *= 2;
        void *resized_routes = realloc(router->routes, sizeof(GrayRoute) * router->capacity);
        if (!resized_routes) {
            fprintf(stderr, "gray: out of memory\n");
            exit(1);
        }
        router->routes = resized_routes;
    }
    GrayRoute *route = &router->routes[router->count++];

    /* Null-terminate method and pattern */
    GrayArena *arena = get_server_arena();
    char *method_copy = gray_arena_alloc_uninitialized(arena,method.len + 1);
    memcpy(method_copy, method.data, method.len);
    method_copy[method.len] = '\0';
    route->method = method_copy;

    char *pattern_copy = gray_arena_alloc_uninitialized(arena,pattern.len + 1);
    memcpy(pattern_copy, pattern.data, pattern.len);
    pattern_copy[pattern.len] = '\0';
    route->pattern = pattern_copy;

    route->handler = handler;
}

void gray_server_cors(GrayRouter *router, GrayString origin) {
    for (int32_t i = 0; i < origin.len; i++) {
        if (origin.data[i] == '\r' || origin.data[i] == '\n') {
            gray_panic_code("P0101", "server.cors: origin contains CR or LF — HTTP header injection is not allowed");
        }
    }
    GrayArena *arena = get_server_arena();
    char *origin_copy = gray_arena_alloc_uninitialized(arena,origin.len + 1);
    memcpy(origin_copy, origin.data, origin.len);
    origin_copy[origin.len] = '\0';
    router->cors_origin = origin_copy;
}

void gray_server_use(GrayRouter *router, GrayMiddleware middleware) {
    if (router->middleware_count >= router->middleware_capacity) {
        router->middleware_capacity = router->middleware_capacity == 0 ? 8 : router->middleware_capacity * 2;
        router->middlewares = realloc(router->middlewares, sizeof(GrayMiddleware) * router->middleware_capacity);
    }
    router->middlewares[router->middleware_count++] = middleware;
}

/* Check if a route pattern matches a path, extracting params */
static bool match_route(const char *pattern, const char *path,
                        GrayArena *arena, GrayMap *parameters) {
    const char *pattern_cursor = pattern;
    const char *path_cursor = path;

    while (*pattern_cursor && *path_cursor) {
        if (*pattern_cursor == ':') {
            /* Path parameter — extract name and value */
            pattern_cursor++; /* skip : */
            const char *name_start = pattern_cursor;
            while (*pattern_cursor && *pattern_cursor != '/') pattern_cursor++;
            int32_t name_length = (int32_t)(pattern_cursor - name_start);

            const char *value_start = path_cursor;
            while (*path_cursor && *path_cursor != '/') path_cursor++;
            int32_t value_length = (int32_t)(path_cursor - value_start);

            GrayString key = gray_string_new(arena, name_start, name_length);
            GrayString value = gray_string_new(arena, value_start, value_length);
            GRAY_MAP_SET(arena, parameters, &key, &value);
        } else {
            if (*pattern_cursor != *path_cursor) return false;
            pattern_cursor++;
            path_cursor++;
        }
    }

    /* Both must be fully consumed (or both at trailing /) */
    if (*pattern_cursor == '\0' && *path_cursor == '\0') return true;
    if (*pattern_cursor == '\0' && *path_cursor == '/' && *(path_cursor+1) == '\0') return true;
    if (*path_cursor == '\0' && *pattern_cursor == '/' && *(pattern_cursor+1) == '\0') return true;
    return false;
}

/* Parse HTTP request from raw data */
static bool parse_request(GrayArena *arena, const char *data, int data_length,
                          GrayRequest *request) {
    if (data_length < 10) return false;

    /* Parse request line: METHOD /path HTTP/1.1 */
    const char *first_space = memchr(data, ' ', data_length);
    if (!first_space) return false;
    request->method = gray_string_new(arena, data, (int32_t)(first_space - data));

    const char *path_start = first_space + 1;
    const char *second_space = memchr(path_start, ' ', data_length - (path_start - data));
    if (!second_space) return false;

    /* Split path and query string */
    const char *qmark = memchr(path_start, '?', second_space - path_start);
    if (qmark) {
        request->path = gray_string_new(arena, path_start, (int32_t)(qmark - path_start));
        /* Parse query params */
        const char *query_string = qmark + 1;
        int32_t query_string_length = (int32_t)(second_space - query_string);
        /* Simple key=value&key2=value2 parser */
        const char *cursor = query_string;
        const char *end_cursor = query_string + query_string_length;
        while (cursor < end_cursor) {
            const char *equals_sign = memchr(cursor, '=', end_cursor - cursor);
            if (!equals_sign) break;
            const char *ampersand = memchr(equals_sign, '&', end_cursor - equals_sign);
            if (!ampersand) ampersand = end_cursor;

            GrayString key = gray_string_new(arena, cursor, (int32_t)(equals_sign - cursor));
            GrayString value = gray_string_new(arena, equals_sign + 1, (int32_t)(ampersand - equals_sign - 1));
            GRAY_MAP_SET(arena, &request->query, &key, &value);

            cursor = ampersand + 1;
        }
    } else {
        request->path = gray_string_new(arena, path_start, (int32_t)(second_space - path_start));
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
            const char *value_start = colon + 1;
            while (*value_start == ' ') value_start++;
            int32_t value_length = (int32_t)(end_of_line - value_start);

            GrayString key = gray_string_new(arena, header_start, key_length);
            GrayString value = gray_string_new(arena, value_start, value_length);
            GRAY_MAP_SET(arena, &request->headers, &key, &value);
        }
        header_start = end_of_line + 2;
    }

    /* Body */
    if (body_separator) {
        const char *body_start = body_separator + 4;
        int32_t body_length = (int32_t)(data_length - (body_start - data));
        if (body_length > 0) {
            request->body = gray_string_new(arena, body_start, body_length);
        }
    }

    return true;
}

/* Connection handler — runs in its own thread */
typedef struct {
    int client_descriptor;
    GrayRouter *router;
} ConnCtx;

static void *cleanup_connection(ConnCtx *connection_context, GrayArena *arena) {
    gray_sock_close(connection_context->client_descriptor);
    free(connection_context);
    gray_arena_destroy(arena, __FILE__, __LINE__);
    free(arena);
    gray_atomic_sub32(&active_connections, 1);
    return NULL;
}

static void *handle_connection(void *argument) {
    ConnCtx *connection_context = (ConnCtx *)argument;
    GrayArena *arena = gray_arena_create(GRAY_SERVER_REQUEST_ARENA); /* 64KB per request */

    /* Apply read timeout so slow or idle connections don't hold threads indefinitely */
    GRAY_SOCK_TIMEOUT_TYPE timeout_value;
    gray_sock_timeout_value(GRAY_SERVER_READ_TIMEOUT_MILLISECONDS, &timeout_value);
    setsockopt(connection_context->client_descriptor, SOL_SOCKET, SO_RCVTIMEO, gray_sock_timeout_arg(timeout_value), sizeof(timeout_value));

    /* Receive request data */
    char buffer[GRAY_SERVER_BUFFER_SIZE];
    int64_t bytes_received = (int64_t)recv(connection_context->client_descriptor, buffer, (int)sizeof(buffer) - 1, 0);
    if (bytes_received <= 0) {
        return cleanup_connection(connection_context, arena);
    }
    buffer[bytes_received] = '\0';

    /* Parse request */
    GrayRequest request;
    request.body = (GrayString){"", 0};
    request.query = gray_map_new_kind(arena, sizeof(GrayString), sizeof(GrayString), 8, GRAY_ELEM_STRING, GRAY_ELEM_STRING);
    request.headers = gray_map_new_kind(arena, sizeof(GrayString), sizeof(GrayString), 16, GRAY_ELEM_STRING, GRAY_ELEM_STRING);
    request.params = gray_map_new_kind(arena, sizeof(GrayString), sizeof(GrayString), 8, GRAY_ELEM_STRING, GRAY_ELEM_STRING);

    GrayResponse resp;
    resp.status = 404;
    resp.body = gray_string_new(arena, "Not Found", sizeof("Not Found") - 1);
    resp.content_type = gray_string_new(arena, "text/plain", sizeof("text/plain") - 1);

    if (parse_request(arena, buffer, (int)bytes_received, &request)) {
        /* Reject oversized method or path before copying into fixed stack buffers */
        if (request.method.len >= GRAY_HTTP_METHOD_BUFFER_SIZE || request.path.len >= GRAY_HTTP_PATH_BUFFER_SIZE_SERVER) {
            const char *error_text = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
            send(connection_context->client_descriptor, error_text, strlen(error_text), 0);
            return cleanup_connection(connection_context, arena);
        }

        /* Find matching route */
        char method_buffer[GRAY_HTTP_METHOD_BUFFER_SIZE], path_buffer[GRAY_HTTP_PATH_BUFFER_SIZE_SERVER];
        memcpy(method_buffer, request.method.data, request.method.len);
        method_buffer[request.method.len] = '\0';
        memcpy(path_buffer, request.path.data, request.path.len);
        path_buffer[request.path.len] = '\0';

        for (int i = 0; i < connection_context->router->count; i++) {
            GrayRoute *route = &connection_context->router->routes[i];
            if (strcmp(route->method, method_buffer) == 0 &&
                match_route(route->pattern, path_buffer, arena, &request.params)) {
                resp = route->handler(request);
                break;
            }
        }

        /* Run middleware */
        for (int i = 0; i < connection_context->router->middleware_count; i++) {
            connection_context->router->middlewares[i](&request, &resp);
        }
    }

    /* Build HTTP response with optional CORS headers */
    char cors_hdrs[GRAY_SERVER_CORS_BUFFER_SIZE];
    cors_hdrs[0] = '\0';
    if (connection_context->router->cors_origin) {
        snprintf(cors_hdrs, sizeof(cors_hdrs),
            "Access-Control-Allow-Origin: %s\r\n"
            "Access-Control-Allow-Methods: GET, POST, PUT, DELETE, PATCH, OPTIONS\r\n"
            "Access-Control-Allow-Headers: Content-Type, Authorization\r\n",
            connection_context->router->cors_origin);
    }

    char response_buffer[GRAY_SERVER_BUFFER_SIZE];
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

    size_t send_length = (response_length > 0 && (size_t)response_length < sizeof(response_buffer))
        ? (size_t)response_length : sizeof(response_buffer) - 1;
    send(connection_context->client_descriptor, response_buffer, send_length, 0);
    return cleanup_connection(connection_context, arena);
}

void gray_server_listen_host(int64_t port, GrayString host, GrayRouter *router) {
    GrayArena *arena = get_server_arena();
    GraySocket listener = gray_net_listen_host(arena, host, port);
    if (listener.file_descriptor < 0) {
        fprintf(stderr, "server: failed to listen on %.*s:%d\n", host.len, host.data, (int)port);
        return;
    }

    printf("Grayscale server listening on %.*s:%d\n", host.len, host.data, (int)port);
    fflush(stdout);

    while (1) {
        GraySocket client = gray_net_accept(arena, listener);
        if (client.file_descriptor < 0) continue;

        int32_t previous_value = gray_atomic_add32(&active_connections, 1);
        if (previous_value >= GRAY_SERVER_MAX_CONNECTIONS) {
            gray_atomic_sub32(&active_connections, 1);
            const char *service_unavailable_response = "HTTP/1.1 503 Service Unavailable\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
            send(client.file_descriptor, service_unavailable_response, strlen(service_unavailable_response), 0);
            gray_sock_close(client.file_descriptor);
            continue;
        }

        ConnCtx *connection_context = malloc(sizeof(ConnCtx));
        if (!connection_context) {
            gray_atomic_sub32(&active_connections, 1);
            gray_sock_close(client.file_descriptor);
            continue;
        }
        connection_context->client_descriptor = client.file_descriptor;
        connection_context->router = router;

        pthread_t thread;
        if (pthread_create(&thread, NULL, handle_connection, connection_context) != 0) {
            gray_atomic_sub32(&active_connections, 1);
            fprintf(stderr, "server: failed to create thread\n");
            free(connection_context);
            gray_sock_close(client.file_descriptor);
            continue;
        }
        pthread_detach(thread);
    }
}

void gray_server_listen(int64_t port, GrayRouter *router) {
    gray_server_listen_host(port, gray_string_lit("0.0.0.0"), router);
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
