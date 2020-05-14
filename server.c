#include "server.h"
#include "enum.h"
#include "file_io.h"
#include "logger.h"
#include "utils.h"
#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <netdb.h>
#include <signal.h>
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

int server_init(uint16_t const port, struct fio_torrent_t *torrent) {

    if (torrent->downloaded_file_size == 0) {
        log_message(LOG_INFO, "Nothing to download! File size is 0");
        return 0;
    }

    int s = server__init_socket(port);

    if (s < 0) {
        log_printf(LOG_DEBUG, "Failed to init socket with port %i", port);
        return -1;
    }

    if (server__non_blocking(s, torrent)) {
        log_message(LOG_DEBUG, "Error while calling server__non_blocking");
        return -1;
    }

    return 0;
}

#define SERVER__BACKLOG 10
int server__init_socket(const uint16_t port) {

    struct sockaddr_in hint;
    memset(&hint, 0, sizeof(struct sockaddr_in));
    hint.sin_family = AF_INET;
    hint.sin_addr.s_addr = INADDR_ANY;
    hint.sin_port = htons(port);

    // init socket
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) {
        log_printf(LOG_DEBUG, "Socket failed: %s", strerror(errno));
        return -1;
    }

    // set to a non-blocking socket
    if (fcntl(s, F_SETFL, O_NONBLOCK)) {
        log_printf(LOG_DEBUG, "fcntl failed: %s", strerror(errno));
        return -1;
    }

    // bind it!
    if (bind(s, (struct sockaddr *)&hint, sizeof(hint))) {
        log_printf(LOG_DEBUG, "Bind failed: %s", strerror(errno));
        return -1;
    }

    // and listen to incoming connections
    if (listen(s, SERVER__BACKLOG)) {
        log_printf(LOG_DEBUG, "Listen failed: %s", strerror(errno));
        return -1;
    }

    return s;
}

void server__die(char *file_name, int file_line, struct utils_array_rcv_data_t *ptrData, struct utils_array_pollfd_t *ptrPoll) {

    log_printf(LOG_DEBUG, "Program exitted at %s:%d", file_name, file_line);

    if (ptrPoll != NULL)
        utils_array_pollfd_destroy(ptrPoll);
    if (ptrData != NULL)
        utils_array_rcv_destroy(ptrData);

    exit(EXIT_FAILURE);
}

void server__remove_client(struct utils_array_rcv_data_t *ptrData, struct utils_array_pollfd_t *ptrPoll, int sock) {

    if (utils_array_rcv_remove(ptrData, sock)) {
        log_printf(LOG_DEBUG, "No message from %i in the array", sock);
        // SEVER_DIE(ptrData, ptrPoll);
    }

    if (utils_array_pollfd_remove(ptrPoll, sock)) {
        log_printf(LOG_DEBUG, "Could not remove dead socket from polling array");
        SEVER_DIE(ptrData, ptrPoll);
    }

    if (close(sock)) {
        log_printf(LOG_INFO, "Could not close socket %i", sock, strerror(errno));
        SEVER_DIE(ptrData, ptrPoll);
    }
}

int server__non_blocking(const int sockd, struct fio_torrent_t *const torrent) {
    struct utils_array_pollfd_t p;   // array to poll
    struct utils_array_rcv_data_t d; // array to store the rcv messages

    utils_array_pollfd_init(&p);
    utils_array_rcv_init(&d);

    utils_array_pollfd_add(&p, sockd, POLLIN);

    while (1) {
        int revent_c;

        if ((revent_c = poll(p.content, p.size, TIME_TO_POLL)) == -1) {
            log_message(LOG_DEBUG, "Polling failed");
            return -1;
        }

        log_printf(LOG_DEBUG, "Polling returned with %i", revent_c);
        uint32_t c = p.size;
        for (size_t i = 0; i < c; i++) {

            struct pollfd *t = &p.content[i]; // easier to write

            if (t->revents & POLLIN) {

                if (t->fd == sockd) { // accept incoming connections
                    struct sockaddr_in client;
                    unsigned int size = sizeof(struct sockaddr_in);
                    int rcv = accept(sockd, (struct sockaddr *)&client, &size);

                    if (rcv < 0) {
                        log_printf(LOG_DEBUG, "Error while accepting the connection: %s, ignoring connection", strerror(errno));
                        errno = 0;
                        // return -1;
                    }

                    // set socket to non-blocking
                    if (fcntl(rcv, F_SETFL, O_NONBLOCK)) {
                        log_printf(LOG_DEBUG, "cannot set the socket to non-blocking, dropping socket: %s", strerror(errno));
                        errno = 0;
                        continue;
                    }

                    log_printf(LOG_INFO, "Got a connection from %s in socket %i", inet_ntoa(client.sin_addr), rcv);

                    if (utils_array_pollfd_add(&p, rcv, POLLIN)) {
                        log_printf(LOG_INFO, "Could save not message from socket %i to the array", t->fd);
                    }

                } else { // if not server, read data
                         // mark to handle message
                    t->events = POLLOUT;
                    struct utils_message_t buffer;
                    ssize_t read = utils_recv_all(t->fd, &buffer, RAW_MESSAGE_SIZE);

                    if (read < 0) {
                        log_printf(LOG_DEBUG, "Error while reading: %s", strerror(errno));
                        errno = 0;
                        continue;
                    }

                    if (read > 0) {
                        log_printf(LOG_INFO, "Got %i bytes from socket %i", read, t->fd);

                        // store the data to use later

                        if (utils_array_rcv_add(&d, t->fd, &buffer)) {
                            log_printf(LOG_INFO, "Could save not message from socket %i to the array", t->fd);
                        }

                    } else if (read == 0) { // connection closed

                        log_printf(LOG_INFO, "Connection closed on socket %i", t->fd);

                        // remove dead client
                        server__remove_client(&d, &p, t->fd);
                    }
                }

            } else if (t->revents & POLLOUT) { // if we can send without blocking

                // mark for recieving
                t->events = POLLIN;

                struct utils_message_t *msg_rcv = utils_array_rcv_find(&d, t->fd); // find message recieved

                if (msg_rcv == NULL) {
                    log_printf(LOG_DEBUG, "No messages recieved from %i, socket marked for POLLIN", t->fd);
                    log_printf(LOG_DEBUG, "This shouldn't have happened");
                    continue;
                }

                if (msg_rcv->magic_number != MAGIC_NUMBER) { // check recieved message
                    log_printf(LOG_INFO, "Magic number is wrong!");
                    server__remove_client(&d, &p, t->fd);
                    continue;
                }

                if (msg_rcv->message_code != MSG_REQUEST) {
                    log_printf(LOG_INFO, "Message code is wrong!");
                    server__remove_client(&d, &p, t->fd);
                    continue;
                }

                if (msg_rcv->block_number >= torrent->block_count) {
                    log_printf(LOG_INFO, "Block number is outside of bounds!");
                    server__remove_client(&d, &p, t->fd);
                    continue;
                }

                if (!torrent->block_map[msg_rcv->block_number]) { // check if we have the block
                    log_message(LOG_INFO, "Block hash incorrect hash, sending MSG_RESPONSE_NA");
                    struct utils_message_payload_t payload;
                    payload.message_code = MSG_RESPONSE_NA;

                    if (utils_send_all(t->fd, &payload, RAW_MESSAGE_SIZE) <= 0) {
                        log_printf(LOG_INFO, "Could not send MSG_RESPONSE_NA: %s", strerror(errno));
                        errno = 0;
                        continue;
                    }

                    log_printf(LOG_INFO, "Send sucess");
                } else {
                    // contruct the payload and send it

                    struct fio_block_t block;
                    struct utils_message_payload_t payload;

                    payload.magic_number = MAGIC_NUMBER;
                    payload.message_code = MSG_RESPONSE_OK;
                    payload.block_number = msg_rcv->block_number;
                    log_printf(LOG_INFO, "Sending payload for block %lu from socked %i", payload.block_number, t->fd);

                    if (fio_load_block(torrent, payload.block_number, &block)) {
                        log_printf(LOG_INFO, "Cannot load block %i", payload.block_number);
                        continue;
                    }

                    memcpy(payload.data, block.data, block.size);

                    if (utils_send_all(t->fd, &payload, RAW_MESSAGE_SIZE + block.size) <= 0) {
                        log_printf(LOG_INFO, "Could not send the payload: %s", strerror(errno));
                        errno = 0;
                        continue;
                    }

                    log_printf(LOG_INFO, "Send sucess");
                }

            } // POLLOUT

        } // foor loop

    } // while loop

    log_message(LOG_INFO, "Exitting");

    utils_array_pollfd_destroy(&p);
    utils_array_rcv_destroy(&d);

    return 0;
}
