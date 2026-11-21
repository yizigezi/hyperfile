/*
 * QuickJS bridge for the standalone hyperfile-httpd process.
 * The app only sends START/STATUS/STOP commands; HTTP work never runs in
 * the QuickJS process.
 * SPDX-License-Identifier: AGPL-3.0
 */
#define _GNU_SOURCE
#include "quickjs.h"

#include <dlfcn.h>
#include <dirent.h>
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

#define CONTROL_SOCKET "/userdisk/HyperFile/httpd.sock"
#define SERVICE_DIR "/userdisk/HyperFile"
#define LOG_FILE "/userdisk/HyperFile/httpd.log"
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
static int regular_executable_candidate(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode) && access(path, R_OK) == 0;
}

/*
 * Prefer the daemon next to this JSAPI module. If the loader does not expose
 * a usable module path, scan every installed app package and both A/B slots.
 */
static void set_error(int error)
{
    errno = error ? error : EIO;
}

/*
 * Search all app package slots deterministically. dladdr() is intentionally
 * not trusted here: Falcon may return a loader alias rather than a filesystem
 * path. Return a concrete source path or preserve the last useful error.
 */
static int daemon_path(const char *prefix, char *out, size_t capacity)
{
    const char *pkg_root = "/userdisk/miniapp/data/mini_app/pkg";
    DIR *apps = opendir(pkg_root);
    struct dirent *app;
    int last_error = ENOENT;

    if (!apps) return -1;

    while ((app = readdir(apps)) != NULL) {
        const char *slots[] = { "a", "b" };
        if (app->d_name[0] == '.') continue;

        for (size_t i = 0; i < sizeof(slots) / sizeof(slots[0]); ++i) {
            char libs_dir[PATH_MAX];
            DIR *libs;
            struct dirent *entry;

            if (snprintf(libs_dir, sizeof(libs_dir), "%s/%s/%s/libs",
                         pkg_root, app->d_name, slots[i]) >= (int)sizeof(libs_dir))
                continue;

            libs = opendir(libs_dir);
            if (!libs) {
                if (errno != ENOENT && errno != ENOTDIR) last_error = errno;
                continue;
            }

            while ((entry = readdir(libs)) != NULL) {
                char candidate[PATH_MAX];
                struct stat st;
                size_t prefix_len = strlen(prefix);
                int length;

                /* The installer appends a generated suffix to the library name. */
                if (strncmp(entry->d_name, prefix, prefix_len) != 0) continue;
                if (entry->d_name[prefix_len] == '\0') continue;

                length = snprintf(candidate, sizeof(candidate), "%s/%s",
                                  libs_dir, entry->d_name);
                if (length < 0 || length >= (int)sizeof(candidate)) continue;

                if (stat(candidate, &st) != 0) {
                    last_error = errno;
                    continue;
                }
                if (!S_ISREG(st.st_mode)) continue;
                if (access(candidate, R_OK) != 0) {
                    last_error = errno;
                    continue;
                }
                if ((size_t)length + 1 > capacity) {
                    closedir(libs);
                    closedir(apps);
                    errno = ENAMETOOLONG;
                    return -1;
                }

                memcpy(out, candidate, (size_t)length + 1);
                closedir(libs);
                closedir(apps);
                return 0;
            }
            closedir(libs);
        }
    }

    closedir(apps);
    errno = last_error;
    return -1;
}

static int copy_daemon(const char *source, const char *destination)
{
    int in_fd = -1, out_fd = -1;
    char temporary[PATH_MAX];
    char buffer[16384];
    ssize_t n;
    int saved_errno = 0;

    if (snprintf(temporary, sizeof(temporary), "%s.tmp.%ld",
                 destination, (long)getpid()) >= (int)sizeof(temporary)) {
        errno = ENAMETOOLONG;
        return -1;
    }

    in_fd = open(source, O_RDONLY);
    if (in_fd < 0) return -1;

    out_fd = open(temporary, O_WRONLY | O_CREAT | O_TRUNC, 0700);
    if (out_fd < 0) {
        saved_errno = errno;
        close(in_fd);
        errno = saved_errno;
        return -1;
    }

    while ((n = read(in_fd, buffer, sizeof(buffer))) > 0) {
        ssize_t offset = 0;
        while (offset < n) {
            ssize_t written = write(out_fd, buffer + offset, (size_t)(n - offset));
            if (written < 0) {
                if (errno == EINTR) continue;
                saved_errno = errno;
                goto fail;
            }
            offset += written;
        }
    }
    if (n < 0) {
        saved_errno = errno;
        goto fail;
    }
    if (fchmod(out_fd, 0755) != 0 || fsync(out_fd) != 0) {
        saved_errno = errno;
        goto fail;
    }
    if (close(out_fd) != 0) {
        out_fd = -1;
        saved_errno = errno;
        goto fail;
    }
    out_fd = -1;
    close(in_fd);
    in_fd = -1;

    if (rename(temporary, destination) != 0) {
        saved_errno = errno;
        unlink(temporary);
        errno = saved_errno;
        return -1;
    }
    return 0;

fail:
    if (out_fd >= 0) close(out_fd);
    if (in_fd >= 0) close(in_fd);
    unlink(temporary);
    errno = saved_errno ? saved_errno : EIO;
    return -1;
}

static int initialized_daemon_path(char *executable, size_t capacity)
{
    struct stat st;
    if (snprintf(executable, capacity, "%s/hyperfile-httpd", SERVICE_DIR) >= (int)capacity) {
        errno = ENAMETOOLONG;
        return -1;
    }
    if (stat(executable, &st) != 0 || !S_ISREG(st.st_mode) || access(executable, X_OK) != 0) {
        if (errno == 0) errno = ENOENT;
        return -1;
    }
    return 0;
}

static int initialize_daemon(char *stage, size_t stage_capacity, char *source_out, size_t source_capacity)
{
    char source[PATH_MAX], executable[PATH_MAX];
    struct stat st;

    if (stage && stage_capacity) snprintf(stage, stage_capacity, "create-service-directory");
    if (mkdir(SERVICE_DIR, 0755) != 0 && errno != EEXIST) return -1;

    if (stage && stage_capacity) snprintf(stage, stage_capacity, "search-a-b-slots");
    if (daemon_path("libhyperfile-httpd", source, sizeof(source)) != 0) return -1;
    if (source_out && source_capacity) snprintf(source_out, source_capacity, "%s", source);

    if (stage && stage_capacity) snprintf(stage, stage_capacity, "build-destination-path");
    if (snprintf(executable, sizeof(executable), "%s/hyperfile-httpd", SERVICE_DIR) >= (int)sizeof(executable)) {
        errno = ENAMETOOLONG;
        return -1;
    }

    if (stage && stage_capacity) snprintf(stage, stage_capacity, "copy-daemon");
    if (copy_daemon(source, executable) != 0) return -1;

    if (stage && stage_capacity) snprintf(stage, stage_capacity, "verify-executable");
    if (stat(executable, &st) != 0) return -1;
    if (!S_ISREG(st.st_mode)) {
        errno = EINVAL;
        return -1;
    }
    if (access(executable, X_OK) != 0) return -1;

    if (stage && stage_capacity) snprintf(stage, stage_capacity, "search-updater");
    if (daemon_path("libhyperfile-updater", source, sizeof(source)) != 0) return -1;
    if (stage && stage_capacity) snprintf(stage, stage_capacity, "copy-updater");
    if (copy_daemon(source, SERVICE_DIR "/hyperfile-updater") != 0) return -1;
    if (access(SERVICE_DIR "/hyperfile-updater", X_OK) != 0) return -1;

    if (stage && stage_capacity) snprintf(stage, stage_capacity, "complete");
    return 0;
}

static int launch_daemon(int port, const char *dir)
{
    char executable[PATH_MAX], port_text[16];
    pid_t pid;
    int logfd;

    if (initialized_daemon_path(executable, sizeof(executable)) != 0) { errno = ENOENT; return -1; }

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
            dup2(logfd, STDOUT_FILENO);
            dup2(logfd, STDERR_FILENO);
            close(logfd);
        }
        close(STDIN_FILENO);
        execl(executable, executable, port_text, dir, (char *)NULL);
        _exit(127);
    }
    (void)waitpid(pid, NULL, 0);
    return 0;
}
static JSValue js_initialize(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    char result[256], stage[64] = "begin", source[PATH_MAX] = "";
    int error;
    (void)this_val; (void)argc; (void)argv;

    if (initialize_daemon(stage, sizeof(stage), source, sizeof(source)) != 0) {
        char message[512];
        error = errno;
        snprintf(message, sizeof(message),
                 "httpd initialization failed: stage=%s errno=%d (%s) source=%s",
                 stage, error, strerror(error), source[0] ? source : "(not found)");
        return rejected(ctx, message);
    }

    snprintf(result, sizeof(result),
             "{\"ok\":true,\"initialized\":true,\"path\":\"%s/hyperfile-httpd\"}",
             SERVICE_DIR);
    return resolved(ctx, parse_json(ctx, result));
}

static JSValue js_enable_adb(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    const char *markers[] = { "/tmp/.adbOn", "/tmp/.adb_auth_verified" };
    (void)this_val; (void)argc; (void)argv;
    if (mkdir("/tmp", 0755) != 0 && errno != EEXIST)
        return rejected(ctx, "cannot access /tmp");
    for (size_t i = 0; i < sizeof(markers) / sizeof(markers[0]); ++i) {
        int fd = open(markers[i], O_WRONLY | O_CREAT | O_CLOEXEC, 0600);
        if (fd < 0) {
            char message[160];
            snprintf(message, sizeof(message), "cannot create %s: %s", markers[i], strerror(errno));
            return rejected(ctx, message);
        }
        if (fsync(fd) != 0) {
            close(fd);
            return rejected(ctx, "cannot sync ADB marker files");
        }
        close(fd);
    }
    return resolved(ctx, parse_json(ctx, "{\"ok\":true,\"enabled\":true}"));
}

static JSValue js_update_token(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    char token[32] = "";
    FILE *fp;
    (void)this_val; (void)argc; (void)argv;
    fp = fopen("/tmp/hyperfile-update.token", "r");
    if (fp) {
        (void)fgets(token, sizeof(token), fp);
        fclose(fp);
        token[strcspn(token, "\r\n")] = 0;
    }
    return resolved(ctx, JS_NewString(ctx, token));
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
    char response[1600], executable[PATH_MAX], fallback[128];
    int initialized;
    (void)this_val; (void)argc; (void)argv;
    initialized = initialized_daemon_path(executable, sizeof(executable)) == 0;
    if (control_request("STATUS", response, sizeof(response)) != 0) {
        snprintf(fallback, sizeof(fallback),
                 "{\"ok\":true,\"running\":false,\"initialized\":%s}",
                 initialized ? "true" : "false");
        return resolved(ctx, parse_json(ctx, fallback));
    }
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
    JS_SetPropertyStr(ctx, api, "initialize", JS_NewCFunction(ctx, js_initialize, "initialize", 0));
    JS_SetPropertyStr(ctx, api, "enableAdb", JS_NewCFunction(ctx, js_enable_adb, "enableAdb", 0));
    JS_SetPropertyStr(ctx, api, "updateToken", JS_NewCFunction(ctx, js_update_token, "updateToken", 0));
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
