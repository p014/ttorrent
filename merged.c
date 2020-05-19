#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <netdb.h>
#include <netinet/in.h>
#include <openssl/sha.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/fcntl.h>
#include <sys/poll.h>
#include <sys/unistd.h>
#include <unistd.h>
#define SHA256_STRING_LEN 65

static const uint32_t MAGIC_NUMBER = 0xde1c3230;
static const uint8_t MSG_REQUEST = 0;
static const uint8_t MSG_RESPONSE_OK = 1;
static const uint8_t MSG_RESPONSE_NA = 2;
enum { RAW_MESSAGE_SIZE = 13 };
enum log_level_e { LOG_NONE = 0,
                   LOG_INFO = 1,
                   LOG_DEBUG = 2
};
void set_log_level(const enum log_level_e log_level);
void log_message(const enum log_level_e log_level, const char *const message);
void log_printf(const enum log_level_e log_level, const char *const format, ...);
static enum log_level_e current_log_level = LOG_INFO;
static unsigned long LOG_COUNT = 1;
void set_log_level(const enum log_level_e log_level) {
    assert(log_level >= LOG_NONE);
    current_log_level = log_level;
}
void log_message(const enum log_level_e log_level, const char *const message) {
    assert(log_level > LOG_NONE);
    assert(message != NULL);
    if (log_level > current_log_level) {
        return;
    }
    (void)fprintf(stderr, "%lu: %s\n", LOG_COUNT, message);
    LOG_COUNT++;
}
void log_printf(const enum log_level_e log_level, const char *const format, ...) {
    assert(log_level > LOG_NONE);
    assert(format != NULL);
    if (log_level > current_log_level) {
        return;
    }
    va_list ap;
    va_start(ap, format);
    fprintf(stderr, "%lu: ", LOG_COUNT);
    (void)vfprintf(stderr, format, ap);
    va_end(ap);
    (void)fputs("\n", stderr);
    LOG_COUNT++;
}
enum { FIO_MAX_BLOCK_SIZE = 0x10000 };
typedef unsigned char fio_sha256_hash_t[SHA256_DIGEST_LENGTH];
struct fio_peer_information_t {
    uint8_t peer_address[4];
    uint16_t peer_port;
};
struct fio_torrent_t {
    const char *metainfo_file_name;
    FILE *downloaded_file_stream;
    fio_sha256_hash_t downloaded_file_hash;
    uint64_t downloaded_file_size;
    uint64_t block_count;
    fio_sha256_hash_t *block_hashes;
    uint_fast8_t *block_map;
    uint64_t peer_count;
    struct fio_peer_information_t *peers;
};
struct fio_block_t {
    uint8_t data[FIO_MAX_BLOCK_SIZE];
    uint64_t size;
};
int fio_create_torrent_from_metainfo_file(char const *const metainfo_file_name, struct fio_torrent_t *const torrent,
                                          char const *const downloaded_file_name);
uint64_t fio_get_block_size(const struct fio_torrent_t *const torrent, const uint64_t block_number);
int fio_load_block(const struct fio_torrent_t *const torrent, const uint64_t block_number, struct fio_block_t *const block);
int fio_store_block(struct fio_torrent_t *const torrent, const uint64_t block_number, const struct fio_block_t *const block);
int fio_destroy_torrent(struct fio_torrent_t *const torrent);
typedef struct fio__SHA256_STR {
    char hash[SHA256_STRING_LEN];
} fio___SHA256_STR_t;
struct fio__metainfo_t {
    uint64_t size;
    uint64_t block_count;
    char file_hash[SHA256_STRING_LEN];
    fio___SHA256_STR_t *block_sha256;
};
int fio_create_metainfo(char *);
int fio__writemetainfo(char *, struct fio__metainfo_t *);
int fio__sha256_string(char *, size_t, char outputBuffer[65]);
int fio__sha256_file(FILE *const, char outputBuffer[65]);
static int fio__verify_block(const struct fio_block_t *const block, const fio_sha256_hash_t target_digest) {
    assert(block != NULL);
    assert(block->size > 0);
    assert(block->size <= FIO_MAX_BLOCK_SIZE);
    unsigned char real_digest[SHA256_DIGEST_LENGTH];
    SHA256(block->data, block->size, real_digest);
    if (memcmp(real_digest, target_digest, SHA256_DIGEST_LENGTH)) {
        return -1;
    } else {
        return 0;
    }
}
static int fio__skip_comment_lines(FILE *const f) {
    while (1) {
        int c = fgetc(f);
        if (c == EOF) {
            if (feof(f)) {
                errno = EBADMSG;
            }
            return -1;
        }
        if (c == '#') {
            log_printf(LOG_DEBUG, "\t(comment skipped)");
            while ((c = fgetc(f)) != '\n') {
                if (c == EOF) {
                    if (feof(f)) {
                        errno = EBADMSG;
                    }
                    return -1;
                }
            }
        } else {
            ungetc(c, f);
            return 0;
        }
    }
}
static int fio__read_hash_from_file(fio_sha256_hash_t hash, FILE *const f) {
    char buffer[SHA256_DIGEST_LENGTH * 2 + 1] = {0};
    const int r = fscanf(f, "%64[0-9A-Fa-f] ", buffer);
    if (r != 1) {
        if (feof(f)) {
            errno = EBADMSG;
        }
        return -1;
    }
    log_printf(LOG_DEBUG, "\tHash is: %s", buffer);
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        char a = buffer[2 * i];
        char b = buffer[2 * i + 1];
        unsigned char c = (unsigned char)(a < 'A' ? (a - '0') : (a > 'F' ? a - 'a' + 10 : a - 'A' + 10));
        unsigned char d = (unsigned char)(b < 'A' ? (b - '0') : (b > 'F' ? b - 'a' + 10 : b - 'A' + 10));
        hash[i] = (unsigned char)(c << 4) | d;
    }
    return 0;
}
int fio_create_torrent_from_metainfo_file(const char *const metainfo_file_name, struct fio_torrent_t *const torrent,
                                          const char *const downloaded_file_name) {
    assert(metainfo_file_name != NULL);
    assert(torrent != NULL);
    assert(downloaded_file_name != NULL);
    log_printf(LOG_DEBUG, "Loading contents of metainfo file %s...", metainfo_file_name);
    FILE *const f = fopen(metainfo_file_name, "rb");
    if (!f) {
        return -1;
    }
    torrent->metainfo_file_name = metainfo_file_name;
    if (fio__skip_comment_lines(f)) {
        return -1;
    }
    if (fio__read_hash_from_file(torrent->downloaded_file_hash, f)) {
        return -1;
    }
    if (fio__skip_comment_lines(f)) {
        return -1;
    }
    if (fscanf(f, "%" SCNu64 " ", &torrent->downloaded_file_size) != 1) {
        if (feof(f)) {
            errno = EBADMSG;
        }
        return -1;
    }
    log_printf(LOG_DEBUG, "\tDownloaded file size is: %d", torrent->downloaded_file_size);
    if (fio__skip_comment_lines(f)) {
        return -1;
    }
    if (fscanf(f, "%" SCNu64 " ", &torrent->peer_count) != 1) {
        if (feof(f)) {
            errno = EBADMSG;
        }
        return -1;
    }
    log_printf(LOG_DEBUG, "\tPeer count is: %d", torrent->peer_count);
    torrent->block_count = (torrent->downloaded_file_size + FIO_MAX_BLOCK_SIZE - 1) / FIO_MAX_BLOCK_SIZE;
    if (torrent->peer_count == 0 || torrent->peer_count > 0xFFFF) {
        errno = EBADMSG;
        return -1;
    }
    assert(sizeof(fio_sha256_hash_t) < FIO_MAX_BLOCK_SIZE);
    assert(sizeof(uint_fast8_t) < FIO_MAX_BLOCK_SIZE);
    if (torrent->block_count > UINT64_MAX / FIO_MAX_BLOCK_SIZE || torrent->peer_count > UINT64_MAX / sizeof(struct fio_peer_information_t)) {
        errno = ENOMEM;
        return -1;
    }
    torrent->block_hashes = malloc(sizeof(fio_sha256_hash_t) * torrent->block_count);
    if (torrent->block_hashes == NULL) {
        return -1;
    }
    torrent->block_map = malloc(sizeof(uint_fast8_t) * torrent->block_count);
    if (torrent->block_hashes == NULL) {
        free(torrent->block_hashes);
        return -1;
    }
    torrent->peers = malloc(sizeof(struct fio_peer_information_t) * torrent->peer_count);
    if (torrent->peers == NULL) {
        free(torrent->block_hashes);
        free(torrent->block_map);
        return -1;
    }
    for (uint64_t i = 0; i < torrent->block_count; i++) {
        if (fio__skip_comment_lines(f)) {
            return -1;
        }
        if (fio__read_hash_from_file(torrent->block_hashes[i], f)) {
            return -1;
        }
    }
    for (uint64_t i = 0; i < torrent->peer_count; i++) {
        if (fio__skip_comment_lines(f)) {
            return -1;
        }
        const size_t MAX_PEER_STRING = 1024;
        char buffer[MAX_PEER_STRING];
        assert(MAX_PEER_STRING <= INT_MAX);
        char const *const s = fgets(buffer, (int)MAX_PEER_STRING, f);
        if (s == NULL) {
            if (feof(f)) {
                errno = EBADMSG;
            }
            return -1;
        }
        if (strlen(buffer) == MAX_PEER_STRING - 1) {
            errno = EBADMSG;
            return -1;
        }
        if (buffer[strlen(buffer) - 1] == '\n') {
            buffer[strlen(buffer) - 1] = '\0';
        }
        char *const colon = strrchr(buffer, ':');
        if (colon == NULL) {
            errno = EBADMSG;
            return -1;
        }
        *colon = '\0';
        log_printf(LOG_DEBUG, "\tResolving %s %s ...", buffer, colon + 1);
        struct addrinfo hints = {0};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = 0;
        struct addrinfo *result;
        const int r = getaddrinfo(buffer, colon + 1, &hints, &result);
        if (r != 0) {
            log_printf(LOG_INFO, "getaddrinfo: %s", gai_strerror(r));
            errno = EBADMSG;
            return -1;
        }
        assert(result != NULL);
        assert(result->ai_addr->sa_family == AF_INET);
        struct sockaddr_in const *const addr_in = (struct sockaddr_in *)result->ai_addr;
        uint32_t addr = ntohl(addr_in->sin_addr.s_addr);
        torrent->peers[i].peer_address[0] = (uint8_t)(addr >> 24);
        torrent->peers[i].peer_address[1] = (uint8_t)(addr >> 16);
        torrent->peers[i].peer_address[2] = (uint8_t)(addr >> 8);
        torrent->peers[i].peer_address[3] = (uint8_t)(addr >> 0);
        torrent->peers[i].peer_port = addr_in->sin_port;
        log_printf(LOG_DEBUG, "\t... to %d.%d.%d.%d %d",
                   torrent->peers[i].peer_address[0],
                   torrent->peers[i].peer_address[1],
                   torrent->peers[i].peer_address[2],
                   torrent->peers[i].peer_address[3],
                   torrent->peers[i].peer_port);
        freeaddrinfo(result);
    }
    if (fclose(f)) {
        return -1;
    }
    log_message(LOG_DEBUG, "Metainfo successfully loaded; checking downloaded file...");
    const int fd = open(downloaded_file_name, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if (fd < 0) {
        return -1;
    }
    torrent->downloaded_file_stream = fdopen(fd, "r+");
    if (torrent->downloaded_file_stream == NULL) {
        return -1;
    }
    const off_t file_size = (off_t)torrent->downloaded_file_size;
    if (file_size < 0 || (uint64_t)file_size != torrent->downloaded_file_size) {
        errno = EOVERFLOW;
        return -1;
    }
    if (ftruncate(fileno(torrent->downloaded_file_stream), file_size)) {
        return -1;
    }
    for (uint64_t block_number = 0; block_number < torrent->block_count; block_number++) {
        struct fio_block_t block;
        if (fio_load_block(torrent, block_number, &block)) {
            return -1;
        }
        torrent->block_map[block_number] = fio__verify_block(&block, torrent->block_hashes[block_number]) == 0;
        log_printf(LOG_DEBUG, "\tBlock %d is %s", block_number,
                   torrent->block_map[block_number] ? "correct" : "missing");
    }
    return 0;
}
uint64_t fio_get_block_size(const struct fio_torrent_t *const torrent, const uint64_t block_number) {
    assert(torrent != NULL);
    assert(block_number < torrent->block_count);
    assert(torrent->downloaded_file_size > 0);
    const uint64_t last_block_size = torrent->downloaded_file_size % FIO_MAX_BLOCK_SIZE;
    return block_number + 1 == torrent->block_count ? last_block_size : FIO_MAX_BLOCK_SIZE;
}
int fio_load_block(const struct fio_torrent_t *const torrent, const uint64_t block_number, struct fio_block_t *const block) {
    assert(torrent != NULL);
    assert(block_number < torrent->block_count);
    assert(block != NULL);
    assert(torrent->downloaded_file_stream != NULL);
    const uint64_t offset64 = block_number * FIO_MAX_BLOCK_SIZE;
    const off_t offset = (off_t)offset64;
    if (offset < 0 || (uint64_t)offset != offset64) {
        errno = EOVERFLOW;
        return -1;
    }
    const int r1 = fseeko(torrent->downloaded_file_stream, offset, SEEK_SET);
    if (r1) {
        return r1;
    }
    block->size = fio_get_block_size(torrent, block_number);
    const size_t r2 = fread(block->data, 1, block->size, torrent->downloaded_file_stream);
    if (r2 < block->size) {
        if (feof(torrent->downloaded_file_stream)) {
            errno = EIO;
        }
        return -1;
    }
    return 0;
}
int fio_store_block(struct fio_torrent_t *const torrent, const uint64_t block_number, const struct fio_block_t *const block) {
    assert(torrent != NULL);
    assert(torrent->downloaded_file_stream != NULL);
    assert(block_number < torrent->block_count);
    assert(block != NULL);
    assert(block->size > 0);
    const int r1 = fio__verify_block(block, torrent->block_hashes[block_number]);
    if (r1) {
        errno = EINVAL;
        return r1;
    }
    const uint64_t offset64 = block_number * FIO_MAX_BLOCK_SIZE;
    const off_t offset = (off_t)offset64;
    if (offset < 0 || (uint64_t)offset != offset64) {
        errno = EOVERFLOW;
        return -1;
    }
    const int r2 = fseeko(torrent->downloaded_file_stream, offset, SEEK_SET);
    if (r2) {
        return r2;
    }
    const size_t r3 = fwrite(block->data, 1, block->size, torrent->downloaded_file_stream);
    if (r3 < block->size) {
        return -1;
    }
    torrent->block_map[block_number] = 1;
    return 0;
}
int fio_destroy_torrent(struct fio_torrent_t *const torrent) {
    assert(torrent != NULL);
    assert(torrent->block_hashes != NULL);
    assert(torrent->block_map != NULL);
    assert(torrent->peers != NULL);
    assert(torrent->downloaded_file_stream != NULL);
    free(torrent->block_hashes);
    free(torrent->block_map);
    free(torrent->peers);
    return fclose(torrent->downloaded_file_stream);
}
int fio__sha256_file(FILE *const f, char outputBuffer[65]) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256_CTX sha256;
    if (!SHA256_Init(&sha256))
        return -1;
    size_t i;
    uint8_t buf[FIO_MAX_BLOCK_SIZE] = {0};
    while ((i = fread(buf, 1, FIO_MAX_BLOCK_SIZE, f)) > 0)
        if (!SHA256_Update(&sha256, buf, i)) return -1;
    if (!SHA256_Final(hash, &sha256)) return -1;
    for (i = 0; i < SHA256_DIGEST_LENGTH; i++)
        sprintf(outputBuffer + (i * 2), "%02x", hash[i]);
    outputBuffer[64] = 0;
    rewind(f);
    return 0;
}
int fio__sha256_string(char *input, size_t len, char outputBuffer[65]) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256_CTX sha256;
    if (!SHA256_Init(&sha256))
        return -1;
    if (!SHA256_Update(&sha256, input, len))
        return -1;
    if (!SHA256_Final(hash, &sha256))
        return -1;
    for (uint64_t i = 0; i < SHA256_DIGEST_LENGTH; i++)
        sprintf(outputBuffer + (i * 2), "%02x", hash[i]);
    outputBuffer[64] = 0;
    return 0;
}
#define PEERCOUNT 20
int fio__writemetainfo(char *file_name, struct fio__metainfo_t *info) {
    FILE *const out = fopen(strcat(file_name, ".ttorrent"), "w");
    if (!out) {
        log_printf(LOG_INFO, "Failed to create %s file: %s", strcat(file_name, ".ttorrent"), strerror(errno));
        return -1;
    }
    fprintf(out, "#SHA-256 of the file is\n%s\n", info->file_hash);
    fprintf(out, "#Size\n%li\n", info->size);
    fprintf(out, "#Peer count is\n%i\n", PEERCOUNT);
    fprintf(out, "#SHA-256, number of blocks is %li\n", info->block_count);
    for (size_t i = 0; i < info->block_count; i++) {
        fprintf(out, "%s\n", info->block_sha256[i].hash);
    }
    fprintf(out, "#Peers\n");
    for (ssize_t i = 0; i < PEERCOUNT; i++) {
        fprintf(out, "localhost:%li\n", 8080 + i);
    }
    fclose(out);
    return 0;
}
int fio_create_metainfo(char *file_name) {
    assert(file_name != NULL);
    log_printf(LOG_DEBUG, "Creating metainfo for %s", file_name);
    struct fio__metainfo_t info = {0};
    FILE *const f = fopen(file_name, "rb");
    if (!f) {
        log_printf(LOG_INFO, "Failed to open file %s: %s", file_name, strerror(errno));
        return -1;
    }
    if (fio__sha256_file(f, info.file_hash)) {
        log_printf(LOG_INFO, "Failed to generate hash for the file");
        return -1;
    }
    log_printf(LOG_DEBUG, "File hash: %s", info.file_hash);
    fseek(f, 0L, SEEK_END);
    long temp = ftell(f);
    if (temp < 0) {
        log_printf(LOG_DEBUG, "Error obtaining file size: %s", strerror(errno));
        return -1;
    }
    info.size = (uint64_t)temp;
    rewind(f);
    log_printf(LOG_DEBUG, "Size is %li bytes", info.size);
    info.block_count = (info.size + FIO_MAX_BLOCK_SIZE - 1) / FIO_MAX_BLOCK_SIZE;
    log_printf(LOG_DEBUG, "Block count is %li", info.block_count);
    info.block_sha256 = (fio___SHA256_STR_t *)malloc(sizeof(fio___SHA256_STR_t) * info.block_count);
    if (info.block_sha256 == NULL) return -1;
    for (size_t i = 0; i < info.block_count; i++) {
        char buffer[FIO_MAX_BLOCK_SIZE] = {0};
        size_t size = fread(buffer, 1, FIO_MAX_BLOCK_SIZE, f);
        log_printf(LOG_DEBUG, "Calculating hash for block number %li (%i bytes)", i, size);
        if (fio__sha256_string(buffer, size, info.block_sha256[i].hash) < 0) {
            log_printf(LOG_DEBUG, "Error while generating hash for block %li (%i bytes)", i, size);
            free(info.block_sha256);
            return -1;
        }
        log_printf(LOG_DEBUG, "Hash for block %li is: %s", i, info.block_sha256[i].hash);
    }
    fclose(f);
    if (fio__writemetainfo(file_name, &info) < 0) {
        log_printf(LOG_DEBUG, "Failed to write metainfo file: %s", strerror(errno));
        free(info.block_sha256);
        return -1;
    }
    free(info.block_sha256);
    return 0;
}
struct utils_message_t {
    uint32_t magic_number;
    uint8_t message_code;
    uint64_t block_number;
} __attribute__((packed));
struct utils_message_payload_t {
    uint32_t magic_number;
    uint8_t message_code;
    uint64_t block_number;
    uint8_t data[FIO_MAX_BLOCK_SIZE];
} __attribute__((packed));
struct utils__rcv_data_t {
    int from;
    struct utils_message_t data;
};
struct utils_array_rcv_data_t {
    struct utils__rcv_data_t *__restrict content;
    uint32_t size;
    uint32_t _allocated;
};
struct utils_array_pollfd_t {
    struct pollfd *__restrict content;
    uint32_t size;
    uint32_t _allocated;
};
int utils_create_torrent_struct(char *metainfo, struct fio_torrent_t *torrent);
int utils_array_pollfd_init(struct utils_array_pollfd_t *this);
int utils_array_rcv_init(struct utils_array_rcv_data_t *this);
int utils_array_pollfd_add(struct utils_array_pollfd_t *this, const int sockd,
                           const short event);
int utils_array_rcv_add(struct utils_array_rcv_data_t *this, const int sockd,
                        const struct utils_message_t *const data);
struct utils_message_t *utils_array_rcv_find(struct utils_array_rcv_data_t *this,
                                             const int sockd);
struct pollfd *utils_array_pollfd_find(struct utils_array_pollfd_t *this,
                                       const int sockd);
int utils_array_pollfd_remove(struct utils_array_pollfd_t *this, const int sockd);
int utils_array_rcv_remove(struct utils_array_rcv_data_t *, const int sockd);
int utils_array_pollfd_destroy(struct utils_array_pollfd_t *this);
int utils_array_rcv_destroy(struct utils_array_rcv_data_t *this);
ssize_t utils_send_all(int socket, void *buffer, size_t length);
ssize_t utils_recv_all(int socket, void *buffer, size_t length);
int utils_create_torrent_struct(char *metainfo, struct fio_torrent_t *torrent) {
    assert(metainfo != NULL);
    assert(torrent != NULL);
    char *end = strstr(metainfo, ".ttorrent");
    if (end == NULL) {
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
    this->content = malloc(sizeof(struct utils__rcv_data_t) * 4);
    if (this->content == NULL) {
        log_printf(LOG_DEBUG, "Malloc failed for utils_rcv_strcut_init:", strerror(errno));
        return -1;
    }
    this->_allocated = 4;
    this->size = 0;
    return 0;
}
int utils_array_pollfd_init(struct utils_array_pollfd_t *this) {
    this->content = malloc(sizeof(struct pollfd) * 4);
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
    for (size_t i = 0; i < this->size; i++) {
        if (this->content[i].fd == sockd)
            return &this->content[i];
    }
    log_printf(LOG_DEBUG, "Socket %i not found in the polling array", sockd);
    return NULL;
}
struct utils_message_t *utils_array_rcv_find(struct utils_array_rcv_data_t *this,
                                             const int sockd) {
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
#include <poll.h>
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
int client_init(struct fio_torrent_t *torrent);
int client__handle_connection(struct fio_torrent_t *t, const int s);
char client__is_completed(struct fio_torrent_t *const t);
int client__start(struct fio_torrent_t *t);
char client__is_completed(struct fio_torrent_t *const t) {
    for (size_t i = 0; i < t->block_count; i++) {
        if (!t->block_map[i]) {
            return 0;
        }
    }
    return 1;
}
int client_init(struct fio_torrent_t *t) {
    if (t->downloaded_file_size == 0) {
        log_message(LOG_INFO, "Nothing to download! File size is 0");
        return 0;
    }
    if (client__is_completed(t)) {
        log_message(LOG_INFO, "File is complete!");
        return 0;
    }
    if (client__start(t)) {
        log_printf(LOG_DEBUG, "Client failed");
        return -1;
    }
    log_printf(LOG_DEBUG, "Finished");
    return 0;
}
int client__start(struct fio_torrent_t *t) {
    for (uint64_t i = 0; i < t->peer_count; i++) {
        int s = socket(AF_INET, SOCK_STREAM, 0);
        if (s < 0) {
            log_printf(LOG_DEBUG, "Failed to create a socket %s", strerror(errno));
            errno = 0;
            return -1;
        }
        char ip_address[20];
        if (!sprintf(ip_address, "%d.%d.%d.%d",
                     t->peers[i].peer_address[0], t->peers[i].peer_address[1],
                     t->peers[i].peer_address[2], t->peers[i].peer_address[3])) {
            log_printf(LOG_DEBUG, "Library call failed (sprintf) at %s:%d", __FILE__, __LINE__);
            return -1;
        }
        log_printf(LOG_DEBUG, "Connecting to %s %u", ip_address,
                   ntohs(t->peers[i].peer_port));
        struct sockaddr_in srv_addr;
        memset(&srv_addr, 0, sizeof(struct sockaddr_in));
        srv_addr.sin_family = AF_INET;
        srv_addr.sin_addr.s_addr = inet_addr(ip_address);
        srv_addr.sin_port = t->peers[i].peer_port;
        if (connect(s, (struct sockaddr *)&srv_addr, sizeof(srv_addr))) {
            log_printf(LOG_INFO, "Connection failed for peer %s %u: %s", ip_address,
                       ntohs(t->peers[i].peer_port), strerror(errno));
            log_printf(LOG_INFO, "Trying try next peer");
            errno = 0;
            close(s);
            continue;
        }
        log_printf(LOG_DEBUG, "Connected! Socket %i", s);
        if (client__handle_connection(t, s)) {
            log_printf(LOG_INFO, "Something went wrong with peer: %s %u", ip_address,
                       ntohs(t->peers[i].peer_port));
            log_printf(LOG_INFO, "Trying next peer");
            close(s);
            continue;
        }
        log_printf(LOG_DEBUG, "Closing socket %i", s);
        if (close(s)) {
            log_printf(LOG_DEBUG, "Failed to close socket %i: %s", s, strerror(errno));
            errno = 0;
            return -1;
        }
        if (client__is_completed(t)) {
            log_message(LOG_INFO, "File is complete!");
            return 0;
        }
    }
    return 0;
}
int client__handle_connection(struct fio_torrent_t *t, const int s) {
    for (uint64_t k = 0; k < t->block_count; k++) {
        if (!t->block_map[k]) {
            struct utils_message_t message;
            message.magic_number = MAGIC_NUMBER;
            message.message_code = MSG_REQUEST;
            message.block_number = k;
            log_printf(LOG_INFO, "requesting magic_number = %x, message_code = %u, block_number = %lu",
                       message.magic_number, message.message_code, message.block_number);
            if (utils_send_all(s, &message, RAW_MESSAGE_SIZE) < 0) {
                log_printf(LOG_DEBUG, "Could not send %s", strerror(errno));
                errno = 0;
                return -1;
            }
            ssize_t recv_count;
            char buffer[RAW_MESSAGE_SIZE];
            recv_count = utils_recv_all(s, &buffer, RAW_MESSAGE_SIZE);
            if (recv_count == 0) {
                log_printf(LOG_DEBUG, "Connection closed");
                return -1;
            } else if (recv_count == -1) {
                log_printf(LOG_DEBUG, "Could not recieve %s", strerror(errno));
                errno = 0;
                return -1;
            }
            struct utils_message_t *response_msg = (struct utils_message_t *)buffer;
            log_printf(LOG_INFO, "Recieved magic_number = %x, message_code = %u, block_number = %lu ",
                       response_msg->magic_number, response_msg->message_code, response_msg->block_number);
            if (response_msg->magic_number != MAGIC_NUMBER ||
                response_msg->message_code != MSG_RESPONSE_OK ||
                response_msg->block_number != k) {
                log_printf(LOG_INFO, "Magic number, messagecode or block number wrong, trying next peer!");
                return -1;
            }
            struct fio_block_t block;
            log_printf(LOG_INFO, "Response is correct!");
            block.size = fio_get_block_size(t, k);
            recv_count = utils_recv_all(s, &block.data, block.size);
            if (recv_count == 0) {
                log_printf(LOG_DEBUG, "Connection closed");
                return -1;
            } else if (recv_count == -1) {
                log_printf(LOG_DEBUG, "Could not recieve %s", strerror(errno));
                errno = 0;
                return -1;
            }
            if (fio_store_block(t, k, &block)) {
                log_printf(LOG_DEBUG, "Failed to store block %i: %s", k, strerror(errno));
                errno = 0;
            }
            log_printf(LOG_DEBUG, "Block %i stored saved", k);
        }
    }
    return 0;
}
int server__init_socket(const uint16_t port);
int server__non_blocking(const int sockd, struct fio_torrent_t *const t);
int server__blocking(const int sockd);
void server__die(char *file_name, int file_line, struct utils_array_rcv_data_t *ptrData, struct utils_array_pollfd_t *ptrPoll);
#define SEVER_DIE(ptrData, ptrPoll) server__die(__FILE__, __LINE__, (ptrData), (ptrPoll));
void server__remove_client(struct utils_array_rcv_data_t *d, struct utils_array_pollfd_t *p, int sock);
int server_init(uint16_t const port, struct fio_torrent_t *torrent);
#define TIME_TO_POLL -1

int server_init(uint16_t const port, struct fio_torrent_t *torrent) {
    if (torrent->downloaded_file_size == 0) {
        log_message(LOG_INFO, "Nothing to download! File size is 0");
        return 0;
    }
    int s = server__init_socket(port);
    if (s < 0) {
        log_printf(LOG_DEBUG, "Failed to init socket with port %i", port);
        return -1;
    }
    if (server__non_blocking(s, torrent)) {
        log_message(LOG_DEBUG, "Error while calling server__non_blocking");
        return -1;
    }
    return 0;
}
#define SERVER__BACKLOG 10
int server__init_socket(const uint16_t port) {
    struct sockaddr_in hint;
    memset(&hint, 0, sizeof(struct sockaddr_in));
    hint.sin_family = AF_INET;
    hint.sin_addr.s_addr = INADDR_ANY;
    hint.sin_port = htons(port);
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) {
        log_printf(LOG_DEBUG, "Socket failed: %s", strerror(errno));
        return -1;
    }
    if (fcntl(s, F_SETFL, O_NONBLOCK)) {
        log_printf(LOG_DEBUG, "fcntl failed: %s", strerror(errno));
        return -1;
    }
    if (bind(s, (struct sockaddr *)&hint, sizeof(hint))) {
        log_printf(LOG_DEBUG, "Bind failed: %s", strerror(errno));
        return -1;
    }
    if (listen(s, SERVER__BACKLOG)) {
        log_printf(LOG_DEBUG, "Listen failed: %s", strerror(errno));
        return -1;
    }
    return s;
}
void server__die(char *file_name, int file_line, struct utils_array_rcv_data_t *ptrData, struct utils_array_pollfd_t *ptrPoll) {
    log_printf(LOG_DEBUG, "Program exitted at %s:%d", file_name, file_line);
    if (ptrPoll != NULL)
        utils_array_pollfd_destroy(ptrPoll);
    if (ptrData != NULL)
        utils_array_rcv_destroy(ptrData);
    exit(EXIT_FAILURE);
}
void server__remove_client(struct utils_array_rcv_data_t *ptrData, struct utils_array_pollfd_t *ptrPoll, int sock) {
    if (utils_array_rcv_remove(ptrData, sock)) {
        log_printf(LOG_DEBUG, "No message from %i in the array", sock);
    }
    if (utils_array_pollfd_remove(ptrPoll, sock)) {
        log_printf(LOG_DEBUG, "Could not remove dead socket from polling array");
        SEVER_DIE(ptrData, ptrPoll);
    }
    if (close(sock)) {
        log_printf(LOG_INFO, "Could not close socket %i", sock, strerror(errno));
        SEVER_DIE(ptrData, ptrPoll);
    }
}
int server__non_blocking(const int sockd, struct fio_torrent_t *const torrent) {
    struct utils_array_pollfd_t p;
    struct utils_array_rcv_data_t d;
    utils_array_pollfd_init(&p);
    utils_array_rcv_init(&d);
    utils_array_pollfd_add(&p, sockd, POLLIN);
    while (1) {
        int revent_c;
        if ((revent_c = poll(p.content, p.size, TIME_TO_POLL)) == -1) {
            log_message(LOG_DEBUG, "Polling failed");
            return -1;
        }
        log_printf(LOG_DEBUG, "Polling returned with %i", revent_c);
        uint32_t c = p.size;
        for (size_t i = 0; i < c; i++) {
            struct pollfd *t = &p.content[i];
            if (t->revents & POLLIN) {
                if (t->fd == sockd) {
                    struct sockaddr_in client;
                    unsigned int size = sizeof(struct sockaddr_in);
                    int rcv = accept(sockd, (struct sockaddr *)&client, &size);
                    if (rcv < 0) {
                        log_printf(LOG_DEBUG, "Error while accepting the connection: %s, ignoring connection", strerror(errno));
                        errno = 0;
                    }
                    if (fcntl(rcv, F_SETFL, O_NONBLOCK)) {
                        log_printf(LOG_DEBUG, "cannot set the socket to non-blocking, dropping socket: %s", strerror(errno));
                        errno = 0;
                        continue;
                    }
                    log_printf(LOG_INFO, "Got a connection from %s in socket %i", inet_ntoa(client.sin_addr), rcv);
                    if (utils_array_pollfd_add(&p, rcv, POLLIN)) {
                        log_printf(LOG_INFO, "Could save not message from socket %i to the array", t->fd);
                    }
                } else {
                    t->events = POLLOUT;
                    struct utils_message_t buffer;
                    ssize_t read = utils_recv_all(t->fd, &buffer, RAW_MESSAGE_SIZE);
                    if (read < 0) {
                        log_printf(LOG_DEBUG, "Error while reading: %s", strerror(errno));
                        errno = 0;
                        continue;
                    }
                    if (read > 0) {
                        log_printf(LOG_INFO, "Got %i bytes from socket %i", read, t->fd);
                        if (utils_array_rcv_add(&d, t->fd, &buffer)) {
                            log_printf(LOG_INFO, "Could save not message from socket %i to the array", t->fd);
                        }
                    } else if (read == 0) {
                        log_printf(LOG_INFO, "Connection closed on socket %i", t->fd);
                        server__remove_client(&d, &p, t->fd);
                    }
                }
            } else if (t->revents & POLLOUT) {
                t->events = POLLIN;
                struct utils_message_t *msg_rcv = utils_array_rcv_find(&d, t->fd);
                if (msg_rcv == NULL) {
                    log_printf(LOG_DEBUG, "No messages recieved from %i, socket marked for POLLIN", t->fd);
                    log_printf(LOG_DEBUG, "This shouldn't have happened");
                    continue;
                }
                log_printf(LOG_INFO, "Recieved magic_number = %x, message_code = %u, block_number = %lu ",
                           msg_rcv->magic_number, msg_rcv->message_code, msg_rcv->block_number);
                if (msg_rcv->magic_number != MAGIC_NUMBER ||
                    msg_rcv->message_code != MSG_REQUEST ||
                    msg_rcv->block_number >= torrent->block_count) {
                    log_printf(LOG_INFO, "Magic number, messagecode or block number wrong, dropping client!");
                    server__remove_client(&d, &p, t->fd);
                    continue;
                }
                if (!torrent->block_map[msg_rcv->block_number]) {
                    log_message(LOG_INFO, "Block hash incorrect hash, sending MSG_RESPONSE_NA");
                    struct utils_message_payload_t payload;
                    payload.message_code = MSG_RESPONSE_NA;
                    if (utils_send_all(t->fd, &payload, RAW_MESSAGE_SIZE) <= 0) {
                        log_printf(LOG_INFO, "Could not send MSG_RESPONSE_NA: %s", strerror(errno));
                        errno = 0;
                        continue;
                    }
                    log_printf(LOG_INFO, "Send sucess");
                    continue;
                }
                struct fio_block_t block;
                struct utils_message_payload_t payload;
                payload.magic_number = MAGIC_NUMBER;
                payload.message_code = MSG_RESPONSE_OK;
                payload.block_number = msg_rcv->block_number;
                log_printf(LOG_INFO, "Sending payload for block %lu from socked %i", payload.block_number, t->fd);
                if (fio_load_block(torrent, payload.block_number, &block)) {
                    log_printf(LOG_INFO, "Cannot load block %i", payload.block_number);
                    continue;
                }
                memcpy(payload.data, block.data, block.size);
                if (utils_send_all(t->fd, &payload, RAW_MESSAGE_SIZE + block.size) <= 0) {
                    log_printf(LOG_INFO, "Could not send the payload: %s", strerror(errno));
                    errno = 0;
                    continue;
                }
                log_printf(LOG_INFO, "Send sucess");
                continue;
            }
        }
    }
    log_message(LOG_INFO, "Exitting");
    utils_array_pollfd_destroy(&p);
    utils_array_rcv_destroy(&d);
    return 0;
}
int main(int argc, char **argv) {
    set_log_level(LOG_DEBUG);
    log_printf(LOG_INFO, "Trivial Torrent (build %s %s)", __DATE__, __TIME__);
    switch (argc) {
    case 2: {
        log_message(LOG_INFO, "Starting Client...");
        struct fio_torrent_t t = {0};
        if (utils_create_torrent_struct(argv[1], &t)) {
            log_printf(LOG_DEBUG, "Failed to create torrent struct from for filename: %s", argv[1]);
            break;
        }
        if (client_init(&t)) {
            log_printf(LOG_INFO, "Somewthing went wrong with the client");
        }
        if (fio_destroy_torrent(&t)) {
            log_printf(LOG_DEBUG, "Error while destroying the torrent struct: %s", strerror(errno));
            break;
        }
        break;
    }
    case 3: {
        if (strcmp(argv[1], "-c") != 0) {
            log_printf(LOG_INFO, "Invalid switch, run without arguments to get help");
            break;
        }
        if (fio_create_metainfo(argv[2]) != 0) {
            log_printf(LOG_INFO, "Failed to create ttorrent file for %s", argv[2]);
            break;
        }
        break;
    }
    case 4: {
        if (strcmp(argv[1], "-l") != 0) {
            log_printf(LOG_INFO, "Invalid switch, run without arguments to get help");
            break;
        }
        log_message(LOG_INFO, "Starting server...");
        int32_t port = atoi(argv[2]);
        if (!(port <= 65535 && port > 0)) {
            log_printf(LOG_INFO, "Port must be a number between %i and %i", 65535, 1);
            break;
        }
        struct fio_torrent_t t = {0};
        if (utils_create_torrent_struct(argv[3], &t)) {
            log_printf(LOG_DEBUG, "Failed to create torrent struct from for filename: %s", argv[3]);
            break;
        }
        if (server_init((uint16_t)port, &t)) {
            log_printf(LOG_INFO, "Somewthing went wrong with the server");
        }
        if (fio_destroy_torrent(&t)) {
            log_printf(LOG_DEBUG, "Error while destroying the torrent struct: %s", strerror(errno));
            break;
        }
        break;
    }
    default: {
        const char HELP_MESSAGE[] =
            "Usage:\nDownload a file: ttorrent file.ttorrent\nUpload a file: ttorrent -l 8080 file.ttorrent\nCreate ttorrent file: ttorrent -c file\n";
        log_printf(LOG_INFO, "%s", HELP_MESSAGE);
        break;
    }
    }
    return 0;
}