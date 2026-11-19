/*
 * http_server.h — HyperFile 原生文件接收服务器公共接口
 * SPDX-License-Identifier: AGPL-3.0
 */
#ifndef HYPERFILE_HTTP_SERVER_H
#define HYPERFILE_HTTP_SERVER_H

#include <stddef.h>
#include <time.h>

typedef struct {
    int running;
    int port;
    char ip[64];
    char dir[512];
    long received;
    char last_file[256];
} hf_status_t;

/* 启动 server 线程；0 成功，-1 失败（已在跑视作成功返回 0） */
int hf_server_start(int port, const char* upload_dir);

/* 停止并回收线程；幂等 */
int hf_server_stop(void);

/* 拷贝当前状态 */
void hf_server_status(hf_status_t* out);

#endif
