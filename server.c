#include "server.h"
#include "enum.h"
#include "file_io.h"
#include "logger.h"
#include "utils.h"
#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <netdb.h>
#include <stdio.h>
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
int server_init(uint16_t const port, const char *const metainfo) {
    struct fio_torrent_t torrent;
    // get original filename
    char *filename = utils_original_file_name(metainfo);
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

// int server__blocking(const int sockd) {
//     struct sockaddr_storage their_addr;

//     unsigned int size = sizeof(their_addr);

//     // accept incoming connections
//     int rcv = accept(sockd, (struct sockaddr *)&their_addr, &size);

//     if (rcv < 0) {
//         log_printf(LOG_DEBUG, "Error while accepting the connection: %s", strerror(errno));
//         return -1;
//     }

//     log_message(LOG_DEBUG, "Recieved connection");
//     return 0;
// }

#define BUFFER_SIZE 1

int server__non_blocking(const int sockd, struct fio_torrent_t *const torrent) {
    struct utils_array_pollfd_t p = {0};
    struct utils_array_rcv_data_t data = {0};       // array to store the rcv messages
    struct server__message_t buffer = {0};          // buffer to store the message
    struct server__message_payload_t payload = {0}; // struct for the payload
    struct fio_block_t block = {0};

    if (utils_array_pollfd_init(&p)) {
        log_printf(LOG_DEBUG, "Failed to initiate server__poll_array_t: %s", strerror(errno));
        return -1;
    }
    if (utils_array_pollfd_add(&p, sockd, POLLIN)) {
        log_printf(LOG_DEBUG, "Failed to add socket to the array: %s", strerror(errno));
    }
    if (utils_array_rcv_init(&data)) {
        log_printf(LOG_DEBUG, "Failed to initiate server__poll_array_t: %s", strerror(errno));
        return -1;
    }
    // debug info
    log_printf(LOG_DEBUG, "Contents of the torrent struct");
    log_printf(LOG_DEBUG, "\tmetainfo_file_name: %s", torrent->metainfo_file_name);
    log_printf(LOG_DEBUG, "\tdownloaded_file_stream: %x", torrent->downloaded_file_stream);
    log_printf(LOG_DEBUG, "\tdownloaded_file_hash: %x", torrent->downloaded_file_hash);
    log_printf(LOG_DEBUG, "\tdownloaded_file_size: %u", torrent->downloaded_file_size);
    log_printf(LOG_DEBUG, "\tblock_count: %u", torrent->block_count);
    for (size_t i = 0; i < torrent->block_count; i++) {
        log_printf(LOG_DEBUG, "\t\tblock_hashes, block_map: %x,%i",
                   torrent->block_hashes[i], torrent->block_map[i]);
    }
    log_printf(LOG_DEBUG, "\tpeer_count: %u", torrent->peer_count);
    for (size_t i = 0; i < torrent->peer_count; i++) {
        log_printf(LOG_DEBUG, "\t\tpeers: %x", torrent->peers[i]);
    }

    // Main loop
    while (1) {
        int revent_c;
        if ((revent_c = poll(p.content, p.size, TIME_TO_POLL)) == -1) {
            log_message(LOG_DEBUG, "Polling failed");
            return -1;
        }

        log_printf(LOG_DEBUG, "Polling succed number of revents %i", revent_c);

        // check for the returned event
        for (size_t i = 0; i < p.size; i++) {
            struct pollfd *t = &p.content[i];

            if (t->revents & POLLIN) {
                if (t->fd == sockd) {
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
                    if (utils_array_pollfd_add(&p, rcv, POLLIN)) {
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

                    ssize_t read = recv(t->fd, &buffer, RAW_MESSAGE_SIZE, 0);
                    // mark for POLLOUT
                    t->events = POLLOUT;

                    if (read > 0) {
                        log_printf(LOG_DEBUG, "Got %i bytes from socket %i", read, t->fd);
                        // we need to store the data to use later
                        utils_array_rcv_add(&data, t->fd, &buffer);
                    } else if (read == 0) {
                        log_printf(LOG_DEBUG, "Connection closed on socket %i", t->fd);
                        if (close(t->fd)) {
                            log_printf(LOG_DEBUG, "Error while closing socket %i: %s", t->fd, strerror(errno));
                            // ignore error
                            // return -1;
                        }
                    } else {
                        log_printf(LOG_DEBUG, "Error while reading: %s", strerror(errno));
                        // ignore error
                        // return -1
                    }
                }
            } // if POLLIN, we can send without blocking
            else if (t->revents & POLLOUT) {
                log_printf(LOG_DEBUG, "Can send from %i", t->fd);

                buffer = *utils_array_rcv_find(&data, t->fd);

                if (buffer.magic_number == MAGIC_NUMBER) {

                    log_message(LOG_DEBUG, "Correct magic number");

                    if (buffer.message_code == MSG_REQUEST) {

                        log_printf(LOG_DEBUG, "Got request for block %u", buffer.block_number);

                        if (buffer.block_number < torrent->block_count) { // check bounds

                            payload = *(struct server__message_payload_t *)&buffer;

                            if (torrent->block_map[buffer.block_number]) { // check block hash
                                log_message(LOG_DEBUG, "Block has correct hash");
                                // contruct the payload
                                payload.message_code = MSG_RESPONSE_OK;
                                fio_load_block(torrent, buffer.block_number, &block);
                                memcpy(payload.data, block.data, block.size);

                                // finally send it
                                if (send(t->fd, &payload, RAW_MESSAGE_SIZE + block.size, 0) == -1) {
                                    log_printf(LOG_DEBUG, "Could not send the payload: %s", strerror(errno));
                                }

                            } else {
                                log_message(LOG_DEBUG, "Block has incorrect hash");
                                payload.message_code = MSG_RESPONSE_NA;

                                if (send(t->fd, &payload, RAW_MESSAGE_SIZE, 0) == -1) {
                                    log_printf(LOG_DEBUG, "Could not send MSG_RESPONSE_NA: %s", strerror(errno));
                                }
                            }

                            // mark for reading
                            t->events = POLLOUT;

                        } else {
                            log_message(LOG_DEBUG, "Block index is out of bounds");
                        } // block hash

                    } // request

                } else {
                    log_message(LOG_DEBUG, "Magic number mismatch");
                } // magic number

            } // POLLOUT

        } // foor loop

    } // while loop

    utils_array_pollfd_destroy(&p);
    return 0;
}
