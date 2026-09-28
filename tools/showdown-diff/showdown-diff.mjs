// Diff the engine's JSON catalog against Pokemon Showdown's data (@pkmn/dex).
// Usage: node showdown-diff.mjs <battle-engine dir>
// Reads JSON the way data_loader.cpp does, so a loader quirk shows up as a diff.
import { Dex } from '@pkmn/dex';
import fs from 'node:fs';
import path from 'node:path';

const root = path.resolve(process.argv[2] ?? '.');
const files = [];
(function walk(dir) {
  for (const e of fs.readdirSync(dir, { withFileTypes: true })) {
    if (['build', 'node_modules', '.git'].includes(e.name)) continue;
    const p = path.join(dir, e.name);
    if (e.isDirectory()) walk(p);
    else if (e.name.endsWith('.json') || e.name === 'data_loader.cpp') files.push(p);
  }
})(root);

// Mirror the StatChange target rule actually compiled into the engine.
const loaderSrc = files.filter((f) => f.endsWith('data_loader.cpp')).map((f) => fs.readFileSync(f, 'utf8')).join('');
const oldLoader = loaderSrc.includes('j.value("target", std::string("user")) == "user"');
if (oldLoader) console.log('! data_loader.cpp: StatChange without "target" hits the USER (old rule)\n');

const moves = [], species = [];
for (const f of files.filter((f) => f.endsWith('.json'))) {
  let j;
  try { j = JSON.parse(fs.readFileSync(f, 'utf8')); } catch { continue; }
  if (j?.effects) moves.push(j);
  else if (j?.movepool) species.push(j);
}
const used = new Set(species.flatMap((s) => s.movepool));

const STATUS = { Burn: 'brn', Paralysis: 'par', Poison: 'psn', Toxic: 'tox', Sleep: 'slp', Freeze: 'frz' };
const pct = (n, d) => Math.round((100 * n) / d);
const low = (s) => String(s).toLowerCase().replace(/[^a-z0-9]/g, '');
const WEATHER = { raindance: 'rain', sunnyday: 'sun', sandstorm: 'sand', hail: 'snow', snowscape: 'snow' };
const weather = (w) => WEATHER[low(w)] ?? low(w);
// Showdown implements these in scripts (onHit), so its data can't be compared.
const SCRIPTED = new Set(['curse', 'tidyup', 'defog']);

function ourTags(m) {
  const t = new Set();
  for (const e of m.effects) {
    const c = e.chance ?? 100;
    switch (e.kind) {
      case 'StatChange': {
        const user = oldLoader
          ? (e.target ?? 'user') === 'user'
          : e.affectsUser === true || e.target === 'user';
        t.add(`boost:${user ? 'user' : 'target'}:${low(e.stat)}:${e.delta}@${c}`);
        break;
      }
      case 'ApplyStatus': t.add(`status:${STATUS[e.status] ?? e.status}@${c}`); break;
      case 'Flinch': t.add(`flinch@${c}`); break;
      case 'Recoil': t.add(`recoil:${pct(1, e.denominator)}`); break;
      case 'Drain': t.add(`drain:${pct(1, e.denominator ?? 2)}`); break;
      case 'Recovery': t.add(`heal:${pct(1, e.denominator)}`); break;
      case 'Pivot': t.add('pivot'); break;
      case 'ForceSwitch': t.add('forceswitch'); break;
      case 'SetHazard': t.add(`hazard:${low(e.hazard)}`); break;
      case 'SetWeather': t.add(`weather:${weather(e.weather)}`); break;
      case 'SetScreen': t.add('screen'); break;
      case 'Protect': t.add('protect'); break;
    }
  }
  const mh = m.multiHit;
  if (mh) t.add(`multihit:${mh.count ?? mh.powers?.length ?? `${mh.min}-${mh.max}`}`);
  if (m.twoTurn) t.add('twoturn');
  if (m.bypassesProtect) t.add('breaksprotect');
  if (m.highCrit) t.add('highcrit');
  return t;
}

function sdTags(x) {
  const t = new Set();
  const boosts = (b, who, c) => b && Object.entries(b).forEach(([s, d]) => t.add(`boost:${who}:${s}:${d}@${c}`));
  boosts(x.self?.boosts, 'user', 100);
  boosts(x.selfBoost?.boosts, 'user', 100);
  boosts(x.boosts, x.target === 'self' ? 'user' : 'target', 100);
  if (x.status) t.add(`status:${x.status}@100`);
  if (x.volatileStatus === 'confusion') t.add('confusion@100');
  for (const s of x.secondaries ?? []) {
    const c = s.chance ?? 100;
    boosts(s.boosts, 'target', c);
    boosts(s.self?.boosts, 'user', c);
    if (s.status) t.add(`status:${s.status}@${c}`);
    if (s.volatileStatus === 'flinch') t.add(`flinch@${c}`);
    else if (s.volatileStatus) t.add(`${s.volatileStatus}@${c}`);
  }
  if (x.recoil) t.add(`recoil:${pct(...x.recoil)}`);
  if (x.drain) t.add(`drain:${pct(...x.drain)}`);
  if (x.heal) t.add(`heal:${pct(...x.heal)}`);
  if (x.multihit) t.add(`multihit:${Array.isArray(x.multihit) ? x.multihit.join('-') : x.multihit}`);
  if (x.selfSwitch) t.add('pivot');
  if (x.forceSwitch) t.add('forceswitch');
  if (['stealthrock', 'spikes', 'toxicspikes'].includes(x.sideCondition)) t.add(`hazard:${x.sideCondition}`);
  if (['reflect', 'lightscreen', 'auroraveil'].includes(x.sideCondition)) t.add('screen');
  if (x.weather) t.add(`weather:${weather(x.weather)}`);
  if (x.stallingMove) t.add('protect');
  if (x.flags.charge) t.add('twoturn');
  const fieldTarget = ['self', 'allySide', 'foeSide', 'all', 'allies'].includes(x.target);
  if (x.breaksProtect || (!x.flags.protect && !fieldTarget && !SCRIPTED.has(x.id))) t.add('breaksprotect');
  if ((x.critRatio ?? 1) >= 2) t.add('highcrit');
  return t;
}

let issues = 0;
const report = (name, lines, note = '') => {
  if (!lines.length) return;
  issues += lines.length;
  console.log(`${name}${note}`);
  for (const l of lines) console.log(`   ${l}`);
};

console.log(`== Moves (${moves.length})`);
for (const m of moves.sort((a, b) => a.name.localeCompare(b.name))) {
  const x = Dex.moves.get(m.name);
  if (!x.exists) { report(m.name, ['not found in Showdown']); continue; }
  const d = [];
  const cmp = (k, ours, theirs) => ours !== theirs && d.push(`${k}: engine=${ours} showdown=${theirs}`);
  cmp('type', m.type, x.type);
  cmp('category', m.category, x.category);
  cmp('power', m.power ?? 0, x.basePower);
  if (!m.selfOrField) cmp('accuracy', m.accuracy ?? 0, x.accuracy === true ? 0 : x.accuracy);
  cmp('pp', m.pp, x.pp);
  cmp('priority', m.priority ?? 0, x.priority);
  cmp('contact', !!m.contact, !!x.flags.contact);
  cmp('punch', !!m.punch, !!x.flags.punch);
  cmp('slicing', !!m.slicing, !!x.flags.slicing);
  cmp('bulletproof', !!m.bulletproof, !!x.flags.bullet);
  cmp('reflectable', !!m.reflectable, !!x.flags.reflectable);
  // data_loader.cpp: blockedByProtect = !selfOrField (it also drives Pressure).
  cmp('stoppedByProtect', !m.selfOrField && !m.bypassesProtect, !!x.flags.protect && !x.breaksProtect);
  const a = ourTags(m), b = sdTags(x);
  for (const k of a)
    if (!b.has(k) && !(SCRIPTED.has(x.id) && k.startsWith('boost:'))) d.push(`- engine only:   ${k}`);
  for (const k of b) if (!a.has(k)) d.push(`+ showdown only: ${k}`);
  report(m.name, d, used.has(m.name) ? '' : '  (not in any movepool)');
}

const STAT = { hp: 'hp', atk: 'atk', def: 'def', specAtk: 'spa', specDef: 'spd', speed: 'spe' };
console.log(`\n== Species (${species.length})`);
for (const s of species.sort((a, b) => a.id.localeCompare(b.id))) {
  let x = Dex.species.get(s.id);
  const mega = /^Mega(.+?)([XY])?$/.exec(s.id);
  if (!x.exists && mega) x = Dex.species.get(`${mega[1]}-Mega${mega[2] ? `-${mega[2]}` : ''}`);
  if (!x.exists) x = Dex.species.get(s.displayName);
  if (!x.exists) { report(s.id, ['not found in Showdown']); continue; }
  const d = [];
  for (const [k, v] of Object.entries(s.baseStats))
    if (v !== x.baseStats[STAT[k]]) d.push(`${k}: engine=${v} showdown=${x.baseStats[STAT[k]]}`);
  const ours = [s.type1, s.type2].filter(Boolean).join('/');
  if (ours !== x.types.join('/')) d.push(`types: engine=${ours} showdown=${x.types.join('/')}`);
  const abil = Object.values(x.abilities);
  if (!abil.some((a) => low(a) === low(s.ability ?? ''))) d.push(`ability: engine=${s.ability || '<empty>'} showdown=${abil.join('|')}`);
  if (Math.abs(s.weight - x.weightkg) > 0.05) d.push(`weight: engine=${s.weight} showdown=${x.weightkg}`);
  const legend = x.tags.some((t) => /Legendary|Mythical/.test(t));
  if (!!s.legendary !== legend) d.push(`legendary: engine=${!!s.legendary} showdown=${legend} (${x.tags.join(', ')})`);
  report(s.id, d, `  [${x.name}]`);
}

console.log(`\n${issues} difference(s).`);
