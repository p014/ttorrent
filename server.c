#include "server.h"
#include "file_io.h"
#include "logger.h"
#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
char *server__original_file_name(char *ttorrent) {
    char *const end = strstr(ttorrent, ".ttorrent");
    if (end == NULL) {
        log_message(LOG_DEBUG, "Invalid file extension");
        return NULL;
    }
    *end = '\0';

    char *const buffer = (char *)malloc(sizeof(char) * strlen(ttorrent));
    if (buffer == NULL) {
        log_printf(LOG_DEBUG, "failed to allocate buffer");
        return NULL;
    }

    strcpy(buffer, ttorrent);
    *end = '.';

    return buffer;
}

int server__init_socket(char *port) {

    struct addrinfo hint, *res;
    memset(&hint, 0, sizeof(struct sockaddr_in));
    hint.ai_family = AF_INET;
    hint.ai_socktype = SOCK_STREAM;
    hint.ai_flags = AI_PASSIVE;

    int c;
    if ((c = getaddrinfo(NULL, port, &hint, &res)) != 0) {
        log_printf(LOG_DEBUG, "Error while getting information about the host: %s", gai_strerror(c));
        return -1;
    }

    int s = socket(res->ai_family, res->ai_family, res->ai_protocol);
    if (s == -1) {
        log_printf(LOG_DEBUG, "Error while creating socket: %s", strerror(errno));
        return -1;
    }

    if (!bind(s, res->ai_addr, res->ai_addrlen)) {
        log_message(LOG_DEBUG, "Error while binding");
        return -1;
    }
    return s;
}

#define SERVER__BACKLOG 10
int server_init(char *port, char *metainfo) {
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
    int s;
    if ((s = server__init_socket(port)) == -1) {
        log_printf(LOG_DEBUG, "Failed to init socket with port %i: ", port, strerror(errno));
        return -1;
    }

    log_printf(LOG_DEBUG, "Listening on port %s", port);
    if (listen(s, SERVER__BACKLOG) == -1) {
        log_printf(LOG_DEBUG, "Failed to listen: %s", strerror(errno));
        return -1;
    }

    log_message(LOG_DEBUG, "Recieved a new connection");
    struct sockaddr_storage their_addr;
    unsigned int size = sizeof(their_addr);
    int rcv = accept(s, (struct sockaddr *)&their_addr, &size);
    (void)rcv;
    return 0;
}