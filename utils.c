#include "utils.h"
#include "logger.h"
#include "server.h"
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

char *utils_original_file_name(const char *const ttorrent) {
    const char *const end = strstr(ttorrent, ".ttorrent");
    if (end == NULL) {
        log_message(LOG_DEBUG, "Invalid file extension");
        return NULL;
    }

    uint32_t count = (uint32_t)(labs(ttorrent - end));
    // Check if count is 0 (when the torrent name is just the extension ex.: .ttorrent)
    if (!count) {
        log_printf(LOG_INFO, "Invalid filename, filename should be name.ttorrent");
        return NULL;
    }

    char *const buffer = (char *)malloc(sizeof(char) * (count));
    if (buffer == NULL) {
        log_printf(LOG_DEBUG, "Failed to allocate buffer");
        return NULL;
    }

    strncpy(buffer, ttorrent, count);

    return buffer;
}

int utils_array_rcv_init(struct utils_array_rcv_data_t *this) {
    this->content = malloc(sizeof(struct utils_array_rcv_data_t) * 4); // start with 4 elements
    if (this->content == NULL) {
        log_printf(LOG_DEBUG, "Malloc failed for utils_rcv_strcut_init:", strerror(errno));
        return -1;
    }

    this->_allocated = 4;
    this->size = 0;
    return 0;
}

int utils_array_rcv_add(struct utils_array_rcv_data_t *this, int sockd, struct server__message_payload_t *buffer) {
    assert(this->size <= this->_allocated);
    struct server__message_t *found = utils_array_rcv_find(this, sockd);
    if (found) {
        // *found = *buffer;
        found->block_number = buffer->block_number;
        found->magic_number = buffer->magic_number;
        found->message_code = buffer->message_code;
    } else {
        // not enough allocated memory, reallocate with 50% more memory.
        if (this->size == this->_allocated) {
            uint32_t new = this->_allocated + this->_allocated / 2;
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
    }
    return 0;
}

struct server__message_t *utils_array_rcv_find(struct utils_array_rcv_data_t *this, int sockd) {
    // TODO we can use binary search here because sockd are always incremented
    for (size_t i = 0; i < this->size; i++) {
        if (this->content[i].from == sockd) {
            return &this->content[i].data;
        }
    }

    return NULL;
}

int utils_array_rcv_remove(struct utils_array_rcv_data_t *this, int fd) {
    // TODO Use binary search
    for (size_t i = 0; i < this->size; i++) {
        if (this->content[i].from == fd) {
            log_printf(LOG_DEBUG, "Deleted socket %i from the msg array", fd);
            for (size_t k = i; k < this->size - 1; k++) {
                this->content[k] = this->content[k + 1];
            }
            this->size--;
            return 0;
        }
    }
    return -1;
}

int utils_array_rcv_destroy(struct utils_array_rcv_data_t *this) {
    free(this->content);
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

int utils_array_pollfd_add(struct utils_array_pollfd_t *this, const int sockd, const short event) {
    assert(this->size <= this->_allocated);

    // not enough allocated memory, reallocate with 50% more memory.
    if (this->size == this->_allocated) {
        uint32_t new = this->_allocated + this->_allocated / 2;
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

    return 0;
}
int utils_array_pollfd_remove(struct utils_array_pollfd_t *this, int fd) {
    // TODO Use binary search
    for (size_t i = 0; i < this->size; i++) {
        if (this->content[i].fd == fd) {
            log_printf(LOG_DEBUG, "Deleted socket %i from polling", fd);
            for (size_t k = i; k < this->size - 1; k++) {
                this->content[k] = this->content[k + 1];
            }
            this->size--;
            return 0;
        }
    }
    return -1;
}

int utils_array_pollfd_destroy(struct utils_array_pollfd_t *this) {
    assert(this->content != NULL);
    free(this->content);
    return 0;
}
