#!/usr/bin/env node
/**
 * Patch a raw LGFXScreenBuilder .h export for ESP32 GCC 8.4.
 *
 * LGFXScreenBuilder 0.2.x descriptor structs use default member initializers,
 * so brace-initialized arrays from the web export do not compile on ESP32.
 * This script rewrites descriptor tables using factory helpers.
 *
 * Usage:
 *   node scripts/patch-lgfxsb-export.mjs include/RotaryUi.h
 *   node scripts/patch-lgfxsb-export.mjs include/RotaryUi.h --no-swap-assets
 */

import fs from 'node:fs';

const file = process.argv[2];
const swapAssets = !process.argv.includes('--no-swap-assets');
if (!file) {
  console.error('Usage: node scripts/patch-lgfxsb-export.mjs <exported.h>');
  process.exit(1);
}

const src = fs.readFileSync(file, 'utf8');

function isAlreadyPatched(text) {
  if (text.includes('LGFXSB_ESP32_PATCHED')) return true;
  return text.includes('inline const lgfxsb::PartDesc* parts()') &&
         !text.includes('static const lgfxsb::PartDesc kParts[]');
}

function extractBalancedFn(text, signature) {
  const start = text.indexOf(signature);
  if (start < 0) return null;
  const braceStart = text.indexOf('{', start);
  if (braceStart < 0) return null;
  let depth = 0;
  for (let i = braceStart; i < text.length; i++) {
    const ch = text[i];
    if (ch === '{') depth++;
    else if (ch === '}') {
      depth--;
      if (depth === 0) {
        let end = i + 1;
        if (text[end] === '\n') end++;
        return { start, end, body: text.slice(start, end) };
      }
    }
  }
  return null;
}

function swap565Literal(hex) {
  const v = parseInt(hex, 16);
  const swapped = ((v & 0xff) << 8) | (v >> 8);
  return swapped.toString(16).padStart(4, '0');
}

function swapAssetArrays(text) {
  return text.replace(/static const uint16_t (kAsset_\w+)\[\] = \{([^}]+)\};/g, (match, name, body) => {
    const swappedBody = body.replace(/0x([0-9a-fA-F]{4})/g, (_, hex) => `0x${swap565Literal(hex)}`);
    return `static const uint16_t ${name}[] = {${swappedBody}};`;
  });
}

function markAssetSwap(text) {
  if (text.includes('LGFXSB_SWAP_ASSETS')) return text;
  return text.replace(
    '// LGFXSB_ESP32_PATCHED',
    '// LGFXSB_ESP32_PATCHED\n// LGFXSB_SWAP_ASSETS — RGB565 byte order for LovyanGFX pushImage on ESP32',
  );
}

function repairAssetOrder(text) {
  const assetData = text.search(/static const uint16_t kAsset_\w+\[\]/);
  const assetsFn = text.search(/inline const lgfxsb::AssetDesc\* assets\(\)/);
  if (assetData < 0 || assetsFn < 0 || assetsFn < assetData) {
    return text;
  }

  const assets = extractBalancedFn(text, 'inline const lgfxsb::AssetDesc* assets()');
  const project = extractBalancedFn(text, 'inline const lgfxsb::Project& project()');
  if (!assets || !project) return text;

  let repaired = text;
  const removeRanges = [assets, project].sort((a, b) => b.start - a.start);
  for (const block of removeRanges) {
    repaired = repaired.slice(0, block.start) + repaired.slice(block.end);
  }

  const tail = `\n${assets.body}\n${project.body}\n`;
  const marker = '} // namespace detail';
  const closeIdx = repaired.lastIndexOf(marker);
  if (closeIdx < 0) return text;
  return repaired.slice(0, closeIdx) + tail + marker + repaired.slice(closeIdx + marker.length);
}

if (isAlreadyPatched(src)) {
  let repaired = repairAssetOrder(src);
  if (swapAssets && src.includes('kAsset_') && !src.includes('LGFXSB_SWAP_ASSETS')) {
    repaired = markAssetSwap(swapAssetArrays(repaired));
  }
  const parts = parsePatchedParts(repaired);
  const scenes = computeSceneCounts(parts, parsePatchedScenes(repaired));
  repaired = finalizeHeader(repaired, parts, scenes);
  if (repaired !== src) {
    fs.writeFileSync(file, repaired);
    if (swapAssets && src.includes('kAsset_') && !src.includes('LGFXSB_SWAP_ASSETS')) {
      console.log(`${file}: byte-swapped image assets for LovyanGFX`);
    } else if (repaired.includes('void showHome(') && !src.includes('void showHome(')) {
      console.log(`${file}: injected showHome() for visibility-based Home scene`);
    } else {
      console.log(`${file}: repaired asset function order`);
    }
  } else {
    console.log(`${file}: already patched`);
  }
  process.exit(0);
}

function extractBlock(text, startMarker, endMarker) {
  const start = text.indexOf(startMarker);
  if (start < 0) return null;
  const from = start + startMarker.length;
  const end = text.indexOf(endMarker, from);
  if (end < 0) return null;
  return text.slice(from, end);
}

function parseParts(block) {
  const re = /\{\s*"([^"]+)"\s*,\s*lgfxsb::PartType::(\w+)\s*,\s*(-?\d+)\s*\}/g;
  const out = [];
  let m;
  while ((m = re.exec(block)) !== null) {
    out.push({ id: m[1], type: m[2], asset: m[3] });
  }
  return out;
}

function parseScenes(block) {
  const re = /\{\s*(\d+)\s*,\s*"([^"]+)"\s*,\s*(\d+)\s*,\s*(\d+)\s*\}/g;
  const out = [];
  let m;
  while ((m = re.exec(block)) !== null) {
    out.push({ id: m[1], name: m[2], start: m[3], count: m[4] });
  }
  return out;
}

function parseProfiles(block) {
  const re = /\{\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*(\d+)\s*\}/g;
  const out = [];
  let m;
  while ((m = re.exec(block)) !== null) {
    out.push({ w: m[1], h: m[2], rot: m[3] });
  }
  return out;
}

function parseLayouts(block) {
  const lines = block.split('\n').filter((l) => l.trim().startsWith('{'));
  const out = [];
  for (const line of lines) {
    const body = line.replace(/\/\/.*$/, '').trim().replace(/,\s*$/, '');
    const inner = body.slice(1, -1);
    const fields = splitTopLevel(inner);
    if (fields.length < 14) continue;
    out.push({
      x: fields[0], y: fields[1], w: fields[2], h: fields[3],
      x2: fields[4], y2: fields[5], r: fields[6], datum: fields[7],
      size: fields[8], color: fields[9], fill: fields[10], visible: fields[11],
      font: fields[12], text: fields[13],
      comment: (line.match(/\/\/\s*(.+)$/) || [])[1] || '',
    });
  }
  return out;
}

function splitTopLevel(s) {
  const fields = [];
  let cur = '';
  let depth = 0;
  for (let i = 0; i < s.length; i++) {
    const ch = s[i];
    if (ch === '(') depth++;
    if (ch === ')') depth--;
    if (ch === ',' && depth === 0) {
      fields.push(cur.trim());
      cur = '';
      continue;
    }
    cur += ch;
  }
  if (cur.trim()) fields.push(cur.trim());
  return fields;
}

function parseAssets(block) {
  const re = /\{\s*kAsset_(\w+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*\}/g;
  const out = [];
  let m;
  while ((m = re.exec(block)) !== null) {
    out.push({ id: m[1], w: m[2], h: m[3] });
  }
  return out;
}

function parseProject(block) {
  const bg = (block.match(/\/\*background\*\/\s*(0x[0-9a-fA-F]+)/) || [])[1] || '0x000000';
  const hasAssets = block.includes('detail::kAssets');
  return { background: bg, hasAssets };
}

function parsePatchedParts(text) {
  const re = /part\("([^"]+)",\s*lgfxsb::PartType::\w+/g;
  const out = [];
  let m;
  while ((m = re.exec(text)) !== null) {
    out.push({ id: m[1] });
  }
  return out;
}

function parsePatchedScenes(text) {
  const re = /scene\(\d+,\s*"([^"]+)",\s*(\d+),\s*(\d+)\)/g;
  const out = [];
  let m;
  while ((m = re.exec(text)) !== null) {
    out.push({ name: m[1], start: Number(m[2]), count: Number(m[3]) });
  }
  return out;
}

function computeSceneCounts(parts, scenes) {
  return scenes.map((s, i) => {
    const next = scenes[i + 1];
    const actualCount = next ? next.start - s.start : parts.length - s.start;
    return { ...s, count: actualCount };
  });
}

function repairSceneCounts(text, parts, scenes) {
  const fixed = computeSceneCounts(parts, scenes);
  let out = text;
  for (const s of fixed) {
    out = out.replace(
      new RegExp(`scene\\((\\d+), "${s.name}", ${s.start}, \\d+\\)`, 'g'),
      `scene($1, "${s.name}", ${s.start}, ${s.count})`,
    );
  }
  const home = fixed.find((s) => s.name === 'Home');
  if (home) {
    out = out.replace(/#define ROTARY_UI_HOME_PART_COUNT \d+\n/g, '');
    if (out.includes('#define ROTARY_UI_HOME_DYNAMIC 1')) {
      out = out.replace(
        /#define ROTARY_UI_HOME_DYNAMIC 1\n/,
        `#define ROTARY_UI_HOME_DYNAMIC 1\n#define ROTARY_UI_HOME_PART_COUNT ${home.count}\n`,
      );
    }
    out = out.replace(/p\.partCount = \d+;/g, `p.partCount = ${parts.length};`);
  }
  return out;
}

function generateHomeIndexDefines(home, parts) {
  const homeParts = parts.slice(home.start, home.start + home.count);
  if (!homeParts.some((p) => p.id === 'klickBgInactive')) return '';
  return `${homeParts.map((p, i) => `#define ROTARY_UI_HOME_IDX_${p.id} ${i}`).join('\n')}\n`;
}

function generateRenderHomeSceneMethod() {
  return `
  void renderHomeScene(const lgfxsb::Value* values, uint16_t count) {
    renderScene(Scene::Home::id, values, count, nullptr, nullptr, nullptr);
  }`;
}

function homePartIndex(homeParts, id) {
  return homeParts.findIndex((p) => p.id === id);
}

function generateShowHomeMethod(home, parts) {
  const homeParts = parts.slice(home.start, home.start + home.count);
  if (!homeParts.some((p) => p.id === 'klickBgInactive')) {
    return null;
  }

  const lines = [];
  const accentIdx = homePartIndex(homeParts, 'bpmAccent');
  if (accentIdx >= 0) lines.push(`    v[${accentIdx}] = lgfxsb::Value::boolean(editing);`);

  const bpmIdx = homePartIndex(homeParts, 'bpmText');
  if (bpmIdx >= 0) lines.push(`    if (bpmText) v[${bpmIdx}] = lgfxsb::Value::text(bpmText);`);

  const klickInactive = [
    ['klickBgInactive', '!clickEnabled'],
    ['klickTextInactive', '!clickEnabled'],
    ['klickBgActive', 'clickEnabled'],
    ['klickTextActive', 'clickEnabled'],
  ];
  for (const [id, expr] of klickInactive) {
    const i = homePartIndex(homeParts, id);
    if (i >= 0) lines.push(`    v[${i}] = lgfxsb::Value::boolean(${expr});`);
  }

  const pulsInactive = [
    ['pulsBgInactive', '!pulseEnabled'],
    ['pulsTextInactive', '!pulseEnabled'],
    ['pulsBgActive', 'pulseEnabled'],
    ['pulsTextActive', 'pulseEnabled'],
  ];
  for (const [id, expr] of pulsInactive) {
    const i = homePartIndex(homeParts, id);
    if (i >= 0) lines.push(`    v[${i}] = lgfxsb::Value::boolean(${expr});`);
  }

  const intervalIdx = homePartIndex(homeParts, 'intervalText');
  if (intervalIdx >= 0) lines.push(`    if (intervalText) v[${intervalIdx}] = lgfxsb::Value::text(intervalText);`);

  return `
  void showHome(const char* bpmText, const char* intervalText, bool editing,
                bool clickEnabled, bool pulseEnabled) {
    lgfxsb::Value v[${home.count}]{};
${lines.join('\n')}
    renderScene(Scene::Home::id, v, ${home.count}, nullptr, nullptr, nullptr);
  }`;
}

function injectShowHome(text, parts, scenes) {
  const fixedScenes = computeSceneCounts(parts, scenes);
  const home = fixedScenes.find((s) => s.name === 'Home');
  if (!home) return repairSceneCounts(text, parts, scenes);

  const homeParts = parts.slice(home.start, home.start + home.count);
  if (!homeParts.some((p) => p.id === 'klickBgInactive')) {
    return repairSceneCounts(text, parts, scenes);
  }

  const method = generateShowHomeMethod(home, parts);
  const renderMethod = generateRenderHomeSceneMethod();
  const indexDefines = generateHomeIndexDefines(home, parts);

  let out = text.replace(/\n  void showHome\([\s\S]*?\n  \}\n/g, '\n');
  out = out.replace(/\n  void renderHomeScene\([\s\S]*?\n  \}\n/g, '\n');

  const screenMethods = `${method}\n${renderMethod}\n`;
  if (out.match(/void setOverlay\(void \(\*\)fn\)\(Canvas&, const Scene::Home&\)\)/)) {
    out = out.replace(
      /(  void setOverlay\(void \(\*\)fn\)\(Canvas&, const Scene::Home&\)\) \{ _ov_Home = fn; \}\n)/,
      `$1${screenMethods}`,
    );
  } else if (out.match(/void show\(const Scene::Home&/)) {
    out = out.replace(/(  void show\(const Scene::Home&[\s\S]*?\n  \}\n)/, `$1${screenMethods}`);
  } else {
    out = out.replace(/(\n};)(\n\n}  \/\/ namespace RotaryUi)/, `\n${screenMethods}\n$1$2`);
  }

  out = out.replace(/#define ROTARY_UI_HOME_IDX_\w+ \d+\n/g, '');
  out = out.replace(/#define ROTARY_UI_HOME_PART_COUNT \d+\n/g, '');
  out = out.replace(/#define ROTARY_UI_HOME_DYNAMIC 1\n/g, '');

  if (out.includes('#define ROTARY_UI_HAS_SCENE_Home 1')) {
    out = out.replace(
      /#define ROTARY_UI_HAS_SCENE_Home 1\n/,
      `#define ROTARY_UI_HAS_SCENE_Home 1\n#define ROTARY_UI_HOME_DYNAMIC 1\n#define ROTARY_UI_HOME_PART_COUNT ${home.count}\n${indexDefines}`,
    );
  } else {
    out = out.replace(
      '#pragma once\n',
      `#pragma once\n\n#define ROTARY_UI_HOME_DYNAMIC 1\n#define ROTARY_UI_HOME_PART_COUNT ${home.count}\n${indexDefines}`,
    );
  }

  return repairSceneCounts(out, parts, fixedScenes);
}

function repairUndefinedSceneIds(text) {
  if (!text.includes('scene(undefined')) return text;
  let i = 0;
  return text.replace(
    /scene\(undefined,\s*"([^"]+)",\s*(\d+),\s*(\d+)\)/g,
    (_, name, start, count) => `scene(${i++}, "${name}", ${start}, ${count})`,
  );
}

const HOME_ALWAYS_VISIBLE_PARTS = new Set([
  'bpmPanel', 'bpmText', 'bpmLabel', 'intervalBg', 'intervalText', 'swipeHint',
]);

function repairLayoutVisibility(text) {
  let out = text;
  for (const id of HOME_ALWAYS_VISIBLE_PARTS) {
    out = out.replace(
      new RegExp(`(layout\\([^\\n]*), (true|false), false(, [^\\n]*${id}[^\\n]*)`, 'g'),
      '$1, $2, true$3',
    );
  }
  return out;
}

function stripHomeOverlay(text) {
  return text
    .replace(
      /renderScene\(Scene::Home::id,([^;]*), _ov_Home \? &_ovt_Home : nullptr, &s, &_ov_Home\)/g,
      'renderScene(Scene::Home::id,$1, nullptr, nullptr, nullptr)',
    )
    .replace(/\n  void setOverlay\(void \(\*\)fn\)\(Canvas&, const Scene::Home&\)\) \{ _ov_Home = fn; \}\n/g, '\n');
}

function finalizeHeader(text, parts, scenes) {
  let out = injectShowHome(repairUndefinedSceneIds(text), parts, scenes);
  out = repairLayoutVisibility(out);
  out = stripHomeOverlay(out);
  return out;
}

function partIdFromComment(comment) {
  const m = String(comment).match(/\.(\w+)\s*$/);
  return m ? m[1] : '';
}

const scenesBlock = extractBlock(src, 'static const lgfxsb::SceneDesc kScenes[] = {', '};');
const layoutsBlock = extractBlock(src, 'static const lgfxsb::PartLayout kLayouts[] = {', '};');
const profilesBlock = extractBlock(src, 'static const lgfxsb::ProfileDesc kProfiles[] = {', '};');
const assetsBlock = extractBlock(src, 'static const lgfxsb::AssetDesc kAssets[] = {', '};');
const projectBlock = extractBlock(src, 'static const lgfxsb::Project project = {', '};');

if (!partsBlock || !scenesBlock || !layoutsBlock || !profilesBlock || !projectBlock) {
  console.error('Could not find LGFXScreenBuilder descriptor tables in', file);
  process.exit(1);
}

const parts = parseParts(partsBlock);
const scenes = computeSceneCounts(parts, parseScenes(scenesBlock).map((s) => ({
  id: Number(s.id),
  name: s.name,
  start: Number(s.start),
  count: Number(s.count),
})));
const layouts = parseLayouts(layoutsBlock);
const profiles = parseProfiles(profilesBlock);
const assets = assetsBlock ? parseAssets(assetsBlock) : [];
const project = parseProject(projectBlock);

const helpers = `
// LGFXSB_ESP32_PATCHED — factory helpers for ESP32 GCC (non-aggregate descriptors).

inline lgfxsb::PartDesc part(const char* id, lgfxsb::PartType type, int16_t assetIndex = -1) {
  lgfxsb::PartDesc p;
  p.id = id;
  p.type = type;
  p.assetIndex = assetIndex;
  return p;
}

inline lgfxsb::SceneDesc scene(lgfxsb::SceneId id, const char* name, uint16_t partStart, uint16_t partCount) {
  lgfxsb::SceneDesc s;
  s.id = id;
  s.name = name;
  s.partStart = partStart;
  s.partCount = partCount;
  return s;
}

inline lgfxsb::ProfileDesc profile(int16_t w, int16_t h, uint8_t rotation = 0) {
  lgfxsb::ProfileDesc p;
  p.w = w;
  p.h = h;
  p.rotation = rotation;
  return p;
}

inline lgfxsb::PartLayout layout(int16_t x, int16_t y, int16_t w, int16_t h, int16_t x2, int16_t y2,
                                 int16_t r, uint8_t datum, float size, uint32_t color,
                                 bool fill, bool visible, const void* font, const char* text) {
  lgfxsb::PartLayout l;
  l.x = x;
  l.y = y;
  l.w = w;
  l.h = h;
  l.x2 = x2;
  l.y2 = y2;
  l.r = r;
  l.datum = datum;
  l.size = size;
  l.color = color;
  l.fill = fill;
  l.visible = visible;
  l.font = font;
  l.text = text;
  return l;
}

inline lgfxsb::AssetDesc asset(const uint16_t* data, int16_t w, int16_t h) {
  lgfxsb::AssetDesc a;
  a.data = data;
  a.w = w;
  a.h = h;
  return a;
}

inline const lgfxsb::PartDesc* parts() {
  static const lgfxsb::PartDesc data[] = {
${parts.map((p) => `      part("${p.id}", lgfxsb::PartType::${p.type}, ${p.asset}),`).join('\n')}
  };
  return data;
}

inline const lgfxsb::SceneDesc* scenes() {
  static const lgfxsb::SceneDesc data[] = {
${scenes.map((s) => `      scene(${s.id}, "${s.name}", ${s.start}, ${s.count}),`).join('\n')}
  };
  return data;
}

inline const lgfxsb::PartLayout* layouts() {
  static const lgfxsb::PartLayout data[] = {
${layouts.map((l) => {
  const font = l.font === 'nullptr' ? 'nullptr' : l.font;
  const text = l.text === 'nullptr' ? 'nullptr' : l.text;
  const partId = partIdFromComment(l.comment);
  const visible = HOME_ALWAYS_VISIBLE_PARTS.has(partId) ? 'true' : l.visible;
  return `      layout(${l.x}, ${l.y}, ${l.w}, ${l.h}, ${l.x2}, ${l.y2}, ${l.r}, ${l.datum}, ${l.size}, ${l.color}, ${l.fill}, ${visible}, ${font}, ${text}),  // ${l.comment}`;
}).join('\n')}
  };
  return data;
}

inline const lgfxsb::ProfileDesc* profiles() {
  static const lgfxsb::ProfileDesc data[] = {
${profiles.map((p) => `      profile(${p.w}, ${p.h}, ${p.rot}),`).join('\n')}
  };
  return data;
}
`;

const assetsFn = assets.length
  ? `
inline const lgfxsb::AssetDesc* assets() {
  static const lgfxsb::AssetDesc data[] = {
${assets.map((a) => `      asset(kAsset_${a.id}, ${a.w}, ${a.h}),`).join('\n')}
  };
  return data;
}
`
  : '';

const projectFn = `
inline const lgfxsb::Project& project() {
  static const lgfxsb::Project data = []() {
    lgfxsb::Project p;
    p.profiles = profiles();
    p.profileCount = ${profiles.length};
    p.scenes = scenes();
    p.sceneCount = ${scenes.length};
    p.parts = parts();
    p.partCount = ${parts.length};
    p.layouts = layouts();
    p.background = ${project.background};
    ${project.hasAssets ? `p.assets = assets();\n    p.assetCount = ${assets.length};` : 'p.assets = nullptr;\n    p.assetCount = 0;'}
    return p;
  }();
  return data;
}
`;

const tailInsert = `${assetsFn}${projectFn}\n`;

let out = src;
out = out.replace(
  /static const lgfxsb::PartDesc kParts\[\] = \{[\s\S]*?\};\nstatic constexpr uint16_t kPartCount = \d+;\n\n/,
  '',
);
out = out.replace(/static const lgfxsb::SceneDesc kScenes\[\] = \{[\s\S]*?\};\n\n/, '');
out = out.replace(/static const lgfxsb::PartLayout kLayouts\[\] = \{[\s\S]*?\};\n\n/, '');
out = out.replace(/static const lgfxsb::ProfileDesc kProfiles\[\] = \{[\s\S]*?\};\n\n/, '');
out = out.replace(/static const lgfxsb::AssetDesc kAssets\[\] = \{[\s\S]*?\};\n/, '');
out = out.replace(/static const lgfxsb::Project project = \{[\s\S]*?\};\n\n/, '');

const detailInsert = `${helpers}\n`;
if (out.includes('namespace detail {\n\n')) {
  out = out.replace('namespace detail {\n\n', `namespace detail {\n${detailInsert}\n`);
} else {
  out = out.replace('namespace detail {\n', `namespace detail {\n${detailInsert}`, 1);
}
if (!out.includes('inline const lgfxsb::Project& project()')) {
  const marker = '} // namespace detail';
  const closeIdx = out.lastIndexOf(marker);
  if (closeIdx >= 0) {
    out = out.slice(0, closeIdx) + tailInsert + marker + out.slice(closeIdx + marker.length);
  }
}

out = out.replace(
  /explicit Screen\(lgfx::LGFX_Device& gfx\) : Base\(gfx, project\) \{\}/,
  'explicit Screen(lgfx::LGFX_Device& gfx) : Base(gfx, detail::project()) {}',
);

if (!out.includes('LGFXSB_ESP32_PATCHED')) {
  const sceneDefines = scenes.map((s) => `#define ROTARY_UI_HAS_SCENE_${s.name} 1`).join('\n');
  out = out.replace(
    '#pragma once\n',
    `#pragma once\n\n// LGFXSB_ESP32_PATCHED by scripts/patch-lgfxsb-export.mjs\n#define ROTARY_UI_PART_COUNT ${parts.length}\n#define ROTARY_UI_PROFILE_COUNT ${profiles.length}\n${sceneDefines}\n`,
  );
}

if (swapAssets && out.includes('kAsset_')) {
  out = markAssetSwap(swapAssetArrays(out));
}

out = finalizeHeader(out, parts, scenes);

fs.writeFileSync(file, out);
console.log(`Patched ${file} (${parts.length} parts, ${profiles.length} profiles, ${layouts.length} layouts${out.includes('void showHome(') ? ', showHome()' : ''}${swapAssets && out.includes('kAsset_') ? ', assets byte-swapped' : ''})`);
