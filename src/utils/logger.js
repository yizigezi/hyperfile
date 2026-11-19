/**
 * 极简 logger（HyperBilibili 的 global.logger 等价物）。
 */
const TAG = '[hyperbili]';

export const logger = {
  log(...args) {
    console.log(TAG, ...args);
  },
  warn(...args) {
    console.log(TAG, '[warn]', ...args);
  },
  error(...args) {
    console.log(TAG, '[error]', ...args);
  }
};

export default logger;
