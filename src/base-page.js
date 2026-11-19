/**
 * 页面基类，两件事：
 *   1. 管理页面级定时器，页面销毁时自动清理；
 *   2. 把 Page 生命周期桥接到根组件的同名方法（onShow/onHide/onDestroy/onUnload）。
 *      注意：falcon 的 Page 与 Vue 根组件是两个对象，组件里的同名钩子不会被
 *      运行时自动调用，必须在这里转发。组件方法里的 this 是 Vue 实例，
 *      定时器等工具请通过 this.$page 访问。
 */
export class BasePage extends $falcon.Page {
  constructor() {
    super();
    this._timers = new Set();
    this.options = {};
  }

  // 定时器工具（组件里用 this.$page.setTimeout / setInterval / clearTimer）
  setTimeout(fn, ms) {
    const token = setTimeout(() => {
      this._timers.delete(token);
      fn();
    }, ms);
    this._timers.add(token);
    return token;
  }

  setInterval(fn, ms) {
    const token = setInterval(fn, ms);
    this._timers.add(token);
    return token;
  }

  clearTimer(token) {
    clearTimeout(token);
    clearInterval(token);
    this._timers.delete(token);
  }

  _clearAllTimers() {
    for (const token of this._timers) {
      clearTimeout(token);
      clearInterval(token);
    }
    this._timers.clear();
  }

  _callRootHook(name, arg) {
    const root = this.$root;
    if (root && typeof root[name] === 'function') {
      try {
        root[name](arg);
      } catch (err) {
        console.log('[hyperbili] 组件钩子 ' + name + ' 出错:', err);
      }
    }
  }

  // ---- Page 生命周期 ----

  onLoad(options) {
    if (typeof super.onLoad === 'function') super.onLoad(options);
    this.options = options || {};
    this._callRootHook('onLoad', this.options);
  }

  onNewOptions(options) {
    if (typeof super.onNewOptions === 'function') super.onNewOptions(options);
    this.options = options || {};
    this._callRootHook('onNewOptions', this.options);
  }

  onShow(options) {
    if (typeof super.onShow === 'function') super.onShow(options);
    this._callRootHook('onShow', options);
  }

  onHide() {
    if (typeof super.onHide === 'function') super.onHide();
    this._callRootHook('onHide');
  }

  onDestroy() {
    this._callRootHook('onDestroy');
    this._clearAllTimers();
    if (typeof super.onDestroy === 'function') super.onDestroy();
  }

  onUnload() {
    this._callRootHook('onUnload');
    this._clearAllTimers();
    if (typeof super.onUnload === 'function') super.onUnload();
  }
}
