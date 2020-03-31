#ifndef SERVER_H
#define SERVER_H
#include <stdint.h>

/**
 * Get rid of the ttorrent extension to obtain original filename
 * @param metainfo string with .ttorrent extension 
 * @return pointer to the string without .ttorrent or NULL if error
 */
char *server__original_file_name(const char *const);
/**
 * Enum containing the flags passed to server__init_socket
 * */
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
int server__non_blocking(const int);

/**
 * Manage a blocking socket, must be used after calling server__init_socket
 * @param sockd A descriptor to a blocking socket 
 * @return 0 if no error or -1 if error 
 */
int server__blocking(const int);

/**
 * Main function
 * @param port the port to listen to 
 * @return 
 */
int server_init(const uint16_t, const char *const);

#endif