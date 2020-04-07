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

    int s = server__init_socket(port);

    if (s < 0) {
        log_printf(LOG_DEBUG, "Failed to init socket with port %s", port);
        return -1;
    }

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
int server__init_socket(const uint16_t port) {

    struct sockaddr_in hint;
    memset(&hint, 0, sizeof(struct sockaddr_in));
    hint.sin_family = AF_INET;
    hint.sin_addr.s_addr = INADDR_ANY;
    hint.sin_port = htons(port);

    // init socket
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) {
        return -1;
    }

    // set to a non-blocking socket
    if (fcntl(s, F_SETFL, O_NONBLOCK)) {
        return -1;
    }

    // bind it!
    if (bind(s, (struct sockaddr *)&hint, sizeof(hint))) {
        return -1;
    }

    // and listen to incoming connections
    if (listen(s, SERVER__BACKLOG)) {
        return -1;
    }

    return s;
}

int server__non_blocking(const int sockd, struct fio_torrent_t *const torrent) {
    struct utils_array_pollfd_t p;            // array to poll
    struct utils_array_rcv_data_t data;       // array to store the rcv messages
    struct server__message_payload_t payload; // struct for the payload

    utils_array_pollfd_init(&p);
    utils_array_rcv_init(&data);

    utils_array_pollfd_add(&p, sockd, POLLIN);

    while (1) {
        int revent_c;
        if ((revent_c = poll(p.content, p.size, TIME_TO_POLL)) == -1) {
            log_message(LOG_DEBUG, "Polling failed");
            return -1;
        }

        log_printf(LOG_DEBUG, "Polling returned with %i", revent_c);

        for (size_t i = 0; i < p.size; i++) {
            struct pollfd *t = &p.content[i];

            if (t->revents & POLLIN) {

                if (t->fd == sockd) { // accept incoming connections
                    struct sockaddr_in client;
                    unsigned int size = sizeof(struct sockaddr_in);
                    int rcv = accept(sockd, (struct sockaddr *)&client, &size);
                    // set socket to non-blocking
                    fcntl(rcv, F_SETFL, O_NONBLOCK);

                    if (rcv < 0) {
                        log_printf(LOG_DEBUG, "Error while accepting the connection: %s", strerror(errno));
                        return -1;
                    }

                    log_printf(LOG_INFO, "Got a connection from %s in socket %i", inet_ntoa(client.sin_addr), rcv);
                    utils_array_pollfd_add(&p, rcv, POLLIN);

                } else { // read data

                    ssize_t read = recv(t->fd, &payload, RAW_MESSAGE_SIZE, 0);

                    if (read > 0) {
                        log_printf(LOG_INFO, "Got %i bytes from socket %i", read, t->fd);
                        // store the data to use later
                        utils_array_rcv_add(&data, t->fd, &payload);
                        t->events = POLLOUT;
                    } else if (read == 0) {

                        // remove from polling
                        log_printf(LOG_INFO, "Connection closed on socket %i", t->fd);
                        close(t->fd);
                        utils_array_pollfd_remove(&p, t->fd);
                        utils_array_rcv_remove(&data, t->fd);

                    } else {
                        log_printf(LOG_DEBUG, "Error while reading: %s", strerror(errno));
                    }
                }

            } else if (t->revents & POLLOUT) { // if we can send without blocking
                t->events = POLLIN;

                struct server__message_t *te = utils_array_rcv_find(&data, t->fd);
                payload.magic_number = te->magic_number;
                payload.block_number = te->block_number;
                payload.message_code = te->message_code;

                if (payload.magic_number == MAGIC_NUMBER &&
                    payload.message_code == MSG_REQUEST &&
                    payload.block_number < torrent->block_count) {

                    if (torrent->block_map[payload.block_number]) { // check block hash
                        struct fio_block_t block;

                        // contruct the payload and send it
                        log_printf(LOG_INFO, "Sending payload for block %i from socked %i", payload.block_number, t->fd);
                        payload.message_code = MSG_RESPONSE_OK;
                        fio_load_block(torrent, payload.block_number, &block);
                        memcpy(payload.data, block.data, block.size);

                        if (send(t->fd, &payload, RAW_MESSAGE_SIZE + block.size, 0) == -1) {
                            log_printf(LOG_INFO, "Could not send the payload: %s", strerror(errno));
                        } else {
                            log_printf(LOG_INFO, "Send sucess");
                        }

                    } else {
                        log_message(LOG_INFO, "Block hash incorrect hash, sending MSG_RESPONSE_NA");
                        payload.message_code = MSG_RESPONSE_NA;

                        if (send(t->fd, &payload, RAW_MESSAGE_SIZE, 0) == -1) {
                            log_printf(LOG_INFO, "Could not send MSG_RESPONSE_NA: %s", strerror(errno));
                        } else {
                            log_printf(LOG_INFO, "Send sucess");
                        }
                    }

                } // check

            } // POLLOUT

        } // foor loop

    } // while loop

    utils_array_pollfd_destroy(&p);
    utils_array_rcv_destroy(&data);
    return 0;
}
