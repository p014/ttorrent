// Trivial Torrent

#include "client.h"
#include "file_io.h"
#include "logger.h"
#include "server.h"
#include <netdb.h>
#include <stdlib.h>
#include <string.h>

// https://en.wikipedia.org/wiki/Magic_number_(programming)#In_protocols
static const uint32_t MAGIC_NUMBER = 0xde1c3230;

static const uint8_t MSG_REQUEST = 0;
static const uint8_t MSG_RESPONSE_OK = 1;
static const uint8_t MSG_RESPONSE_NA = 2;

enum { RAW_MESSAGE_SIZE = 13 };
int main(int argc, char **argv) {
    set_log_level(LOG_DEBUG);

    log_printf(LOG_INFO, "Trivial Torrent (build %s %s)\n", __DATE__, __TIME__);
    log_printf(LOG_DEBUG, "Commandline arguments (%i arguments)", argc);
    for (int i = 0; i < argc; i++) {
        log_printf(LOG_DEBUG, "%i %s", i, argv[i]);
    }
    log_printf(LOG_DEBUG, "\n");

    if (argc == 1) { // no params
        char help_message[] = "\
            Usage:\n\
            Download a file: ttorrent file.metainfo\n\
            Upload a file: ttorrent -l 8080 file.metainfo\n\
            Create metainfo: ttorrent -c file\n\
            ";
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
        server_init(atoi(argv[2]), argv[3]);
    }
    (void)MAGIC_NUMBER;
    (void)MSG_REQUEST;
    (void)MSG_RESPONSE_NA;
    (void)MSG_RESPONSE_OK;

    exit(EXIT_SUCCESS);
}
