/**
 * Bookshelf —— 书架存储（纯前端 JS + fs 门面）。
 *
 * 目录布局（/userdisk/HyperFile/bookshelf/）：
 *   shelf.json        书籍清单（含元数据、章节目录、阅读进度）
 *   <id>/chunk-0.json 每 10 页一个分页缓存 {start, pages: [...]}
 *   <id>/chunk-1.json …
 *
 * 加入书架时即按当前字号/行距完成分页与章节识别，阅读时按需读 chunk，
 * 打开书籍只需读 shelf.json + 一个 chunk，最大限度降低加载时间。
 */
import { fs } from './FileSystem/FileSystem.js'
import { buildPages } from './JsPagination.js'

const SHELF_DIR = '/userdisk/HyperFile/bookshelf'
const SHELF_FILE = SHELF_DIR + '/shelf.json'
export const CHUNK_PAGES = 10

// 与阅读页几何一致（800×254 横屏：文本区 624×230）
const TEXT_WIDTH = 624
const TEXT_HEIGHT = 230
const ADD_MAX_CHARS = 3000000

function sanitizeId(name) {
  const value = String(name || 'book').replace(/[^\w\u4e00-\u9fa5-]+/g, '_')
  return (value.slice(0, 24) || 'book') + '-' + Date.now()
}

function chunkFile(id, index) {
  return SHELF_DIR + '/' + id + '/chunk-' + index + '.json'
}

export function chunkIndexOf(page) {
  return Math.floor(page / CHUNK_PAGES)
}

export function paginationOptions(settings) {
  return {
    unitsPerLine: Math.max(8, Math.floor(TEXT_WIDTH / (settings.fontSize / 2))),
    linesPerPage: Math.max(1, Math.floor(TEXT_HEIGHT / (settings.fontSize * settings.lineHeight)))
  }
}

async function loadShelf() {
  try {
    if (!(await fs.exists(SHELF_FILE))) return []
    const text = await fs.readFile(SHELF_FILE)
    const list = JSON.parse(text)
    return Array.isArray(list) ? list : []
  } catch (e) {
    return []
  }
}

async function saveShelf(list) {
  await fs.mkdir(SHELF_DIR)
  await fs.writeFile(SHELF_FILE, JSON.stringify(list))
}

export async function listBooks() {
  return loadShelf()
}

export async function getBook(id) {
  const list = await loadShelf()
  for (let i = 0; i < list.length; i++) {
    if (list[i].id === id) return list[i]
  }
  return null
}

export async function loadChunk(id, index) {
  try {
    const text = await fs.readFile(chunkFile(id, index))
    const data = JSON.parse(text)
    return Array.isArray(data && data.pages) ? data.pages : []
  } catch (e) {
    return []
  }
}

export async function updateProgress(id, page) {
  const list = await loadShelf()
  for (let i = 0; i < list.length; i++) {
    if (list[i].id === id) {
      list[i].progress = Math.max(0, Math.floor(page) || 0)
      await saveShelf(list)
      return list[i]
    }
  }
  return null
}

export async function removeBook(id) {
  const list = await loadShelf()
  const next = []
  for (let i = 0; i < list.length; i++) {
    if (list[i].id !== id) next.push(list[i])
  }
  await saveShelf(next)
  try {
    await fs.rm(SHELF_DIR + '/' + id)
  } catch (e) {}
  return next
}

/**
 * 修改书籍显示名称（不改动原文件名）。
 */
export async function renameBook(id, name) {
  const title = String(name || '').trim()
  if (!title) throw new Error('书名不能为空')
  const list = await loadShelf()
  let found = false
  for (let i = 0; i < list.length; i++) {
    if (list[i].id === id) {
      list[i].name = title
      found = true
      break
    }
  }
  if (!found) throw new Error('书籍不存在')
  await saveShelf(list)
  return list
}

/**
 * 用新字号/行距重新生成书籍分页缓存，按原阅读进度比例恢复位置。
 */
export async function updateBookSettings(id, settings) {
  const book = await getBook(id)
  if (!book) throw new Error('书籍不存在')
  const ratio = book.pageCount > 1 ? (book.progress || 0) / (book.pageCount - 1) : 0
  const entry = await addBook(book.path, book.name, settings)
  entry.progress = Math.round(ratio * Math.max(0, entry.pageCount - 1))
  await updateProgress(entry.id, entry.progress)
  return entry
}

/**
 * 加入（或按新设置重加）书架：分页 + 章节识别 + 分块落盘。
 * 同一路径的旧条目会被替换（重复加入 = 按当前设置重新分页）。
 */
export async function addBook(filePath, name, settings) {
  const text = await fs.readFile(filePath)
  const content = typeof text === 'string' ? text : (text ? String(text) : '')
  if (!content) throw new Error('文件为空')
  if (content.length > ADD_MAX_CHARS) throw new Error('文件过大（超过 ' + ADD_MAX_CHARS + ' 字符）')

  const opts = paginationOptions(settings)
  const result = buildPages(content, opts)

  await fs.mkdir(SHELF_DIR)
  const id = sanitizeId(name)
  await fs.mkdir(SHELF_DIR + '/' + id)
  for (let start = 0; start < result.pages.length; start += CHUNK_PAGES) {
    const chunk = {
      start: start,
      pages: result.pages.slice(start, start + CHUNK_PAGES)
    }
    await fs.writeFile(chunkFile(id, chunk.start / CHUNK_PAGES), JSON.stringify(chunk))
  }

  const entry = {
    id: id,
    name: String(name || filePath.split('/').pop() || '未命名'),
    path: filePath,
    fontSize: settings.fontSize,
    lineHeight: settings.lineHeight,
    pageCount: result.pages.length,
    chunkCount: Math.ceil(result.pages.length / CHUNK_PAGES),
    chapters: result.chapters,
    progress: 0,
    addedAt: Date.now()
  }

  const list = await loadShelf()
  const next = [entry]
  for (let i = 0; i < list.length; i++) {
    if (list[i].path === filePath) {
      try {
        await fs.rm(SHELF_DIR + '/' + list[i].id)
      } catch (e) {}
      continue
    }
    next.push(list[i])
  }
  await saveShelf(next)
  return entry
}
