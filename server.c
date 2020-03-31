#include "server.h"
#include "enum.h"
#include "file_io.h"
#include "logger.h"
#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/fcntl.h>
#include <sys/unistd.h>

/*
1. Load a metainfo file (functionality is already available in the file_io API).
  a. Check for the existence of the associated downloaded file.
  b. Check which blocks are correct using the SHA256 hashes in the metainfo file.
2. Forever listen to incoming connections, and for each connection:
  a. Wait for a message.
  b. If a message requests a block that can be served (correct hash), respond with the appropriate message,
  followed by the raw block data.
  c. Otherwise, respond with a message signaling the unavailability of the block.
*/

char *server__original_file_name(const char *const ttorrent) {
    const char *const end = strstr(ttorrent, ".ttorrent");
    if (end == NULL) {
        log_message(LOG_DEBUG, "Invalid file extension");
        return NULL;
    }

    uint32_t size = (uint32_t)(labs(ttorrent - end));
    char *const buffer = (char *)malloc(sizeof(char) * (size));
    if (buffer == NULL) {
        log_printf(LOG_DEBUG, "Failed to allocate buffer");
        return NULL;
    }

    strncpy(buffer, ttorrent, size);

    return buffer;
}

int server__non_blocking(const int sockd) {
    (void)sockd;
    return 0;
}

int server__blocking(const int sockd) {
    struct sockaddr_storage their_addr;
    unsigned int size = sizeof(their_addr);
    int rcv = accept(sockd, (struct sockaddr *)&their_addr, &size);
    if (rcv < 0) {
        log_printf(LOG_DEBUG, "Error while accepting the connection: %s", strerror(errno));
        return -1;
    }
    log_message(LOG_DEBUG, "Recieved connection");
    return 0;
}

int server__init_socket(const uint16_t port, const int blocking) {

    struct sockaddr_in hint;
    memset(&hint, 0, sizeof(struct sockaddr_in));
    hint.sin_family = AF_INET;
    hint.sin_addr.s_addr = INADDR_ANY;
    hint.sin_port = htons(port);

    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) {
        log_printf(LOG_DEBUG, "Error while creating socket: %s", strerror(errno));
        return -1;
    }
    if (blocking == SERVER__NON_BLOCK)
        log_message(LOG_DEBUG, "Using non-blocking socket.");
    else if (blocking == SERVER__BLOCK)
        log_message(LOG_DEBUG, "Using blocking socket.");

    if (blocking == SERVER__NON_BLOCK && fcntl(s, O_NONBLOCK)) {
        log_printf(LOG_DEBUG, "Error while setting the socket to nonblocking: %s", strerror(errno));
        return -1;
    }

    if (bind(s, (struct sockaddr *)&hint, sizeof(hint))) {
        log_printf(LOG_DEBUG, "Error while binding: %s", strerror(errno));
        return -1;
    }
    return s;
}

#define SERVER__BACKLOG 10
int server_init(uint16_t const port, const char *const metainfo) {
    struct fio_torrent_t torrent;
    // get original filename
    char *filename = server__original_file_name(metainfo);
    if (!filename) {
        log_printf(LOG_DEBUG, "Error filename is %s", metainfo);
        return -1;
    }

    log_printf(LOG_DEBUG, "metainfo: %s, filename: %s", metainfo, filename);

    if (fio_create_torrent_from_metainfo_file(metainfo, &torrent, filename)) {
        log_printf(LOG_INFO, "Failed to load metainfo: %s", strerror(errno));
        return -1;
    }
    int s = server__init_socket(port, SERVER__BLOCK);
    if (s < 0) {
        log_printf(LOG_DEBUG, "Failed to init socket with port %s", port);
        return -1;
    }

    log_printf(LOG_DEBUG, "Listening on port %s\n", port);
    if (listen(s, SERVER__BACKLOG)) {
        log_printf(LOG_DEBUG, "Failed to listen: %s", strerror(errno));
        return -1;
    }

    if (fio_destroy_torrent(&torrent)) {
        log_printf(LOG_DEBUG, "Error while destroying the torrent struct: ", strerror(errno));
        return -1;
    }
    return 0;
}