#define _GNU_SOURCE
#include "file_manager.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <limits.h>

static int valid_relative(const char *rel)
{
    const char *p = rel ? rel : "";
    if (*p == '/' || strchr(p, '\\')) return 0;
    while (*p) {
        const char *start = p;
        while (*p && *p != '/') {
            unsigned char c = (unsigned char)*p;
            if (c < 0x20 || c == 0x7f) return 0;
            ++p;
        }
        size_t n = (size_t)(p - start);
        if ((n == 1 && start[0] == '.') || (n == 2 && start[0] == '.' && start[1] == '.')) return 0;
        if (*p == '/') ++p;
    }
    return 1;
}

/* Resolve a relative path while rejecting symlinks in every existing component. */
static int resolve_path(const char *root, const char *relative, char *out, size_t cap, int allow_missing)
{
    struct stat st;
    size_t used;
    const char *p;
    if (!root || !relative || !valid_relative(relative)) { errno = EINVAL; return -1; }
    if (snprintf(out, cap, "%s", root) >= (int)cap) { errno = ENAMETOOLONG; return -1; }
    used = strlen(out);
    p = relative;
    while (*p) {
        const char *start = p;
        size_t n;
        while (*p && *p != '/') ++p;
        n = (size_t)(p - start);
        if (used + n + 2 > cap) { errno = ENAMETOOLONG; return -1; }
        out[used++] = '/'; memcpy(out + used, start, n); used += n; out[used] = 0;
        if (lstat(out, &st) == 0) {
            if (S_ISLNK(st.st_mode)) { errno = ELOOP; return -1; }
        } else if (!(allow_missing && errno == ENOENT && *p == 0)) {
            return -1;
        }
        if (*p == '/') ++p;
    }
    return 0;
}

int hf_fm_resolve_dir(const char *root, const char *relative, char *out, size_t cap)
{
    struct stat st;
    if (resolve_path(root, relative, out, cap, 0) != 0) return -1;
    if (stat(out, &st) != 0) return -1;
    if (!S_ISDIR(st.st_mode)) { errno = ENOTDIR; return -1; }
    return 0;
}

static int append(char *out, size_t cap, size_t *used, const char *s)
{
    size_t n = strlen(s);
    if (*used + n >= cap) { errno = ENOSPC; return -1; }
    memcpy(out + *used, s, n); *used += n; out[*used] = 0; return 0;
}

static int json_string(char *out, size_t cap, size_t *used, const char *s)
{
    if (append(out, cap, used, "\\"") != 0) return -1;
    for (; *s; ++s) {
        char tmp[7];
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\\\') { tmp[0] = '\\\\'; tmp[1] = (char)c; tmp[2] = 0; }
        else if (c < 0x20) { snprintf(tmp, sizeof(tmp), "\\\\u%04x", c); }
        else { tmp[0] = (char)c; tmp[1] = 0; }
        if (append(out, cap, used, tmp) != 0) return -1;
    }
    return append(out, cap, used, "\\"");
}

int hf_fm_list(const char *root, const char *relative, char *json, size_t cap)
{
    char dirpath[PATH_MAX]; DIR *dir; struct dirent *ent; size_t used = 0; int first = 1;
    if (hf_fm_resolve_dir(root, relative, dirpath, sizeof(dirpath)) != 0) return -1;
    dir = opendir(dirpath); if (!dir) return -1;
    json[0] = 0;
    if (append(json, cap, &used, "{\\"ok\\":true,\\"path\\":") != 0 ||
        json_string(json, cap, &used, relative) != 0 ||
        append(json, cap, &used, ",\\"entries\\":[") != 0) { closedir(dir); return -1; }
    while ((ent = readdir(dir)) != NULL) {
        char full[PATH_MAX]; struct stat st; char item[64];
        if (!strcmp(ent->d_name, ".") || !strcmp(ent->d_name, "..")) continue;
        if (snprintf(full, sizeof(full), "%s/%s", dirpath, ent->d_name) >= (int)sizeof(full)) continue;
        if (lstat(full, &st) != 0 || S_ISLNK(st.st_mode)) continue;
        if (!first && append(json, cap, &used, ",") != 0) { closedir(dir); return -1; }
        first = 0;
        if (append(json, cap, &used, "{\\"name\\":") != 0 ||
            json_string(json, cap, &used, ent->d_name) != 0) { closedir(dir); return -1; }
        snprintf(item, sizeof(item), ",\\"directory\\":%s,\\"size\\":%lld}",
                 S_ISDIR(st.st_mode) ? "true" : "false", (long long)st.st_size);
        if (append(json, cap, &used, item) != 0) { closedir(dir); return -1; }
    }
    closedir(dir);
    return append(json, cap, &used, "]}");
}

int hf_fm_mkdir(const char *root, const char *relative)
{
    char path[PATH_MAX];
    if (!relative || !*relative || resolve_path(root, relative, path, sizeof(path), 1) != 0) return -1;
    return mkdir(path, 0755);
}

static int remove_tree(const char *path)
{
    struct stat st;
    if (lstat(path, &st) != 0) return -1;
    if (S_ISDIR(st.st_mode)) {
        DIR *d = opendir(path); struct dirent *e;
        if (!d) return -1;
        while ((e = readdir(d)) != NULL) {
            char child[PATH_MAX];
            if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
            if (snprintf(child, sizeof(child), "%s/%s", path, e->d_name) >= (int)sizeof(child)) { closedir(d); errno = ENAMETOOLONG; return -1; }
            if (remove_tree(child) != 0) { closedir(d); return -1; }
        }
        closedir(d); return rmdir(path);
    }
    return unlink(path);
}

int hf_fm_delete(const char *root, const char *relative)
{
    char path[PATH_MAX];
    if (!relative || !*relative || resolve_path(root, relative, path, sizeof(path), 0) != 0) return -1;
    if (!strcmp(path, root)) { errno = EBUSY; return -1; }
    return remove_tree(path);
}

static int copy_file(const char *src, const char *dst, mode_t mode)
{
    int in = open(src, O_RDONLY), out = -1; char buf[16384]; ssize_t n;
    if (in < 0) return -1;
    out = open(dst, O_WRONLY | O_CREAT | O_EXCL, mode & 0777);
    if (out < 0) { close(in); return -1; }
    while ((n = read(in, buf, sizeof(buf))) > 0) {
        ssize_t off = 0;
        while (off < n) { ssize_t w = write(out, buf + off, (size_t)(n - off)); if (w <= 0) { close(in); close(out); unlink(dst); return -1; } off += w; }
    }
    close(in);
    if (n < 0) { close(out); unlink(dst); return -1; }
    if (close(out) != 0) { unlink(dst); return -1; }
    return 0;
}

static int copy_tree(const char *src, const char *dst)
{
    struct stat st;
    if (lstat(src, &st) != 0 || S_ISLNK(st.st_mode)) { errno = EINVAL; return -1; }
    if (S_ISREG(st.st_mode)) return copy_file(src, dst, st.st_mode);
    if (!S_ISDIR(st.st_mode)) { errno = EINVAL; return -1; }
    if (mkdir(dst, st.st_mode & 0777) != 0) return -1;
    DIR *d = opendir(src); struct dirent *e;
    if (!d) { rmdir(dst); return -1; }
    while ((e = readdir(d)) != NULL) {
        char a[PATH_MAX], b[PATH_MAX];
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        if (snprintf(a, sizeof(a), "%s/%s", src, e->d_name) >= (int)sizeof(a) ||
            snprintf(b, sizeof(b), "%s/%s", dst, e->d_name) >= (int)sizeof(b) ||
            copy_tree(a, b) != 0) { closedir(d); return -1; }
    }
    closedir(d); return 0;
}

int hf_fm_copy(const char *root, const char *source, const char *destination)
{
    char src[PATH_MAX], dst[PATH_MAX];
    if (!source || !*source || !destination || !*destination ||
        resolve_path(root, source, src, sizeof(src), 0) != 0 ||
        resolve_path(root, destination, dst, sizeof(dst), 1) != 0) return -1;
    if (!strcmp(src, dst) || !strncmp(dst, src, strlen(src)) && dst[strlen(src)] == '/') { errno = EINVAL; return -1; }
    return copy_tree(src, dst);
}
