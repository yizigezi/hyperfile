/*
 * HyperFile self updater for the Youdao X5 / Buildroot runtime.
 * Runs independently from the app and HTTP daemon.
 */
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define APP_ID "8026111800000002"
#define SERVICE_DIR "/userdisk/HyperFile"
#define UPDATER_PATH SERVICE_DIR "/hyperfile-updater"
#define PACKAGE_PATH "/tmp/hyperfile-update.amr"
#define UPDATE_LOG "/tmp/hyperfile-update.log"
#define CONTROL_SOCKET SERVICE_DIR "/httpd.sock"
#define LOCK_PATH "/tmp/hyperfile-update.lock"

static FILE *g_log;

static void log_line(const char *message)
{
    if (!g_log) return;
    fprintf(g_log, "%s\n", message);
    fflush(g_log);
    sync();
}

static int wait_child(pid_t pid, const char *label)
{
    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno == EINTR) continue;
        log_line("waitpid failed");
        return -1;
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        char msg[160];
        snprintf(msg, sizeof(msg), "%s failed (status=%d)", label, status);
        log_line(msg);
        return -1;
    }
    return 0;
}

static int run_cli(const char *verb, const char *argument)
{
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        char *const argv[] = { (char *)"miniapp_cli", (char *)verb, (char *)argument, NULL };
        setenv("PATH", "/usr/bin:/bin:/sbin:/usr/sbin", 1);
        if (g_log) {
            int fd = fileno(g_log);
            dup2(fd, STDOUT_FILENO);
            dup2(fd, STDERR_FILENO);
        }
        execvp("miniapp_cli", argv);
        _exit(127);
    }
    return wait_child(pid, verb);
}

static int stop_httpd(void)
{
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un addr;
    if (fd < 0) return -1;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", CONTROL_SOCKET);
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
        (void)send(fd, "STOP", 4, 0);
        close(fd);
    } else {
        close(fd);
    }

    for (int i = 0; i < 300; ++i) {
        if (access(CONTROL_SOCKET, F_OK) != 0) return 0;
        usleep(100000);
    }
    log_line("HTTP daemon did not stop before timeout");
    return -1;
}

static int remove_tree(const char *path)
{
    struct stat st;
    if (lstat(path, &st) != 0) return errno == ENOENT ? 0 : -1;
    if (!S_ISDIR(st.st_mode) || S_ISLNK(st.st_mode)) return unlink(path);

    DIR *dir = opendir(path);
    if (!dir) return -1;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        char child[1024];
        if (snprintf(child, sizeof(child), "%s/%s", path, entry->d_name) >= (int)sizeof(child)) {
            closedir(dir);
            errno = ENAMETOOLONG;
            return -1;
        }
        if (remove_tree(child) != 0) {
            closedir(dir);
            return -1;
        }
    }
    closedir(dir);
    return rmdir(path);
}

static int clean_service_dir(void)
{
    DIR *dir = opendir(SERVICE_DIR);
    if (!dir) return errno == ENOENT ? 0 : -1;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..") ||
            !strcmp(entry->d_name, "hyperfile-updater")) continue;
        char path[1024];
        if (snprintf(path, sizeof(path), "%s/%s", SERVICE_DIR, entry->d_name) >= (int)sizeof(path)) {
            closedir(dir);
            errno = ENAMETOOLONG;
            return -1;
        }
        if (remove_tree(path) != 0) {
            closedir(dir);
            return -1;
        }
    }
    closedir(dir);
    return 0;
}

static int finish(int code, const char *message)
{
    log_line(message);
    if (g_log) fclose(g_log);
    unlink(LOCK_PATH);
    return code;
}

int main(void)
{
    struct stat package_stat;
    if (geteuid() != 0) return 10;
    g_log = fopen(UPDATE_LOG, "a");
    if (!g_log) return 11;
    setvbuf(g_log, NULL, _IOLBF, 0);
    log_line("HyperFile updater started");

    int lock = open(LOCK_PATH, O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (lock < 0) {
        log_line("another update is already running");
        if (g_log) fclose(g_log);
        return 12;
    }
    close(lock);

    if (stat(PACKAGE_PATH, &package_stat) != 0 || !S_ISREG(package_stat.st_mode) ||
        package_stat.st_size < 1024) {
        return finish(13, "update package missing or invalid");
    }

    log_line("Stopping HTTP daemon");
    if (stop_httpd() != 0) return finish(14, "cannot stop HTTP daemon");

    log_line("Uninstalling current HyperFile app");
    if (run_cli("uninstall", APP_ID) != 0)
        return finish(15, "uninstall failed; service files were preserved");

    log_line("Cleaning HyperFile service directory (preserving updater)");
    if (clean_service_dir() != 0)
        return finish(16, "cannot clean service directory");

    log_line("Installing uploaded AMR package");
    if (run_cli("install", PACKAGE_PATH) != 0)
        return finish(17, "install failed; inspect update log and reinstall manually");

    log_line("Starting updated HyperFile app");
    if (run_cli("start", APP_ID) != 0)
        return finish(18, "start failed; app is installed but did not start");

    return finish(0, "HyperFile update completed successfully");
}
