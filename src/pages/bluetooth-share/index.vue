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
          <text class="section-heading">标准蓝牙文件接收</text>
          <text class="setting-name">OBEX Object Push · Channel 9</text>
          <text class="setting-description">兼容支持 OPP 的手机系统蓝牙分享。需要设备端 BlueZ 提供 sdptool。</text>
          <div class="setting-action-card" @click="toggleObexReceiver"><text class="setting-name">{{ obexRunning ? "停止 OBEX 接收" : "开启 OBEX 接收" }}</text><text class="setting-description">{{ obexRunning ? "正在等待手机发送文件" : "让手机通过系统蓝牙分享发送文件" }}</text><text class="setting-chevron">{{ obexRunning ? "停止" : "启动" }}</text></div>
          <text class="settings-note">SDP：{{ obexSdpRegistered ? "已注册" : "未确认" }} · 已接收 {{ obexReceived }} 个 · 最近：{{ obexLastFile || "暂无" }}</text>
          <text class="section-heading">HyperFile 自定义传输</text>
          <text class="setting-name">RFCOMM · Channel 7</text>
          <text class="setting-description">自定义 HFBT 协议，不依赖 OBEX。接收文件保存到 /userdisk/Favorite/HyperFile。</text>
          <div class="setting-action-card" @click="toggleReceiver"><text class="setting-name">{{ receiverRunning ? '停止接收' : '开启接收' }}</text><text class="setting-description">{{ receiverRunning ? '正在监听蓝牙文件' : '开启后允许兼容端发送文件' }}</text><text class="setting-chevron">{{ receiverRunning ? '停止' : '启动' }}</text></div>
          <text class="setting-description">发送到设备 MAC</text>
          <input class="setting-input" v-model="sendMac" placeholder="AA:BB:CC:DD:EE:FF" />
          <text class="setting-description">词典笔上的文件完整路径</text>
          <input class="setting-input" v-model="sendPath" placeholder="/userdisk/Favorite/example.txt" />
          <div class="setting-action-card" @click="sendBluetoothFile"><text class="setting-name">发送文件</text><text class="setting-chevron">发送</text></div>
          <text class="settings-note">已接收 {{ receiverReceived }} 个文件；最近：{{ receiverLastFile || '暂无' }}</text>
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
      toast: '',
      obexRunning: false,
      obexSdpRegistered: false,
      obexReceived: 0,
      obexLastFile: ''
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
        const match = line.match(/^Device\s+([0-9A-Fa-f:]{17})\s*(.*)$/)
        if (match) found[match[1].toUpperCase()] = { address: match[1].toUpperCase(), name: match[2] || '', paired: false, connected: false }
      })
      return Object.keys(found).map((address) => found[address])
    },
    parseOutput(output) {
      const lines = String(output || '').split(/\r?\n/).map((line) => line.trim()).filter(Boolean)
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
        this.devices = this.parseDevices(String(output).split(/\r?\n/).map((line) => line.trim()).filter(Boolean))
        this.showToast('搜索完成：' + this.devices.length + ' 台设备')
      } catch (e) {
        this.showToast('搜索失败：' + (e && e.message ? e.message : e))
      }
    },
    async pairConnect(address) {
      const mac = String(address || '').toUpperCase()
      if (!/^([0-9A-F]{2}:){5}[0-9A-F]{2}$/.test(mac)) { this.showToast('设备地址无效'); return }
      try {
        this.showToast('正在配对并连接…')
        const device = this.devices.find((item) => item.address === mac)
        const command = device && device.connected
          ? 'bluetoothctl disconnect ' + mac
          : 'bluetoothctl power on >/dev/null 2>&1; bluetoothctl pair ' + mac + ' 2>&1; bluetoothctl trust ' + mac + ' 2>&1; bluetoothctl connect ' + mac + ' 2>&1'
        const output = await this.runShell(command + '; bluetoothctl info ' + mac + ' 2>&1')
        const connected = /Connected:\\s*yes/i.test(output)
        const paired = /Paired:\\s*yes/i.test(output)
        if (device) { device.connected = connected; device.paired = paired }
        this.showToast(connected ? '设备已连接' : (paired ? '已配对，连接未成功' : '配对或连接失败'))
      } catch (e) { this.showToast('连接失败：' + (e && e.message ? e.message : e)) }
    },
    async toggleReceiver() {
      try {
        const bt = await import('hyperfile_bluetooth').then((m) => m.default || m)
        if (this.receiverRunning) await bt.stopServer()
        else await bt.startServer('/userdisk/Favorite/HyperFile')
        const status = await bt.status()
        this.receiverRunning = !!status.running
        this.receiverReceived = status.received || 0
        this.receiverLastFile = status.lastFile || ''
        this.showToast(this.receiverRunning ? '蓝牙接收服务已启动，通道 7' : '蓝牙接收服务已停止')
      } catch (e) { this.showToast('蓝牙接收服务失败：' + (e && e.message ? e.message : e)) }
    },
    async toggleObexReceiver() {
      try {
        const obex = await import('hyperfile_obex').then((m) => m.default || m)
        if (this.obexRunning) await obex.stopServer()
        else await obex.startServer('/userdisk/Favorite/HyperFile')
        let status = await obex.status()
        for (let i = 0; i < 20 && !status.running && !this.obexRunning; i++) {
          await new Promise((resolve) => this.$page.setTimeout(resolve, 150))
          status = await obex.status()
        }
        this.obexRunning = !!status.running
        this.obexSdpRegistered = !!status.sdpRegistered
        this.obexReceived = status.received || 0
        this.obexLastFile = status.lastFile || ''
        this.showToast(this.obexRunning ? 'OBEX 接收服务已启动' : 'OBEX 启动未成功，请检查 BlueZ / sdptool')
      } catch (e) { this.showToast('OBEX 服务失败：' + (e && e.message ? e.message : e)) }
    },
    async sendBluetoothFile() {
      const mac = String(this.sendMac || '').trim().toUpperCase()
      const path = String(this.sendPath || '').trim()
      if (!/^([0-9A-F]{2}:){5}[0-9A-F]{2}$/.test(mac) || !path) { this.showToast('请填写正确的 MAC 和文件路径'); return }
      try {
        const bt = await import('hyperfile_bluetooth').then((m) => m.default || m)
        this.showToast('正在通过 RFCOMM 发送…')
        await bt.sendFile(mac, path)
        this.showToast('文件发送完成')
      } catch (e) { this.showToast('发送失败：' + (e && e.message ? e.message : e)) }
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
