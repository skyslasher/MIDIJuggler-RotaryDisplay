#!/usr/bin/env node
/**
 * Quick sanity check for a patched include/RotaryUi.h before flashing.
 * Usage: node scripts/verify-rotary-ui.mjs include/RotaryUi.h
 */

import fs from 'node:fs';

const file = process.argv[2];
if (!file) {
  console.error('Usage: node scripts/verify-rotary-ui.mjs include/RotaryUi.h');
  process.exit(1);
}

const text = fs.readFileSync(file, 'utf8');
let ok = true;

function fail(msg) {
  console.error(`FAIL: ${msg}`);
  ok = false;
}

function pass(msg) {
  console.log(`OK: ${msg}`);
}

if (!text.includes('klickBgInactive')) {
  fail('Home chip part klickBgInactive fehlt');
} else {
  pass('klickBgInactive vorhanden');
}

if (!text.includes('ROTARY_UI_HOME_DYNAMIC')) {
  fail('ROTARY_UI_HOME_DYNAMIC fehlt — patch erneut ausführen');
} else {
  pass('ROTARY_UI_HOME_DYNAMIC gesetzt');
}

const partCount = text.match(/#define ROTARY_UI_HOME_PART_COUNT (\d+)/);
if (!partCount) {
  fail('ROTARY_UI_HOME_PART_COUNT fehlt');
} else if (Number(partCount[1]) < 14) {
  fail(`ROTARY_UI_HOME_PART_COUNT ist ${partCount[1]}, erwartet 14`);
} else {
  pass(`ROTARY_UI_HOME_PART_COUNT = ${partCount[1]}`);
}

const homeScene = text.match(/scene\(\d+, "Home", (\d+), (\d+)\)/);
if (!homeScene) {
  fail('Home-Szene in scenes() nicht gefunden');
} else if (Number(homeScene[2]) < 14) {
  fail(`Home-Szene hat nur ${homeScene[2]} Parts (erwartet 14) — patch erneut ausführen`);
} else {
  pass(`Home-Szene: start=${homeScene[1]}, count=${homeScene[2]}`);
}

if (!text.includes('renderHomeScene')) {
  fail('renderHomeScene() fehlt in Screen — patch erneut ausführen');
} else {
  pass('renderHomeScene() vorhanden');
}

const projectPartCount = text.match(/p\.partCount = (\d+);/);
const partEntries = (text.match(/part\("/g) || []).length;
if (projectPartCount && Number(projectPartCount[1]) !== partEntries) {
  fail(`p.partCount=${projectPartCount[1]} aber ${partEntries} parts() Einträge`);
} else if (projectPartCount) {
  pass(`p.partCount = ${projectPartCount[1]}`);
}

const profileCount = text.match(/p\.profileCount = (\d+);/);
const profileEntries = (text.match(/profile\(\d+/g) || []).length;
const layoutEntries = (text.match(/^\s+layout\(/gm) || []).length;
if (profileCount && projectPartCount) {
  const expectedLayouts = Number(profileCount[1]) * Number(projectPartCount[1]);
  if (layoutEntries !== expectedLayouts) {
    fail(`layouts(): ${layoutEntries} Einträge, erwartet ${expectedLayouts} (${profileCount[1]} profiles × ${projectPartCount[1]} parts)`);
  } else {
    pass(`layouts(): ${layoutEntries} (= ${profileCount[1]} × ${projectPartCount[1]})`);
  }
}
if (profileCount && Number(profileCount[1]) !== profileEntries) {
  fail(`p.profileCount=${profileCount[1]} aber ${profileEntries} profiles() Einträge`);
}

if (text.includes('void setOverlay(void (*fn)(Canvas&, const Scene::Home&))')) {
  fail('setOverlay(Home) noch vorhanden — patch erneut ausführen (stripHomeOverlay)');
} else {
  pass('Home-Overlay-Hook entfernt');
}

const accentLayouts = [...text.matchAll(/layout\([^\n]*0x([0-9a-f]{6})[^\n]*bpmAccent/gi)];
if (accentLayouts.length === 0) {
  fail('bpmAccent Layout-Zeile nicht gefunden');
} else {
  for (const m of accentLayouts) {
    console.log(`INFO: bpmAccent Farbe in Layout: 0x${m[1]}`);
  }
}

if (text.includes('static const lgfxsb::PartDesc kParts[]')) {
  console.log('INFO: Roh-Export (kParts[]) — vollständiger Patch wird Layouts aktualisieren.');
} else if (text.includes('LGFXSB_ESP32_PATCHED')) {
  console.log('HINWEIS: Bereits gepatchte Datei. Farbänderungen aus LGFXScreenBuilder erfordern einen frischen Export (kParts[]), dann erneut patchen.');
}

process.exit(ok ? 0 : 1);
