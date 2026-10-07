// A WebSocket relay for the classroom self-test's C++ tool (classroom_selftest --relay, which
// starts it): the core's socket hooks (Host::socketOpen / socketSend / socketClose,
// CLASSROOM.md 3.14) come in on stdin and what happens to each socket goes out on stdout, one
// line per event, while node's own WebSocket talks to the server. For the test only -- the
// apps use their platform's sockets (mac/App/ClassroomHooks.swift on the Mac) -- so that the
// portable core's live connection runs against the real Worker under wrangler dev.
//
//   in:  O <id> {"url":"ws://...","headers":{...}}     open
//        S <id> "<text>"                               send (a JSON string)
//        X <id> <code>                                 close (nothing more is said about it)
//   out: O <id>                                        opened
//        T <id> "<text>"                               a text frame (a JSON string)
//        C <id> <code>                                 closed (1006 when there was no code)

import readline from "node:readline";

const sockets = new Map();
const out = (line) => process.stdout.write(line + "\n");

readline.createInterface({ input: process.stdin }).on("line", (line) => {
  const sp1 = line.indexOf(" ");
  const sp2 = line.indexOf(" ", sp1 + 1);
  const cmd = line.slice(0, sp1);
  const id = Number(sp2 < 0 ? line.slice(sp1 + 1) : line.slice(sp1 + 1, sp2));
  const arg = sp2 < 0 ? "" : line.slice(sp2 + 1);
  if (cmd === "O") {
    let ws;
    try {
      const { url, headers } = JSON.parse(arg);
      ws = new WebSocket(url, { headers });
    } catch {
      out(`C ${id} 1006`);
      return;
    }
    sockets.set(id, ws);
    ws.onopen = () => { if (sockets.get(id) === ws) out(`O ${id}`); };
    ws.onmessage = (e) => { if (sockets.get(id) === ws) out(`T ${id} ${JSON.stringify(String(e.data))}`); };
    ws.onerror = () => {};
    ws.onclose = (e) => { if (sockets.get(id) === ws) { sockets.delete(id); out(`C ${id} ${e.code || 1006}`); } };
  } else if (cmd === "S") {
    const ws = sockets.get(id);
    if (ws && ws.readyState === 1) ws.send(JSON.parse(arg));
  } else if (cmd === "X") {
    const ws = sockets.get(id);
    sockets.delete(id);
    if (ws) { try { ws.close(1000); } catch { /* closed */ } }
  }
});
process.stdin.on("end", () => {
  for (const ws of sockets.values()) { try { ws.close(1000); } catch { /* closed */ } }
  process.exit(0);
});
