/**
 * This file implements the File IO API specified in file_io.h.
 *
 * This file must be linked with -lssl -lcrypto (provided in the libssl-dev debian package).
 */
#include "file_io.h"
#include "logger.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <netdb.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/**
 * Verifies that a block matches a given checksum.
 * @param block is the block to verify.
 * @param target_digest is the expected checksum.
 * @return 0 if block matches checksum, -1 otherwise.
 */
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

/**
 * Skips lines that start by '#'.
 * @param f is a FILE stream.
 * @return 0 on success, -1 otherwise.
 */
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
            // This is guaranteed to never fail for one byte.
            ungetc(c, f);
            return 0;
        }
    }
}

/**
 * Reads hexadecimal hashes from files.
 * @param hash is where the hash is stored.
 * @param f is an input FILE stream.
 * @return 0 on success, -1 otherwise.
 */
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

    // OPEN FILE

    FILE *const f = fopen(metainfo_file_name, "rb");

    if (!f) {
        return -1;
    }

    torrent->metainfo_file_name = metainfo_file_name;

    // FILE HEADER
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

    // This is the correct way to do ceil(torrent->downloaded_file_size / (double)MAX_BLOCK_SIZE)
    torrent->block_count = (torrent->downloaded_file_size + FIO_MAX_BLOCK_SIZE - 1) / FIO_MAX_BLOCK_SIZE;

    // We technically allow for torrent->downloaded_file_size == 0 and torrent->block_count == 0.
    if (torrent->peer_count == 0 || torrent->peer_count > 0xFFFF) {
        errno = EBADMSG;
        return -1;
    }

    // MEMORY ALLOCATION

    // We assume that...
    assert(sizeof(fio_sha256_hash_t) < FIO_MAX_BLOCK_SIZE);
    assert(sizeof(uint_fast8_t) < FIO_MAX_BLOCK_SIZE);

    // Let us then check for possible overflows in the following mallocs.
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

    // READ BLOCK HASHES

    for (uint64_t i = 0; i < torrent->block_count; i++) {
        if (fio__skip_comment_lines(f)) {
            return -1;
        }

        if (fio__read_hash_from_file(torrent->block_hashes[i], f)) {
            return -1;
        }
    }

    // READ PEERS

    for (uint64_t i = 0; i < torrent->peer_count; i++) {
        if (fio__skip_comment_lines(f)) {
            return -1;
        }

        // Read a text line of up to 1022 bytes (+ '\0').

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

        // This line was too long...

        if (strlen(buffer) == MAX_PEER_STRING - 1) {
            errno = EBADMSG;
            return -1;
        }

        // Remove the trailing \n

        if (buffer[strlen(buffer) - 1] == '\n') {
            buffer[strlen(buffer) - 1] = '\0';
        }

        // 'Parse' host and port

        char *const colon = strrchr(buffer, ':');

        if (colon == NULL) {
            errno = EBADMSG;
            return -1;
        }

        *colon = '\0';

        log_printf(LOG_DEBUG, "\tResolving %s %s ...", buffer, colon + 1);

        // Do name and service resolution, if necessary, and store numeric addresses and ports

        struct addrinfo hints = {0};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        //hints.ai_flags = AI_NUMERICHOST | AI_NUMERICSERV;
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

    // Close metainfo file
    if (fclose(f)) {
        return -1;
    }

    log_message(LOG_DEBUG, "Metainfo successfully loaded; checking downloaded file...");

    // FILE VERIFICATION

    // Open the file, and create it if it does not exist.
    const int fd = open(downloaded_file_name, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);

    if (fd < 0) {
        return -1;
    }

    torrent->downloaded_file_stream = fdopen(fd, "r+");

    if (torrent->downloaded_file_stream == NULL) {
        return -1;
    }

    // Make sure the file is the right size

    // No fflush() seems to be needed here (nor before nor after) as per POSIX.1-2008 § 2.5.1 Interaction of File
    // Descriptors and Standard I/O Streams.

    const off_t file_size = (off_t)torrent->downloaded_file_size;

    if (file_size < 0 || (uint64_t)file_size != torrent->downloaded_file_size) {
        errno = EOVERFLOW;
        return -1;
    }

    if (ftruncate(fileno(torrent->downloaded_file_stream), file_size)) {
        return -1;
    }

    // Populate block_map

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

    // While a zero-length file may be valid, it does not have any block for which to ask its size.
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
            // We made sure the file was the right size in create_torrent_from_metainfo_file.
            // Somebody modified the file under our noses. We treat this as an I/O error.
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
    /* file format
    #SHA-256 of the file
    #Size
    #Peer count
    #SHA-256 of the blocks
    #Peers
    */

    FILE *const out = fopen(strcat(file_name, ".ttorrent"), "w");
    if (!out) {
        log_printf(LOG_INFO, "Failed to create %s file: %s", strcat(file_name, ".ttorrent"), strerror(errno));
        return -1;
    }

    fprintf(out, "#SHA-256 of the file is\n%s\n", info->file_hash);

    fprintf(out, "#Size\n%li\n", info->size);

    fprintf(out, "#Peer count is\n%i\n", PEERCOUNT); // 20 peers from port 8080 to 8099

    fprintf(out, "#SHA-256, number of blocks is %li\n", info->block_count);

    for (size_t i = 0; i < info->block_count; i++) {
        fprintf(out, "%s\n", info->block_sha256[i].hash);
    }

    // default peer,
    fprintf(out, "#Peers\n");
    for (ssize_t i = 0; i < PEERCOUNT; i++) {
        fprintf(out, "localhost:%li\n", 8080 + i);
    }

    fclose(out);
    return 0;
}

int fio_create_metainfo(char *file_name) {
    /*
    A metainfo file contains, among others:
    - The downloaded file length.
    - The SHA256 hash of each block [https://en.wikipedia.org/wiki/SHA-2].
    - A list of address of multiple server peers from which this file can be downloaded
    */
    assert(file_name != NULL);

    log_printf(LOG_DEBUG, "Creating metainfo for %s", file_name);

    // struct to save metainfo data
    struct fio__metainfo_t info = {0};

    // open file
    FILE *const f = fopen(file_name, "rb");
    if (!f) {
        log_printf(LOG_INFO, "Failed to open file %s: %s", file_name, strerror(errno));
        return -1;
    }

    //file hash
    if (fio__sha256_file(f, info.file_hash)) {
        log_printf(LOG_INFO, "Failed to generate hash for the file");
        return -1;
    }

    log_printf(LOG_DEBUG, "File hash: %s", info.file_hash);

    // obtain file size
    fseek(f, 0L, SEEK_END);
    long temp = ftell(f);
    if (temp < 0) {
        log_printf(LOG_DEBUG, "Error obtaining file size: %s", strerror(errno));
        return -1;
    }
    info.size = (uint64_t)temp;
    rewind(f);

    log_printf(LOG_DEBUG, "Size is %li bytes", info.size);

    // Calculate number of blocks
    info.block_count = (info.size + FIO_MAX_BLOCK_SIZE - 1) / FIO_MAX_BLOCK_SIZE;
    log_printf(LOG_DEBUG, "Block count is %li", info.block_count);

    // Calculate SHA-256 for each block
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

    // Create metainfo file
    if (fio__writemetainfo(file_name, &info) < 0) {
        log_printf(LOG_DEBUG, "Failed to write metainfo file: %s", strerror(errno));
        free(info.block_sha256);
        return -1;
    }

    free(info.block_sha256);
    return 0;
}
