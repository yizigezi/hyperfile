/**
 * ReaderSession —— 阅读器目标文件的轻量共享存储（照 readex-nt BookSession 的做法）。
 * 页面间不走路由参数：进入前 setReaderTarget，阅读器 onShow 时 getReaderTarget 读取。
 * 读取不清空（生命周期钩子可能多次触发），下次进入由 setReaderTarget 覆盖。
 */
let current = { path: '', name: '' }

export function setReaderTarget(path, name) {
  current = { path: path || '', name: name || '' }
}

export function getReaderTarget() {
  return current
}
