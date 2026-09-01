/*
 * server.h — Public interface for the server stdlib module.
 * Declares HTTP server types, routing, path parameters, CORS
 * configuration, and handler function signatures.
 *
 * Author:  Marshall A Burns (@SchoolyB)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

#ifndef GRAY_SERVER_H
#define GRAY_SERVER_H

#include "../runtime/runtime.h"
#include "../runtime/map.h"

/*@man HttpRequest
 *@module server
 *@group Types
 *@kind type
 *@field method string
 *@field path string
 *@field body string
 *@field query map[string:string]
 *@field headers map[string:string]
 *@field params map[string:string]
 *@desc The request object passed to every handler function.
 *@example
 *   import @server
 *   do handler(req HttpRequest) -> HttpResponse {
 *       mut id = req.params["id"]
 *       return server.json(200, id)
 *   }
 *@end
 */
/* Request struct passed to handlers */
typedef struct {
    GrayString method;
    GrayString path;
    GrayString body;
    GrayMap query;       /* map[string:string] */
    GrayMap headers;     /* map[string:string] */
    GrayMap params;      /* map[string:string] — path params from :id patterns */
} GrayRequest;

/*@man HttpResponse
 *@module server
 *@group Types
 *@kind type
 *@field status int
 *@field body string
 *@field content_type string
 *@desc The response object returned by handler functions. Build using server.text(), server.json(), server.html(), or server.redirect() rather than constructing directly.
 *@example
 *   import @server
 *   do handler(req HttpRequest) -> HttpResponse {
 *       return server.text(200, "hello")
 *   }
 *@end
 */
/* Response struct returned by handlers */
typedef struct {
    int64_t status;
    GrayString body;
    GrayString content_type;
} GrayResponse;

/* Route entry */
typedef struct {
    const char *method;
    const char *pattern;
    GrayResponse (*handler)(GrayRequest);
} GrayRoute;

/* Middleware function pointer */
typedef void (*GrayMiddleware)(GrayRequest *req, GrayResponse *resp);

/*@man Router
 *@module server
 *@group Types
 *@kind type
 *@desc The router object returned by add_router(). Pass it to add_route(), cors(), use(), and listen(). Its internal fields are not user-accessible.
 *@example
 *   import @server
 *   mut r = server.add_router()
 *   server.add_route(r, "GET", "/", ()home)
 *   server.listen(r, 8080)
 *@end
 */
/* Router — holds registered routes */
typedef struct {
    GrayRoute *routes;
    int count;
    int capacity;
    const char *cors_origin;    /* NULL = no CORS */
    GrayMiddleware *middlewares;
    int mw_count;
    int mw_capacity;
} GrayRouter;

/*@man add_router
 *@module server
 *@group Routing
 *@sig add_router() -> Router
 *@desc Creates and returns a new router. Pass the router to add_route(), cors(), use(), and listen().
 *@example
 *   import @server
 *   mut r = server.add_router()
 *@end
 */
/* Create a new router */
GrayRouter gray_server_router(void);

/*@man add_route
 *@module server
 *@group Routing
 *@sig add_route(router Router, method string, path string, handler func(HttpRequest) -> HttpResponse)
 *@desc Registers a route on the router. method is an HTTP verb ("GET", "POST", etc.). path may contain :param segments for dynamic matching. handler is a function that takes HttpRequest and returns HttpResponse.
 *@example
 *   import @server
 *   do home(req HttpRequest) -> HttpResponse {
 *       return server.text(200, "hello")
 *   }
 *   mut r = server.add_router()
 *   server.add_route(r, "GET", "/", ()home)
 *@end
 */
/* Register a route: server.route(router, method, pattern, handler) */
void gray_server_route(GrayRouter *r, GrayString method, GrayString pattern,
                     GrayResponse (*handler)(GrayRequest));

/*@man listen
 *@module server
 *@group Routing
 *@sig listen(router Router, port int)
 *@desc Starts the HTTP server on the given port. Blocks until the process is killed.
 *@example
 *   import @server
 *   mut r = server.add_router()
 *   server.add_route(r, "GET", "/", ()home)
 *   server.listen(r, 8080)
 *@end
 */
/* Start listening — blocks until killed */
void gray_server_listen(int64_t port, GrayRouter *r);
void gray_server_listen_host(int64_t port, GrayString host, GrayRouter *r);

/*@man cors
 *@module server
 *@group Routing
 *@sig cors(router Router, origin string)
 *@desc Enables CORS on the router for the given origin. Use "*" to allow all origins.
 *@example
 *   import @server
 *   mut r = server.add_router()
 *   server.cors(r, "*")
 *@end
 */
/* Enable CORS with the given origin (e.g. "*" or "http://example.com") */
void gray_server_cors(GrayRouter *r, GrayString origin);

/*@man use
 *@module server
 *@group Routing
 *@sig use(router Router, middleware func(^HttpRequest, ^HttpResponse))
 *@desc Registers a middleware function on the router. Middleware runs before each handler and receives pointers to the request and response so it can inspect or modify either.
 *@example
 *   import @server
 *   mut r = server.add_router()
 *   server.use(r, ()my_logger)
 *@end
 */
/* Register a middleware function */
void gray_server_use(GrayRouter *r, GrayMiddleware fn);

/*@man text
 *@module server
 *@group Response Builders
 *@sig text(status int, body string) -> HttpResponse
 *@desc Returns an HttpResponse with Content-Type: text/plain and the given status code and body.
 *@example
 *   import @server
 *   do handler(req HttpRequest) -> HttpResponse {
 *       return server.text(200, "hello")
 *   }
 *@end
 */
/* Response builders */
GrayResponse gray_server_text(int64_t status, GrayString body);

/*@man json
 *@module server
 *@group Response Builders
 *@sig json(status int, body string) -> HttpResponse
 *@desc Returns an HttpResponse with Content-Type: application/json and the given status code and body.
 *@example
 *   import @server
 *   do handler(req HttpRequest) -> HttpResponse {
 *       return server.json(200, "{\"ok\": true}")
 *   }
 *@end
 */
GrayResponse gray_server_json(int64_t status, GrayString body);

/*@man html
 *@module server
 *@group Response Builders
 *@sig html(status int, body string) -> HttpResponse
 *@desc Returns an HttpResponse with Content-Type: text/html and the given status code and body.
 *@example
 *   import @server
 *   do handler(req HttpRequest) -> HttpResponse {
 *       return server.html(200, "<h1>Hello</h1>")
 *   }
 *@end
 */
GrayResponse gray_server_html(int64_t status, GrayString body);

/*@man redirect
 *@module server
 *@group Response Builders
 *@sig redirect(status int, url string) -> HttpResponse
 *@desc Returns an HttpResponse that redirects to url. Use 301 for permanent or 302 for temporary redirects.
 *@example
 *   import @server
 *   do handler(req HttpRequest) -> HttpResponse {
 *       return server.redirect(302, "/new-path")
 *   }
 *@end
 */
GrayResponse gray_server_redirect(int64_t status, GrayString location);

#endif
