#include "client.h"
/*
1. Load a metainfo file (functionality is already available in the file_io API).
  a. Check for the existence of the associated downloaded file.
  b. Check which blocks are correct using the SHA256 hashes in the metainfo file.
2. For each server peer in the metainfo file:
  a. Connect to that server peer.
  b. For each incorrect block in the downloaded file (the hash does not match in 1b).
    i. Send a request to the server peer.
    ii. If the server responds with the block, store it to the downloaded file.
    iii. Otherwise, if the server signals the unavailablity of the block, do nothing.
  c. Close the connection.
3. Terminate.
*/

int client_init(char *meta) {
    (void)meta;
    return 0;
}
