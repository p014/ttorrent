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
        struct sockaddr_in srv_addr;
        char ip_address[20];

        if (s < 0) {
            log_printf(LOG_DEBUG, "Failed to create a socket %s", strerror(errno));
            return -1;
        }

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
                    char buffer[RAW_MESSAGE_SIZE];
                    struct utils_message_t *response_msg;
                    ssize_t recv_count;

                    message.magic_number = MAGIC_NUMBER;
                    message.message_code = MSG_REQUEST;
                    message.block_number = k;

                    log_printf(LOG_INFO, "requesting magic_number = %x, message_code = %u, block_number = %lu", message.magic_number, message.message_code, message.block_number);

                    if (send_all(s, &message, RAW_MESSAGE_SIZE) < 0) {
                        log_printf(LOG_DEBUG, "Could not send %s", strerror(errno));
                        break; // try next peer
                    }

                    // recieve block
                    recv_count = recv_all(s, &buffer, RAW_MESSAGE_SIZE);
                    if (recv_count == 0) {
                        log_printf(LOG_DEBUG, "Connection closed");
                        break; // next peer
                    } else if (recv_count == -1) {
                        log_printf(LOG_DEBUG, "Could not recieve %s", strerror(errno));
                        break; // next peer
                    }

                    response_msg = (struct utils_message_t *)buffer;
                    log_printf(LOG_INFO, "Recieved magic_number = %x, message_code = %u, block_number = %lu ", response_msg->magic_number, response_msg->message_code, response_msg->block_number);

                    if (response_msg->magic_number == MAGIC_NUMBER) {
                        if (response_msg->message_code == MSG_RESPONSE_OK && response_msg->block_number == k) {
                            struct fio_block_t block;
                            log_printf(LOG_INFO, "Response is correct!");
                            block.size = fio_get_block_size(&t, k);
                            recv_all(s, &block.data, block.size);

                            if (fio_store_block(&t, k, &block)) {
                                log_printf(LOG_DEBUG, "Failed to store block %i: %s", k, strerror(errno));
                            } else {
                                log_printf(LOG_DEBUG, "Store block %i", k);
                            }
                        }
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

    log_printf(LOG_DEBUG, "Finished");
    return 0;
}