// Trivial Torrent
// #define ENABLE_FUZZING

#include "client.h"
#include "file_io.h"
#include "logger.h"
#include "server.h"
#include "utils.h"
#include <errno.h>
#include <netdb.h>
#include <stdlib.h>
#include <string.h>
#ifdef ENABLE_FUZZING
#include "sockets_harness.h"
#endif

// https://en.wikipedia.org/wiki/Magic_number_(programming)#In_protocols

int main(int argc, char **argv) {
    set_log_level(LOG_DEBUG);

    log_printf(LOG_INFO, "Trivial Torrent (build %s %s)", __DATE__, __TIME__);

    switch (argc) {
    case 2: {
        log_message(LOG_INFO, "Starting Client...");
        struct fio_torrent_t t = {0};

        if (utils_create_torrent_struct(argv[1], &t)) {
            log_printf(LOG_DEBUG, "Failed to create torrent struct from for filename: %s", argv[1]);
            break;
        }

        if (client_init(&t)) {
            log_printf(LOG_INFO, "Somewthing went wrong with the client");
        }

        if (fio_destroy_torrent(&t)) {
            log_printf(LOG_DEBUG, "Error while destroying the torrent struct: %s", strerror(errno));
            break;
        }

        break;
    }
    case 3: {
        if (strcmp(argv[1], "-c") != 0) { // create metainfo file
            log_printf(LOG_INFO, "Invalid switch, run without arguments to get help");
            break;
        }

        if (fio_create_metainfo(argv[2]) != 0) {
            log_printf(LOG_INFO, "Failed to create ttorrent file for %s", argv[2]);
            break;
        }

        break;
    }
    case 4: {
        if (strcmp(argv[1], "-l") != 0) { // server
            log_printf(LOG_INFO, "Invalid switch, run without arguments to get help");
            break;
        }

        log_message(LOG_INFO, "Starting server...");

        int32_t port = atoi(argv[2]);

        if (!(port <= 65535 && port > 0)) { // 65535 should be UINT16_MAX
            log_printf(LOG_INFO, "Port must be a number between %i and %i", 65535, 1);
            break;
        }

        struct fio_torrent_t t = {0};

        if (utils_create_torrent_struct(argv[3], &t)) {
            log_printf(LOG_DEBUG, "Failed to create torrent struct from for filename: %s", argv[3]);
            break;
        }

        if (server_init((uint16_t)port, &t)) {
            log_printf(LOG_INFO, "Somewthing went wrong with the server");
        }

        if (fio_destroy_torrent(&t)) {
            log_printf(LOG_DEBUG, "Error while destroying the torrent struct: %s", strerror(errno));
            break;
        }

        break;
    }
    default: {

        const char HELP_MESSAGE[] =
            "Usage:\nDownload a file: ttorrent file.ttorrent\nUpload a file: ttorrent -l 8080 file.ttorrent\nCreate ttorrent file: ttorrent -c file\n";

        log_printf(LOG_INFO, "%s", HELP_MESSAGE);
        break;
    }
    }

    return 0;
}
