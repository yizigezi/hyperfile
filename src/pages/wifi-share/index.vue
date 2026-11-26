<template>
  <div class="app-page">
    <div class="app-topbar">
      <div class="icon-btn" @click="goBack"><image class="material-icon" src="/assets/icons/arrow_back.png" /></div>
      <text class="app-title">Wi-Fi 共享</text>
      <div class="top-spacer"></div>
      <div class="icon-btn" @click="refreshShare"><image class="material-icon" src="/assets/icons/refresh.png" /></div>
    </div>
    <div class="page-body share-page">
      <scroller class="share-shelf" :style="{flexDirection: 'row'}" scroll-direction="horizontal" show-scrollbar="false">
        <div class="share-feature-card">
          <scroller class="card-scroll" scroll-y="true" show-scrollbar="false">
            <text class="section-heading">Wi-Fi 文件共享</text>
            <text class="section-subtitle">{{ running ? '手机或电脑连接同一 Wi-Fi 后扫码访问' : '启动服务后，通过浏览器传输和管理文件' }}</text>
            <div class="share-qr-panel">
              <FlQrcode v-if="running && qrUrl" :text="qrUrl" :size="104" />
              <text class="share-address" v-if="running">{{ qrUrl }}</text>
              <text class="share-muted" v-if="running && updateToken">更新验证码：{{ updateToken }}</text>
              <text class="share-muted" v-if="!running">服务尚未启动</text>
            </div>
            <div :class="running ? 'primary-action action-stop' : 'primary-action'" @click="Toggle">
              <text :class="running ? 'primary-action-label-stop' : 'primary-action-label'">{{ running ? '停止共享服务' : (initialized ? '启动共享服务' : '初始化并启动') }}</text>
            </div>
            <text class="share-footnote">网页端支持上传、下载、复制、删除和 AMR 安装</text>
          </scroller>
        </div>
        <div class="share-directory-card">
          <scroller class="card-scroll" scroll-y="true" show-scrollbar="false">
            <text class="section-heading">共享目录</text>
            <text class="share-directory-path">/userdisk/Favorite/HyperFile</text>
            <div class="share-file-summary">
              <text class="share-file-count">{{ files.length }}</text>
              <text class="share-file-count-label">个文件</text>
            </div>
            <div v-if="files.length === 0" class="shared-empty"><text class="empty-text">目录暂无文件</text></div>
            <div class="shared-file-row" v-for="f in files" :key="f">
              <image class="shared-file-icon" :src="iconForName(f)" />
              <text class="shared-file-name">{{ f }}</text>
            </div>
            <text class="side-footnote">完整文件操作请在浏览器文件管理器中进行</text>
          </scroller>
        </div>
        <div class="share-info-card">
          <scroller class="card-scroll" scroll-y="true" show-scrollbar="false">
            <text class="section-heading">连接信息</text>
            <text class="share-info-label">服务状态</text>
            <text class="share-info-value">{{ running ? '运行中' : '已停止' }}</text>
            <text class="share-info-label">监听端口</text>
            <text class="share-info-value">8080</text>
            <text class="share-info-label">累计接收</text>
            <text class="share-info-value">{{ formatSize(received) }}</text>
          </scroller>
        </div>
      </scroller>
    </div>
    <div class="toast" v-if="toast"><text class="toast-text">{{ toast }}</text></div>
    <div class="dialog-mask" v-if="showSetupConfirm">
      <div class="dialog-card">
        <text class="dialog-title">首次配置共享服务</text>
        <text class="dialog-description">需要先将独立 HTTP 服务程序复制到设备并设置执行权限。此步骤不会立即启动服务。</text>
        <text class="dialog-path">/userdisk/HyperFile/hyperfile-httpd</text>
        <div class="dialog-actions">
          <div class="dialog-button" @click="showSetupConfirm = false"><text class="dialog-button-label">暂不配置</text></div>
          <div class="dialog-button dialog-button-primary" @click="ConfirmSetup"><text class="dialog-button-label dialog-button-primary-label">开始初始化</text></div>
        </div>
      </div>
    </div>
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
    return { running: false, initialized: false, ip: '', qrUrl: '', updateToken: '', received: 0, files: [], toast: '', showSetupConfirm: false }
  },
  onShow() { this.refreshShare() },
  methods: {
    goBack() { this.$page.finish() },
    showToast(message) {
      this.toast = message
      this.$page.setTimeout(() => { this.toast = '' }, 3000)
    },
    formatSize(size) {
      const value = Number(size) || 0
      if (value < 1024) return value + ' B'
      if (value < 1024 * 1024) return (value / 1024).toFixed(1) + ' KB'
      return (value / (1024 * 1024)).toFixed(1) + ' MB'
    },
    iconForName(name) {
      const ext = String(name || '').split('.').pop().toUpperCase()
      return '/assets/icons/' + ({ TXT: 'text', PDF: 'pdf', AMR: 'package', PNG: 'image', JPG: 'image', ZIP: 'archive' }[ext] || 'file') + '.png'
    },
    async refreshShare() {
      await this.RefreshStatus()
      await this.RefreshFiles()
    },
    async RefreshStatus() {
      try {
        const http = await getHttp()
        const st = await http.status()
        this.running = !!st.running
        this.initialized = !!st.initialized || this.running
        this.ip = st.ip || ''
        this.received = st.received || 0
        this.qrUrl = st.running && st.ip ? 'http://' + st.ip + ':' + st.port + '/' : ''
        this.updateToken = st.running ? await http.updateToken() : ''
      } catch (e) {
        this.showToast('共享服务状态获取失败')
      }
    },
    async RefreshFiles() {
      try {
        await fs.mkdir(UPLOAD_DIR)
        const list = await fs.readdir(UPLOAD_DIR)
        this.files = (list || []).map((entry) => typeof entry === 'string' ? entry : entry.name).reverse()
      } catch (e) { this.files = [] }
    },
    async waitForServer(attempts = 40) {
      const http = await getHttp()
      for (let i = 0; i < attempts; i++) {
        await new Promise((resolve) => this.$page.setTimeout(resolve, 200))
        const status = await http.status()
        if (status.running) return status
        if (status.initError) throw new Error(status.initError)
      }
      throw new Error('HTTP 服务启动超时，请检查 /userdisk/HyperFile/httpd.log')
    },
    async ConfirmSetup() {
      this.showSetupConfirm = false
      try {
        const http = await getHttp()
        const result = await http.initialize()
        if (result && result.initializing) {
          this.showToast('正在后台初始化 HTTP 服务…')
          let completed = false
          for (let i = 0; i < 120; i++) {
            await new Promise((resolve) => this.$page.setTimeout(resolve, 250))
            const state = await http.status()
            if (state.initialized) { completed = true; break }
            if (state.initError) throw new Error(state.initError)
          }
          if (!completed) throw new Error('初始化超时，请检查 /userdisk/HyperFile/init.status 和 init.log')
        }
        this.initialized = true
        this.showToast('初始化完成，请再次启动共享服务')
      } catch (e) {
        this.showToast('初始化失败：' + (e && e.message ? e.message : e))
      }
    },
    async Toggle() {
      if (!this.running) {
        if (this.initialized) await this.StartHttpd()
        else this.showSetupConfirm = true
        return
      }
      try {
        const http = await getHttp()
        const status = await http.stop()
        this.running = !!status.running
        this.qrUrl = ''
        this.updateToken = ''
        this.showToast('共享服务已停止')
      } catch (e) {
        this.showToast('停止服务失败：' + (e && e.message ? e.message : e))
      }
    },
    async StartHttpd() {
      try {
        const http = await getHttp()
        let status = await http.start(PORT, UPLOAD_DIR)
        // start() 可能先返回 starting，或返回时状态尚未同步；统一轮询 status，
        // 避免服务已起来但旧返回值导致 UI 报“启动失败”。
        if (!status.running) {
          this.showToast('正在启动共享服务…')
          status = await this.waitForServer()
        }
        this.running = !!status.running
        this.ip = status.ip || ''
        this.received = status.received || 0
        this.qrUrl = status.ip ? 'http://' + status.ip + ':' + (status.port || PORT) + '/' : ''
        this.updateToken = this.running ? await http.updateToken() : ''
        this.showToast(this.running ? '共享服务已启动' : '启动失败')
      } catch (e) {
        // 最后再读取一次权威状态；如果服务实际已启动，按运行中处理。
        try {
          const http = await getHttp()
          const status = await http.status()
          if (status.running) {
            this.running = true
            this.ip = status.ip || ''
            this.received = status.received || 0
            this.qrUrl = status.ip ? 'http://' + status.ip + ':' + (status.port || PORT) + '/' : ''
            this.updateToken = await http.updateToken()
            this.showToast('共享服务已启动')
            return
          }
        } catch (ignored) {}
        this.running = false
        this.showToast('启动失败：' + (e && e.message ? e.message : e))
      }
    }
  }
}
</script>
<style lang="less">
@import '../../styles/material.less';
</style>
