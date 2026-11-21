<template>
  <div class="page">
    <text class="title">HyperFile</text>
    <text class="sub">{{ running ? '手机连同一 Wi-Fi，扫码上传' : '未启动' }}</text>

    <div class="card">
      <FlQrcode v-if="running && qrUrl" :text="qrUrl" :size="150" />
      <text class="addr" v-if="running">{{ qrUrl }}</text>
      <text class="addr" v-else>目标目录 {{ uploadDir }}</text>
    </div>

    <div class="btn" @click="Toggle">
      <text class="btn-text">{{ running ? '停止' : '启动' }}</text>
    </div>

    <div class="row">
      <text class="row-title">已接收 {{ received }}</text>
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

    <div class="toast" v-if="toast">
      <text class="toast-text">{{ toast }}</text>
    </div>

    <div class="setup-page" v-if="showSetupConfirm">
      <text class="setup-title">首次配置 HTTP 服务</text>
      <text class="setup-desc">HyperFile 需要启动独立的 httpd 服务，才能让其他设备通过浏览器访问并上传文件。</text>
      <text class="setup-path">服务程序将复制到：/userdisk/HyperFile/hyperfile-httpd</text>
      <text class="setup-desc">此步骤只负责查找程序、复制文件和设置执行权限，不会启动 HTTP 服务。初始化完成后，你可以在下一步单独启动。</text>
      <div class="setup-actions">
        <div class="setup-cancel" @click="CancelSetup">
          <text class="setup-cancel-text">暂不配置</text>
        </div>
        <div class="setup-confirm" @click="ConfirmSetup">
          <text class="setup-confirm-text">开始初始化</text>
        </div>
      </div>
    </div>

    <div class="setup-page" v-if="showStartConfirm">
      <text class="setup-title">httpd 已完成初始化</text>
      <text class="setup-desc">程序已复制到 /userdisk/HyperFile/hyperfile-httpd，执行权限检查通过。</text>
      <text class="setup-desc">初始化和启动是两个独立步骤。现在可以启动 HTTP 服务，之后也可以随时停止。</text>
      <div class="setup-actions">
        <div class="setup-cancel" @click="CancelStart">
          <text class="setup-cancel-text">稍后启动</text>
        </div>
        <div class="setup-confirm" @click="StartHttpd">
          <text class="setup-confirm-text">启动服务</text>
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
  name: 'index',
  components: { FlQrcode },
  data() {
    return {
      running: false,
      showSetupConfirm: false,
      showStartConfirm: false,\n      initialized: false,
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
      throw new Error('HTTP 服务启动超时，请检查 /userdisk/hyperfile/httpd.log')
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
    CancelStart() {
      this.showStartConfirm = false
    },
    async ConfirmSetup() {
      this.showSetupConfirm = false
      try {
        const http = await getHttp()
        await http.initialize()
        this.initialized = true
        this.showStartConfirm = true
        this.showToast('httpd 初始化完成')
      } catch (e) {
        this.showToast('初始化失败：' + (e && e.message ? e.message : e))
      }
    },
    async Toggle() {
      if (!this.running) {
        if (this.initialized) this.showStartConfirm = true
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
      this.showStartConfirm = false
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

.setup-page {
  position: absolute;
  left: 0;
  right: 0;
  top: 0;
  bottom: 0;
  z-index: 20;
  background-color: #1c1c1c;
  padding: 28px 22px;
  align-items: center;
  justify-content: center;
}

.setup-title {
  font-size: 23px;
  font-weight: 600;
  color: #ffffff;
  margin-bottom: 18px;
}

.setup-desc {
  width: 100%;
  font-size: 16px;
  line-height: 25px;
  color: #cccccc;
  margin-bottom: 14px;
}

.setup-path {
  width: 100%;
  font-size: 14px;
  line-height: 22px;
  color: #ff7da8;
  margin-bottom: 14px;
}

.setup-actions {
  width: 100%;
  flex-direction: row;
  justify-content: space-between;
  margin-top: 22px;
}

.setup-cancel {
  padding: 12px 18px;
  border-radius: 20px;
  background-color: #333333;
}

.setup-cancel-text {
  font-size: 16px;
  color: #cccccc;
}

.setup-confirm {
  padding: 12px 20px;
  border-radius: 20px;
  background-color: #ff7da8;
}

.setup-confirm-text {
  font-size: 16px;
  font-weight: 600;
  color: #ffffff;
}

.sub {
  font-size: 15px;
  color: #888888;
  margin-top: 4px;
}

.card {
  margin-top: 18px;
  width: 92%;
  background: #242424;
  border-radius: 14px;
  padding: 16px;
  align-items: center;
}

.addr {
  margin-top: 10px;
  font-size: 15px;
  color: #cccccc;
}

.btn {
  margin-top: 16px;
  padding: 12px 46px;
  border-radius: 22px;
  background-color: #ff7da8;
}

.btn-text {
  font-size: 18px;
  font-weight: 600;
  color: #ffffff;
}

.row {
  width: 92%;
  margin-top: 18px;
  flex-direction: row;
  align-items: center;
  justify-content: space-between;
}

.row-title {
  font-size: 15px;
  color: #888888;
}

.mini-btn {
  padding: 6px 16px;
  border-radius: 14px;
  background-color: #2c2c2c;
}

.mini-text {
  font-size: 14px;
  color: #cccccc;
}

.filelist {
  width: 92%;
  margin-top: 8px;
  flex-direction: column;
}

.file {
  padding: 10px 12px;
  border-bottom-width: 1px;
  border-bottom-color: #2c2c2c;
}

.file-name {
  font-size: 15px;
  color: #ffffff;
  text-overflow: ellipsis;
  max-height: 36px;
}

.bottom-space {
  height: 20px;
}
</style>
