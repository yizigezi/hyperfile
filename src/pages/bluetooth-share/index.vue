<template>
  <div class="app-page">
    <div class="app-topbar">
      <div class="icon-btn" @click="goBack"><image class="material-icon" src="/assets/icons/arrow_back.png" /></div>
      <text class="app-title">蓝牙共享</text><div class="top-spacer"></div>
      <div class="icon-btn" @click="refreshStatus"><image class="material-icon" src="/assets/icons/refresh.png" /></div>
    </div>
    <div class="page-body share-page">
      <scroller class="share-shelf" :style="{flexDirection: 'row'}" scroll-direction="horizontal" show-scrollbar="false">
        <div class="settings-card settings-card-wide"><scroller class="card-scroll" scroll-y="true" show-scrollbar="false">
          <text class="section-heading">蓝牙适配器</text><text class="setting-name">{{ adapterReady ? '蓝牙已就绪' : '未就绪' }}</text>
          <text class="setting-description">{{ adapterInfo || '检测设备蓝牙控制器与 BlueZ' }}</text>
          <div class="setting-action-card" @click="refreshStatus"><text class="setting-name">刷新状态</text><text class="setting-chevron">检查</text></div>
        </scroller></div>
        <div class="settings-card settings-card-actions"><scroller class="card-scroll" scroll-y="true" show-scrollbar="false">
          <text class="section-heading">附近设备</text>
          <div class="setting-action-card" @click="scanDevices"><text class="setting-name">搜索设备</text><text class="setting-description">扫描附近可发现的蓝牙设备</text></div>
          <div class="setting-action-card" @click="enablePairing"><text class="setting-name">开放配对</text><text class="setting-description">允许其他设备发现并配对</text></div>
        </scroller></div>
        <div class="settings-card settings-card-wide"><scroller class="card-scroll" scroll-y="true" show-scrollbar="false">
          <text class="section-heading">设备列表</text><text class="setting-description" v-if="devices.length === 0">搜索后显示设备</text>
          <div class="setting-action-card" v-for="device in devices" :key="device.address" @click="pairConnect(device.address)"><text class="setting-name">{{ device.name || device.address }}</text><text class="setting-description">{{ device.address }} · {{ device.connected ? "已连接" : (device.paired ? "已配对" : "点击配对并连接") }}</text><text class="setting-chevron">{{ device.connected ? "断开" : "连接" }}</text></div>
        </scroller></div>
        <div class="settings-card settings-card-about"><scroller class="card-scroll" scroll-y="true" show-scrollbar="false">
          <text class="section-heading">文件传输</text><text class="setting-name">不依赖 OBEX</text>
          <text class="setting-description">词典笔系统没有 OBEX。后续采用自定义蓝牙文件传输协议实现双向收发，不依赖 obexd / obexftp。</text>
          <text class="settings-note">当前版本仅提供适配器检查、搜索和配对控制，文件收发尚未实现。</text>
        </scroller></div>
      </scroller>
    </div>
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
      toast: ''
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
    parseDevices(lines) {
      const found = {}
      lines.filter((line) => line.indexOf('Device ') === 0).forEach((line) => {
        const match = line.match(/^Device\\s+([0-9A-Fa-f:]{17})\\s*(.*)$/)
        if (match) found[match[1].toUpperCase()] = { address: match[1].toUpperCase(), name: match[2] || '', paired: false, connected: false }
      })
      return Object.keys(found).map((address) => found[address])
    },
    parseOutput(output) {
      const lines = String(output || '').split(/\\r?\\n/).map((line) => line.trim()).filter(Boolean)
      this.devices = this.parseDevices(lines)
      this.adapterReady = lines.indexOf('ADAPTER:UP') >= 0
      const info = lines.find((line) => line.indexOf('ADAPTER:') === 0 && line !== 'ADAPTER:UP')
      this.adapterInfo = info ? info.slice(8) : (this.adapterReady ? 'hci0 / BlueZ' : '未检测到可用适配器')
    },
    async refreshStatus() {
      try {
        const output = await this.runShell('if command -v bluetoothctl >/dev/null 2>&1; then bluetoothctl show 2>/dev/null | grep -E "Powered:|Name:" | head -n 1; bluetoothctl devices 2>/dev/null; else echo "ADAPTER:bluetoothctl 不存在"; fi\nif hciconfig hci0 2>/dev/null | grep -q UP; then echo "ADAPTER:UP"; else echo "ADAPTER:DOWN"; fi')
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
        this.devices = this.parseDevices(String(output).split(/\\r?\\n/).map((line) => line.trim()).filter(Boolean))
        this.showToast('搜索完成：' + this.devices.length + ' 台设备')
      } catch (e) {
        this.showToast('搜索失败：' + (e && e.message ? e.message : e))
      }
    },
    async pairConnect(address) {\n      const mac = String(address || '').toUpperCase()\n      if (!/^([0-9A-F]{2}:){5}[0-9A-F]{2}$/.test(mac)) { this.showToast('设备地址无效'); return }\n      try {\n        this.showToast('正在配对并连接…')\n        const output = await this.runShell('bluetoothctl power on >/dev/null 2>&1; bluetoothctl pair ' + mac + ' 2>&1; bluetoothctl trust ' + mac + ' 2>&1; bluetoothctl connect ' + mac + ' 2>&1; bluetoothctl info ' + mac + ' 2>&1')\n        const connected = /Connected:\\s*yes/i.test(output)\n        const paired = /Paired:\\s*yes/i.test(output)\n        const device = this.devices.find((item) => item.address === mac)\n        if (device) { device.connected = connected; device.paired = paired }\n        this.showToast(connected ? '设备已连接' : (paired ? '已配对，连接未成功' : '配对或连接失败'))\n      } catch (e) { this.showToast('连接失败：' + (e && e.message ? e.message : e)) }\n    },\n    async enablePairing() {
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
