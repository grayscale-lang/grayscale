/*
 * net.h — Public interface for the net stdlib module.
 * Declares low-level TCP/UDP networking primitives for connect,
 * listen, accept, send, and receive operations.
 *
 * Author:  Marshall A Burns (@SchoolyB)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

#ifndef GRAY_NET_H
#define GRAY_NET_H

#include "../runtime/runtime.h"
#include "io.h" /* GrayResult_string */

/* Opaque socket handle */
typedef struct {
    int fd;
} GraySocket;

/*@man connect
 *@module net
 *@group Client
 *@sig connect(host string, port int) -> (Socket, Error)
 *@desc Opens a TCP connection to host on port. Always use destructuring (`mut v, err = ...` or `mut v, _ = ...`) — single-variable assignment is a compile error.
 *@example
 *   import @net
 *   mut sock, err = net.connect("example.com", 80)
 *   if err != nil { println("connect failed") }
 *   net.close(sock)
 *@end
 */

/*@man listen
 *@module net
 *@group Server
 *@sig listen(port int) -> (Listener, Error)
 *@desc Opens a TCP listener on port. Pass a host string as an optional first argument to bind a specific interface, e.g. listen("127.0.0.1", 8080). Always use destructuring (`mut v, err = ...` or `mut v, _ = ...`) — single-variable assignment is a compile error.
 *@example
 *   import @net
 *   mut listener, err = net.listen(8080)
 *   if err != nil { println("listen failed") }
 *   net.close(listener)
 *@end
 */

/*@man accept
 *@module net
 *@group Server
 *@sig accept(listener Listener) -> (Socket, Error)
 *@desc Blocks until an incoming connection arrives on listener, then returns a socket for it. Always use destructuring (`mut v, err = ...` or `mut v, _ = ...`) — single-variable assignment is a compile error.
 *@example
 *   import @net
 *   mut listener, _ = net.listen(8080)
 *   mut conn, err = net.accept(listener)
 *   if err != nil { println("accept failed") }
 *@end
 */

/*@man send
 *@module net
 *@group Data
 *@sig send(sock Socket, data string) -> (int, Error)
 *@desc Sends data over sock and returns the number of bytes written. Always use destructuring (`mut v, err = ...` or `mut v, _ = ...`) — single-variable assignment is a compile error.
 *@example
 *   import @net
 *   mut sock, _ = net.connect("example.com", 80)
 *   mut n, err = net.send(sock, "GET / HTTP/1.0\r\n\r\n")
 *   if err != nil { println("send failed") }
 *@end
 */

/*@man receive
 *@module net
 *@group Data
 *@sig receive(sock Socket, max_bytes int) -> (string, Error)
 *@desc Reads up to max_bytes from sock and returns them as a string. Always use destructuring (`mut v, err = ...` or `mut v, _ = ...`) — single-variable assignment is a compile error.
 *@example
 *   import @net
 *   mut sock, _ = net.connect("example.com", 80)
 *   mut data, err = net.receive(sock, 1024)
 *   if err != nil { println("receive failed") }
 *@end
 */

/*@man close
 *@module net
 *@group Lifecycle
 *@sig close(sock Socket)
 *@desc Closes a socket or listener. No return value.
 *@example
 *   import @net
 *   mut listener, _ = net.listen(8080)
 *   net.close(listener)
 *@end
 */

/*@man set_timeout
 *@module net
 *@group Configuration
 *@sig set_timeout(sock Socket, ms int)
 *@desc Sets the send and receive timeout on sock in milliseconds. No return value.
 *@example
 *   import @net
 *   mut sock, _ = net.connect("example.com", 80)
 *   net.set_timeout(sock, 5000)
 *@end
 */

/*@man resolve
 *@module net
 *@group DNS
 *@sig resolve(hostname string) -> (string, Error)
 *@desc Resolves hostname to an IP address string. Always use destructuring (`mut v, err = ...` or `mut v, _ = ...`) — single-variable assignment is a compile error.
 *@example
 *   import @net
 *   mut ip, err = net.resolve("localhost")
 *   if err != nil { println("resolve failed") }
 *   println(ip)
 *@end
 */

/* TCP client */
GraySocket gray_net_dial(GrayArena *arena, GrayString host, int64_t port);
void gray_net_close(GraySocket sock);

/* Send/receive raw data */
int64_t gray_net_send(GraySocket sock, GrayString data);
GrayString gray_net_recv(GrayArena *arena, GraySocket sock, int64_t max_bytes);

/* TCP server */
GraySocket gray_net_listen(GrayArena *arena, int64_t port);
GraySocket gray_net_listen_host(GrayArena *arena, GrayString host, int64_t port);
GraySocket gray_net_accept(GrayArena *arena, GraySocket listener);

/* Socket options */
void gray_net_set_timeout(GraySocket sock, int64_t milliseconds);

/* DNS resolution */
GrayString gray_net_resolve(GrayArena *arena, GrayString hostname);

/* _result variants */
typedef struct { GraySocket v0; GrayError *v1; } GrayResult_socket;

GrayResult_socket gray_net_dial_result(GrayArena *arena, GrayString host, int64_t port);
GrayResult_socket gray_net_listen_result(GrayArena *arena, int64_t port);
GrayResult_socket gray_net_listen_host_result(GrayArena *arena, GrayString host, int64_t port);
GrayResult_socket gray_net_accept_result(GrayArena *arena, GraySocket listener);
GrayResult_int gray_net_send_result(GrayArena *arena, GraySocket sock, GrayString data);
GrayResult_string gray_net_recv_result(GrayArena *arena, GraySocket sock, int64_t max_bytes);
GrayResult_string gray_net_resolve_result(GrayArena *arena, GrayString hostname);

#endif
