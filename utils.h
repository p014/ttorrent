#ifndef UTILS_H_
#define UTILS_H_
#include "enum.h"
#include "file_io.h"
#include <poll.h>

/**
 * Structure to store the messages from the client or server
 * Disable structure packing so we can use it as a buffer for recieving messages.
 * */
struct utils_message_t {
    uint32_t magic_number;
    uint8_t message_code;
    uint64_t block_number;
} __attribute__((packed));

/**
 * Structure to store the messages from the client or server
 * Disable structure packing so we can use it as a buffer for recieving messages.
 * */
struct utils_message_payload_t {
    uint32_t magic_number;
    uint8_t message_code;
    uint64_t block_number;
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
    struct utils__rcv_data_t *__restrict content; // array
    uint32_t size;                                // number of elements
    uint32_t _allocated;                          // real size of the array
};

/**
 * Struct to manage the pollfd array 
 */
struct utils_array_pollfd_t {
    struct pollfd *__restrict content; // array
    uint32_t size;                     // number of elements
    uint32_t _allocated;               // real size of the array
};

/**
 * Get rid of the ttorrent extension to obtain original filename
 * @param metainfo string with .ttorrent extension 
 * @return pointer to the string without .ttorrent or NULL if error
 */
char *utils_original_file_name(const char *const metainfo);

/**
 * Init utils_array_pollfd_t with an array of 4 elements.
 * @param this pointer to the structure
 * @return 0 if no error or -1 on error
 */
int utils_array_pollfd_init(struct utils_array_pollfd_t *this);

/**
 * Init utils_array_pollfd_t with an array of 4 elements.
 * @param this pointer to the structure
 * @return 0 if no error or -1 on error
 */
int utils_array_rcv_init(struct utils_array_rcv_data_t *this);

/**
 * Add socked and events to the array of pollfd
 * @param this struct initated with server__poll_struct_init
 * @param sockd Socked descriptor to be added
 * @param event Events to be added to the pollfd struct
 * @return 0 on success or -1 on error. 
 * If the function fails the struct is not modified.
 */
int utils_array_pollfd_add(struct utils_array_pollfd_t *this, const int sockd,
                           const short event);

/**
 * Add socked and events to the array of pollfd
 * @param this struct initated with server__poll_struct_init
 * @param sockd Socked descriptor to be added
 * @param event Events to be added to the pollfd struct
 * @return 0 on success or -1 on error. 
 * If the function fails the struct is not modified.
 */
int utils_array_rcv_add(struct utils_array_rcv_data_t *this, const int sockd,
                        const struct utils_message_t *const data);

/**
 * Find inside the array the data recieved from sockd
 * @param this pointer to the structure
 * @param socketd socket to find
 * @return pointer to the message recievec on succes or NULL on error
 */
struct utils_message_t *utils_array_rcv_find(struct utils_array_rcv_data_t *this,
                                             const int sockd);

/**
 * Find inside the array the data recieved from sockd
 * @param this pointer to the structure
 * @param socketd socket to find
 * @return pointer to the message recievec on succes or NULL on error
 */
struct pollfd *utils_array_pollfd_find(struct utils_array_pollfd_t *this,
                                       const int sockd);

/**
 * Remove from the array
 * @param this pointer to the structure
 * @param socketd socket to remove
 * @return 0 on succes -1 on error
 */
int utils_array_pollfd_remove(struct utils_array_pollfd_t *this, const int sockd);

/**
 * Remove from the array
 * @param this pointer to the structure
 * @param socketd socket to remove
 * @return 0 on succes -1 on error
 */
int utils_array_rcv_remove(struct utils_array_rcv_data_t *, const int sockd);

/**
 * Free array inside the struct 
 * @param this struct to be freed
 * @return 0 on success or -1 on error
 */
int utils_array_pollfd_destroy(struct utils_array_pollfd_t *this);

/**
 * Free array inside the struct 
 * @param this struct to be freed
 * @return 0 on success or -1 on error
 */
int utils_array_rcv_destroy(struct utils_array_rcv_data_t *this);

/**
 * Wrappers for send and recieving fragmented data 
 * https://stackoverflow.com/questions/13479760/c-socket-recv-and-send-all-data
 * @param socket descriptor to send or recieve data from
 * @param buffer Buffer containing the data to send
 * @param lenght of the buffer
 * @return Same behavior as the unwrapped versions 
 * 
 * > man send
 * Send N bytes of BUF to socket FD. Returns the number sent or -1. 
 * This function is a cancellation point and therefore not marked with
 * __THROW.
 */
ssize_t utils_send_all(int socket, void *buffer, size_t length);

/**
 * Wrappers for send and recieving fragmented data 
 * https://stackoverflow.com/questions/13479760/c-socket-recv-and-send-all-data
 * @param socket descriptor to send or recieve data from
 * @param buffer Buffer containing the data to send
 * @param lenght of the buffer
 * @return Same behavior as the unwrapped versions 
 * 
 * > man recv
 * Read N bytes into BUF from socket FD.
 * Returns the number read or -1 for errors.
 * This function is a cancellation point and therefore not marked with
 * __THROW.
 */
ssize_t utils_recv_all(int socket, void *buffer, size_t length);

#endif