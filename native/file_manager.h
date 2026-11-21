#ifndef HYPERFILE_FILE_MANAGER_H
#define HYPERFILE_FILE_MANAGER_H
#include <stddef.h>
int hf_fm_list(const char *root, const char *relative, char *json, size_t capacity);
int hf_fm_mkdir(const char *root, const char *relative);
int hf_fm_delete(const char *root, const char *relative);
int hf_fm_copy(const char *root, const char *source, const char *destination);
int hf_fm_resolve_dir(const char *root, const char *relative, char *out, size_t capacity);
#endif
