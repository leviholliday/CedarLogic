#!/usr/bin/env node
// The phone's side of adding a device by scanning (SYNC.md 11.7), for the Mac's
// pairing check (mac/Tools/sync-check.sh): reads the QR code's link and the
// phone's sync code from files the check writes, opens the hello, and answers.
// Written against the spec on its own (node's crypto), not the engine's code.
//
//   node sync-pair-answer.mjs <http://localhost:PORT/api/sync/v1> <dir> "<this phone's name>"
//
// Prints "L: hello from <device>" and "L: answered" (exit 0), or why not (exit 1).
import crypto from "node:crypto";
import fs from "node:fs";
import path from "node:path";

const [, , base, dir, phoneName = "Sam’s phone"] = process.argv;
if (!base || !dir) { console.error("usage: sync-pair-answer.mjs <sync url> <dir> [name]"); process.exit(2); }

const ALPHABET = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
const b64u = (buf) => Buffer.from(buf).toString("base64url");
const hkdf = (ikm, info, n) => Buffer.from(crypto.hkdfSync("sha256", ikm, "cedarlogic-pair-v1", info, n));

function decodePairing(text) {
  const i = text.indexOf("#p=");
  if (i < 0) throw new Error("not a pairing link");
  const sym = text.slice(i + 3).toUpperCase().replace(/[^0-9A-Z]/g, "").replace(/O/g, "0").replace(/[IL]/g, "1");
  if (sym.length !== 28) throw new Error("a pairing code is 28 symbols");
  let bits = "";
  for (const ch of sym) bits += ALPHABET.indexOf(ch).toString(2).padStart(5, "0");
  return Buffer.from(bits.slice(0, 128).match(/.{8}/g).map((b) => parseInt(b, 2)));
}

function open(key, pairId, kind, env) {
  const raw = Buffer.from(env, "base64url");
  if (raw.length < 29 || raw[0] !== 1) throw new Error("damaged");
  const d = crypto.createDecipheriv("aes-256-gcm", key, raw.subarray(1, 13));
  d.setAAD(Buffer.from(`cedarlogic-pair-v1|${kind}|${pairId}`));
  d.setAuthTag(raw.subarray(raw.length - 16));
  const msg = JSON.parse(Buffer.concat([d.update(raw.subarray(13, raw.length - 16)), d.final()]).toString("utf8"));
  if (msg.v !== 1 || msg.kind !== kind || typeof msg.device !== "string") throw new Error("damaged");
  return msg;
}

function seal(key, pairId, kind, json) {
  const nonce = crypto.randomBytes(12);
  const c = crypto.createCipheriv("aes-256-gcm", key, nonce);
  c.setAAD(Buffer.from(`cedarlogic-pair-v1|${kind}|${pairId}`));
  const ct = Buffer.concat([c.update(Buffer.from(json, "utf8")), c.final()]);
  return b64u(Buffer.concat([Buffer.from([1]), nonce, ct, c.getAuthTag()]));
}

async function waitFor(file, ms) {
  const end = Date.now() + ms;
  while (Date.now() < end) {
    try { const t = fs.readFileSync(file, "utf8").trim(); if (t) return t; } catch {}
    await new Promise((r) => setTimeout(r, 100));
  }
  throw new Error(`${path.basename(file)} never appeared`);
}

try {
  const link = await waitFor(path.join(dir, "pair-link.txt"), 60000);
  const code = await waitFor(path.join(dir, "pair-code.txt"), 5000);
  const P = decodePairing(link);
  const pairId = hkdf(P, "pair-id", 16).toString("hex");
  const key = hkdf(P, "pair-key", 32);
  let r = await fetch(`${base}/pair/${pairId}`);
  if (r.status !== 200) throw new Error(`GET /pair answered ${r.status}`);
  const got = await r.json();
  const hello = open(key, pairId, "hello", got.hello);
  console.log(`L: hello from ${hello.device}`);
  const answer = seal(key, pairId, "answer", JSON.stringify({ v: 1, kind: "answer", code, device: phoneName }));
  r = await fetch(`${base}/pair/${pairId}/answer`, { method: "POST", headers: { "content-type": "application/json" }, body: JSON.stringify({ answer }) });
  if (r.status !== 200) throw new Error(`POST answer answered ${r.status}`);
  console.log("L: answered");
} catch (e) {
  console.error("L: " + e.message);
  process.exit(1);
}
