#ifndef SERVER_H
#define SERVER_H
#include "file_io.h"
#include <stdint.h>
// #include "utils.h"
/**
 * Enum containing the flags passed to server__init_socket
 */
enum { SERVER__BLOCK,
       SERVER__NON_BLOCK };
/**
 * Create a socket and bind it to INADDR_ANY:port 
 * @param port port to listen * 
 * @param blocking if set to SERVER__BLOCK  use nonblocking or use non blocking sockets if set to SERVER__NON_BLOCK
 * @return socket descriptor or -1 on error
 */
int server__init_socket(const uint16_t, const int);

/**
 * Manage a non-blocking socket, must be used after calling server__init_socket
 * @param sockd A descriptor to a non blocking socket 
 * @return 0 if no error or -1 if error 
 */
int server__non_blocking(const int, struct fio_torrent_t *const);

/**
 * Manage a blocking socket, must be used after calling server__init_socket
 * @param sockd A descriptor to a blocking socket 
 * @return 0 if no error or -1 if error 
 */
int server__blocking(const int);

/**
 * Handle the incoming messages 
 * @param data pointer to the location containing the recieved message
 * @return message to send or NULL if an error ocurred
 */
char *server__craft_message(char *);

/**
 * Main function
 * @param port the port to listen to 
 * @return 0 if everything went correctly or -1 if error
 */
int server_init(const uint16_t, const char *const);

#endif