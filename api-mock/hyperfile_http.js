/**
 * build:web 预览用 hyperfile_http mock（真机走 libjsapi_hyperfile_http.so）。
 */
let running = false;
let received = 3;

export default {
  async start(port, dir) {
    running = true;
    return { running: true, port: port || 8080, ip: '192.168.1.233', dir, received };
  },
  async stop() {
    running = false;
    return { running: false, port: 8080, ip: '', dir: '', received };
  },
  async status() {
    return {
      running,
      port: 8080,
      ip: running ? '192.168.1.233' : '',
      dir: '/userdisk/Favorite/HyperFile',
      received,
      lastFile: '示例文档.pdf'
    };
  }
};
