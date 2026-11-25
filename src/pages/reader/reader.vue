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
      <text class="reader-text" :style="{ fontSize: readerFontSize + 'px', lineHeight: (readerFontSize + 9) + 'px' }">{{ readerText }}</text>
    </scroll>
  </div>
</template>

<script>
import router from '@system.router'
import { fs } from '../../utils/FileSystem/FileSystem.js'

export default {
  name: 'reader',
  data() {
    return {
      readerTitle: '',
      readerText: '正在读取…',
      readerFontSize: 16,
      filePath: ''
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
        this.readerText = typeof text === 'string' && text ? text : (text ? String(text) : '（空文件）')
      } catch (e) {
        this.readerText = '读取失败：' + (e && e.message ? e.message : e)
      }
    },
    changeReaderFont(delta) {
      let size = this.readerFontSize + delta
      if (size < 12) size = 12
      if (size > 32) size = 32
      this.readerFontSize = size
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
