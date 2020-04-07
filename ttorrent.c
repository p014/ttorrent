// Trivial Torrent

#include "client.h"
#include "file_io.h"
#include "logger.h"
#include "server.h"
#include <netdb.h>
#include <stdlib.h>
#include <string.h>

// https://en.wikipedia.org/wiki/Magic_number_(programming)#In_protocols

int main(int argc, char **argv) {
    set_log_level(LOG_DEBUG);

    log_printf(LOG_INFO, "Trivial Torrent (build %s %s)\n", __DATE__, __TIME__);
    log_printf(LOG_DEBUG, "Commandline arguments (%i arguments)", argc);
    for (int i = 0; i < argc; i++) {
        log_printf(LOG_DEBUG, "%i %s", i, argv[i]);
    }
    log_printf(LOG_DEBUG, "\n");

    if (argc == 1) { // no params
        char help_message[] =
            "Usage:\nDownload a file: ttorrent file.metainfo\nUpload a file: ttorrent -l 8080 file.metainfo\nCreate metainfo: ttorrent -c file\n";
        log_printf(LOG_INFO, "%s", help_message);
        exit(EXIT_SUCCESS);
    }

    if (argc == 3 && strcmp(argv[1], "-c") == 0) { // create metainfo file
        if (fio_create_metainfo(argv[2]) != 0) {
            log_printf(LOG_INFO, "Failed to create metainfo file for %s", argv[2]);
            exit(EXIT_FAILURE);
        }
        exit(EXIT_SUCCESS);
    }

    if (argc == 2 && strstr(argv[1], ".metainfo") != NULL) { // client
        client_init(argv[1]);
    }

    if (argc == 4 && strcmp(argv[1], "-l") == 0) { // server
        log_message(LOG_INFO, "Starting server...");
        int32_t port = atoi(argv[2]);
        if (port > UINT16_MAX || port <= 0) {
            log_printf(LOG_INFO, "Port must be a number between %i and %i", UINT16_MAX, 0);
            exit(EXIT_FAILURE);
        }
        server_init((uint16_t)port, argv[3]);
    }

    exit(EXIT_SUCCESS);
}
