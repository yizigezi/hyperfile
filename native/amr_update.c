#define _GNU_SOURCE
#include "amr_info.h"
#include "amr_update.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif
#ifndef O_NOFOLLOW
#define O_NOFOLLOW 0
#endif

#define UPDATE_PACKAGE "/tmp/hyperfile-update.amr"
#define UPDATE_PART "/tmp/hyperfile-update.amr.part"
#define UPDATE_LOG "/tmp/hyperfile-update.log"
#define UPDATER_PATH "/userdisk/HyperFile/hyperfile-updater"
#define UPDATE_LOCK "/tmp/hyperfile-update.lock"
#define UPDATE_PACKAGE_MAX (256 * 1024 * 1024)

static int write_all(int fd, const unsigned char *data, size_t length)
{
    size_t offset = 0;
    while (offset < length) {
        ssize_t n = write(fd, data + offset, length - offset);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) {
            if (n == 0) errno = EIO;
            return -1;
        }
        offset += (size_t)n;
    }
    return 0;
}

static int package_has_hyperfile_appid(const char *path)
{
    char *manifest = NULL;
    size_t length = 0;
    int matches;
    if (hf_amr_read_manifest(path, &manifest, &length) != 0) return -1;
    matches = hf_amr_manifest_appid_matches(manifest, length, HF_HYPERFILE_APP_ID);
    free(manifest);
    if (!matches) errno = EINVAL;
    return matches ? 0 : -1;
}

static int copy_to_staging(const char *source_path)
{
    struct stat source_stat;
    unsigned char buffer[16384];
    int in = -1, out = -1, saved = 0;
    size_t copied = 0;

    in = open(source_path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (in < 0) return -1;
    if (fstat(in, &source_stat) != 0) goto fail;
    if (!S_ISREG(source_stat.st_mode) || source_stat.st_size < 1024 ||
        source_stat.st_size > UPDATE_PACKAGE_MAX) {
        errno = EINVAL;
        goto fail;
    }

    if (unlink(UPDATE_PART) != 0 && errno != ENOENT) goto fail;
    out = open(UPDATE_PART, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if (out < 0) goto fail;

    for (;;) {
        ssize_t n = read(in, buffer, sizeof(buffer));
        if (n < 0 && errno == EINTR) continue;
        if (n < 0) goto fail;
        if (n == 0) break;
        copied += (size_t)n;
        if (copied > UPDATE_PACKAGE_MAX ||
            write_all(out, buffer, (size_t)n) != 0) {
            goto fail;
        }
    }
    if (copied != (size_t)source_stat.st_size) {
        errno = EIO;
        goto fail;
    }
    if (fsync(out) != 0) {
        goto fail;
    }
    if (close(out) != 0) {
        out = -1;
        goto fail;
    }
    out = -1;
    close(in);
    in = -1;

    if (package_has_hyperfile_appid(UPDATE_PART) != 0) goto fail;
    if (rename(UPDATE_PART, UPDATE_PACKAGE) != 0) goto fail;
    return 0;

fail:
    saved = errno ? errno : EIO;
    if (in >= 0) close(in);
    if (out >= 0) close(out);
    unlink(UPDATE_PART);
    errno = saved;
    return -1;
}

static int launch_updater(void)
{
    int pipefd[2];
    pid_t first;
    int lock_result = access(UPDATE_LOCK, F_OK);
    if (geteuid() != 0) {
        errno = EPERM;
        return -1;
    }
    if (lock_result == 0) {
        errno = EBUSY;
        return -1;
    }
    if (errno != ENOENT) return -1;
    if (pipe(pipefd) != 0) return -1;
    (void)fcntl(pipefd[1], F_SETFD, FD_CLOEXEC);
    first = fork();
    if (first < 0) {
        int saved = errno;
        close(pipefd[0]);
        close(pipefd[1]);
        errno = saved;
        return -1;
    }
    if (first == 0) {
        close(pipefd[0]);
        if (setsid() < 0) {
            int saved = errno;
            (void)write(pipefd[1], &saved, sizeof(saved));
            _exit(126);
        }
        pid_t second = fork();
        if (second < 0) {
            int saved = errno;
            (void)write(pipefd[1], &saved, sizeof(saved));
            _exit(126);
        }
        if (second > 0) _exit(0);
        int logfd = open(UPDATE_LOG, O_WRONLY | O_CREAT | O_APPEND, 0600);
        if (logfd >= 0) {
            (void)dup2(logfd, STDOUT_FILENO);
            (void)dup2(logfd, STDERR_FILENO);
            if (logfd > STDERR_FILENO) close(logfd);
        }
        execl(UPDATER_PATH, UPDATER_PATH, (char *)NULL);
        int saved = errno;
        (void)write(pipefd[1], &saved, sizeof(saved));
        _exit(127);
    }
    close(pipefd[1]);
    int status = 0;
    while (waitpid(first, &status, 0) < 0 && errno == EINTR) {}
    int child_errno = 0;
    ssize_t n;
    do { n = read(pipefd[0], &child_errno, sizeof(child_errno)); }
    while (n < 0 && errno == EINTR);
    close(pipefd[0]);
    if (n > 0) {
        errno = child_errno;
        return -1;
    }
    if (n < 0) return -1;
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        errno = ECHILD;
        return -1;
    }
    return 0;
}

int hf_amr_start_update_from_path(const char *source_path)
{
    int lock_result;
    if (!source_path || !source_path[0]) {
        errno = EINVAL;
        return -1;
    }
    lock_result = access(UPDATE_LOCK, F_OK);
    if (lock_result == 0) {
        errno = EBUSY;
        return -1;
    }
    if (errno != ENOENT) return -1;
    if (package_has_hyperfile_appid(source_path) != 0) return -1;
    if (copy_to_staging(source_path) != 0) return -1;
    if (launch_updater() != 0) {
        int saved = errno;
        unlink(UPDATE_PACKAGE);
        errno = saved;
        return -1;
    }
    return 0;
}

int hf_amr_start_staged_update(void)
{
    struct stat st;
    int lock_result = access(UPDATE_LOCK, F_OK);
    if (lock_result == 0) {
        errno = EBUSY;
        return -1;
    }
    if (errno != ENOENT) return -1;
    if (stat(UPDATE_PACKAGE, &st) != 0) return -1;
    if (!S_ISREG(st.st_mode) || st.st_size < 1024 ||
        st.st_size > UPDATE_PACKAGE_MAX) {
        errno = EINVAL;
        return -1;
    }
    if (package_has_hyperfile_appid(UPDATE_PACKAGE) != 0) return -1;
    return launch_updater();
}
