#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <errno.h>
#include <utime.h>
#include <sys/inotify.h>
#include <sys/select.h>

#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 12345
#define BUFSIZE 4096
#define WATCH_DIR "./client_dir"
#define EVENT_MASK (IN_CREATE | IN_MODIFY)
#define MAX_TRACKED_FILES 100

typedef struct {
    char filename[512];
    time_t last_sent_mtime;
} FileTracker;

FileTracker tracked_files[MAX_TRACKED_FILES];
int tracked_count = 0;

ssize_t send_all(int sockfd, const void *buf, size_t len) {
    size_t total = 0;
    const char *ptr = buf;
    while (total < len) {
        ssize_t n = send(sockfd, ptr + total, len - total, 0);
        if (n <= 0) return n;
        total += n;
    }
    return total;
}

void send_file(const char *filepath, int sockfd) {
    struct stat st;
    if (stat(filepath, &st) < 0) return;

    // Check mtime cache
    for (int i = 0; i < tracked_count; ++i) {
        if (strcmp(tracked_files[i].filename, filepath) == 0 &&
            tracked_files[i].last_sent_mtime == st.st_mtime)
            return; // already sent, no change
    }

    struct utimbuf times = { st.st_atime, st.st_mtime };
    FILE *fp = fopen(filepath, "rb");
    if (!fp) return;

    const char *filename = strrchr(filepath, '/');
    filename = filename ? filename + 1 : filepath;

    uint32_t name_len = htonl(strlen(filename));
    uint64_t filesize = htobe64(st.st_size);

    send_all(sockfd, &name_len, sizeof(name_len));
    send_all(sockfd, filename, strlen(filename));
    send_all(sockfd, &filesize, sizeof(filesize));
    send_all(sockfd, &st.st_mode, sizeof(st.st_mode));
    send_all(sockfd, &times, sizeof(times));

    char buffer[BUFSIZE];
    size_t n;
    while ((n = fread(buffer, 1, BUFSIZE, fp)) > 0)
        send_all(sockfd, buffer, n);

    fclose(fp);
    printf("Sent file: %s\n", filename);

    // Update cache
    int updated = 0;
    for (int i = 0; i < tracked_count; ++i) {
        if (strcmp(tracked_files[i].filename, filepath) == 0) {
            tracked_files[i].last_sent_mtime = st.st_mtime;
            updated = 1;
            break;
        }
    }
    if (!updated && tracked_count < MAX_TRACKED_FILES) {
        strcpy(tracked_files[tracked_count].filename, filepath);
        tracked_files[tracked_count].last_sent_mtime = st.st_mtime;
        tracked_count++;
    }
}

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

void receive_file(int sockfd) {
    uint32_t name_len;
    if (recv_all(sockfd, &name_len, sizeof(name_len)) <= 0) return;
    name_len = ntohl(name_len);

    char filename[512];
    if (recv_all(sockfd, filename, name_len) <= 0) return;
    filename[name_len] = '\0';

    uint64_t filesize;
    if (recv_all(sockfd, &filesize, sizeof(filesize)) <= 0) return;
    filesize = be64toh(filesize);

    mode_t permissions;
    recv_all(sockfd, &permissions, sizeof(permissions));
    struct utimbuf times;
    recv_all(sockfd, &times, sizeof(times));

    char fullpath[600];
    snprintf(fullpath, sizeof(fullpath), "%s/%s", WATCH_DIR, filename);
    FILE *fp = fopen(fullpath, "wb");
    if (!fp) return;

    size_t received = 0;
    char buffer[BUFSIZE];
    while (received < filesize) {
        size_t to_read = (filesize - received < BUFSIZE) ? (filesize - received) : BUFSIZE;
        ssize_t n = recv(sockfd, buffer, to_read, 0);
        if (n <= 0) break;
        fwrite(buffer, 1, n, fp);
        received += n;
    }

    fclose(fp);
    chmod(fullpath, permissions);
    utime(fullpath, &times);
    printf("Received and restored: %s\n", filename);
}

int main() {
    mkdir(WATCH_DIR, 0755);

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in server_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(SERVER_PORT),
    };
    inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr);
    connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr));
    printf("🔗 Connected to server.\n");

    int inotify_fd = inotify_init1(IN_NONBLOCK);
    inotify_add_watch(inotify_fd, WATCH_DIR, EVENT_MASK);

    fd_set fds;
    while (1) {
        FD_ZERO(&fds);
        FD_SET(inotify_fd, &fds);
        FD_SET(sockfd, &fds);
        int maxfd = (inotify_fd > sockfd ? inotify_fd : sockfd) + 1;

        if (select(maxfd, &fds, NULL, NULL, NULL) < 0) break;

        if (FD_ISSET(inotify_fd, &fds)) {
            char buffer[4096];
            int length = read(inotify_fd, buffer, sizeof(buffer));
            int i = 0;
            while (i < length) {
                struct inotify_event *event = (struct inotify_event *)&buffer[i];
                if (event->len && !(event->mask & IN_ISDIR)) {
                    char filepath[600];
                    snprintf(filepath, sizeof(filepath), "%s/%s", WATCH_DIR, event->name);
                    send_file(filepath, sockfd);
                }
                i += sizeof(struct inotify_event) + event->len;
            }
        }

        if (FD_ISSET(sockfd, &fds)) {
            receive_file(sockfd);
        }
    }

    close(sockfd);
    return 0;
}
