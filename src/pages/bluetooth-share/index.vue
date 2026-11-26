<template>
  <div class="share-manager-page">
    <div class="share-manager-header">
      <div class="manager-back" @click="goBack">
        <image class="material-icon" src="/assets/icons/arrow_back.png" />
      </div>
      <text class="share-manager-title">蓝牙共享</text>
      <div class="manager-spacer"></div>
      <div class="manager-refresh" @click="refreshStatus">
        <image class="material-icon" src="/assets/icons/refresh.png" />
      </div>
    </div>
    <scroller class="share-manager-content" scroll-y="true" show-scrollbar="false">
      <div class="manager-panel">
        <text class="section-heading">蓝牙适配器</text>
        <text class="manager-status">{{ adapterReady ? '蓝牙已就绪' : '正在检查蓝牙环境' }}</text>
        <text class="manager-description">{{ adapterInfo || '检测设备蓝牙控制器与 BlueZ 工具' }}</text>
        <div class="manager-actions" :style="{flexDirection: 'row'}">
          <div class="manager-action" @click="scanDevices"><text class="manager-action-label">搜索设备</text></div>
          <div class="manager-action" @click="enablePairing"><text class="manager-action-label">开放配对</text></div>
        </div>
      </div>
      <div class="manager-panel">
        <text class="section-heading">附近设备</text>
        <text class="manager-description">搜索结果由系统 bluetoothctl 提供</text>
        <text class="manager-device" v-for="device in devices" :key="device">{{ device }}</text>
        <text class="manager-description" v-if="devices.length === 0">尚无设备列表，点击“搜索设备”</text>
      </div>
      <div class="manager-panel">
        <text class="section-heading">文件传输能力</text>
        <text class="manager-capability" v-for="item in capabilities" :key="item.name">{{ item.name }}：{{ item.available ? '可用' : '未找到' }}</text>
        <text class="manager-description">双向文件传输需要设备端具备 OBEX 服务/客户端。此页面会检查工具是否存在；未检测到时不会显示为可传输。</text>
      </div>
      <div class="manager-panel">
        <text class="section-heading">收发目录</text>
        <text class="manager-path">/userdisk/Favorite/HyperFile</text>
      </div>
    </scroller>
    <div class="toast" v-if="toast"><text class="toast-text">{{ toast }}</text></div>
  </div>
</template>

<script>
import { fs } from '../../utils/FileSystem/FileSystem.js'
const DATA_DIR = '/userdisk/HyperFile_Data'
const SCRIPT_PATH = DATA_DIR + '/bluetooth-check.sh'

export default {
  name: 'bluetooth-share',
  data() {
    return {
      adapterReady: false,
      adapterInfo: '',
      devices: [],
      capabilities: [],
      toast: '',
      runningTask: false
    }
  },
  onShow() {
    this.refreshStatus()
  },
  methods: {
    goBack() { this.$page.finish() },
    showToast(message) {
      this.toast = message
      this.$page.setTimeout(() => { this.toast = '' }, 3000)
    },
    async runShell(script) {
      await fs.mkdir(DATA_DIR)
      await fs.writeFile(SCRIPT_PATH, '#!/bin/sh\n' + script + '\n')
      const native = await import('hyperfile_local_ops').then((m) => m.default || m)
      const task = await native.runScript(SCRIPT_PATH)
      for (let i = 0; i < 80; i++) {
        const result = await native.taskStatus(task.taskId)
        if (!result.running) return String(result.output || '')
        await new Promise((resolve) => this.$page.setTimeout(resolve, 250))
      }
      throw new Error('命令执行超时')
    },
    parseOutput(output) {
      const lines = String(output || '').split(/\r?\n/).map((line) => line.trim()).filter(Boolean)
      this.devices = lines.filter((line) => line.indexOf('Device ') === 0).map((line) => line.replace(/^Device\s+/, ''))
      this.capabilities = ['bluetoothctl', 'obexd', 'obexftp', 'obexctl'].map((name) => ({
        name,
        available: lines.indexOf('TOOL:' + name) >= 0
      }))
      this.adapterReady = lines.indexOf('ADAPTER:UP') >= 0
      const info = lines.find((line) => line.indexOf('ADAPTER:') === 0 && line !== 'ADAPTER:UP')
      this.adapterInfo = info ? info.slice(8) : (this.adapterReady ? 'hci0 / BlueZ' : '未检测到可用适配器')
    },
    async refreshStatus() {
      try {
        const output = await this.runShell('for tool in bluetoothctl obexd obexftp obexctl; do command -v "$tool" >/dev/null 2>&1 && echo "TOOL:$tool"; done\nif command -v bluetoothctl >/dev/null 2>&1; then bluetoothctl show 2>/dev/null | grep -E "Powered:|Name:" | head -n 1; bluetoothctl devices 2>/dev/null; else echo "ADAPTER:bluetoothctl 不存在"; fi\nif hciconfig hci0 2>/dev/null | grep -q UP; then echo "ADAPTER:UP"; else echo "ADAPTER:DOWN"; fi')
        this.parseOutput(output)
      } catch (e) {
        this.adapterReady = false
        this.adapterInfo = e && e.message ? e.message : String(e)
        this.showToast('蓝牙环境检测失败')
      }
    },
    async scanDevices() {
      try {
        this.showToast('正在搜索蓝牙设备…')
        const output = await this.runShell('bluetoothctl power on >/dev/null 2>&1; bluetoothctl scan on >/dev/null 2>&1; sleep 8; bluetoothctl scan off >/dev/null 2>&1; bluetoothctl devices 2>/dev/null')
        this.devices = String(output).split(/\r?\n/).map((line) => line.trim()).filter((line) => line.indexOf('Device ') === 0).map((line) => line.replace(/^Device\s+/, ''))
        this.showToast('搜索完成：' + this.devices.length + ' 台设备')
      } catch (e) {
        this.showToast('搜索失败：' + (e && e.message ? e.message : e))
      }
    },
    async enablePairing() {
      try {
        await this.runShell('bluetoothctl power on >/dev/null 2>&1; bluetoothctl pairable on; bluetoothctl discoverable on')
        this.showToast('已开启可配对与可发现')
      } catch (e) {
        this.showToast('开启配对失败：' + (e && e.message ? e.message : e))
      }
    }
  }
}
</script>

<style lang="less">
@import '../../styles/material.less';
</style>
