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
    async Toggle() {
      try {
        const http = await getHttp()
        if (this.running) {
          const st = await http.stop()
          this.running = !!st.running
          this.qrUrl = ''
          this.showToast('已停止')
        } else {
          let st = await http.start(PORT, UPLOAD_DIR)
          if (!st.running && st.starting) {
            this.showToast('正在启动服务…')
            st = await this.waitForServer()
          }
          this.running = !!st.running
          this.ip = st.ip || ''
          this.received = st.received || 0
          this.qrUrl = st.ip ? `http://${st.ip}:${st.port}/` : ''
          this.showToast(this.running ? '已启动' : '启动失败')
        }
      } catch (e) {
        this.showToast('操作失败：' + (e && e.message ? e.message : e))
      }
    }
  }
}
</script>

<style lang="less">
@import '../../styles/hyper.less';

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
