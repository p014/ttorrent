#include "utils.h"
#include "file_io.h"
#include "logger.h"
#include "server.h"
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

int utils_create_torrent_struct(char *metainfo, struct fio_torrent_t *torrent) {
    assert(metainfo != NULL);
    assert(torrent != NULL);

    char *end = strstr(metainfo, ".ttorrent");
    if (end == NULL) {
        log_message(LOG_DEBUG, "Invalid file extension");
        return -1;
    }

    if (strcmp(end, ".ttorrent")) {
        log_message(LOG_DEBUG, "Invalid file extension");
        return -1;
    }

    uint32_t charcount = (uint32_t)(end - metainfo);
    if (charcount > 255) {
        log_printf(LOG_INFO, "File name cannot have more than 255 characters: got %u", charcount);
        return -1;
    }

    char filename[255] = "";

    strncpy(filename, metainfo, charcount);

    if (fio_create_torrent_from_metainfo_file(metainfo, torrent, filename)) {
        log_printf(LOG_INFO, "Failed to load metainfo: %s", strerror(errno));
        errno = 0;
        return -1;
    }

    return 0;
}

int utils_array_rcv_init(struct utils_array_rcv_data_t *this) {
    this->content = malloc(sizeof(struct utils__rcv_data_t) * 4); // start with 4 elements
    if (this->content == NULL) {
        log_printf(LOG_DEBUG, "Malloc failed for utils_rcv_strcut_init:", strerror(errno));
        return -1;
    }

    this->_allocated = 4;
    this->size = 0;
    return 0;
}

int utils_array_pollfd_init(struct utils_array_pollfd_t *this) {
    this->content = malloc(sizeof(struct pollfd) * 4); // start with 4 elements
    if (this->content == NULL) {
        log_printf(LOG_DEBUG, "Malloc failed for utils_poll_struct_init:", strerror(errno));
        return -1;
    }

    this->_allocated = 4;
    this->size = 0;
    return 0;
}

int utils_array_rcv_add(struct utils_array_rcv_data_t *this, const int sockd,
                        const struct utils_message_t *const __restrict buffer) {
    assert(this->size <= this->_allocated);

    for (size_t i = 0; i < this->size; i++) {
        if (this->content[i].from == sockd) {
            this->content[i].data.block_number = buffer->block_number;
            this->content[i].data.magic_number = buffer->magic_number;
            this->content[i].data.message_code = buffer->message_code;
            log_printf(LOG_DEBUG, "Socket %i found in rcv array, updating message: magic_number = %x; message_code = %i; block_number = %i;",
                       sockd, buffer->magic_number, buffer->message_code, buffer->block_number);
            return 0;
        }
    }

    // not enough allocated memory
    if (this->size == this->_allocated) {
        uint32_t new = this->_allocated * 2;
        struct utils__rcv_data_t *temp = (struct utils__rcv_data_t *)realloc(this->content, new * sizeof(struct utils__rcv_data_t));
        if (temp == NULL) {
            log_printf(LOG_DEBUG, "Reallocation failed for utils_rcv_strcut_add: %s", strerror(errno));
            return -1;
        }
        this->content = temp;
        this->_allocated = new;
    }

    struct utils__rcv_data_t *t = &(this->content[this->size]);
    t->from = sockd;
    t->data.block_number = buffer->block_number;
    t->data.magic_number = buffer->magic_number;
    t->data.message_code = buffer->message_code;
    this->size++;

    log_printf(LOG_DEBUG, "Socket %i not found, added message: magic_number = %x; message_code = %i; block_number = %i;",
               sockd, buffer->magic_number, buffer->message_code, buffer->block_number);
    return 0;
}

int utils_array_pollfd_add(struct utils_array_pollfd_t *this, const int sockd,
                           const short event) {
    assert(this->size <= this->_allocated);

    // for (size_t i = 0; i < this->size; i++) {
    //     if (this->content[i].fd == sockd) {
    //         this->content[i].events = event;
    //         log_printf(LOG_DEBUG, "Found socket in polling array\n\tSocket %i added to polling", sockd);
    //         return 0;
    //     }
    // }

    // not enough allocated memory
    if (this->size == this->_allocated) {
        uint32_t new = this->_allocated * 2;
        struct pollfd *temp = (struct pollfd *)realloc(this->content, new * sizeof(struct pollfd));
        if (temp == NULL) {
            log_printf(LOG_DEBUG, "Reallocation failed: %s", strerror(errno));
            return -1;
        }
        this->content = temp;
        this->_allocated = new;
    }

    struct pollfd *t = &(this->content[this->size]);
    t->fd = sockd;
    t->events = event;
    this->size++;

    log_printf(LOG_DEBUG, "Socket %i added to polling", sockd);
    return 0;
}

int utils_array_rcv_remove(struct utils_array_rcv_data_t *this,
                           const int sockd) {
    for (size_t i = 0; i < this->size; i++) {
        if (this->content[i].from == sockd) {
            log_printf(LOG_DEBUG, "Deleted socket %i from the msg array", sockd);
            for (size_t k = i; k < this->size - 1; k++) {
                this->content[k] = this->content[k + 1];
            }
            this->size--;
            return 0;
        }
    }
    log_printf(LOG_DEBUG, "Could not delete socket %i from the msg array", sockd);
    return -1;
}

int utils_array_pollfd_remove(struct utils_array_pollfd_t *this,
                              const int sockd) {
    for (size_t i = 0; i < this->size; i++) {
        if (this->content[i].fd == sockd) {
            log_printf(LOG_DEBUG, "Deleted socket %i from polling", sockd);
            for (size_t k = i; k < this->size - 1; k++) {
                this->content[k] = this->content[k + 1];
            }
            this->size--;
            return 0;
        }
    }

    log_printf(LOG_DEBUG, "Could not delete socket %i from polling", sockd);
    return -1;
}

struct pollfd *utils_array_pollfd_find(struct utils_array_pollfd_t *this,
                                       const int sockd) {
    // TODO use binary search?
    for (size_t i = 0; i < this->size; i++) {
        if (this->content[i].fd == sockd)
            return &this->content[i];
    }
    log_printf(LOG_DEBUG, "Socket %i not found in the polling array", sockd);
    return NULL;
}

struct utils_message_t *utils_array_rcv_find(struct utils_array_rcv_data_t *this,
                                             const int sockd) {
    // TODO use binary search?
    for (size_t i = 0; i < this->size; i++) {
        if (this->content[i].from == sockd) {
            return &this->content[i].data;
        }
    }
    log_printf(LOG_DEBUG, "Socket %i not found in the msg array", sockd);
    return NULL;
}

int utils_array_rcv_destroy(struct utils_array_rcv_data_t *this) {
    assert(this->content != NULL);
    free(this->content);
    return 0;
}

int utils_array_pollfd_destroy(struct utils_array_pollfd_t *this) {
    assert(this->content != NULL);
    free(this->content);
    return 0;
}

ssize_t utils_send_all(int socket, void *buffer, size_t length) {
    char *ptr = (char *)buffer;
    size_t total_lenth = 0;
    while (length > 0) {
        ssize_t i = send(socket, ptr, length, MSG_NOSIGNAL);
        if (i < 1)
            return i;
        ptr += (uint64_t)i;
        length -= (size_t)i;
        total_lenth += (size_t)i;
    }
    return (ssize_t)total_lenth;
}

ssize_t utils_recv_all(int socket, void *buffer, size_t length) {
    char *ptr = (char *)buffer;
    size_t total_lenth = 0;
    while (length > 0) {
        ssize_t i = recv(socket, ptr, length, MSG_NOSIGNAL);
        if (i < 1)
            return i;
        ptr += (uint64_t)i;
        length -= (size_t)i;
        total_lenth += (size_t)i;
    }
    return (ssize_t)total_lenth;
}
