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
    log_printf(LOG_DEBUG, "Commandline arguments");
    for (size_t i = 1; i < argc; i++) {
        log_printf(LOG_DEBUG, "%i %s", i, argv[i]);
    }
    log_printf(LOG_DEBUG, "\n");

    if (argc == 1) { // no params
        char help_message[] =
            "\
            Usage:\n\
            Download a file: ttorrent file.metainfo\n\
            Upload a file: ttorrent -l 8080 file.metainfo\n\
            Create metainfo: ttorrent -c file\n\
            ";
        log_printf(LOG_INFO, "%s", help_message);
        exit(EXIT_SUCCESS);
    }

    if (argc == 3) {
        if (strcmp(argv[1], "-c") == 0) { // create metainfo file
            fio_create_metainfo(argv[2]);
        }
    }

    return 0;
}
