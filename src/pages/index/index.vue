<template>
  <div class="app-page">
    <div class="app-topbar">
      <text class="app-title">HyperFile</text>
      <div class="nav-segment">
        <div :class="page === 'local' ? 'nav-item nav-item-active' : 'nav-item'" @click="switchPage('local')">
          <text :class="page === 'local' ? 'nav-label nav-label-active' : 'nav-label'">本地文件</text>
        </div>
        <div :class="(page === 'settings' || page === 'about') ? 'nav-item nav-item-active' : 'nav-item'" @click="switchPage('settings')">
          <text :class="(page === 'settings' || page === 'about') ? 'nav-label nav-label-active' : 'nav-label'">设置</text>
        </div>
        <div :class="page === 'apps' ? 'nav-item nav-item-active' : 'nav-item'" @click="switchPage('apps')">
          <text :class="page === 'apps' ? 'nav-label nav-label-active' : 'nav-label'">应用</text>
        </div>
      </div>
      <text class="path-label" v-if="page === 'local'">{{ currentPath }}</text>
      <text class="entry-count" v-if="page === 'local'">{{ entries.length }} 项</text>
      <div class="top-spacer"></div>
      <div class="icon-btn" v-if="page === 'local' && currentPath !== '/'" @click="goParent">
        <image class="material-icon" src="/assets/icons/arrow_upward.png" />
      </div>
      <div class="icon-btn gap-left" v-if="page === 'local'" @click="refreshLocal">
        <image class="material-icon" src="/assets/icons/refresh.png" />
      </div>
      <div class="icon-btn gap-left" v-if="page === 'share'" @click="refreshShare">
        <image class="material-icon" src="/assets/icons/refresh.png" />
      </div>
    </div>

    <!-- 本地文件：readex-nt 横向卡片书架 -->
    <div class="page-body local-page" v-if="page === 'local'">
      <div v-if="localLoading || localError || entries.length === 0" class="local-status">
        <text v-if="localLoading" class="empty-text">正在读取目录…</text>
        <text v-else-if="localError" class="error-text">{{ localError }}</text>
        <text v-else class="empty-text">此目录为空</text>
      </div>
      <scroller v-else class="local-shelf" :style="{flexDirection: 'row'}" scroll-direction="horizontal" show-scrollbar="false">
        <div class="local-file-card" v-for="entry in entries" :key="entry.path" @click="openLocalEntry(entry)">
          <div :class="entry.isDirectory ? 'local-card-folder' : 'local-card-cover'">
            <image class="local-card-icon" :src="iconFor(entry)" />
          </div>
          <text class="local-card-name">{{ entry.name }}</text>
          <text class="local-card-meta">{{ entry.isDirectory ? '打开目录' : formatSize(entry.size) }}</text>
        </div>
      </scroller>
    </div>

    <!-- 文件共享：服务与共享目录均为横向卡片 -->
    <div class="page-body share-page" v-else-if="page === 'share'">
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
            <div v-if="files.length === 0" class="shared-empty">
              <text class="empty-text">目录暂无文件</text>
            </div>
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

    <!-- 设置：每组设置独立成卡片，横向滑动 -->
    <div class="page-body settings-body" v-else-if="page === 'settings'">
      <scroller class="settings-shelf" :style="{flexDirection: 'row'}" scroll-direction="horizontal" show-scrollbar="false">
        <div class="settings-card settings-card-wide">
          <scroller class="card-scroll" scroll-y="true" show-scrollbar="false">
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
          </scroller>
        </div>
        <div class="settings-card settings-card-actions">
          <scroller class="card-scroll" scroll-y="true" show-scrollbar="false">
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
          </scroller>
        </div>
        <div class="settings-card settings-card-about">
          <div class="about-entry" @click="switchPage('about')">
            <image class="about-entry-icon" src="/assets/icons/info.png" />
            <div class="about-entry-text">
              <text class="about-entry-title">关于 HyperFile</text>
              <text class="about-entry-version">版本 {{ appVersion }}</text>
            </div>
            <text class="setting-chevron">查看</text>
          </div>
        </div>
      </scroller>
    </div>

    <div class="page-body apps-body" v-else-if="page === 'apps'">
      <scroller class="apps-shelf" :style="{flexDirection: 'row'}" scroll-direction="horizontal" show-scrollbar="false">
        <div class="app-card" v-for="app in apps" :key="app.name" @click="openApp(app)">
          <div class="app-card-icon-box">
            <image class="app-card-icon" :src="app.icon" />
          </div>
          <text class="app-card-name">{{ app.name }}</text>
        </div>
      </scroller>
    </div>

    <div class="page-body about-body" v-else-if="page === 'about'">
      <div class="about-card">
        <div class="about-left">
          <div class="about-wordmark" :style="{flexDirection: 'row'}">
            <text class="about-word-hyper">Hyper</text>
            <text class="about-word-file">File</text>
          </div>
          <text class="about-version">版本 {{ appVersion }}</text>
        </div>
        <div class="about-divider"></div>
        <div class="about-right">
          <div class="about-info-row" :style="{flexDirection: 'row'}">
            <text class="about-info-label">构建</text>
            <text class="about-info-value">{{ buildBranch }} · {{ buildCommit }} · {{ buildDate }}</text>
          </div>
          <div class="about-info-row" :style="{flexDirection: 'row'}">
            <text class="about-info-label">仓库</text>
            <text class="about-info-value">{{ repoUrl }}</text>
          </div>
          <div class="about-info-row" :style="{flexDirection: 'row'}">
            <text class="about-info-label">许可</text>
            <text class="about-info-value">{{ license }}</text>
          </div>
        </div>
      </div>
    </div>

    <div class="toast" v-if="toast">
      <text class="toast-text">{{ toast }}</text>
    </div>

    <div class="dialog-mask pagination-loading-mask" v-if="paginationLoading">
      <div class="dialog-card pagination-loading-card">
        <text class="dialog-title">正在生成分页缓存</text>
        <text class="dialog-description">正在识别章节并保存分页，请稍候…</text>
      </div>
    </div>

    <div class="dialog-mask" v-if="showFileActions">
      <div class="dialog-card file-actions-card">
        <text class="dialog-title file-actions-title">{{ selectedEntry ? selectedEntry.name : '' }}</text>
        <text class="dialog-description file-actions-meta">{{ selectedEntry ? (selectedEntry.isDirectory ? '文件夹' : (selectedEntry.extension || '文件') + ' · ' + formatSize(selectedEntry.size)) : '' }}</text>
        <scroller class="file-actions-scroll" scroll-y="true" show-scrollbar="false">
        <text class="file-actions-section">打开方式</text>
        <div class="file-action-row" v-if="selectedEntry && !selectedEntry.isDirectory && textExtensions[selectedEntry.extension]" @click="selectOpenWith('read')">
          <text class="file-action-name">文本阅读器</text>
          <text class="file-action-description">在独立页面阅读，可滚动并调整字号</text>
        </div>
        <div class="file-action-row" v-if="selectedEntry && !selectedEntry.isDirectory && selectedEntry.extension === 'AMR'" @click="selectOpenWith('amr')">
          <text class="file-action-name">安装 AMR 应用</text>
          <text class="file-action-description">使用 miniapp_cli 安装应用包</text>
        </div>
        <div class="file-action-row" v-if="selectedEntry && !selectedEntry.isDirectory && selectedEntry.extension === 'SH'" @click="selectOpenWith('shell')">
          <text class="file-action-name">运行 Shell 脚本</text>
          <text class="file-action-description">通过原生任务执行器运行脚本</text>
        </div>
        <text class="file-actions-empty" v-if="selectedEntry && !selectedEntry.isDirectory && !textExtensions[selectedEntry.extension] && selectedEntry.extension !== 'AMR' && selectedEntry.extension !== 'SH'">暂无可用打开方式</text>
        <text class="file-actions-section">书籍</text>
        <div class="file-action-row" v-if="selectedEntry && !selectedEntry.isDirectory && selectedEntry.extension === 'TXT'" @click="AddToShelf">
          <text class="file-action-name">加入书架</text>
          <text class="file-action-description">按当前阅读设置预分页并生成目录，秒开续读</text>
        </div>
        <text class="file-actions-section">文件操作</text>
        <div class="file-action-row" v-if="selectedEntry && !selectedEntry.isDirectory" @click="CopyToClipboard">
          <text class="file-action-name">复制</text>
          <text class="file-action-description">复制此文件，随后可在任意目录粘贴</text>
        </div>
        <div class="file-action-row" v-if="clipboardPath" @click="PasteFromClipboard">
          <text class="file-action-name">粘贴到当前目录</text>
          <text class="file-action-description">{{ clipboardName }}</text>
        </div>
        <div class="file-action-row" v-if="selectedEntry && !selectedEntry.isDirectory" @click="OpenRename">
          <text class="file-action-name">重命名</text>
          <text class="file-action-description">修改文件名</text>
        </div>
        <div class="file-action-row" v-if="selectedEntry && !selectedEntry.isDirectory" @click="OpenDelete">
          <text class="file-action-name">删除</text>
          <text class="file-action-description">删除此文件（有确认）</text>
        </div>
        </scroller>
        <div class="dialog-actions file-actions-actions">
          <div class="dialog-button" @click="showFileActions = false">
            <text class="dialog-button-label">关闭</text>
          </div>
        </div>
      </div>
    </div>

    <div class="dialog-mask" v-if="showRename">
      <div class="dialog-card">
        <text class="dialog-title">重命名</text>
        <input class="text-input" v-model="renameValue" placeholder="输入新文件名" />
        <div class="dialog-actions">
          <div class="dialog-button" @click="showRename = false">
            <text class="dialog-button-label">取消</text>
          </div>
          <div class="dialog-button dialog-button-primary" @click="ConfirmRename">
            <text class="dialog-button-primary-label">保存</text>
          </div>
        </div>
      </div>
    </div>

    <div class="dialog-mask" v-if="showDelete">
      <div class="dialog-card">
        <text class="dialog-title">删除文件</text>
        <text class="dialog-description">确认删除《{{ selectedEntry ? selectedEntry.name : '' }}》？删除后无法恢复。</text>
        <div class="dialog-actions">
          <div class="dialog-button" @click="showDelete = false">
            <text class="dialog-button-label">取消</text>
          </div>
          <div class="dialog-button dialog-button-primary" @click="ConfirmDelete">
            <text class="dialog-button-primary-label">删除</text>
          </div>
        </div>
      </div>
    </div>

    <div class="dialog-mask" v-if="showInstallConfirm">
      <div class="dialog-card">
        <text class="dialog-title">{{ isPendingSelfUpdate ? '更新 HyperFile' : '安装应用' }}</text>
        <text v-if="isPendingSelfUpdate" class="dialog-description">
          已识别为当前 HyperFile。确认后将由独立更新器替换应用，完成后自动重新启动。
        </text>
        <text v-if="installInfoLoading" class="dialog-description">正在读取 AMR 应用信息…</text>
        <text v-else-if="pendingInstallInfo" class="dialog-description">应用名称：{{ pendingInstallInfo.appName || '未提供' }}</text>
        <text v-if="pendingInstallInfo" class="dialog-description">App ID：{{ pendingInstallInfo.appid || '未提供' }}</text>
        <text v-if="pendingInstallInfo" class="dialog-description">版本：{{ pendingInstallInfo.version || '未提供' }}</text>
        <text v-if="pendingInstallInfo && pendingInstallInfo.description" class="dialog-description">简介：{{ pendingInstallInfo.description }}</text>
        <text v-if="!installInfoLoading && !pendingInstallInfo" class="dialog-description">
          {{ pendingInstallInfoError || '未读取到应用信息' }}
        </text>
        <text class="dialog-path">{{ pendingInstallPath }}</text>
        <div class="dialog-actions">
          <div class="dialog-button" @click="showInstallConfirm = false">
            <text class="dialog-button-label">取消</text>
          </div>
          <div class="dialog-button dialog-button-primary" @click="InstallLocalAmr">
            <text class="dialog-button-label dialog-button-primary-label">{{ installInfoLoading ? '读取中…' : (isPendingSelfUpdate ? '更新' : '安装') }}</text>
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
import { withTimeout } from '../../utils/async.js'
import { setReaderTarget, getReaderSettings, loadReaderSettings } from '../../utils/ReaderSession.js'
import { addBook } from '../../utils/Bookshelf.js'
import BuildInfo from '../../utils/BuildInfo.js'
const HYPERFILE_APP_ID = '8026111800000002'
const UPLOAD_DIR = '/userdisk/Favorite/HyperFile'
const DATA_DIR = '/userdisk/HyperFile_Data'
const LAST_PATH_FILE = DATA_DIR + '/last-directory.json'
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

// 扩展名 → assets/icons 图标名（readex-nt 做法：随 AMR 打包的 Material PNG）
const EXT_ICONS = {
  PDF: 'pdf',
  TXT: 'text', MD: 'text', LOG: 'text', NOTE: 'text', SRT: 'text', INI: 'text', CFG: 'text', CONF: 'text',
  DOC: 'doc', DOCX: 'doc', RTF: 'doc', ODT: 'doc', WPS: 'doc',
  XLS: 'sheet', XLSX: 'sheet', CSV: 'sheet', ODS: 'sheet',
  PPT: 'slides', PPTX: 'slides', ODP: 'slides',
  JPG: 'image', JPEG: 'image', PNG: 'image', GIF: 'image', WEBP: 'image', BMP: 'image', SVG: 'image', ICO: 'image',
  MP3: 'audio', WAV: 'audio', FLAC: 'audio', AAC: 'audio', M4A: 'audio', OGG: 'audio', WMA: 'audio', MID: 'audio',
  MP4: 'video', MKV: 'video', AVI: 'video', MOV: 'video', FLV: 'video', WMV: 'video', WEBM: 'video', '3GP': 'video', RMVB: 'video',
  ZIP: 'archive', RAR: 'archive', '7Z': 'archive', TAR: 'archive', GZ: 'archive', TGZ: 'archive', BZ2: 'archive', XZ: 'archive',
  APK: 'apk',
  AMR: 'package',
  SH: 'code', JS: 'code', PY: 'code', C: 'code', CPP: 'code', H: 'code', JAVA: 'code', TS: 'code',
  JSON: 'code', XML: 'code', YML: 'code', YAML: 'code', CSS: 'code', LUA: 'code', SQL: 'code',
  HTML: 'web', HTM: 'web', URL: 'web'
}

function iconPathForName(name, directory) {
  if (directory) return '/assets/icons/folder.png'
  const value = String(name || '')
  const dot = value.lastIndexOf('.')
  const ext = dot > 0 ? value.slice(dot + 1).toUpperCase() : ''
  return '/assets/icons/' + (EXT_ICONS[ext] || 'file') + '.png'
}

// 可用内置阅读器打开的文本类扩展名
const TEXT_EXTS = {
  TXT: 1, MD: 1, LOG: 1, NOTE: 1, SRT: 1, INI: 1, CFG: 1, CONF: 1,
  JSON: 1, XML: 1, YML: 1, YAML: 1, CSV: 1,
  SH: 1, JS: 1, PY: 1, C: 1, CPP: 1, H: 1, JAVA: 1, TS: 1, CSS: 1, LUA: 1, SQL: 1,
  HTML: 1, HTM: 1
}

export default {
  name: 'index',
  components: { FlQrcode },
  data() {
    return {
      page: 'local',
      currentPath: '/',
      lastPathRestored: false,
      initialLoadPromise: null,
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
      appVersion: BuildInfo.version || '0.1.0',
      buildBranch: BuildInfo.branch || 'local',
      buildCommit: BuildInfo.commit || 'local',
      buildDate: BuildInfo.builtAt || '—',
      repoUrl: 'github.com/yizigezi/hyperfile',
      license: 'GPL-3.0',
      apps: [
        { name: 'Wi-Fi 共享', icon: '/assets/icons/wifi.png', action: 'share' },
        { name: 'Readex 书架', icon: '/assets/icons/menu_book.png', action: 'shelf' }
      ],
      paginationLoading: false,
      showSetupConfirm: false,
      showDiagnostics: false,
      diagnosticLines: [],
      showFileActions: false,
      selectedEntry: null,
      textExtensions: TEXT_EXTS,
      clipboardPath: '',
      clipboardName: '',
      showRename: false,
      renameValue: '',
      showDelete: false,
      showInstallConfirm: false,
      pendingInstallPath: '',
      pendingInstallInfo: null,
      pendingInstallInfoError: '',
      installInfoLoading: false,
      showScriptConfirm: false,
      pendingScriptPath: '',
      showScriptResult: false,
      scriptResultTitle: '',
      scriptResultText: '',
      scriptRunning: false
    }
  },
  // 首次加载由 Vue created 触发（必火）；页面钩子双保险：
  // methods 同名项供 BasePage._callRootHook 转发，顶层同名项供运行时 $options 直调。
  created() {
    console.log('[HyperFile][index] created: start initial load')
    this.initializePage()
  },
  onShow() { if (this && this.initializePage) this.initializePage() },
  onHide() { if (this && this.lastPathRestored) this.saveLastPath() },
  computed: {
    isPendingSelfUpdate() {
      return !!this.pendingInstallInfo &&
        this.pendingInstallInfo.appid === HYPERFILE_APP_ID
    }
  },
  methods: {
    onShow() {
      this.initializePage()
    },
    onHide() {
      if (this.lastPathRestored) this.saveLastPath()
    },
    initializePage() {
      if (this.initialLoadPromise) return this.initialLoadPromise
      console.log('[HyperFile][index] initializePage: begin')
      const task = withTimeout((async () => {
        if (!this.lastPathRestored) {
          await this.restoreLastPath()
          this.lastPathRestored = true
        }
        console.log('[HyperFile][index] restored path:', this.currentPath)
        await Promise.all([this.refreshLocal(), this.refreshShare()])
        console.log('[HyperFile][index] initializePage: complete')
      })(), 20000, '页面数据加载')
      this.initialLoadPromise = task
      task.then(
        () => { if (this.initialLoadPromise === task) this.initialLoadPromise = null },
        (error) => {
          console.log('[HyperFile] 页面初始加载失败:', error)
          if (this.initialLoadPromise === task) this.initialLoadPromise = null
        }
      )
      return task
    },
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
    async restoreLastPath() {
      try {
        const raw = await fs.readFile(LAST_PATH_FILE)
        const state = JSON.parse(String(raw || ''))
        const path = normalizePath(state && state.path)
        await fs.readdir(path)
        this.currentPath = path
      } catch (e) {
        this.currentPath = '/'
      }
    },
    async saveLastPath() {
      try {
        await fs.mkdir(DATA_DIR)
        await fs.writeFile(LAST_PATH_FILE, JSON.stringify({ path: this.currentPath }))
      } catch (e) {
        console.log('[HyperFile] 保存上次目录失败:', e)
      }
    },
    async refreshLocal() {
      console.log('[HyperFile][index] refreshLocal: begin', this.currentPath)
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
        console.log('[HyperFile][index] refreshLocal: complete, entries=', result.length)
      } catch (e) {
        console.log('[HyperFile][index] refreshLocal: failed', e)
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
      this.saveLastPath()
      this.refreshLocal()
    },
    openLocalEntry(entry) {
      if (entry.isDirectory) {
        this.currentPath = normalizePath(entry.path)
        this.saveLastPath()
        this.refreshLocal()
        return
      }
      this.selectedEntry = entry
      this.showFileActions = true
    },
    openReader(entry) {
      if (!entry || !entry.path) {
        this.showToast('文件路径无效')
        return
      }
      // 双通道传参：ReaderSession 共享存储 + Falcon 页面参数（BasePage 会保存 $page.options）
      setReaderTarget(entry.path, entry.name, null)
      $falcon.navTo('reader', { path: entry.path, name: entry.name })
    },
    OpenShelf() {
      $falcon.navTo('shelf')
    },
    openApp(app) {
      if (app.action === 'share') this.switchPage('share')
      else if (app.action === 'shelf') this.OpenShelf()
    },
    shellQuote(value) {
      return "'" + String(value).replace(/'/g, "'\\''") + "'"
    },
    async AddToShelf() {
      const entry = this.selectedEntry
      this.showFileActions = false
      if (!entry || this.paginationLoading) return
      this.paginationLoading = true
      try {
        await new Promise(resolve => this.$page.setTimeout(resolve, 16))
        await loadReaderSettings()
        const book = await addBook(entry.path, entry.name, getReaderSettings())
        this.showToast('已加入书架并保存原件：共 ' + book.pageCount + ' 页')
      } catch (e) {
        this.showToast('加入书架失败：' + (e && e.message ? e.message : e))
      } finally {
        this.paginationLoading = false
      }
    },
    CopyToClipboard() {
      const entry = this.selectedEntry
      this.showFileActions = false
      if (!entry) return
      this.clipboardPath = entry.path
      this.clipboardName = entry.name
      this.showToast('已复制：' + entry.name)
    },
    async PasteFromClipboard() {
      const source = this.clipboardPath
      this.showFileActions = false
      if (!source) return
      try {
        await execShell('cp -f ' + this.shellQuote(source) + ' ' + this.shellQuote(this.currentPath + '/'))
        this.showToast('已粘贴到当前目录')
        this.refreshLocal()
      } catch (e) {
        this.showToast('粘贴失败：' + (e && e.message ? e.message : e))
      }
    },
    OpenRename() {
      const entry = this.selectedEntry
      this.showFileActions = false
      if (!entry) return
      this.renameValue = entry.name
      this.showRename = true
    },
    async ConfirmRename() {
      const entry = this.selectedEntry
      const name = String(this.renameValue || '').trim()
      this.showRename = false
      if (!entry || !name || name.indexOf('/') >= 0) {
        this.showToast('文件名不合法')
        return
      }
      if (name === entry.name) return
      try {
        const dir = entry.path.slice(0, entry.path.lastIndexOf('/'))
        await execShell('mv -f ' + this.shellQuote(entry.path) + ' ' + this.shellQuote(dir + '/' + name))
        this.showToast('已重命名为 ' + name)
        this.refreshLocal()
      } catch (e) {
        this.showToast('重命名失败：' + (e && e.message ? e.message : e))
      }
    },
    OpenDelete() {
      this.showFileActions = false
      this.showDelete = true
    },
    async ConfirmDelete() {
      const entry = this.selectedEntry
      this.showDelete = false
      if (!entry) return
      try {
        await fs.rm(entry.path)
        this.showToast('已删除 ' + entry.name)
        this.refreshLocal()
      } catch (e) {
        this.showToast('删除失败：' + (e && e.message ? e.message : e))
      }
    },
    selectOpenWith(type) {
      const entry = this.selectedEntry
      if (!entry || !entry.path) return
      this.showFileActions = false
      if (type === 'read') {
        this.openReader(entry)
      } else if (type === 'amr') {
        this.pendingInstallPath = entry.path
        this.pendingInstallInfo = null
        this.pendingInstallInfoError = ''
        this.installInfoLoading = true
        this.showInstallConfirm = true
        this.loadPendingAmrInfo(entry.path)
      } else if (type === 'shell') {
        this.pendingScriptPath = entry.path
        this.showScriptConfirm = true
      }
    },
    async loadPendingAmrInfo(path) {
      try {
        const native = await import('hyperfile_local_ops').then((m) => m.default || m)
        const info = await native.amrInfo(path)
        if (this.pendingInstallPath === path) this.pendingInstallInfo = info
      } catch (e) {
        if (this.pendingInstallPath === path) {
          this.pendingInstallInfoError = '读取应用信息失败：' + (e && e.message ? e.message : e)
        }
      } finally {
        if (this.pendingInstallPath === path) this.installInfoLoading = false
      }
    },
    async InstallLocalAmr() {
      if (this.installInfoLoading) return
      const path = this.pendingInstallPath
      this.showInstallConfirm = false
      if (!path) return
      try {
        const native = await import('hyperfile_local_ops').then((m) => m.default || m)
        if (this.isPendingSelfUpdate) {
          const task = await native.updateAmr(path)
          this.showToast('HyperFile 自更新已启动')
          this.showTaskResult(native, task.taskId, 'HyperFile 自更新')
          return
        }
        const task = await native.installAmr(path)
        this.showToast('安装任务已启动')
        this.showTaskResult(native, task.taskId, 'AMR 安装')
      } catch (e) {
        this.showToast('启动安装失败：' + (e && e.message ? e.message : e))
      }
    },
    async showTaskResult(native, taskId, title) {
      this.scriptResultTitle = title
      this.scriptResultText = '正在运行…'
      this.scriptRunning = true
      this.showScriptResult = true
      try {
        for (let i = 0; i < 600; i++) {
          const result = await native.taskStatus(taskId)
          this.scriptResultText = (result.output || '（无输出）') +
            (result.running ? '\\n\\n运行中…' : '\\n\\n退出码：' + result.exitCode)
          if (!result.running) {
            this.scriptRunning = false
            return
          }
          await new Promise((resolve) => this.$page.setTimeout(resolve, 500))
        }
        this.scriptRunning = false
        this.scriptResultText += '\\n\\n等待超时；任务可能仍在运行。'
      } catch (e) {
        this.scriptRunning = false
        this.scriptResultText = '读取任务结果失败：' + (e && e.message ? e.message : e)
      }
    },
    async RunShellScript() {
      const path = this.pendingScriptPath
      this.showScriptConfirm = false
      if (!path) return
      try {
        const native = await import('hyperfile_local_ops').then((m) => m.default || m)
        const task = await native.runScript(path)
        this.showTaskResult(native, task.taskId, path)
      } catch (e) {
        this.showToast('启动脚本失败：' + (e && e.message ? e.message : e))
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
    },
    iconFor(entry) {
      return iconPathForName(entry && entry.name, !!(entry && entry.isDirectory))
    },
    iconForName(name) {
      return iconPathForName(name, false)
    }
  }
}
</script>

<style lang="less">
@import '../../styles/material.less';
</style>
