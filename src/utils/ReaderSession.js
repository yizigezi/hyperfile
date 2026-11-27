/**
 * ReaderSession —— 阅读目标与持久化默认阅读设置。
 */
import { fs } from './FileSystem/FileSystem.js'

let current = { path: '', name: '', book: null }
const DEFAULT_SETTINGS = { fontSize: 17, lineHeight: 1.55 }
let settings = { fontSize: DEFAULT_SETTINGS.fontSize, lineHeight: DEFAULT_SETTINGS.lineHeight }
const SETTINGS_DIR = '/userdisk/HyperFile_Data'
const SETTINGS_FILE = SETTINGS_DIR + '/reader-settings.json'
let settingsLoadPromise = null

export function setReaderTarget(path, name, book) {
  current = { path: path || '', name: name || '', book: book || null }
}

export function getReaderTarget() {
  return current
}

export function getReaderSettings() {
  return { fontSize: settings.fontSize, lineHeight: settings.lineHeight }
}

export async function loadReaderSettings() {
  if (!settingsLoadPromise) {
    settingsLoadPromise = (async () => {
      try {
        const raw = await fs.readFile(SETTINGS_FILE)
        const saved = JSON.parse(String(raw || ''))
        if (Number.isFinite(Number(saved.fontSize))) settings.fontSize = Math.max(12, Math.min(32, Number(saved.fontSize)))
        if (Number.isFinite(Number(saved.lineHeight))) settings.lineHeight = Math.max(1.2, Math.min(2.2, Number(saved.lineHeight)))
      } catch (e) {}
      return getReaderSettings()
    })()
  }
  return settingsLoadPromise
}

export function setReaderSettings(patch) {
  if (patch && typeof patch.fontSize === 'number') settings.fontSize = Math.max(12, Math.min(32, patch.fontSize))
  if (patch && typeof patch.lineHeight === 'number') settings.lineHeight = Math.max(1.2, Math.min(2.2, patch.lineHeight))
  const snapshot = getReaderSettings()
  fs.mkdir(SETTINGS_DIR)
    .then(() => fs.writeFile(SETTINGS_FILE, JSON.stringify(snapshot)))
    .catch(e => console.log('[HyperFile Reader] 保存阅读设置失败:', e))
  return snapshot
}
