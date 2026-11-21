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
#include "file_manager.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <limits.h>
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
"<html lang=\"zh-CN\"><head><meta charset=\"utf-8\">"
"<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
"<title>HyperFile</title><style>"
":root{color-scheme:dark;font-family:system-ui,sans-serif;background:#171717;color:#eee}"
"*{box-sizing:border-box}body{margin:0;min-height:100vh}header{height:64px;padding:0 24px;display:flex;align-items:center;justify-content:space-between;background:#222;border-bottom:1px solid #333}"
"h1{font-size:21px;margin:0;color:#ff7da8}.hint{font-size:13px;color:#999}"
"main{max-width:1100px;margin:24px auto;padding:0 18px}.toolbar{display:flex;flex-wrap:wrap;gap:9px;margin:16px 0}"
"button,.pick{border:0;border-radius:9px;padding:10px 15px;background:#333;color:#eee;font-size:14px;cursor:pointer}"
"button.primary,.pick{background:#ff7da8;color:#20151a;font-weight:600}button.danger{color:#ff8c8c}"
".crumb{padding:12px 14px;background:#242424;border-radius:9px;color:#bbb;overflow-wrap:anywhere}"
".panel{margin-top:14px;background:#222;border:1px solid #333;border-radius:12px;overflow:hidden}"
".entry{display:grid;grid-template-columns:minmax(0,1fr) 90px 150px;gap:12px;align-items:center;padding:13px 16px;border-bottom:1px solid #333}"
".entry:last-child{border-bottom:0}.name{overflow:hidden;text-overflow:ellipsis;white-space:nowrap;cursor:pointer}"
".folder{color:#ffb6ce}.meta{font-size:12px;color:#888;text-align:right}.actions{display:flex;justify-content:flex-end;gap:6px}.actions button{padding:7px 9px;font-size:12px}"
"input[type=file]{display:none}.empty{padding:32px;text-align:center;color:#888}.status{min-height:24px;color:#aaa;font-size:13px;margin:12px 2px}"
"@media(max-width:620px){main{padding:0 10px}.entry{grid-template-columns:minmax(0,1fr) 64px}.meta{display:none}.actions{grid-column:1/-1;justify-content:flex-start}}"
"</style></head><body><header><h1>HyperFile</h1><span class=\"hint\">设备文件管理</span></header>"
"<main><div class=\"crumb\" id=\"crumb\">/</div><div class=\"toolbar\">"
"<label class=\"pick\">上传文件<input id=\"upload\" type=\"file\" multiple></label>"
"<button class=\"primary\" id=\"newdir\">新建文件夹</button><button id=\"paste\">粘贴</button>"
"<button id=\"up\">上一级</button><button id=\"refresh\">刷新</button></div><div class=\"status\" id=\"status\"></div>"
"<section class=\"panel\" id=\"list\"></section></main><script>"
"var current='',clipboard=null;var list=document.getElementById('list'),statusEl=document.getElementById('status');"
"function enc(v){return encodeURIComponent(v)}function join(a,b){return a?a+'/'+b:b}"
"function say(s){statusEl.textContent=s}function api(url,method){return fetch(url,{method:method||'GET'}).then(function(r){return r.json().then(function(j){if(!r.ok||j.ok===false)throw Error(j.error||('HTTP '+r.status));return j})})}"
"function load(){document.getElementById('crumb').textContent='/'+current;list.innerHTML='';"
"api('/api/files?path='+enc(current)).then(function(data){var entries=data.entries||[];entries.sort(function(a,b){return (b.directory-a.directory)||a.name.localeCompare(b.name)});"
"if(!entries.length){list.innerHTML='<div class=empty>此文件夹为空</div>';return}"
"entries.forEach(function(e){var rel=join(current,e.name),row=document.createElement('div');row.className='entry';"
"var name=document.createElement('div');name.className='name'+(e.directory?' folder':'');name.textContent=(e.directory?'▸ ':'')+e.name;"
"name.onclick=function(){if(e.directory){current=rel;load()}};row.appendChild(name);"
"var meta=document.createElement('div');meta.className='meta';meta.textContent=e.directory?'文件夹':formatSize(e.size);row.appendChild(meta);"
"var actions=document.createElement('div');actions.className='actions';"
"var cp=document.createElement('button');cp.textContent='复制';cp.onclick=function(){clipboard=rel;say('已复制：'+rel)};actions.appendChild(cp);"
"var del=document.createElement('button');del.className='danger';del.textContent='删除';del.onclick=function(){if(!confirm('确定删除 '+e.name+'？'))return;api('/api/file?path='+enc(rel),'DELETE').then(function(){say('已删除');load()}).catch(function(x){say('删除失败：'+x.message)})};actions.appendChild(del);"
"row.appendChild(actions);list.appendChild(row)});}).catch(function(e){say('读取失败：'+e.message)})}"
"function formatSize(n){if(n<1024)return n+' B';if(n<1048576)return (n/1024).toFixed(1)+' KB';return (n/1048576).toFixed(1)+' MB'}"
"document.getElementById('refresh').onclick=load;"
"document.getElementById('up').onclick=function(){if(!current)return;current=current.split('/').slice(0,-1).join('/');load()};"
"document.getElementById('newdir').onclick=function(){var n=prompt('文件夹名称');if(!n)return;api('/api/mkdir?path='+enc(join(current,n)),'POST').then(function(){say('文件夹已创建');load()}).catch(function(e){say('创建失败：'+e.message)})};"
"document.getElementById('paste').onclick=function(){if(!clipboard){say('剪贴板为空');return}var name=clipboard.split('/').pop(),dest=join(current,name);"
"api('/api/copy?src='+enc(clipboard)+'&dest='+enc(dest),'POST').then(function(){say('已粘贴：'+dest);load()}).catch(function(e){say('粘贴失败：'+e.message)})};"
"document.getElementById('upload').onchange=function(){var files=Array.prototype.slice.call(this.files);var i=0;function next(){if(i>=files.length){say('上传完成');load();return}var file=files[i++],xhr=new XMLHttpRequest();say('正在上传：'+file.name);"
"xhr.open('PUT','/upload?path='+enc(current)+'&name='+enc(file.name));xhr.onload=function(){if(xhr.status===200)next();else say('上传失败：'+file.name)};xhr.onerror=function(){say('网络错误：'+file.name)};xhr.send(file)}next();this.value=''};"
"load();</script></body></html>\n";


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
static int hf_read_headers(int fd, char* buf, size_t buflen, size_t* total_read)
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
    char relative_dir[512] = "";
    char upload_dir[PATH_MAX];
    hf_sanitize_name(name, sizeof(name), raw_name);
    if (hf_get_query(path, "path", relative_dir, sizeof(relative_dir))) {
        char decoded_dir[512];
        hf_url_decode(decoded_dir, sizeof(decoded_dir), relative_dir);
        snprintf(relative_dir, sizeof(relative_dir), "%s", decoded_dir);
    }
    if (hf_fm_resolve_dir(g_status.dir, relative_dir, upload_dir, sizeof(upload_dir)) != 0) {
        hf_respond_json(fd, 400, "{\\"ok\\":false,\\"error\\":\\"invalid upload directory\\"}");
        return;
    }

    char filepath[PATH_MAX];
    if (snprintf(filepath, sizeof(filepath), "%s/%s", upload_dir, name) >= (int)sizeof(filepath)) {
        hf_respond_json(fd, 400, "{\\"ok\\":false,\\"error\\":\\"file path too long\\"}");
        return;
    }

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
    size_t total_read = 0;\n    int hlen = hf_read_headers(fd, headers, sizeof(headers), &total_read);
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
    } else if (strcmp(method, "GET") == 0 && strncmp(path, "/api/files", 10) == 0) {
        char rel[512] = "", decoded[512], json[32768];
        hf_get_query(path, "path", rel, sizeof(rel));
        hf_url_decode(decoded, sizeof(decoded), rel);
        if (hf_fm_list(g_status.dir, decoded, json, sizeof(json)) == 0)
            hf_respond_json(fd, 200, json);
        else
            hf_respond_json(fd, 400, "{\\"ok\\":false,\\"error\\":\\"cannot list directory\\"}");
    } else if (strcmp(method, "POST") == 0 && strncmp(path, "/api/mkdir", 10) == 0) {
        char rel[512] = "", decoded[512];
        hf_get_query(path, "path", rel, sizeof(rel));
        hf_url_decode(decoded, sizeof(decoded), rel);
        if (hf_fm_mkdir(g_status.dir, decoded) == 0)
            hf_respond_json(fd, 200, "{\\"ok\\":true}");
        else
            hf_respond_json(fd, 400, "{\\"ok\\":false,\\"error\\":\\"cannot create directory\\"}");
    } else if (strcmp(method, "DELETE") == 0 && strncmp(path, "/api/file", 9) == 0) {
        char rel[512] = "", decoded[512];
        hf_get_query(path, "path", rel, sizeof(rel));
        hf_url_decode(decoded, sizeof(decoded), rel);
        if (hf_fm_delete(g_status.dir, decoded) == 0)
            hf_respond_json(fd, 200, "{\\"ok\\":true}");
        else
            hf_respond_json(fd, 400, "{\\"ok\\":false,\\"error\\":\\"cannot delete item\\"}");
    } else if (strcmp(method, "POST") == 0 && strncmp(path, "/api/copy", 9) == 0) {
        char src[512] = "", dst[512] = "", decoded_src[512], decoded_dst[512];
        hf_get_query(path, "src", src, sizeof(src));
        hf_get_query(path, "dest", dst, sizeof(dst));
        hf_url_decode(decoded_src, sizeof(decoded_src), src);
        hf_url_decode(decoded_dst, sizeof(decoded_dst), dst);
        if (hf_fm_copy(g_status.dir, decoded_src, decoded_dst) == 0)
            hf_respond_json(fd, 200, "{\\"ok\\":true}");
        else
            hf_respond_json(fd, 400, "{\\"ok\\":false,\\"error\\":\\"cannot copy item (destination may exist)\\"}");
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
