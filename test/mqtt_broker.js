// Minimal MQTT 3.1.1 broker -- just enough to accept the board's connection
// and print what it publishes.  Bench tool: no auth, no QoS>0, no retention.
const net = require('net');

const TYPE = { CONNECT: 1, CONNACK: 2, PUBLISH: 3, SUBSCRIBE: 8, PINGREQ: 12, DISCONNECT: 14 };

// Remaining Length is a 1-4 byte varint, 7 bits each, high bit = continue.
function varint(buf, off) {
  let mult = 1, val = 0, b;
  do {
    if (off >= buf.length) return null;
    b = buf[off++];
    val += (b & 0x7f) * mult;
    mult *= 128;
  } while (b & 0x80);
  return { val, off };
}

const str = (b, o) => {
  const n = b.readUInt16BE(o);
  return { s: b.slice(o + 2, o + 2 + n).toString('utf8'), off: o + 2 + n };
};

let count = 0;
net.createServer(sock => {
  const who = sock.remoteAddress.replace('::ffff:', '');
  console.log(`\n[+] ${who} connected`);
  let acc = Buffer.alloc(0);

  sock.on('data', chunk => {
    acc = Buffer.concat([acc, chunk]);
    for (;;) {
      if (acc.length < 2) return;
      const type = acc[0] >> 4;
      const rl = varint(acc, 1);
      if (!rl) return;
      const total = rl.off + rl.val;
      if (acc.length < total) return;             // wait for the rest
      const body = acc.slice(rl.off, total);
      acc = acc.slice(total);

      if (type === TYPE.CONNECT) {
        // protocol name, level, flags, keepalive, then client id
        let o = 0;
        const proto = str(body, o); o = proto.off;
        const level = body[o++], flags = body[o++];
        o += 2;                                    // keepalive
        const cid = str(body, o); o = cid.off;
        let will = null, user = null;
        if (flags & 0x04) {
          const wt = str(body, o); o = wt.off;
          const wm = str(body, o); o = wm.off;
          will = `${wt.s} = ${wm.s}`;
        }
        if (flags & 0x80) { const u = str(body, o); o = u.off; user = u.s; }
        console.log(`    CONNECT proto=${proto.s}/${level} client=${cid.s}`);
        console.log(`    username=${user === null ? '(none)' : JSON.stringify(user)}`);
        if (will) console.log(`    will:    ${will}`);
        sock.write(Buffer.from([0x20, 0x02, 0x00, 0x00]));   // CONNACK accepted
      } else if (type === TYPE.PUBLISH) {
        const t = str(body, 0);
        const payload = body.slice(t.off).toString('utf8');
        count++;
        console.log(`\n--- publish #${count} -> ${t.s}   (${payload.length} B)`);
        console.log(payload);
        try {
          const o = JSON.parse(payload);
          console.log(`    ${Object.keys(o).length} keys: ${Object.keys(o).join(' ')}`);
        } catch (e) { console.log(`    !! NOT VALID JSON: ${e.message}`); }
      } else if (type === TYPE.PINGREQ) {
        sock.write(Buffer.from([0xd0, 0x00]));
      } else if (type === TYPE.DISCONNECT) {
        console.log('    DISCONNECT');
      }
    }
  });
  sock.on('error', e => console.log(`[!] ${who}: ${e.message}`));
  sock.on('close', () => console.log(`[-] ${who} gone`));
}).listen(1883, '0.0.0.0', () => console.log('broker listening on 1883'));
