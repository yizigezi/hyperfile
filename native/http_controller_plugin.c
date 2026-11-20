/*
 * QuickJS bridge for the standalone hyperfile-httpd process.
 * The app only sends START/STATUS/STOP commands; HTTP work never runs in
 * the QuickJS process.
 * SPDX-License-Identifier: AGPL-3.0
 */
#define _GNU_SOURCE
#include "quickjs.h"

#include <dlfcn.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#define CONTROL_SOCKET "/userdisk/hyperfile/httpd.sock"
#define SERVICE_DIR "/userdisk/hyperfile"
#define LOG_FILE "/userdisk/hyperfile/httpd.log"
#define DEFAULT_UPLOAD_DIR "/userdisk/Favorite/HyperFile"
#define DEFAULT_PORT 8080

typedef JSModuleDef* (*LoadCModuleFunction)(JSContext*, const char*);
extern void registerCModuleLoader(const char*, LoadCModuleFunction);

static JSValue resolved(JSContext *ctx, JSValue value)
{
    JSValue funcs[2], promise = JS_NewPromiseCapability(ctx, funcs);
    JS_Call(ctx, funcs[0], JS_UNDEFINED, 1, (JSValueConst *)&value);
    JS_FreeValue(ctx, funcs[0]); JS_FreeValue(ctx, funcs[1]); JS_FreeValue(ctx, value);
    return promise;
}
static JSValue rejected(JSContext *ctx, const char *message)
{
    JSValue error = JS_NewError(ctx);
    JS_SetPropertyStr(ctx, error, "message", JS_NewString(ctx, message));
    JSValue funcs[2], promise = JS_NewPromiseCapability(ctx, funcs);
    JS_Call(ctx, funcs[1], JS_UNDEFINED, 1, (JSValueConst *)&error);
    JS_FreeValue(ctx, funcs[0]); JS_FreeValue(ctx, funcs[1]); JS_FreeValue(ctx, error);
    return promise;
}
static JSValue parse_json(JSContext *ctx, const char *json)
{
    return JS_ParseJSON(ctx, json, strlen(json), "<hyperfile-httpd>");
}
static int control_request(const char *command, char *response, size_t capacity)
{
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un addr;
    ssize_t n;
    if (fd < 0) return -1;
    memset(&addr, 0, sizeof(addr)); addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", CONTROL_SOCKET);
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(fd); return -1;
    }
    (void)send(fd, command, strlen(command), 0);
    n = recv(fd, response, capacity - 1, 0);
    close(fd);
    if (n <= 0) return -1;
    response[n] = '\0';
    return 0;
}
static int daemon_path(char *out, size_t capacity)
{
    Dl_info info;
    char *slash;
    if (!dladdr((void *)&daemon_path, &info) || !info.dli_fname) return -1;
    if (strlen(info.dli_fname) + sizeof("/libhyperfile-httpd.so") > capacity) return -1;
    snprintf(out, capacity, "%s", info.dli_fname);
    slash = strrchr(out, '/');
    if (!slash) return -1;
    strcpy(slash + 1, "libhyperfile-httpd.so");
    return 0;
}
static int launch_daemon(int port, const char *dir)
{
    char executable[PATH_MAX], port_text[16];
    pid_t pid;
    int logfd;
    if (daemon_path(executable, sizeof(executable)) != 0) return -1;

    /*
     * The Falcon resource extractor may drop executable permissions when it
     * materializes files from the app package. Restore them on first launch.
     * chmod() only changes the mode; it does not elevate the app's privileges.
     */
    if (access(executable, X_OK) != 0) {
        if (chmod(executable, 0755) != 0 || access(executable, X_OK) != 0)
            return -1;
    }

    (void)mkdir("/userdisk/hyperfile", 0755);
    snprintf(port_text, sizeof(port_text), "%d", port);
    pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        (void)setsid();
        pid_t second = fork();
        if (second < 0) _exit(126);
        if (second > 0) _exit(0);
        logfd = open(LOG_FILE, O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (logfd >= 0) {
            dup2(logfd, STDOUT_FILENO); dup2(logfd, STDERR_FILENO); close(logfd);
        }
        close(STDIN_FILENO);
        execl(executable, executable, port_text, dir, (char *)NULL);
        _exit(127);
    }
    (void)waitpid(pid, NULL, 0);
    return 0;
}
static JSValue js_start(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    int port = DEFAULT_PORT;
    const char *dir = DEFAULT_UPLOAD_DIR;
    char response[1600];
    (void)this_val;
    if (argc > 0) JS_ToInt32(ctx, &port, argv[0]);
    if (argc > 1) dir = JS_ToCString(ctx, argv[1]);
    if (port < 1 || port > 65535 || !dir || !dir[0]) {
        if (argc > 1 && dir) JS_FreeCString(ctx, dir);
        return rejected(ctx, "invalid port or upload directory");
    }
    if (control_request("STATUS", response, sizeof(response)) == 0) {
        JSValue result = parse_json(ctx, response);
        if (argc > 1) JS_FreeCString(ctx, dir);
        return resolved(ctx, result);
    }
    if (launch_daemon(port, dir) != 0) {
        if (argc > 1) JS_FreeCString(ctx, dir);
        return rejected(ctx, "cannot launch hyperfile-httpd; check daemon path and permissions");
    }
    if (argc > 1) JS_FreeCString(ctx, dir);
    return resolved(ctx, parse_json(ctx, "{\"ok\":true,\"running\":false,\"starting\":true}"));
}
static JSValue js_status(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    char response[1600];
    (void)this_val; (void)argc; (void)argv;
    if (control_request("STATUS", response, sizeof(response)) != 0)
        return resolved(ctx, parse_json(ctx, "{\"ok\":true,\"running\":false}"));
    return resolved(ctx, parse_json(ctx, response));
}
static JSValue js_stop(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    char response[256];
    (void)this_val; (void)argc; (void)argv;
    if (control_request("STOP", response, sizeof(response)) != 0)
        return resolved(ctx, parse_json(ctx, "{\"ok\":true,\"running\":false}"));
    return resolved(ctx, parse_json(ctx, response));
}
static int module_init(JSContext *ctx, JSModuleDef *m)
{
    JSValue api = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, api, "start", JS_NewCFunction(ctx, js_start, "start", 2));
    JS_SetPropertyStr(ctx, api, "stop", JS_NewCFunction(ctx, js_stop, "stop", 0));
    JS_SetPropertyStr(ctx, api, "status", JS_NewCFunction(ctx, js_status, "status", 0));
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
    registerCModuleLoader("hyperfile_http", module_load);
}
