// Runs the simulator's own JavaScript (simulator/index.html) headless, as the reference for the firmware.
// Same command line and output format as the PC build of the firmware (host/host_main.c):
//   node simref.js show <k> <seedCounter> <frames> <out.bin>
//   node simref.js run <rngSeed> <frames> <clock> <events> <out.bin>
// Math.random() is replaced by the firmware's xorshift32 stream, so both draw the same numbers.
"use strict";
const fs = require("fs");
const path = require("path");
const vm = require("vm");

const SIM = path.join(__dirname, "..", "..", "..", "simulator");
const html = fs.readFileSync(path.join(SIM, "index.html"), "utf8");
const body = html.slice(html.lastIndexOf("<script>") + 8, html.lastIndexOf("</script>"));
const MAIN = "/* ---------- main loop ---------- */";
if (!body.includes(MAIN)) throw new Error("simulator layout changed: main loop marker not found");
const logic = body.slice(0, body.indexOf(MAIN));

// A do-nothing DOM: every property is another stub, every call returns a stub.
const stub = new Proxy(function () {}, {
  get: (t, p) => (p === Symbol.toPrimitive ? () => 0 : p === "matches" ? false : stub),
  apply: () => stub, construct: () => stub, set: () => true,
});
let rng = 1;
const random = () => { rng ^= rng << 13; rng >>>= 0; rng ^= rng >>> 17; rng ^= rng << 5; rng >>>= 0; return rng / 4294967296; };
const mathR = Object.create(Math); mathR.random = random;
const ctx = { window: {}, document: stub, ResizeObserver: stub, requestAnimationFrame: () => 0, performance: { now: () => 0 },
              localStorage: { getItem: () => null, setItem: () => {} }, console, Math: mathR };
ctx.window.matchMedia = () => ({ matches: false });
ctx.window.devicePixelRatio = 1;
ctx.window.addEventListener = () => {};
vm.createContext(ctx);
vm.runInContext(fs.readFileSync(path.join(SIM, "led_map.js"), "utf8"), ctx);
const [mode, ...args] = process.argv.slice(2);
if (mode === "run") rng = (Number(args[0]) >>> 0) || 1;          // seeded before the script draws brain.favorite
vm.runInContext(logic + `
globalThis.__sim = { ANIMS, instance, stepInst, brain, touch, touchTick, engineTick, SCRIPTS, mix, RATE, N,
                     setSeedCounter: c => { seedCounter = c; } };`, ctx);
const S = ctx.__sim;
const q12 = v => Math.max(0, Math.min(65535, Math.round(Math.max(0, v) * 4096)));

if (mode === "show") {
  const [k, c, frames, out] = [Number(args[0]), Number(args[1]), Number(args[2]), args[3]];
  S.setSeedCounter(c);
  const inst = S.instance(k);
  const buf = Buffer.alloc(frames * S.N * 2);
  for (let n = 0; n < frames; n++) {
    S.stepInst(inst);
    for (let i = 0; i < S.N; i++) buf.writeUInt16LE(q12(inst.out[i]), (n * S.N + i) * 2);
  }
  fs.writeFileSync(out, buf);
} else if (mode === "run") {
  const [frames, clock, events, out] = [Number(args[1]), Number(args[2]), args[3], args[4]];
  const STATES = ["morning", "work", "daydream", "doze", "sleep", "stir", "grumpy", "excited", "reminded", "react"];
  const EMO = ["energy", "focus", "mood", "annoy", "excite", "bond"];
  const ev = {};
  for (const e of (events || "").split(",").filter(Boolean)) { const [f, g] = e.split(":"); (ev[+f] = ev[+f] || []).push(g); }
  S.brain.clock = clock;
  S.brain.face("morning", "morning");
  const rec = S.N * 2 + 1 + 48;
  const buf = Buffer.alloc(frames * rec);
  for (let n = 0; n < frames; n++) {
    for (const g of ev[n] || []) {
      if (g === "dark") S.brain.setAmbient(false);
      else if (g === "light") S.brain.setAmbient(true);
      else { S.touch.script = S.SCRIPTS[g]; S.touch.scriptT = S.touch.now; }
    }
    S.touchTick(S.RATE);
    S.brain.tick(S.RATE);
    S.engineTick(S.RATE, S.mix);
    let o = n * rec;
    for (let i = 0; i < S.N; i++) { buf.writeUInt16LE(Math.min(4096, q12(S.mix[i])), o); o += 2; }
    buf.writeUInt8(STATES.indexOf(S.brain.state), o); o += 1;
    for (const k of EMO) { buf.writeDoubleLE(S.brain.E[k], o); o += 8; }
  }
  fs.writeFileSync(out, buf);
} else {
  console.error("usage: see the top of test/simref.js");
  process.exit(1);
}
