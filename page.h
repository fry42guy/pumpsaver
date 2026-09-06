#pragma once

/*
  The web page lives here, not in the .ino, deliberately.

  Arduino generates C++ forward prototypes by scanning .ino files for things
  that look like function definitions -- and it does not understand raw string
  literals. A line reading "function drawDiag(d){" inside R"HTML(...)HTML"
  gets turned into a prototype at the top of the generated .cpp and the build
  dies with "function does not name a type". Arduino does not preprocess .h
  files, so the page is safe here however much JavaScript it grows.
*/

const char PAGE[] PROGMEM = R"HTML(<!doctype html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>PumpSaver</title>
<style>
body{font-family:system-ui,sans-serif;margin:0;background:#f4f5f2;color:#1a2230}
header{background:#1f6f8b;color:#fff;padding:14px 18px;font-weight:600;font-size:18px}
main{padding:14px;max-width:560px;margin:auto}
.sim{background:#8a1c1c;color:#fff;padding:10px 18px;font-weight:700;letter-spacing:.04em}
.tiles{display:grid;grid-template-columns:1fr 1fr 1fr;gap:8px}
.t{background:#fff;border-radius:6px;padding:9px 10px}
.t small{display:block;color:#666;font-size:10px;letter-spacing:.06em;text-transform:uppercase}
.t b{font-size:22px;font-variant-numeric:tabular-nums}
.st{margin:12px 0;padding:10px 12px;border-radius:6px;background:#e6efe9;font-size:14px}
.st.bad{background:#fbe6e0}
section{background:#fff;border-radius:6px;padding:12px;margin-top:12px}
h2{font-size:13px;margin:0 0 8px;color:#1f6f8b;text-transform:uppercase;letter-spacing:.06em}
label{display:grid;grid-template-columns:1fr 92px;align-items:center;gap:8px;margin:5px 0;font-size:14px}
input[type=number]{width:100%;padding:6px;font-size:15px;border:1px solid #bbb;border-radius:4px;box-sizing:border-box}
button{padding:10px 14px;border:0;border-radius:6px;background:#1f6f8b;color:#fff;font-size:15px;margin:4px 4px 0 0}
button.red{background:#b4531a}button.grey{background:#666}
table{width:100%;border-collapse:collapse;font-size:13px}
th,td{text-align:left;padding:4px 6px;border-bottom:1px solid #eee}
.cap{display:grid;grid-template-columns:28px 1fr 1fr;gap:6px;font-size:13px;align-items:center}
.sl{display:grid;grid-template-columns:96px 1fr 68px;gap:8px;align-items:center;font-size:14px;margin-top:8px}
.sl input[type=range]{width:100%;margin:0}
.sl input[type=number]{padding:4px}
.rng{display:flex;gap:5px;align-items:center;font-size:11px;color:#888;margin:2px 0 0 96px}
.rng input{width:56px;padding:2px 4px;font-size:11px;border:1px solid #ccc;border-radius:3px}
.hint{font-size:11px;color:#777;margin:-2px 0 8px;line-height:1.5}
.dg{display:grid;grid-template-columns:1fr 1fr;gap:0 14px;font-size:12px;font-variant-numeric:tabular-nums}
.dg div{display:flex;justify-content:space-between;padding:2px 0;border-bottom:1px solid #f0f0f0}
.dg span:first-child{color:#666}
.dg b{font-weight:600}
h3{font-size:11px;margin:10px 0 3px;color:#888;text-transform:uppercase;letter-spacing:.07em;grid-column:1/-1}
.ok{color:#1d7a4c}.no{color:#b4531a}.warn{color:#b4531a;font-weight:700}
.bar{height:3px;background:#e4e4e4;border-radius:2px;overflow:hidden;margin-top:2px}
.bar i{display:block;height:100%;background:#1f6f8b}
pre{background:#eceeea;padding:8px;font-size:12px;white-space:pre-wrap;margin:8px 0 0}
</style></head><body>
<header>PumpSaver <span id="ver" style="float:right;font-weight:400;font-size:12px;opacity:.8"></span></header>
<div class="sim" id="simbar" style="display:none">SIMULATION - NOT CONTROLLING REAL HARDWARE</div>
<main>
<div class="tiles">
<div class="t"><small>Pressure</small><b id="psi">--</b></div>
<div class="t"><small>Setpoint</small><b id="spa">--</b></div>
<div class="t"><small>State</small><b id="stt" style="font-size:15px">--</b></div>
<div class="t"><small>Cmd Hz</small><b id="cmdhz">--</b></div>
<div class="t"><small>Cap Hz</small><b id="caphz">--</b></div>
<div class="t"><small>Shutoff</small><b id="shz">--</b></div>
</div>
<div class="st" id="st">connecting...</div>

<section><h2>Operating envelope</h2>
<div id="gwrap" style="overflow-x:auto"><svg id="g" viewBox="0 0 520 300" style="width:100%;min-width:400px;height:auto"></svg></div>
<div style="font-size:11px;color:#555;line-height:1.7;margin-top:4px">
<span style="color:#b4531a">&#9473;</span> cavitation cap &nbsp;
<span style="color:#b4531a">&#9476;</span> <span id="lgq">--</span> gpm onset &nbsp;
<span style="color:#4a6fa5">&#9473;</span> shutoff (no flow below) &nbsp;
<span style="color:#1a2230">&#9679;</span> now
<div id="marg"></div></div>
</section>

<section><h2>Run</h2>
<button onclick="sendCmd('start')">Enable</button>
<button class="red" onclick="sendCmd('stop')">Stop</button>
<button class="grey" onclick="sendCmd('reset')">Fault reset</button>
<button class="grey" onclick="sendCmd('scan')">Rescan drives</button>
</section>

<section><h2>Diagnostics</h2>
<div class="dg" id="dg"></div>
</section>

<section><h2>Drives</h2>
<table><thead><tr><th>#</th><th>Addr</th><th>Comms</th><th>Hz</th><th>A</th><th>State</th></tr></thead>
<tbody id="drv"></tbody></table>
</section>

<section id="simsec" style="display:none"><h2>Simulated plant</h2>
<div class="sl"><span>Drive by</span><select id="sm" style="padding:6px;font-size:14px;border:1px solid #bbb;border-radius:4px">
<option value="0">Demand &mdash; set a draw, plant finds its pressure</option>
<option value="1">Pressure &mdash; hold the header where I put it</option></select><span></span></div>
<div class="hint" style="margin-left:96px">Pressure mode makes the header an infinitely stiff source, so the loop
reacts to whatever you dial in and the pump cannot move it. Quickest way to sweep the cap curve
and watch the sleep gates flip without waiting on tank dynamics.</div>
<div id="psirow">
<div class="sl"><span>Header psi</span><input type="range" id="sp2" min="0" max="90" step="0.5"><input type="number" id="sp2v" step="0.5"></div>
<div class="rng">range<input type="number" id="sp2min" value="0"><span>to</span><input type="number" id="sp2max" value="90"></div>
</div>
<div class="sl"><span>Demand gpm</span><input type="range" id="sd" min="0" max="120" step="0.5"><input type="number" id="sdv" step="0.5"></div>
<div class="rng">range<input type="number" id="sdmin" value="0"><span>to</span><input type="number" id="sdmax" value="120"></div>
<div class="sl"><span>Time scale &times;</span><input type="range" id="ts" min="1" max="60" step="1"><input type="number" id="tsv" step="1"></div>
<div class="rng">range<input type="number" id="tsmin" value="1"><span>to</span><input type="number" id="tsmax" value="60"></div>
<div class="sl"><span>Tank gal/psi</span><input type="range" id="cg" min="0.2" max="10" step="0.1"><input type="number" id="cgv" step="0.1"></div>
<div class="rng">range<input type="number" id="cgmin" value="0.2"><span>to</span><input type="number" id="cgmax" value="10"></div>
<div style="font-size:12px;color:#666;margin-top:10px">Flow <b id="fl">--</b> gpm &middot; actual <b id="ha">--</b> Hz
<div style="color:#888;font-size:11px;margin-top:3px">A trickle sleeps; real demand should not. At 55 psi the loop needs
about 51.4 Hz to hold 40 gpm, and phase 1 only arms at or below <b id="shz2">50</b> Hz.</div></div>
</section>

<form id="f" onsubmit="return save()">
<section><h2>Setpoint &amp; loop</h2>
<label>Setpoint psi<input type="number" step="0.5" name="setpoint"></label>
<label>Kp Hz/psi<input type="number" step="0.01" name="kp"></label>
<label>Ki Hz/psi&middot;s<input type="number" step="0.01" name="ki"></label>
<label>Min Hz<input type="number" step="0.5" name="minHz"></label>
<label>Max Hz<input type="number" step="0.5" name="maxHz"></label>
<label>Fill ramp psi/s<input type="number" step="0.5" name="spRampPsiS"></label>
<label>Fill preload psi<input type="number" step="1" name="spStepPsi"></label>
<label>Transducer span psi<input type="number" step="5" name="xdcrSpanPsi"></label>
</section>

<section><h2>Pump curve</h2>
<label>Shutoff psi at 60Hz<input type="number" step="1" name="shutoffPsiAt60"></label>
<div class="hint"><b>Deadhead pressure.</b> Close the discharge valve, run at 60 Hz, read
the gauge. Your PLC already assumes 84 (it computes shutoff speed as 60&middot;&radic;(SP/84)).</div>
<label>Free flow gpm at 60Hz<input type="number" step="1" name="qMax60"></label>
<div class="hint"><b>Flow at 60 Hz with the discharge wide open</b> (zero head). If you
cannot measure that, use the solver below &mdash; one ordinary operating point is enough.
Only the envelope plot and the gpm readouts use it; the control loop never does.</div>
<label>Cavitation onset gpm<input type="number" step="1" name="cavOnsetGPM"></label>
<div class="hint">The flow where the bench heard it rattle, 103&ndash;107 on yours.</div>
<div style="background:#f7f8f6;border-radius:5px;padding:8px;margin-top:6px">
<div style="font-size:12px;font-weight:600;margin-bottom:4px">Solve free flow from one measured point</div>
<div class="sl" style="grid-template-columns:1fr 1fr 1fr"><input type="number" id="fq" placeholder="Hz"><input type="number" id="fp" placeholder="psi"><input type="number" id="fg" placeholder="gpm"></div>
<button type="button" onclick="solveQ()" style="font-size:13px;padding:7px 12px">Compute</button>
<span id="fout" style="font-size:12px;margin-left:8px"></span>
<div class="hint" style="margin-top:6px">Run the pump anywhere you can read speed, pressure and flow
at the same moment, then Compute. It back-solves the curve and fills the box above.</div>
</div>
</section>

<section><h2>Cavitation cap (psi &rarr; max Hz)</h2>
<div class="cap"><span>Pt</span><span>psi</span><span>Hz</span>
<span>1</span><input type="number" name="cp0" step="1"><input type="number" name="ch0" step="0.5">
<span>2</span><input type="number" name="cp1" step="1"><input type="number" name="ch1" step="0.5">
<span>3</span><input type="number" name="cp2" step="1"><input type="number" name="ch2" step="0.5">
<span>4</span><input type="number" name="cp3" step="1"><input type="number" name="ch3" step="0.5">
</div><label>Points used<input type="number" name="capPts" min="2" max="4"></label>
</section>

<section><h2>Sleep</h2>
<label>Delay s (both phases)<input type="number" step="1" name="sleepDlyS"></label>
<label>Threshold from shutoff? 1/0<input type="number" min="0" max="1" name="sleepRelShutoff"></label>
<div class="hint">1 = the sleep threshold sits a fixed margin above the <i>shutoff speed</i>, so it
tracks the setpoint. 0 = the old fixed Hz below. A fixed 50 Hz can never sleep above about
62 psi setpoint, because holding 70 psi needs 54.8 Hz just to reach shutoff.</div>
<label>Margin above shutoff Hz<input type="number" step="0.05" name="sleepHzMargin"></label>
<label>Phase 1 Hz (fixed mode)<input type="number" step="0.5" name="sleepHz"></label>
<label>Phase 2 offset (fixed mode)<input type="number" step="0.5" name="sleepHz2"></label>
<label>Band psi<input type="number" step="0.5" name="sleepBand"></label>
<label>Charge psi (0 = off)<input type="number" step="0.5" name="sleepBoost"></label>
<label>Charge ramp s<input type="number" step="0.5" name="chargeRampS"></label>
<div class="hint">Eases the setpoint up into the charge instead of stepping it, so the boost
does not arrive as a jump on the speed command.</div>
<label>Charge max s<input type="number" step="1" name="boostMaxS"></label>
<label>Wake drop psi<input type="number" step="0.5" name="wakeDrop"></label>
<label>Min sleep s<input type="number" step="1" name="sleepMinS"></label>
<label>Flow meter fitted? 1/0<input type="number" min="0" max="1" name="useFlow"></label>
<div class="hint">Speed is a poor proxy for flow near shutoff &mdash; at 55 psi, 2 gpm sits at
48.56 Hz and 20 gpm at 49.3 Hz, only 0.75 Hz apart. With a meter fitted the sleep decision
uses actual gpm instead and the two are trivially distinguishable.</div>
<label>Idle gpm<input type="number" step="0.5" name="idleGPM"></label>
<label>Wake gpm<input type="number" step="0.5" name="wakeGPM"></label>
</section>

<section><h2>Staging</h2>
<div class="hint" style="margin:0 0 8px"><b>Staging is bringing the second pump in and out.</b>
One pump is lead and runs whenever the system is awake; the lag pump joins it when the lead is
pinned at its cavitation cap and pressure is still losing ground, and drops out again when one
pump alone could carry the flow. Both always run at the <i>same speed</i> &mdash; on a common
discharge header a slower pump sits below header pressure and just churns. The delays and the
minimum run time are what stop it hunting in and out.</div>
<label>Stage up below SP-<input type="number" step="0.5" name="stageUpPsi"></label>
<label>Stage up delay s<input type="number" step="1" name="stageUpDlyS"></label>
<label>Stage down Hz<input type="number" step="0.5" name="stageDownHz"></label>
<label>Stage down delay s<input type="number" step="1" name="stageDownDlyS"></label>
<label>Lag min run s<input type="number" step="1" name="lagMinRunS"></label>
</section>
<button type="submit">Save settings</button>
</form>

<section><h2>Network</h2>
<div class="hint" style="margin:0 0 8px">The <b>FCW-PUMP</b> access point stays up permanently,
so this page is reachable whether or not the site network is working. Joining the customer
WiFi is optional and only feeds telemetry &mdash; PumpSaver never takes a command from the
network. Scanning drops AP clients for a second: one radio has to leave the channel to look
around.</div>
<div id="nst" class="st">&hellip;</div>
<form id="nf" onsubmit="return saveNet()">
<label>SSID<span></span></label>
<div style="display:grid;grid-template-columns:1fr auto;gap:6px;margin:0 0 6px">
  <select id="ssidSel" onchange="pickSsid()"><option value="">-- scan or type below --</option></select>
  <button type="button" class="grey" onclick="scan()" id="scanBtn">Scan</button>
</div>
<label>SSID<input name="ssid" id="ssidIn" placeholder="network name"></label>
<label>Password<input type="password" name="pass" placeholder="unchanged"></label>
<label>MQTT on<input type="number" step="1" min="0" max="1" name="mqttOn"></label>
<label>Broker host<input name="host" placeholder="10.0.0.5 or broker.example.com"></label>
<label>Broker port<input type="number" step="1" name="port"></label>
<label>Username<input name="user" placeholder="optional"></label>
<label>Password<input type="password" name="mpass" placeholder="unchanged"></label>
<label>Topic prefix<input name="topic"></label>
<label>Publish every ms<input type="number" step="100" name="pubMs"></label>
<button type="submit">Save network</button>
</form>
</section>

<section><h2>Backup &amp; restore</h2>
<div class="hint" style="margin:0 0 8px">Every setting on this page as a JSON file &mdash; commission
one skid, then paste the same config into the rest. <b>Passwords are never included</b>, so a config
that gets emailed around cannot leak a customer's WiFi key. Importing applies immediately, the same
way Save does; it cannot start a pump, because the run state is not in the file.</div>
<button type="button" onclick="location.href='/export'">Download file</button>
<button type="button" class="grey" onclick="showCfg()">Show / copy</button>
<textarea id="cfgTa" rows="8" placeholder="Paste a saved config here, then Import"
  style="width:100%;box-sizing:border-box;margin-top:8px;font-family:ui-monospace,monospace;font-size:12px;border:1px solid #bbb;border-radius:4px;padding:6px"></textarea>
<label style="grid-template-columns:1fr 92px">Also apply network<input type="number" step="1" min="0" max="1" id="impNet" value="0"></label>
<button type="button" class="red" onclick="doImport()">Import pasted config</button>
</section>

<pre id="log"></pre>
</main>
<script>
const f=document.getElementById('f');
const SN=['idle','fill','regulate','capped','charging','asleep','staged','FAULT'];
let SET=null,trail=[];

// ---- operating envelope -------------------------------------------------
// x = header pressure, y = speed.  Above the cap curve is forbidden; below the
// shutoff curve the pump is deadheaded and delivering nothing.  The dashed
// contour is the real constant-flow line at cavitation onset -- the cap table
// is a piecewise approximation of it, and the gap between them is the margin.
function capAt(p,cp,ch,n){
  if(p<=cp[0])return ch[0];
  if(p>=cp[n-1])return ch[n-1];
  for(let i=0;i<n-1;i++)
    if(p>=cp[i]&&p<cp[i+1])return ch[i]+(ch[i+1]-ch[i])*((p-cp[i])/(cp[i+1]-cp[i]));
  return ch[0];
}
// Q(f,P) = qMax60*(f/60)*sqrt(1-P/H0),  H0 = shutoff*(f/60)^2
function flowAt(hz,p,q0,sh){
  const r=hz/60,h0=sh*r*r;
  if(hz<1||p>=h0)return 0;
  return q0*r*Math.sqrt(1-p/h0);
}
function drawGraph(j,s){
  const n=s.capPts,cp=s.capPsi,ch=s.capHz,yM=s.maxHz,sh=s.shutoffPsiAt60;
  const q0=s.qMax60,ql=s.cavOnsetGPM;
  let xM=Math.max(60,cp[n-1]+20,s.setpoint+15);xM=Math.ceil(xM/10)*10;
  const W=520,H=300,ML=38,MR=12,MT=12,MB=26,N=80;
  const px=p=>ML+(p/xM)*(W-ML-MR), py=h=>H-MB-(Math.min(h,yM)/yM)*(H-MT-MB);
  const pth=a=>a.map((v,i)=>(i?'L':'M')+px(v[0]).toFixed(1)+' '+py(v[1]).toFixed(1)).join('');
  let cap=[],sho=[],iso=[];
  for(let i=0;i<=N;i++){const p=xM*i/N;
    cap.push([p,capAt(p,cp,ch,n)]);
    sho.push([p,60*Math.sqrt(p/sh)]);
    iso.push([p,60*Math.sqrt(Math.pow(ql/q0,2)+p/sh)]);}
  let o='';
  // forbidden: above the cap
  o+='<path d="'+pth(cap)+'L'+px(xM)+' '+py(yM)+'L'+px(0)+' '+py(yM)+'Z" fill="#b4531a" opacity=".10"/>';
  // no delivery: below shutoff
  o+='<path d="'+pth(sho)+'L'+px(xM)+' '+py(0)+'L'+px(0)+' '+py(0)+'Z" fill="#4a6fa5" opacity=".10"/>';
  // grid + axes
  for(let p=0;p<=xM;p+=10){o+='<line x1="'+px(p)+'" y1="'+py(0)+'" x2="'+px(p)+'" y2="'+py(yM)+'" stroke="#ddd"/>'
    +'<text x="'+px(p)+'" y="'+(H-9)+'" font-size="9" fill="#777" text-anchor="middle">'+p+'</text>';}
  for(let h=0;h<=yM;h+=10){o+='<line x1="'+px(0)+'" y1="'+py(h)+'" x2="'+px(xM)+'" y2="'+py(h)+'" stroke="#ddd"/>'
    +'<text x="'+(ML-5)+'" y="'+(py(h)+3)+'" font-size="9" fill="#777" text-anchor="end">'+h+'</text>';}
  o+='<text x="'+(W/2)+'" y="'+(H-1)+'" font-size="9" fill="#777" text-anchor="middle">header psi</text>';
  o+='<text x="9" y="'+(MT+8)+'" font-size="9" fill="#777">Hz</text>';
  // min speed
  o+='<line x1="'+px(0)+'" y1="'+py(s.minHz)+'" x2="'+px(xM)+'" y2="'+py(s.minHz)+'" stroke="#999" stroke-dasharray="2 3"/>';
  // setpoint
  o+='<line x1="'+px(s.setpoint)+'" y1="'+py(0)+'" x2="'+px(s.setpoint)+'" y2="'+py(yM)+'" stroke="#1f6f8b" stroke-dasharray="4 3" opacity=".7"/>';
  // curves
  o+='<path d="'+pth(iso)+'" fill="none" stroke="#b4531a" stroke-width="1.4" stroke-dasharray="5 4"/>';
  o+='<path d="'+pth(sho)+'" fill="none" stroke="#4a6fa5" stroke-width="1.6"/>';
  o+='<path d="'+pth(cap)+'" fill="none" stroke="#b4531a" stroke-width="2.2"/>';
  // where we have been, and where we are
  if(trail.length>1)o+='<path d="'+pth(trail)+'" fill="none" stroke="#1a2230" stroke-width="1" opacity=".35"/>';
  if(j.state!=0)o+='<circle cx="'+px(j.psi)+'" cy="'+py(j.hzCmd)+'" r="5" fill="#1a2230"/>';
  document.getElementById('g').innerHTML=o;
  lgq.textContent=ql.toFixed(0);
  // worst-case margin along the cap curve
  let worst=0,at=0;
  for(let i=0;i<=N;i++){const q=flowAt(cap[i][1],cap[i][0],q0,sh);if(q>worst){worst=q;at=cap[i][0];}}
  const live=flowAt(j.hzCmd,j.psi,q0,sh);
  marg.innerHTML='Cap curve peaks at <b>'+worst.toFixed(0)+' gpm</b> near '+at.toFixed(0)
    +' psi &mdash; '+(ql-worst).toFixed(0)+' gpm of margin'
    +(s.capTableOK?'':' &middot; <b style="color:#b4531a">cap table invalid, parked at row 1</b>')
    +'<br>Now: <b>'+live.toFixed(0)+' gpm</b> at '+j.hzCmd.toFixed(1)+' Hz, '+j.psi.toFixed(1)+' psi';
}
async function tick(){try{const j=await(await fetch('/status')).json();
ver.textContent='v'+j.ver;
if(j.sim){simbar.style.display='block';simsec.style.display='block';
  fl.textContent=j.flow.toFixed(1);ha.textContent=j.hzAct.toFixed(1);}
psi.textContent=j.psi.toFixed(1);spa.textContent=j.spActive.toFixed(1);
stt.textContent=SN[j.state];cmdhz.textContent=j.hzCmd.toFixed(1);
caphz.textContent=j.capHz.toFixed(1);shz.textContent=j.shutoffHz.toFixed(1);
let r='';for(const d of j.drives){r+='<tr><td>'+d.n+'</td><td>'+d.addr+'</td><td>'+
 (d.present?(d.commsOK?'ok':'LOST'):'-')+'</td><td>'+d.hz.toFixed(1)+'</td><td>'+
 d.amps.toFixed(1)+'</td><td>'+(d.tripped?('TRIP '+d.tripCode):(d.running?'run':'stop'))+
 '</td></tr>';}drv.innerHTML=r;
let s=j.enable?'Enabled':'Disabled';
if(!j.psiValid)s='PRESSURE INVALID - pumps held';
else if(j.addr1)s+=' - uncommissioned drive at address 1';
st.textContent=s+' - sleep cycles '+j.sleepCycles;
st.className='st'+((!j.psiValid||j.state==7)?' bad':'');
if(SET){if(j.state!=0&&j.psiValid){trail.push([j.psi,j.hzCmd]);if(trail.length>300)trail.shift();}
        drawGraph(j,SET);}
drawDiag(await(await fetch('/diag')).json());
const lg=await(await fetch('/log')).text();if(lg)log.textContent=lg;
}catch(e){st.textContent='no link';st.className='st bad'}}
// ---- diagnostics --------------------------------------------------------
const R=(k,v,c)=>'<div><span>'+k+'</span><b class="'+(c||'')+'">'+v+'</b></div>';
const B=x=>x?'<span class="ok">yes</span>':'<span class="no">no</span>';
const T=(a,p)=>'<div><span>'+a[0]+'</span><b>'+a[1].toFixed(1)+' / '+p.toFixed(0)+
  's</b><div class="bar" style="flex-basis:100%"><i style="width:'+
  Math.min(100,p>0?a[1]/p*100:0).toFixed(0)+'%"></i></div></div>';
function drawDiag(d){
  let o='<h3>Loop</h3>';
  o+=R('setpoint',d.sp.toFixed(1)+' psi')+R('chasing (spEff)',d.spEff.toFixed(2));
  o+=R('active target',d.spActive.toFixed(1))+R('pressure',d.psi.toFixed(2));
  o+=R('error',d.err.toFixed(2)+' psi',Math.abs(d.err)>5?'warn':'');
  o+=R('P term (Kp '+d.kp.toFixed(2)+')',d.p.toFixed(2)+' Hz');
  o+=R('I term (Ki '+d.ki.toFixed(2)+')',d.i.toFixed(2)+' Hz',
       (d.iAtFloor||d.iAtCap)?'warn':'');
  o+=R('PI raw out',d.raw.toFixed(2)+' Hz');
  o+=R('anti-windup',d.clamp.toFixed(2)+' Hz',Math.abs(d.clamp)>0.01?'warn':'');
  o+=R('commanded',d.hz.toFixed(2)+' Hz')+R('floor / cap',d.floor.toFixed(1)+' / '+d.cap.toFixed(2));
  o+=R('integrator pinned',d.iAtFloor?'at FLOOR':(d.iAtCap?'at CAP':'no'),
       (d.iAtFloor||d.iAtCap)?'warn':'ok');
  o+=R('delivering',d.nowgpm.toFixed(1)+' gpm');
  o+='<h3>Cavitation cap</h3>';
  o+=R('filtered psi',d.psiFilt.toFixed(2))+R('cap target',d.capTgt.toFixed(2)+' Hz');
  o+=R('cap now (slewed)',d.cap.toFixed(2)+' Hz')+R('table valid',B(d.capOK));
  o+='<h3>Sleep &mdash; stage '+d.stage+'</h3>';
  o+=R('shutoff @ SP',d.shSP.toFixed(2)+' Hz')+R('shutoff @ charge',d.shAct.toFixed(2)+' Hz');
  o+=R('phase 1 threshold',d.thr1.toFixed(2)+' Hz &asymp; '+d.thr1gpm.toFixed(0)+' gpm');
  o+=R('phase 2 threshold',d.thr2.toFixed(2)+' Hz &asymp; '+d.thr2gpm.toFixed(0)+' gpm');
  o+=R('P1: fill done',B(d.g1spEff))+R('P1: psi in band',B(d.g1psi));
  o+=R('P1: speed low',B(d.g1hz))+R('flow idle',B(d.flowIdle));
  o+=R('P2: psi charged',B(d.g2psi))+R('P2: speed low',B(d.g2hz));
  o+=R('flow meter',d.useFlow?d.flowGPM.toFixed(1)+' gpm':'not fitted',d.useFlow?'':'no');
  o+=R('wake condition',B(d.wake))+R('charge abandons',d.abandons);
  o+=T(['phase 1 hold',d.tP1],d.dly)+T(['phase 2 hold',d.tP2],d.dly);
  o+=T(['charge timeout',d.tBst],d.bstS)+T(['min sleep',d.tMin],d.minS);
  o+='<h3>Staging</h3>';
  o+=R('lag pump on',B(d.lagOn))+R('up / down asking',B(d.stageUp)+' / '+B(d.stageDown));
  o+=T(['stage up hold',d.tUp],d.upS)+T(['stage down hold',d.tDn],d.dnS);
  o+=T(['lag min run',d.tLag],d.lagS);
  document.getElementById('dg').innerHTML=o;
  const s=document.getElementById('shz2');if(s)s.textContent=d.thr1.toFixed(1);
}
// Back-solve free-flow from one measured point: Q = q0*r*sqrt(1-P/(S*r^2))
function solveQ(){const hz=+fq.value,p=+fp.value,g=+fg.value,sh=+f.elements.shutoffPsiAt60.value;
 const r=hz/60,h0=sh*r*r;
 if(!(hz>0&&g>0)){fout.innerHTML='<span class="no">need Hz and gpm</span>';return;}
 if(p>=h0){fout.innerHTML='<span class="no">'+p+' psi is at or above this speed’s shutoff ('
  +h0.toFixed(1)+' psi) — no flow is possible there, check the numbers</span>';return;}
 const q0=g/(r*Math.sqrt(1-p/h0));
 f.elements.qMax60.value=q0.toFixed(0);
 fout.innerHTML='<span class="ok">free flow &asymp; '+q0.toFixed(0)+' gpm — Save to keep</span>';}

async function loadS(){const j=await(await fetch('/settings')).json();SET=j;
for(const k in j){const el=f.elements[k];if(el)el.value=j[k];}
for(let i=0;i<4;i++){f.elements['cp'+i].value=j.capPsi[i];f.elements['ch'+i].value=j.capHz[i];}
sd.value=j.simDemandGPM;ts.value=j.simTimeScale;cg.value=j.simCapGalPsi;
sm.value=j.simMode;sp2.value=j.simPsi;simEcho();}

// Sliders: the readout follows the thumb live, the POST is debounced while
// dragging and fires immediately on release, so there is no Apply button.
let simT=null;
function simEcho(){sdv.value=sd.value;tsv.value=ts.value;cgv.value=cg.value;sp2v.value=sp2.value;
 psirow.style.display=(sm.value=='1')?'block':'none';}
function simDrag(){simEcho();clearTimeout(simT);simT=setTimeout(pushSim,150);}
function simDrop(){simEcho();clearTimeout(simT);pushSim();}
function simBox(){sd.value=sdv.value;ts.value=tsv.value;cg.value=cgv.value;sp2.value=sp2v.value;simDrop();}
function simRange(){sd.min=sdmin.value;sd.max=sdmax.value;ts.min=tsmin.value;ts.max=tsmax.value;
 cg.min=cgmin.value;cg.max=cgmax.value;sp2.min=sp2min.value;sp2.max=sp2max.value;simEcho();}
async function pushSim(){const d=new URLSearchParams();
 d.set('simMode',sm.value);d.set('simPsi',sp2.value);
 d.set('simDemandGPM',sd.value);d.set('simTimeScale',ts.value);d.set('simCapGalPsi',cg.value);
 await fetch('/sim',{method:'POST',body:d});}
for(const el of [sd,ts,cg,sp2]){el.addEventListener('input',simDrag);el.addEventListener('change',simDrop);}
for(const el of [sdv,tsv,cgv,sp2v])el.addEventListener('change',simBox);
for(const el of [sdmin,sdmax,tsmin,tsmax,cgmin,cgmax,sp2min,sp2max])el.addEventListener('change',simRange);
sm.addEventListener('change',simDrop);
// ---- backup / restore ---------------------------------------------------
async function showCfg(){
  const t=await(await fetch('/export')).text();
  const ta=document.getElementById('cfgTa');ta.value=t;ta.focus();ta.select();
  try{await navigator.clipboard.writeText(t);log.textContent='Config copied to clipboard.';}
  catch(e){log.textContent='Config shown above -- select and copy.';}
}
async function doImport(){
  const t=document.getElementById('cfgTa').value.trim();
  if(!t){log.textContent='Paste a config first.';return;}
  const d=new URLSearchParams();
  d.set('cfg',t);d.set('withNet',document.getElementById('impNet').value);
  log.textContent=await(await fetch('/import',{method:'POST',body:d})).text();
  trail=[];await loadS();await loadNet();
}

// ---- network ------------------------------------------------------------
const nf=document.getElementById('nf');
function pickSsid(){const v=document.getElementById('ssidSel').value;if(v)document.getElementById('ssidIn').value=v;}

async function scan(){
  const b=document.getElementById('scanBtn');b.disabled=true;b.textContent='Scanning';
  try{
    let j=await(await fetch('/scan')).json();
    for(let i=0;i<12&&j.state!=='done';i++){await new Promise(r=>setTimeout(r,700));
      j=await(await fetch('/scan')).json();}
    const sel=document.getElementById('ssidSel');
    sel.innerHTML='<option value="">-- scan or type below --</option>';
    (j.nets||[]).sort((a,b)=>b.rssi-a.rssi).forEach(nw=>{
      const o=document.createElement('option');
      o.value=nw.ssid;o.textContent=nw.ssid+'  ('+nw.rssi+' dBm'+(nw.lock?', locked':'')+')';
      sel.appendChild(o);});
    if(!(j.nets||[]).length)sel.innerHTML='<option value="">no networks found</option>';
  }catch(e){}
  b.disabled=false;b.textContent='Scan';
}

async function loadNet(){
  const j=await(await fetch('/net')).json();
  for(const k of ['ssid','host','port','user','topic','pubMs'])
    if(nf.elements[k])nf.elements[k].value=j[k];
  nf.elements.mqttOn.value=j.mqttOn?1:0;
  const d=document.getElementById('nst');
  let t='AP '+j.apip+' &middot; id <b>'+j.id+'</b><br>';
  t+= j.sta ? 'Joined <b>'+j.ssid+'</b> as '+j.ip+' ('+j.rssi+' dBm)'
            : (j.ssid ? 'Not joined to <b>'+j.ssid+'</b> yet' : 'No network set &mdash; AP only');
  if(j.mqttOn)t+='<br>MQTT '+(j.mqtt?'connected':'not connected'+(j.mqttFails?' ('+j.mqttFails+' tries)':''));
  if(j.log)t+='<br>'+j.log;
  d.innerHTML=t;d.className='st'+((j.ssid&&!j.sta)||(j.mqttOn&&!j.mqtt)?' bad':'');
}

async function saveNet(){
  const d=new URLSearchParams(new FormData(nf));
  const r=await fetch('/net',{method:'POST',body:d});
  document.getElementById('log').textContent=await r.text();
  setTimeout(loadNet,1500);
  return false;
}

async function save(){const d=new URLSearchParams(new FormData(f));
log.textContent=await(await fetch('/set',{method:'POST',body:d})).text();
trail=[];await loadS();return false;}
async function sendCmd(c){log.textContent=await(await fetch('/cmd?c='+c,{method:'POST'})).text();}
loadS();loadNet();tick();setInterval(tick,1000);setInterval(loadNet,5000);
</script></body></html>)HTML";
