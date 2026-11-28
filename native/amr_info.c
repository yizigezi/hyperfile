#define _GNU_SOURCE
#include "amr_info.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef O_NOFOLLOW
#define O_NOFOLLOW 0
#endif

#define ZIP_EOCD_SIZE 22
#define ZIP_MAX_COMMENT 65535
#define AMR_COMPRESSED_MAX (4 * 1024 * 1024)

static uint16_t read_u16(const unsigned char *p)
{
    return (uint16_t)p[0] | (uint16_t)((uint16_t)p[1] << 8);
}

static uint32_t read_u32(const unsigned char *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void write_u32(unsigned char *p, uint32_t value)
{
    p[0] = (unsigned char)value;
    p[1] = (unsigned char)(value >> 8);
    p[2] = (unsigned char)(value >> 16);
    p[3] = (unsigned char)(value >> 24);
}

static int read_at(int fd, void *buffer, size_t length, uint64_t offset)
{
    unsigned char *p = buffer;
    size_t done = 0;
    while (done < length) {
        ssize_t n = pread(fd, p + done, length - done, (off_t)(offset + done));
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) {
            if (n == 0) errno = EIO;
            return -1;
        }
        done += (size_t)n;
    }
    return 0;
}

static int write_all(int fd, const void *buffer, size_t length)
{
    const unsigned char *p = buffer;
    size_t done = 0;
    while (done < length) {
        ssize_t n = write(fd, p + done, length - done);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) {
            if (n == 0) errno = EIO;
            return -1;
        }
        done += (size_t)n;
    }
    return 0;
}

static uint32_t manifest_crc32(const unsigned char *data, size_t length)
{
    uint32_t crc = 0xffffffffU;
    for (size_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++)
            crc = (crc >> 1) ^ (0xedb88320U & (uint32_t)-(int32_t)(crc & 1));
    }
    return ~crc;
}

static int find_manifest(int fd, uint64_t file_size, uint64_t *data_offset,
                         uint32_t *compressed_size, uint32_t *uncompressed_size,
                         uint32_t *crc, uint16_t *method)
{
    size_t tail_size = file_size < ZIP_EOCD_SIZE + ZIP_MAX_COMMENT
                           ? (size_t)file_size
                           : ZIP_EOCD_SIZE + ZIP_MAX_COMMENT;
    unsigned char *tail = malloc(tail_size);
    uint64_t eocd_offset = 0, central_offset, central_size, central_end;
    uint16_t entry_count = 0;
    int found = 0;

    if (!tail) return -1;
    if (read_at(fd, tail, tail_size, file_size - tail_size) != 0) {
        free(tail);
        return -1;
    }
    if (tail_size < ZIP_EOCD_SIZE) {
        free(tail);
        errno = EINVAL;
        return -1;
    }

    for (size_t i = tail_size - ZIP_EOCD_SIZE + 1; i-- > 0;) {
        if (read_u32(tail + i) == 0x06054b50U &&
            i + ZIP_EOCD_SIZE + read_u16(tail + i + 20) == tail_size) {
            eocd_offset = file_size - tail_size + i;
            entry_count = read_u16(tail + i + 10);
            if (read_u16(tail + i + 4) != 0 || read_u16(tail + i + 6) != 0 ||
                read_u16(tail + i + 8) != entry_count) {
                free(tail);
                errno = ENOTSUP;
                return -1;
            }
            central_size = read_u32(tail + i + 12);
            central_offset = read_u32(tail + i + 16);
            found = 1;
            break;
        }
    }
    free(tail);
    if (!found || central_offset > file_size || central_size > file_size - central_offset) {
        errno = EINVAL;
        return -1;
    }
    central_end = central_offset + central_size;
    if (central_end > eocd_offset) {
        errno = EINVAL;
        return -1;
    }

    uint64_t cursor = central_offset;
    for (uint32_t i = 0; i < entry_count; i++) {
        unsigned char header[46], local[30];
        uint16_t flags, entry_method, name_length, extra_length, comment_length;
        uint32_t comp_size, uncomp_size, local_offset;
        uint64_t record_end;
        char name[14];

        if (cursor > central_end || central_end - cursor < sizeof(header) ||
            read_at(fd, header, sizeof(header), cursor) != 0) {
            errno = EINVAL;
            return -1;
        }
        if (read_u32(header) != 0x02014b50U) {
            errno = EINVAL;
            return -1;
        }
        flags = read_u16(header + 8);
        entry_method = read_u16(header + 10);
        comp_size = read_u32(header + 20);
        uncomp_size = read_u32(header + 24);
        name_length = read_u16(header + 28);
        extra_length = read_u16(header + 30);
        comment_length = read_u16(header + 32);
        local_offset = read_u32(header + 42);
        record_end = cursor + sizeof(header) + name_length + extra_length + comment_length;
        if (record_end > central_end) {
            errno = EINVAL;
            return -1;
        }

        if (name_length == 13) {
            if (read_at(fd, name, name_length, cursor + sizeof(header)) != 0) return -1;
            if (memcmp(name, "manifest.json", 13) == 0) {
                if (found == 2) {
                    errno = EINVAL;
                    return -1;
                }
                found = 2;
                if ((flags & (1U | 0x40U)) != 0 ||
                    comp_size == UINT32_MAX || uncomp_size == UINT32_MAX ||
                    local_offset == UINT32_MAX ||
                    comp_size > AMR_COMPRESSED_MAX ||
                    uncomp_size > HF_AMR_MANIFEST_MAX ||
                    local_offset > file_size || file_size - local_offset < sizeof(local)) {
                    errno = EINVAL;
                    return -1;
                }
                if (entry_method != 0 && entry_method != 8) {
                    errno = ENOTSUP;
                    return -1;
                }
                if (read_at(fd, local, sizeof(local), local_offset) != 0 ||
                    read_u32(local) != 0x04034b50U ||
                    read_u16(local + 8) != entry_method) {
                    errno = EINVAL;
                    return -1;
                }
                uint16_t local_name_length = read_u16(local + 26);
                uint16_t local_extra_length = read_u16(local + 28);
                uint64_t local_data = (uint64_t)local_offset + sizeof(local) +
                                      local_name_length + local_extra_length;
                if (local_name_length != name_length ||
                    local_data > file_size || comp_size > file_size - local_data) {
                    errno = EINVAL;
                    return -1;
                }
                char local_name[13];
                if (read_at(fd, local_name, sizeof(local_name),
                            (uint64_t)local_offset + sizeof(local)) != 0 ||
                    memcmp(local_name, "manifest.json", sizeof(local_name)) != 0) {
                    errno = EINVAL;
                    return -1;
                }
                *data_offset = local_data;
                *compressed_size = comp_size;
                *uncompressed_size = uncomp_size;
                *crc = read_u32(header + 16);
                *method = entry_method;
            }
        }
        cursor = record_end;
    }
    if (found != 2) {
        errno = ENOENT;
        return -1;
    }
    return 0;
}

static int inflate_with_gunzip(FILE *gzip_file, unsigned char *output,
                               size_t expected_size, size_t *output_size)
{
    int output_pipe[2], exec_pipe[2], saved_error = 0, status = 0;
    pid_t child;
    size_t used = 0;

    if (fflush(gzip_file) != 0 || fseek(gzip_file, 0, SEEK_SET) != 0) return -1;
    if (pipe(output_pipe) != 0) return -1;
    if (pipe(exec_pipe) != 0) {
        int saved = errno;
        close(output_pipe[0]);
        close(output_pipe[1]);
        errno = saved;
        return -1;
    }
    if (fcntl(exec_pipe[1], F_SETFD, FD_CLOEXEC) != 0) {
        int saved = errno;
        close(output_pipe[0]);
        close(output_pipe[1]);
        close(exec_pipe[0]);
        close(exec_pipe[1]);
        errno = saved;
        return -1;
    }

    child = fork();
    if (child < 0) {
        int saved = errno;
        close(output_pipe[0]);
        close(output_pipe[1]);
        close(exec_pipe[0]);
        close(exec_pipe[1]);
        errno = saved;
        return -1;
    }
    if (child == 0) {
        int error;
        close(output_pipe[0]);
        close(exec_pipe[0]);
        if (dup2(fileno(gzip_file), STDIN_FILENO) < 0 ||
            dup2(output_pipe[1], STDOUT_FILENO) < 0) {
            error = errno;
            if (write_all(exec_pipe[1], &error, sizeof(error)) != 0) _exit(126);
            _exit(126);
        }
        close(output_pipe[1]);
        execlp("gunzip", "gunzip", "-c", (char *)NULL);
        error = errno;
        if (write_all(exec_pipe[1], &error, sizeof(error)) != 0) _exit(127);
        _exit(127);
    }

    close(output_pipe[1]);
    close(exec_pipe[1]);
    size_t exec_bytes = 0;
    while (exec_bytes < sizeof(saved_error)) {
        ssize_t n = read(exec_pipe[0], (unsigned char *)&saved_error + exec_bytes,
                         sizeof(saved_error) - exec_bytes);
        if (n < 0 && errno == EINTR) continue;
        if (n < 0) {
            int saved = errno;
            close(exec_pipe[0]);
            close(output_pipe[0]);
            (void)waitpid(child, &status, 0);
            errno = saved;
            return -1;
        }
        if (n == 0) break;
        exec_bytes += (size_t)n;
    }
    close(exec_pipe[0]);
    if (exec_bytes != 0) {
        if (exec_bytes != sizeof(saved_error)) {
            close(output_pipe[0]);
            while (waitpid(child, &status, 0) < 0 && errno == EINTR) {}
            errno = EIO;
            return -1;
        }
        close(output_pipe[0]);
        (void)waitpid(child, &status, 0);
        errno = saved_error;
        return -1;
    }

    for (;;) {
        unsigned char buffer[4096];
        ssize_t n = read(output_pipe[0], buffer, sizeof(buffer));
        if (n < 0 && errno == EINTR) continue;
        if (n < 0) {
            int saved = errno;
            close(output_pipe[0]);
            while (waitpid(child, &status, 0) < 0 && errno == EINTR) {}
            errno = saved;
            return -1;
        }
        if (n == 0) break;
        if ((size_t)n > expected_size - used) {
            close(output_pipe[0]);
            (void)kill(child, SIGKILL);
            while (waitpid(child, &status, 0) < 0 && errno == EINTR) {}
            errno = EFBIG;
            return -1;
        }
        memcpy(output + used, buffer, (size_t)n);
        used += (size_t)n;
    }
    close(output_pipe[0]);
    pid_t waited;
    do { waited = waitpid(child, &status, 0); } while (waited < 0 && errno == EINTR);
    if (waited < 0 && errno != ECHILD) return -1;
    if ((waited > 0 && (!WIFEXITED(status) || WEXITSTATUS(status) != 0)) ||
        used != expected_size) {
        errno = EBADMSG;
        return -1;
    }
    *output_size = used;
    return 0;
}

int hf_amr_read_manifest(const char *path, char **manifest, size_t *length)
{
    static const unsigned char gzip_header[10] =
        { 0x1f, 0x8b, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff };
    struct stat st;
    uint64_t data_offset;
    uint32_t compressed_size, uncompressed_size, expected_crc;
    uint16_t method;
    unsigned char *result = NULL;
    int fd = -1, saved;

    if (!path || !manifest || !length) {
        errno = EINVAL;
        return -1;
    }
    *manifest = NULL;
    *length = 0;
    fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) return -1;
    if (fstat(fd, &st) != 0) {
        saved = errno;
        close(fd);
        errno = saved;
        return -1;
    }

    if (!S_ISREG(st.st_mode) || st.st_size < ZIP_EOCD_SIZE) {
        close(fd);
        errno = EINVAL;
        return -1;
    }
    if (find_manifest(fd, (uint64_t)st.st_size, &data_offset,
                      &compressed_size, &uncompressed_size, &expected_crc, &method) != 0) {
        saved = errno;
        close(fd);
        errno = saved;
        return -1;
    }

    result = malloc((size_t)uncompressed_size + 1);
    if (!result) {
        close(fd);
        return -1;
    }

    if (method == 0) {
        if (compressed_size != uncompressed_size ||
            read_at(fd, result, uncompressed_size, data_offset) != 0) {
            saved = errno ? errno : EINVAL;
            goto fail;
        }
    } else {
        FILE *gzip_file = tmpfile();
        unsigned char trailer[8], buffer[8192];
        uint32_t remaining = compressed_size;
        uint64_t offset = data_offset;
        size_t inflated_size = 0;
        if (!gzip_file) {
            saved = errno;
            goto fail;
        }
        write_u32(trailer, expected_crc);
        write_u32(trailer + 4, uncompressed_size);
        if (write_all(fileno(gzip_file), gzip_header, sizeof(gzip_header)) != 0) {
            saved = errno;
            fclose(gzip_file);
            goto fail;
        }
        while (remaining > 0) {
            size_t amount = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
            if (read_at(fd, buffer, amount, offset) != 0 ||
                write_all(fileno(gzip_file), buffer, amount) != 0) {
                saved = errno;
                fclose(gzip_file);
                goto fail;
            }
            remaining -= (uint32_t)amount;
            offset += amount;
        }
        if (write_all(fileno(gzip_file), trailer, sizeof(trailer)) != 0 ||
            inflate_with_gunzip(gzip_file, (unsigned char *)result,
                                uncompressed_size, &inflated_size) != 0) {
            saved = errno;
            fclose(gzip_file);
            goto fail;
        }
        fclose(gzip_file);
        if (inflated_size != uncompressed_size) {
            saved = EBADMSG;
            goto fail;
        }
    }
    close(fd);
    result[uncompressed_size] = '\0';
    if (manifest_crc32((unsigned char *)result, uncompressed_size) != expected_crc) {
        free(result);
        errno = EBADMSG;
        return -1;
    }
    *manifest = (char *)result;
    *length = uncompressed_size;
    return 0;

fail:
    if (fd >= 0) close(fd);
    free(result);
    errno = saved;
    return -1;
}

static void json_skip_whitespace(const char *json, size_t length, size_t *offset)
{
    while (*offset < length &&
           (json[*offset] == ' ' || json[*offset] == '\t' ||
            json[*offset] == '\r' || json[*offset] == '\n'))
        (*offset)++;
}

static int json_append_codepoint(char *out, size_t capacity, size_t *used,
                                 uint32_t codepoint, int *overflow)
{
    unsigned char encoded[4];
    size_t count;
    if (codepoint <= 0x7f) {
        encoded[0] = (unsigned char)codepoint;
        count = 1;
    } else if (codepoint <= 0x7ff) {
        encoded[0] = (unsigned char)(0xc0 | (codepoint >> 6));
        encoded[1] = (unsigned char)(0x80 | (codepoint & 0x3f));
        count = 2;
    } else if (codepoint <= 0xffff) {
        encoded[0] = (unsigned char)(0xe0 | (codepoint >> 12));
        encoded[1] = (unsigned char)(0x80 | ((codepoint >> 6) & 0x3f));
        encoded[2] = (unsigned char)(0x80 | (codepoint & 0x3f));
        count = 3;
    } else if (codepoint <= 0x10ffff) {
        encoded[0] = (unsigned char)(0xf0 | (codepoint >> 18));
        encoded[1] = (unsigned char)(0x80 | ((codepoint >> 12) & 0x3f));
        encoded[2] = (unsigned char)(0x80 | ((codepoint >> 6) & 0x3f));
        encoded[3] = (unsigned char)(0x80 | (codepoint & 0x3f));
        count = 4;
    } else {
        return -1;
    }

    if (out && capacity > 0) {
        if (count > capacity - 1 - (*used < capacity ? *used : capacity - 1)) {
            *overflow = 1;
        } else {
            memcpy(out + *used, encoded, count);
        }
    }
    *used += count;
    return 0;
}

static int json_hex_quad(const char *json, size_t length, size_t *offset,
                         uint32_t *value)
{
    uint32_t result = 0;
    for (int i = 0; i < 4; i++) {
        unsigned char c;
        if (*offset >= length) return -1;
        c = (unsigned char)json[(*offset)++];
        if (c >= '0' && c <= '9') result = (result << 4) | (c - '0');
        else if (c >= 'a' && c <= 'f') result = (result << 4) | (c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') result = (result << 4) | (c - 'A' + 10);
        else return -1;
    }
    *value = result;
    return 0;
}

static int json_parse_string(const char *json, size_t length, size_t *offset,
                             char *out, size_t capacity, int *overflow)
{
    size_t used = 0;
    if (*offset >= length || json[(*offset)++] != '"') return -1;
    if (overflow) *overflow = 0;

    while (*offset < length) {
        uint32_t codepoint;
        unsigned char c = (unsigned char)json[(*offset)++];
        if (c == '"') {
            if (out && capacity > 0)
                out[*overflow ? capacity - 1 : used] = '\0';
            return 0;
        }
        if (c < 0x20) return -1;
        if (c == '\\') {
            if (*offset >= length) return -1;
            c = (unsigned char)json[(*offset)++];
            switch (c) {
                case '"': codepoint = '"'; break;
                case '\\': codepoint = '\\'; break;
                case '/': codepoint = '/'; break;
                case 'b': codepoint = '\b'; break;
                case 'f': codepoint = '\f'; break;
                case 'n': codepoint = '\n'; break;
                case 'r': codepoint = '\r'; break;
                case 't': codepoint = '\t'; break;
                case 'u':
                    if (json_hex_quad(json, length, offset, &codepoint) != 0)
                        return -1;
                    if (codepoint >= 0xd800 && codepoint <= 0xdbff) {
                        uint32_t low;
                        if (*offset + 2 > length || json[*offset] != '\\' ||
                            json[*offset + 1] != 'u')
                            return -1;
                        *offset += 2;
                        if (json_hex_quad(json, length, offset, &low) != 0 ||
                            low < 0xdc00 || low > 0xdfff)
                            return -1;
                        codepoint = 0x10000 + ((codepoint - 0xd800) << 10) +
                                    (low - 0xdc00);
                    } else if (codepoint >= 0xdc00 && codepoint <= 0xdfff) {
                        return -1;
                    }
                    break;
                default: return -1;
            }
        } else {
            codepoint = c;
        }
        if (json_append_codepoint(out, capacity, &used, codepoint,
                                  overflow ? overflow : &(int){0}) != 0)
            return -1;
    }
    return -1;
}

static int json_skip_number(const char *json, size_t length, size_t *offset)
{
    size_t i = *offset;
    if (i < length && json[i] == '-') i++;
    if (i >= length) return -1;
    if (json[i] == '0') {
        i++;
    } else if (json[i] >= '1' && json[i] <= '9') {
        do { i++; } while (i < length && json[i] >= '0' && json[i] <= '9');
    } else {
        return -1;
    }
    if (i < length && json[i] == '.') {
        i++;
        if (i >= length || json[i] < '0' || json[i] > '9') return -1;
        do { i++; } while (i < length && json[i] >= '0' && json[i] <= '9');
    }
    if (i < length && (json[i] == 'e' || json[i] == 'E')) {
        i++;
        if (i < length && (json[i] == '+' || json[i] == '-')) i++;
        if (i >= length || json[i] < '0' || json[i] > '9') return -1;
        do { i++; } while (i < length && json[i] >= '0' && json[i] <= '9');
    }
    *offset = i;
    return 0;
}

static int json_skip_value(const char *json, size_t length, size_t *offset,
                           unsigned depth)
{
    if (depth > 64) return -1;
    json_skip_whitespace(json, length, offset);
    if (*offset >= length) return -1;

    if (json[*offset] == '"') {
        int overflow;
        return json_parse_string(json, length, offset, NULL, 0, &overflow);
    }
    if (json[*offset] == '{') {
        (*offset)++;
        json_skip_whitespace(json, length, offset);
        if (*offset < length && json[*offset] == '}') {
            (*offset)++;
            return 0;
        }
        for (;;) {
            int overflow;
            char ignored[1];
            if (json_parse_string(json, length, offset, ignored,
                                  sizeof(ignored), &overflow) != 0)
                return -1;
            json_skip_whitespace(json, length, offset);
            if (*offset >= length || json[(*offset)++] != ':') return -1;
            if (json_skip_value(json, length, offset, depth + 1) != 0)
                return -1;
            json_skip_whitespace(json, length, offset);
            if (*offset >= length) return -1;
            if (json[*offset] == '}') {
                (*offset)++;
                return 0;
            }
            if (json[(*offset)++] != ',') return -1;
            json_skip_whitespace(json, length, offset);
        }
    }
    if (json[*offset] == '[') {
        (*offset)++;
        json_skip_whitespace(json, length, offset);
        if (*offset < length && json[*offset] == ']') {
            (*offset)++;
            return 0;
        }
        for (;;) {
            if (json_skip_value(json, length, offset, depth + 1) != 0)
                return -1;
            json_skip_whitespace(json, length, offset);
            if (*offset >= length) return -1;
            if (json[*offset] == ']') {
                (*offset)++;
                return 0;
            }
            if (json[(*offset)++] != ',') return -1;
            json_skip_whitespace(json, length, offset);
        }
    }

    if (length - *offset >= 4 && memcmp(json + *offset, "true", 4) == 0) {
        *offset += 4;
        return 0;
    }
    if (length - *offset >= 5 && memcmp(json + *offset, "false", 5) == 0) {
        *offset += 5;
        return 0;
    }
    if (length - *offset >= 4 && memcmp(json + *offset, "null", 4) == 0) {
        *offset += 4;
        return 0;
    }
    return json_skip_number(json, length, offset);
}

int hf_amr_manifest_appid_matches(const char *manifest, size_t length,
                                  const char *appid)
{
    size_t offset = 0;
    int found = 0, matches = 0;
    if (!manifest || !appid) return 0;
    json_skip_whitespace(manifest, length, &offset);
    if (offset >= length || manifest[offset++] != '{') return 0;

    for (;;) {
        char key[64], value[64];
        int key_overflow, value_overflow;
        json_skip_whitespace(manifest, length, &offset);
        if (offset >= length) return 0;
        if (manifest[offset] == '}') {
            offset++;
            break;
        }
        if (json_parse_string(manifest, length, &offset, key, sizeof(key),
                              &key_overflow) != 0)
            return 0;
        json_skip_whitespace(manifest, length, &offset);
        if (offset >= length || manifest[offset++] != ':') return 0;
        json_skip_whitespace(manifest, length, &offset);

        if (!key_overflow && strcmp(key, "appid") == 0) {
            if (found) return 0;
            found = 1;
            if (json_parse_string(manifest, length, &offset, value,
                                  sizeof(value), &value_overflow) != 0)
                return 0;
            matches = !value_overflow && strcmp(value, appid) == 0;
        } else if (json_skip_value(manifest, length, &offset, 1) != 0) {
            return 0;
        }

        json_skip_whitespace(manifest, length, &offset);
        if (offset >= length) return 0;
        if (manifest[offset] == '}') {
            offset++;
            break;
        }
        if (manifest[offset++] != ',') return 0;
        json_skip_whitespace(manifest, length, &offset);
        if (offset >= length || manifest[offset] == '}') return 0;
    }
    json_skip_whitespace(manifest, length, &offset);
    return found && offset == length && matches;
}
