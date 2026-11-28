<template>
  <div class="app-page">
    <div class="app-topbar">
      <div class="icon-btn" @click="goBack">
        <image class="material-icon" src="/assets/icons/arrow_back.png" />
      </div>
      <text class="app-title">关于</text>
      <div class="top-spacer"></div>
    </div>

    <div class="page-body about-body">
      <div class="about-card">
        <div class="about-left">
          <div class="about-wordmark" :style="{flexDirection: 'row'}">
            <text class="about-word-hyper">Hyper</text>
            <text class="about-word-file">File</text>
          </div>
          <text class="about-version">版本 {{ version }}</text>
        </div>
        <div class="about-divider"></div>
        <div class="about-right">
          <div class="about-info-row" :style="{flexDirection: 'row'}">
            <text class="about-info-label">构建</text>
            <text class="about-info-value">{{ branch }} · {{ commit }} · {{ builtAt }}</text>
          </div>
          <div class="about-info-row" :style="{flexDirection: 'row'}">
            <text class="about-info-label">仓库</text>
            <text class="about-info-value">{{ repo }}</text>
          </div>
          <div class="about-info-row" :style="{flexDirection: 'row'}">
            <text class="about-info-label">许可</text>
            <text class="about-info-value">{{ license }}</text>
          </div>
        </div>
      </div>
    </div>
  </div>
</template>

<script>
import BuildInfo from '../../utils/BuildInfo.js'

export default {
  name: 'about',
  data() {
    return {
      version: BuildInfo.version || '0.1.0',
      branch: BuildInfo.branch || 'local',
      commit: BuildInfo.commit || 'local',
      builtAt: BuildInfo.builtAt || '—',
      repo: 'github.com/yizigezi/hyperfile',
      license: 'GPL-3.0'
    }
  },
  methods: {
    goBack() {
      this.$page.finish()
    }
  }
}
</script>
