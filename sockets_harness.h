/**

This file should be included in your ttorrent.c as follows.

#ifdef ENABLE_FUZZING
#include "sockets_harness.h"
#endif

It must be the last header included.

This fuzzing harness mocks the API functions used by trivial torrent server
and produces results based on what is read from the STDIN. To be used
with American Fuzzy Lop.

*/

// Eventually we might want to do this with LD_PRELOAD

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#ifndef _SYS_SOCKET_H
#error "This header must be included last!"
#endif

int _fuzzying_harness_socket(int domain, int type, int protocol);

int _fuzzying_harness_bind(int fd, const struct sockaddr *addr, socklen_t len);

int _fuzzying_harness_connect(int fd, const struct sockaddr *addr, socklen_t len);

int _fuzzying_harness_listen(int fd, int n);

int _fuzzying_harness_accept(int fd, struct sockaddr *addr, socklen_t *addr_len);

int _fuzzying_harness_poll(struct pollfd *fds, nfds_t nfds, int timeout);

ssize_t _fuzzying_harness_recvfrom(int fd, void *buf, size_t n, int flags, struct sockaddr *addr, socklen_t *addr_len);

ssize_t _fuzzying_harness_recv(int fd, void *buf, size_t n, int flags);

ssize_t _fuzzying_harness_sendto(int fd, const void *buf, size_t n, int flags, const struct sockaddr *addr, socklen_t addr_len);

ssize_t _fuzzying_harness_send(int fd, const void *buf, size_t n, int flags);

int _fuzzying_harness_close(int fd);

int _fuzzying_harness_fcntl(int fd, int cmd, ...);

void *_fuzzying_harness_malloc(size_t size);

void *_fuzzying_harness_calloc(size_t nmemb, size_t size);

void *_fuzzying_harness_strdup(const char *s);

int _fuzzying_harness_store_block(struct torrent_t *const torrent, const uint64_t block_number,
                                  const struct block_t *const block);

#ifndef SOCKETS_HARNESS_NO_RENAME

#define socket _fuzzying_harness_socket
#define bind _fuzzying_harness_bind
#define connect _fuzzying_harness_connect
#define listen _fuzzying_harness_listen
#define accept _fuzzying_harness_accept
#define poll _fuzzying_harness_poll
#define recvfrom _fuzzying_harness_recvfrom
#define recv _fuzzying_harness_recv
#define sendto _fuzzying_harness_sendto
#define send _fuzzying_harness_send
#define close _fuzzying_harness_close
#define fcntl _fuzzying_harness_fcntl
#define malloc _fuzzying_harness_malloc
#define calloc _fuzzying_harness_calloc
#define strdup _fuzzying_harness_strdup

#define store_block _fuzzying_harness_store_block

#endif
