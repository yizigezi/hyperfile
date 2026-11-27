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
      <div class="local-file-card" v-for="book in books" :key="book.id" @click="openBook(book)">
        <div class="local-card-cover">
          <image class="local-card-icon" src="/assets/icons/text.png" />
        </div>
        <text class="local-card-name">{{ book.name }}</text>
        <text class="local-card-meta">{{ book.pageCount }} 页 · 已读 {{ percentOf(book) }}%</text>
        <div class="shelf-remove" @click.stop="openBookSettings(book)">
          <text class="shelf-settings-text">书籍设置</text>
        </div>
      </div>
    </scroller>


    <div class="dialog-mask" v-if="settingsVisible">
      <div class="dialog-card shelf-settings-card">
        <text class="dialog-title">{{ settingsTarget ? '书籍设置' : '默认阅读设置' }}</text>
        <div class="reader-setting-row" :style="{flexDirection: 'row'}">
          <text class="reader-setting-label">字号</text>
          <div class="reader-stepper" :style="{flexDirection: 'row'}">
            <div class="reader-stepper-btn" @click="changeDraftFont(-1)"><image class="material-icon" src="/assets/icons/remove.png" /></div>
            <text class="reader-stepper-value">{{ draftSettings.fontSize }} px</text>
            <div class="reader-stepper-btn" @click="changeDraftFont(1)"><image class="material-icon" src="/assets/icons/add.png" /></div>
          </div>
        </div>
        <div class="reader-setting-row" :style="{flexDirection: 'row'}">
          <text class="reader-setting-label">行距</text>
          <div class="reader-stepper" :style="{flexDirection: 'row'}">
            <div class="reader-stepper-btn" @click="changeDraftLine(-0.05)"><image class="material-icon" src="/assets/icons/remove.png" /></div>
            <text class="reader-stepper-value">{{ draftSettings.lineHeight.toFixed(2) }} ×</text>
            <div class="reader-stepper-btn" @click="changeDraftLine(0.05)"><image class="material-icon" src="/assets/icons/add.png" /></div>
          </div>
        </div>
        <div v-if="settingsTarget" class="shelf-rename-row">
          <input class="text-input shelf-rename-input" v-model="renameValue" placeholder="书籍名称" />
          <div class="dialog-button dialog-button-primary shelf-rename-button" @click="renameSelected"><text class="dialog-button-primary-label">重命名</text></div>
        </div>
        <div class="dialog-actions">
          <div class="dialog-button" @click="settingsVisible = false"><text class="dialog-button-label">取消</text></div>
          <div class="dialog-button dialog-button-primary" @click="applySettings"><text class="dialog-button-primary-label">{{ settingsTarget ? '应用并重新分页' : '保存默认设置' }}</text></div>
        </div>
        <div v-if="settingsTarget" class="shelf-remove-settings" @click="askRemove(settingsTarget); settingsVisible = false">
          <text class="shelf-remove-text">移出书架</text>
        </div>
      </div>
    </div>

    <div class="dialog-mask" v-if="repaginateConfirm">
      <div class="dialog-card">
        <text class="dialog-title">重新分页</text>
        <text class="dialog-description">字号或行距已更改，需要重新生成这本书的分页缓存。继续吗？</text>
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
import { listBooks, removeBook, updateBookSettings, renameBook } from '../../utils/Bookshelf.js'
import { setReaderTarget, getReaderSettings, setReaderSettings, loadReaderSettings } from '../../utils/ReaderSession.js'

export default {
  name: 'shelf',
  data() {
    return {
      books: [],
      loading: false,
      removeTarget: null,
      toast: '',
      settingsVisible: false,
      settingsTarget: null,
      draftSettings: { fontSize: 17, lineHeight: 1.55 },
      renameValue: '',
      repaginateConfirm: false,
      repaginating: false
    }
  },
  onShow() {
    this.refresh()
  },
  methods: {
    openDefaultSettings() {
      this.settingsTarget = null
      this.draftSettings = getReaderSettings()
      this.settingsVisible = true
    },
    openBookSettings(book) {
      this.settingsTarget = book
      this.draftSettings = { fontSize: book.fontSize, lineHeight: book.lineHeight }
      this.renameValue = book.name
      this.settingsVisible = true
    },
    changeDraftFont(delta) {
      const value = this.draftSettings.fontSize + delta
      if (value >= 12 && value <= 32) this.draftSettings.fontSize = value
    },
    changeDraftLine(delta) {
      const value = Math.round((this.draftSettings.lineHeight + delta) * 100) / 100
      if (value >= 1.2 && value <= 2.2) this.draftSettings.lineHeight = value
    },
    async applySettings() {
      if (!this.settingsTarget) {
        setReaderSettings(this.draftSettings)
        this.settingsVisible = false
        this.showToast('默认阅读设置已保存')
        return
      }
      const book = this.settingsTarget
      if (book.fontSize === this.draftSettings.fontSize && book.lineHeight === this.draftSettings.lineHeight) {
        this.settingsVisible = false
        this.showToast('设置未变化')
        return
      }
      this.settingsVisible = false
      this.repaginateConfirm = true
    },
    async confirmRepaginate() {
      const book = this.settingsTarget
      this.repaginateConfirm = false
      if (!book) return
      this.repaginating = true
      try {
        const updated = await updateBookSettings(book.id, this.draftSettings)
        this.books = await listBooks()
        this.settingsTarget = updated
        this.showToast('分页已更新')
      } catch (e) {
        this.showToast('重新分页失败：' + (e && e.message ? e.message : e))
      }
      this.repaginating = false
    },
    async renameSelected() {
      const book = this.settingsTarget
      if (!book) return
      try {
        this.books = await renameBook(book.id, this.renameValue)
        this.settingsTarget = this.books.find(item => item.id === book.id) || book
        this.showToast('书籍已重命名')
      } catch (e) {
        this.showToast('重命名失败：' + (e && e.message ? e.message : e))
      }
    },
    async refresh() {
      await loadReaderSettings()
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
