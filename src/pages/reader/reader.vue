<!--
  文本阅读器（readex-nt 阅读器移植版，纯 JS 分页，无原生依赖）
  - txt：分页浏览（左右翻页）+ 小说章节目录（自动生成，可跳转）
  - 其他文本：保留滚动浏览
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
      <scroll v-else class="reader-scroll-area" scroll-y="true">
        <text class="reader-scroll-text" :style="textStyle">{{ rawText }}</text>
      </scroll>
    </div>

    <div class="right-rail">
      <div class="rail-button" v-if="mode === 'paged'" @click="previousPage">
        <image class="material-icon" src="/assets/icons/chevron_left.png" />
      </div>
      <div class="rail-button" v-if="mode === 'paged'" @click="nextPage">
        <image class="material-icon" src="/assets/icons/chevron_right.png" />
      </div>
      <div class="rail-button" @click="toggleSettings">
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
        <scroll class="toc-scroll" scroll-y="true">
          <div class="toc-row" v-for="(chapter, index) in chapters" :key="index" @click="jumpToChapter(index)">
            <text :class="index === currentChapter ? 'toc-label toc-label-active' : 'toc-label'">{{ chapter.title }}</text>
            <text class="toc-page">{{ chapter.page + 1 }}</text>
          </div>
        </scroll>
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
      settings: { fontSize: 17, lineHeight: 1.55 },
      settingsVisible: false,
      tocVisible: false
    }
  },
  async created() {
    // readex 原版在 created 中直接读取 Falcon 路由参数；
    // HyperFile 的 BasePage 会在页面组件创建前保存 $page.options。
    const options = this.$page && this.$page.options ? this.$page.options : {}
    const path = options.path
    if (!path) {
      this.rawText = '未收到文件路径参数'
      return
    }
    this.filePath = path
    this.readerTitle = options.name || path.split('/').pop() || '文本阅读器'
    await this.loadText()
  },
  computed: {
    textStyle() {
      return {
        fontSize: this.settings.fontSize + 'px',
        lineHeight: (this.settings.fontSize * this.settings.lineHeight) + 'px'
      }
    },
    pageText() {
      return this.pages[this.currentPage] || ''
    },
    progressLabel() {
      if (!this.pages.length) return '—'
      const percent = Math.round((this.currentPage + 1) / this.pages.length * 100)
      return (this.currentPage + 1) + ' / ' + this.pages.length + ' 页\n' + percent + '%'
    },
    currentChapter() {
      return chapterIndexOf(this.chapters, this.currentPage)
    }
  },
  methods: {
    async loadText() {
      if (!this.filePath) {
        this.rawText = '文件路径无效'
        return
      }
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
        this.buildPagination(0)
      } else {
        this.mode = 'scroll'
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
    repaginate() {
      const ratio = this.pages.length > 1 ? this.currentPage / (this.pages.length - 1) : 0
      this.buildPagination(ratio)
    },
    previousPage() {
      if (this.currentPage > 0) this.currentPage -= 1
    },
    nextPage() {
      if (this.currentPage < this.pages.length - 1) this.currentPage += 1
    },
    jumpToChapter(index) {
      const chapter = this.chapters[index]
      if (chapter) this.currentPage = Math.min(chapter.page, this.pages.length - 1)
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
      if (this.mode === 'paged') this.repaginate()
    },
    changeLineHeight(delta) {
      const value = Math.round((this.settings.lineHeight + delta) * 100) / 100
      if (value < 1.2 || value > 2.2) return
      this.settings.lineHeight = value
      if (this.mode === 'paged') this.repaginate()
    },
    goBack() {
      this.$page.finish()
    }
  }
}
</script>

<style lang="less">
@import '../../styles/material.less';
</style>
