// Trivial Torrent

#include "client.h"
#include "file_io.h"
#include "logger.h"
#include "server.h"
#include <netdb.h>

// https://en.wikipedia.org/wiki/Magic_number_(programming)#In_protocols
static const uint32_t MAGIC_NUMBER = 0xde1c3230;

static const uint8_t MSG_REQUEST = 0;
static const uint8_t MSG_RESPONSE_OK = 1;
static const uint8_t MSG_RESPONSE_NA = 2;

enum { RAW_MESSAGE_SIZE = 13 };
int main(int argc, char **argv) {

    set_log_level(LOG_DEBUG);

    log_printf(LOG_INFO, "Trivial Torrent (build %s %s)", __DATE__, __TIME__);

    return 0;
}
