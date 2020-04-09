#include "client.h"
#include "file_io.h"
#include "logger.h"
#include "server.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define SERVER_COUNT 20
#define CLIENT_COUNT 5

// TODO

int main() {
    pid_t pid_server[SERVER_COUNT];
    pid_t pid_client[CLIENT_COUNT];
    const char filename[] = "testfile.txt.ttorrent";
    struct fio_torrent_t torrent;
    set_log_level(LOG_DEBUG);

    if (fio_create_torrent_from_metainfo_file(filename, &torrent, "testfile.txt")) {
        log_printf(LOG_INFO, "Failed to load metainfo: %s", strerror(errno));
        return -1;
    }

    for (size_t i = 0; i < SERVER_COUNT; i++) {
        pid_t pid = fork();
        if (pid == 0) { // child, start server
            log_printf(LOG_DEBUG, "Start server %i", i);

            // corrupt the blocks from i to i + 10 for the child process

            for (size_t z = i; z < 10; z++) {
                torrent.block_map[z] = 0;
            }

            // port 8080 + i
            int s = server__init_socket((uint16_t)(8080 + i));
            if (s < 0) log_printf(LOG_DEBUG, "Failed to init socket with port %s, Child %i ", 8080 + i, i);
            if (server__non_blocking(s, &torrent)) log_message(LOG_DEBUG, "Error while calling server__non_blocking");
            log_printf(LOG_DEBUG, "Exit for child server %i", i);
            exit(EXIT_SUCCESS);
        } else { // parent save pid
            pid_server[i] = pid;
        }
    }

    for (size_t i = 0; i < CLIENT_COUNT; i++) {
        pid_t pid = fork();
        if (pid == 0) { // child, start client
            log_printf(LOG_DEBUG, "Start Client %i", i);

            // corrupt all blocks
            for (size_t j = 0; j < torrent.block_count; j++) {
                torrent.block_map[j] = 0;
            }
            if (client__start(&torrent)) {
                log_printf(LOG_DEBUG, "CLIENT FAILED");
            }
            if (client__is_completed(&torrent)) {
                log_printf(LOG_DEBUG, "File is completed for child %i", i);
            }
            log_printf(LOG_DEBUG, "Exit for child client %i", i);
            exit(EXIT_SUCCESS);
        } else { // parent save pid
            pid_client[i] = pid;
        }
    }

    sleep(1);
    log_printf(LOG_DEBUG, "-------------------PARENT------------------");

    for (size_t i = 0; i < CLIENT_COUNT; i++) {
        int stat;
        pid_t pid = waitpid(pid_client[i], &stat, 0);
        if (WIFEXITED(stat))
            log_printf(LOG_DEBUG, "%d terminated: %d\n", pid, WEXITSTATUS(stat));
    }
    // kill server
    for (size_t i = 0; i < SERVER_COUNT; i++) {
        if (!kill(pid_server[i], SIGTERM)) {
            log_printf(LOG_DEBUG, "Server %i killed", pid_server[i]);
        }
    }

    log_printf(LOG_DEBUG, "-------------------TEST COMPLETED------------------");
    return 0;
}