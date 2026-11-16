/*
 * http.c — Implementation of the http stdlib module.
 * Minimal HTTP/1.1 client using raw POSIX sockets with support for
 * GET, POST, PUT, DELETE, and custom headers. No TLS.
 *
 * Author:  Marshall A Burns (@SchoolyB)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

#include "http.h"
#include "net.h"
#include <ctype.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define GRAY_HTTP_DEFAULT_PORT    80
#define GRAY_HTTP_TIMEOUT_MILLISECONDS      10000
#define GRAY_HTTP_URL_BUFFER_SIZE         4096
#define GRAY_HTTP_HOST_BUFFER_SIZE        256
#define GRAY_HTTP_PATH_BUFFER_SIZE        2048
#define GRAY_HTTP_HEADER_BUFFER_SIZE         4096
#define GRAY_HTTP_RESPONSE_BUFFER_SIZE        1048576
#define GRAY_HTTP_MINIMUM_RESPONSE_LENGTH    12


/* True when `text` starts with `prefix`, ignoring ASCII case. */
static bool starts_with_ignore_case(const char *text, const char *prefix) {
    for (; *prefix; text++, prefix++) {
        char text_char = *text, prefix_char = *prefix;
        if (text_char >= 'A' && text_char <= 'Z') text_char = (char)(text_char - 'A' + 'a');
        if (text_char != prefix_char) return false;
    }
    return true;
}

/* Parse a URL into host, port, and path components. Returns NULL on success,
 * otherwise the failure message, with the matching code in *failure_code. The
 * fragment is dropped; a query directly after the authority gets a "/" path. */
static const char *parse_url(const char *url_text, char *host, size_t host_size,
                             int *port, char *path, size_t path_size, int64_t *failure_code) {
    *failure_code = GRAY_ERR_InvalidInput;
    if (!starts_with_ignore_case(url_text, "http://"))
        return "invalid URL: expected http:// scheme";
    const char *cursor = url_text + 7;

    if (*cursor == '[') {
        *failure_code = GRAY_ERR_Unsupported;
        return "IPv6 address literals are not supported";
    }

    /* The authority ends at the first '/', '?' or '#'. */
    const char *authority_end = cursor + strcspn(cursor, "/?#");

    for (const char *scan_cursor = cursor; scan_cursor < authority_end; scan_cursor++) {
        if (*scan_cursor == '@') return "invalid URL: userinfo is not supported";
    }

    const char *colon = memchr(cursor, ':', (size_t)(authority_end - cursor));
    const char *host_end = colon ? colon : authority_end;
    size_t host_length = (size_t)(host_end - cursor);
    if (host_length == 0) return "invalid URL: empty host";
    if (host_length >= host_size) return "invalid URL: host is too long";
    memcpy(host, cursor, host_length);
    host[host_length] = '\0';

    *port = GRAY_HTTP_DEFAULT_PORT;
    if (colon) {
        if (colon + 1 == authority_end) return "invalid URL: empty port";
        long port_value = 0;
        for (const char *digit = colon + 1; digit < authority_end; digit++) {
            if (*digit < '0' || *digit > '9') return "invalid URL: port must be a number from 1 to 65535";
            port_value = port_value * 10 + (*digit - '0');
            if (port_value > 65535) return "invalid URL: port must be a number from 1 to 65535";
        }
        if (port_value < 1) return "invalid URL: port must be a number from 1 to 65535";
        *port = (int)port_value;
    }

    const char *target = authority_end;
    size_t target_length = strcspn(target, "#");
    size_t prefix_length = (*target == '/' || target_length == 0) ? 0 : 1;
    if (target_length == 0) {
        path[0] = '/';
        path[1] = '\0';
        return NULL;
    }
    if (prefix_length + target_length >= path_size) return "invalid URL: path is too long";
    if (prefix_length) path[0] = '/';
    memcpy(path + prefix_length, target, target_length);
    path[prefix_length + target_length] = '\0';
    return NULL;
}

static bool http_ascii_equal_ignore_case(const char *left, int32_t left_length, const char *right) {
    if ((size_t)left_length != strlen(right)) return false;
    for (int32_t i = 0; i < left_length; i++) {
        char left_char = left[i], right_char = right[i];
        if (left_char >= 'A' && left_char <= 'Z') left_char = (char)(left_char - 'A' + 'a');
        if (right_char >= 'A' && right_char <= 'Z') right_char = (char)(right_char - 'A' + 'a');
        if (left_char != right_char) return false;
    }
    return true;
}

/* True when the response headers carry Transfer-Encoding: chunked. */
static bool response_is_chunked(GrayMap *headers) {
    for (int32_t i = 0; i < headers->order_len; i++) {
        int32_t slot = headers->order[i];
        if (slot < 0) continue;
        GrayString *key = (GrayString *)gray_map_key_at(headers, slot);
        GrayString *value = (GrayString *)gray_map_value_at(headers, slot);
        if (!key || !value || !http_ascii_equal_ignore_case(key->data, key->len, "transfer-encoding")) continue;
        for (int32_t start = 0; start + 7 <= value->len; start++) {
            if (http_ascii_equal_ignore_case(value->data + start, 7, "chunked")) return true;
        }
    }
    return false;
}

/* Decodes a chunked body into its concatenated chunk data. Returns false on a
 * non-hex size, a chunk shorter than its declared size, or a body that ends
 * before the terminating zero-size chunk. */
static bool decode_chunked_body(GrayArena *arena, const char *data, int32_t data_length, GrayString *decoded) {
    char *output = gray_arena_alloc_uninitialized(arena, (size_t)data_length + 1);
    int32_t output_length = 0;
    int32_t position = 0;
    for (;;) {
        /* Size line: hex digits, then an optional ";extension", then CRLF. */
        int64_t size = 0;
        int32_t digit_count = 0;
        while (position < data_length) {
            char digit = data[position];
            int value;
            if (digit >= '0' && digit <= '9') value = digit - '0';
            else if (digit >= 'a' && digit <= 'f') value = digit - 'a' + 10;
            else if (digit >= 'A' && digit <= 'F') value = digit - 'A' + 10;
            else break;
            if (++digit_count > 8) return false;
            size = size * 16 + value;
            position++;
        }
        if (digit_count == 0) return false;
        if (position < data_length && data[position] != ';' && data[position] != '\r' && data[position] != '\n') return false;
        while (position < data_length && data[position] != '\n') position++;
        if (position >= data_length) return false;
        position++; /* the LF */

        if (size == 0) break;
        if (data_length - position < size) return false;
        memcpy(output + output_length, data + position, (size_t)size);
        output_length += (int32_t)size;
        position += (int32_t)size;
        if (position < data_length && data[position] == '\r') position++;
        if (position >= data_length || data[position] != '\n') return false;
        position++;
    }
    output[output_length] = '\0';
    *decoded = (GrayString){output, output_length};
    return true;
}

/* Parse HTTP response: extract status code, headers, body. *malformed is set
 * when a chunked body cannot be decoded. */
static GrayHttpResponse parse_response(GrayArena *arena, const char *data, int data_length, bool *malformed) {
    *malformed = false;
    GrayHttpResponse resp;
    resp.status = 0;
    resp.body = (GrayString){"", 0};
    resp.headers = gray_map_new_kind(arena, sizeof(GrayString), sizeof(GrayString), 16, GRAY_ELEM_STRING, GRAY_ELEM_STRING);

    if (data_length < GRAY_HTTP_MINIMUM_RESPONSE_LENGTH) return resp;

    /* Parse status line: HTTP/1.1 200 OK */
    if (strncmp(data, "HTTP/", 5) == 0) {
        const char *space_position = strchr(data, ' ');
        if (space_position) resp.status = atoi(space_position + 1);
    }

    /* Find header/body separator */
    const char *body_start = NULL;
    const char *header_end = strstr(data, "\r\n\r\n");
    if (header_end) {
        body_start = header_end + 4;
    } else {
        header_end = strstr(data, "\n\n");
        if (header_end) body_start = header_end + 2;
    }

    /* Parse headers */
    const char *line = strchr(data, '\n');
    if (line) line++;
    while (line && line < header_end) {
        const char *end_of_line = strchr(line, '\n');
        /* The last header's LF sits one past header_end in a CRLF separator. */
        if (!end_of_line || end_of_line > header_end + 1) break;

        const char *colon = memchr(line, ':', (size_t)(end_of_line - line));
        if (colon) {
            int32_t key_length = (int32_t)(colon - line);
            const char *value_start = colon + 1;
            while (*value_start == ' ') value_start++;
            int32_t value_length = (int32_t)(end_of_line - value_start);
            if (value_length > 0 && value_start[value_length - 1] == '\r') value_length--;

            GrayString key = gray_string_new(arena, line, key_length);
            /* Canonical form: each hyphen-separated word is Capitalized. */
            char *key_text = (char *)key.data;
            bool word_start = true;
            for (int32_t i = 0; i < key.len; i++) {
                char letter = key_text[i];
                if (letter == '-') { word_start = true; continue; }
                if (word_start && letter >= 'a' && letter <= 'z') key_text[i] = (char)(letter - 'a' + 'A');
                else if (!word_start && letter >= 'A' && letter <= 'Z') key_text[i] = (char)(letter - 'A' + 'a');
                word_start = false;
            }
            GrayString value = gray_string_new(arena, value_start, value_length);
            GRAY_MAP_SET(arena, &resp.headers, &key, &value);
        }
        line = end_of_line + 1;
    }

    /* Body */
    if (body_start) {
        int32_t body_length = (int32_t)(data_length - (int)(body_start - data));
        if (body_length > 0 && response_is_chunked(&resp.headers)) {
            if (!decode_chunked_body(arena, body_start, body_length, &resp.body)) *malformed = true;
        } else if (body_length > 0) {
            resp.body = gray_string_new(arena, body_start, body_length);
        }
    }

    return resp;
}

/* True when `headers` has an entry named `name`, compared case-insensitively. */
static bool headers_contain(GrayMap *headers, const char *name) {
    if (!headers) return false;
    for (int32_t i = 0; i < headers->order_len; i++) {
        int32_t slot = headers->order[i];
        if (slot < 0) continue;
        GrayString *key = (GrayString *)gray_map_key_at(headers, slot);
        if (key && http_ascii_equal_ignore_case(key->data, key->len, name)) return true;
    }
    return false;
}

/* The client always sets these itself; a caller entry for one is not sent. */
static bool header_is_client_managed(GrayString name) {
    return http_ascii_equal_ignore_case(name.data, name.len, "host") ||
           http_ascii_equal_ignore_case(name.data, name.len, "content-length") ||
           http_ascii_equal_ignore_case(name.data, name.len, "connection");
}

/* A header name is an RFC 9110 token; a value carries no CR, LF or NUL. Either
 * violation would let the caller's text start a new header line. */
static bool header_is_valid(GrayString name, GrayString value) {
    if (name.len == 0) return false;
    for (int32_t i = 0; i < name.len; i++) {
        unsigned char character = (unsigned char)name.data[i];
        if (!isalnum(character) && !strchr("!#$%&'*+-.^_`|~", character)) return false;
    }
    for (int32_t i = 0; i < value.len; i++) {
        if (value.data[i] == '\r' || value.data[i] == '\n' || value.data[i] == '\0') return false;
    }
    return true;
}

/* A failed request: status 0, the fixed `detail` as the body, and the error
 * code the result variants report. */
static GrayHttpResponse request_failure(GrayArena *arena, GrayHttpResponse failure, int64_t *error_code,
                                        int64_t code, const char *detail) {
    failure.body = gray_string_new(arena, detail, (int32_t)strlen(detail));
    if (error_code) *error_code = code;
    return failure;
}

/* Core HTTP request function */
static GrayHttpResponse do_request(GrayArena *arena, const char *method,
                                  GrayString url, GrayString body, GrayMap *custom_headers, int64_t *error_code) {
    GrayHttpResponse error_response;
    error_response.status = 0;
    error_response.body = (GrayString){"", 0};
    error_response.headers = gray_map_new_kind(arena, sizeof(GrayString), sizeof(GrayString), 4, GRAY_ELEM_STRING, GRAY_ELEM_STRING);

    /* The URL is handled as a C string below, so a NUL would cut it short and
     * send the request to a different host than the full URL names. */
    if (url.len > 0 && memchr(url.data, '\0', (size_t)url.len))
        return request_failure(arena, error_response, error_code, GRAY_ERR_InvalidInput,
            "invalid URL: contains a space or control character");

    char url_buffer[GRAY_HTTP_URL_BUFFER_SIZE];
    gray_cstr(url, url_buffer, sizeof(url_buffer));

    if (starts_with_ignore_case(url_buffer, "https://")) {
        return request_failure(arena, error_response, error_code, GRAY_ERR_Unsupported,
            "https:// is not supported; use http://");
    }

    /* A space or control byte would break the request line or inject headers. */
    for (const char *scan_cursor = url_buffer; *scan_cursor; scan_cursor++) {
        unsigned char byte = (unsigned char)*scan_cursor;
        if (byte <= ' ' || byte == 0x7f)
            return request_failure(arena, error_response, error_code, GRAY_ERR_InvalidInput,
                "invalid URL: contains a space or control character");
    }

    char host[GRAY_HTTP_HOST_BUFFER_SIZE], path[GRAY_HTTP_PATH_BUFFER_SIZE];
    int port;
    int64_t parse_failure_code;
    const char *parse_failure = parse_url(url_buffer, host, sizeof(host), &port, path, sizeof(path),
                                          &parse_failure_code);
    if (parse_failure) {
        return request_failure(arena, error_response, error_code, parse_failure_code, parse_failure);
    }

    if (custom_headers) {
        for (int32_t i = 0; i < custom_headers->order_len; i++) {
            int32_t slot = custom_headers->order[i];
            if (slot < 0) continue;
            GrayString *header_key = (GrayString *)gray_map_key_at(custom_headers, slot);
            GrayString *header_value = (GrayString *)gray_map_value_at(custom_headers, slot);
            if (!header_key || !header_value) continue;
            if (!header_is_valid(*header_key, *header_value)) {
                return request_failure(arena, error_response, error_code, GRAY_ERR_InvalidInput,
                    "invalid header: name must be a token and value must not contain CR, LF or NUL");
            }
        }
    }

    /* Connect */
    GrayString host_text = gray_string_new(arena, host, (int32_t)strlen(host));
    GraySocket sock = gray_net_dial(arena, host_text, port);
    if (sock.file_descriptor < 0) {
        return request_failure(arena, error_response, error_code, gray_errno_code(errno), "connection failed");
    }

    /* Set 10s timeout */
    gray_net_set_timeout(sock, GRAY_HTTP_TIMEOUT_MILLISECONDS);

    /* The Host header carries the port unless it is the default one. */
    char host_header[GRAY_HTTP_HOST_BUFFER_SIZE + 8];
    if (port == GRAY_HTTP_DEFAULT_PORT) snprintf(host_header, sizeof(host_header), "%s", host);
    else snprintf(host_header, sizeof(host_header), "%s:%d", host, port);

    /* Build headers — body is sent separately to avoid truncation */
    char header[GRAY_HTTP_HEADER_BUFFER_SIZE];
    int header_length;

    if (body.data && body.len > 0) {
        /* The form default applies only when the caller names no Content-Type. */
        const char *default_content_type = headers_contain(custom_headers, "content-type")
            ? "" : "Content-Type: application/x-www-form-urlencoded\r\n";
        header_length = snprintf(header, sizeof(header),
            "%s %s HTTP/1.1\r\n"
            "Host: %s\r\n"
            "Content-Length: %d\r\n"
            "%s"
            "Connection: close\r\n",
            method, path, host_header, (int)body.len, default_content_type);
    } else {
        header_length = snprintf(header, sizeof(header),
            "%s %s HTTP/1.1\r\n"
            "Host: %s\r\n"
            "Connection: close\r\n",
            method, path, host_header);
    }

    /* Append custom headers */
    if (custom_headers && custom_headers->count > 0) {
        for (int32_t i = 0; i < custom_headers->order_len; i++) {
            int32_t slot = custom_headers->order[i];
            if (slot < 0) continue;
            GrayString *header_key = (GrayString *)gray_map_key_at(custom_headers, slot);
            GrayString *header_value = (GrayString *)gray_map_value_at(custom_headers, slot);
            if (!header_key || !header_value) continue;
            if (header_is_client_managed(*header_key)) continue;
            int remaining = (int)(sizeof(header)) - header_length;
            if (remaining <= 0) {
                gray_net_close(sock);
                return request_failure(arena, error_response, error_code, GRAY_ERR_OutOfRange,
                    "request headers too large");
            }
            int bytes_received = snprintf(header + header_length, (size_t)remaining,
                "%.*s: %.*s\r\n", (int)header_key->len, header_key->data, (int)header_value->len, header_value->data);
            if (bytes_received < 0 || bytes_received >= remaining) {
                gray_net_close(sock);
                return request_failure(arena, error_response, error_code, GRAY_ERR_OutOfRange,
                    "request headers too large");
            }
            header_length += bytes_received;
        }
    }

    /* Terminate headers */
    if ((size_t)header_length + 2 < sizeof(header)) {
        header[header_length++] = '\r';
        header[header_length++] = '\n';
    }

    if (header_length <= 0 || (size_t)header_length >= sizeof(header)) {
        gray_net_close(sock);
        return request_failure(arena, error_response, error_code, GRAY_ERR_OutOfRange,
            "request URL or path too long");
    }

    GrayString header_text = {header, (int32_t)header_length};
    bool sent = gray_net_send(sock, header_text) >= 0;
    if (sent && body.data && body.len > 0) {
        sent = gray_net_send(sock, body) >= 0;
    }
    if (!sent) {
        int64_t code = gray_errno_code(errno);
        gray_net_close(sock);
        return request_failure(arena, error_response, error_code, code, "request send failed");
    }

    /* Receive response (up to 1MB) — heap-allocated to avoid stack overflow */
    char *response_buffer = malloc(GRAY_HTTP_RESPONSE_BUFFER_SIZE + 1);
    if (!response_buffer) {
        gray_net_close(sock);
        return request_failure(arena, error_response, error_code, GRAY_ERR_Unavailable, "out of memory");
    }
    int total = 0;
    int receive_error = 0;
    while (total < GRAY_HTTP_RESPONSE_BUFFER_SIZE) {
        GrayString chunk = gray_net_recv_status(arena, sock, GRAY_HTTP_RESPONSE_BUFFER_SIZE - total, &receive_error);
        if (chunk.len <= 0) break;
        memcpy(response_buffer + total, chunk.data, (size_t)chunk.len);
        total += chunk.len;
    }
    bool over_limit = false;
    if (total == GRAY_HTTP_RESPONSE_BUFFER_SIZE) {
        /* The buffer is full: any further byte means the response is larger. */
        over_limit = gray_net_recv(arena, sock, 1).len > 0;
    }
    response_buffer[total] = '\0';

    gray_net_close(sock);

    if (receive_error != 0) {
        free(response_buffer);
        /* The socket is blocking, so this is the 10 s timeout elapsing. */
        if (receive_error == EAGAIN || receive_error == EWOULDBLOCK || receive_error == ETIMEDOUT)
            return request_failure(arena, error_response, error_code, GRAY_ERR_Timeout, "request timed out");
        return request_failure(arena, error_response, error_code, gray_errno_code(receive_error), "receive failed");
    }

    if (over_limit) {
        free(response_buffer);
        return request_failure(arena, error_response, error_code, GRAY_ERR_OutOfRange,
            "response exceeds the 1 MB limit");
    }

    bool malformed;
    GrayHttpResponse result = parse_response(arena, response_buffer, total, &malformed);
    free(response_buffer);
    if (result.status == 0) {
        return request_failure(arena, error_response, error_code, GRAY_ERR_ParseFailure,
            "response is not an HTTP response");
    }
    if (malformed) {
        return request_failure(arena, error_response, error_code, GRAY_ERR_ParseFailure,
            "malformed chunked response body");
    }
    return result;
}

GrayHttpResponse gray_http_get(GrayArena *arena, GrayString url, GrayMap *headers) {
    return do_request(arena, "GET", url, (GrayString){"", 0}, headers, NULL);
}

GrayHttpResponse gray_http_post(GrayArena *arena, GrayString url, GrayString body, GrayMap *headers) {
    return do_request(arena, "POST", url, body, headers, NULL);
}

GrayHttpResponse gray_http_put(GrayArena *arena, GrayString url, GrayString body, GrayMap *headers) {
    return do_request(arena, "PUT", url, body, headers, NULL);
}

GrayHttpResponse gray_http_delete(GrayArena *arena, GrayString url, GrayMap *headers) {
    return do_request(arena, "DELETE", url, (GrayString){"", 0}, headers, NULL);
}

GrayHttpResponse gray_http_head(GrayArena *arena, GrayString url, GrayMap *headers) {
    return do_request(arena, "HEAD", url, (GrayString){"", 0}, headers, NULL);
}

GrayHttpResponse gray_http_patch(GrayArena *arena, GrayString url, GrayString body, GrayMap *headers) {
    return do_request(arena, "PATCH", url, body, headers, NULL);
}

/* _result variants — status==0 indicates connection/request failure */

static GrayResult_http http_result(GrayArena *arena, GrayHttpResponse resp, int64_t error_code) {
    GrayResult_http result;
    result.v0 = resp;
    if (resp.status == 0) {
        /* The body of a failed request is its fixed detail text. */
        result.v1 = gray_error_new(arena, error_code, resp.body);
    } else {
        result.v1 = NULL;
    }
    return result;
}

GrayResult_http gray_http_get_result(GrayArena *arena, GrayString url, GrayMap *headers) {
    int64_t error_code = GRAY_ERR_Unknown;
    GrayHttpResponse resp = do_request(arena, "GET", url, (GrayString){"", 0}, headers, &error_code);
    return http_result(arena, resp, error_code);
}

GrayResult_http gray_http_post_result(GrayArena *arena, GrayString url, GrayString body, GrayMap *headers) {
    int64_t error_code = GRAY_ERR_Unknown;
    GrayHttpResponse resp = do_request(arena, "POST", url, body, headers, &error_code);
    return http_result(arena, resp, error_code);
}

GrayResult_http gray_http_put_result(GrayArena *arena, GrayString url, GrayString body, GrayMap *headers) {
    int64_t error_code = GRAY_ERR_Unknown;
    GrayHttpResponse resp = do_request(arena, "PUT", url, body, headers, &error_code);
    return http_result(arena, resp, error_code);
}

GrayResult_http gray_http_delete_result(GrayArena *arena, GrayString url, GrayMap *headers) {
    int64_t error_code = GRAY_ERR_Unknown;
    GrayHttpResponse resp = do_request(arena, "DELETE", url, (GrayString){"", 0}, headers, &error_code);
    return http_result(arena, resp, error_code);
}

GrayResult_http gray_http_head_result(GrayArena *arena, GrayString url, GrayMap *headers) {
    int64_t error_code = GRAY_ERR_Unknown;
    GrayHttpResponse resp = do_request(arena, "HEAD", url, (GrayString){"", 0}, headers, &error_code);
    return http_result(arena, resp, error_code);
}

GrayResult_http gray_http_patch_result(GrayArena *arena, GrayString url, GrayString body, GrayMap *headers) {
    int64_t error_code = GRAY_ERR_Unknown;
    GrayHttpResponse resp = do_request(arena, "PATCH", url, body, headers, &error_code);
    return http_result(arena, resp, error_code);
}
