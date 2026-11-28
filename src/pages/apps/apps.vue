<template>
  <div class="app-page">
    <div class="app-topbar">
      <div class="icon-btn" @click="goBack">
        <image class="material-icon" src="/assets/icons/arrow_back.png" />
      </div>
      <text class="app-title">应用</text>
      <div class="top-spacer"></div>
    </div>

    <div class="page-body apps-body">
      <scroller class="apps-shelf" :style="{flexDirection: 'row'}" scroll-direction="horizontal" show-scrollbar="false">
        <div class="app-card" v-for="app in apps" :key="app.name" @click="openApp(app)">
          <div class="app-card-icon-box">
            <image class="app-card-icon" :src="app.icon" />
          </div>
          <text class="app-card-name">{{ app.name }}</text>
        </div>
      </scroller>
    </div>
  </div>
</template>

<script>
export default {
  name: 'apps',
  data() {
    return {
      apps: [
        { name: 'Wi-Fi 共享', icon: '/assets/icons/wifi.png', action: 'share' },
        { name: 'Readex 书架', icon: '/assets/icons/menu_book.png', action: 'shelf' }
      ]
    }
  },
  methods: {
    openApp(app) {
      if (app.action === 'share') $falcon.navTo('index', { tab: 'share' })
      else if (app.action === 'shelf') $falcon.navTo('shelf')
      this.$page.finish()
    },
    goBack() {
      this.$page.finish()
    }
  }
}
</script>
