# HyperFile 自动更新设计

本文记录 HyperFile 从 GitHub Releases 检查并安装更新的实现约定。更新功能面向词典笔上的 Falcon / QuickJS / Buildroot 环境，不假设浏览器端存在 `fetch`、XHR、Node.js 或系统 `curl/wget`。

## 目标

- 在“关于 HyperFile”页面检查 GitHub Release。
- 展示版本号、发布时间、更新说明和可用安装包。
- 支持 GitHub 直连及可配置的镜像/代理下载地址（例如 ghproxy）。
- 下载完成后校验文件，再调用现有独立更新器安装。
- 更新失败时保留当前应用可恢复状态，并给出日志位置。

## 已有实现

- `native/updater_main.c` 已负责停止 HTTP 服务、卸载旧应用、安装 `/tmp/hyperfile-update.amr` 并重新启动应用。
- `native/http_controller_plugin.c` 负责部署和启动独立服务进程；它本身不执行 HTTP 请求。
- `native/local_ops_plugin.c` 提供本地 AMR 安装和脚本任务能力，但没有网络下载接口。
- 更新器清理的是 `/userdisk/HyperFile` 服务目录；书籍、阅读进度等用户数据位于独立的 `/userdisk/HyperFile_Data`，更新流程不得触碰该目录。

## 分阶段实现

### 阶段 1：确认设备端网络能力（本 PR 的前置工作）

在添加下载代码前，先确定设备实际可用的 HTTPS/TLS 实现及证书来源。此前设备环境出现过证书目录为空、HTTPS 返回未知错误的情况，因此不能仅凭开发机行为认定词典笔可用。

验收：
- 明确设备端可用的请求/下载实现及其依赖。
- 验证 GitHub Releases API 的 HTTPS 请求。
- 验证镜像 URL 的重定向、状态码和超时行为。
- 不引入未经验证的 Falcon JSAPI。

### 阶段 2：Release 检查

建议使用 GitHub 官方 Releases API：
`https://api.github.com/repos/yizigezi/hyperfile/releases/latest`

解析 `tag_name`、`name`、`body`、`published_at` 和 `assets`。版本比较使用明确的版本解析逻辑，不直接进行字符串大小比较。网络失败、无 Release、响应格式异常均应区分显示。

### 阶段 3：下载源与代理

- 默认源为 GitHub Release asset 的 `browser_download_url`。
- 允许用户配置代理 URL 模板；模板以完整 asset URL 为输入。
- 支持多个下载源按顺序回退；每次失败记录源地址和错误原因。
- 不将代理站点硬编码为唯一来源，也不把代理返回的 HTML 错误页当作安装包。

### 阶段 4：校验和安装

- 下载到临时文件，完成后检查 HTTP 状态、文件大小及 AMR 文件格式。
- Release 应提供 SHA-256 校验值；校验通过后才移动到更新器约定的 `/tmp/hyperfile-update.amr`。
- 安装仍由现有独立更新器完成，UI 不直接卸载或安装应用。
- 不删除或覆盖 `/userdisk/HyperFile_Data`。
- 保留更新日志，并在失败时明确提示手动恢复方式。

## 安全与兼容性约束

- 只接受 HTTPS 下载源；自定义代理也必须是 HTTPS。
- 对下载文件设置合理大小上限，避免磁盘耗尽。
- 不信任文件名或 Release 文本中的路径。
- 不执行 Release 附带的任意脚本。
- 不允许在校验失败或下载未完成时启动更新器。
- 设备端证书校验不能为了“能连上”而整体关闭。

## 当前未完成项

本设计文档不代表设备端 HTTPS 下载已实现。阶段 1 未通过前，不应开放“下载并更新”按钮；尤其不能用未经验证的 `fetch` / XHR / `curl` 作为实现假设。
