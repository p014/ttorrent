#ifndef SERVER_H
#define SERVER_H

/**
 * Get rid of the ttorrent extension to obtain original filename
 * @param metainfo string with .ttorrent extension 
 * @return pointer to the string without .ttorrent or NULL if error
 */
char *server__original_file_name(char *);

/**
 * Create a socket and bind it to INADDR_ANY:port 
 * @param port port to listen * 
 * @return socket descriptor or -1 on error
 */
int server__init_socket(char *);

int server_init(char *, char *);

#endif