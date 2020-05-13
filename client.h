#ifndef CLIENT_H
#define CLIENT_H
#include "file_io.h"

/**
 * Main function for the client 
 * @param metainfo name of the torrent file, must have the extension .ttorrent and must be in the same directory
 * @return 0 for succes or -1 for errors
 */
int client_init(struct fio_torrent_t *t);

/** 
 * Handle connections to the peers
 * @param t pointer to struct created with fio_create_torrent_from_metainfo_file
 * @param s descriptor to the connection
 * @param 
 */
int client__handle_connection(struct fio_torrent_t *t, const int s);

/**
 * Check if torrent is completed 
 * @param t pointer to struct created with fio_create_torrent_from_metainfo_file
 * @return 1 if completed, 0 if not completed
 */
char client__is_completed(struct fio_torrent_t *const t);

int client__start(struct fio_torrent_t *t);

#endif