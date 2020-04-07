#ifndef UTILS_H
#define UTILS_H
#include "enum.h"
#include "file_io.h"
#include "server.h"
#include <poll.h>

/**
 * Structure to store the messages from the client or server
 * Disable structure packing so we can use it as a buffer for recieving messages.
 * */
struct utils_message_t {
    uint32_t magic_number;
    uint64_t block_number;
    uint8_t message_code;
} __attribute__((packed));

struct utils_message_payload_t {
    uint32_t magic_number;
    uint64_t block_number;
    uint8_t message_code;
    uint8_t data[FIO_MAX_BLOCK_SIZE];
} __attribute__((packed));

/**
 * Struct to store the data recieved
 */
struct utils__rcv_data_t {
    int from;                    // socket
    struct utils_message_t data; // data
};

struct utils_array_rcv_data_t {
    struct utils__rcv_data_t *__restrict content;
    uint32_t size;       // number of elements
    uint32_t _allocated; // allocated but empty
};

/**
 * Struct to manage the pollfd array 
 */
struct utils_array_pollfd_t {
    struct pollfd *__restrict content;
    uint32_t size;       // number of elements
    uint32_t _allocated; // allocated but empty
};

/**
 * Get rid of the ttorrent extension to obtain original filename
 * @param metainfo string with .ttorrent extension 
 * @return pointer to the string without .ttorrent or NULL if error
 */
char *utils_original_file_name(const char *const);

/**
 * Init utils_array_pollfd_t with an array of 4 elements.
 * @param this pointer to the structure
 * @return 0 if no error or -1 on error
 */
int utils_array_pollfd_init(struct utils_array_pollfd_t *);

int utils_array_rcv_init(struct utils_array_rcv_data_t *);

/**
 * Add socked and events to the array of pollfd
 * @param this struct initated with server__poll_struct_init
 * @param sockd Socked descriptor to be added
 * @param event Events to be added to the pollfd struct
 * @return 0 on success or -1 on error. 
 * If the function fails the struct is not modified.
 */
int utils_array_pollfd_add(struct utils_array_pollfd_t *, const int, const short);

int utils_array_rcv_add(struct utils_array_rcv_data_t *, int, struct utils_message_t *);

/**
 * Find inside the array the data recieved from sockd
 * @param this pointer to the structure
 * @param socketd socket to find
 * @return pointer to the message recievec on succes or NULL on error
 */
struct utils_message_t *utils_array_rcv_find(struct utils_array_rcv_data_t *, int);

struct pollfd *utils_array_pollfd_find(struct utils_array_pollfd_t *, int);

/**
 * Remove from the array
 * @param this pointer to the structure
 * @param socketd socket to remove
 * @return 0 on succes -1 on error
 */
int utils_array_pollfd_remove(struct utils_array_pollfd_t *, int);

int utils_array_rcv_remove(struct utils_array_rcv_data_t *, int);

/**
 * Free array inside the struct 
 * @param this struct to be freed
 * @return 0 on success or -1 on error
 */
int utils_array_pollfd_destroy(struct utils_array_pollfd_t *);

int utils_array_rcv_destroy(struct utils_array_rcv_data_t *);

// int utils_send_all(const int s, const int len, char *buff);
ssize_t send_all(int socket, void *buffer, size_t length);
ssize_t recv_all(int socket, void *buffer, size_t length);

#endif