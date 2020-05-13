
#include <netinet/in.h>
#include <sys/socket.h>

#include "file_io.h"

#define SOCKETS_HARNESS_NO_RENAME

#include "sockets_harness.h"

struct _fuzzying_harness_socket_t {
    int used;
    int domain;
    int type;
    int bind_called;
    int listen_called;
    int connect_called;
    int accept_called;
    int accepted_socket;
    int close_called;
    int unrecoverable_error_signaled;
    int sendto_called;
    int recvfrom_called;
    int flags;
    int read_eof_reached;
};

enum { MAX_SOCKETS = 5 };

static struct _fuzzying_harness_socket_t socket_table[MAX_SOCKETS] = {0};

static uint32_t used_sockets = 0;

static int unbuffered_getchar() {
    // this is called a lot, so let us make sure often that errno stays at zero.
    assert(errno == 0);

    static int count = 0;

    if (count++ > 2048) {
        return 255;
    }

    char c;

    const ssize_t r = read(STDIN_FILENO, &c, 1);

    return r == 1 ? (unsigned char)c : 255;
}

int _fuzzying_harness_socket(int domain, int type, int protocol) {
    assert(domain == AF_INET || domain == AF_INET6);
    assert(type == SOCK_STREAM || type == SOCK_DGRAM);
    assert(type != SOCK_STREAM || protocol == 0 || protocol == IPPROTO_TCP);
    assert(type != SOCK_DGRAM || protocol == 0 || protocol == IPPROTO_UDP);

    const int r = unbuffered_getchar();

    if (r) {
        errno = r;
        return -1;
    }

    if (used_sockets == MAX_SOCKETS) {
        // Why do you need so many sockets!?
        abort();
    }

    used_sockets++;

    int fd = -1;

    for (int i = 0; i < MAX_SOCKETS; i++) {
        if (!socket_table[i].used) {
            fd = i;
        }
    }

    assert(fd >= 0);

    socket_table[fd] = (struct _fuzzying_harness_socket_t){0};
    socket_table[fd].used = 1;
    socket_table[fd].type = type;
    socket_table[fd].domain = domain;
    socket_table[fd].flags = (int)(0xDEADBEEF & ~(unsigned int)O_NONBLOCK);

    return fd;
}

static void _assert_valid_socket_fd(int fd) {
    assert(fd >= 0 && fd < MAX_SOCKETS);
    assert(socket_table[fd].used);
}

static void _assert_valid_const_sockaddr(int domain, const struct sockaddr *addr, socklen_t len) {
    assert(addr != NULL);

    if (domain == AF_INET) {
        assert(len == sizeof(struct sockaddr_in) || len == sizeof(struct sockaddr_storage));
    } else {
        assert(len == sizeof(struct sockaddr_in6) || len == sizeof(struct sockaddr_storage));
    }

    // Let's try to trigger some segmentation fault
    struct sockaddr_storage test = {0};
    memcpy(&test, addr, len);

    char *start = (char *)&test;
    char *end = start + sizeof(struct sockaddr_storage);
    int non_zero = 0;

    while (start < end) {
        if (*start != 0) {
            non_zero = 1;
        }

        start++;
    }

    assert(non_zero);
}

static void _assert_valid_sockaddr(int domain, struct sockaddr *addr, socklen_t *addr_len) {
    assert(addr != NULL);

    if (domain == AF_INET) {
        assert(*addr_len == sizeof(struct sockaddr_in) || *addr_len == sizeof(struct sockaddr_storage));
    } else {
        assert(*addr_len == sizeof(struct sockaddr_in6) || *addr_len == sizeof(struct sockaddr_storage));
    }

    // Let's try to trigger some segmentation fault
    memset(addr, 0, *addr_len);
}

int _fuzzying_harness_bind(int fd, const struct sockaddr *addr, socklen_t len) {
    _assert_valid_socket_fd(fd);
    _assert_valid_const_sockaddr(socket_table[fd].domain, addr, len);

    assert(!socket_table[fd].bind_called);
    assert(!socket_table[fd].close_called);
    assert(!socket_table[fd].connect_called);
    assert(!socket_table[fd].listen_called);
    assert(!socket_table[fd].accept_called);
    assert(!socket_table[fd].accepted_socket);
    assert(!socket_table[fd].unrecoverable_error_signaled);

    socket_table[fd].bind_called = 1;

    const int r = unbuffered_getchar();

    if (r) {
        errno = r;
        socket_table[fd].unrecoverable_error_signaled = 1;
        return -1;
    }

    return 0;
}

int _fuzzying_harness_connect(int fd, const struct sockaddr *addr, socklen_t len) {
    _assert_valid_socket_fd(fd);
    _assert_valid_const_sockaddr(socket_table[fd].domain, addr, len);

    assert(socket_table[fd].type == SOCK_STREAM);
    assert(!socket_table[fd].bind_called); // This is not technically an error, but very suspicious
    assert(!socket_table[fd].close_called);
    assert(!socket_table[fd].connect_called);
    assert(!socket_table[fd].listen_called);
    assert(!socket_table[fd].accept_called);
    assert(!socket_table[fd].accepted_socket);
    assert(!socket_table[fd].unrecoverable_error_signaled);

    socket_table[fd].connect_called = 1;

    const int r = unbuffered_getchar();

    if (r) {
        errno = r;
        socket_table[fd].unrecoverable_error_signaled = 1;
        return -1;
    }

    return 0;
}

int _fuzzying_harness_listen(int fd, int n) {
    (void)n;

    _assert_valid_socket_fd(fd);

    assert(socket_table[fd].type == SOCK_STREAM);
    assert(socket_table[fd].bind_called);
    assert(!socket_table[fd].connect_called);
    assert(!socket_table[fd].listen_called);
    assert(!socket_table[fd].accept_called);
    assert(!socket_table[fd].close_called);
    assert(!socket_table[fd].accepted_socket);
    assert(!socket_table[fd].unrecoverable_error_signaled);

    socket_table[fd].listen_called = 1;

    const int r = unbuffered_getchar();

    if (r) {
        errno = r;
        socket_table[fd].unrecoverable_error_signaled = 1;
        return -1;
    }

    return 0;
}

int _fuzzying_harness_accept(int fd, struct sockaddr *addr, socklen_t *addr_len) {
    _assert_valid_socket_fd(fd);
    _assert_valid_sockaddr(socket_table[fd].domain, addr, addr_len);

    assert(socket_table[fd].type == SOCK_STREAM);
    assert(socket_table[fd].bind_called);
    assert(!socket_table[fd].connect_called);
    assert(socket_table[fd].listen_called);
    //assert(! socket_table[fd].accept_called);
    assert(!socket_table[fd].close_called);
    assert(!socket_table[fd].accepted_socket);
    assert(!socket_table[fd].unrecoverable_error_signaled);

    socket_table[fd].accept_called = 1;

    const int r = unbuffered_getchar();

    if (r) {
        errno = r;
        if (errno != ECONNABORTED) {
            socket_table[fd].unrecoverable_error_signaled = 1;
        }
        return -1;
    }

    int new_socket = _fuzzying_harness_socket(socket_table[fd].domain, socket_table[fd].type, 0);

    if (new_socket < 0) {
        return new_socket;
    }

    socket_table[new_socket].accepted_socket = 1;

    return new_socket;
}

int _fuzzying_harness_poll(struct pollfd *fds, nfds_t nfds, int timeout) {
    assert(fds != NULL);
    assert(nfds <= used_sockets);

    const int r = unbuffered_getchar();

    for (nfds_t i = 0; i < nfds; i++) {
        _assert_valid_socket_fd(fds[i].fd);
        assert(fds[i].events == 0 || (fds[i].events & ~(POLLIN | POLLOUT)) == 0); // This is not technically an error, but very suspicious

        if (r) {
            socket_table[fds[i].fd].unrecoverable_error_signaled = 1;
        }
    }

    if (r) {
        errno = r;
        return -1;
    }

    assert(timeout != 0); // This is not technically an error, but very suspicious

    int count = 0;

    for (nfds_t i = 0; i < nfds; i++) {
        if (socket_table[fds[i].fd].accept_called && used_sockets == MAX_SOCKETS) {
            // no new connections until something is closed
            continue;
        }

        int c = unbuffered_getchar();

        // Only return things we asked for and ERR and HUP
        fds[i].revents = c & (fds[i].events | POLLERR | POLLHUP);

        if (fds[i].revents != 0) {
            count++;
        }
    }

    return count;
}

ssize_t _fuzzying_harness_recvfrom(int fd, void *buf, size_t n, int flags, struct sockaddr *addr, socklen_t *addr_len) {
    _assert_valid_socket_fd(fd);
    assert(buf != NULL);
    assert(n > 0);          // This is not technically an error, but very suspicious
    assert(n < UINT32_MAX); // Underflow somewhere
    assert(socket_table[fd].type == SOCK_STREAM || (flags | MSG_WAITALL) == 0);
    assert(flags == MSG_WAITALL || flags == 0);
    _assert_valid_sockaddr(socket_table[fd].domain, addr, addr_len);

    assert(!socket_table[fd].close_called);
    assert(!socket_table[fd].listen_called);
    assert(!socket_table[fd].accept_called);
    assert(!socket_table[fd].unrecoverable_error_signaled);
    assert(!socket_table[fd].read_eof_reached);

    if (socket_table[fd].type == SOCK_STREAM) {
        assert(!socket_table[fd].bind_called);
        assert(socket_table[fd].connect_called || socket_table[fd].accepted_socket);
    } else {
        assert(socket_table[fd].bind_called);
    }

    socket_table[fd].recvfrom_called = 1;

    // Let's try to trigger some segmentation fault
    memset(buf, -1, n);

    const int r = unbuffered_getchar();

    if (r) {
        errno = r;
        socket_table[fd].unrecoverable_error_signaled = 1;
        return -1;
    }

    size_t to_read = n;

    const int r2 = unbuffered_getchar();

    if (r2 == 0) {
        to_read = 0;
    } else if (r2 < 255) {
        to_read = (size_t)r2;
    } else {
        /* nothing */
    }

    char *char_buf = buf;

    for (size_t i = 0; i < to_read; i++) {
        char_buf[i] = (char)unbuffered_getchar(); // shamelessly casting EOF to something else.
    }

    if (to_read == 0) {
        socket_table[fd].read_eof_reached = 1;
    }

    return (ssize_t)to_read;
}

ssize_t _fuzzying_harness_recv(int fd, void *buf, size_t n, int flags) {
    struct sockaddr_storage addr;
    socklen_t addr_len = sizeof(addr);
    return _fuzzying_harness_recvfrom(fd, buf, n, flags, (struct sockaddr *)&addr, &addr_len);
}

ssize_t _fuzzying_harness_sendto(int fd, const void *buf, size_t n, int flags, const struct sockaddr *addr, socklen_t addr_len) {
    _assert_valid_socket_fd(fd);
    assert(buf != NULL);
    assert(n > 0);          // This is not technically an error, but very suspicious
    assert(n < UINT32_MAX); // Underflow somewhere
    assert(flags == 0);
    _assert_valid_const_sockaddr(socket_table[fd].domain, addr, addr_len);

    assert(!socket_table[fd].close_called);
    assert(!socket_table[fd].listen_called);
    assert(!socket_table[fd].accept_called);
    assert(!socket_table[fd].unrecoverable_error_signaled);

    if (socket_table[fd].type == SOCK_STREAM) {
        assert(!socket_table[fd].bind_called);
        assert(socket_table[fd].connect_called || socket_table[fd].accepted_socket);
    } else {
        assert(socket_table[fd].bind_called);
    }

    socket_table[fd].sendto_called = 1;

    const int r = unbuffered_getchar();

    if (r) {
        errno = r;
        socket_table[fd].unrecoverable_error_signaled = 1;
        return -1;
    }

    return (ssize_t)n;
}

ssize_t _fuzzying_harness_send(int fd, const void *buf, size_t n, int flags) {
    struct sockaddr_storage addr;
    return _fuzzying_harness_sendto(fd, buf, n, flags, (const struct sockaddr *)&addr, sizeof(addr));
}

// EIO
int _fuzzying_harness_close(int fd) {
    _assert_valid_socket_fd(fd);

    assert(!socket_table[fd].close_called);

    if (!socket_table[fd].unrecoverable_error_signaled &&
        (socket_table[fd].connect_called || socket_table[fd].accepted_socket)) {

        assert(socket_table[fd].sendto_called || socket_table[fd].recvfrom_called); // Very suspicious...
    }

    if (!socket_table[fd].unrecoverable_error_signaled && socket_table[fd].type == SOCK_DGRAM) {
        assert(socket_table[fd].sendto_called || socket_table[fd].recvfrom_called); // Very suspicious...
    }

    const int r = unbuffered_getchar();

    if (r) {
        errno = r;
        socket_table[fd].unrecoverable_error_signaled = 1;
        return -1;
    }

    socket_table[fd].used = 0;
    used_sockets--;

    return 0;
}

int _fuzzying_harness_fcntl(int fd, int cmd, ...) {
    _assert_valid_socket_fd(fd);

    const int r = unbuffered_getchar();

    if (r) {
        errno = r;
        socket_table[fd].unrecoverable_error_signaled = 1;
        return -1;
    }

    if (cmd == F_GETFL) {
        return socket_table[fd].flags; // returns flags associated with socket
    } else if (cmd == F_SETFL) {
        va_list ap;
        va_start(ap, cmd);
        int flags = va_arg(ap, int);

        assert(((socket_table[fd].flags | O_NONBLOCK) == flags) || (socket_table[fd].flags == (flags | O_NONBLOCK)) || socket_table[fd].flags == flags);

        socket_table[fd].flags = flags;

        va_end(ap);

        return 0;
    } else {
        abort();
    }
}

void *_fuzzying_harness_malloc(size_t size) {
    const int r = unbuffered_getchar();

    if (r) {
        errno = r;
        return NULL;
    }

    return malloc(size);
}

void *_fuzzying_harness_calloc(size_t nmemb, size_t size) {
    const int r = unbuffered_getchar();

    if (r) {
        errno = r;
        return NULL;
    }

    return calloc(nmemb, size);
}

void *_fuzzying_harness_strdup(const char *s) {
    const int r = unbuffered_getchar();

    if (r) {
        errno = r;
        return NULL;
    }

    return strdup(s);
}

int _fuzzying_harness_store_block(struct torrent_t *const torrent, const uint64_t block_number,
                                  const struct block_t *const block) {
    assert(torrent != NULL);
    assert(torrent->downloaded_file_stream != NULL);

    assert(block_number < torrent->block_count);

    assert(block != NULL);
    assert(block->size > 0);

    const int r = unbuffered_getchar();

    if (r) {
        errno = r;
        return -1;
    }

    return 0;
}
