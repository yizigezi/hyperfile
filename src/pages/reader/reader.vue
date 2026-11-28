<!--
  文本阅读器（readex-nt 阅读器移植版，纯 JS 分页，无原生依赖）
  - txt：分页浏览（左右翻页）+ 小说章节目录（自动生成，可跳转）
  - 书架书籍：加入时已预分页，按 chunk 懒加载，进度记忆到 shelf.json
  - 其他文本：保留滚动浏览；ad-hoc 阅读进度记 reader-history.json
  - 设置：字号 / 行距步进器（readex-nt 同款），改动后按比例保持阅读位置
  几何沿 readex-nt：左轨 68 / 右轨 76 / 文本区 624×230（800×254 横屏）
-->
<template>
  <div class="reader-page" :style="{flexDirection: 'row'}">
    <div class="left-rail">
      <div class="rail-button back-button" @click="goBack">
        <image class="material-icon" src="/assets/icons/arrow_back.png" />
      </div>
      <div class="rail-button rail-button-accent" v-if="mode === 'paged' && chapters.length > 0" @click="toggleToc">
        <image class="material-icon" src="/assets/icons/description.png" />
      </div>
      <text class="rail-progress" v-if="mode === 'paged'">{{ progressLabel }}</text>
    </div>

    <div class="center-content">
      <text v-if="mode === 'paged'" class="reader-page-text" :style="textStyle">{{ pageText }}</text>
      <scroller v-else class="reader-scroll-area" scroll-y="true" show-scrollbar="false">
        <text class="reader-scroll-text" :style="textStyle">{{ rawText }}</text>
      </scroller>
    </div>

    <div class="right-rail">
      <div class="rail-button" v-if="mode === 'paged'" @click="previousPage">
        <image class="material-icon" src="/assets/icons/chevron_left.png" />
      </div>
      <div class="rail-button" v-if="mode === 'paged'" @click="nextPage">
        <image class="material-icon" src="/assets/icons/chevron_right.png" />
      </div>
      <div class="rail-button" v-if="!book" @click="toggleSettings">
        <image class="material-icon" src="/assets/icons/settings.png" />
      </div>
    </div>

    <!-- 目录（小说章节自动生成） -->
    <div class="reader-panel-mask" v-if="tocVisible" @click="tocVisible = false">
      <div class="toc-panel" @click.stop>
        <div class="reader-panel-header" :style="{flexDirection: 'row'}">
          <text class="reader-panel-title">目录 · {{ chapters.length }} 章</text>
          <div class="reader-panel-close" @click="tocVisible = false">
            <image class="material-icon" src="/assets/icons/close.png" />
          </div>
        </div>
        <scroller class="toc-scroll" scroll-y="true" show-scrollbar="false">
          <div class="toc-row" v-for="(chapter, index) in chapters" :key="index" @click="jumpToChapter(index)">
            <text :class="index === currentChapter ? 'toc-label toc-label-active' : 'toc-label'">{{ chapter.title }}</text>
            <text class="toc-page">{{ chapter.page + 1 }}</text>
          </div>
        </scroller>
      </div>
    </div>

    <!-- 阅读设置 -->
    <div class="reader-panel-mask" v-if="settingsVisible" @click="settingsVisible = false">
      <div class="reader-panel" @click.stop>
        <div class="reader-panel-header" :style="{flexDirection: 'row'}">
          <text class="reader-panel-title">阅读设置</text>
          <div class="reader-panel-close" @click="settingsVisible = false">
            <image class="material-icon" src="/assets/icons/close.png" />
          </div>
        </div>
        <div class="reader-setting-row" :style="{flexDirection: 'row'}">
          <text class="reader-setting-label">字号</text>
          <div class="reader-stepper" :style="{flexDirection: 'row'}">
            <div class="reader-stepper-btn" @click="changeFontSize(-1)">
              <image class="material-icon" src="/assets/icons/remove.png" />
            </div>
            <text class="reader-stepper-value">{{ settings.fontSize }} px</text>
            <div class="reader-stepper-btn" @click="changeFontSize(1)">
              <image class="material-icon" src="/assets/icons/add.png" />
            </div>
          </div>

          <div class="dialog-mask pagination-loading-mask" v-if="busy">
            <div class="dialog-card pagination-loading-card">
              <text class="dialog-title">{{ busyMessage || '正在处理分页' }}</text>
              <text class="dialog-description">请稍候，完成后会自动继续。</text>
            </div>
          </div>
        </div>
        <div class="reader-setting-row" :style="{flexDirection: 'row'}">
          <text class="reader-setting-label">行距</text>
          <div class="reader-stepper" :style="{flexDirection: 'row'}">
            <div class="reader-stepper-btn" @click="changeLineHeight(-0.05)">
              <image class="material-icon" src="/assets/icons/remove.png" />
            </div>
            <text class="reader-stepper-value">{{ settings.lineHeight.toFixed(2) }} ×</text>
            <div class="reader-stepper-btn" @click="changeLineHeight(0.05)">
              <image class="material-icon" src="/assets/icons/add.png" />
            </div>
          </div>
        </div>
      </div>
    </div>
  </div>
</template>

<script>
import { fs } from '../../utils/FileSystem/FileSystem.js'
import { buildPages, chapterIndexOf } from '../../utils/JsPagination.js'
import { getReaderTarget, getReaderSettings, setReaderSettings, loadReaderSettings } from '../../utils/ReaderSession.js'
import { loadChunk, chunkIndexOf, CHUNK_PAGES, updateProgress, addBook } from '../../utils/Bookshelf.js'

function decodeText(value) {
  if (typeof value === 'string') return value
  if (value == null) return ''
  const isBuffer = value.constructor && value.constructor.name === 'Buffer'
  if (isBuffer && typeof value.toString === 'function') {
    try { return value.toString('utf8') } catch (e) {}
  }
  let bytes = null
  if (typeof ArrayBuffer !== 'undefined' && value instanceof ArrayBuffer) {
    bytes = new Uint8Array(value)
  } else if (typeof ArrayBuffer !== 'undefined' && ArrayBuffer.isView && ArrayBuffer.isView(value)) {
    bytes = new Uint8Array(value.buffer, value.byteOffset, value.byteLength)
  } else if (Array.isArray(value)) {
    bytes = value
  } else if (typeof value.data === 'string') {
    return value.data
  }
  if (!bytes && typeof value.toString === 'function') {
    try {
      const decoded = value.toString('utf8')
      if (typeof decoded === 'string' && decoded !== '[object Object]') return decoded
    } catch (e) {}
  }
  if (!bytes) return String(value)
  let output = ''
  for (let i = 0; i < bytes.length;) {
    const b1 = bytes[i++] & 255
    if (b1 < 0x80) output += String.fromCharCode(b1)
    else if (b1 < 0xe0 && i < bytes.length) {
      output += String.fromCharCode(((b1 & 0x1f) << 6) | (bytes[i++] & 0x3f))
    } else if (b1 < 0xf0 && i + 1 < bytes.length) {
      const b2 = bytes[i++] & 0x3f
      const b3 = bytes[i++] & 0x3f
      output += String.fromCharCode(((b1 & 0x0f) << 12) | (b2 << 6) | b3)
    } else if (i + 2 < bytes.length) {
      const b2 = bytes[i++] & 0x3f
      const b3 = bytes[i++] & 0x3f
      const b4 = bytes[i++] & 0x3f
      const cp = ((b1 & 7) << 18) | (b2 << 12) | (b3 << 6) | b4
      const value = cp - 0x10000
      output += String.fromCharCode(0xd800 + (value >> 10), 0xdc00 + (value & 0x3ff))
    } else output += '\ufffd'
  }
  return output
}

// readex-nt 阅读页几何（800 × 254 横屏）
const TEXT_WIDTH = 624
const TEXT_HEIGHT = 230
const PAGED_MAX_CHARS = 8000000 // 超大文本降级为滚动，避免一次分页卡死
const DATA_DIR = '/userdisk/HyperFile_Data'
const HISTORY_FILE = DATA_DIR + '/reader-history.json'
const HISTORY_LIMIT = 50

export default {
  name: 'reader',
  data() {
    return {
      filePath: '',
      readerTitle: '',
      mode: 'scroll',
      rawText: '',
      pages: [],
      chapters: [],
      currentPage: 0,
      history: [],
      settings: getReaderSettings(),
      settingsVisible: false,
      tocVisible: false,
      book: null,
      chunkPages: [],
      chunkIndex: -1,
      busy: false,
      busyMessage: ''
    }
  },
  created() {
    this.loadTarget()
  },
  onShow() {
    this.loadTarget()
  },
  onHide() {
    this.saveProgress()
  },
  computed: {
    textStyle() {
      return {
        fontSize: this.settings.fontSize + 'px',
        lineHeight: (this.settings.fontSize * this.settings.lineHeight) + 'px'
      }
    },
    pageText() {
      if (this.book) {
        const offset = this.currentPage - this.chunkIndex * CHUNK_PAGES
        return this.chunkPages[offset] || ''
      }
      return this.pages[this.currentPage] || ''
    },
    totalPages() {
      return this.book ? this.book.pageCount : this.pages.length
    },
    progressLabel() {
      if (this.busy) return '重新分页中…'
      const total = this.totalPages
      if (!total) return '—'
      const percent = Math.round((this.currentPage + 1) / total * 100)
      return (this.currentPage + 1) + ' / ' + total + ' 页\n' + percent + '%'
    },
    currentChapter() {
      return chapterIndexOf(this.chapters, this.currentPage)
    }
  },
  methods: {
    async loadTarget() {
      await loadReaderSettings()
      this.settings = getReaderSettings()
      // 双通道取参：ReaderSession 共享存储优先，Falcon 页面参数兜底
      const target = getReaderTarget()
      let path = target.path
      let name = target.name
      if (!path) {
        const options = this.$page && this.$page.options ? this.$page.options : {}
        path = options.path || ''
        name = options.name || ''
      }
      if (!path) {
        this.rawText = '未收到文件路径参数'
        return
      }
      if (path === this.filePath && (!target.book || (this.book && this.book.id === target.book.id))) return
      this.filePath = path
      this.readerTitle = name || path.split('/').pop() || '文本阅读器'
      if (target.book) await this.openBook(target.book)
      else await this.loadText()
    },
    async openBook(book) {
      this.book = book
      this.mode = 'paged'
      this.settings = { fontSize: book.fontSize, lineHeight: book.lineHeight }
      this.chapters = book.chapters || []
      this.currentPage = Math.min(Math.max(0, book.progress || 0), Math.max(0, book.pageCount - 1))
      this.chunkIndex = -1
      this.chunkPages = []
      await this.ensureChunkFor(this.currentPage)
    },
    async ensureChunkFor(page) {
      if (!this.book) return
      const index = chunkIndexOf(page)
      if (index === this.chunkIndex && this.chunkPages.length) return
      const pages = await loadChunk(this.book.id, index)
      this.chunkIndex = index
      this.chunkPages = pages
    },
    async goToPage(page) {
      const total = this.totalPages
      const target = Math.min(Math.max(0, page), Math.max(0, total - 1))
      if (this.book) await this.ensureChunkFor(target)
      this.currentPage = target
      this.saveProgress()
    },
    saveProgress() {
      if (this.book) {
        updateProgress(this.book.id, this.currentPage)
      } else if (this.mode === 'paged') {
        this.saveHistory()
      }
    },
    async loadText() {
      if (!this.filePath) {
        this.rawText = '文件路径无效'
        return
      }
      this.busy = true
      this.busyMessage = '正在读取并生成分页…'
      try {
        await new Promise(resolve => this.$page.setTimeout(resolve, 16))
        await this.loadHistory()
        const saved = this.history.find(item => item && item.path === this.filePath) || null
        try {
          const text = await fs.readFile(this.filePath)
          const decoded = decodeText(text)
          this.rawText = decoded.length ? decoded : '（空文件）'
        } catch (e) {
          this.rawText = '读取失败：' + (e && e.message ? e.message : e)
          return
        }
        const isTxt = /\.txt$/i.test(this.filePath) || /\.txt$/i.test(this.readerTitle)
        if (isTxt && this.rawText.length <= PAGED_MAX_CHARS) {
          this.mode = 'paged'
          this.book = null
          this.buildPagination(0)
          if (saved && Number.isFinite(Number(saved.page))) {
            this.currentPage = Math.max(0, Math.min(Number(saved.page), this.pages.length - 1))
          }
        } else {
          this.mode = 'scroll'
        }
        await this.saveHistory()
      } finally {
        this.busy = false
        this.busyMessage = ''
      }
    },
    linesPerPage() {
      return Math.max(1, Math.floor(TEXT_HEIGHT / (this.settings.fontSize * this.settings.lineHeight)))
    },
    unitsPerLine() {
      return Math.max(8, Math.floor(TEXT_WIDTH / (this.settings.fontSize / 2)))
    },
    buildPagination(ratio) {
      const result = buildPages(this.rawText, {
        unitsPerLine: this.unitsPerLine(),
        linesPerPage: this.linesPerPage()
      })
      this.pages = result.pages
      this.chapters = result.chapters
      const maxIndex = Math.max(0, result.pages.length - 1)
      this.currentPage = Math.min(Math.round(ratio * maxIndex), maxIndex)
    },
    async repaginate() {
      if (this.busy) return
      this.busy = true
      this.busyMessage = '正在重新分页…'
      try {
        await new Promise(resolve => this.$page.setTimeout(resolve, 16))
        const ratio = this.pages.length > 1 ? this.currentPage / (this.pages.length - 1) : 0
        this.buildPagination(ratio)
      } finally {
        this.busy = false
        this.busyMessage = ''
      }
    },
    previousPage() {
      this.goToPage(this.currentPage - 1)
    },
    nextPage() {
      this.goToPage(this.currentPage + 1)
    },
    jumpToChapter(index) {
      const chapter = this.chapters[index]
      if (chapter) this.goToPage(chapter.page)
      this.tocVisible = false
    },
    toggleToc() {
      this.tocVisible = !this.tocVisible
    },
    toggleSettings() {
      this.settingsVisible = !this.settingsVisible
    },
    changeFontSize(delta) {
      const size = this.settings.fontSize + delta
      if (size < 12 || size > 32) return
      this.settings.fontSize = size
      setReaderSettings({ fontSize: size })
      if (this.book) this.rechunkBook()
      else if (this.mode === 'paged') this.repaginate()
      this.saveProgress()
    },
    changeLineHeight(delta) {
      const value = Math.round((this.settings.lineHeight + delta) * 100) / 100
      if (value < 1.2 || value > 2.2) return
      this.settings.lineHeight = value
      setReaderSettings({ lineHeight: value })
      if (this.book) this.rechunkBook()
      else if (this.mode === 'paged') this.repaginate()
      this.saveProgress()
    },
    async rechunkBook() {
      const book = this.book
      if (!book || this.busy) return
      this.busy = true
      this.busyMessage = '正在重新分页并更新缓存…'
      try {
        await new Promise(resolve => this.$page.setTimeout(resolve, 16))
        const ratio = book.pageCount > 1 ? this.currentPage / (book.pageCount - 1) : 0
        const entry = await addBook(book.path, book.name, this.settings)
        entry.progress = Math.round(ratio * Math.max(0, entry.pageCount - 1))
        await updateProgress(entry.id, entry.progress)
        await this.openBook(entry)
      } catch (e) {
        this.readerTitle = '重新分页失败：' + (e && e.message ? e.message : e)
      } finally {
        this.busy = false
        this.busyMessage = ''
      }
    },
    async loadHistory() {
      try {
        const raw = await fs.readFile(HISTORY_FILE)
        const parsed = JSON.parse(String(raw || ''))
        this.history = Array.isArray(parsed) ? parsed : []
      } catch (e) {
        this.history = []
      }
    },
    async saveHistory() {
      if (!this.filePath || this.book) return
      const record = {
        path: this.filePath,
        name: this.readerTitle,
        mode: this.mode,
        page: this.currentPage,
        updatedAt: Date.now()
      }
      const next = [record].concat(this.history.filter(item => item && item.path !== this.filePath))
        .slice(0, HISTORY_LIMIT)
      this.history = next
      try {
        await fs.mkdir(DATA_DIR)
        await fs.writeFile(HISTORY_FILE, JSON.stringify(next))
      } catch (e) {
        console.log('[HyperFile Reader] 保存阅读历史失败:', e)
      }
    },
    goBack() {
      this.saveProgress()
      this.$page.finish()
    }
  }
}
</script>

<style lang="less">
@import '../../styles/material.less';
</style>
