#include "server.h"
#include "enum.h"
#include "file_io.h"
#include "logger.h"
#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/fcntl.h>
#include <sys/poll.h>
#include <sys/unistd.h>
#define TIME_TO_POLL -1 //  wait forever

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

    uint32_t count = (uint32_t)(labs(ttorrent - end));
    // Check if count is 0 (when the torrent name is just the extension ex.: .ttorrent)
    if (!count) {
        log_printf(LOG_INFO, "Invalid filename, filename should be name.ttorrent");
        return NULL;
    }

    char *const buffer = (char *)malloc(sizeof(char) * (count));
    if (buffer == NULL) {
        log_printf(LOG_DEBUG, "Failed to allocate buffer");
        return NULL;
    }

    strncpy(buffer, ttorrent, count);

    return buffer;
}

int server__rcv_strcut_init(struct server__rcv_data_array_t *this, int socketd, char *buffer) {
    this->content = malloc(sizeof(struct server__rcv_data_array_t) * 4); // start with 4 elements
    if (this->content == NULL) {
        log_printf(LOG_DEBUG, "Malloc failed for server___array_t:", strerror(errno));
        return -1;
    }

    this->_allocated = 4;
    this->size = 1;
    this->content[0].from = socketd;
    strcpy(this->content[0].data, buffer);
    return 0;
}

int server__rcv_strcut_add(struct server__rcv_data_array_t *this, int sockd, char *buffer) {
    assert(this->size <= this->_allocated);

    // not enough allocated memory, reallocate with 50% more memory.
    if (this->size == this->_allocated) {
        uint32_t new = this->_allocated + this->_allocated / 2;
        struct server__rcv_data_t *temp = (struct server__rcv_data_t *)realloc(this->content, new * sizeof(struct server__rcv_data_t));
        if (temp == NULL) {
            log_printf(LOG_DEBUG, "Reallocation failed: %s", strerror(errno));
            return -1;
        }
        this->content = temp;
        this->_allocated = new;
    }

    struct server__rcv_data_t *t = &(this->content[this->size]);
    t->from = sockd;
    strcpy(t->data, buffer);
    this->size++;

    return 0;
}

// int server__rcv_strcut_remove(struct server__data_array_t *, int);

int server__rcv_strcut_destroy(struct server__rcv_data_array_t *this) {
    free(this->content);
    return 0;
}

int server__poll_struct_init(struct server__poll_array_t *this, const int sockd, const short event) {
    this->content = malloc(sizeof(struct pollfd) * 4); // start with 4 elements
    if (this->content == NULL) {
        log_printf(LOG_DEBUG, "Malloc failed for server__poll_struct_init:", strerror(errno));
        return -1;
    }

    this->_allocated = 4;
    this->size = 1;
    this->content[0].fd = sockd;
    this->content[0].events = event;
    return 0;
}

int server__poll_struct_add(struct server__poll_array_t *this, const int sockd, const short event) {
    assert(this->size <= this->_allocated);

    // not enough allocated memory, reallocate with 50% more memory.
    if (this->size == this->_allocated) {
        uint32_t new = this->_allocated + this->_allocated / 2;
        struct pollfd *temp = (struct pollfd *)realloc(this->content, new * sizeof(struct pollfd));
        if (temp == NULL) {
            log_printf(LOG_DEBUG, "Reallocation failed: %s", strerror(errno));
            return -1;
        }
        this->content = temp;
        this->_allocated = new;
    }

    struct pollfd *t = &(this->content[this->size]);
    t->fd = sockd;
    t->events = event;
    this->size++;

    return 0;
}

int server__poll_struct_destroy(struct server__poll_array_t *this) {
    assert(this->content != NULL);
    free(this->content);
    return 0;
}

// int server__search_for_block

int server__non_blocking(const int sockd, struct fio_torrent_t *const torrent) {
    struct server__poll_array_t p = {0};
    // struct server__rcv_data_t data = {0};
    char buffer[RAW_MESSAGE_SIZE + 1] = {0};
    if (server__poll_struct_init(&p, sockd, POLLIN)) {
        log_printf(LOG_DEBUG, "Failed to initiate server__poll_array_t: %s", strerror(errno));
        return -1;
    }

    log_printf(LOG_DEBUG, "Contents of the torrent struct");
    log_printf(LOG_DEBUG, "\tmetainfo_file_name: %s", torrent->metainfo_file_name);
    // log_printf(LOG_DEBUG,"downloaded_file_stream: %x",torrent->downloaded_file_stream);
    log_printf(LOG_DEBUG, "tdownloaded_file_hash: %x", torrent->downloaded_file_hash);
    log_printf(LOG_DEBUG, "\tdownloaded_file_size: %u", torrent->downloaded_file_size);
    log_printf(LOG_DEBUG, "\tblock_count: %u", torrent->block_count);
    for (size_t i = 0; i < torrent->block_count; i++) {
        log_printf(LOG_DEBUG, "\t\tblock_hashes, block_map: %x,%i", torrent->block_hashes[i], torrent->block_map[i]);
    }
    log_printf(LOG_DEBUG, "\tpeer_count: %u", torrent->peer_count);
    // for (size_t i = 0; i < torrent->peer_count; i++) {
    //     // log_printf(LOG_DEBUG, "peers: %x", torrent->peers[i]);
    // }

    while (1) {
        int revent_c;
        if ((revent_c = poll(p.content, p.size, TIME_TO_POLL)) == -1) {
            log_message(LOG_DEBUG, "Polling failed");
            return -1;
        }

        log_printf(LOG_DEBUG, "Polling succed number of revents %i", revent_c);
        // check for the returned event
        for (size_t i = 0; i < p.size; i++) {
            if (p.content[i].revents & POLLIN) {
                if (p.content[i].fd == sockd) {
                    // accept incoming connections
                    struct sockaddr_in client;
                    unsigned int size = sizeof(struct sockaddr_in);
                    int rcv = accept(sockd, (struct sockaddr *)&client, &size);
                    // set socket to non-blocking
                    if (fcntl(rcv, F_SETFL, O_NONBLOCK)) {
                        log_printf(LOG_DEBUG, "Error while setting the socket to nonblocking: %s", strerror(errno));
                        return -1;
                    }

                    if (rcv < 0) {
                        log_printf(LOG_DEBUG, "Error while accepting the connection: %s", strerror(errno));
                        return -1;
                    }

                    log_printf(LOG_INFO, "Got a connection from: %s", inet_ntoa(client.sin_addr));
                    // add descriptor to the poll
                    if (server__poll_struct_add(&p, rcv, POLLIN)) {
                        log_printf(LOG_DEBUG, "Incoming connection handling failed, an error ocurred while adding the connection to the array");
                        // ignore connection
                        // return -1
                    }
                    log_printf(LOG_DEBUG, "Connection added to polling");

                } // if incoming connection
                else {
                    // handle connections
                    // a. Wait for a message.
                    // b. If a message requests a block that can be served (correct hash), respond with the appropriate message,
                    // followed by the raw block data.
                    // c. Otherwise, respond with a message signaling the unavailability of the block.
                    ssize_t read = recv(p.content[i].fd, buffer, RAW_MESSAGE_SIZE, 0);
                    // mark for POLLOUT
                    p.content[i].events = POLLOUT;

                    if (read > 0) {
                        buffer[read + 1] = 0;
                        log_printf(LOG_DEBUG, "Got %i bytes from socket %i", read, p.content[i].fd);
                        log_printf(LOG_DEBUG, "\tDATA: %x", buffer);
                        // we need to store the data to use later
                    } else if (read == 0) {
                        log_printf(LOG_DEBUG, "Connection closed on socket %i", p.content[i].fd);
                        if (close(p.content[i].fd)) {
                            log_printf(LOG_DEBUG, "Error while closing socket %i: %s", p.content[i].fd, strerror(errno));
                            // ignore error
                            // return -1;
                        }
                    } else {
                        log_printf(LOG_DEBUG, "Error while reading");
                        // ignore error
                        // return -1
                    }
                }

            } // if POLLIN
            else if (p.content[i].revents & POLLOUT) {
            }

        } // foor loop

    } // while loop

    server__poll_struct_destroy(&p);
    return 0;
}

int server__blocking(const int sockd) {
    struct sockaddr_storage their_addr;

    unsigned int size = sizeof(their_addr);

    // accept incoming connections
    int rcv = accept(sockd, (struct sockaddr *)&their_addr, &size);

    if (rcv < 0) {
        log_printf(LOG_DEBUG, "Error while accepting the connection: %s", strerror(errno));
        return -1;
    }

    log_message(LOG_DEBUG, "Recieved connection");
    return 0;
}

#define SERVER__BACKLOG 10
int server__init_socket(const uint16_t port, const int blocking) {

    struct sockaddr_in hint;
    memset(&hint, 0, sizeof(struct sockaddr_in));
    hint.sin_family = AF_INET;
    hint.sin_addr.s_addr = INADDR_ANY;
    hint.sin_port = htons(port);

    // init socket
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) {
        log_printf(LOG_DEBUG, "Error while creating socket: %s", strerror(errno));
        return -1;
    }
    if (blocking == SERVER__NON_BLOCK) {
        log_message(LOG_DEBUG, "Using non-blocking socket.");
    } else if (blocking == SERVER__BLOCK) {
        log_message(LOG_DEBUG, "Using blocking socket.");
    }

    // set to a non-blocking socket if SERVER__NON_BLOCK is set
    if (blocking == SERVER__NON_BLOCK) {

        if (fcntl(s, F_SETFL, O_NONBLOCK)) {
            log_printf(LOG_DEBUG, "Error while setting the socket to nonblocking: %s", strerror(errno));
            return -1;
        }
    }

    // bind it!
    if (bind(s, (struct sockaddr *)&hint, sizeof(hint))) {
        log_printf(LOG_DEBUG, "Error while binding: %s", strerror(errno));
        return -1;
    }

    // and listen to incoming connections
    log_printf(LOG_DEBUG, "Listening on port %i\n", port);

    if (listen(s, SERVER__BACKLOG)) {
        log_printf(LOG_DEBUG, "Failed to listen: %s", strerror(errno));
        return -1;
    }

    return s;
}

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

    int s = server__init_socket(port, SERVER__NON_BLOCK);
    if (s < 0) {
        log_printf(LOG_DEBUG, "Failed to init socket with port %s", port);
        return -1;
    }

    // since we used SERVER__NON_BLOCK in server__init_socket we need to call server__non_blocking
    if (server__non_blocking(s, &torrent)) {
        log_message(LOG_DEBUG, "Error while calling server__non_blocking");
        return -1;
    }

    if (fio_destroy_torrent(&torrent)) {
        log_printf(LOG_DEBUG, "Error while destroying the torrent struct: ", strerror(errno));
        return -1;
    }
    return 0;
}