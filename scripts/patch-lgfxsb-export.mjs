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
 */

import fs from 'node:fs';
import path from 'node:path';

const file = process.argv[2];
if (!file) {
  console.error('Usage: node scripts/patch-lgfxsb-export.mjs <exported.h>');
  process.exit(1);
}

const src = fs.readFileSync(file, 'utf8');
if (src.includes('LGFXSB_ESP32_PATCHED')) {
  console.log(`${file}: already patched`);
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

const partsBlock = extractBlock(src, 'static const lgfxsb::PartDesc kParts[] = {', '};');
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
const scenes = parseScenes(scenesBlock);
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
  return `      layout(${l.x}, ${l.y}, ${l.w}, ${l.h}, ${l.x2}, ${l.y2}, ${l.r}, ${l.datum}, ${l.size}, ${l.color}, ${l.fill}, ${l.visible}, ${font}, ${text}),  // ${l.comment}`;
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

const detailInsert = `${helpers}${assetsFn}${projectFn}\n`;
out = out.replace('namespace detail {\n\n', `namespace detail {\n${detailInsert}`);

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

fs.writeFileSync(file, out);
console.log(`Patched ${file} (${parts.length} parts, ${profiles.length} profiles, ${layouts.length} layouts)`);
