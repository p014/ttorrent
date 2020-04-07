#ifndef CLIENT_H
#define CLIENT_H
#include "file_io.h"

int client_init(const char *const);
int client__handle(struct fio_torrent_t *const, const int);
char client__is_completed(struct fio_torrent_t *const t);
#endif