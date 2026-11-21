/*
 * hyperfile-httpd: standalone HTTP service process.
 * SPDX-License-Identifier: AGPL-3.0
 */
#define _GNU_SOURCE
#include "http_server.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <time.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#define CONTROL_SOCKET "/userdisk/HyperFile/httpd.sock"
#define CONTROL_BACKLOG 4

static volatile sig_atomic_t g_quit;

static void on_signal(int sig) { (void)sig; g_quit = 1; }

#define UPDATE_TOKEN_FILE "/tmp/hyperfile-update.token"

static int write_update_token(void)
{
    uint32_t value = (uint32_t)time(NULL) ^ (uint32_t)getpid();
    int random_fd = open("/dev/urandom", O_RDONLY);
    if (random_fd >= 0) {
        uint32_t random_value;
        if (read(random_fd, &random_value, sizeof(random_value)) == (ssize_t)sizeof(random_value))
            value = random_value;
        close(random_fd);
    }
    char token[16];
    int len = snprintf(token, sizeof(token), "%09u", value % 1000000000U);
    int fd = open(UPDATE_TOKEN_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) return -1;
    ssize_t written = write(fd, token, (size_t)len);
    int result = written == len && fsync(fd) == 0 ? 0 : -1;
    close(fd);
    return result;
}

static void make_status_json(char *out, size_t size)
{
    hf_status_t st;
    hf_server_status(&st);
    snprintf(out, size,
        "{\"ok\":true,\"running\":%s,\"port\":%d,\"ip\":\"%s\","
        "\"dir\":\"%s\",\"received\":%ld,\"lastFile\":\"%s\"}",
        st.running ? "true" : "false", st.port, st.ip, st.dir,
        st.received, st.last_file);
}

int main(int argc, char **argv)
{
    int port = argc > 1 ? atoi(argv[1]) : 8080;
    const char *upload_dir = argc > 2 ? argv[2] : "/userdisk/Favorite/HyperFile";
    int control_fd = -1;
    struct sockaddr_un addr;

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGPIPE, SIG_IGN);

    if (hf_server_start(port, upload_dir) != 0)
        return 4;
    if (write_update_token() != 0) {
        hf_server_stop();
        return 5;
    }

    unlink(CONTROL_SOCKET);
    control_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (control_fd < 0) {
        hf_server_stop();
        return 2;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", CONTROL_SOCKET);
    if (bind(control_fd, (struct sockaddr *)&addr, sizeof(addr)) != 0 ||
        listen(control_fd, CONTROL_BACKLOG) != 0) {
        close(control_fd);
        unlink(CONTROL_SOCKET);
        hf_server_stop();
        return 3;
    }

    while (!g_quit) {
        int client = accept(control_fd, NULL, NULL);
        char command[32] = {0};
        ssize_t n;
        if (client < 0) {
            if (errno == EINTR) continue;
            break;
        }
        n = recv(client, command, sizeof(command) - 1, 0);
        if (n > 0 && strncmp(command, "STATUS", 6) == 0) {
            char json[1400];
            make_status_json(json, sizeof(json));
            (void)send(client, json, strlen(json), 0);
        } else if (n > 0 && strncmp(command, "STOP", 4) == 0) {
            (void)send(client, "{\"ok\":true,\"stopping\":true}", sizeof("{\"ok\":true,\"stopping\":true}") - 1, 0);
            g_quit = 1;
        } else {
            (void)send(client, "{\"ok\":false,\"error\":\"bad command\"}", sizeof("{\"ok\":false,\"error\":\"bad command\"}") - 1, 0);
        }
        close(client);
    }

    hf_server_stop();
    close(control_fd);
    unlink(CONTROL_SOCKET);
    unlink(UPDATE_TOKEN_FILE);
    return 0;
}
