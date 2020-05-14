#ifndef CLIENT_H
#define CLIENT_H
#include "file_io.h"

/**
 * Main function for the client 
 * @param torrent Pointer to the torrent structure previously created with utils_create_torrent_struct
 * @return 0 for succes or -1 for errors
 */
int client_init(struct fio_torrent_t *torrent);

/** 
 * Handle connections to the peers
 * @param t pointer to struct created with utils_create_torrent_struct
 * @param s descriptor to the connection
 * @param 
 */
int client__handle_connection(struct fio_torrent_t *t, const int s);

/**
 * Check if torrent is completed 
 * @param t pointer to struct created with utils_create_torrent_struct
 * @return 1 if completed, 0 if not completed
 */
char client__is_completed(struct fio_torrent_t *const t);

int client__start(struct fio_torrent_t *t);

#endif