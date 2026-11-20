/*
 * hyperfile-httpd: standalone HTTP service process.
 * SPDX-License-Identifier: AGPL-3.0
 */
#define _GNU_SOURCE
#include "http_server.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#define CONTROL_SOCKET "/userdisk/hyperfile/httpd.sock"
#define CONTROL_BACKLOG 4

static volatile sig_atomic_t g_quit;

static void on_signal(int sig) { (void)sig; g_quit = 1; }

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
    return 0;
}
