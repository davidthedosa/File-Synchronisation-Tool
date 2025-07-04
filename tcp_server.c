#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <utime.h>
#include <errno.h>

//Standard C and POSIX headers for I/O, sockets, networking, file operations, and error handling.

#define PORT 12345 //PORT: The TCP port the server will listen on.
#define BACKLOG 5 //BACKLOG: Number of pending connections allowed in the queue.
#define BUFSIZE 4096 // Buffer size for reading file data
 //Buffer size for reading file data.


// Helper function to receive exactly n bytes
//recv_all ensures exactly len bytes are received from the socket.
//Loops until all requested bytes are received (since recv might return fewer bytes than requested).
//Returns the total bytes received or an error.
ssize_t recv_all(int sockfd, void *buf, size_t len) {
    size_t total = 0;
    char *ptr = buf;
    while (total < len) {
        ssize_t n = recv(sockfd, ptr + total, len - total, 0);
        if (n <= 0) return n;
        total += n;
    }
    return total;
}

int main() {
    int server_fd, client_fd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);

    //server_fd: Server socket file descriptor.

    //client_fd: Client connection file descriptor.

    //server_addr, client_addr: Structures to hold address info.

    //client_len: Size of the client address structure.

    // 1. Set up socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) { perror("socket"); exit(1); }
    
    //Creates a TCP socket (SOCK_STREAM) using IPv4 (AF_INET).

    //Checks for errors.

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    //sin_family: Address family (IPv4).

    //sin_addr.s_addr: Listen on all interfaces (INADDR_ANY).

    //sin_port: Port number (converted to network byte order).

    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind"); exit(1);
    }

    //Binds the socket to the specified address and port.
    //Checks for errors.


    if (listen(server_fd, BACKLOG) < 0) { perror("listen"); exit(1); }

    //Enables the socket to accept incoming connections.
    //BACKLOG specifies the maximum number of queued connections.

    printf("Server listening on port %d...\n", PORT);

    client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
    if (client_fd < 0) { perror("accept"); exit(1); }
    printf("Client connected.\n");
    
    //Waits for a client to connect.
    //On success, client_fd is a new socket for communication with the client.
    
    // 2. Receive file metadata
    uint32_t name_len;
    if (recv_all(client_fd, &name_len, sizeof(name_len)) <= 0) { perror("recv name_len"); exit(1); }
    name_len = ntohl(name_len);

    //Receives the length of the filename (sent as a 32-bit unsigned int in network byte order).
    //Converts it to host byte order.

    char filename[512];
    if (name_len > sizeof(filename) - 1) { fprintf(stderr, "Filename too long\n"); exit(1); }
    if (recv_all(client_fd, filename, name_len) <= 0) { perror("recv filename"); exit(1); }
    filename[name_len] = '\0';

    //Checks that the filename isn’t too long for the buffer.
    //Receives the filename bytes and null-terminates the string.

    uint64_t filesize;
    if (recv_all(client_fd, &filesize, sizeof(filesize)) <= 0) { perror("recv filesize"); exit(1); }
    filesize = be64toh(filesize);

    //Receives the file size (sent as a 64-bit unsigned int in network byte order).
    //Converts it to host byte order.

    mode_t permissions;
    if (recv_all(client_fd, &permissions, sizeof(permissions)) <= 0) { perror("recv permissions"); exit(1); }
    
    //Receives the file permissions (as a mode_t value).

    struct utimbuf times;
    if (recv_all(client_fd, &times, sizeof(times)) <= 0) { perror("recv utimbuf"); exit(1); }

    //Receives the access and modification times (as a struct utimbuf).

    printf("Receiving file: %s (%lu bytes)\n", filename, filesize);
    
    //Prints info about the incoming file.
    
    // 3. Receive file contents
    FILE *fp = fopen(filename, "wb");
    if (!fp) { perror("fopen"); exit(1); }
    
    //Opens the file for binary writing.
    //Checks for errors.

    size_t received = 0;
    char buffer[BUFSIZE];
    while (received < filesize) {
        size_t to_read = (filesize - received) < BUFSIZE ? (filesize - received) : BUFSIZE;
        ssize_t n = recv(client_fd, buffer, to_read, 0);
        if (n <= 0) { perror("recv file data"); break; }
        fwrite(buffer, 1, n, fp);
        received += n;
    }
    fclose(fp);
    
    //Loops until the entire file is received.
    //Reads up to BUFSIZE bytes at a time from the socket.
    //Writes each chunk to the file.
    //Tracks total bytes received.
    //Closes the file when done.

    // 4. Restore permissions and timestamps
    if (chmod(filename, permissions) < 0) perror("chmod");
    if (utime(filename, &times) < 0) perror("utime");
    
    //Sets the file’s permissions.

    //Sets the file’s access and modification times.

    printf("File received and metadata restored.\n");
 
    close(client_fd);
    close(server_fd);
    return 0;
}
//Prints a completion message.

//Closes the client and server sockets.

//Exits the program.
