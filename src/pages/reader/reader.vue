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
import { getReaderTarget } from '../../utils/ReaderSession.js'

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
  onShow() {
    const target = getReaderTarget()
    if (target.path && target.path !== this.filePath) {
      this.filePath = target.path
      this.readerTitle = target.name || target.path.split('/').pop() || '文本阅读器'
      this.loadText()
    }
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
        this.rawText = typeof text === 'string' && text ? text : (text ? String(text) : '（空文件）')
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
