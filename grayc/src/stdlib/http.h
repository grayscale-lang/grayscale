/*
 * http.h — Public interface for the http stdlib module.
 * HTTP/1.1 client using raw sockets with no libcurl dependency.
 *
 * Author:  Marshall A Burns (@SchoolyB)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

#ifndef GRAY_HTTP_H
#define GRAY_HTTP_H

#include "../runtime/runtime.h"
#include "../runtime/map.h"

/*@man HttpResponse
 *@module http
 *@group Types
 *@kind type
 *@field status int
 *@field body string
 *@field headers map[string:string]
 *@desc The response object returned by all http request functions. Also available when the server module is imported.
 *@example
 *   import @http
 *   mut resp, err = http.get("http://example.com")
 *   println(resp.status)
 *   println(resp.body)
 *@end
 */
/* HTTP response: status code, body, headers */
typedef struct {
    int64_t status;
    GrayString body;
    GrayMap headers;
} GrayHttpResponse;

/*@man get
 *@module http
 *@group Requests
 *@sig get(url string, headers map[string:string]) -> (HttpResponse, Error)
 *@desc Sends an HTTP GET request to url with optional custom headers. Always use destructuring (`mut resp, err = ...` or `mut resp, _ = ...`) — single-variable assignment is a compile error.
 *@example
 *   import @http
 *   mut headers map[string:string] = {"Authorization": "Bearer token"}
 *   mut resp, err = http.get("http://example.com", headers)
 *   println(resp.status)
 *   println(resp.body)
 *@end
 */

/*@man post
 *@module http
 *@group Requests
 *@sig post(url string, body string, headers map[string:string]) -> (HttpResponse, Error)
 *@desc Sends an HTTP POST request to url with the given body and optional custom headers. Always use destructuring (`mut resp, err = ...` or `mut resp, _ = ...`) — single-variable assignment is a compile error.
 *@example
 *   import @http
 *   mut headers map[string:string] = {"Content-Type": "application/json"}
 *   mut resp, err = http.post("http://example.com/api", "{\"key\": \"value\"}", headers)
 *   println(resp.status)
 *@end
 */

/*@man put
 *@module http
 *@group Requests
 *@sig put(url string, body string, headers map[string:string]) -> (HttpResponse, Error)
 *@desc Sends an HTTP PUT request to url with the given body and optional custom headers. Always use destructuring (`mut resp, err = ...` or `mut resp, _ = ...`) — single-variable assignment is a compile error.
 *@example
 *   import @http
 *   mut headers map[string:string] = {"Content-Type": "application/json"}
 *   mut resp, err = http.put("http://example.com/api/1", "{\"name\": \"Alice\"}", headers)
 *   println(resp.status)
 *@end
 */

/*@man patch
 *@module http
 *@group Requests
 *@sig patch(url string, body string, headers map[string:string]) -> (HttpResponse, Error)
 *@desc Sends an HTTP PATCH request to url with the given body and optional custom headers. Always use destructuring (`mut resp, err = ...` or `mut resp, _ = ...`) — single-variable assignment is a compile error.
 *@example
 *   import @http
 *   mut headers map[string:string] = {"Content-Type": "application/json"}
 *   mut resp, err = http.patch("http://example.com/api/1", "{\"age\": 30}", headers)
 *   println(resp.status)
 *@end
 */

/*@man delete
 *@module http
 *@group Requests
 *@sig delete(url string, headers map[string:string]) -> (HttpResponse, Error)
 *@desc Sends an HTTP DELETE request to url with optional custom headers. Always use destructuring (`mut resp, err = ...` or `mut resp, _ = ...`) — single-variable assignment is a compile error.
 *@example
 *   import @http
 *   mut headers map[string:string] = {"Authorization": "Bearer token"}
 *   mut resp, err = http.delete("http://example.com/api/1", headers)
 *   println(resp.status)
 *@end
 */

/*@man head
 *@module http
 *@group Requests
 *@sig head(url string, headers map[string:string]) -> (HttpResponse, Error)
 *@desc Sends an HTTP HEAD request to url with optional custom headers. Returns only headers with no body. Always use destructuring (`mut resp, err = ...` or `mut resp, _ = ...`) — single-variable assignment is a compile error.
 *@example
 *   import @http
 *   mut headers map[string:string] = {"Authorization": "Bearer token"}
 *   mut resp, err = http.head("http://example.com", headers)
 *   println(resp.status)
 *@end
 */

/* HTTP methods */
GrayHttpResponse gray_http_get(GrayArena *arena, GrayString url, GrayMap *headers);
GrayHttpResponse gray_http_post(GrayArena *arena, GrayString url, GrayString body, GrayMap *headers);
GrayHttpResponse gray_http_put(GrayArena *arena, GrayString url, GrayString body, GrayMap *headers);
GrayHttpResponse gray_http_delete(GrayArena *arena, GrayString url, GrayMap *headers);
GrayHttpResponse gray_http_head(GrayArena *arena, GrayString url, GrayMap *headers);
GrayHttpResponse gray_http_patch(GrayArena *arena, GrayString url, GrayString body, GrayMap *headers);

/* _result variants */
typedef struct { GrayHttpResponse v0; GrayError *v1; } GrayResult_http;

GrayResult_http gray_http_get_result(GrayArena *arena, GrayString url, GrayMap *headers);
GrayResult_http gray_http_post_result(GrayArena *arena, GrayString url, GrayString body, GrayMap *headers);
GrayResult_http gray_http_put_result(GrayArena *arena, GrayString url, GrayString body, GrayMap *headers);
GrayResult_http gray_http_delete_result(GrayArena *arena, GrayString url, GrayMap *headers);
GrayResult_http gray_http_head_result(GrayArena *arena, GrayString url, GrayMap *headers);
GrayResult_http gray_http_patch_result(GrayArena *arena, GrayString url, GrayString body, GrayMap *headers);

#endif
