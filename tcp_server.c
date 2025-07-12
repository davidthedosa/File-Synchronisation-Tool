// tcp_server.c
#include <time.h>
#define LOG_FILE "sync_log.txt"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>
#include <errno.h>
#include <utime.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/inotify.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define PORT 12345
#define BUFSIZE 4096
#define WATCH_DIR "./server_dir"
#define EVENT_MASK (IN_CREATE | IN_MODIFY | IN_CLOSE_WRITE | IN_MOVED_TO | IN_ATTRIB)
#define MAX_TRACKED_FILES 100
typedef struct {
    char filename[512];
    time_t received_time;
} ReceivedFile;

#define MAX_RECEIVED 100
ReceivedFile recently_received[MAX_RECEIVED];
int recent_count = 0;

void log_event(const char *direction, const char *action, const char *filename) {
    FILE *log = fopen(LOG_FILE, "a");
    if (!log) return;

    static int header_written = 0;

    if (!header_written) {
        fprintf(log, "+---------------------+----------------------+----------------------+\n");
        fprintf(log, "|      Timestamp      |       CLIENT         |       SERVER         |\n");
        fprintf(log, "+---------------------+----------------------+----------------------+\n");
        header_written = 1;
    }

    char timebuf[64];
    time_t now = time(NULL);
    strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", localtime(&now));

    char client_col[64] = "", server_col[64] = "";

    if (strcmp(direction, "CLIENT -> SERVER") == 0)
        snprintf(client_col, sizeof(client_col), "%s: %s", action, filename);
    else if (strcmp(direction, "SERVER -> CLIENT") == 0)
        snprintf(server_col, sizeof(server_col), "%s: %s", action, filename);

    fprintf(log, "| %-19s | %-20s | %-20s |\n", timebuf, client_col, server_col);
    fclose(log);
}


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

void send_file(const char *filepath, int sockfd) {
	// Prevent sync loop: ignore files received in the last 5 seconds
time_t now = time(NULL);
for (int i = 0; i < recent_count; ++i) {
    if (strcmp(recently_received[i].filename, filepath) == 0 &&
        now - recently_received[i].received_time <= 5) {
        // Recent receive — skip sending back
        return;
    }
}

    struct stat st;
    if (stat(filepath, &st) < 0) return;

    for (int i = 0; i < tracked_count; ++i) {
        if (strcmp(tracked_files[i].filename, filepath) == 0 &&
            tracked_files[i].last_sent_mtime == st.st_mtime)
            return;
    }

    FILE *fp = fopen(filepath, "rb");
    if (!fp) return;

    const char *filename = strrchr(filepath, '/');
    filename = filename ? filename + 1 : filepath;

    struct utimbuf times = { st.st_atime, st.st_mtime };
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
    log_event("SERVER -> CLIENT", "Sent", filename);
    printf("?? Sent: %s\n", filename);

    for (int i = 0; i < tracked_count; ++i) {
        if (strcmp(tracked_files[i].filename, filepath) == 0) {
            tracked_files[i].last_sent_mtime = st.st_mtime;
            return;
        }
    }
    if (tracked_count < MAX_TRACKED_FILES) {
        strcpy(tracked_files[tracked_count].filename, filepath);
        tracked_files[tracked_count].last_sent_mtime = st.st_mtime;
        tracked_count++;
    }
}

void poll_files(const char *dirpath, int sockfd) {
    DIR *dir = opendir(dirpath);
    if (!dir) return;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type != DT_REG) continue;
        if (entry->d_name[0] == '.' || strstr(entry->d_name, ".swp")) continue;

        char filepath[600];
        snprintf(filepath, sizeof(filepath), "%s/%s", dirpath, entry->d_name);

        struct stat st;
        if (stat(filepath, &st) == 0) {
            int should_send = 1;
            for (int i = 0; i < tracked_count; ++i) {
                if (strcmp(tracked_files[i].filename, filepath) == 0 &&
                    tracked_files[i].last_sent_mtime == st.st_mtime) {
                    should_send = 0;
                    break;
                }
            }
            if (should_send)
                send_file(filepath, sockfd);
        }
    }
usleep(500000);  // 0.5 seconds delay

    closedir(dir);
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
    log_event("CLIENT -> SERVER", "Received", filename);
    printf("?? Received: %s\n", filename);
    // Save to recently received cache
int updated = 0;
for (int i = 0; i < recent_count; ++i) {
    if (strcmp(recently_received[i].filename, filename) == 0) {
        recently_received[i].received_time = time(NULL);
        updated = 1;
        break;
    }
}
if (!updated && recent_count < MAX_RECEIVED) {
    strcpy(recently_received[recent_count].filename, filename);
    recently_received[recent_count].received_time = time(NULL);
    recent_count++;
}
sleep(5); // Let the system catch up before sending any updates again

}

int main() {
    mkdir(WATCH_DIR, 0755);

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in server_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(PORT),
        .sin_addr.s_addr = INADDR_ANY
    };
    bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr));
    listen(server_fd, 1);
    printf("?? Server listening on port %d...\n", PORT);

    int client_fd = accept(server_fd, NULL, NULL);
    printf("? Client connected.\n");

    int inotify_fd = inotify_init1(IN_NONBLOCK);
    inotify_add_watch(inotify_fd, WATCH_DIR, EVENT_MASK);

    fd_set fds;
    struct timeval timeout;

    while (1) {
        FD_ZERO(&fds);
        FD_SET(inotify_fd, &fds);
        FD_SET(client_fd, &fds);
        int maxfd = (inotify_fd > client_fd ? inotify_fd : client_fd) + 1;

        timeout.tv_sec = 2;
        timeout.tv_usec = 0;

        int activity = select(maxfd, &fds, NULL, NULL, &timeout);

        if (activity < 0 && errno != EINTR) break;

        if (activity == 0) {
            poll_files(WATCH_DIR, client_fd);
            continue;
        }

        if (FD_ISSET(inotify_fd, &fds)) {
            char buffer[4096];
            int length = read(inotify_fd, buffer, sizeof(buffer));
            int i = 0;
            while (i < length) {
                struct inotify_event *event = (struct inotify_event *)&buffer[i];
                if (event->len && !(event->mask & IN_ISDIR)) {
                    if (event->name[0] == '.' || strstr(event->name, ".swp")) {
                        i += sizeof(struct inotify_event) + event->len;
                        continue;
                    }
                    char filepath[600];
                    snprintf(filepath, sizeof(filepath), "%s/%s", WATCH_DIR, event->name);
                    send_file(filepath, client_fd);
                }
                i += sizeof(struct inotify_event) + event->len;
            }
        }

        if (FD_ISSET(client_fd, &fds)) {
            receive_file(client_fd);
        }
    }

    close(server_fd);
    return 0;
}
