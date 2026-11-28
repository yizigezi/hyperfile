#ifndef HYPERFILE_AMR_INFO_H
#define HYPERFILE_AMR_INFO_H

#include <stddef.h>

#define HF_AMR_MANIFEST_MAX 65536
#define HF_HYPERFILE_APP_ID "8026111800000002"

int hf_amr_read_manifest(const char *path, char **manifest, size_t *length);
int hf_amr_manifest_appid_matches(const char *manifest, size_t length,
                                 const char *appid);

#endif
