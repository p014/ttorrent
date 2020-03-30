#include "server.h"
#include "file_io.h"
#include "logger.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

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
char *server__original_file_name(char *original) {
    char *const end = strstr(original, ".metainfo");
    if (end == NULL) return NULL;
    *end = '\0';

    char *const buffer = (char *)malloc(sizeof(char) * strlen(original));
    if (buffer == NULL) {
        log_printf(LOG_DEBUG, "failed to allocate buffer");
        return NULL;
    }
    strcpy(buffer, original);
    *end = '.';
    return buffer;
}

int server_init(int port, char *metainfo) {
    struct fio_torrent_t torrent;
    (void)port;
    if (fio_create_torrent_from_metainfo_file(metainfo, &torrent, "f")) {
        log_printf(LOG_INFO, "Failed to load metainfo: %s", strerror(errno));
        return -1;
    }

    // get original filename
    char *filename = server__original_file_name(metainfo);
    if (!filename) {
        log_printf(LOG_DEBUG, "Error");
        return -1;
    }
    log_printf(LOG_DEBUG, "Metainfo: %s", metainfo);
    log_printf(LOG_DEBUG, "Filename: %s", filename);
    return 0;
}