<!--
  书架（阅读器首页）：展示已加入书架的书籍，点按续读。
  书籍在加入时已完成分页与章节识别，打开只需读清单 + 一个分页块。
-->
<template>
  <div class="app-page">
    <div class="app-topbar">
      <div class="icon-btn" @click="goBack">
        <image class="material-icon" src="/assets/icons/arrow_back.png" />
      </div>
      <text class="app-title">书架</text>
      <div class="top-spacer"></div>
      <div class="icon-btn" @click="refresh">
        <image class="material-icon" src="/assets/icons/refresh.png" />
      </div>
    </div>

    <div class="local-status" v-if="loading || books.length === 0">
      <text class="empty-text">{{ loading ? '正在读取书架…' : '书架空空的，在文件操作里把 TXT 加入书架吧' }}</text>
    </div>

    <scroller v-else class="local-shelf" :style="{flexDirection: 'row'}" scroll-direction="horizontal" show-scrollbar="false">
      <div class="local-file-card" v-for="book in books" :key="book.id" @click="openBook(book)">
        <div class="local-card-cover">
          <image class="local-card-icon" src="/assets/icons/text.png" />
        </div>
        <text class="local-card-name">{{ book.name }}</text>
        <text class="local-card-meta">{{ book.pageCount }} 页 · 已读 {{ percentOf(book) }}%</text>
        <div class="shelf-remove" @click.stop="askRemove(book)">
          <text class="shelf-remove-text">移出书架</text>
        </div>
      </div>
    </scroller>

    <div class="toast" v-if="toast">
      <text class="toast-text">{{ toast }}</text>
    </div>

    <div class="dialog-mask" v-if="removeTarget">
      <div class="dialog-card">
        <text class="dialog-title">移出书架</text>
        <text class="dialog-description">将《{{ removeTarget.name }}》移出书架并删除分页缓存，原文件不受影响。确认？</text>
        <div class="dialog-actions">
          <div class="dialog-button" @click="removeTarget = null">
            <text class="dialog-button-label">取消</text>
          </div>
          <div class="dialog-button dialog-button-primary" @click="removeBook">
            <text class="dialog-button-primary-label">移出</text>
          </div>
        </div>
      </div>
    </div>
  </div>
</template>

<script>
import { listBooks, removeBook } from '../../utils/Bookshelf.js'
import { setReaderTarget } from '../../utils/ReaderSession.js'

export default {
  name: 'shelf',
  data() {
    return {
      books: [],
      loading: false,
      removeTarget: null,
      toast: ''
    }
  },
  onShow() {
    this.refresh()
  },
  methods: {
    async refresh() {
      this.loading = true
      try {
        this.books = await listBooks()
      } catch (e) {
        this.books = []
        this.showToast('书架读取失败')
      }
      this.loading = false
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
    async removeBook() {
      const book = this.removeTarget
      this.removeTarget = null
      if (!book) return
      try {
        this.books = await removeBook(book.id)
        this.showToast('已移出书架')
      } catch (e) {
        this.showToast('移出失败：' + (e && e.message ? e.message : e))
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
