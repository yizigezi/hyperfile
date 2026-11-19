# HyperFile — 词典笔 Wi-Fi 传文件

同 Wi-Fi 下其他设备打开网页（或扫码），把文件传到 `/userdisk/Favorite/HyperFile`。

## 架构

- **native/http_server.c**：最小 HTTP 文件接收服务器（单线程 accept 串行处理，流式写盘，路径消毒防穿越）
  - `GET /` 上传网页（内嵌单文件 HTML，XHR PUT + 进度条）
  - `GET /status` JSON 状态
  - `PUT|POST /upload?name=` 流式收 body 写盘（也认 `X-File-Name` 头）
- **native/http_plugin.c**：quickjs 胶水，注册 ES 模块 `hyperfile_http`（start/stop/status，Promise 形状）
- **libs/libjsapi_fs.so**：直接用 readex-nt 那一个（native/fs_plugin.c，AGPL-3.0）
- **UI**：falcon 单页（启动/停止、IP + QR 扫码、已接收列表）

## 构建

- X5：`zig cc -target arm-linux-gnueabihf.2.23`，`tools/build-http-plugin.sh`（含 ELF 自检）
- 本地逻辑测试：`tools/build-http-plugin.sh --test` 出 x86 `hf-server` 直接 curl 测
- CI：`.github/workflows/build.yml`（zig 编 native → aiot build:prod → 上传 amr）

## 已验证（2026-11-19 本地）

- x86 实测：上传（query 名 / X-File-Name 头 / 自动命名）、5MB 大文件 md5 一致、路径穿越拦截、/status 计数、网页输出
- ARM so：ELF 自检通过（hard-float，GLIBC 上限 2.7，NEEDED 仅 libc/libpthread）
- web 预览：UI 渲染 + 启停交互 + 文件列表 + QR

## 待真机（X5）

- [ ] `hyperfile_http` 模块加载（libs/so 提取，升级前 uninstall）
- [ ] 手机同 Wi-Fi 实传（含大文件、浏览器兼容）
- [ ] 端口 8080 可达性（系统防火墙）
- [ ] `/userdisk/Favorite/HyperFile` 自动建目录

## 许可

- LICENSE：GPL-3（与 readex-nt 保持一致）
- libjsapi_fs.so / FileSystem.js：来自 yizigezi/readex-nt（上游 adogecheems/doge-comic，AGPL-3.0，源内 SPDX 标注保留）
- native 新增源码沿用 readex-nt 生态标注（AGPL-3.0）
