<template>
  <div class="app-page">
    <div class="app-topbar">
      <div class="icon-btn" @click="goBack">
        <image class="material-icon" src="/assets/icons/arrow_back.png" />
      </div>
      <text class="app-title">{{ book ? book.name : '默认阅读设置' }}</text>
      <div class="top-spacer"></div>
    </div>

    <div class="local-status" v-if="loading || (!isDefault && !book)">
      <text class="empty-text">{{ loading ? '正在读取书籍设置…' : '书籍不存在或已删除' }}</text>
    </div>
    <scroller v-else class="shelf-settings-shelf" :style="{flexDirection: 'row'}" scroll-direction="horizontal" show-scrollbar="false">
      <div class="shelf-setting-card">
        <text class="section-heading">{{ book ? '阅读排版' : '默认阅读设置' }}</text>
        <div class="shelf-setting-row" :style="{flexDirection: 'row'}">
          <text class="reader-setting-label">字号</text>
          <div class="reader-stepper" :style="{flexDirection: 'row'}">
            <div class="reader-stepper-btn shelf-stepper-btn" @click="changeFont(-1)"><image class="material-icon" src="/assets/icons/remove.png" /></div>
            <text class="reader-stepper-value shelf-stepper-value">{{ draftSettings.fontSize }}</text>
            <div class="reader-stepper-btn shelf-stepper-btn" @click="changeFont(1)"><image class="material-icon" src="/assets/icons/add.png" /></div>
          </div>
        </div>
        <div class="shelf-setting-row" :style="{flexDirection: 'row'}">
          <text class="reader-setting-label">行距</text>
          <div class="reader-stepper" :style="{flexDirection: 'row'}">
            <div class="reader-stepper-btn shelf-stepper-btn" @click="changeLine(-0.05)"><image class="material-icon" src="/assets/icons/remove.png" /></div>
            <text class="reader-stepper-value shelf-stepper-value">{{ draftSettings.lineHeight.toFixed(2) }}</text>
            <div class="reader-stepper-btn shelf-stepper-btn" @click="changeLine(0.05)"><image class="material-icon" src="/assets/icons/add.png" /></div>
          </div>
        </div>
        <div class="shelf-setting-save" @click="applySettings">
          <text class="shelf-setting-save-label">{{ book ? '应用并重新分页' : '保存默认设置' }}</text>
        </div>
      </div>

      <div class="shelf-setting-card shelf-book-info-card" v-if="book">
        <text class="section-heading">书籍信息</text>
        <text class="shelf-book-info">{{ book.pageCount }} 页 · 已读 {{ percentOf(book) }}%</text>
        <input class="text-input shelf-rename-input" v-model="renameValue" placeholder="书籍名称" />
        <div class="shelf-setting-save" @click="renameSelected">
          <text class="shelf-setting-save-label">保存书名</text>
        </div>
      </div>
    </scroller>

    <div class="toast" v-if="toast">
      <text class="toast-text">{{ toast }}</text>
    </div>

    <div class="dialog-mask" v-if="repaginateConfirm">
      <div class="dialog-card">
        <text class="dialog-title">重新分页</text>
        <text class="dialog-description">字号或行距已更改，将根据书架保存的原件重新生成分页缓存。继续吗？</text>
        <div class="dialog-actions">
          <div class="dialog-button" @click="repaginateConfirm = false"><text class="dialog-button-label">取消</text></div>
          <div class="dialog-button dialog-button-primary" @click="confirmRepaginate"><text class="dialog-button-primary-label">重新分页</text></div>
        </div>
      </div>
    </div>

    <div class="dialog-mask shelf-loading-mask" v-if="repaginating">
      <div class="dialog-card shelf-loading-card">
        <text class="dialog-title">正在重新分页</text>
        <text class="dialog-description">正在更新书籍页面和章节目录，请稍候…</text>
      </div>
    </div>
  </div>
</template>

<script>
import { listBooks, updateBookSettings, renameBook } from '../../utils/Bookshelf.js'
import { getReaderSettings, setReaderSettings, loadReaderSettings } from '../../utils/ReaderSession.js'
import { withTimeout } from '../../utils/async.js'

export default {
  name: 'shelf-settings',
  data() {
    return {
      book: null,
      isDefault: true,
      loading: true,
      loaded: false,
      loadPromise: null,
      draftSettings: { fontSize: 17, lineHeight: 1.55 },
      renameValue: '',
      repaginateConfirm: false,
      repaginating: false,
      toast: ''
    }
  },
  // 首次读取由 Vue created 触发（必火）；页面钩子双保险：
  // methods 同名项供 BasePage._callRootHook 转发，顶层同名项供运行时 $options 直调。
  created() {
    console.log('[HyperFile][shelf-settings] created: start load')
    this.load()
  },
  onShow() { if (this && this.load && !this.loaded) this.load() },
  onNewOptions() { if (this && this.load) this.load(true) },
  methods: {
    onShow() {
      if (!this.loaded) this.load()
    },
    onNewOptions() {
      this.load(true)
    },
    load(force) {
      if (this.loadPromise && !force) return this.loadPromise
      console.log('[HyperFile][shelf-settings] load: begin')
      this.loading = true
      const task = withTimeout((async () => {
        await loadReaderSettings()
        console.log('[HyperFile][shelf-settings] reader settings loaded')
        this.draftSettings = getReaderSettings()
        const options = this.$page && this.$page.options ? this.$page.options : {}
        const bookId = options.bookId || ''
        this.isDefault = !bookId
        this.book = null
        if (bookId) {
          console.log('[HyperFile][shelf-settings] loading book:', bookId)
          const books = await listBooks()
          console.log('[HyperFile][shelf-settings] bookshelf loaded:', books.length)
          this.book = books.find(item => item.id === bookId) || null
          if (this.book) {
            this.draftSettings = { fontSize: this.book.fontSize, lineHeight: this.book.lineHeight }
            this.renameValue = this.book.name
          } else {
            this.showToast('书籍不存在或已删除')
          }
        }
      })(), 15000, '设置读取')
      this.loadPromise = task
      task.then(
        () => {
          this.loading = false
          this.loaded = true
          console.log('[HyperFile][shelf-settings] load: finished')
          if (this.loadPromise === task) this.loadPromise = null
        },
        (e) => {
          console.log('[HyperFile][shelf-settings] load: failed', e)
          this.loading = false
          this.showToast('设置读取失败：' + (e && e.message ? e.message : e))
          if (this.loadPromise === task) this.loadPromise = null
        }
      )
      return task
    },
    changeFont(delta) {
      const value = this.draftSettings.fontSize + delta
      if (value >= 12 && value <= 32) this.draftSettings.fontSize = value
    },
    changeLine(delta) {
      const value = Math.round((this.draftSettings.lineHeight + delta) * 100) / 100
      if (value >= 1.2 && value <= 2.2) this.draftSettings.lineHeight = value
    },
    async applySettings() {
      if (this.isDefault) {
        setReaderSettings(this.draftSettings)
        this.showToast('默认阅读设置已保存')
        return
      }
      if (!this.book) return
      if (this.book.fontSize === this.draftSettings.fontSize && this.book.lineHeight === this.draftSettings.lineHeight) {
        this.showToast('设置未变化')
        return
      }
      this.repaginateConfirm = true
    },
    async confirmRepaginate() {
      this.repaginateConfirm = false
      if (!this.book) return
      this.repaginating = true
      try {
        await new Promise(resolve => this.$page.setTimeout(resolve, 16))
        this.book = await updateBookSettings(this.book.id, this.draftSettings)
        this.renameValue = this.book.name
        this.showToast('分页已更新')
      } catch (e) {
        this.showToast('重新分页失败：' + (e && e.message ? e.message : e))
      } finally {
        this.repaginating = false
      }
    },
    async renameSelected() {
      if (!this.book) return
      try {
        const books = await renameBook(this.book.id, this.renameValue)
        this.book = books.find(item => item.id === this.book.id) || this.book
        this.renameValue = this.book.name
        this.showToast('书籍已重命名')
      } catch (e) {
        this.showToast('重命名失败：' + (e && e.message ? e.message : e))
      }
    },
    percentOf(book) {
      if (!book.pageCount) return 0
      return Math.round(((book.progress || 0) + 1) / book.pageCount * 100)
    },
    showToast(msg) {
      this.toast = msg
      this.$page.setTimeout(() => { this.toast = '' }, 3000)
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
