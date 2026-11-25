/*
 * HyperFile reader pagination.
 * JS-only port of Readex's basic estimating paginator; deliberately does not
 * import or depend on the Readex native module.
 */

const OPENING = '「『（【《“‘([{<';
const CLOSING = '，。！？；：、）」』】〕〉》”’)]}>…';
const PAIRS = '—…～ー々';
const PUNCTUATION = '，。！？；：、：「」『』——……';

function isAsciiWord(ch) {
  return !!ch && /^[A-Za-z0-9]$/.test(ch);
}

function isOpening(ch) {
  return OPENING.indexOf(ch) >= 0;
}

function isClosing(ch) {
  return CLOSING.indexOf(ch) >= 0;
}

function isPair(ch) {
  return PAIRS.indexOf(ch) >= 0;
}

function isSpace(ch) {
  return ch === ' ' || ch === '\t' || ch === '\u3000';
}

function charWidth(ch, fontSize, estimator) {
  const cp = ch.codePointAt(0);
  if (cp === 9) return fontSize * estimator.asciiWidthEm * 4;
  if (PUNCTUATION.indexOf(ch) >= 0) {
    return fontSize * estimator.punctuationWidthEm;
  }
  if (cp >= 0x20 && cp <= 0x7e) {
    return fontSize * estimator.asciiWidthEm;
  }
  if (cp > 0xffff ||
      (cp >= 0x2e80 && cp <= 0x9fff) ||
      (cp >= 0xac00 && cp <= 0xd7af) ||
      (cp >= 0xf900 && cp <= 0xfaff) ||
      (cp >= 0xfe30 && cp <= 0xfe6f) ||
      (cp >= 0xff00 && cp <= 0xffef)) {
    return fontSize * estimator.cjkWidthEm;
  }
  return fontSize * estimator.asciiWidthEm;
}

function canBreak(chars, boundary) {
  if (boundary <= 0 || boundary >= chars.length) return false;
  const left = chars[boundary - 1];
  const right = chars[boundary];
  if (isOpening(left) || isClosing(right)) return false;
  if (isAsciiWord(left) && isAsciiWord(right)) return false;
  if (isPair(left) && left === right) return false;
  return true;
}

function wrapParagraph(paragraph, maxWidth, fontSize, letterSpacing, estimator) {
  const chars = Array.from(paragraph);
  if (!chars.length) return [''];

  const lines = [];
  let start = 0;
  while (start < chars.length) {
    let width = 0;
    let i = start;
    let lastBreak = -1;

    while (i < chars.length) {
      const nextWidth = charWidth(chars[i], fontSize, estimator) +
        (i < chars.length - 1 ? letterSpacing : 0);

      if (width + nextWidth > maxWidth && i > start) break;
      width += nextWidth;
      i++;

      if (canBreak(chars, i)) {
        lastBreak = i;
      }

      // A single glyph wider than the column must still make progress.
      if (width > maxWidth && i === start + 1) break;
    }

    let end = i;
    if (i < chars.length && lastBreak > start) end = lastBreak;
    if (end <= start) end = Math.min(start + 1, chars.length);

    lines.push(chars.slice(start, end).join(''));
    start = end;
  }
  return lines;
}

/**
 * @param {string} text decoded text content
 * @param {object} config page dimensions and font settings
 * @returns {string[]} pages, each containing explicit line breaks
 */
export function paginateText(text, config) {
  const content = text == null ? '' : String(text);
  const cfg = config || {};
  const fontSize = Number(cfg.fontSize) || 16;
  const lineHeight = fontSize * (Number(cfg.lineHeight) || 1.45);
  const width = Math.max(1, (Number(cfg.width) || 760) -
    (Number(cfg.paddingLeft) || 0) - (Number(cfg.paddingRight) || 0));
  const height = Math.max(1, (Number(cfg.height) || 160) -
    (Number(cfg.paddingTop) || 0) - (Number(cfg.paddingBottom) || 0));
  const letterSpacing = Math.max(0, Number(cfg.letterSpacing) || 0);
  const estimator = Object.assign({
    asciiWidthEm: 0.5,
    cjkWidthEm: 1,
    punctuationWidthEm: 1
  }, cfg.estimator || {});

  const paragraphs = content.replace(/\r\n/g, '\n').replace(/\r/g, '\n').split('\n');
  const lines = [];
  for (let p = 0; p < paragraphs.length; p++) {
    const wrapped = wrapParagraph(paragraphs[p], width, fontSize, letterSpacing, estimator);
    for (let i = 0; i < wrapped.length; i++) lines.push(wrapped[i]);
    if (p < paragraphs.length - 1) lines.push('');
  }
  if (!lines.length) lines.push('');

  const maxLines = Math.max(1, Math.floor(height / lineHeight));
  const pages = [];
  for (let i = 0; i < lines.length; i += maxLines) {
    pages.push(lines.slice(i, i + maxLines).join('\n'));
  }
  return pages.length ? pages : [''];
}
