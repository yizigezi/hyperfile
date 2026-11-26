/**
 * ReaderSession —— 阅读器目标文件 / 阅读设置 的轻量共享存储（照 readex-nt BookSession 做法）。
 * 页面间不走路由参数：进入前 setReaderTarget，阅读器 onShow 时 getReaderTarget 读取。
 * 读取不清空（生命周期钩子可能多次触发），下次进入由 setReaderTarget 覆盖。
 */
let current = { path: '', name: '', book: null }
let settings = { fontSize: 17, lineHeight: 1.55 }

export function setReaderTarget(path, name, book) {
  current = { path: path || '', name: name || '', book: book || null }
}

export function getReaderTarget() {
  return current
}

export function getReaderSettings() {
  return { fontSize: settings.fontSize, lineHeight: settings.lineHeight }
}

export function setReaderSettings(patch) {
  if (patch && typeof patch.fontSize === 'number') settings.fontSize = patch.fontSize
  if (patch && typeof patch.lineHeight === 'number') settings.lineHeight = patch.lineHeight
}
