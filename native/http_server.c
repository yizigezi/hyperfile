/*
 * http_server.c — HyperFile 最小 HTTP 文件接收服务器（纯 C，无 quickjs 依赖）
 *
 * 功能面（刻意最小）：
 *   GET  /            -> 内置上传网页（web_upload_html）
 *   GET  /status      -> JSON 状态
 *   PUT|POST /upload  -> 流式接收 body 写入 upload 目录；文件名取
 *                        query ?name= 或 X-File-Name 头（URL 解码 + basename 消毒）
 *   其他              -> 404
 *
 * 并发模型：单 server 线程串行处理连接（手机单人传文件场景够用，免锁）。
 * 停止：hf_server_stop() 置停止位并 close 监听 fd，accept 报错退出后 join。
 *
 * 本文件可脱离 quickjs 独立编译测试（见 test_main.c / tools/test-server.sh）。
 *
 * SPDX-License-Identifier: AGPL-3.0
 */

#include "http_server.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <strings.h>

/* ---------------- 内置上传网页（单文件，无外部依赖） ---------------- */

static const char WEB_UPLOAD_HTML[] =
"<!DOCTYPE html>\n"
"<html lang=\"zh-CN\">\n"
"<head>\n"
"<meta charset=\"utf-8\">\n"
"<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">\n"
"<title>HyperFile</title>\n"
"<style>\n"
"body{margin:0;font-family:system-ui,sans-serif;background:#1c1c1c;color:#fff;display:flex;flex-direction:column;align-items:center;min-height:100vh}\n"
"h1{font-size:22px;margin:28px 0 6px}\n"
".sub{color:#888;font-size:14px;margin-bottom:24px}\n"
".card{background:#242424;border-radius:14px;width:86%;max-width:480px;padding:18px;margin:10px 0}\n"
"label.pick{display:block;text-align:center;border:2px dashed #ff7da8;border-radius:12px;padding:26px 10px;font-size:17px;cursor:pointer}\n"
"input[type=file]{display:none}\n"
".row{margin-top:14px}\n"
".name{font-size:14px;display:flex;justify-content:space-between}\n"
".bar{height:8px;background:#3a3a3a;border-radius:4px;margin-top:6px;overflow:hidden}\n"
".fill{height:100%;width:0;background:#ff7da8;border-radius:4px}\n"
".ok{color:#7dd87d}.err{color:#ff7d7d}\n"
"</style>\n"
"</head>\n"
"<body>\n"
"<h1>HyperFile</h1>\n"
"<div class=\"sub\">选择文件，传到词典笔</div>\n"
"<div class=\"card\">\n"
"<label class=\"pick\">选择文件<input id=\"f\" type=\"file\" multiple></label>\n"
"<div id=\"list\"></div>\n"
"</div>\n"
"<script>\n"
"var list=document.getElementById('list');\n"
"document.getElementById('f').addEventListener('change',function(){\n"
"  Array.prototype.forEach.call(this.files,function(file){upload(file)});\n"
"  this.value='';\n"
"});\n"
"function upload(file){\n"
"  var row=document.createElement('div');row.className='row';\n"
"  var name=document.createElement('div');name.className='name';\n"
"  var left=document.createElement('span');left.textContent=file.name;\n"
"  var right=document.createElement('span');right.textContent='0%';\n"
"  name.appendChild(left);name.appendChild(right);\n"
"  var bar=document.createElement('div');bar.className='bar';\n"
"  var fill=document.createElement('div');fill.className='fill';bar.appendChild(fill);\n"
"  row.appendChild(name);row.appendChild(bar);list.appendChild(row);\n"
"  var xhr=new XMLHttpRequest();\n"
"  xhr.open('PUT','/upload?name='+encodeURIComponent(file.name),true);\n"
"  xhr.upload.onprogress=function(e){\n"
"    if(e.lengthComputable){\n"
"      var p=Math.round(e.loaded/e.total*100);fill.style.width=p+'%';right.textContent=p+'%';\n"
"    }\n"
"  };\n"
"  xhr.onload=function(){\n"
"    if(xhr.status===200){fill.style.width='100%';right.textContent='完成';right.className='ok';}\n"
"    else{right.textContent='失败';right.className='err';}\n"
"  };\n"
"  xhr.onerror=function(){right.textContent='失败';right.className='err';};\n"
"  xhr.send(file);\n"
"}\n"
"</script>\n"
"</body>\n"
"</html>\n";

/* ---------------- 全局状态（单 server 线程 + 主线程读，够用） ---------------- */

static hf_status_t g_status;
static int g_listen_fd = -1;
static volatile int g_stop = 0;
static pthread_t g_thread;
static int g_thread_started = 0;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

/* ---------------- 工具 ---------------- */

/* 取非回环 IPv4，wlan0 优先 */
static void hf_detect_ip(char* out, size_t outlen)
{
    out[0] = '\0';
    struct ifaddrs* ifap = NULL;
    if (getifaddrs(&ifap) != 0)
        return;

    char fallback[64] = "";
    for (struct ifaddrs* p = ifap; p; p = p->ifa_next) {
        if (!p->ifa_addr || p->ifa_addr->sa_family != AF_INET)
            continue;
        struct sockaddr_in* sin = (struct sockaddr_in*)p->ifa_addr;
        char buf[64];
        if (!inet_ntop(AF_INET, &sin->sin_addr, buf, sizeof(buf)))
            continue;
        if (strcmp(buf, "127.0.0.1") == 0)
            continue;
        if (p->ifa_name && (strstr(p->ifa_name, "wlan") || strstr(p->ifa_name, "eth") ||
                            strstr(p->ifa_name, "en"))) {
            snprintf(out, outlen, "%s", buf);
            break;
        }
        if (!fallback[0])
            snprintf(fallback, sizeof(fallback), "%s", buf);
    }
    freeifaddrs(ifap);
    if (!out[0])
        snprintf(out, outlen, "%s", fallback);
}

/* URL 解码（%XX 与 + -> 空格），就地写 out，返回 out */
static char* hf_url_decode(char* out, size_t outlen, const char* in)
{
    size_t o = 0;
    for (size_t i = 0; in[i] && o + 1 < outlen; i++) {
        if (in[i] == '%' && in[i + 1] && in[i + 2]) {
            char hex[3] = { in[i + 1], in[i + 2], 0 };
            out[o++] = (char)strtol(hex, NULL, 16);
            i += 2;
        } else if (in[i] == '+') {
            out[o++] = ' ';
        } else {
            out[o++] = in[i];
        }
    }
    out[o] = '\0';
    return out;
}

/* 文件名消毒：URL 解码 -> basename -> 去控制字符/穿越；空则按时间戳命名 */
static void hf_sanitize_name(char* out, size_t outlen, const char* raw)
{
    char decoded[512];
    hf_url_decode(decoded, sizeof(decoded), raw ? raw : "");

    const char* base = decoded;
    for (const char* p = decoded; *p; p++) {
        if (*p == '/' || *p == '\\')
            base = p + 1;
    }

    size_t o = 0;
    for (size_t i = 0; base[i] && o + 1 < outlen; i++) {
        unsigned char c = (unsigned char)base[i];
        if (c < 0x20 || c == 0x7f)
            continue;
        out[o++] = (char)c;
    }
    out[o] = '\0';

    /* 纯点名（"." ".."）视为非法 */
    int all_dots = out[0] != '\0';
    for (size_t i = 0; out[i]; i++) {
        if (out[i] != '.') {
            all_dots = 0;
            break;
        }
    }
    if (all_dots)
        out[0] = '\0';

    if (!out[0])
        snprintf(out, outlen, "upload-%ld", (long)time(NULL));
}

/* ---------------- HTTP 读写小工具 ---------------- */

/* 读到 \r\n\r\n；返回头长度（含空行），出错 -1，超上限 -2 */
static int hf_read_headers(int fd, char* buf, size_t buflen)
{
    size_t used = 0;
    while (used + 1 < buflen) {
        ssize_t n = recv(fd, buf + used, buflen - 1 - used, 0);
        if (n <= 0)
            return -1;
        used += (size_t)n;
        buf[used] = '\0';
        const char* end = strstr(buf, "\r\n\r\n");
        if (end)
            return (int)(end - buf) + 4; /* 头长度（含空行），body 还在 buf 里但不算入 */
    }
    return -2;
}

static void hf_send_all(int fd, const char* data, size_t len)
{
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(fd, data + sent, len - sent, 0);
        if (n <= 0)
            return;
        sent += (size_t)n;
    }
}

static void hf_respond(int fd, int code, const char* reason, const char* ctype,
                       const char* body, size_t bodylen)
{
    char head[256];
    int hlen = snprintf(head, sizeof(head),
                        "HTTP/1.1 %d %s\r\n"
                        "Content-Type: %s\r\n"
                        "Content-Length: %zu\r\n"
                        "Connection: close\r\n"
                        "Access-Control-Allow-Origin: *\r\n"
                        "\r\n",
                        code, reason, ctype, bodylen);
    hf_send_all(fd, head, (size_t)hlen);
    if (bodylen)
        hf_send_all(fd, body, bodylen);
}

static void hf_respond_json(int fd, int code, const char* json)
{
    hf_respond(fd, code, code == 200 ? "OK" : "Error", "application/json",
               json, strlen(json));
}

/* 从头部里取一个字段值（大小写不敏感），找到返回 1 */
static int hf_get_header(const char* headers, const char* name, char* out, size_t outlen)
{
    size_t namelen = strlen(name);
    for (const char* p = headers; *p;) {
        const char* eol = strstr(p, "\r\n");
        size_t linelen = eol ? (size_t)(eol - p) : strlen(p);
        if (linelen > namelen && strncasecmp(p, name, namelen) == 0 && p[namelen] == ':') {
            const char* v = p + namelen + 1;
            while (*v == ' ' || *v == '\t')
                v++;
            size_t vlen = linelen - (size_t)(v - p);
            if (vlen >= outlen)
                vlen = outlen - 1;
            memcpy(out, v, vlen);
            out[vlen] = '\0';
            /* 去尾部空白 */
            while (vlen && (out[vlen - 1] == ' ' || out[vlen - 1] == '\t' ||
                            out[vlen - 1] == '\r'))
                out[--vlen] = '\0';
            return 1;
        }
        p = eol ? eol + 2 : p + linelen;
    }
    return 0;
}

/* 从请求路径里取 query 参数值 */
static int hf_get_query(const char* path, const char* name, char* out, size_t outlen)
{
    const char* q = strchr(path, '?');
    if (!q)
        return 0;
    q++;
    size_t namelen = strlen(name);
    while (*q) {
        if (strncmp(q, name, namelen) == 0 && q[namelen] == '=') {
            const char* v = q + namelen + 1;
            size_t vlen = 0;
            while (v[vlen] && v[vlen] != '&')
                vlen++;
            if (vlen >= outlen)
                vlen = outlen - 1;
            memcpy(out, v, vlen);
            out[vlen] = '\0';
            return 1;
        }
        while (*q && *q != '&')
            q++;
        if (*q == '&')
            q++;
    }
    return 0;
}

/* ---------------- 连接处理 ---------------- */

static void hf_handle_upload(int fd, const char* path, const char* headers,
                             const char* body_prefix, size_t prefix_len, long content_len)
{
    if (content_len < 0) {
        hf_respond_json(fd, 411, "{\"ok\":false,\"error\":\"need Content-Length\"}");
        return;
    }

    char raw_name[512] = "";
    if (!hf_get_query(path, "name", raw_name, sizeof(raw_name)))
        hf_get_header(headers, "X-File-Name", raw_name, sizeof(raw_name));

    char name[512];
    hf_sanitize_name(name, sizeof(name), raw_name);

    char filepath[1024];
    snprintf(filepath, sizeof(filepath), "%s/%s", g_status.dir, name);

    FILE* fp = fopen(filepath, "wb");
    if (!fp) {
        hf_respond_json(fd, 500, "{\"ok\":false,\"error\":\"open file failed\"}");
        return;
    }

    /* 先写已读到的前缀，再把剩下的 body 收完 */
    long remain = content_len;
    if (prefix_len) {
        size_t w = prefix_len < (size_t)remain ? prefix_len : (size_t)remain;
        fwrite(body_prefix, 1, w, fp);
        remain -= (long)w;
    }
    char buf[16384];
    while (remain > 0) {
        size_t want = remain > (long)sizeof(buf) ? sizeof(buf) : (size_t)remain;
        ssize_t n = recv(fd, buf, want, 0);
        if (n <= 0)
            break;
        fwrite(buf, 1, (size_t)n, fp);
        remain -= n;
    }
    fclose(fp);

    if (remain > 0) {
        /* 没收完，当失败，删残文件 */
        unlink(filepath);
        hf_respond_json(fd, 500, "{\"ok\":false,\"error\":\"transfer interrupted\"}");
        return;
    }

    pthread_mutex_lock(&g_lock);
    g_status.received += 1;
    snprintf(g_status.last_file, sizeof(g_status.last_file), "%.255s", name);
    pthread_mutex_unlock(&g_lock);

    char json[640];
    snprintf(json, sizeof(json), "{\"ok\":true,\"name\":\"%s\",\"size\":%ld}", name, content_len);
    hf_respond_json(fd, 200, json);
}

static void hf_handle_conn(int fd)
{
    char headers[16384];
    int hlen = hf_read_headers(fd, headers, sizeof(headers));
    if (hlen <= 0) {
        close(fd);
        return;
    }

    /* 请求行：METHOD SP PATH SP HTTP/x.x */
    char method[16] = "", path[1024] = "";
    sscanf(headers, "%15s %1023s", method, path);

    /* body 可能已经随头部一起读进来了，记下前缀 */
    const char* body_prefix = headers + hlen;
    /* headers 缓冲里尾部还有 recv 多读的 body 字节，重算长度 */
    size_t total_read = strlen(headers);
    size_t prefix_len = total_read > (size_t)hlen ? total_read - (size_t)hlen : 0;

    char cl_buf[32] = "";
    long content_len = -1;
    if (hf_get_header(headers, "Content-Length", cl_buf, sizeof(cl_buf)))
        content_len = strtol(cl_buf, NULL, 10);

    if (strcmp(method, "GET") == 0 && strcmp(path, "/") == 0) {
        hf_respond(fd, 200, "OK", "text/html; charset=utf-8",
                   WEB_UPLOAD_HTML, sizeof(WEB_UPLOAD_HTML) - 1);
    } else if (strcmp(method, "GET") == 0 && strncmp(path, "/status", 7) == 0) {
        char json[1280];
        pthread_mutex_lock(&g_lock);
        snprintf(json, sizeof(json),
                 "{\"ok\":true,\"running\":true,\"port\":%d,\"ip\":\"%s\","
                 "\"dir\":\"%s\",\"received\":%ld,\"lastFile\":\"%s\"}",
                 g_status.port, g_status.ip, g_status.dir,
                 g_status.received, g_status.last_file);
        pthread_mutex_unlock(&g_lock);
        hf_respond_json(fd, 200, json);
    } else if ((strcmp(method, "PUT") == 0 || strcmp(method, "POST") == 0) &&
               strncmp(path, "/upload", 7) == 0) {
        hf_handle_upload(fd, path, headers, body_prefix, prefix_len, content_len);
    } else {
        hf_respond_json(fd, 404, "{\"ok\":false,\"error\":\"not found\"}");
    }

    close(fd);
}

/* ---------------- 服务器循环与生命周期 ---------------- */

static void* hf_server_thread(void* arg)
{
    (void)arg;
    while (!g_stop) {
        int fd = accept(g_listen_fd, NULL, NULL);
        if (fd < 0) {
            if (g_stop)
                break;
            if (errno == EINTR)
                continue;
            /* 监听套接字异常：退避后继续 */
            usleep(200 * 1000);
            continue;
        }
        /* 防卡死：收发各 10 分钟超时（大文件慢传也够） */
        struct timeval tv = { 600, 0 };
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
        int nodelay = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));

        hf_handle_conn(fd); /* 内部会 close(fd) */
    }
    return NULL;
}

int hf_server_start(int port, const char* upload_dir)
{
    pthread_mutex_lock(&g_lock);
    if (g_thread_started) {
        pthread_mutex_unlock(&g_lock);
        return 0; /* 已在跑 */
    }

    snprintf(g_status.dir, sizeof(g_status.dir), "%s", upload_dir);
    g_status.port = port;
    g_status.received = 0;
    g_status.last_file[0] = '\0';
    hf_detect_ip(g_status.ip, sizeof(g_status.ip));
    g_stop = 0;

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        pthread_mutex_unlock(&g_lock);
        return -1;
    }
    int reuse = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((uint16_t)port);
    if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) != 0 || listen(fd, 8) != 0) {
        close(fd);
        pthread_mutex_unlock(&g_lock);
        return -1;
    }

    g_listen_fd = fd;
    if (pthread_create(&g_thread, NULL, hf_server_thread, NULL) != 0) {
        close(fd);
        g_listen_fd = -1;
        pthread_mutex_unlock(&g_lock);
        return -1;
    }
    g_thread_started = 1;
    g_status.running = 1;
    pthread_mutex_unlock(&g_lock);
    return 0;
}

int hf_server_stop(void)
{
    pthread_mutex_lock(&g_lock);
    if (!g_thread_started) {
        pthread_mutex_unlock(&g_lock);
        return 0;
    }
    g_stop = 1;
    if (g_listen_fd >= 0) {
        shutdown(g_listen_fd, SHUT_RDWR); /* wake a thread blocked in accept */
        close(g_listen_fd);
        g_listen_fd = -1;
    }
    pthread_mutex_unlock(&g_lock);

    pthread_join(g_thread, NULL);

    pthread_mutex_lock(&g_lock);
    g_thread_started = 0;
    g_status.running = 0;
    pthread_mutex_unlock(&g_lock);
    return 0;
}

void hf_server_status(hf_status_t* out)
{
    pthread_mutex_lock(&g_lock);
    *out = g_status;
    pthread_mutex_unlock(&g_lock);
}
