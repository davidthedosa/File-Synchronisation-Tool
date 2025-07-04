/*#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <utime.h>
#include <errno.h>

#define SERVER_IP "127.0.0.1"  // Replace with actual server IP
#define SERVER_PORT 12345
#define BUFSIZE 4096

// Helper: send exactly n bytes
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

// Send one file to the server
int send_file(const char *filepath) {
    // 1. Gather metadata
    struct stat st;
    if (stat(filepath, &st) < 0) {
        perror("stat");
        return -1;
    }

    struct utimbuf times;
    times.actime = st.st_atime;
    times.modtime = st.st_mtime;

    // 2. Open the file
    FILE *fp = fopen(filepath, "rb");
    if (!fp) {
        perror("fopen");
        return -1;
    }

    // 3. Connect to server
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) { perror("socket"); return -1; }

    struct sockaddr_in server_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(SERVER_PORT),
    };
    inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr);

    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("connect");
        fclose(fp);
        return -1;
    }

    // 4. Send metadata
    const char *filename = strrchr(filepath, '/');
    filename = filename ? filename + 1 : filepath;

    uint32_t name_len = htonl(strlen(filename));
    uint64_t filesize = htobe64(st.st_size);

    if (send_all(sockfd, &name_len, sizeof(name_len)) <= 0 ||
        send_all(sockfd, filename, strlen(filename)) <= 0 ||
        send_all(sockfd, &filesize, sizeof(filesize)) <= 0 ||
        send_all(sockfd, &st.st_mode, sizeof(st.st_mode)) <= 0 ||
        send_all(sockfd, &times, sizeof(times)) <= 0) {
        perror("send metadata");
        close(sockfd); fclose(fp);
        return -1;
    }

    // 5. Send file data
    char buffer[BUFSIZE];
    size_t n;
    while ((n = fread(buffer, 1, BUFSIZE, fp)) > 0) {
        if (send_all(sockfd, buffer, n) <= 0) {
            perror("send file data");
            close(sockfd); fclose(fp);
            return -1;
        }
    }

    printf("File '%s' sent successfully.\n", filename);

    // 6. Clean up
    fclose(fp);
    close(sockfd);
    return 0;
}

int main() {
    // Example: Send a single test file
    const char *file_to_send = "test.txt";  // Replace with path to actual file

    if (send_file(file_to_send) < 0) {
        fprintf(stderr, "Failed to send file.\n");
        return 1;
    }

    return 0;
}
*/
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <utime.h>
#include <errno.h>
#include <time.h>

#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 12345
#define BUFSIZE 4096
#define SYNC_INTERVAL 10
#define WATCH_DIR "./watch"
#define MAX_FILES 100

// Store file path and last modified time
typedef struct {
    char path[512];
    time_t last_mtime;
} FileInfo;

FileInfo file_cache[MAX_FILES];
int file_count = 0;

// Helper: send exactly n bytes
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

// Send one file to the server
int send_file(const char *filepath) {
    struct stat st;
    if (stat(filepath, &st) < 0) {
        perror("stat");
        return -1;
    }

    struct utimbuf times = { st.st_atime, st.st_mtime };

    FILE *fp = fopen(filepath, "rb");
    if (!fp) {
        perror("fopen");
        return -1;
    }

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) { perror("socket"); fclose(fp); return -1; }

    struct sockaddr_in server_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(SERVER_PORT),
    };
    inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr);

    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("connect");
        close(sockfd); fclose(fp);
        return -1;
    }

    const char *filename = strrchr(filepath, '/');
    filename = filename ? filename + 1 : filepath;

    uint32_t name_len = htonl(strlen(filename));
    uint64_t filesize = htobe64(st.st_size);

    if (send_all(sockfd, &name_len, sizeof(name_len)) <= 0 ||
        send_all(sockfd, filename, strlen(filename)) <= 0 ||
        send_all(sockfd, &filesize, sizeof(filesize)) <= 0 ||
        send_all(sockfd, &st.st_mode, sizeof(st.st_mode)) <= 0 ||
        send_all(sockfd, &times, sizeof(times)) <= 0) {
        perror("send metadata");
        close(sockfd); fclose(fp);
        return -1;
    }

    char buffer[BUFSIZE];
    size_t n;
    while ((n = fread(buffer, 1, BUFSIZE, fp)) > 0) {
        if (send_all(sockfd, buffer, n) <= 0) {
            perror("send file data");
            close(sockfd); fclose(fp);
            return -1;
        }
    }

    printf("File '%s' sent successfully.\n", filename);
    fclose(fp);
    close(sockfd);
    return 0;
}

// Check if the file is new or modified
int find_file_info(const char *path) {
    for (int i = 0; i < file_count; ++i) {
        if (strcmp(file_cache[i].path, path) == 0)
            return i;
    }
    return -1;
}

// Compare mtime and trigger sync if changed
void maybe_sync_file(const char *filepath) {
    struct stat st;
    if (stat(filepath, &st) < 0) {
        perror("stat");
        return;
    }

    int index = find_file_info(filepath);
    if (index == -1 || file_cache[index].last_mtime != st.st_mtime) {
        if (index == -1) {
            printf("[NEW] %s detected. Sending...\n", filepath);
        } else {
            printf("[MODIFIED] %s changed. Resending...\n", filepath);
        }

        if (send_file(filepath) == 0) {
            if (index == -1 && file_count < MAX_FILES) {
                strncpy(file_cache[file_count].path, filepath, sizeof(file_cache[file_count].path));
                file_cache[file_count].last_mtime = st.st_mtime;
                file_count++;
            } else if (index != -1) {
                file_cache[index].last_mtime = st.st_mtime;
            }
        }
    }
}

// Scan a directory for regular files
void sync_directory(const char *dir_path) {
    DIR *dir = opendir(dir_path);
    if (!dir) {
        perror("opendir");
        return;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_REG) {
            char filepath[512];
            snprintf(filepath, sizeof(filepath), "%s/%s", dir_path, entry->d_name);
            maybe_sync_file(filepath);
        }
    }

    closedir(dir);
}

int main() {
    printf("Watching directory: %s (every %d seconds)\n", WATCH_DIR, SYNC_INTERVAL);
    while (1) {
        sync_directory(WATCH_DIR);
        sleep(SYNC_INTERVAL);
    }
    return 0;
}

