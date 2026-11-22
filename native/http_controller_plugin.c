/*
 * HyperFile controller JSAPI.
 *
 * Responsibilities are deliberately narrow:
 *   - locate and atomically install the standalone executables;
 *   - launch/inspect/stop the detached HTTP daemon;
 *   - expose small device helpers to QuickJS.
 *
 * No HTTP work is performed in this process. Long-running initialization is
 * delegated to a detached child; the QuickJS thread only starts that worker
 * and polls its state. The daemon launch path never waits for daemon startup.
 *
 * SPDX-License-Identifier: AGPL-3.0
 */
#define _GNU_SOURCE
#include "quickjs.h"

#include <dirent.h>
#include <dlfcn.h>
#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif
#ifndef O_NOFOLLOW
#define O_NOFOLLOW 0
#endif

#define SERVICE_DIR       "/userdisk/HyperFile"
#define HTTPD_PATH        SERVICE_DIR "/hyperfile-httpd"
#define UPDATER_PATH      SERVICE_DIR "/hyperfile-updater"
#define CONTROL_SOCKET    SERVICE_DIR "/httpd.sock"
#define HTTPD_LOG         SERVICE_DIR "/httpd.log"
#define INIT_STATE        SERVICE_DIR "/init.status"
#define INIT_LOG          SERVICE_DIR "/init.log"
#define UPDATE_TOKEN_FILE "/tmp/hyperfile-update.token"
#define DEFAULT_UPLOAD_DIR "/userdisk/Favorite/HyperFile"
#define DEFAULT_PORT      8080
#define CONTROL_TIMEOUT_MS 120

typedef JSModuleDef *(*LoadCModuleFunction)(JSContext *, const char *);
extern void registerCModuleLoader(const char *, LoadCModuleFunction);

/* ---------- QuickJS promise/result helpers ---------- */

static JSValue promise_result(JSContext *ctx, JSValue value, int reject)
{
    JSValue funcs[2];
    JSValue promise = JS_NewPromiseCapability(ctx, funcs);
    if (JS_IsException(promise)) {
        JS_FreeValue(ctx, value);
        return promise;
    }
    JS_Call(ctx, reject ? funcs[1] : funcs[0], JS_UNDEFINED, 1,
            (JSValueConst *)&value);
    JS_FreeValue(ctx, funcs[0]);
    JS_FreeValue(ctx, funcs[1]);
    JS_FreeValue(ctx, value);
    return promise;
}

static JSValue promise_json(JSContext *ctx, const char *json)
{
    return promise_result(ctx, JS_ParseJSON(ctx, json, strlen(json),
                                            "<hyperfile-controller>"), 0);
}

static JSValue promise_error(JSContext *ctx, const char *message)
{
    JSValue error = JS_NewError(ctx);
    JS_SetPropertyStr(ctx, error, "message", JS_NewString(ctx, message));
    return promise_result(ctx, error, 1);
}

static JSValue promise_errno(JSContext *ctx, const char *operation, int error)
{
    char message[384];
    if (!error) error = EIO;
    snprintf(message, sizeof(message), "%s: errno=%d (%s)",
             operation, error, strerror(error));
    return promise_error(ctx, message);
}

/* ---------- Small, errno-preserving filesystem primitives ---------- */

static int write_all(int fd, const void *data, size_t length)
{
    const char *p = (const char *)data;
    size_t offset = 0;
    while (offset < length) {
        ssize_t n = write(fd, p + offset, length - offset);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) {
            if (n == 0) errno = EIO;
            return -1;
        }
        offset += (size_t)n;
    }
    return 0;
}

static int write_state(const char *state)
{
    char tmp[PATH_MAX];
    int fd, saved;
    int n = snprintf(tmp, sizeof(tmp), "%s.tmp.%ld", INIT_STATE, (long)getpid());
    if (n < 0) { errno = EIO; return -1; }
    if ((size_t)n >= sizeof(tmp)) { errno = ENAMETOOLONG; return -1; }
    fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (fd < 0) return -1;
    if (write_all(fd, state, strlen(state)) != 0 || fsync(fd) != 0) {
        saved = errno;
        close(fd);
        unlink(tmp);
        errno = saved;
        return -1;
    }
    if (close(fd) != 0) {
        saved = errno;
        unlink(tmp);
        errno = saved;
        return -1;
    }
    if (rename(tmp, INIT_STATE) != 0) {
        saved = errno;
        unlink(tmp);
        errno = saved;
        return -1;
    }
    return 0;
}

/*
 * Search every package and both A/B slots. Missing slots are normal; errors
 * other than ENOENT/ENOTDIR are retained and reported if no candidate works.
 */
static int find_package_binary(const char *prefix, char *out, size_t capacity)
{
    const char *root = "/userdisk/miniapp/data/mini_app/pkg";
    DIR *apps = opendir(root);
    struct dirent *app;
    int useful_error = 0;

    if (!apps) return -1;
    while ((app = readdir(apps)) != NULL) {
        const char *slots[] = { "a", "b" };
        size_t i;
        if (app->d_name[0] == '.') continue;

        for (i = 0; i < 2; ++i) {
            char dirpath[PATH_MAX];
            DIR *libs;
            struct dirent *entry;
            int n = snprintf(dirpath, sizeof(dirpath), "%s/%s/%s/libs",
                             root, app->d_name, slots[i]);
            if (n < 0 || (size_t)n >= sizeof(dirpath)) {
                if (!useful_error) useful_error = ENAMETOOLONG;
                continue;
            }
            libs = opendir(dirpath);
            if (!libs) {
                if (errno != ENOENT && errno != ENOTDIR && !useful_error)
                    useful_error = errno;
                continue;
            }
            while ((entry = readdir(libs)) != NULL) {
                char candidate[PATH_MAX];
                struct stat st;
                size_t prefix_len = strlen(prefix);
                if (strncmp(entry->d_name, prefix, prefix_len) != 0 ||
                    entry->d_name[prefix_len] == '\0')
                    continue;
                n = snprintf(candidate, sizeof(candidate), "%s/%s",
                             dirpath, entry->d_name);
                if (n < 0 || (size_t)n >= sizeof(candidate)) {
                    if (!useful_error) useful_error = ENAMETOOLONG;
                    continue;
                }
                if (stat(candidate, &st) != 0) {
                    if (!useful_error) useful_error = errno;
                    continue;
                }
                if (!S_ISREG(st.st_mode)) continue;
                if (access(candidate, R_OK) != 0) {
                    if (!useful_error) useful_error = errno;
                    continue;
                }
                if ((size_t)n + 1 > capacity) {
                    closedir(libs);
                    closedir(apps);
                    errno = ENAMETOOLONG;
                    return -1;
                }
                memcpy(out, candidate, (size_t)n + 1);
                closedir(libs);
                closedir(apps);
                return 0;
            }
            closedir(libs);
        }
    }
    closedir(apps);
    errno = useful_error ? useful_error : ENOENT;
    return -1;
}

/* Copy to a same-directory temporary file, fsync, chmod, then atomic rename. */
static int install_binary(const char *source, const char *destination)
{
    char temporary[PATH_MAX];
    char buffer[16384];
    struct stat st;
    int in = -1, out = -1, saved = 0, n;
    ssize_t count;

    n = snprintf(temporary, sizeof(temporary), "%s.tmp.%ld",
                 destination, (long)getpid());
    if (n < 0) { errno = EIO; return -1; }
    if ((size_t)n >= sizeof(temporary)) { errno = ENAMETOOLONG; return -1; }

    in = open(source, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (in < 0) return -1;
    if (fstat(in, &st) != 0) { saved = errno; goto fail; }
    if (!S_ISREG(st.st_mode)) { saved = EINVAL; goto fail; }

    out = open(temporary, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0700);
    if (out < 0) { saved = errno; goto fail; }

    for (;;) {
        count = read(in, buffer, sizeof(buffer));
        if (count < 0 && errno == EINTR) continue;
        if (count < 0) { saved = errno; goto fail; }
        if (count == 0) break;
        if (write_all(out, buffer, (size_t)count) != 0) {
            saved = errno;
            goto fail;
        }
    }
    if (fchmod(out, 0755) != 0 || fsync(out) != 0) {
        saved = errno;
        goto fail;
    }
    if (close(out) != 0) { out = -1; saved = errno; goto fail; }
    out = -1;
    if (close(in) != 0) { in = -1; saved = errno; goto fail; }
    in = -1;
    if (rename(temporary, destination) != 0) {
        saved = errno;
        goto fail;
    }
    return 0;

fail:
    if (!saved) saved = EIO;
    if (out >= 0) close(out);
    if (in >= 0) close(in);
    unlink(temporary);
    errno = saved;
    return -1;
}

static int validate_executable(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0) return -1;
    if (!S_ISREG(st.st_mode)) { errno = EINVAL; return -1; }
    if (access(path, X_OK) != 0) return -1;
    return 0;
}

static int validate_installation(void)
{
    if (validate_executable(HTTPD_PATH) != 0) return -1;
    if (validate_executable(UPDATER_PATH) != 0) return -1;
    return 0;
}

static int initialized(void)
{
    return validate_installation() == 0;
}

/* ---------- Detached initialization worker ---------- */

static int initialize_worker(void)
{
    char source[PATH_MAX], state[PATH_MAX + 256], stage[64] = "mkdir";
    int saved;

    if (mkdir(SERVICE_DIR, 0755) != 0 && errno != EEXIST) goto failed;
    (void)write_state("running\n");

    snprintf(stage, sizeof(stage), "find-httpd");
    if (find_package_binary("libhyperfile-httpd", source, sizeof(source)) != 0)
        goto failed;
    snprintf(stage, sizeof(stage), "copy-httpd");
    if (install_binary(source, HTTPD_PATH) != 0) goto failed;

    snprintf(stage, sizeof(stage), "find-updater");
    if (find_package_binary("libhyperfile-updater", source, sizeof(source)) != 0)
        goto failed;
    snprintf(stage, sizeof(stage), "copy-updater");
    if (install_binary(source, UPDATER_PATH) != 0) goto failed;

    if (validate_executable(HTTPD_PATH) != 0) {
        snprintf(stage, sizeof(stage), "verify-httpd");
        goto failed;
    }
    if (validate_executable(UPDATER_PATH) != 0) {
        snprintf(stage, sizeof(stage), "verify-updater");
        goto failed;
    }
    (void)write_state("ready\n");
    return 0;

failed:
    saved = errno ? errno : EIO;
    snprintf(state, sizeof(state), "error\nstage=%s\nerrno=%d\nmessage=%s\n",
             stage, saved, strerror(saved));
    (void)write_state(state);
    return -1;
}

static int spawn_detached_worker(void (*worker)(void))
{
    pid_t first = fork();
    int status = 0;
    if (first < 0) return -1;
    if (first == 0) {
        pid_t second;
        (void)setsid();
        second = fork();
        if (second < 0) _exit(126);
        if (second > 0) _exit(0);
        worker();
        _exit(0);
    }
    while (waitpid(first, &status, 0) < 0) {
        if (errno == EINTR) continue;
        return -1;
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        errno = ECHILD;
        return -1;
    }
    return 0;
}

static void initialize_worker_entry(void)
{
    int logfd = open(INIT_LOG, O_WRONLY | O_CREAT | O_APPEND, 0600);
    if (logfd >= 0) {
        (void)dup2(logfd, STDOUT_FILENO);
        (void)dup2(logfd, STDERR_FILENO);
        if (logfd > STDERR_FILENO) close(logfd);
    }
    (void)initialize_worker();
}

/* ---------- Detached HTTP daemon launcher ---------- */

/*
 * The first child exits immediately after forking the daemon. The QuickJS
 * thread waits only for that tiny fork hand-off, never for exec/readiness.
 * exec errors are written to httpd.log by the daemon child itself.
 */
static int launch_daemon(int port, const char *directory)
{
    pid_t first;
    int status = 0, pipefd[2], child_error = 0, polled;
    char port_text[16];
    struct pollfd pfd;
    ssize_t received;

    if (validate_executable(HTTPD_PATH) != 0) return -1;
    if (pipe(pipefd) != 0) return -1;
    if (fcntl(pipefd[1], F_SETFD, FD_CLOEXEC) != 0) {
        int saved = errno;
        close(pipefd[0]);
        close(pipefd[1]);
        errno = saved;
        return -1;
    }
    snprintf(port_text, sizeof(port_text), "%d", port);

    first = fork();
    if (first < 0) {
        int saved = errno;
        close(pipefd[0]);
        close(pipefd[1]);
        errno = saved;
        return -1;
    }
    if (first == 0) {
        pid_t second;
        close(pipefd[0]);
        if (setsid() < 0) {
            child_error = errno;
            (void)write(pipefd[1], &child_error, sizeof(child_error));
            _exit(126);
        }
        second = fork();
        if (second < 0) {
            child_error = errno;
            (void)write(pipefd[1], &child_error, sizeof(child_error));
            _exit(126);
        }
        if (second > 0) _exit(0);

        {
            int logfd = open(HTTPD_LOG, O_WRONLY | O_CREAT | O_APPEND, 0644);
            if (logfd >= 0) {
                (void)dup2(logfd, STDOUT_FILENO);
                (void)dup2(logfd, STDERR_FILENO);
                if (logfd > STDERR_FILENO) close(logfd);
            } else {
                int nullfd = open("/dev/null", O_WRONLY);
                if (nullfd >= 0) {
                    (void)dup2(nullfd, STDOUT_FILENO);
                    (void)dup2(nullfd, STDERR_FILENO);
                    if (nullfd > STDERR_FILENO) close(nullfd);
                }
            }
        }
        close(STDIN_FILENO);
        execl(HTTPD_PATH, HTTPD_PATH, port_text, directory, (char *)NULL);
        child_error = errno;
        dprintf(STDERR_FILENO,
                "[controller] exec %s failed: errno=%d (%s)\n",
                HTTPD_PATH, child_error, strerror(child_error));
        (void)write(pipefd[1], &child_error, sizeof(child_error));
        _exit(127);
    }

    close(pipefd[1]);
    while (waitpid(first, &status, 0) < 0) {
        if (errno == EINTR) continue;
        {
            int saved = errno;
            close(pipefd[0]);
            errno = saved;
            return -1;
        }
    }
    pfd.fd = pipefd[0];
    pfd.events = POLLIN | POLLHUP;
    pfd.revents = 0;
    do { polled = poll(&pfd, 1, CONTROL_TIMEOUT_MS); }
    while (polled < 0 && errno == EINTR);
    if (polled <= 0) {
        int saved = polled == 0 ? ETIMEDOUT : errno;
        close(pipefd[0]);
        errno = saved;
        return -1;
    }

    if (pfd.revents & POLLIN) {
        do { received = read(pipefd[0], &child_error, sizeof(child_error)); }
        while (received < 0 && errno == EINTR);
        if (received < 0) {
            int saved = errno;
            close(pipefd[0]);
            errno = saved;
            return -1;
        }
        if (received == (ssize_t)sizeof(child_error)) {
            close(pipefd[0]);
            errno = child_error;
            return -1;
        }
        if (received != 0) {
            close(pipefd[0]);
            errno = EIO;
            return -1;
        }
    } else if (!(pfd.revents & POLLHUP)) {
        close(pipefd[0]);
        errno = EIO;
        return -1;
    }

    /* POLLHUP means exec closed the FD_CLOEXEC writer: exec succeeded. */
    close(pipefd[0]);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        errno = ECHILD;
        return -1;
    }
    return 0;
}

/* ---------- Nonblocking local control protocol ---------- */

/*
 * Never wait for a daemon that is not accepting control connections.
 * ENOENT/ECONNREFUSED mean "not running"; all other errors stay distinguishable.
 */
static int control_request(const char *command, char *response, size_t capacity)
{
    int fd, flags, saved;
    struct sockaddr_un address;
    ssize_t n;
    size_t length = strlen(command);

    if (!response || capacity < 2) { errno = EINVAL; return -1; }
    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) != 0) {
        saved = errno;
        close(fd);
        errno = saved;
        return -1;
    }

    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    snprintf(address.sun_path, sizeof(address.sun_path), "%s", CONTROL_SOCKET);
    if (connect(fd, (struct sockaddr *)&address, sizeof(address)) != 0) {
        struct pollfd pfd;
        int polled;
        if (errno != EINPROGRESS && errno != EAGAIN) {
            saved = errno;
            close(fd);
            errno = saved;
            return -1;
        }
        pfd.fd = fd;
        pfd.events = POLLOUT;
        pfd.revents = 0;
        do { polled = poll(&pfd, 1, CONTROL_TIMEOUT_MS); }
        while (polled < 0 && errno == EINTR);
        if (polled <= 0) {
            saved = polled == 0 ? ETIMEDOUT : errno;
            close(fd);
            errno = saved;
            return -1;
        }
        {
            int socket_error = 0;
            socklen_t error_size = sizeof(socket_error);
            if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &socket_error, &error_size) != 0) {
                saved = errno;
                close(fd);
                errno = saved;
                return -1;
            }
            if (socket_error) {
                close(fd);
                errno = socket_error;
                return -1;
            }
        }
    }

    {
        size_t sent = 0;
        while (sent < length) {
            n = send(fd, command + sent, length - sent, MSG_DONTWAIT
#ifdef MSG_NOSIGNAL
                    | MSG_NOSIGNAL
#endif
                    );
            if (n < 0 && errno == EINTR) continue;
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                struct pollfd pfd = { fd, POLLOUT, 0 };
                int polled;
                do { polled = poll(&pfd, 1, CONTROL_TIMEOUT_MS); }
                while (polled < 0 && errno == EINTR);
                if (polled > 0) continue;
                saved = polled == 0 ? ETIMEDOUT : errno;
            } else {
                saved = n == 0 ? EPIPE : errno;
            }
            if (n <= 0) {
                close(fd);
                errno = saved;
                return -1;
            }
            sent += (size_t)n;
        }
    }

    for (;;) {
        n = recv(fd, response, capacity - 1, MSG_DONTWAIT);
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            struct pollfd pfd = { fd, POLLIN, 0 };
            int polled;
            do { polled = poll(&pfd, 1, CONTROL_TIMEOUT_MS); }
            while (polled < 0 && errno == EINTR);
            if (polled > 0) continue;
            saved = polled == 0 ? ETIMEDOUT : errno;
            close(fd);
            errno = saved;
            return -1;
        }
        if (n <= 0) {
            saved = n == 0 ? ECONNRESET : errno;
            close(fd);
            errno = saved;
            return -1;
        }
        break;
    }
    response[n] = '\0';
    close(fd);
    return 0;
}

static int control_unavailable_error(int error)
{
    return error == ENOENT || error == ECONNREFUSED || error == ECONNRESET ||
           error == EAGAIN || error == EWOULDBLOCK || error == EINPROGRESS;
}

/* ---------- Diagnostic and status payloads ---------- */

static void mount_info(char *mountpoint, size_t mountpoint_size,
                       char *options, size_t options_size, int *noexec)
{
    FILE *fp = fopen("/proc/mounts", "r");
    char device[256], point[PATH_MAX], type[64], opts[512];
    size_t best = 0;
    *noexec = -1;
    mountpoint[0] = options[0] = '\0';
    if (!fp) return;
    while (fscanf(fp, "%255s %4095s %63s %511s %*d %*d",
                  device, point, type, opts) == 4) {
        size_t n = strlen(point);
        if (n < best || strncmp(SERVICE_DIR, point, n) != 0 ||
            (SERVICE_DIR[n] != '\0' && SERVICE_DIR[n] != '/'))
            continue;
        best = n;
        snprintf(mountpoint, mountpoint_size, "%s", point);
        snprintf(options, options_size, "%s", opts);
        *noexec = strstr(opts, "noexec") != NULL;
    }
    fclose(fp);
}

static void read_tail(const char *path, char *out, size_t capacity)
{
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    off_t end;
    ssize_t n;
    if (!capacity) return;
    out[0] = '\0';
    if (fd < 0) return;
    end = lseek(fd, 0, SEEK_END);
    if (end > 0) {
        size_t amount = end > (off_t)(capacity - 1) ? capacity - 1 : (size_t)end;
        if (lseek(fd, end - (off_t)amount, SEEK_SET) >= 0) {
            n = read(fd, out, amount);
            if (n > 0) out[n] = '\0';
        }
    }
    close(fd);
}

static JSValue js_diagnose(JSContext *ctx, JSValueConst this_val,
                           int argc, JSValueConst *argv)
{
    char mountpoint[PATH_MAX], options[512], log[3073];
    struct stat st;
    int exists, executable, error = 0, noexec;
    int elf32 = 0, machine = 0, arm = 0;
    (void)this_val; (void)argc; (void)argv;

    exists = stat(HTTPD_PATH, &st) == 0;
    if (!exists) error = errno;
    else if (!S_ISREG(st.st_mode)) error = EINVAL;
    executable = exists && S_ISREG(st.st_mode) && access(HTTPD_PATH, X_OK) == 0;
    if (exists && !executable && !error) error = errno;

    if (exists && S_ISREG(st.st_mode)) {
        Elf32_Ehdr header;
        int fd = open(HTTPD_PATH, O_RDONLY | O_CLOEXEC);
        if (fd >= 0) {
            ssize_t n = read(fd, &header, sizeof(header));
            close(fd);
            if (n == (ssize_t)sizeof(header) &&
                memcmp(header.e_ident, ELFMAG, SELFMAG) == 0) {
                elf32 = header.e_ident[EI_CLASS] == ELFCLASS32;
                machine = header.e_machine;
                arm = elf32 && machine == EM_ARM;
            }
        }
    }
    mount_info(mountpoint, sizeof(mountpoint), options, sizeof(options), &noexec);
    read_tail(HTTPD_LOG, log, sizeof(log));
    if (!log[0]) snprintf(log, sizeof(log), "日志不存在或为空");

    JSValue result = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, result, "path", JS_NewString(ctx, HTTPD_PATH));
    JS_SetPropertyStr(ctx, result, "exists", JS_NewBool(ctx, exists));
    JS_SetPropertyStr(ctx, result, "regularFile", JS_NewBool(ctx, exists && S_ISREG(st.st_mode)));
    JS_SetPropertyStr(ctx, result, "executable", JS_NewBool(ctx, executable));
    JS_SetPropertyStr(ctx, result, "mode", JS_NewInt32(ctx, exists ? (int)(st.st_mode & 0777) : 0));
    JS_SetPropertyStr(ctx, result, "size", JS_NewInt64(ctx, exists ? (int64_t)st.st_size : 0));
    JS_SetPropertyStr(ctx, result, "elf32", JS_NewBool(ctx, elf32));
    JS_SetPropertyStr(ctx, result, "elfMachine", JS_NewInt32(ctx, machine));
    JS_SetPropertyStr(ctx, result, "arm", JS_NewBool(ctx, arm));
    JS_SetPropertyStr(ctx, result, "errno", JS_NewInt32(ctx, error));
    JS_SetPropertyStr(ctx, result, "error", JS_NewString(ctx, error ? strerror(error) : "none"));
    JS_SetPropertyStr(ctx, result, "mountPoint", JS_NewString(ctx, mountpoint));
    JS_SetPropertyStr(ctx, result, "mountOptions", JS_NewString(ctx, options));
    JS_SetPropertyStr(ctx, result, "noexec", JS_NewInt32(ctx, noexec));
    JS_SetPropertyStr(ctx, result, "log", JS_NewString(ctx, log));
    return promise_result(ctx, result, 0);
}

static JSValue js_status(JSContext *ctx, JSValueConst this_val,
                         int argc, JSValueConst *argv)
{
    char response[1600], state[512] = "";
    int control_error = 0, init_error = 0, init_result;
    (void)this_val; (void)argc; (void)argv;

    if (control_request("STATUS", response, sizeof(response)) == 0) {
        JSValue value = JS_ParseJSON(ctx, response, strlen(response), "<httpd-status>");
        return promise_result(ctx, value, 0);
    }
    control_error = errno;
    if (!control_unavailable_error(control_error))
        return promise_errno(ctx, "control STATUS failed", control_error);

    read_tail(INIT_STATE, state, sizeof(state));
    {
        char json[1024];
        int is_ready;
        const char *init_message;
        init_result = validate_installation();
        is_ready = init_result == 0;
        if (!is_ready) init_error = errno;
        int is_initializing = strncmp(state, "running\n", 8) == 0;
        init_message = strncmp(state, "error\n", 6) == 0
                     ? "初始化失败，详情见 /userdisk/HyperFile/init.status"
                     : "";
        snprintf(json, sizeof(json),
                 "{\"ok\":true,\"running\":false,\"initialized\":%s,"
                 "\"initializing\":%s,\"initError\":\"%s\","
                 "\"initErrno\":%d,\"controlErrno\":%d}",
                 is_ready ? "true" : "false",
                 is_initializing ? "true" : "false",
                 init_message, init_error, control_error);
        return promise_json(ctx, json);
    }
}

/* ---------- Public JSAPI ---------- */

static JSValue js_initialize(JSContext *ctx, JSValueConst this_val,
                             int argc, JSValueConst *argv)
{
    char state[512] = "";
    int fd;
    (void)this_val; (void)argc; (void)argv;

    if (initialized())
        return promise_json(ctx, "{\"ok\":true,\"initialized\":true,\"initializing\":false}");

    if (mkdir(SERVICE_DIR, 0755) != 0 && errno != EEXIST)
        return promise_errno(ctx, "create service directory failed", errno);

    read_tail(INIT_STATE, state, sizeof(state));
    if (strncmp(state, "running\n", 8) == 0)
        return promise_json(ctx, "{\"ok\":true,\"initialized\":false,\"initializing\":true}");

    /* Remove stale state before spawning a fresh worker. */
    if (unlink(INIT_STATE) != 0 && errno != ENOENT)
        return promise_errno(ctx, "remove stale initialization state failed", errno);

    /* Create the running state before fork so status is immediately coherent. */
    fd = open(INIT_STATE, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (fd < 0) return promise_errno(ctx, "create initialization state failed", errno);
    if (write_all(fd, "running\n", 8) != 0 || close(fd) != 0) {
        int saved = errno;
        close(fd);
        return promise_errno(ctx, "write initialization state failed", saved);
    }

    if (spawn_detached_worker(initialize_worker_entry) != 0) {
        int saved = errno;
        char state_error[128];
        snprintf(state_error, sizeof(state_error), "error\nstage=spawn\nerrno=%d\n", saved);
        (void)write_state(state_error);
        return promise_errno(ctx, "start initialization worker failed", saved);
    }
    return promise_json(ctx, "{\"ok\":true,\"initialized\":false,\"initializing\":true}");
}

static JSValue js_start(JSContext *ctx, JSValueConst this_val,
                        int argc, JSValueConst *argv)
{
    int32_t port = DEFAULT_PORT;
    const char *directory = DEFAULT_UPLOAD_DIR;
    char response[1600];
    int error;
    (void)this_val;

    if (argc > 0 && JS_ToInt32(ctx, &port, argv[0]) < 0)
        return promise_error(ctx, "invalid port");
    if (argc > 1) {
        directory = JS_ToCString(ctx, argv[1]);
        if (!directory) return JS_EXCEPTION;
    }
    if (port < 1 || port > 65535 || !directory || !directory[0]) {
        if (argc > 1 && directory) JS_FreeCString(ctx, directory);
        return promise_error(ctx, "invalid port or upload directory");
    }

    if (control_request("STATUS", response, sizeof(response)) == 0) {
        if (argc > 1) JS_FreeCString(ctx, directory);
        return promise_result(ctx,
            JS_ParseJSON(ctx, response, strlen(response), "<httpd-status>"), 0);
    }
    error = errno;
    if (!control_unavailable_error(error)) {
        if (argc > 1) JS_FreeCString(ctx, directory);
        return promise_errno(ctx, "control STATUS failed", error);
    }

    if (validate_executable(HTTPD_PATH) != 0) {
        error = errno;
        if (argc > 1) JS_FreeCString(ctx, directory);
        return promise_errno(ctx, "validate HTTP daemon executable failed", error);
    }

    if (launch_daemon(port, directory) != 0) {
        error = errno;
        if (argc > 1) JS_FreeCString(ctx, directory);
        return promise_errno(ctx, "launch hyperfile-httpd failed", error);
    }
    if (argc > 1) JS_FreeCString(ctx, directory);
    return promise_json(ctx, "{\"ok\":true,\"running\":false,\"starting\":true}");
}

static JSValue js_stop(JSContext *ctx, JSValueConst this_val,
                       int argc, JSValueConst *argv)
{
    char response[256];
    int error;
    (void)this_val; (void)argc; (void)argv;
    if (control_request("STOP", response, sizeof(response)) == 0)
        return promise_result(ctx,
            JS_ParseJSON(ctx, response, strlen(response), "<httpd-stop>"), 0);
    error = errno;
    if (control_unavailable_error(error))
        return promise_json(ctx, "{\"ok\":true,\"running\":false}");
    return promise_errno(ctx, "control STOP failed", error);
}

static JSValue js_update_token(JSContext *ctx, JSValueConst this_val,
                               int argc, JSValueConst *argv)
{
    char token[32] = "";
    FILE *fp;
    (void)this_val; (void)argc; (void)argv;
    fp = fopen(UPDATE_TOKEN_FILE, "r");
    if (fp) {
        (void)fgets(token, sizeof(token), fp);
        fclose(fp);
        token[strcspn(token, "\r\n")] = '\0';
    }
    return promise_result(ctx, JS_NewString(ctx, token), 0);
}

static JSValue js_enable_adb(JSContext *ctx, JSValueConst this_val,
                             int argc, JSValueConst *argv)
{
    const char *markers[] = { "/tmp/.adbOn", "/tmp/.adb_auth_verified" };
    size_t i;
    (void)this_val; (void)argc; (void)argv;

    for (i = 0; i < sizeof(markers) / sizeof(markers[0]); ++i) {
        int fd = open(markers[i], O_WRONLY | O_CREAT | O_CLOEXEC, 0600);
        if (fd < 0) return promise_errno(ctx, "create ADB marker failed", errno);
        if (fsync(fd) != 0) {
            int saved = errno;
            close(fd);
            return promise_errno(ctx, "sync ADB marker failed", saved);
        }
        if (close(fd) != 0) return promise_errno(ctx, "close ADB marker failed", errno);
    }
    return promise_json(ctx, "{\"ok\":true,\"enabled\":true}");
}

static int module_init(JSContext *ctx, JSModuleDef *module)
{
    JSValue api = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, api, "initialize", JS_NewCFunction(ctx, js_initialize, "initialize", 0));
    JS_SetPropertyStr(ctx, api, "diagnose", JS_NewCFunction(ctx, js_diagnose, "diagnose", 0));
    JS_SetPropertyStr(ctx, api, "enableAdb", JS_NewCFunction(ctx, js_enable_adb, "enableAdb", 0));
    JS_SetPropertyStr(ctx, api, "updateToken", JS_NewCFunction(ctx, js_update_token, "updateToken", 0));
    JS_SetPropertyStr(ctx, api, "start", JS_NewCFunction(ctx, js_start, "start", 2));
    JS_SetPropertyStr(ctx, api, "stop", JS_NewCFunction(ctx, js_stop, "stop", 0));
    JS_SetPropertyStr(ctx, api, "status", JS_NewCFunction(ctx, js_status, "status", 0));
    JS_SetModuleExport(ctx, module, "default", api);
    return 0;
}

static JSModuleDef *module_load(JSContext *ctx, const char *name)
{
    JSModuleDef *module = JS_NewCModule(ctx, name, module_init);
    if (module) JS_AddModuleExport(ctx, module, "default");
    return module;
}

__attribute__((visibility("default"))) void custom_init_jsapis(void)
{
    registerCModuleLoader("hyperfile_http", module_load);
}
