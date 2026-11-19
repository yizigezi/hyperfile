/*
 * test_main.c — HyperFile 服务器本地测试入口（x86 直跑，不经 quickjs）
 * 用法: ./hf-server <port> <upload_dir>
 * 流程: 起服务 -> 打印地址 -> 等 SIGINT/SIGTERM 退出
 */
#include "http_server.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static volatile int g_quit = 0;
static void on_sig(int sig) { (void)sig; g_quit = 1; }

int main(int argc, char** argv)
{
    int port = argc > 1 ? atoi(argv[1]) : 8080;
    const char* dir = argc > 2 ? argv[2] : "/tmp/hyperfile-test";

    if (hf_server_start(port, dir) != 0) {
        fprintf(stderr, "start failed\n");
        return 1;
    }

    hf_status_t st;
    hf_server_status(&st);
    printf("listening http://%s:%d  upload dir: %s\n", st.ip, st.port, st.dir);

    signal(SIGINT, on_sig);
    signal(SIGTERM, on_sig);
    while (!g_quit)
        sleep(1);

    hf_server_stop();
    printf("stopped\n");
    return 0;
}
