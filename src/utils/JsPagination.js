/**
 * JsPagination —— 纯 JS 文本分页引擎（无原生依赖）
 *
 * 思路（与 readex-nt 的几何约定对齐）：
 *   1. 按字符显示宽度把逻辑行折成显示行：全角/CJK 记 2 单位，半角记 1 单位，
 *      每行可容纳 unitsPerLine 个单位（= 文本区宽度 / (字号 / 2)）。
 *   2. 每 linesPerPage 个显示行合成一页。
 *   3. 章节识别：行首匹配「第X章/节/回/卷/篇/集」「序章/楔子/番外…」「Chapter N」
 *      且整行较短的视为章节标题，输出 {title, page} 供目录跳转。
 *
 * 已知取舍（simple by design）：
 *   - 贪心折行，不做标点悬挂/避头尾；
 *   - 分页按显示行切分，字体或行距变化后整体重算；
 *   - 超大文本由调用方自行决定是否降级为滚动模式。
 */

const UNIT_TABLE_THRESHOLD = 0x1100

function charUnits(cp) {
  if (cp === 0x09) return 4
  if (cp < UNIT_TABLE_THRESHOLD) return 1
  if (
    (cp >= 0x2e80 && cp <= 0x303e) ||
    (cp >= 0x3041 && cp <= 0x33ff) ||
    (cp >= 0x3400 && cp <= 0x4dbf) ||
    (cp >= 0x4e00 && cp <= 0x9fff) ||
    (cp >= 0xa000 && cp <= 0xa4cf) ||
    (cp >= 0xac00 && cp <= 0xd7a3) ||
    (cp >= 0xf900 && cp <= 0xfaff) ||
    (cp >= 0xfe30 && cp <= 0xfe4f) ||
    (cp >= 0xff00 && cp <= 0xff60) ||
    (cp >= 0xffe0 && cp <= 0xffe6)
  ) {
    return 2
  }
  return 1
}

function wrapLine(line, unitsPerLine) {
  const out = []
  let current = ''
  let units = 0
  let i = 0
  while (i < line.length) {
    const cp = line.codePointAt(i)
    const ch = String.fromCodePoint(cp)
    const w = charUnits(cp)
    if (units + w > unitsPerLine && current) {
      out.push(current)
      current = ''
      units = 0
      continue
    }
    current += ch
    units += w
    i += ch.length
  }
  out.push(current)
  return out
}

const CHAPTER_PATTERNS = [
  /^\s*第[0-9０-９零〇一二三四五六七八九十百千万亿两]+\s*[章卷回节部篇集][^\n]{0,40}$/,
  /^\s*(序章|楔子|前言|引子|后记|尾声|终章|番外)[^\n]{0,40}$/,
  /^\s*(Chapter|CHAPTER)\s+[0-9IVXLC]+[^\n]{0,40}$/
]

function isChapterLine(line) {
  const trimmed = line.trim()
  if (!trimmed || trimmed.length > 50) return false
  for (let i = 0; i < CHAPTER_PATTERNS.length; i++) {
    if (CHAPTER_PATTERNS[i].test(trimmed)) return true
  }
  return false
}

/**
 * 把整篇文本分页。
 * @param {string} text 原文
 * @param {{unitsPerLine: number, linesPerPage: number}} options
 * @returns {{pages: string[], chapters: {title: string, page: number}[], totalLines: number}}
 */
export function buildPages(text, options) {
  const unitsPerLine = Math.max(8, Math.floor(options.unitsPerLine) || 36)
  const linesPerPage = Math.max(1, Math.floor(options.linesPerPage) || 8)

  const logicalLines = String(text || '').split('\n')
  const displayLines = []
  const chapterMarks = []

  for (let li = 0; li < logicalLines.length; li++) {
    const line = logicalLines[li]
    const start = displayLines.length
    if (isChapterLine(line)) {
      chapterMarks.push({ title: line.trim().replace(/\s+/g, ' ').slice(0, 24), line: start })
    }
    const wrapped = wrapLine(line, unitsPerLine)
    for (let w = 0; w < wrapped.length; w++) displayLines.push(wrapped[w])
  }

  const pages = []
  for (let i = 0; i < displayLines.length; i += linesPerPage) {
    pages.push(displayLines.slice(i, i + linesPerPage).join('\n'))
  }
  if (!pages.length) pages.push('')

  const chapters = []
  for (let i = 0; i < chapterMarks.length; i++) {
    const mark = chapterMarks[i]
    const page = Math.min(Math.floor(mark.line / linesPerPage), pages.length - 1)
    if (chapters.length && chapters[chapters.length - 1].page === page) continue
    chapters.push({ title: mark.title, page })
  }

  return { pages, chapters, totalLines: displayLines.length }
}

export async function buildPagesNative(text, options) {
  try {
    const native = await import('hyperfile_local_ops').then((m) => m.default || m)
    if (!native || typeof native.buildPages !== 'function') return null
    const started = Date.now()
    const result = native.buildPages(
      text,
      Math.max(8, Math.floor(options.unitsPerLine) || 36),
      Math.max(1, Math.floor(options.linesPerPage) || 8)
    )
    return {
      result,
      engine: 'native',
      elapsedMs: Date.now() - started
    }
  } catch (e) {
    return null
  }
}

/** 当前页对应的章节下标（取页码不大于当前页的最后一个章节）。 */
export function chapterIndexOf(chapters, page) {
  let index = -1
  for (let i = 0; i < chapters.length; i++) {
    if (chapters[i].page <= page) index = i
    else break
  }
  return index
}
