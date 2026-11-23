<template>
  <div class="app-page">
    <div class="app-topbar">
      <text class="app-title">HyperFile</text>
      <div class="nav-segment">
        <div :class="page === 'local' ? 'nav-item nav-item-active' : 'nav-item'" @click="switchPage('local')">
          <text :class="page === 'local' ? 'nav-label nav-label-active' : 'nav-label'">本地文件</text>
        </div>
        <div :class="page === 'share' ? 'nav-item nav-item-active' : 'nav-item'" @click="switchPage('share')">
          <text :class="page === 'share' ? 'nav-label nav-label-active' : 'nav-label'">文件共享</text>
        </div>
        <div :class="page === 'settings' ? 'nav-item nav-item-active' : 'nav-item'" @click="switchPage('settings')">
          <text :class="page === 'settings' ? 'nav-label nav-label-active' : 'nav-label'">设置</text>
        </div>
      </div>
      <div class="top-spacer"></div>
      <div class="top-action" v-if="page === 'local'" @click="refreshLocal">
        <text class="top-action-label">刷新</text>
      </div>
      <div class="top-action" v-if="page === 'share'" @click="refreshShare">
        <text class="top-action-label">刷新</text>
      </div>
    </div>

    <!-- 本地文件：readex-nt 风格的横向卡片书架 -->
    <div class="page-body local-page" v-if="page === 'local'">
      <div class="local-toolbar">
        <div class="toolbar-action" @click="goParent" v-if="currentPath !== '/'">
          <text class="toolbar-action-text">上一级</text>
        </div>
        <text class="path-label">{{ currentPath }}</text>
        <text class="entry-count">{{ entries.length }} 项</text>
      </div>
      <div v-if="localLoading || localError || entries.length === 0" class="local-status">
        <text v-if="localLoading" class="empty-text">正在读取目录…</text>
        <text v-else-if="localError" class="error-text">{{ localError }}</text>
        <text v-else class="empty-text">此目录为空</text>
      </div>
      <scroller v-else class="local-shelf" :style="{flexDirection: 'row'}" scroll-direction="horizontal" show-scrollbar="false">
        <div class="local-file-card" v-for="entry in entries" :key="entry.path" @click="openLocalEntry(entry)">
          <div :class="entry.isDirectory ? 'local-card-cover local-card-folder' : 'local-card-cover'">
            <text class="local-card-type">{{ entry.isDirectory ? '文件夹' : (entry.extension || '文件') }}</text>
          </div>
          <text class="local-card-name">{{ entry.name }}</text>
          <text class="local-card-meta">{{ entry.isDirectory ? '打开目录' : formatSize(entry.size) }}</text>
          <div class="local-card-install" v-if="!entry.isDirectory && entry.extension === 'AMR'">
            <text class="local-card-install-text">安装 AMR</text>
          </div>
          <div class="local-card-install" v-if="!entry.isDirectory && entry.extension === 'SH'">
            <text class="local-card-install-text">运行脚本</text>
          </div>
        </div>
      </scroller>
      <text class="local-footer-hint">设备文件系统 · 支持浏览目录与安装 AMR</text>
    </div>

    <!-- 文件共享：服务与共享目录均为横向卡片 -->
    <div class="page-body share-page" v-else-if="page === 'share'">
      <scroller class="share-shelf" :style="{flexDirection: 'row'}" scroll-direction="horizontal" show-scrollbar="false">
        <div class="share-feature-card">
          <text class="section-heading">Wi-Fi 文件共享</text>
          <text class="section-subtitle">{{ running ? '手机或电脑连接同一 Wi-Fi 后扫码访问' : '启动服务后，通过浏览器传输和管理文件' }}</text>
          <div class="share-qr-panel">
            <FlQrcode v-if="running && qrUrl" :text="qrUrl" :size="88" />
            <text class="share-address" v-if="running">{{ qrUrl }}</text>
            <text class="share-muted" v-if="running && updateToken">更新验证码：{{ updateToken }}</text>
            <text class="share-muted" v-if="!running">服务尚未启动</text>
          </div>
          <div :class="running ? 'primary-action action-stop' : 'primary-action'" @click="Toggle">
            <text :class="running ? 'primary-action-label-stop' : 'primary-action-label'">{{ running ? '停止共享服务' : (initialized ? '启动共享服务' : '初始化并启动') }}</text>
          </div>
          <text class="share-footnote">网页端支持上传、下载、复制、删除和 AMR 安装</text>
        </div>
        <div class="share-directory-card">
          <text class="section-heading">共享目录</text>
          <text class="share-directory-path">/userdisk/Favorite/HyperFile</text>
          <div class="share-file-summary">
            <text class="share-file-count">{{ files.length }}</text>
            <text class="share-file-count-label">个文件</text>
          </div>
          <scroller class="shared-files" scroll-direction="vertical" show-scrollbar="false">
            <div v-if="files.length === 0" class="shared-empty">
              <text class="empty-text">目录暂无文件</text>
            </div>
            <div class="shared-file-row" v-for="f in files" :key="f">
              <text class="shared-file-name">{{ f }}</text>
            </div>
          </scroller>
          <text class="side-footnote">完整文件操作请在浏览器文件管理器中进行</text>
        </div>
        <div class="share-info-card">
          <text class="section-heading">连接信息</text>
          <text class="share-info-label">服务状态</text>
          <text class="share-info-value">{{ running ? '运行中' : '已停止' }}</text>
          <text class="share-info-label">监听端口</text>
          <text class="share-info-value">8080</text>
          <text class="share-info-label">累计接收</text>
          <text class="share-info-value">{{ formatSize(received) }}</text>
        </div>
      </scroller>
    </div>

    <!-- 设置：每组设置独立成卡片，横向滑动 -->
    <div class="page-body settings-body" v-else>
      <scroller class="settings-shelf" :style="{flexDirection: 'row'}" scroll-direction="horizontal" show-scrollbar="false">
        <div class="settings-card settings-card-wide">
          <text class="section-heading">设备与服务</text>
          <div class="setting-line">
            <div class="setting-copy">
              <text class="setting-name">HTTP 文件共享服务</text>
              <text class="setting-description">{{ running ? '独立服务正在运行' : '当前已停止' }}</text>
            </div>
            <text class="setting-value">{{ running ? '运行中' : '已停止' }}</text>
          </div>
          <div class="setting-divider"></div>
          <div class="setting-line">
            <div class="setting-copy">
              <text class="setting-name">服务目录</text>
              <text class="setting-description">独立 HTTP 服务程序与运行日志</text>
            </div>
          </div>
          <text class="setting-path-value">/userdisk/HyperFile</text>
          <div class="setting-divider"></div>
          <div class="setting-line">
            <div class="setting-copy">
              <text class="setting-name">共享目录</text>
              <text class="setting-description">浏览器文件管理器的根目录</text>
            </div>
          </div>
          <text class="setting-path-value">/userdisk/Favorite/HyperFile</text>
        </div>
        <div class="settings-card settings-card-actions">
          <text class="section-heading">设备维护</text>
          <div class="setting-action-card" @click="EnableADB">
            <text class="setting-name">打开 ADB</text>
            <text class="setting-description">启用设备的 ADB 调试标记</text>
            <text class="setting-chevron">执行</text>
          </div>
          <div class="setting-action-card" @click="RunDiagnostics">
            <text class="setting-name">HTTP 服务诊断</text>
            <text class="setting-description">检查程序文件、启动状态与运行日志</text>
            <text class="setting-chevron">查看</text>
          </div>
        </div>
        <div class="settings-card settings-card-about">
          <text class="section-heading">关于 HyperFile</text>
          <text class="settings-about-title">HyperFile</text>
          <text class="setting-description">词典笔本地文件与网络共享管理</text>
          <text class="settings-version">版本 0.1.0</text>
          <text class="settings-note">当前支持目录浏览、AMR 安装与 Wi-Fi 文件共享。</text>
        </div>
      </scroller>
    </div>

    <div class="toast" v-if="toast">
      <text class="toast-text">{{ toast }}</text>
    </div>

    <div class="dialog-mask" v-if="showInstallConfirm">
      <div class="dialog-card">
        <text class="dialog-title">安装应用</text>
        <text class="dialog-description">确认安装以下 AMR 应用包？</text>
        <text class="dialog-path">{{ pendingInstallPath }}</text>
        <div class="dialog-actions">
          <div class="dialog-button" @click="showInstallConfirm = false">
            <text class="dialog-button-label">取消</text>
          </div>
          <div class="dialog-button dialog-button-primary" @click="InstallLocalAmr">
            <text class="dialog-button-label dialog-button-primary-label">安装</text>
          </div>
        </div>
      </div>
    </div>

    <div class="dialog-mask" v-if="showScriptConfirm">
      <div class="dialog-card">
        <text class="dialog-title">运行 Shell 脚本</text>
        <text class="dialog-description">安全提示：脚本会以当前应用权限执行系统命令，可能修改或删除文件、改变系统设置。只运行你自己编写或确认可信的脚本。</text>
        <text class="dialog-path">{{ pendingScriptPath }}</text>
        <text class="dialog-description">若文件没有执行权限，HyperFile 会先尝试添加执行权限（chmod +x），然后运行。</text>
        <div class="dialog-actions">
          <div class="dialog-button" @click="showScriptConfirm = false">
            <text class="dialog-button-label">取消</text>
          </div>
          <div class="dialog-button dialog-button-primary" @click="RunShellScript">
            <text class="dialog-button-label dialog-button-primary-label">确认运行</text>
          </div>
        </div>
      </div>
    </div>

    <div class="dialog-mask" v-if="showScriptResult">
      <div class="dialog-card diagnostics-card">
        <text class="dialog-title">{{ scriptRunning ? '脚本运行中' : '脚本运行结果' }}</text>
        <text class="diagnostic-label">{{ scriptResultTitle }}</text>
        <scroll class="diagnostics-scroll" scroll-y="true">
          <text class="script-output">{{ scriptResultText }}</text>
        </scroll>
        <div class="dialog-actions">
          <div class="dialog-button dialog-button-primary" @click="showScriptResult = false">
            <text class="dialog-button-label dialog-button-primary-label">关闭</text>
          </div>
        </div>
      </div>
    </div>

    <div class="dialog-mask" v-if="showSetupConfirm">
      <div class="dialog-card">
        <text class="dialog-title">首次配置共享服务</text>
        <text class="dialog-description">需要先将独立 HTTP 服务程序复制到设备并设置执行权限。此步骤不会立即启动服务。</text>
        <text class="dialog-path">/userdisk/HyperFile/hyperfile-httpd</text>
        <div class="dialog-actions">
          <div class="dialog-button" @click="showSetupConfirm = false">
            <text class="dialog-button-label">暂不配置</text>
          </div>
          <div class="dialog-button dialog-button-primary" @click="ConfirmSetup">
            <text class="dialog-button-label dialog-button-primary-label">开始初始化</text>
          </div>
        </div>
      </div>
    </div>

    <div class="dialog-mask" v-if="showDiagnostics">
      <div class="dialog-card diagnostics-card">
        <text class="dialog-title">HTTP 服务诊断</text>
        <scroll class="diagnostics-scroll" scroll-y="true">
          <div class="diagnostic-row" v-for="line in diagnosticLines" :key="line.label">
            <text class="diagnostic-label">{{ line.label }}</text>
            <text class="diagnostic-value">{{ line.value }}</text>
          </div>
        </scroll>
        <div class="dialog-actions">
          <div class="dialog-button dialog-button-primary" @click="showDiagnostics = false">
            <text class="dialog-button-label dialog-button-primary-label">关闭</text>
          </div>
        </div>
      </div>
    </div>
  </div>
</template>

<script>
import FlQrcode from 'falcon-ui/src/packages/qr-code/index.vue'
import { fs, execShell } from '../../utils/FileSystem/FileSystem.js'

const UPLOAD_DIR = '/userdisk/Favorite/HyperFile'
const PORT = 8080

async function getHttp() {
  return import('hyperfile_http').then((m) => m.default || m)
}

function isDirectory(item) {
  if (typeof item.isDirectory === 'function') return item.isDirectory()
  if (typeof item.isDirectory === 'boolean') return item.isDirectory
  return false
}

function normalizePath(path) {
  const value = String(path || '/').replace(/\\/g, '/')
  const parts = value.split('/')
  const normalized = []
  for (let i = 0; i < parts.length; i++) {
    if (!parts[i] || parts[i] === '.') continue
    if (parts[i] === '..') normalized.pop()
    else normalized.push(parts[i])
  }
  return '/' + normalized.join('/')
}

function joinPath(parent, name) {
  return (parent === '/' ? '' : parent) + '/' + name
}

function shellQuote(value) {
  return "'" + String(value).replace(/'/g, "'\\''") + "'"
}

export default {
  name: 'index',
  components: { FlQrcode },
  data() {
    return {
      page: 'local',
      currentPath: '/',
      entries: [],
      localLoading: false,
      localError: '',
      running: false,
      initialized: false,
      ip: '',
      qrUrl: '',
      updateToken: '',
      received: 0,
      files: [],
      toast: '',
      showSetupConfirm: false,
      showDiagnostics: false,
      diagnosticLines: [],
      showInstallConfirm: false,
      pendingInstallPath: '',
      showScriptConfirm: false,
      pendingScriptPath: '',
      showScriptResult: false,
      scriptResultTitle: '',
      scriptResultText: '',
      scriptRunning: false
    }
  },
  onShow() {
    this.refreshLocal()
    this.refreshShare()
  },
  methods: {
    switchPage(next) {
      this.page = next
      if (next === 'local') this.refreshLocal()
      if (next === 'share') this.refreshShare()
    },
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
    async refreshLocal() {
      this.localLoading = true
      this.localError = ''
      try {
        const raw = await fs.readdir(this.currentPath, { withFileTypes: true })
        const result = []
        for (let i = 0; i < (raw || []).length; i++) {
          const item = raw[i]
          const name = item && item.name ? item.name : String(item || '')
          if (!name || name === '.' || name === '..') continue
          const path = item && item.path ? item.path : joinPath(this.currentPath, name)
          let directory = isDirectory(item)
          let size = 0
          try {
            const stat = await fs.stat(path)
            if (stat) {
              if (typeof stat.isDirectory === 'function') directory = stat.isDirectory()
              else if (typeof stat.isDirectory === 'boolean') directory = stat.isDirectory
              size = Number(stat.size) || 0
            }
          } catch (e) {}
          const dot = name.lastIndexOf('.')
          const extension = dot > 0 ? name.slice(dot + 1).toUpperCase() : ''
          result.push({ name, path, isDirectory: directory, size, extension })
        }
        result.sort((a, b) => {
          if (a.isDirectory !== b.isDirectory) return a.isDirectory ? -1 : 1
          return a.name.localeCompare(b.name)
        })
        this.entries = result
      } catch (e) {
        this.entries = []
        this.localError = e && e.message ? e.message : String(e)
      } finally {
        this.localLoading = false
      }
    },
    goParent() {
      if (this.currentPath === '/') return
      let path = this.currentPath
      while (path.length > 1 && path.charAt(path.length - 1) === '/') path = path.slice(0, -1)
      const index = path.lastIndexOf('/')
      this.currentPath = index <= 0 ? '/' : path.slice(0, index)
      this.refreshLocal()
    },
    openLocalEntry(entry) {
      if (entry.isDirectory) {
        this.currentPath = normalizePath(entry.path)
        this.refreshLocal()
        return
      }
      if (entry.extension === 'AMR') {
        this.pendingInstallPath = entry.path
        this.showInstallConfirm = true
        return
      }
      if (entry.extension === 'SH') {
        this.pendingScriptPath = entry.path
        this.showScriptConfirm = true
        return
      }
      this.showToast('暂不支持打开此格式文件')
    },
    async InstallLocalAmr() {
      const path = this.pendingInstallPath
      this.showInstallConfirm = false
      if (!path) return
      try {
        await execShell('miniapp_cli install ' + shellQuote(path) + ' >/tmp/hyperfile-local-install.log 2>&1 &')
        this.showToast('已提交安装任务，日志：/tmp/hyperfile-local-install.log')
      } catch (e) {
        this.showToast('启动安装失败：' + (e && e.message ? e.message : e))
      }
    },
    async RunShellScript() {
      const path = this.pendingScriptPath
      this.showScriptConfirm = false
      if (!path) return
      const token = String(Date.now())
      const logPath = '/tmp/hyperfile-script-' + token + '.log'
      const statusPath = '/tmp/hyperfile-script-' + token + '.status'
      this.scriptResultTitle = path
      this.scriptResultText = '正在启动…'
      this.scriptRunning = true
      this.showScriptResult = true
      try {
        const command = "( if [ ! -x " + shellQuote(path) + " ]; then chmod +x " + shellQuote(path) +
          "; chmod_rc=$?; if [ \"$chmod_rc\" -ne 0 ]; then echo 'chmod +x 失败' ; printf '126' > " +
          shellQuote(statusPath) + "; exit 0; fi; fi; " + shellQuote(path) +
          "; rc=$?; printf '%s' \"$rc\" > " + shellQuote(statusPath) +
          "; ) > " + shellQuote(logPath) + " 2>&1 < /dev/null &"
        await execShell(command)
        this.scriptResultText = '正在运行…'
        for (let i = 0; i < 600; i++) {
          await new Promise((resolve) => this.$page.setTimeout(resolve, 500))
          const status = await execShell('if [ -f ' + shellQuote(statusPath) +
            ' ]; then cat ' + shellQuote(statusPath) + '; else printf RUNNING; fi')
          const output = await execShell('cat ' + shellQuote(logPath) + ' 2>/dev/null || true')
          if (status.trim() !== 'RUNNING') {
            this.scriptRunning = false
            this.scriptResultText = (output || '(无输出)') + '\n\n退出码：' + status.trim()
            return
          }
          this.scriptResultText = (output || '脚本正在运行…') + '\n\n运行中…'
        }
        this.scriptRunning = false
        this.scriptResultText += '\n\n等待超时；脚本可能仍在后台运行。'
      } catch (e) {
        this.scriptRunning = false
        this.scriptResultText = '启动或读取结果失败：' + (e && e.message ? e.message : e)
      }
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
      } catch (e) {
        this.files = []
      }
    },
    async waitForServer(attempts = 40) {
      const http = await getHttp()
      for (let i = 0; i < attempts; i++) {
        await new Promise((resolve) => this.$page.setTimeout(resolve, 200))
        const status = await http.status()
        if (status.running) return status
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
            if (state.initialized) {
              completed = true
              break
            }
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
        if (this.initialized) {
          this.StartHttpd()
        } else {
          this.showSetupConfirm = true
        }
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
        if (!status.running && status.starting) {
          this.showToast('正在启动共享服务…')
          status = await this.waitForServer()
        }
        this.running = !!status.running
        this.ip = status.ip || ''
        this.received = status.received || 0
        this.qrUrl = status.ip ? 'http://' + status.ip + ':' + status.port + '/' : ''
        this.updateToken = this.running ? await http.updateToken() : ''
        this.showToast(this.running ? '共享服务已启动' : '启动失败')
      } catch (e) {
        this.showToast('启动失败：' + (e && e.message ? e.message : e))
        await this.RunDiagnostics()
      }
    },
    async EnableADB() {
      try {
        const http = await getHttp()
        await http.enableAdb()
        this.showToast('ADB 已开启')
      } catch (e) {
        this.showToast('开启 ADB 失败：' + (e && e.message ? e.message : e))
      }
    },
    async RunDiagnostics() {
      try {
        const http = await getHttp()
        const d = await http.diagnose()
        const yesNo = (value) => value === true ? '是' : (value === false ? '否' : '未知')
        this.diagnosticLines = [
          { label: '执行文件', value: d.path || '' },
          { label: '文件存在', value: yesNo(d.exists) },
          { label: '普通文件', value: yesNo(d.regularFile) },
          { label: '可执行', value: yesNo(d.executable) },
          { label: '权限', value: d.mode ? '0' + Number(d.mode).toString(8) : '未知' },
          { label: '文件大小', value: String(d.size || 0) + ' bytes' },
          { label: 'ELF 32 位', value: yesNo(d.elf32) },
          { label: 'ARM 架构', value: yesNo(d.arm) + ' (e_machine=' + String(d.elfMachine || 0) + ')' },
          { label: '检查错误', value: String(d.errno || 0) + ' / ' + (d.error || '') },
          { label: '挂载点', value: d.mountPoint || '未知' },
          { label: '挂载参数', value: d.mountOptions || '未知' },
          { label: 'noexec', value: d.noexec === 1 ? '是' : (d.noexec === 0 ? '否' : '无法读取') },
          { label: 'httpd.log', value: d.log || '空' }
        ]
        this.showDiagnostics = true
      } catch (e) {
        this.showToast('诊断失败：' + (e && e.message ? e.message : e))
      }
    }
  }
}
</script>

<style lang="less">
@import '../../styles/material.less';
</style>
