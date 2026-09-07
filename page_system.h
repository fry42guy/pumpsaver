#pragma once
#include "ui_common.h"

/*
  Screen 4 -- System.  Everything that is about the box rather than the pump:
  the PLC/CAN gateway, backup and restore, the drive commissioning guide, and
  what firmware is actually running.

  The drive setup guide is kept verbatim from 0.8.1 because it is field
  knowledge, not decoration -- it is the difference between a commissioning
  visit and a phone call.  It is the one place on the UI where long prose
  earns its space, so it lives behind collapsed <details> rather than being
  deleted.
*/

const char PAGE_SYSTEM[] PROGMEM = R"HTML(<!doctype html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<title>PumpSaver &middot; System</title><style>)HTML" CSS_BASE R"HTML(
.trace{background:#101820;color:#8fe0a8;padding:10px;border-radius:10px;font-size:11px;
  line-height:1.4;max-height:280px;overflow:auto;white-space:pre;border:0}
details table{margin-top:7px}
details .body{font-size:12.5px;color:var(--mut);line-height:1.6;margin:7px 0 2px}
details .body b{color:var(--ink)}
</style></head><body>)HTML" NAV_HTML R"HTML(
<main>

<section><h2>Controller</h2><div class="kv" id="sysInfo"></div></section>

<section><h2>Simulation</h2>
<div class="alert ok" id="simst" style="margin-bottom:11px">&hellip;</div>
<div class="fld"><span class="k">Simulate the plant<i>runs the real control logic against a modelled pump and tank</i></span>
<label class="sw"><input type="checkbox" id="simOn"><span></span></label></div>
<div class="fld"><span class="k">Pumps on the skid</span>
<select id="simDrives"><option value="1">1 &mdash; lead only</option>
<option value="2">2 &mdash; lead + lag</option></select></div>
<p class="note">The control block, the sleep state machine and staging all run
exactly as they do on real hardware &mdash; only the pressure and the drives are
modelled. With two pumps you can watch the speeds stay matched through a stage
up and down; with one, staging is correctly never offered a lag pump.<br><br>
Set the header pressure and the demand on <a href="/pump">Pump</a>, and watch the
operating point move on the envelope plot there.</p>
<p class="note"><b>Turning simulation on stops the pumps</b> and writes stop / 0 Hz
to any real drive on the bus first, so a drive is never left running against
invented feedback, and never left to trip on its own comms watchdog. Turning it
off rescans the bus.</p>
</section>

<section><h2>PLC gateway (CAN)</h2>
<div class="alert ok" id="cst" style="margin-bottom:11px">&hellip;</div>
<div class="fld"><span class="k">Gateway on<i>answer the skid PLC as if this were the drive</i></span>
<label class="sw"><input type="checkbox" id="enable"><span></span></label></div>
<div class="fld"><span class="k">Listen only<i>transmit nothing; capture a live bus</i></span>
<label class="sw"><input type="checkbox" id="listenOnly"><span></span></label></div>
<div class="fld"><span class="k">Enforce P-01 / P-02<i>clamp the PLC speed reference here</i></span>
<label class="sw"><input type="checkbox" id="capEnforce"><span></span></label></div>
<div class="fld"><span class="k">CAN node ID</span><input type="number" id="node" min="1" max="127" inputmode="numeric"></div>
<div class="fld"><span class="k">Drive address<i>0 = first found</i></span><input type="number" id="driveAddr" min="0" max="247" inputmode="numeric"></div>
<div class="fld"><span class="k">Heartbeat<i>ms</i></span><input type="number" id="hbMs" min="50" max="3000" step="10" inputmode="numeric"></div>
<div class="fld"><span class="k">TX PDO<i>ms</i></span><input type="number" id="pdoMs" min="20" max="1000" step="10" inputmode="numeric"></div>
<h3>Identity &mdash; read these off a real bus in listen mode</h3>
<div class="fld"><span class="k">Vendor 0x1018:1</span><input type="number" id="vendor" inputmode="numeric"></div>
<div class="fld"><span class="k">Product 0x1018:2</span><input type="number" id="product" inputmode="numeric"></div>
<div class="fld"><span class="k">Revision 0x1018:3</span><input type="number" id="revision" inputmode="numeric"></div>
<div class="fld"><span class="k">Serial 0x1018:4</span><input type="number" id="serial" inputmode="numeric"></div>
<div class="fld"><span class="k">Device type 0x1000</span><input type="number" id="devType" inputmode="numeric"></div>
<div class="row" style="margin-top:11px">
<button type="button" onclick="saveCan()">Save gateway</button>
<button type="button" class="ghost sm" onclick="canTrace()">Frame log</button></div>
<p class="note">Bus settings apply on reboot. Listen mode transmits nothing &mdash; put the
board on a working PLC + drive bus, power-cycle the PLC, and the log captures the
CANopen Manager's startup and the drive's real identity.</p>
<pre id="ctr" class="trace" style="display:none"></pre>
</section>

<section><h2>Backup &amp; restore</h2>
<div class="row"><button class="ghost" onclick="location.href='/export'">Download config</button></div>
<p class="note">Named JSON, not the NVS blob, so a file written today still imports into
later firmware. <b>Passwords are never exported</b> &mdash; SSID, broker and topic come
across, the two secrets get typed in on the board.</p>
<textarea id="cfgTa" placeholder="Paste a saved config here, then Import" style="margin-top:9px"></textarea>
<div class="fld"><span class="k">Also apply network<i>SSID, broker, topic</i></span>
<label class="sw"><input type="checkbox" id="impNet"><span></span></label></div>
<div class="row"><button class="ghost" onclick="doImport()">Import config</button></div>
<pre id="log" style="display:none"></pre>
</section>

<section><h2>Drive setup (E3 keypad)</h2>
<p class="note" style="margin-top:0">What a drive needs before it will answer this board.
Once per drive, from the front keypad, motor stopped. Unlock with <b>P-14 = 101</b> first
&mdash; nothing past P-14 is visible until you do.</p>

<details><summary>1 &middot; Factory reset &mdash; the clean start</summary>
<div class="body">Drive stopped. Hold <b>UP + DOWN + STOP</b> together about 2 s until the
display shows <b>P-dEF</b>, then press <b>STOP</b> to confirm. Everything returns to
default: Modbus address 1, 115.2 kbaud, watchdog off, terminal control (P-12 = 0). A
fresh drive from the box is already in this state. Motor data is wiped too, so re-enter
it in step 3.</div></details>

<details open><summary>2 &middot; Comms parameters &mdash; the ones that make it talk</summary>
<table><thead><tr><th>Param</th><th>Set to</th><th>Why</th></tr></thead><tbody>
<tr><td>P-14</td><td>101</td><td>Unlocks the extended parameters below</td></tr>
<tr><td>P-12</td><td>3</td><td>Modbus RTU control &mdash; run/stop and speed come from this board</td></tr>
<tr><td>P-36 &middot; 1st</td><td>2, then 3, 4, 5</td><td>Modbus address. <b>Never leave a drive at 1</b> &mdash; that is the uncommissioned slot</td></tr>
<tr><td>P-36 &middot; 2nd</td><td>115.2</td><td>kbaud &mdash; must match this board (it is the drive default)</td></tr>
<tr><td>P-36 &middot; 3rd</td><td>t 1000</td><td>Comms watchdog: the drive trips itself after 1 s of silence. Not optional &mdash; it is what stops the pump if this board dies</td></tr>
<tr><td>P-31</td><td>0</td><td>Start only on a command from this board; never auto-run at power-up</td></tr>
<tr><td>P-16</td><td>t 4-20</td><td>Transducer on analog input 1 as 4&ndash;20 mA, trip on a broken loop</td></tr>
</tbody></table>
<div class="body">P-36 is one parameter with three fields &mdash; the Navigate key steps
address &rarr; baud &rarr; watchdog. The hardware enable (terminal 1&ndash;2 link) must be
closed or the drive shows a stop regardless of what Modbus says. Set the address
<b>last</b>: once it changes the drive stops answering at 1 and appears under Drives on
the next rescan.</div></details>

<details><summary>3 &middot; Motor data &mdash; the overload protection</summary>
<div class="body">From the <b>motor nameplate</b>, not the drive rating: <b>P-07</b> rated
volts, <b>P-08</b> rated amps (a 5 HP drive left at its own rating will not protect a
5.9 A motor), <b>P-09</b> rated Hz, <b>P-10</b> rated rpm. Then P-01 max Hz if the skid
has a ceiling below 60.</div></details>

<details><summary>4 &middot; Wiring, and what each scan result means</summary>
<div class="body">Cat5e from the drive's front RJ45 to this board: pins <b>7 / 8</b> are
the Modbus A / B pair; +24 V and 0 V ride the same cable. Power the drive <b>first</b>
&mdash; it boots slower than this board, so a scan at the same instant finds nothing.
Then press <i>Rescan</i>.<br><br>
<b>no drives found</b> &mdash; nothing answered. Drive off or still booting, cable in the
wrong RJ45 (the RS-485 pair is the front port, not the PC port), P-12 not 3, or A/B open.<br>
<b>CRC errors, bus wiring suspect</b> &mdash; something is on the wire but frames are
garbled. Almost always A and B swapped; otherwise a baud mismatch in P-36, or termination
missing or doubled (120 &Omega; on this board's jumper, none at the drive).<br>
<b>uncommissioned drive at address 1</b> &mdash; wiring and baud are proven good. Set the
P-36 address to 2 and rescan.<br>
<b>drive shows SC-trP</b> &mdash; its comms watchdog tripped: it was talking, then heard
nothing for the P-36 timeout. Check this board is up, then <i>Fault reset</i>. A comms
trip shortly after power-up usually means the drive booted, got the watchdog armed, and
the master was not talking yet.<br>
<b>drive shows SC-F01</b> &mdash; observed on the bench 2026-09-07. Same family as
SC-trP; confirm the exact meaning against the E3 manual before relying on it. See the
open questions in DESIGN_NOTES.</div></details>
</section>

</main><script>)HTML" NAV_JS R"HTML(
const CANF=['node','driveAddr','hbMs','pdoMs','vendor','product','revision','serial','devType'];
const CANB=['enable','listenOnly','capEnforce'];

async function loadSys(){
  const j=await chrome();
  try{
    const s=await(await fetch('/sys')).json();
    $('sysInfo').innerHTML=
      '<div><span>firmware</span><b>v'+esc(s.ver)+'</b></div>'+
      '<div><span>unit</span><b>'+esc(s.id)+'</b></div>'+
      '<div><span>build</span><b>'+(s.sim?'SIM':'real drive')+'</b></div>'+
      '<div><span>uptime</span><b>'+hhmm(s.up)+'</b></div>'+
      '<div><span>last reset</span><b>'+esc(s.reset)+'</b></div>'+
      '<div><span>free heap</span><b>'+(s.heap/1024).toFixed(0)+' KB</b></div>'+
      '<div><span>free PSRAM</span><b>'+(s.psram/1048576).toFixed(1)+' MB</b></div>'+
      '<div><span>sketch</span><b>'+(s.sketch/1048576).toFixed(2)+' / '+
        (s.appSpace/1048576).toFixed(2)+' MB</b></div>'+
      '<div><span>flash chip</span><b>'+(s.flash/1048576).toFixed(0)+' MB</b></div>'+
      '<div><span>CPU</span><b>'+s.mhz+' MHz</b></div>';
  }catch(e){$('sysInfo').innerHTML='<div><span>controller</span><b class="no">no link</b></div>';}
}

// ---- CAN ------------------------------------------------------------------
// Every field the form posts is also read back from /can. Before 0.10.0 six of
// them were absent from that JSON, so they rendered empty and saving wrote
// "".toFloat() == 0 over them -- silently turning OFF P-01/P-02 enforcement
// and zeroing the identity. Fixed by returning the whole config.
let canDirty=false;
async function loadCan(){
  if(canDirty)return;
  const j=await(await fetch('/can')).json();
  for(const k of CANF)if(j[k]!==undefined)$(k).value=j[k];
  $('enable').checked=!!j.enable;
  $('listenOnly').checked=!!j.listen;
  $('capEnforce').checked=!!j.capEnforce;
  const on=j.up&&j.enable;
  let txt,cls='ok';
  if(!j.enable)             {txt='Gateway off';cls='ok';}
  else if(!j.up)            {txt='Gateway enabled but the CAN driver is down';cls='bad';}
  else if(j.listen)         {txt='Listen only — '+j.frames+' frames captured, transmitting nothing';cls='warn';}
  else                      {txt='Node '+j.node+' · '+j.nmt+' · '+j.frames+' frames · '+
                                 j.sdo+' SDO'+(j.aborts?' ('+j.aborts+' aborts)':'');
                             cls=(j.nmt=='operational'&&j.link)?'ok':'warn';}
  $('cst').textContent=txt;$('cst').className='alert '+cls;
}
for(const k of CANF.concat(CANB))
  $(k).addEventListener('input',()=>{canDirty=true;});

async function saveCan(){
  const b={};
  for(const k of CANF)b[k]=$(k).value;
  for(const k of CANB)b[k]=swVal($(k));
  toast(await post('/canset',b));
  canDirty=false;setTimeout(loadCan,700);
}
async function canTrace(){
  const el=$('ctr');
  if(el.style.display=='block'){el.style.display='none';return;}
  el.textContent=await(await fetch('/cantrace')).text();
  el.style.display='block';
}

// ---- backup ---------------------------------------------------------------
async function doImport(){
  const cfg=$('cfgTa').value.trim();
  if(!cfg){toast('Paste a config first.',1);return;}
  const t=await post('/import',{cfg:cfg,withNet:swVal($('impNet'))});
  $('log').style.display='block';$('log').textContent=t;toast('Import done');
}

// ---- simulation -----------------------------------------------------------
let simDirty=false;
async function loadSim(){
  if(simDirty)return;
  const s=await(await fetch('/settings')).json();
  $('simOn').checked=(+s.simOn)>0;
  $('simDrives').value=s.simDrives;
  const on=$('simOn').checked;
  $('simst').textContent=on
    ? 'Simulation ON — '+s.simDrives+' pump'+(s.simDrives>1?'s':'')+
      ', no drive is being driven'
    : 'Simulation off — running against real drives over Modbus';
  $('simst').className='alert '+(on?'warn':'ok');
}
async function saveSim(){
  simDirty=false;
  toast(await post('/sim',{on:swVal($('simOn')),drives:$('simDrives').value}));
  setTimeout(loadSim,600);
}
$('simOn').addEventListener('change',saveSim);
$('simDrives').addEventListener('change',saveSim);

loadSys();loadCan();loadSim();
setInterval(loadSys,5000);setInterval(loadCan,2000);setInterval(loadSim,3000);
</script></body></html>)HTML";
