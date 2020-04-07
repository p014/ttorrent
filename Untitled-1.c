#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <sys/unistd.h>

#include "client.h"
#include "enum.h"
#include "file_io.h"
#include "logger.h"
#include "utils.h"

/*
1. Load a metainfo file (functionality is already available in the file_io API).
  a. Check for the existence of the associated downloaded file.
  b. Check which blocks are correct using the SHA256 hashes in the metainfo file.
2. For each server peer in the metainfo file:
  a. Connect to that server peer.
  b. For each incorrect block in the downloaded file (the hash does not match in 1b).
    i. Send a request to the server peer.
    ii. If the server responds with the block, store it to the downloaded file.
    iii. Otherwise, if the server signals the unavailablity of the block, do nothing.
  c. Close the connection.
3. Terminate.
*/
char client__is_completed(struct fio_torrent_t *const t) {
    for (size_t i = 0; i < t->block_count; i++) {
        if (!t->block_map[i]) {
            return 0;
        }
    }
    return 1;
}

int client_init(const char *const metainfo) {
    struct fio_torrent_t t;
    // get original filename

    char *filename = utils_original_file_name(metainfo);

    if (!filename) {
        log_printf(LOG_DEBUG, "Error filename is %s", metainfo);
        return -1;
    }

    log_printf(LOG_DEBUG, "metainfo: %s, filename: %s", metainfo, filename);

    if (fio_create_torrent_from_metainfo_file(metainfo, &t, filename)) {
        log_printf(LOG_INFO, "Failed to load metainfo: %s", strerror(errno));
        return -1;
    }

    if (client__is_completed(&t)) {
        log_message(LOG_INFO, "File is complete!");
        return 0;
    }

    for (uint64_t i = 0; i < t.peer_count; i++) {
        int s = socket(AF_INET, SOCK_STREAM, 0);
        if (s < 0) {
            log_printf(LOG_DEBUG, "Failed to create a socket %s", strerror(errno));
            return -1;
        }
        struct sockaddr_in srv_addr;
        char ip_address[20];
        sprintf(ip_address, "%d.%d.%d.%d", t.peers[i].peer_address[0], t.peers[i].peer_address[1], t.peers[i].peer_address[2], t.peers[i].peer_address[3]);

        log_printf(LOG_DEBUG, "Connecting to %s %u", ip_address, ntohs(t.peers[i].peer_port));
        srv_addr.sin_family = AF_INET;
        srv_addr.sin_addr.s_addr = inet_addr(ip_address);
        srv_addr.sin_port = t.peers[i].peer_port;

        if (connect(s, (struct sockaddr *)&srv_addr, sizeof(srv_addr))) {
            log_printf(LOG_INFO, "Connection failed for peer %s %u (%s) trying next peer.", ip_address, ntohs(t.peers[i].peer_port), strerror(errno));

        } else {

            log_printf(LOG_DEBUG, "Connected! Socket %i", s);
            for (uint64_t k = 0; k < t.block_count; k++) {
                if (!t.block_map[k]) { // if hash is incorrect
                    struct utils_message_t message;
                    struct fio_block_t block;
                    message.magic_number = MAGIC_NUMBER;
                    message.message_code = MSG_REQUEST;
                    message.block_number = k;

                    log_printf(LOG_INFO, "requesting magic_number = %x, message_code = %u, block_number = %lu", message.magic_number, message.message_code, message.block_number);

                    send(s, &message, RAW_MESSAGE_SIZE, 0);
                    // recieve block
                    char buffer[RAW_MESSAGE_SIZE + FIO_MAX_BLOCK_SIZE];
                    char *buffertart = buffer;
                    char tempbuffer[FIO_MAX_BLOCK_SIZE];
                    block.size = fio_get_block_size(&t, k);
                    ssize_t c;
                    uint64_t partial_size = 0;
                    while ((c = recv(s, &tempbuffer, RAW_MESSAGE_SIZE + block.size, 0)) > 0) {
                        memcpy(buffertart, tempbuffer, (uint64_t)c);
                        buffertart += c;
                        partial_size += (uint64_t)c;
                        if (RAW_MESSAGE_SIZE + block.size == partial_size || RAW_MESSAGE_SIZE == partial_size)
                            break;
                    }

                    struct utils_message_payload_t *response = (struct utils_message_payload_t *)buffer;
                    log_printf(LOG_INFO, "Recieved %lu bytes, magic_number = %x, message_code = %u, block_number = %lu ", partial_size, response->magic_number, response->message_code, response->block_number);

                    // check response
                    if (c > 0) {
                        if (response->magic_number == MAGIC_NUMBER) {
                            if (response->message_code == MSG_RESPONSE_OK && response->block_number == k) {
                                log_printf(LOG_INFO, "Response is correct!");
                                memcpy(block.data, response->data, block.size);

                                if (fio_store_block(&t, k, &block)) {
                                    log_printf(LOG_DEBUG, "Failed to store block %i: %s", k, strerror(errno));
                                } else {
                                    log_printf(LOG_DEBUG, "Store block %i", k);
                                }

                            } else if (response->message_code == MSG_RESPONSE_NA) {
                                log_printf(LOG_DEBUG, "Block %i not available from peer  %s %u", k, ip_address, ntohs(t.peers[i].peer_port));
                                continue;
                            } else {
                                log_message(LOG_DEBUG, "Response Error");
                                return -1;
                            }
                        } // message

                    } else if (c == 0) {
                        log_printf(LOG_DEBUG, "Remote server closed connection");
                    } else {
                        log_printf(LOG_DEBUG, "Error while recieving: %s", strerror(errno));
                    }

                } // block hash

            } // for

        } // connect

        log_printf(LOG_DEBUG, "Closing socket %i", s);
        close(s);
    } // for

    if (fio_destroy_torrent(&t)) {
        log_printf(LOG_DEBUG, "Error while destroying the torrent struct: ", strerror(errno));
        return -1;
    }
    return 0;
}