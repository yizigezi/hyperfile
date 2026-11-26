/*
 * HyperFile Bluetooth RFCOMM file transfer JSAPI.
 * Wire protocol: "HFBT" + uint32 filename length (BE) + uint64 file size (BE)
 * + UTF-8 basename + raw file bytes. RFCOMM channel 7.
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
#include <unistd.h>

#ifndef AF_BLUETOOTH
#define AF_BLUETOOTH 31
#endif
#ifndef BTPROTO_RFCOMM
#define BTPROTO_RFCOMM 3
#endif
#define HF_BT_CHANNEL 7
#define HF_BT_MAX_FILE (1024ULL * 1024ULL * 1024ULL)
typedef struct { sa_family_t family; uint8_t address[6]; uint8_t channel; } hf_sockaddr_rc;

typedef JSModuleDef *(*LoadCModuleFunction)(JSContext *, const char *);
extern void registerCModuleLoader(const char *, LoadCModuleFunction);

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_t g_thread;
static int g_running, g_stop, g_listener = -1;
static unsigned long g_received;
static char g_dir[512] = "/userdisk/Favorite/HyperFile";
static char g_last[256] = "";

static JSValue promise_value(JSContext *ctx, JSValue value, int reject) {
  JSValue f[2], p = JS_NewPromiseCapability(ctx, f);
  if (JS_IsException(p)) { JS_FreeValue(ctx, value); return p; }
  JS_Call(ctx, reject ? f[1] : f[0], JS_UNDEFINED, 1, (JSValueConst *)&value);
  JS_FreeValue(ctx, f[0]); JS_FreeValue(ctx, f[1]); JS_FreeValue(ctx, value);
  return p;
}
static JSValue promise_error(JSContext *ctx, const char *what, int err) {
  char msg[256]; snprintf(msg, sizeof(msg), "%s: %s", what, strerror(err));
  JSValue e = JS_NewError(ctx); JS_SetPropertyStr(ctx, e, "message", JS_NewString(ctx, msg));
  return promise_value(ctx, e, 1);
}
static int recv_all(int fd, void *buf, size_t n) {
  unsigned char *p = (unsigned char *)buf;
  while (n) { ssize_t r = recv(fd, p, n, 0); if (r < 0 && errno == EINTR) continue; if (r <= 0) return -1; p += r; n -= (size_t)r; }
  return 0;
}
static int send_all(int fd, const void *buf, size_t n) {
  const unsigned char *p = (const unsigned char *)buf;
  while (n) { ssize_t r = send(fd, p, n, 0); if (r < 0 && errno == EINTR) continue; if (r <= 0) return -1; p += r; n -= (size_t)r; }
  return 0;
}
static uint64_t read_u64be(const unsigned char *p) {
  uint64_t v = 0; int i; for (i=0;i<8;i++) v=(v<<8)|p[i]; return v;
}
static void write_u64be(unsigned char *p, uint64_t v) {
  int i; for (i=7;i>=0;i--) { p[i]=(unsigned char)(v&255); v>>=8; }
}
static int safe_name(const char *in, char *out, size_t cap) {
  size_t n=0; const unsigned char *p=(const unsigned char *)in;
  while (*p) { unsigned char c=*p++; if (c=='/'||c=='\\\\'||c<32||c==127) continue; if (n+1>=cap) return -1; out[n++]=(char)c; }
  out[n]='\\0'; if (!n || !strcmp(out,".") || !strcmp(out,"..")) return -1; return 0;
}
static void receive_one(int fd) {
  unsigned char header[16], sizebuf[8], buf[16384];
  char name[256], clean[256], path[800];
  uint32_t name_len; uint64_t remaining; int out=-1;
  if (recv_all(fd, header, sizeof(header)) || memcmp(header,"HFBT",4)) return;
  name_len=((uint32_t)header[4]<<24)|((uint32_t)header[5]<<16)|((uint32_t)header[6]<<8)|header[7];
  remaining=read_u64be(header+8);
  if (!name_len || name_len>=sizeof(name) || remaining>HF_BT_MAX_FILE) return;
  if (recv_all(fd,name,name_len)) return; name[name_len]='\\0';
  if (safe_name(name,clean,sizeof(clean))) return;
  snprintf(path,sizeof(path),"%s/%s",g_dir,clean);
  out=open(path,O_WRONLY|O_CREAT|O_TRUNC,0600); if(out<0) return;
  while(remaining) {
    size_t want=remaining>sizeof(buf)?sizeof(buf):(size_t)remaining;
    ssize_t n=recv(fd,buf,want,0);
    if(n<0&&errno==EINTR) continue;
    if(n<=0) { close(out); unlink(path); return; }
    size_t off=0; while(off<(size_t)n) { ssize_t w=write(out,buf+off,(size_t)n-off); if(w<0&&errno==EINTR) continue; if(w<=0){close(out);unlink(path);return;} off+=(size_t)w; }
    remaining-=(uint64_t)n;
  }
  fsync(out); close(out);
  pthread_mutex_lock(&g_lock); g_received++; snprintf(g_last,sizeof(g_last),"%s",clean); pthread_mutex_unlock(&g_lock);
}
static void *server_main(void *unused) {
  int fd; hf_sockaddr_rc addr; (void)unused;
  fd=socket(AF_BLUETOOTH,SOCK_STREAM,BTPROTO_RFCOMM);
  if(fd<0) goto done;
  memset(&addr,0,sizeof(addr)); addr.family=AF_BLUETOOTH; addr.channel=HF_BT_CHANNEL;
  if(bind(fd,(struct sockaddr *)&addr,sizeof(addr))<0 || listen(fd,4)<0) { close(fd); goto done; }
  pthread_mutex_lock(&g_lock); g_listener=fd; g_running=1; pthread_mutex_unlock(&g_lock);
  for(;;) {
    int client=accept(fd,NULL,NULL);
    if(client<0) { if(errno==EINTR) continue; pthread_mutex_lock(&g_lock); int stop=g_stop; pthread_mutex_unlock(&g_lock); if(stop) break; continue; }
    receive_one(client); close(client);
  }
  close(fd);
done:
  pthread_mutex_lock(&g_lock); g_listener=-1; g_running=0; g_stop=0; pthread_mutex_unlock(&g_lock);
  return NULL;
}
static JSValue js_start(JSContext *ctx, JSValueConst th, int argc, JSValueConst *argv) {
  const char *dir=NULL; (void)th;
  if(argc>0) { dir=JS_ToCString(ctx,argv[0]); if(!dir) return JS_EXCEPTION; }
  pthread_mutex_lock(&g_lock);
  if(g_running) { pthread_mutex_unlock(&g_lock); if(dir)JS_FreeCString(ctx,dir); return promise_value(ctx,JS_NewBool(ctx,1),0); }
  if(dir && dir[0]) { if(strlen(dir)>=sizeof(g_dir)){pthread_mutex_unlock(&g_lock);JS_FreeCString(ctx,dir);return promise_error(ctx,"directory too long",ENAMETOOLONG);} snprintf(g_dir,sizeof(g_dir),"%s",dir); }
  if(dir) JS_FreeCString(ctx,dir);
  if(mkdir(g_dir,0700)<0 && errno!=EEXIST) { int e=errno; pthread_mutex_unlock(&g_lock); return promise_error(ctx,"create receive directory",e); }
  g_stop=0;
  if(pthread_create(&g_thread,NULL,server_main,NULL)!=0) { pthread_mutex_unlock(&g_lock); return promise_error(ctx,"start RFCOMM server",EAGAIN); }
  pthread_detach(g_thread); pthread_mutex_unlock(&g_lock);
  return promise_value(ctx,JS_NewBool(ctx,1),0);
}
static JSValue js_stop(JSContext *ctx, JSValueConst th, int argc, JSValueConst *argv) {
  int fd; (void)th;(void)argc;(void)argv;
  pthread_mutex_lock(&g_lock); g_stop=1; fd=g_listener; if(fd>=0) shutdown(fd,SHUT_RDWR); pthread_mutex_unlock(&g_lock);
  return promise_value(ctx,JS_NewBool(ctx,1),0);
}
static JSValue js_status(JSContext *ctx, JSValueConst th, int argc, JSValueConst *argv) {
  JSValue o; (void)th;(void)argc;(void)argv;
  pthread_mutex_lock(&g_lock); o=JS_NewObject(ctx);
  JS_SetPropertyStr(ctx,o,"running",JS_NewBool(ctx,g_running));
  JS_SetPropertyStr(ctx,o,"channel",JS_NewInt32(ctx,HF_BT_CHANNEL));
  JS_SetPropertyStr(ctx,o,"received",JS_NewInt64(ctx,(int64_t)g_received));
  JS_SetPropertyStr(ctx,o,"lastFile",JS_NewString(ctx,g_last));
  pthread_mutex_unlock(&g_lock); return promise_value(ctx,o,0);
}
static int parse_mac(const char *s, uint8_t out[6]) {
  unsigned int b[6]; int i;
  if(sscanf(s,"%2x:%2x:%2x:%2x:%2x:%2x",&b[0],&b[1],&b[2],&b[3],&b[4],&b[5])!=6) return -1;
  for(i=0;i<6;i++) out[i]=(uint8_t)b[5-i]; return 0;
}
static JSValue js_send(JSContext *ctx, JSValueConst th, int argc, JSValueConst *argv) {
  const char *mac,*path; uint8_t addrbytes[6]; hf_sockaddr_rc addr; int fd=-1,in=-1; struct stat st;
  char *base; const char *slash; unsigned char header[16],buf[16384]; ssize_t n; int err=0; (void)th;
  if(argc<2) return promise_error(ctx,"sendFile requires MAC and path",EINVAL);
  mac=JS_ToCString(ctx,argv[0]); path=JS_ToCString(ctx,argv[1]); if(!mac||!path){if(mac)JS_FreeCString(ctx,mac);if(path)JS_FreeCString(ctx,path);return JS_EXCEPTION;}
  if(parse_mac(mac,addrbytes)){err=EINVAL;goto done;}
  in=open(path,O_RDONLY); if(in<0){err=errno;goto done;} if(fstat(in,&st)||!S_ISREG(st.st_mode)||st.st_size<0){err=errno?errno:EINVAL;goto done;}
  fd=socket(AF_BLUETOOTH,SOCK_STREAM,BTPROTO_RFCOMM); if(fd<0){err=errno;goto done;}
  memset(&addr,0,sizeof(addr)); addr.family=AF_BLUETOOTH; memcpy(addr.address,addrbytes,6); addr.channel=HF_BT_CHANNEL;
  if(connect(fd,(struct sockaddr *)&addr,sizeof(addr))<0){err=errno;goto done;}
  slash=strrchr(path,'/'); base=(char *)(slash?slash+1:path); if(!*base || strlen(base)>255){err=EINVAL;goto done;}
  memcpy(header,"HFBT",4); header[4]=0;header[5]=0;header[6]=0;header[7]=(unsigned char)strlen(base);write_u64be(header+8,(uint64_t)st.st_size);
  if(send_all(fd,header,sizeof(header))||send_all(fd,base,strlen(base))){err=errno?errno:EIO;goto done;}
  while((n=read(in,buf,sizeof(buf)))>0) if(send_all(fd,buf,(size_t)n)){err=errno?errno:EIO;goto done;}
  if(n<0) err=errno;
done:
  if(fd>=0)close(fd); if(in>=0)close(in); JS_FreeCString(ctx,mac);JS_FreeCString(ctx,path);
  if(err) return promise_error(ctx,"RFCOMM send failed",err);
  return promise_value(ctx,JS_NewBool(ctx,1),0);
}
static int module_init(JSContext *ctx, JSModuleDef *m) {
  JSValue api=JS_NewObject(ctx);
  JS_SetPropertyStr(ctx,api,"startServer",JS_NewCFunction(ctx,js_start,"startServer",1));
  JS_SetPropertyStr(ctx,api,"stopServer",JS_NewCFunction(ctx,js_stop,"stopServer",0));
  JS_SetPropertyStr(ctx,api,"status",JS_NewCFunction(ctx,js_status,"status",0));
  JS_SetPropertyStr(ctx,api,"sendFile",JS_NewCFunction(ctx,js_send,"sendFile",2));
  JS_SetModuleExport(ctx,m,"default",api); return 0;
}
static JSModuleDef *module_load(JSContext *ctx,const char *name) {
  JSModuleDef *m=JS_NewCModule(ctx,name,module_init); if(m)JS_AddModuleExport(ctx,m,"default"); return m;
}
__attribute__((visibility("default"))) void custom_init_jsapis(void) {
  registerCModuleLoader("hyperfile_bluetooth",module_load);
}
