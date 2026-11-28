/*
 * HyperFile local operations JSAPI.
 * Native, asynchronous AMR installation and shell-script execution.
 * SPDX-License-Identifier: AGPL-3.0
 */
#define _GNU_SOURCE
#include "quickjs.h"
#include "amr_info.h"
#include "amr_update.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

typedef JSModuleDef *(*LoadCModuleFunction)(JSContext *, const char *);
extern void registerCModuleLoader(const char *, LoadCModuleFunction);

static JSValue rejected(JSContext *ctx, const char *op, int err)
{
    char message[320];
    JSValue error = JS_NewError(ctx);
    snprintf(message, sizeof(message), "%s: errno=%d (%s)", op, err, strerror(err));
    JS_SetPropertyStr(ctx, error, "message", JS_NewString(ctx, message));
    return error;
}

static JSValue resolved(JSContext *ctx, JSValue value)
{
    JSValue funcs[2];
    JSValue promise = JS_NewPromiseCapability(ctx, funcs);
    JS_Call(ctx, funcs[0], JS_UNDEFINED, 1, (JSValueConst *)&value);
    JS_FreeValue(ctx, funcs[0]);
    JS_FreeValue(ctx, funcs[1]);
    JS_FreeValue(ctx, value);
    return promise;
}

static JSValue rejected_promise(JSContext *ctx, const char *op, int err)
{
    JSValue funcs[2], promise = JS_NewPromiseCapability(ctx, funcs);
    JSValue error = rejected(ctx, op, err);
    JS_Call(ctx, funcs[1], JS_UNDEFINED, 1, (JSValueConst *)&error);
    JS_FreeValue(ctx, error);
    JS_FreeValue(ctx, funcs[0]);
    JS_FreeValue(ctx, funcs[1]);
    return promise;
}

static int make_paths(pid_t pid, char *log, size_t logcap,
                      char *status, size_t statuscap)
{
    int a = snprintf(log, logcap, "/tmp/hyperfile-task-%ld.log", (long)pid);
    int b = snprintf(status, statuscap, "/tmp/hyperfile-task-%ld.status", (long)pid);
    if (a < 0 || b < 0 || (size_t)a >= logcap || (size_t)b >= statuscap) {
        errno = ENAMETOOLONG;
        return -1;
    }
    return 0;
}

static int write_status(const char *path, int code)
{
    char tmp[PATH_MAX], value[32];
    int fd, n, saved;
    n = snprintf(tmp, sizeof(tmp), "%s.tmp.%ld", path, (long)getpid());
    if (n < 0 || (size_t)n >= sizeof(tmp)) { errno = ENAMETOOLONG; return -1; }
    fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) return -1;
    n = snprintf(value, sizeof(value), "%d\n", code);
    if (write(fd, value, (size_t)n) != n || fsync(fd) != 0) {
        saved = errno ? errno : EIO; close(fd); unlink(tmp); errno = saved; return -1;
    }
    if (close(fd) != 0) { saved = errno; unlink(tmp); errno = saved; return -1; }
    if (rename(tmp, path) != 0) { saved = errno; unlink(tmp); errno = saved; return -1; }
    return 0;
}

static void task_worker(const char *path, int install, const char *log_path,
                        const char *status_path)
{
    int logfd = open(log_path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    int code = 126, status;
    pid_t child;
    struct stat st;

    if (logfd >= 0) {
        dup2(logfd, STDOUT_FILENO);
        dup2(logfd, STDERR_FILENO);
        if (logfd > STDERR_FILENO) close(logfd);
    }
    if (install == 2) {
        if (hf_amr_start_update_from_path(path) != 0) {
            dprintf(STDERR_FILENO, "HyperFile 自更新启动失败：%s\n", strerror(errno));
            goto done;
        }
        dprintf(STDOUT_FILENO, "HyperFile 自更新流程已启动\n");
        code = 0;
        goto done;
    } else if (!install) {
        if (lstat(path, &st) != 0) {
            dprintf(STDERR_FILENO, "lstat failed: %s\n", strerror(errno));
            goto done;
        }
        if (!S_ISREG(st.st_mode)) {
            dprintf(STDERR_FILENO, "拒绝执行：目标不是普通文件\n");
            goto done;
        }
        if (!(st.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH))) {
            if (chmod(path, st.st_mode | S_IXUSR | S_IXGRP | S_IXOTH) != 0) {
                dprintf(STDERR_FILENO, "chmod +x failed: %s\n", strerror(errno));
                goto done;
            }
            dprintf(STDOUT_FILENO, "[HyperFile] 已添加执行权限\n");
        }
    } else {
        if (lstat(path, &st) != 0) {
            dprintf(STDERR_FILENO, "lstat failed: %s\n", strerror(errno));
            goto done;
        }
        if (!S_ISREG(st.st_mode) || (st.st_mode & S_IFMT) == S_IFLNK) {
            dprintf(STDERR_FILENO, "拒绝安装：目标不是普通文件\n");
            goto done;
        }
    }

    child = fork();
    if (child < 0) {
        dprintf(STDERR_FILENO, "fork failed: %s\n", strerror(errno));
        goto done;
    }
    if (child == 0) {
        if (install) {
            execlp("miniapp_cli", "miniapp_cli", "install", path, (char *)NULL);
        } else {
            /*
             * Scripts without a valid executable header (often no shebang)
             * fail execve() with ENOEXEC. Interpret only that case with sh.
             * Other errors, including a missing shebang interpreter, remain
             * visible as their original errno.
             */
            execl(path, path, (char *)NULL);
            if (errno == ENOEXEC) {
                execl("/bin/sh", "sh", path, (char *)NULL);
            }
        }
        dprintf(STDERR_FILENO, "exec failed: %s\\n", strerror(errno));
        _exit(127);
    }
    while (waitpid(child, &status, 0) < 0) {
        if (errno == EINTR) continue;
        dprintf(STDERR_FILENO, "waitpid failed: %s\n", strerror(errno));
        goto done;
    }
    if (WIFEXITED(status)) code = WEXITSTATUS(status);
    else if (WIFSIGNALED(status)) code = 128 + WTERMSIG(status);
    else code = 125;
done:
    (void)write_status(status_path, code);
    _exit(0);
}

#define TASK_HANDSHAKE_MAGIC 0x4846544bU

typedef struct {
    uint32_t magic;
    int32_t error;
    pid_t worker;
} task_handshake;

static int write_full(int fd, const void *buffer, size_t length)
{
    const char *p = (const char *)buffer;
    size_t offset = 0;
    while (offset < length) {
        ssize_t n = write(fd, p + offset, length - offset);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return -1;
        offset += (size_t)n;
    }
    return 0;
}

/* Returns 1 for a complete record, 0 for clean EOF, and -1 for an error. */
static int read_full(int fd, void *buffer, size_t length)
{
    char *p = (char *)buffer;
    size_t offset = 0;
    while (offset < length) {
        ssize_t n = read(fd, p + offset, length - offset);
        if (n < 0 && errno == EINTR) continue;
        if (n < 0) return -1;
        if (n == 0) {
            if (offset == 0) return 0;
            errno = EIO;
            return -1;
        }
        offset += (size_t)n;
    }
    return 1;
}

static void send_handshake(int fd, int error, pid_t worker)
{
    task_handshake message;
    message.magic = TASK_HANDSHAKE_MAGIC;
    message.error = error;
    message.worker = worker;
    (void)write_full(fd, &message, sizeof(message));
}

static JSValue start_task(JSContext *ctx, const char *path, int install)
{
    int fds[2], read_result, saved_error;
    pid_t first;
    task_handshake message;
    char log_path[PATH_MAX], status_path[PATH_MAX], task_id[32];

    if (!path || !path[0]) return rejected_promise(ctx, "invalid path", EINVAL);
    if (pipe(fds) != 0) {
        saved_error = errno;
        return rejected_promise(ctx, "pipe", saved_error);
    }
    first = fork();
    if (first < 0) {
        saved_error = errno;
        close(fds[0]);
        close(fds[1]);
        return rejected_promise(ctx, "fork", saved_error);
    }
    if (first == 0) {
        pid_t second;
        close(fds[0]);
        if (setsid() < 0) {
            saved_error = errno;
            send_handshake(fds[1], saved_error, -1);
            _exit(1);
        }
        second = fork();
        if (second < 0) {
            saved_error = errno;
            send_handshake(fds[1], saved_error, -1);
            _exit(1);
        }
        if (second > 0) {
            send_handshake(fds[1], 0, second);
            _exit(0);
        }
        close(fds[1]);
        {
            struct sigaction sa;
            memset(&sa, 0, sizeof(sa));
            sa.sa_handler = SIG_DFL;
            sigemptyset(&sa.sa_mask);
            (void)sigaction(SIGCHLD, &sa, NULL);
        }
        if (make_paths(getpid(), log_path, sizeof(log_path), status_path, sizeof(status_path)) != 0)
            _exit(1);
        task_worker(path, install, log_path, status_path);
    }

    close(fds[1]);
    read_result = read_full(fds[0], &message, sizeof(message));
    saved_error = errno;
    close(fds[0]);
    if (read_result != 1) {
        return rejected_promise(ctx, "start task handshake",
                                read_result < 0 ? saved_error : EIO);
    }
    if (message.magic != TASK_HANDSHAKE_MAGIC) {
        return rejected_promise(ctx, "start task handshake", EPROTO);
    }
    if (message.error != 0) {
        return rejected_promise(ctx, "start task", message.error);
    }
    if (message.worker <= 1) {
        return rejected_promise(ctx, "start task handshake", EPROTO);
    }

    snprintf(task_id, sizeof(task_id), "%ld", (long)message.worker);
    {
        JSValue result = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, result, "taskId", JS_NewString(ctx, task_id));
        return resolved(ctx, result);
    }
}

static JSValue js_install_amr(JSContext *ctx, JSValueConst this_val,
                              int argc, JSValueConst *argv)
{
    const char *path;
    char *manifest = NULL;
    size_t length = 0;
    int matches;
    JSValue result;
    (void)this_val;
    if (argc < 1) return rejected_promise(ctx, "installAmr", EINVAL);
    path = JS_ToCString(ctx, argv[0]);
    if (!path) return JS_EXCEPTION;
    if (hf_amr_read_manifest(path, &manifest, &length) == 0) {
        matches = hf_amr_manifest_appid_matches(
            manifest, length, HF_HYPERFILE_APP_ID);
        free(manifest);
        if (matches) {
            JS_FreeCString(ctx, path);
            return rejected_promise(ctx, "installAmr", EPERM);
        }
    }
    result = start_task(ctx, path, 1);
    JS_FreeCString(ctx, path);
    return result;
}

static JSValue js_update_amr(JSContext *ctx, JSValueConst this_val,
                             int argc, JSValueConst *argv)
{
    const char *path;
    char *manifest = NULL;
    size_t length = 0;
    int saved_error;
    JSValue result;
    (void)this_val;
    if (argc < 1) return rejected_promise(ctx, "updateAmr", EINVAL);
    path = JS_ToCString(ctx, argv[0]);
    if (!path) return JS_EXCEPTION;
    if (hf_amr_read_manifest(path, &manifest, &length) != 0) {
        saved_error = errno;
        JS_FreeCString(ctx, path);
        return rejected_promise(ctx, "updateAmr", saved_error);
    }
    int matches = hf_amr_manifest_appid_matches(
        manifest, length, HF_HYPERFILE_APP_ID);
    free(manifest);
    if (!matches) {
        JS_FreeCString(ctx, path);
        return rejected_promise(ctx, "updateAmr", EPERM);
    }
    result = start_task(ctx, path, 2);
    JS_FreeCString(ctx, path);
    return result;
}

static JSValue js_amr_info(JSContext *ctx, JSValueConst this_val,
                           int argc, JSValueConst *argv)
{
    const char *path;
    char *manifest;
    size_t length;
    int saved_error;
    JSValue info;
    (void)this_val;
    if (argc < 1) return rejected_promise(ctx, "amrInfo", EINVAL);
    path = JS_ToCString(ctx, argv[0]);
    if (!path) return JS_EXCEPTION;
    if (hf_amr_read_manifest(path, &manifest, &length) != 0) {
        saved_error = errno;
        JS_FreeCString(ctx, path);
        return rejected_promise(ctx, "amrInfo", saved_error);
    }
    JS_FreeCString(ctx, path);
    info = JS_ParseJSON(ctx, manifest, length, "<amr-manifest>");
    free(manifest);
    if (JS_IsException(info)) return info;
    if (!JS_IsObject(info)) {
        JS_FreeValue(ctx, info);
        return rejected_promise(ctx, "amrInfo", EINVAL);
    }
    return resolved(ctx, info);
}

static JSValue js_run_script(JSContext *ctx, JSValueConst this_val,
                             int argc, JSValueConst *argv)
{
    const char *path;
    JSValue result;
    (void)this_val;
    if (argc < 1) return rejected_promise(ctx, "runScript", EINVAL);
    path = JS_ToCString(ctx, argv[0]);
    if (!path) return JS_EXCEPTION;
    result = start_task(ctx, path, 0);
    JS_FreeCString(ctx, path);
    return result;
}

static JSValue js_task_status(JSContext *ctx, JSValueConst this_val,
                              int argc, JSValueConst *argv)
{
    const char *id;
    char *end;
    long pid;
    char log_path[PATH_MAX], status_path[PATH_MAX], output[16385], codebuf[32];
    FILE *fp;
    size_t length;
    int exit_code = -1;
    JSValue obj;
    (void)this_val;
    if (argc < 1) return rejected_promise(ctx, "taskStatus", EINVAL);
    id = JS_ToCString(ctx, argv[0]);
    if (!id) return JS_EXCEPTION;
    errno = 0; pid = strtol(id, &end, 10);
    if (errno || !*id || *end || pid <= 1) {
        JS_FreeCString(ctx, id);
        return rejected_promise(ctx, "taskStatus", EINVAL);
    }
    JS_FreeCString(ctx, id);
    if (make_paths((pid_t)pid, log_path, sizeof(log_path), status_path, sizeof(status_path)) != 0)
        return rejected_promise(ctx, "taskStatus", errno);
    output[0] = '\0';
    fp = fopen(log_path, "r");
    if (fp) {
        length = fread(output, 1, sizeof(output) - 1, fp);
        output[length] = '\0';
        fclose(fp);
    }
    fp = fopen(status_path, "r");
    if (fp) {
        if (fgets(codebuf, sizeof(codebuf), fp)) exit_code = atoi(codebuf);
        fclose(fp);
    }
    obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "running", JS_NewBool(ctx, exit_code < 0));
    JS_SetPropertyStr(ctx, obj, "exitCode", exit_code < 0 ? JS_NULL : JS_NewInt32(ctx, exit_code));
    JS_SetPropertyStr(ctx, obj, "output", JS_NewString(ctx, output));
    return resolved(ctx, obj);
}

typedef struct {
    char *text;
    size_t length;
} pagination_line;

static int utf8_codepoint(const char *text, size_t length, size_t *consumed)
{
    const unsigned char *p = (const unsigned char *)text;
    if (length == 0) return -1;
    if (p[0] < 0x80) {
        *consumed = 1;
        return p[0];
    }
    if ((p[0] & 0xe0) == 0xc0 && length >= 2 && (p[1] & 0xc0) == 0x80) {
        *consumed = 2;
        return ((p[0] & 0x1f) << 6) | (p[1] & 0x3f);
    }
    if ((p[0] & 0xf0) == 0xe0 && length >= 3 &&
        (p[1] & 0xc0) == 0x80 && (p[2] & 0xc0) == 0x80) {
        *consumed = 3;
        return ((p[0] & 0x0f) << 12) | ((p[1] & 0x3f) << 6) | (p[2] & 0x3f);
    }
    if ((p[0] & 0xf8) == 0xf0 && length >= 4 &&
        (p[1] & 0xc0) == 0x80 && (p[2] & 0xc0) == 0x80 &&
        (p[3] & 0xc0) == 0x80) {
        *consumed = 4;
        return ((p[0] & 0x07) << 18) | ((p[1] & 0x3f) << 12) |
               ((p[2] & 0x3f) << 6) | (p[3] & 0x3f);
    }
    *consumed = 1;
    return p[0];
}

static int char_units(int cp)
{
    if (cp == 0x09) return 4;
    if (cp < 0x1100) return 1;
    if ((cp >= 0x2e80 && cp <= 0x303e) ||
        (cp >= 0x3041 && cp <= 0x33ff) ||
        (cp >= 0x3400 && cp <= 0x4dbf) ||
        (cp >= 0x4e00 && cp <= 0x9fff) ||
        (cp >= 0xa000 && cp <= 0xa4cf) ||
        (cp >= 0xac00 && cp <= 0xd7a3) ||
        (cp >= 0xf900 && cp <= 0xfaff) ||
        (cp >= 0xfe30 && cp <= 0xfe4f) ||
        (cp >= 0xff00 && cp <= 0xff60) ||
        (cp >= 0xffe0 && cp <= 0xffe6)) return 2;
    return 1;
}

static char *copy_bytes(const char *text, size_t length)
{
    char *copy = malloc(length + 1);
    if (!copy) return NULL;
    memcpy(copy, text, length);
    copy[length] = '\0';
    return copy;
}

static int contains_bytes(const char *text, size_t length, const char *needle)
{
    size_t needle_length = strlen(needle);
    if (needle_length > length) return 0;
    for (size_t i = 0; i <= length - needle_length; i++) {
        if (memcmp(text + i, needle, needle_length) == 0) return 1;
    }
    return 0;
}

static int is_chapter_line(const char *text, size_t length)
{
    size_t start = 0, end = length;
    while (start < end && (text[start] == ' ' || text[start] == '\t')) start++;
    while (end > start && (text[end - 1] == ' ' || text[end - 1] == '\t' ||
                           text[end - 1] == '\r')) end--;
    length = end - start;
    text += start;
    if (length == 0 || length > 50) return 0;
    if (contains_bytes(text, length, "序章") || contains_bytes(text, length, "楔子") ||
        contains_bytes(text, length, "前言") || contains_bytes(text, length, "引子") ||
        contains_bytes(text, length, "后记") || contains_bytes(text, length, "尾声") ||
        contains_bytes(text, length, "终章") || contains_bytes(text, length, "番外")) return 1;
    if (length >= 8 && (memcmp(text, "Chapter ", 8) == 0 ||
                        memcmp(text, "CHAPTER ", 8) == 0)) return 1;
    if (!contains_bytes(text, length, "第")) return 0;
    return contains_bytes(text, length, "章") || contains_bytes(text, length, "卷") ||
           contains_bytes(text, length, "回") || contains_bytes(text, length, "节") ||
           contains_bytes(text, length, "部") || contains_bytes(text, length, "篇") ||
           contains_bytes(text, length, "集");
}

static char *chapter_title(const char *text, size_t length)
{
    size_t start = 0, end = length, used = 0, count = 0;
    char *title;
    while (start < end && (text[start] == ' ' || text[start] == '\t')) start++;
    while (end > start && (text[end - 1] == ' ' || text[end - 1] == '\t' ||
                           text[end - 1] == '\r')) end--;
    title = malloc(end - start + 1);
    if (!title) return NULL;
    while (start < end && count < 24) {
        size_t consumed;
        int cp = utf8_codepoint(text + start, end - start, &consumed);
        if (cp == ' ' || cp == '\t') {
            if (used > 0 && title[used - 1] != ' ') title[used++] = ' ';
        } else {
            memcpy(title + used, text + start, consumed);
            used += consumed;
        }
        start += consumed;
        count++;
    }
    while (used > 0 && title[used - 1] == ' ') used--;
    title[used] = '\0';
    return title;
}

static JSValue js_build_pages(JSContext *ctx, JSValueConst this_val,
                              int argc, JSValueConst *argv)
{
    const char *text;
    int32_t units_per_line = 36, lines_per_page = 8;
    pagination_line *display_lines = NULL;
    size_t display_count = 0, display_capacity = 0;
    JSValue pages, chapters, result;
    uint32_t chapter_page_array_capacity = 0;
    uint32_t *chapter_pages = NULL;
    char **chapter_titles = NULL;
    size_t chapter_count = 0;
    (void)this_val;

    if (argc < 3) return JS_ThrowTypeError(ctx, "buildPages requires text, unitsPerLine, linesPerPage");
    text = JS_ToCString(ctx, argv[0]);
    if (!text) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &units_per_line, argv[1]) != 0 ||
        JS_ToInt32(ctx, &lines_per_page, argv[2]) != 0) {
        JS_FreeCString(ctx, text);
        return JS_EXCEPTION;
    }
    if (units_per_line < 8) units_per_line = 36;
    if (lines_per_page < 1) lines_per_page = 8;

    {
        const char *logical = text;
        const char *end = text + strlen(text);
        while (logical <= end) {
            const char *newline = logical;
            while (newline < end && *newline != '\n') newline++;
            size_t line_length = (size_t)(newline - logical);
            size_t line_start = 0;

            if (is_chapter_line(logical, line_length)) {
                char *title = chapter_title(logical, line_length);
                if (chapter_count >= chapter_page_array_capacity) {
                    chapter_page_array_capacity = chapter_page_array_capacity ? chapter_page_array_capacity * 2 : 16;
                    chapter_pages = realloc(chapter_pages, chapter_page_array_capacity * sizeof(*chapter_pages));
                    chapter_titles = realloc(chapter_titles, chapter_page_array_capacity * sizeof(*chapter_titles));
                }
                if (chapter_pages && chapter_titles) {
                    chapter_titles[chapter_count] = title;
                    chapter_pages[chapter_count++] = (uint32_t)display_count;
                } else {
                    free(title);
                }
            }

            do {
                size_t units = 0;
                size_t segment_start = line_start;
                size_t i = line_start;
                int has_current = 0;
                while (i < line_length) {
                    size_t consumed;
                    int cp = utf8_codepoint(logical + i, line_length - i, &consumed);
                    int width = char_units(cp);
                    if (units + width > (size_t)units_per_line && has_current) break;
                    units += width;
                    i += consumed;
                    has_current = 1;
                }
                if (i == segment_start && i < line_length) i++;
                if (display_count >= display_capacity) {
                    display_capacity = display_capacity ? display_capacity * 2 : 256;
                    display_lines = realloc(display_lines, display_capacity * sizeof(*display_lines));
                }
                if (!display_lines) {
                    JS_FreeCString(ctx, text);
                    goto pagination_error;
                }
                display_lines[display_count].length = i - segment_start;
                display_lines[display_count].text = copy_bytes(logical + segment_start, i - segment_start);
                if (!display_lines[display_count].text) {
                    JS_FreeCString(ctx, text);
                    goto pagination_error;
                }
                display_count++;
                line_start = i;
            } while (line_start < line_length);
            if (line_length == 0) {
                if (display_count >= display_capacity) {
                    display_capacity = display_capacity ? display_capacity * 2 : 256;
                    display_lines = realloc(display_lines, display_capacity * sizeof(*display_lines));
                }
                if (!display_lines) {
                    JS_FreeCString(ctx, text);
                    goto pagination_error;
                }
                display_lines[display_count].text = copy_bytes("", 0);
                display_lines[display_count].length = 0;
                if (!display_lines[display_count].text) {
                    JS_FreeCString(ctx, text);
                    goto pagination_error;
                }
                display_count++;
            }
            if (newline == end) break;
            logical = newline + 1;
        }
    }

    if (display_count == 0) {
        display_capacity = 1;
        display_lines = calloc(1, sizeof(*display_lines));
        if (!display_lines) {
            JS_FreeCString(ctx, text);
            goto pagination_error;
        }
        display_lines[0].text = copy_bytes("", 0);
        if (!display_lines[0].text) {
            JS_FreeCString(ctx, text);
            goto pagination_error;
        }
        display_count = 1;
    }

    pages = JS_NewArray(ctx);
    for (size_t start = 0; start < display_count; start += (size_t)lines_per_page) {
        size_t end = start + (size_t)lines_per_page;
        size_t length = 0;
        if (end > display_count) end = display_count;
        for (size_t i = start; i < end; i++) length += display_lines[i].length + (i + 1 < end);
        char *page = malloc(length + 1);
        if (!page) {
            JS_FreeValue(ctx, pages);
            JS_FreeCString(ctx, text);
            goto pagination_error;
        }
        size_t used = 0;
        for (size_t i = start; i < end; i++) {
            memcpy(page + used, display_lines[i].text, display_lines[i].length);
            used += display_lines[i].length;
            if (i + 1 < end) page[used++] = '\n';
        }
        page[used] = '\0';
        JS_SetPropertyUint32(ctx, pages, (uint32_t)(start / (size_t)lines_per_page),
                             JS_NewString(ctx, page));
        free(page);
    }

    chapters = JS_NewArray(ctx);
    size_t page_count = (display_count + (size_t)lines_per_page - 1) / (size_t)lines_per_page;
    uint32_t chapter_output_index = 0;
    for (size_t i = 0; i < chapter_count; i++) {
        uint32_t page = (uint32_t)(chapter_pages[i] / (size_t)lines_per_page);
        if (page >= page_count) page = (uint32_t)(page_count - 1);
        if (i > 0 && page == (uint32_t)(chapter_pages[i - 1] / (size_t)lines_per_page)) continue;
        JSValue chapter = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, chapter, "title", JS_NewString(ctx, chapter_titles[i] ? chapter_titles[i] : ""));
        JS_SetPropertyStr(ctx, chapter, "page", JS_NewUint32(ctx, page));
        JS_SetPropertyUint32(ctx, chapters, chapter_output_index++, chapter);
    }

    result = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, result, "pages", pages);
    JS_SetPropertyStr(ctx, result, "chapters", chapters);
    JS_SetPropertyStr(ctx, result, "totalLines", JS_NewUint32(ctx, (uint32_t)display_count));

    JS_FreeCString(ctx, text);
    for (size_t i = 0; i < display_count; i++) free(display_lines[i].text);
    free(display_lines);
    for (size_t i = 0; i < chapter_count; i++) free(chapter_titles[i]);
    free(chapter_titles);
    free(chapter_pages);
    return result;

pagination_error:
    if (display_lines) {
        for (size_t i = 0; i < display_count; i++) free(display_lines[i].text);
        free(display_lines);
    }
    for (size_t i = 0; i < chapter_count; i++) free(chapter_titles[i]);
    free(chapter_titles);
    free(chapter_pages);
    return JS_ThrowOutOfMemory(ctx);
}

static int module_init(JSContext *ctx, JSModuleDef *m)
{
    JSValue api = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, api, "installAmr", JS_NewCFunction(ctx, js_install_amr, "installAmr", 1));
    JS_SetPropertyStr(ctx, api, "updateAmr", JS_NewCFunction(ctx, js_update_amr, "updateAmr", 1));
    JS_SetPropertyStr(ctx, api, "amrInfo", JS_NewCFunction(ctx, js_amr_info, "amrInfo", 1));
    JS_SetPropertyStr(ctx, api, "runScript", JS_NewCFunction(ctx, js_run_script, "runScript", 1));
    JS_SetPropertyStr(ctx, api, "taskStatus", JS_NewCFunction(ctx, js_task_status, "taskStatus", 1));
    JS_SetPropertyStr(ctx, api, "buildPages", JS_NewCFunction(ctx, js_build_pages, "buildPages", 3));
    JS_SetModuleExport(ctx, m, "default", api);
    return 0;
}
static JSModuleDef *module_load(JSContext *ctx, const char *name)
{
    JSModuleDef *m = JS_NewCModule(ctx, name, module_init);
    if (m) JS_AddModuleExport(ctx, m, "default");
    return m;
}
__attribute__((visibility("default"))) void custom_init_jsapis(void)
{
    registerCModuleLoader("hyperfile_local_ops", module_load);
}
