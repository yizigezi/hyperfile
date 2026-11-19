/*
 * http_plugin.c — HyperFile 有道词典笔 jsapi 插件（quickjs 胶水）
 *
 * 按框架 jsapi ABI 导出 custom_init_jsapis，注册为 ES 模块 "hyperfile_http"：
 *   import('hyperfile_http') -> {
 *     start(port, uploadDir) -> Promise<{running, port, ip, dir}>
 *     stop()                 -> Promise<{running}>
 *     status()               -> Promise<{running, port, ip, dir, received, lastFile}>
 *   }
 *
 * 服务器逻辑在 http_server.c（纯 C，可独立测试）。
 * 模块注册方式沿 readex-nt fs_plugin.c 同款（其 ABI 声明来自框架头）。
 *
 * 编译（见 tools/build-http-plugin.sh）：
 *   zig cc -target arm-linux-gnueabihf.2.23 -shared -fPIC -O2 \
 *       -I native native/http_server.c native/http_plugin.c -lpthread -o libs/libjsapi_hyperfile_http.so
 *
 * SPDX-License-Identifier: AGPL-3.0
 */

#include "quickjs.h"
#include "http_server.h"

#include <stdlib.h>
#include <string.h>

/* ---- 宿主框架接口（iot-miniapp-sdk jsmodules/JSCModuleExtension.h）----
 * registerCModuleLoader 由框架提供，.so 内保持未定义动态符号即可。 */
typedef JSModuleDef* (*LoadCModuleFunction)(JSContext* context, const char* moduleName);
extern void registerCModuleLoader(const char* moduleName, LoadCModuleFunction loader);

/* ---------------- Promise 工具（沿 fs_plugin 的包装方式） ---------------- */

static JSValue make_resolved_promise(JSContext* ctx, JSValue value)
{
    JSValue funcs[2];
    JSValue promise = JS_NewPromiseCapability(ctx, funcs);
    JS_Call(ctx, funcs[0], JS_UNDEFINED, 1, (JSValueConst*)&value);
    JS_FreeValue(ctx, funcs[0]);
    JS_FreeValue(ctx, funcs[1]);
    JS_FreeValue(ctx, value);
    return promise;
}

static JSValue make_rejected_promise(JSContext* ctx, JSValue error)
{
    JSValue funcs[2];
    JSValue promise = JS_NewPromiseCapability(ctx, funcs);
    JS_Call(ctx, funcs[1], JS_UNDEFINED, 1, (JSValueConst*)&error);
    JS_FreeValue(ctx, funcs[0]);
    JS_FreeValue(ctx, funcs[1]);
    JS_FreeValue(ctx, error);
    return promise;
}

static JSValue make_error(JSContext* ctx, const char* message)
{
    JSValue error = JS_NewError(ctx);
    JS_SetPropertyStr(ctx, error, "message", JS_NewString(ctx, message));
    return error;
}

static JSValue status_to_js(JSContext* ctx)
{
    hf_status_t st;
    hf_server_status(&st);

    JSValue obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "running", JS_NewBool(ctx, st.running));
    JS_SetPropertyStr(ctx, obj, "port", JS_NewInt32(ctx, st.port));
    JS_SetPropertyStr(ctx, obj, "ip", JS_NewString(ctx, st.ip));
    JS_SetPropertyStr(ctx, obj, "dir", JS_NewString(ctx, st.dir));
    JS_SetPropertyStr(ctx, obj, "received", JS_NewInt64(ctx, st.received));
    JS_SetPropertyStr(ctx, obj, "lastFile", JS_NewString(ctx, st.last_file));
    return obj;
}

/* ---------------- 模块方法 ---------------- */

static JSValue js_start(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
{
    (void)this_val;
    int32_t port = 8080;
    if (argc >= 1)
        JS_ToInt32(ctx, &port, argv[0]);

    const char* dir = NULL;
    if (argc >= 2)
        dir = JS_ToCString(ctx, argv[1]);
    if (!dir || !dir[0]) {
        if (dir)
            JS_FreeCString(ctx, dir);
        return make_rejected_promise(ctx, make_error(ctx, "start: uploadDir required"));
    }

    int rc = hf_server_start(port, dir);
    JS_FreeCString(ctx, dir);

    if (rc != 0)
        return make_rejected_promise(ctx, make_error(ctx, "start: bind/listen failed"));

    return make_resolved_promise(ctx, status_to_js(ctx));
}

static JSValue js_stop(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    hf_server_stop();
    return make_resolved_promise(ctx, status_to_js(ctx));
}

static JSValue js_status(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    return make_resolved_promise(ctx, status_to_js(ctx));
}

/* ---------------- 模块定义与注册 ---------------- */

static int hyperfile_http_module_init(JSContext* ctx, JSModuleDef* m)
{
    JSValue api = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, api, "start", JS_NewCFunction(ctx, js_start, "start", 2));
    JS_SetPropertyStr(ctx, api, "stop", JS_NewCFunction(ctx, js_stop, "stop", 0));
    JS_SetPropertyStr(ctx, api, "status", JS_NewCFunction(ctx, js_status, "status", 0));
    JS_SetModuleExport(ctx, m, "default", api);
    return 0;
}

static JSModuleDef* hyperfile_http_module_load(JSContext* ctx, const char* module_name)
{
    JSModuleDef* m = JS_NewCModule(ctx, module_name, hyperfile_http_module_init);
    if (!m)
        return NULL;
    JS_AddModuleExport(ctx, m, "default");
    return m;
}

/* 框架加载 libs/libjsapi_*.so 时调用的唯一入口 */
__attribute__((visibility("default")))
void custom_init_jsapis(void)
{
    registerCModuleLoader("hyperfile_http", hyperfile_http_module_load);
}
