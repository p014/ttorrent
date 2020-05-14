#ifndef SERVER_H
#define SERVER_H
#include "file_io.h"
#include "utils.h"
#include <stdint.h>

/**
 * Create a socket and bind it to INADDR_ANY:port 
 * @param port A number between 2^16 and 1
 * @return socket descriptor or -1 on error
 */
int server__init_socket(const uint16_t port);

/**
 * Manage a non-blocking socket, must be used after calling server__init_socket
 * @param sockd A descriptor to a non blocking socket 
 * @param t pointer to struct created with utils_create_torrent_struct
 * @return 0 if no error or -1 if error 
 */
int server__non_blocking(const int sockd, struct fio_torrent_t *const t);

/**
 * Manage a blocking socket, must be used after calling server__init_socket
 * @param sockd A descriptor to a blocking socket 
 * @return 0 if no error or -1 if error 
 */
int server__blocking(const int sockd);

/**
 * Your kill switch
 * The struct pointers maybe NULL.
 * @param sockd A descriptor to a blocking socket 
 * @return Doesn't return.
 */
void server__die(char *file_name, int file_line, struct utils_array_rcv_data_t *ptrData, struct utils_array_pollfd_t *ptrPoll);

// Wrapper for server__die
#define SEVER_DIE(ptrData, ptrPoll) server__die(__FILE__, __LINE__, (ptrData), (ptrPoll));

/**
 * Removes a client from the the two arrays and closes the socket
 * @param d Recieved messages array
 * @param p Polling array
 * @param p Socket used by the client
 * If the socket is not in the Polling array the program will exit.
 */
void server__remove_client(struct utils_array_rcv_data_t *d, struct utils_array_pollfd_t *p, int sock);

/**
 * Main function
 * @param port the port to listen to 
 * @return 0 if everything went correctly or -1 if error
 * @param ttorrent Pointer to the struct created with utils_create_torrent_struct
 */
int server_init(uint16_t const port, struct fio_torrent_t *torrent);

#endif