/*
 * HyperFile Bluetooth OPP (OBEX Object Push) receiver.
 * RFCOMM channel 9; SDP record is registered through BlueZ sdptool.
 */
#define _GNU_SOURCE
#include "quickjs.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef AF_BLUETOOTH
#define AF_BLUETOOTH 31
#endif
#ifndef BTPROTO_RFCOMM
#define BTPROTO_RFCOMM 3
#endif
#define OPP_CHANNEL 9
#define OPP_MAX_FILE (1024ULL * 1024ULL * 1024ULL)
#define OPP_MAX_PACKET 65535

typedef struct { sa_family_t family; uint8_t address[6]; uint8_t channel; } hf_sockaddr_rc;
typedef JSModuleDef *(*LoadCModuleFunction)(JSContext *, const char *);
extern void registerCModuleLoader(const char *, LoadCModuleFunction);

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_t g_thread;
static int g_running, g_stop, g_listener = -1, g_sdp_registered;
static unsigned long g_received;
static char g_dir[512] = "/userdisk/Favorite/HyperFile";
static char g_last[256] = "";

static int read_exact(int fd, void *buffer, size_t length) {
  unsigned char *p = (unsigned char *)buffer;
  while (length) {
    ssize_t n = recv(fd, p, length, 0);
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0) return -1;
    p += n; length -= (size_t)n;
  }
  return 0;
}
static int write_exact(int fd, const void *buffer, size_t length) {
  const unsigned char *p = (const unsigned char *)buffer;
  while (length) {
    ssize_t n = send(fd, p, length, 0);
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0) return -1;
    p += n; length -= (size_t)n;
  }
  return 0;
}
static int send_response(int fd, unsigned char code, const unsigned char *extra, size_t extra_len) {
  unsigned char packet[16];
  size_t length = 3 + extra_len;
  if (length > sizeof(packet)) return -1;
  packet[0] = code; packet[1] = (unsigned char)(length >> 8); packet[2] = (unsigned char)length;
  if (extra_len) memcpy(packet + 3, extra, extra_len);
  return write_exact(fd, packet, length);
}
static int read_packet(int fd, unsigned char **out, size_t *out_len) {
  unsigned char head[3]; unsigned int length; unsigned char *packet;
  if (read_exact(fd, head, 3)) return -1;
  length = ((unsigned int)head[1] << 8) | head[2];
  if (length < 3 || length > OPP_MAX_PACKET) return -1;
  packet = (unsigned char *)malloc(length);
  if (!packet) return -1;
  memcpy(packet, head, 3);
  if (length > 3 && read_exact(fd, packet + 3, length - 3)) { free(packet); return -1; }
  *out = packet; *out_len = length; return 0;
}
static int utf16be_to_utf8(const unsigned char *src, size_t bytes, char *out, size_t cap) {
  size_t i = 0, n = 0;
  while (i + 1 < bytes) {
    uint32_t cp = ((uint32_t)src[i] << 8) | src[i + 1]; i += 2;
    if (cp == 0) break;
    if (cp >= 0xD800 && cp <= 0xDBFF) {
      uint32_t low;
      if (i + 1 >= bytes) return -1;
      low = ((uint32_t)src[i] << 8) | src[i + 1]; i += 2;
      if (low < 0xDC00 || low > 0xDFFF) return -1;
      cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
    } else if (cp >= 0xDC00 && cp <= 0xDFFF) return -1;
    if (cp < 0x80) { if (n + 1 >= cap) return -1; out[n++] = (char)cp; }
    else if (cp < 0x800) {
      if (n + 2 >= cap) return -1;
      out[n++] = (char)(0xC0 | (cp >> 6)); out[n++] = (char)(0x80 | (cp & 63));
    } else if (cp < 0x10000) {
      if (n + 3 >= cap) return -1;
      out[n++] = (char)(0xE0 | (cp >> 12)); out[n++] = (char)(0x80 | ((cp >> 6) & 63)); out[n++] = (char)(0x80 | (cp & 63));
    } else {
      if (n + 4 >= cap) return -1;
      out[n++] = (char)(0xF0 | (cp >> 18)); out[n++] = (char)(0x80 | ((cp >> 12) & 63));
      out[n++] = (char)(0x80 | ((cp >> 6) & 63)); out[n++] = (char)(0x80 | (cp & 63));
    }
  }
  out[n] = 0; return n ? 0 : -1;
}
static int safe_basename(const char *input, char *output, size_t cap) {
  size_t i, n = strlen(input);
  if (!n || n >= cap || !strcmp(input, ".") || !strcmp(input, "..")) return -1;
  for (i = 0; i < n; i++) {
    unsigned char c = (unsigned char)input[i];
    if (c < 32 || c == 127 || c == '/' || c == '\\') return -1;
  }
  memcpy(output, input, n + 1); return 0;
}
static int make_destination(const char *name, char *path, size_t cap, int *fd_out) {
  char clean[256]; int fd; unsigned int suffix;
  if (safe_basename(name, clean, sizeof(clean))) return -1;
  for (suffix = 0; suffix < 1000; suffix++) {
    if (!suffix) snprintf(path, cap, "%s/%s", g_dir, clean);
    else snprintf(path, cap, "%s/%u_%s", g_dir, suffix, clean);
    fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
    if (fd >= 0) { *fd_out = fd; return 0; }
    if (errno != EEXIST) return -1;
  }
  errno = EEXIST; return -1;
}
typedef struct {
  char name[512];
  char clean[256];
  char path[800];
  uint64_t expected;
  uint64_t received;
  int output;
  int active;
} opp_transfer;

static void reset_transfer(opp_transfer *t, int remove_partial) {
  if (t->output >= 0) close(t->output);
  if (remove_partial && t->path[0]) unlink(t->path);
  memset(t, 0, sizeof(*t));
  t->expected = UINT64_MAX;
  t->output = -1;
}
static int ensure_output(opp_transfer *t) {
  if (t->output >= 0) return 0;
  if (!t->name[0] || safe_basename(t->name, t->clean, sizeof(t->clean))) return -1;
  if (make_destination(t->clean, t->path, sizeof(t->path), &t->output)) return -1;
  return 0;
}
static int append_body(opp_transfer *t, const unsigned char *data, size_t length) {
  size_t off = 0;
  if (ensure_output(t)) return -1;
  if (t->received + length > OPP_MAX_FILE) return -1;
  while (off < length) {
    ssize_t n = write(t->output, data + off, length - off);
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0) return -1;
    off += (size_t)n;
  }
  t->received += length;
  return 0;
}
static void receive_put(int fd, unsigned char *packet, size_t packet_len, int final, opp_transfer *transfer) {
  unsigned char *p = packet + 3, *end = packet + packet_len;
  int failed = 0;
  if (!transfer->active) {
    transfer->active = 1;
    transfer->expected = UINT64_MAX;
    transfer->output = -1;
  }
  while (p < end) {
    unsigned char id = *p++;
    unsigned int kind = id >> 6;
    if (kind == 0 || kind == 1) {
      unsigned int field_len;
      size_t data_len;
      if (end - p < 2) { failed = 1; break; }
      field_len = ((unsigned int)p[0] << 8) | p[1];
      if (field_len < 3 || (size_t)(end - p) < field_len - 1) { failed = 1; break; }
      data_len = field_len - 3;
      if (id == 0x01) {
        if (utf16be_to_utf8(p + 2, data_len, transfer->name, sizeof(transfer->name))) failed = 1;
      } else if (id == 0x48 || id == 0x49) {
        if (append_body(transfer, p + 2, data_len)) failed = 1;
      }
      p += field_len - 1;
    } else if (kind == 2) {
      if (end - p < 1) { failed = 1; break; }
      p += 1;
    } else {
      uint32_t value;
      if (end - p < 4) { failed = 1; break; }
      value = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
      if (id == 0xC3) transfer->expected = value;
      p += 4;
    }
  }
  if (transfer->expected != UINT64_MAX &&
      (transfer->expected > OPP_MAX_FILE || transfer->received > transfer->expected)) failed = 1;
  if (failed) {
    reset_transfer(transfer, 1);
    send_response(fd, 0xC0, NULL, 0);
    return;
  }
  if (!final) {
    send_response(fd, 0x90, NULL, 0);
    return;
  }
  if (ensure_output(transfer) ||
      (transfer->expected != UINT64_MAX && transfer->received != transfer->expected)) {
    reset_transfer(transfer, 1);
    send_response(fd, 0xC0, NULL, 0);
    return;
  }
  fsync(transfer->output);
  close(transfer->output);
  transfer->output = -1;
  pthread_mutex_lock(&g_lock);
  g_received++;
  snprintf(g_last, sizeof(g_last), "%s", transfer->clean);
  pthread_mutex_unlock(&g_lock);
  send_response(fd, 0xA0, NULL, 0);
  reset_transfer(transfer, 0);
}
static void handle_client(int fd) {
  unsigned char *packet = NULL;
  size_t length = 0;
  int connected = 0;
  opp_transfer transfer;
  memset(&transfer, 0, sizeof(transfer));
  transfer.expected = UINT64_MAX;
  transfer.output = -1;
  for (;;) {
    unsigned char op;
    if (read_packet(fd, &packet, &length)) break;
    op = packet[0];
    if (op == 0x80) {
      const unsigned char connect_reply[4] = { 0x10, 0x00, 0xFF, 0xFE };
      send_response(fd, 0xA0, connect_reply, sizeof(connect_reply));
      connected = 1;
    } else if ((op & 0x7F) == 0x02 && connected) {
      receive_put(fd, packet, length, (op & 0x80) != 0, &transfer);
    } else if (op == 0x81 && connected) {
      send_response(fd, 0xA0, NULL, 0);
      free(packet);
      packet = NULL;
      break;
    } else {
      send_response(fd, 0xC0, NULL, 0);
      free(packet);
      packet = NULL;
      break;
    }
    free(packet);
    packet = NULL;
  }
  free(packet);
  reset_transfer(&transfer, 1);
}
static int run_sdptool(int add) {
  pid_t pid = fork(); int status = 0;
  if (pid < 0) return -1;
  if (pid == 0) {
    if (add) execlp("sdptool", "sdptool", "add", "--channel=9", "OPUSH", (char *)NULL);
    else execlp("sdptool", "sdptool", "del", "OPUSH", (char *)NULL);
    _exit(127);
  }
  if (waitpid(pid, &status, 0) < 0) return -1;
  return WIFEXITED(status) && WEXITSTATUS(status) == 0 ? 0 : -1;
}
static int register_sdp(void) { return run_sdptool(1); }
static void unregister_sdp(void) { (void)run_sdptool(0); }
static void *server_main(void *unused) {
  int server; hf_sockaddr_rc addr; (void)unused;
  if (register_sdp() == 0) { pthread_mutex_lock(&g_lock); g_sdp_registered = 1; pthread_mutex_unlock(&g_lock); }
  server = socket(AF_BLUETOOTH, SOCK_STREAM, BTPROTO_RFCOMM);
  if (server < 0) goto done;
  memset(&addr, 0, sizeof(addr)); addr.family = AF_BLUETOOTH; addr.channel = OPP_CHANNEL;
  if (bind(server, (struct sockaddr *)&addr, sizeof(addr)) < 0 || listen(server, 4) < 0) { close(server); goto done; }
  pthread_mutex_lock(&g_lock); g_listener = server; g_running = 1; pthread_mutex_unlock(&g_lock);
  for (;;) {
    int client = accept(server, NULL, NULL);
    if (client < 0) {
      int stop; if (errno == EINTR) continue;
      pthread_mutex_lock(&g_lock); stop = g_stop; pthread_mutex_unlock(&g_lock);
      if (stop) break; continue;
    }
    handle_client(client); close(client);
  }
  close(server);
done:
  if (g_sdp_registered) { unregister_sdp(); pthread_mutex_lock(&g_lock); g_sdp_registered = 0; pthread_mutex_unlock(&g_lock); }
  pthread_mutex_lock(&g_lock); g_listener = -1; g_running = 0; g_stop = 0; pthread_mutex_unlock(&g_lock);
  return NULL;
}
static JSValue promise_value(JSContext *ctx, JSValue value, int reject) {
  JSValue funcs[2], promise = JS_NewPromiseCapability(ctx, funcs);
  if (JS_IsException(promise)) { JS_FreeValue(ctx, value); return promise; }
  JS_Call(ctx, reject ? funcs[1] : funcs[0], JS_UNDEFINED, 1, (JSValueConst *)&value);
  JS_FreeValue(ctx, funcs[0]); JS_FreeValue(ctx, funcs[1]); JS_FreeValue(ctx, value); return promise;
}
static JSValue promise_error(JSContext *ctx, const char *what, int err) {
  char message[256]; JSValue error = JS_NewError(ctx);
  snprintf(message, sizeof(message), "%s: %s", what, strerror(err));
  JS_SetPropertyStr(ctx, error, "message", JS_NewString(ctx, message));
  return promise_value(ctx, error, 1);
}
static JSValue js_start(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
  const char *dir = NULL; int rc; (void)this_val;
  if (argc > 0) { dir = JS_ToCString(ctx, argv[0]); if (!dir) return JS_EXCEPTION; }
  pthread_mutex_lock(&g_lock);
  if (g_running) { pthread_mutex_unlock(&g_lock); if (dir) JS_FreeCString(ctx, dir); return promise_value(ctx, JS_NewBool(ctx, 1), 0); }
  if (dir && dir[0]) {
    if (strlen(dir) >= sizeof(g_dir)) { pthread_mutex_unlock(&g_lock); JS_FreeCString(ctx, dir); return promise_error(ctx, "directory too long", ENAMETOOLONG); }
    snprintf(g_dir, sizeof(g_dir), "%s", dir);
  }
  if (dir) JS_FreeCString(ctx, dir);
  if (mkdir(g_dir, 0700) < 0 && errno != EEXIST) { int e = errno; pthread_mutex_unlock(&g_lock); return promise_error(ctx, "create receive directory", e); }
  g_stop = 0; rc = pthread_create(&g_thread, NULL, server_main, NULL);
  if (rc != 0) { pthread_mutex_unlock(&g_lock); return promise_error(ctx, "start OBEX server", rc); }
  pthread_detach(g_thread); pthread_mutex_unlock(&g_lock);
  return promise_value(ctx, JS_NewBool(ctx, 1), 0);
}
static JSValue js_stop(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
  int fd; (void)this_val; (void)argc; (void)argv;
  pthread_mutex_lock(&g_lock); g_stop = 1; fd = g_listener; if (fd >= 0) shutdown(fd, SHUT_RDWR); pthread_mutex_unlock(&g_lock);
  return promise_value(ctx, JS_NewBool(ctx, 1), 0);
}
static JSValue js_status(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
  JSValue object; (void)this_val; (void)argc; (void)argv;
  pthread_mutex_lock(&g_lock); object = JS_NewObject(ctx);
  JS_SetPropertyStr(ctx, object, "running", JS_NewBool(ctx, g_running));
  JS_SetPropertyStr(ctx, object, "channel", JS_NewInt32(ctx, OPP_CHANNEL));
  JS_SetPropertyStr(ctx, object, "sdpRegistered", JS_NewBool(ctx, g_sdp_registered));
  JS_SetPropertyStr(ctx, object, "received", JS_NewInt64(ctx, (int64_t)g_received));
  JS_SetPropertyStr(ctx, object, "lastFile", JS_NewString(ctx, g_last));
  pthread_mutex_unlock(&g_lock); return promise_value(ctx, object, 0);
}
static int module_init(JSContext *ctx, JSModuleDef *module) {
  JSValue api = JS_NewObject(ctx);
  JS_SetPropertyStr(ctx, api, "startServer", JS_NewCFunction(ctx, js_start, "startServer", 1));
  JS_SetPropertyStr(ctx, api, "stopServer", JS_NewCFunction(ctx, js_stop, "stopServer", 0));
  JS_SetPropertyStr(ctx, api, "status", JS_NewCFunction(ctx, js_status, "status", 0));
  JS_SetModuleExport(ctx, module, "default", api); return 0;
}
static JSModuleDef *module_load(JSContext *ctx, const char *name) {
  JSModuleDef *module = JS_NewCModule(ctx, name, module_init);
  if (module) JS_AddModuleExport(ctx, module, "default");
  return module;
}
__attribute__((visibility("default"))) void custom_init_jsapis(void) {
  registerCModuleLoader("hyperfile_obex", module_load);
}
