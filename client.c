#include "client.h"
#include "enum.h"
#include "file_io.h"
#include "logger.h"
#include "utils.h"
#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/unistd.h>

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

int client_init(struct fio_torrent_t *t) {
    if (t->downloaded_file_size == 0) {
        log_message(LOG_INFO, "Nothing to download! File size is 0");
        return 0;
    }

    if (client__is_completed(t)) {
        log_message(LOG_INFO, "File is complete!");
        return 0;
    }

    if (client__start(t)) {
        log_printf(LOG_DEBUG, "Client failed");
        return -1;
    }

    log_printf(LOG_DEBUG, "Finished");
    return 0;
}

int client__start(struct fio_torrent_t *t) {
    for (uint64_t i = 0; i < t->peer_count; i++) {

        int s = socket(AF_INET, SOCK_STREAM, 0);

        if (s < 0) {
            log_printf(LOG_DEBUG, "Failed to create a socket %s", strerror(errno));
            errno = 0;
            return -1;
        }

        char ip_address[20];
        if (!sprintf(ip_address, "%d.%d.%d.%d",
                     t->peers[i].peer_address[0], t->peers[i].peer_address[1],
                     t->peers[i].peer_address[2], t->peers[i].peer_address[3])) {
            log_printf(LOG_DEBUG, "Library call failed (sprintf) at %s:%d", __FILE__, __LINE__);
            return -1;
        }

        log_printf(LOG_DEBUG, "Connecting to %s %u", ip_address,
                   ntohs(t->peers[i].peer_port));

        struct sockaddr_in srv_addr;
        memset(&srv_addr, 0, sizeof(struct sockaddr_in));
        srv_addr.sin_family = AF_INET;
        srv_addr.sin_addr.s_addr = inet_addr(ip_address);
        srv_addr.sin_port = t->peers[i].peer_port;

        if (connect(s, (struct sockaddr *)&srv_addr, sizeof(srv_addr))) {
            log_printf(LOG_INFO, "Connection failed for peer %s %u: %s", ip_address,
                       ntohs(t->peers[i].peer_port), strerror(errno));
            log_printf(LOG_INFO, "Trying try next peer");
            errno = 0;
            close(s);
            continue;
        }

        log_printf(LOG_DEBUG, "Connected! Socket %i", s);

        if (client__handle_connection(t, s)) {
            log_printf(LOG_INFO, "Something went wrong with peer: %s %u", ip_address,
                       ntohs(t->peers[i].peer_port));

            log_printf(LOG_INFO, "Trying next peer");
            close(s);
            continue;
        }

        log_printf(LOG_DEBUG, "Closing socket %i", s);
        if (close(s)) {
            log_printf(LOG_DEBUG, "Failed to close socket %i: %s", s, strerror(errno));
            errno = 0;
            return -1;
        }

        if (client__is_completed(t)) {
            log_message(LOG_INFO, "File is complete!");
            return 0;
        }
    }

    return 0;
}

int client__handle_connection(struct fio_torrent_t *t, const int s) {

    for (uint64_t k = 0; k < t->block_count; k++) {

        if (!t->block_map[k]) { // if hash is incorrect, we download the block

            struct utils_message_t message;
            message.magic_number = MAGIC_NUMBER;
            message.message_code = MSG_REQUEST;
            message.block_number = k;

            log_printf(LOG_INFO, "requesting magic_number = %x, message_code = %u, block_number = %lu",
                       message.magic_number, message.message_code, message.block_number);

            if (utils_send_all(s, &message, RAW_MESSAGE_SIZE) < 0) {
                log_printf(LOG_DEBUG, "Could not send %s", strerror(errno));
                errno = 0;
                return -1; // try next peer
            }

            // recieve block
            ssize_t recv_count;
            char buffer[RAW_MESSAGE_SIZE];
            recv_count = utils_recv_all(s, &buffer, RAW_MESSAGE_SIZE);

            if (recv_count == 0) {
                log_printf(LOG_DEBUG, "Connection closed");
                return -1; // try next peer
            } else if (recv_count == -1) {
                log_printf(LOG_DEBUG, "Could not recieve %s", strerror(errno));
                errno = 0;
                return -1; // try next peer
            }

            struct utils_message_t *response_msg = (struct utils_message_t *)buffer;
            log_printf(LOG_INFO, "Recieved magic_number = %x, message_code = %u, block_number = %lu ",
                       response_msg->magic_number, response_msg->message_code, response_msg->block_number);

            if (response_msg->magic_number != MAGIC_NUMBER ||
                response_msg->message_code != MSG_RESPONSE_OK ||
                response_msg->block_number != k) {
                log_printf(LOG_INFO, "Magic number, messagecode or block number wrong, dropping client!");
                return -1; // try next peer
            }

            struct fio_block_t block;
            log_printf(LOG_INFO, "Response is correct!");
            block.size = fio_get_block_size(t, k);
            recv_count = utils_recv_all(s, &block.data, block.size);

            if (recv_count == 0) {
                log_printf(LOG_DEBUG, "Connection closed");
                return -1; // try next peer
            } else if (recv_count == -1) {
                log_printf(LOG_DEBUG, "Could not recieve %s", strerror(errno));
                errno = 0;
                return -1; // try next peer
            }

            if (fio_store_block(t, k, &block)) {
                log_printf(LOG_DEBUG, "Failed to store block %i: %s", k, strerror(errno));
                errno = 0;
            }

            log_printf(LOG_DEBUG, "Block %i stored saved", k);

        } // block hash

    } // for

    return 0;
}
