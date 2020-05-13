// Trivial Torrent
// #define ENABLE_FUZZING

#include "client.h"
#include "file_io.h"
#include "logger.h"
#include "server.h"
#include <netdb.h>
#include <stdlib.h>
#include <string.h>
#ifdef ENABLE_FUZZING
#include "sockets_harness.h"
#endif

// https://en.wikipedia.org/wiki/Magic_number_(programming)#In_protocols

int main(int argc, char **argv) {
    set_log_level(LOG_DEBUG);

    log_printf(LOG_INFO, "Trivial Torrent (build %s %s)\n", __DATE__, __TIME__);

    switch (argc) {
    case 2: {
        if (strstr(argv[1], ".ttorrent") == NULL) { // client
            log_printf(LOG_INFO, "File must have the .ttorrent extension");
            break;
        }

        if (client_init(argv[1])) {
            log_printf(LOG_INFO, "Somewthing went wrong with the client");
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

        if (!(port <= UINT16_MAX && port > 0)) {
            log_printf(LOG_INFO, "Port must be a number between %i and %i", UINT16_MAX, 1);
            break;
        }

        if (server_init((uint16_t)port, argv[3])) {
            log_printf(LOG_INFO, "Somewthing went wrong with the server");
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
