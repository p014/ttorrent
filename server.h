#ifndef SERVER_H
#define SERVER_H
#include "enum.h"
#include "file_io.h"
#include <stdint.h>
/**
 * Get rid of the ttorrent extension to obtain original filename
 * @param metainfo string with .ttorrent extension 
 * @return pointer to the string without .ttorrent or NULL if error
 */
char *server__original_file_name(const char *const);
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
 * Main function
 * @param port the port to listen to 
 * @return 
 */
int server_init(const uint16_t, const char *const);

/**
 * Struct to manage the pollfd array 
 */
struct server__poll_array_t {
    struct pollfd *__restrict content;
    uint32_t size;       // number of elements
    uint32_t _allocated; // allocated but empty
};

/**
 * Struct to store the data recieved
 */
struct server__rcv_data_t {
    int from;                        // socket
    char data[RAW_MESSAGE_SIZE + 1]; // data
};

struct server__rcv_data_array_t {
    struct server__rcv_data_t *__restrict content;
    uint32_t size;       // number of elements
    uint32_t _allocated; // allocated but empty
};

int server__rcv_strcut_init(struct server__rcv_data_array_t *, int, char *);

int server__rcv_strcut_add(struct server__rcv_data_array_t *, int, char *);

int server__rcv_strcut_remove(struct server__rcv_data_array_t *, int);

int server__rcv_strcut_destroy(struct server__rcv_data_array_t *);

/**
 * Init server__poll_array_t with an array of 4 elements and the socket descriptor.
 * @param sockd socket descriptor
 * @return 0 if no error or -1 on error
 */
int server__poll_struct_init(struct server__poll_array_t *, const int, const short);

/**
 * Add socked and events to the array of pollfd
 * @param this struct initated with server__poll_struct_init
 * @param sockd Socked descriptor to be added
 * @param event Events to be added to the pollfd struct
 * @return 0 on success or -1 on error. 
 * If the function fails the struct is not modified.
 */
int server__poll_struct_add(struct server__poll_array_t *, const int, const short);
/**
 * Free array inside the struct 
 * @param this struct to be freed
 * @return 0 on success or -1 on error
 */
int server__poll_struct_destroy(struct server__poll_array_t *);

#endif