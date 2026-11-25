import { BasePage } from './base-page.js'

class App extends $falcon.App {
  constructor() {
    super()
  }

  onLaunch(options) {
    super.onLaunch(options)
    $falcon.useDefaultBasePageClass(BasePage)
  }

  onShow() {
    super.onShow()
  }

  onHide() {
    super.onHide()
  }

  onDestroy() {
    super.onDestroy()
  }
}

export default App

// 与 doge-reader / readex-nt 保持一致：为 Falcon/QuickJS 页面运行环境补齐基础全局对象。
// 第三方组件会碰 window/process，缺了会 ReferenceError 炸掉整个页面链。
try {
  if (typeof globalThis.window === 'undefined') {
    globalThis.window = {
      requestAnimationFrame: (fn) => setTimeout(fn, 16),
      cancelAnimationFrame: (token) => clearTimeout(token)
    }
  }
} catch (err) {
  console.log('window shim skip:', err)
}

try {
  if (typeof globalThis.process === 'undefined') {
    globalThis.process = {
      env: {
        NODE_ENV: 'production'
      }
    }
  }
} catch (err) {
  console.log('process shim skip:', err)
}
