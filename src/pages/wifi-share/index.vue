<template>
  <div class="share-manager-page">
    <div class="share-manager-header">
      <div class="manager-back" @click="goBack">
        <image class="material-icon" src="/assets/icons/arrow_back.png" />
      </div>
      <text class="share-manager-title">Wi-Fi 共享</text>
      <div class="manager-spacer"></div>
      <div class="manager-refresh" @click="refresh">
        <image class="material-icon" src="/assets/icons/refresh.png" />
      </div>
    </div>
    <scroller class="share-manager-content" scroll-y="true" show-scrollbar="false">
      <div class="manager-panel">
        <text class="section-heading">共享服务</text>
        <text class="manager-status">{{ running ? '服务运行中' : '服务已停止' }}</text>
        <text class="manager-description">{{ running ? address : '启动后可通过同一 Wi-Fi 的浏览器访问共享目录' }}</text>
        <div class="manager-qr" v-if="running && address">
          <FlQrcode :text="address" :size="116" />
        </div>
        <div :class="running ? 'primary-action action-stop' : 'primary-action'" @click="toggle">
          <text :class="running ? 'primary-action-label-stop' : 'primary-action-label'">{{ running ? '停止共享' : (initialized ? '启动共享' : '初始化并启动') }}</text>
        </div>
      </div>
      <div class="manager-panel">
        <text class="section-heading">共享目录</text>
        <text class="manager-path">/userdisk/Favorite/HyperFile</text>
        <text class="manager-description">当前目录包含 {{ files.length }} 个条目</text>
        <div class="manager-file-row" v-for="name in files" :key="name">
          <image class="shared-file-icon" :src="iconFor(name)" />
          <text class="shared-file-name">{{ name }}</text>
        </div>
      </div>
      <div class="manager-panel">
        <text class="section-heading">连接状态</text>
        <text class="manager-description">监听端口：8080</text>
        <text class="manager-description">累计接收：{{ formatSize(received) }}</text>
        <text class="manager-description">网页端可上传、下载和管理共享文件</text>
      </div>
    </scroller>
    <div class="toast" v-if="toast"><text class="toast-text">{{ toast }}</text></div>
  </div>
</template>

<script>
import FlQrcode from 'falcon-ui/src/packages/qr-code/index.vue'
import { fs } from '../../utils/FileSystem/FileSystem.js'

const UPLOAD_DIR = '/userdisk/Favorite/HyperFile'
const PORT = 8080

async function getHttp() {
  return import('hyperfile_http').then((m) => m.default || m)
}

export default {
  name: 'wifi-share',
  components: { FlQrcode },
  data() {
    return { running: false, initialized: false, address: '', received: 0, files: [], toast: '' }
  },
  onShow() {
    this.refresh()
  },
  methods: {
    goBack() { this.$page.finish() },
    showToast(message) {
      this.toast = message
      this.$page.setTimeout(() => { this.toast = '' }, 3000)
    },
    async refresh() {
      try {
        const http = await getHttp()
        const status = await http.status()
        this.running = !!status.running
        this.initialized = !!status.initialized || this.running
        this.received = Number(status.received) || 0
        this.address = status.running && status.ip ? 'http://' + status.ip + ':' + (status.port || PORT) + '/' : ''
      } catch (e) {
        this.showToast('共享服务状态获取失败')
      }
      try {
        await fs.mkdir(UPLOAD_DIR)
        const list = await fs.readdir(UPLOAD_DIR)
        this.files = (list || []).map((item) => typeof item === 'string' ? item : item.name).filter(Boolean)
      } catch (e) {
        this.files = []
      }
    },
    async toggle() {
      try {
        const http = await getHttp()
        if (this.running) {
          const status = await http.stop()
          this.running = !!status.running
          this.address = ''
          this.showToast('共享服务已停止')
          return
        }
        if (!this.initialized) {
          const result = await http.initialize()
          if (result && result.initializing) {
            this.showToast('初始化已启动，请稍后刷新状态')
            return
          }
          this.initialized = true
          this.showToast('初始化完成，请再次启动')
          return
        }
        const status = await http.start(PORT, UPLOAD_DIR)
        this.running = !!status.running
        this.address = status.ip ? 'http://' + status.ip + ':' + (status.port || PORT) + '/' : ''
        this.showToast(this.running ? '共享服务已启动' : '启动失败')
      } catch (e) {
        this.showToast('操作失败：' + (e && e.message ? e.message : e))
      }
    },
    formatSize(size) {
      const n = Number(size) || 0
      if (n < 1024) return n + ' B'
      if (n < 1048576) return (n / 1024).toFixed(1) + ' KB'
      return (n / 1048576).toFixed(1) + ' MB'
    },
    iconFor(name) {
      const ext = String(name || '').split('.').pop().toUpperCase()
      return '/assets/icons/' + ({ TXT: 'text', PDF: 'pdf', AMR: 'package', PNG: 'image', JPG: 'image', ZIP: 'archive' }[ext] || 'file') + '.png'
    }
  }
}
</script>

<style lang="less">
@import '../../styles/material.less';
</style>
