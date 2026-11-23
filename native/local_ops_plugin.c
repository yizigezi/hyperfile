/*
 * HyperFile local operations JSAPI.
 * Native, asynchronous AMR installation and shell-script execution.
 * SPDX-License-Identifier: AGPL-3.0
 */
#define _GNU_SOURCE
#include "quickjs.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
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
    if (!install) {
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
        if (install) execlp("miniapp_cli", "miniapp_cli", "install", path, (char *)NULL);
        else execl(path, path, (char *)NULL);
        dprintf(STDERR_FILENO, "exec failed: %s\n", strerror(errno));
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

static JSValue start_task(JSContext *ctx, const char *path, int install)
{
    int fds[2], polled_error = 0;
    pid_t first, worker = -1;
    ssize_t n;
    char log_path[PATH_MAX], status_path[PATH_MAX];
    if (!path || !path[0]) return rejected_promise(ctx, "invalid path", EINVAL);
    if (pipe(fds) != 0) return rejected_promise(ctx, "pipe", errno);
    first = fork();
    if (first < 0) {
        int saved = errno; close(fds[0]); close(fds[1]);
        return rejected_promise(ctx, "fork", saved);
    }
    if (first == 0) {
        pid_t second;
        close(fds[0]);
        if (setsid() < 0) { polled_error = errno; (void)write(fds[1], &polled_error, sizeof(polled_error)); _exit(1); }
        second = fork();
        if (second < 0) { polled_error = errno; (void)write(fds[1], &polled_error, sizeof(polled_error)); _exit(1); }
        if (second > 0) { (void)write(fds[1], &second, sizeof(second)); _exit(0); }
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
    do { n = read(fds[0], &worker, sizeof(worker)); } while (n < 0 && errno == EINTR);
    if (n == (ssize_t)sizeof(int)) {
        close(fds[0]);
        return rejected_promise(ctx, "start task", (int)worker);
    }
    close(fds[0]);
    if (n != (ssize_t)sizeof(pid_t) || worker <= 0)
        return rejected_promise(ctx, "start task handshake", EIO);
    {
        JSValue result = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, result, "taskId", JS_NewString(ctx, ({
            static char id[32]; snprintf(id, sizeof(id), "%ld", (long)worker); id;
        })));
        return resolved(ctx, result);
    }
}

static JSValue js_install_amr(JSContext *ctx, JSValueConst this_val,
                              int argc, JSValueConst *argv)
{
    const char *path;
    JSValue result;
    (void)this_val;
    if (argc < 1) return rejected_promise(ctx, "installAmr", EINVAL);
    path = JS_ToCString(ctx, argv[0]);
    if (!path) return JS_EXCEPTION;
    result = start_task(ctx, path, 1);
    JS_FreeCString(ctx, path);
    return result;
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

static int module_init(JSContext *ctx, JSModuleDef *m)
{
    JSValue api = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, api, "installAmr", JS_NewCFunction(ctx, js_install_amr, "installAmr", 1));
    JS_SetPropertyStr(ctx, api, "runScript", JS_NewCFunction(ctx, js_run_script, "runScript", 1));
    JS_SetPropertyStr(ctx, api, "taskStatus", JS_NewCFunction(ctx, js_task_status, "taskStatus", 1));
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
