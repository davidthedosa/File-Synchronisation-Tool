// tcp_server_threadsafe.c
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <utime.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/inotify.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <libgen.h>

#define PORT 12345
#define BUFSIZE 4096
#define WATCH_DIR "./server_dir"
#define EVENT_MASK (IN_CREATE|IN_MODIFY|IN_CLOSE_WRITE|IN_MOVED_TO|IN_MOVED_FROM|IN_ATTRIB|IN_DELETE)
#define MAX_TRACKED_FILES 256
#define MAX_RECEIVED 256
#define MAX_PATH 2048

#define MSG_TYPE_FILE_SEND   0x01
#define MSG_TYPE_FILE_DELETE 0x02
#define MSG_TYPE_FILE_RENAME 0x03

char LOG_FILE[128] = "sync.log";

pthread_mutex_t file_track_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t recent_recv_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

typedef struct { char filename[MAX_PATH]; time_t last_sent_mtime; } FileTracker;
typedef struct { char filename[MAX_PATH]; time_t received_time; } ReceivedFile;

FileTracker tracked_files[MAX_TRACKED_FILES]; int tracked_count = 0;
ReceivedFile recently_received[MAX_RECEIVED]; int recent_count = 0;

int get_relative_path(const char *full_path, char *rel_path, size_t maxlen) {
    size_t base_len = strlen(WATCH_DIR);
    if (strncmp(full_path, WATCH_DIR, base_len) != 0) return -1;
    const char *sub = full_path + base_len;
    if (*sub == '/') sub++;
    if (strlen(sub) >= maxlen) {
        fprintf(stderr, "Warning: relative path too long, truncating: %s\n", sub);
    }
    snprintf(rel_path, maxlen, "%s", sub);
    rel_path[maxlen-1] = 0;
    return 0;
}

static void ensure_dir(const char *path) {
    char tmp[MAX_PATH];
    snprintf(tmp, sizeof(tmp), "%s", path);
    tmp[sizeof(tmp)-1] = 0;
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            mkdir(tmp, 0775);
            *p = '/';
        }
    }
    mkdir(tmp, 0775);
}

void setup_log_file(const char *peer_ip) {
    mkdir("logs", 0755);
    char choice[4], namein[64];
    printf("Customize log filename? (y/n): ");
    if (!fgets(choice, sizeof(choice), stdin)) exit(1);
    if (choice[0] == 'y' || choice[0] == 'Y') {
        printf("Enter custom name (no ext): ");
        if (!fgets(namein, sizeof(namein), stdin)) exit(1);
        namein[strcspn(namein, "\n")] = 0;
        snprintf(LOG_FILE, sizeof(LOG_FILE), "logs/sync_%s.log", namein);
    }
    else {
        if (strcmp(peer_ip, "127.0.0.1") == 0)
            snprintf(LOG_FILE, sizeof(LOG_FILE), "logs/sync_self.log");
        else {
            char ipf[64];
            strcpy(ipf, peer_ip);
            for (char *p = ipf; *p; p++) if (*p == '.') *p = '_';
            snprintf(LOG_FILE, sizeof(LOG_FILE), "logs/sync_%s.log", ipf);
        }
    }
}

void log_event(const char *direction, const char *action, const char *filename, const char *filename2) {
    pthread_mutex_lock(&log_mutex);
    FILE *log = fopen(LOG_FILE, "a+");
    if (!log) { pthread_mutex_unlock(&log_mutex); return; }
    fseek(log, 0, SEEK_END);
    if (ftell(log) == 0) {
        fprintf(log, "+---------------------+----------------------+----------------------+\n");
        fprintf(log, "|      Timestamp      |       CLIENT         |       SERVER         |\n");
        fprintf(log, "+---------------------+----------------------+----------------------+\n");
    }
    char ts[64], c[256] = "", s[256] = "";
    time_t now = time(NULL);
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", localtime(&now));
    if (strstr(direction, "CLIENT"))
        snprintf(c, sizeof(c), "%s: %s %s", action, filename, filename2 ? filename2 : "");
    else
        snprintf(s, sizeof(s), "%s: %s %s", action, filename, filename2 ? filename2 : "");
    fprintf(log, "| %-19s | %-20s | %-20s |\n", ts, c, s);
    fclose(log);
    pthread_mutex_unlock(&log_mutex);
}

ssize_t send_all(int fd, const void *buf, size_t len) {
    size_t total = 0;
    while (total < len) {
        ssize_t n = send(fd, (char *)buf + total, len - total, 0);
        if (n <= 0) return n;
        total += n;
    }
    return total;
}

ssize_t recv_all(int fd, void *buf, size_t len) {
    size_t total = 0;
    while (total < len) {
        ssize_t n = recv(fd, (char *)buf + total, len - total, 0);
        if (n <= 0) return n;
        total += n;
    }
    return total;
}

void send_rename(const char *old_path, const char *new_path, int fd) {
    char old_rel[MAX_PATH], new_rel[MAX_PATH];
    if (get_relative_path(old_path, old_rel, sizeof(old_rel)) < 0) return;
    if (get_relative_path(new_path, new_rel, sizeof(new_rel)) < 0) return;
    uint8_t msg_type = MSG_TYPE_FILE_RENAME;
    uint32_t oldlen = htonl(strlen(old_rel));
    uint32_t newlen = htonl(strlen(new_rel));
    send_all(fd, &msg_type, 1);
    send_all(fd, &oldlen, sizeof(oldlen));
    send_all(fd, old_rel, strlen(old_rel));
    send_all(fd, &newlen, sizeof(newlen));
    send_all(fd, new_rel, strlen(new_rel));
    log_event("SERVER->CLIENT", "Renamed", old_rel, new_rel);
    printf("? Rename sent: %s -> %s\n", old_rel, new_rel);
}

void send_delete(const char *path, int fd) {
    char rel_path[MAX_PATH];
    if (get_relative_path(path, rel_path, sizeof(rel_path)) < 0) return;
    uint8_t msg_type = MSG_TYPE_FILE_DELETE;
    uint32_t nl = htonl(strlen(rel_path));
    send_all(fd, &msg_type, 1);
    send_all(fd, &nl, sizeof(nl));
    send_all(fd, rel_path, strlen(rel_path));
    log_event("SERVER->CLIENT", "Deleted", rel_path, NULL);
    printf("? Deleted sent: %s\n", rel_path);
}

void send_file(const char *path, int fd) {
    char rel_path[MAX_PATH];
    if (get_relative_path(path, rel_path, sizeof(rel_path)) < 0) return;
    time_t now = time(NULL);

    pthread_mutex_lock(&recent_recv_mutex);
    for (int i = 0; i < recent_count; i++) {
        if (strcmp(recently_received[i].filename, path) == 0 && now - recently_received[i].received_time <= 5) {
            pthread_mutex_unlock(&recent_recv_mutex);
            return;
        }
    }
    pthread_mutex_unlock(&recent_recv_mutex);

    struct stat st;
    if (stat(path, &st) < 0) return;

    pthread_mutex_lock(&file_track_mutex);
    for (int i = 0; i < tracked_count; i++) {
        if (strcmp(tracked_files[i].filename, path) == 0 && tracked_files[i].last_sent_mtime == st.st_mtime) {
            pthread_mutex_unlock(&file_track_mutex);
            return;
        }
    }
    pthread_mutex_unlock(&file_track_mutex);

    FILE *f = fopen(path, "rb");
    if (!f) return;

    uint8_t msg_type = MSG_TYPE_FILE_SEND;
    send_all(fd, &msg_type, 1);

    uint32_t nl = htonl(strlen(rel_path));
    uint64_t fs = htobe64(st.st_size);
    send_all(fd, &nl, sizeof(nl));
    send_all(fd, rel_path, strlen(rel_path));
    send_all(fd, &fs, sizeof(fs));
    send_all(fd, &st.st_mode, sizeof(st.st_mode));
    struct utimbuf ut = {st.st_atime, st.st_mtime};
    send_all(fd, &ut, sizeof(ut));
    char buf[BUFSIZE];
    size_t n;
    while ((n = fread(buf, 1, BUFSIZE, f)) > 0) send_all(fd, buf, n);
    fclose(f);
    log_event("SERVER->CLIENT", "Sent", rel_path, NULL);
    printf("? Sent: %s\n", rel_path);

    pthread_mutex_lock(&file_track_mutex);
    if (tracked_count < MAX_TRACKED_FILES) {
        strcpy(tracked_files[tracked_count].filename, path);
        tracked_files[tracked_count].last_sent_mtime = st.st_mtime;
        tracked_count++;
    }
    pthread_mutex_unlock(&file_track_mutex);
}

void poll_files(int fd, const char *dir, const char *prefix) {
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *e;
    char rel[MAX_PATH];
    while ((e = readdir(d))) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        char path[MAX_PATH];
        int ret = snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
        if (ret < 0 || ret >= (int)sizeof(path)) {
            fprintf(stderr, "Warning: path too long and truncated: %s/%s\n", dir, e->d_name);
            continue;
        }
        if (e->d_type == DT_DIR) {
            char nprefix[MAX_PATH];
            ret = snprintf(nprefix, sizeof(nprefix), "%s%s/", prefix, e->d_name);
            if (ret < 0 || ret >= (int)sizeof(nprefix)) {
                fprintf(stderr, "Warning: prefix too long and truncated\n");
                continue;
            }
            poll_files(fd, path, nprefix);
        } else if (e->d_type == DT_REG) {
            ret = snprintf(rel, sizeof(rel), "%s%s", prefix, e->d_name);
            if (ret < 0 || ret >= (int)sizeof(rel)) {
                fprintf(stderr, "Warning: relative path too long and truncated\n");
                continue;
            }
            char full[MAX_PATH];
            ret = snprintf(full, sizeof(full), "%s/%s", WATCH_DIR, rel);
            if (ret < 0 || ret >= (int)sizeof(full)) {
                fprintf(stderr, "Warning: full path too long and truncated\n");
                continue;
            }
            send_file(full, fd);
        }
    }
    closedir(d);
    usleep(500000);
}

void receive_delete(int fd) {
    uint32_t nl;
    if (recv_all(fd, &nl, sizeof(nl)) <= 0) return;
    nl = ntohl(nl);
    char fn[MAX_PATH];
    if (recv_all(fd, fn, nl) <= 0) return;
    fn[nl] = 0;
    char full[MAX_PATH];
    int ret = snprintf(full, sizeof(full), "%s/%s", WATCH_DIR, fn);
    if (ret < 0 || ret >= (int)sizeof(full)) {
        fprintf(stderr, "Warning: full path truncation on delete\n");
        return;
    }
    if (unlink(full) == 0) {
        log_event("CLIENT->SERVER", "Deleted", fn, NULL);
        printf("? Deleted received: %s\n", fn);
    }
    sleep(1);
}

void receive_rename(int fd) {
    uint32_t olen, nlen;
    if (recv_all(fd, &olen, sizeof(olen)) <= 0) return;
    olen = ntohl(olen);
    char oldrel[MAX_PATH];
    if (recv_all(fd, oldrel, olen) <= 0) return;
    oldrel[olen] = 0;
    if (recv_all(fd, &nlen, sizeof(nlen)) <= 0) return;
    nlen = ntohl(nlen);
    char newrel[MAX_PATH];
    if (recv_all(fd, newrel, nlen) <= 0) return;
    newrel[nlen] = 0;

    char fullold[MAX_PATH], fullnew[MAX_PATH];
    int ret1 = snprintf(fullold, sizeof(fullold), "%s/%s", WATCH_DIR, oldrel);
    int ret2 = snprintf(fullnew, sizeof(fullnew), "%s/%s", WATCH_DIR, newrel);
    if (ret1 < 0 || ret1 >= (int)sizeof(fullold) || ret2 < 0 || ret2 >= (int)sizeof(fullnew)) {
        fprintf(stderr, "Warning: full path truncation on rename\n");
        return;
    }

    char fullnew_copy[MAX_PATH];
    snprintf(fullnew_copy, sizeof(fullnew_copy), "%s", fullnew);
    ensure_dir(dirname(fullnew_copy));

    if (rename(fullold, fullnew) == 0) {
        log_event("CLIENT->SERVER", "Renamed", oldrel, newrel);
        printf("? Rename received: %s -> %s\n", oldrel, newrel);
    }
}

void receive_message(int fd) {
    uint8_t msg_type;
    ssize_t r = recv_all(fd, &msg_type, 1);
    if (r <= 0) return;
    switch (msg_type) {
    case MSG_TYPE_FILE_SEND: {
        uint32_t nl;
        if (recv_all(fd, &nl, sizeof(nl)) <= 0) return;
        nl = ntohl(nl);
        char fn[MAX_PATH];
        if (recv_all(fd, fn, nl) <= 0) return;
        fn[nl] = 0;
        uint64_t fs;
        if (recv_all(fd, &fs, sizeof(fs)) <= 0) return;
        fs = be64toh(fs);
        mode_t pm;
        if (recv_all(fd, &pm, sizeof(pm)) <= 0) return;
        struct utimbuf ut;
        if (recv_all(fd, &ut, sizeof(ut)) <= 0) return;
        char full[MAX_PATH];
        int ret = snprintf(full, sizeof(full), "%s/%s", WATCH_DIR, fn);
        if (ret < 0 || ret >= (int)sizeof(full)) {
            fprintf(stderr, "Warning: full path truncation on receive\n");
            return;
        }

        char full_copy[MAX_PATH];
        snprintf(full_copy, sizeof(full_copy), "%s", full);
        ensure_dir(dirname(full_copy));

        FILE *f = fopen(full, "wb");
        if (!f) return;
        size_t got = 0;
        char buf[BUFSIZE];
        while (got < fs) {
            size_t to_read = (fs - got < BUFSIZE ? fs - got : BUFSIZE);
            ssize_t r = recv(fd, buf, to_read, 0);
            if (r <= 0) break;
            fwrite(buf, 1, r, f);
            got += r;
        }
        fclose(f);
        chmod(full, pm);
        utime(full, &ut);
        log_event("SERVER->CLIENT", "Received", fn, NULL);
        printf("? Received: %s\n", fn);

        pthread_mutex_lock(&recent_recv_mutex);
        int found = 0;
        time_t now = time(NULL);
        for (int i = 0; i < recent_count; i++) {
            if (strcmp(recently_received[i].filename, full) == 0) {
                recently_received[i].received_time = now;
                found = 1;
                break;
            }
        }
        if (!found && recent_count < MAX_RECEIVED) {
            strcpy(recently_received[recent_count].filename, full);
            recently_received[recent_count].received_time = now;
            recent_count++;
        }
        pthread_mutex_unlock(&recent_recv_mutex);
        sleep(5);
        break;
    }
    case MSG_TYPE_FILE_DELETE:
        receive_delete(fd);
        break;
    case MSG_TYPE_FILE_RENAME:
        receive_rename(fd);
        break;
    default:
        fprintf(stderr, "Unknown message type %u\n", msg_type);
        break;
    }
}

int main(void) {
    mkdir(WATCH_DIR, 0755);

    int srv = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(PORT),
        .sin_addr.s_addr = INADDR_ANY
    };
    bind(srv, (struct sockaddr *)&addr, sizeof(addr));
    listen(srv, 1);
    printf("?? Server listening on port %d...\n", PORT);

    int cli = accept(srv, NULL, NULL);

    struct sockaddr_in pi;
    socklen_t pn = sizeof(pi);
    getpeername(cli, (struct sockaddr *)&pi, &pn);
    char peer_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &pi.sin_addr, peer_ip, sizeof(peer_ip));
    setup_log_file(peer_ip);

    int ifd = inotify_init1(IN_NONBLOCK);
    inotify_add_watch(ifd, WATCH_DIR, EVENT_MASK);

    char moved_from[MAX_PATH] = "";
    uint32_t moved_cookie = 0;

    while (1) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(ifd, &fds);
        FD_SET(cli, &fds);
        int max = (ifd > cli) ? ifd + 1 : cli + 1;
        struct timeval to = {2, 0};
        int sel = select(max, &fds, NULL, NULL, &to);
        if (sel < 0 && errno != EINTR) break;
        if (sel == 0) {
            poll_files(cli, WATCH_DIR, "");
            continue;
        }
        if (FD_ISSET(ifd, &fds)) {
            char buf[4096];
            int len = read(ifd, buf, sizeof(buf)), i = 0;
            while (i < len) {
                struct inotify_event *e = (struct inotify_event *)(buf + i);
                if (e->len && !(e->mask & IN_ISDIR) && e->name[0] != '.' && !strstr(e->name, ".swp")) {
                    char fp[MAX_PATH];
                    int ret = snprintf(fp, sizeof(fp), "%s/%s", WATCH_DIR, e->name);
                    if (ret < 0 || ret >= (int)sizeof(fp)) {
                        fprintf(stderr, "Warning: path too long in inotify event, skipping\n");
                        i += sizeof(*e) + e->len;
                        continue;
                    }
                    if ((e->mask & IN_MOVED_FROM) && e->cookie) {
                        strncpy(moved_from, fp, sizeof(moved_from));
                        moved_cookie = e->cookie;
                    } else if ((e->mask & IN_MOVED_TO) && (e->cookie && moved_cookie && e->cookie == moved_cookie)) {
                        char new_fp[MAX_PATH];
                        snprintf(new_fp, sizeof(new_fp), "%s/%s", WATCH_DIR, e->name);
                        send_rename(moved_from, new_fp, cli);
                        moved_from[0] = 0;
                        moved_cookie = 0;
                    } else if (e->mask & IN_DELETE) {
                        send_delete(fp, cli);
                    } else {
                        send_file(fp, cli);
                    }
                }
                i += sizeof(*e) + e->len;
            }
        }
        if (FD_ISSET(cli, &fds)) receive_message(cli);
    }

    close(cli);
    close(srv);
    return 0;
}
