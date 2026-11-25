<template>
  <div class="reader-page">
    <div class="reader-bar">
      <div class="reader-back" @click="goBack">
        <image class="reader-back-icon" src="/assets/icons/arrow_back.png" />
      </div>
      <text class="reader-title">{{ readerTitle }}</text>
      <div class="reader-size-btn" @click="changeReaderFont(-2)">
        <text class="reader-size-label">A−</text>
      </div>
      <div class="reader-size-btn" @click="changeReaderFont(2)">
        <text class="reader-size-label">A+</text>
      </div>
    </div>

    <scroll class="reader-scroll" scroll-y="true">
      <text class="reader-text" :style="{ fontSize: readerFontSize + 'px', lineHeight: readerLineHeight + 'px' }">{{ readerText }}</text>
    </scroll>

    <div class="reader-page-controls">
      <div :class="pageIndex > 0 ? 'reader-page-button' : 'reader-page-button reader-page-button-disabled'" @click="previousPage">
        <text class="reader-page-button-label">上一页</text>
      </div>
      <text class="reader-page-indicator">{{ pageIndex + 1 }} / {{ pages.length }}</text>
      <div :class="pageIndex + 1 < pages.length ? 'reader-page-button' : 'reader-page-button reader-page-button-disabled'" @click="nextPage">
        <text class="reader-page-button-label">下一页</text>
      </div>
    </div>
  </div>
</template>

<script>
import router from '@system.router'
import { fs } from '../../utils/FileSystem/FileSystem.js'
import { paginateText } from '../../utils/reader-pagination.js'

const READER_WIDTH = 772
const READER_HEIGHT = 150

export default {
  name: 'reader',
  data() {
    return {
      readerTitle: '',
      readerText: '正在读取…',
      readerFontSize: 16,
      filePath: '',
      sourceText: '',
      pages: [''],
      pageIndex: 0
    }
  },
  computed: {
    readerLineHeight() {
      return Math.round(this.readerFontSize * 1.45)
    }
  },
  onInit(options) {
    const params = options || {}
    this.filePath = params.path || ''
    this.readerTitle = params.name || this.filePath.split('/').pop() || '文本阅读器'
    this.loadText()
  },
  methods: {
    async loadText() {
      if (!this.filePath) {
        this.readerText = '文件路径无效'
        return
      }
      try {
        const text = await fs.readFile(this.filePath)
        this.sourceText = typeof text === 'string'
          ? text
          : (text ? String(text) : '')
        this.repaginate()
      } catch (e) {
        this.sourceText = ''
        this.pages = ['读取失败：' + (e && e.message ? e.message : e)]
        this.pageIndex = 0
        this.syncPageText()
      }
    },
    repaginate() {
      this.pages = paginateText(this.sourceText, {
        width: READER_WIDTH,
        height: READER_HEIGHT,
        fontSize: this.readerFontSize,
        lineHeight: 1.45,
        letterSpacing: 0,
        paragraphSpacing: 4,
        paddingTop: 0,
        paddingBottom: 0,
        paddingLeft: 0,
        paddingRight: 0
      })
      if (this.pageIndex >= this.pages.length) {
        this.pageIndex = Math.max(0, this.pages.length - 1)
      }
      this.syncPageText()
    },
    syncPageText() {
      this.readerText = this.pages[this.pageIndex] || ''
    },
    previousPage() {
      if (this.pageIndex <= 0) return
      this.pageIndex--
      this.syncPageText()
    },
    nextPage() {
      if (this.pageIndex + 1 >= this.pages.length) return
      this.pageIndex++
      this.syncPageText()
    },
    changeReaderFont(delta) {
      const size = Math.max(12, Math.min(30, this.readerFontSize + delta))
      if (size === this.readerFontSize) return
      this.readerFontSize = size
      this.pageIndex = 0
      this.repaginate()
    },
    goBack() {
      router.back()
    }
  }
}
</script>

<style lang="less">
@import '../../styles/material.less';
</style>
