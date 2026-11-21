<template>
  <div class="page">
    <text class="title">HyperFile</text>
    <div class="layout">
      <div class="left-panel">
        <text class="sub">{{ running ? '手机连同一 Wi-Fi，扫码上传' : 'HTTP 服务未启动' }}</text>
        <div class="card">
          <FlQrcode v-if="running && qrUrl" :text="qrUrl" :size="142" />
          <text class="addr" v-if="running">{{ qrUrl }}</text>
          <text class="addr" v-else>目标目录 {{ uploadDir }}</text>
        </div>
        <div class="btn" @click="Toggle">
          <text class="btn-text">{{ running ? '停止服务' : '启动服务' }}</text>
        </div>
        <text class="received">已接收 {{ received }} 个文件</text>
      </div>
      <div class="right-panel">
        <div class="row">
          <text class="row-title">最近文件</text>
          <div class="mini-btn" @click="RefreshFiles">
            <text class="mini-text">刷新</text>
          </div>
        </div>
        <scroll class="filelist" scroll-y="true">
          <div class="file" v-for="f in files" :key="f">
            <text class="file-name">{{ f }}</text>
          </div>
          <div class="bottom-space"></div>
        </scroll>
      </div>
    </div>
    <div class="toast" v-if="toast">
      <text class="toast-text">{{ toast }}</text>
    </div>
    <div class="setup-page" v-if="showSetupConfirm">
      <text class="setup-title">首次配置 HTTP 服务</text>
      <text class="setup-desc">需要先将独立服务程序复制到设备并设置执行权限。此步骤不会启动服务。</text>
      <text class="setup-path">/userdisk/HyperFile/hyperfile-httpd</text>
      <div class="setup-actions">
        <div class="setup-cancel" @click="CancelSetup"><text class="setup-cancel-text">暂不配置</text></div>
        <div class="setup-confirm" @click="ConfirmSetup"><text class="setup-confirm-text">开始初始化</text></div>
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
  name: 'index',
  components: { FlQrcode },
  data() {
    return {
      running: false,
      showSetupConfirm: false,
      initialized: false,
      ip: '',
      qrUrl: '',
      received: 0,
      files: [],
      toast: '',
      uploadDir: UPLOAD_DIR
    }
  },
  methods: {
    showToast(msg) {
      this.toast = msg
      this.$page.setTimeout(() => { this.toast = '' }, 3000)
    },
    async onShow() {
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
        this.qrUrl = st.running && st.ip ? `http://${st.ip}:${st.port}/` : ''
      } catch (e) {
        this.showToast('状态获取失败')
      }
    },
    async waitForServer(attempts = 40) {
      const http = await getHttp()
      for (let i = 0; i < attempts; i++) {
        await new Promise((resolve) => this.$page.setTimeout(resolve, 200))
        const st = await http.status()
        if (st.running) return st
      }
      throw new Error('HTTP 服务启动超时，请检查 /userdisk/HyperFile/httpd.log')
    },
    async RefreshFiles() {
      try {
        await fs.mkdir(UPLOAD_DIR)
        const entries = await fs.readdir(UPLOAD_DIR)
        this.files = (entries || []).map((e) => (typeof e === 'string' ? e : e.name)).reverse()
      } catch (e) {
        this.files = []
      }
    },
    CancelSetup() {
      this.showSetupConfirm = false
    },
    async ConfirmSetup() {
      this.showSetupConfirm = false
      try {
        const http = await getHttp()
        await http.initialize()
        this.initialized = true
        this.showToast('初始化完成，再次点击启动即可运行')
      } catch (e) {
        this.showToast('初始化失败：' + (e && e.message ? e.message : e))
      }
    },
    async Toggle() {
      if (!this.running) {
        if (this.initialized) this.StartHttpd()
        else this.showSetupConfirm = true
        return
      }
      try {
        const http = await getHttp()
        const st = await http.stop()
        this.running = !!st.running
        this.qrUrl = ''
        this.showToast('已停止')
      } catch (e) {
        this.showToast('操作失败：' + (e && e.message ? e.message : e))
      }
    },
    async StartHttpd() {

      try {
        const http = await getHttp()
        let st = await http.start(PORT, UPLOAD_DIR)
        if (!st.running && st.starting) {
          this.showToast('正在启动 HTTP 服务…')
          st = await this.waitForServer()
        }
        this.running = !!st.running
        this.ip = st.ip || ''
        this.received = st.received || 0
        this.qrUrl = st.ip ? `http://${st.ip}:${st.port}/` : ''
        this.showToast(this.running ? 'HTTP 服务已启动' : '启动失败')
      } catch (e) {
        this.showToast('启动失败：' + (e && e.message ? e.message : e))
      }
    }
  }
}
</script>

<style lang="less">
@import '../../styles/hyper.less';

.page {
  width: 100%;
  height: 100%;
  padding: 14px 18px;
  flex-direction: column;
}
.title {
  font-size: 23px;
  font-weight: 600;
  color: #ffffff;
}
.layout {
  width: 100%;
  flex: 1;
  flex-direction: row;
  margin-top: 8px;
}
.left-panel {
  width: 42%;
  padding-right: 14px;
  align-items: center;
  justify-content: center;
  border-right-width: 1px;
  border-right-color: #333333;
}
.right-panel {
  width: 58%;
  padding-left: 14px;
  flex-direction: column;
}
.sub { font-size: 14px; color: #888888; margin-bottom: 8px; }
.card {
  width: 94%;
  background: #242424;
  border-radius: 12px;
  padding: 12px;
  align-items: center;
}
.addr { margin-top: 8px; font-size: 13px; color: #cccccc; text-align: center; }
.btn {
  margin-top: 14px;
  padding: 10px 32px;
  border-radius: 20px;
  background-color: #ff7da8;
}
.btn-text { font-size: 17px; font-weight: 600; color: #ffffff; }
.received { margin-top: 12px; font-size: 13px; color: #888888; }
.row {
  width: 100%;
  margin-bottom: 8px;
  flex-direction: row;
  align-items: center;
  justify-content: space-between;
}
.row-title { font-size: 15px; color: #cccccc; }
.mini-btn { padding: 6px 14px; border-radius: 14px; background-color: #2c2c2c; }
.mini-text { font-size: 13px; color: #cccccc; }
.filelist { width: 100%; flex: 1; flex-direction: column; }
.file { padding: 9px 10px; border-bottom-width: 1px; border-bottom-color: #2c2c2c; }
.file-name { font-size: 14px; color: #ffffff; text-overflow: ellipsis; max-height: 34px; }
.bottom-space { height: 12px; }
.toast { position: absolute; left: 20%; right: 20%; bottom: 12px; padding: 9px; background-color: #333333; border-radius: 8px; align-items: center; }
.toast-text { color: #ffffff; font-size: 13px; }
.setup-page {
  position: absolute; left: 0; right: 0; top: 0; bottom: 0; z-index: 20;
  background-color: #1c1c1c; padding: 20px 26px;
  align-items: center; justify-content: center;
}
.setup-title { font-size: 22px; font-weight: 600; color: #ffffff; margin-bottom: 12px; }
.setup-desc { width: 100%; font-size: 15px; line-height: 23px; color: #cccccc; margin-bottom: 10px; }
.setup-path { width: 100%; font-size: 13px; color: #ff7da8; margin-bottom: 8px; }
.setup-actions { width: 100%; flex-direction: row; justify-content: flex-end; margin-top: 12px; }
.setup-cancel { padding: 10px 16px; border-radius: 18px; background-color: #333333; margin-right: 10px; }
.setup-cancel-text { font-size: 15px; color: #cccccc; }
.setup-confirm { padding: 10px 18px; border-radius: 18px; background-color: #ff7da8; }
.setup-confirm-text { font-size: 15px; font-weight: 600; color: #ffffff; }
</style>
