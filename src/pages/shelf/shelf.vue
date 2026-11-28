<!-- 书架：展示已加入书架的书籍，点按续读。 -->
<template>
  <div class="app-page">
    <div class="app-topbar">
      <div class="icon-btn" @click="goBack">
        <image class="material-icon" src="/assets/icons/arrow_back.png" />
      </div>
      <text class="app-title">书架</text>
      <div class="top-spacer"></div>
      <div class="icon-btn gap-left" @click="openDefaultSettings">
        <image class="material-icon" src="/assets/icons/settings.png" />
      </div>
      <div class="icon-btn gap-left" @click="refresh">
        <image class="material-icon" src="/assets/icons/refresh.png" />
      </div>
    </div>

    <div class="local-status" v-if="loading || books.length === 0">
      <text class="empty-text">{{ loading ? '正在读取书架…' : '书架空空的，在文件操作里把 TXT 加入书架吧' }}</text>
    </div>

    <scroller v-else class="local-shelf" :style="{flexDirection: 'row'}" scroll-direction="horizontal" show-scrollbar="false">
      <div class="local-file-card" v-for="book in books" :key="book.id">
        <div class="local-card-open" @click="openBook(book)">
          <div class="local-card-cover">
            <image class="local-card-icon" src="/assets/icons/text.png" />
          </div>
          <div class="local-card-name-viewport">
            <text :class="book.name && book.name.length > 12 ? 'local-card-name local-card-name-marquee' : 'local-card-name'">{{ book.name }}</text>
          </div>
          <text class="local-card-meta">{{ book.pageCount }} 页 · 已读 {{ percentOf(book) }}%</text>
        </div>
        <div class="shelf-card-actions" :style="{flexDirection: 'row'}">
          <div class="shelf-card-action" @click="openBookSettings(book)">
            <text class="shelf-settings-text">设置</text>
          </div>
          <div class="shelf-card-action shelf-card-action-delete" @click="askRemove(book)">
            <text class="shelf-remove-text">删除</text>
          </div>
        </div>
      </div>
    </scroller>

    <div class="toast" v-if="toast">
      <text class="toast-text">{{ toast }}</text>
    </div>

    <div class="dialog-mask" v-if="removeTarget">
      <div class="dialog-card">
        <text class="dialog-title">删除书籍</text>
        <text class="dialog-description">删除《{{ removeTarget.name }}》会移除书架保存的原件和分页缓存；原目录中的文件不受影响。确认？</text>
        <div class="dialog-actions">
          <div class="dialog-button" @click="removeTarget = null">
            <text class="dialog-button-label">取消</text>
          </div>
          <div class="dialog-button dialog-button-primary" @click="deleteBook">
            <text class="dialog-button-primary-label">删除</text>
          </div>
        </div>
      </div>
    </div>
  </div>
</template>

<script>
import { listBooks, removeBook } from '../../utils/Bookshelf.js'
import { setReaderTarget, getReaderSettings, loadReaderSettings } from '../../utils/ReaderSession.js'
import { withTimeout } from '../../utils/async.js'

export default {
  name: 'shelf',
  data() {
    return {
      books: [],
      loading: false,
      refreshPromise: null,
      removeTarget: null,
      toast: ''
    }
  },
  // 首次加载由 Vue created 触发（必火）；页面钩子双保险：
  // methods 同名项供 BasePage._callRootHook 转发，顶层同名项供运行时 $options 直调。
  created() {
    console.log('[HyperFile][shelf] created: start refresh')
    this.refresh()
  },
  onShow() { if (this && this.refresh) this.refresh() },
  methods: {
    onShow() {
      this.refresh()
    },
    openDefaultSettings() {
      $falcon.navTo('shelf-settings')
    },
    openBookSettings(book) {
      $falcon.navTo('shelf-settings', { bookId: book.id })
    },
    refresh() {
      if (this.refreshPromise) return this.refreshPromise
      console.log('[HyperFile][shelf] refresh: begin')
      this.loading = true
      const task = withTimeout((async () => {
        await loadReaderSettings()
        this.books = await listBooks()
        console.log('[HyperFile][shelf] refresh: complete, books=', this.books.length)
      })(), 15000, '书架读取')
      this.refreshPromise = task
      task.then(
        () => {
          this.loading = false
          if (this.refreshPromise === task) this.refreshPromise = null
        },
        (e) => {
          console.log('[HyperFile][shelf] refresh: failed', e)
          this.loading = false
          this.showToast('书架读取失败：' + (e && e.message ? e.message : e))
          if (this.refreshPromise === task) this.refreshPromise = null
        }
      )
      return task
    },
    percentOf(book) {
      if (!book.pageCount) return 0
      return Math.round(((book.progress || 0) + 1) / book.pageCount * 100)
    },
    openBook(book) {
      setReaderTarget(book.path, book.name, book)
      $falcon.navTo('reader')
    },
    askRemove(book) {
      this.removeTarget = book
    },
    async deleteBook() {
      const book = this.removeTarget
      this.removeTarget = null
      if (!book) return
      try {
        this.books = await removeBook(book.id)
        this.showToast('书籍已删除')
      } catch (e) {
        this.showToast('删除失败：' + (e && e.message ? e.message : e))
      }
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
