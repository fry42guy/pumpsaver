#pragma once
#include "ui_common.h"

/*
  Screen 2 -- Advanced.  Every parameter the control block has, the operating
  envelope, and the full loop diagnostics.

  Ordered by how often it is touched: readouts, then run controls and the
  setpoint, then the plot, then diagnostics, then the settings form.  The
  explanatory paragraphs that used to sit between the fields are gone -- the
  reasoning they carried lives in DESIGN_NOTES.md, which is where it can be
  kept accurate.  What stays on the page is anything that is a TOOL rather
  than prose: the free-flow solver, the cap-table validity flag, the sim
  sliders.
*/

const char PAGE_ADV[] PROGMEM = R"HTML(<!doctype html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>PumpSaver - Advanced</title><style>)HTML" CSS_BASE R"HTML(
.tiles{display:grid;grid-template-columns:repeat(3,1fr);gap:8px;margin-bottom:11px}
.t{background:var(--card);border-radius:9px;padding:9px 10px;box-shadow:0 1px 2px rgba(23,33,46,.06)}
.t small{display:block;color:var(--mut);font-size:10px;letter-spacing:.07em;text-transform:uppercase}
.t b{font-size:22px;font-variant-numeric:tabular-nums;font-weight:600}
.st{margin-bottom:11px;padding:10px 12px;border-radius:9px;background:#e4efe8;
  color:#166040;font-size:13px;font-weight:600}
.st.bad{background:#f8e2d9;color:#8f3f11}
label{display:grid;grid-template-columns:1fr 96px;align-items:center;gap:8px;
  margin:5px 0;font-size:13px}
label input{padding:6px 7px;font-size:14px}
.cap{display:grid;grid-template-columns:26px 1fr 1fr;gap:6px;font-size:13px;align-items:center}
.cap input{padding:6px 7px;font-size:14px}
.sl{display:grid;grid-template-columns:92px 1fr 72px;gap:8px;align-items:center;
  font-size:13px;margin-top:8px}
.sl input[type=range]{width:100%;margin:0;padding:0;border:0;background:none;accent-color:var(--acc)}
.sl input[type=number]{padding:5px}
.rng{display:flex;gap:5px;align-items:center;font-size:11px;color:var(--mut);margin:3px 0 0 92px}
.rng input{width:58px;padding:2px 4px;font-size:11px}
.dg{display:grid;grid-template-columns:1fr 1fr;gap:0 14px;font-size:12px;
  font-variant-numeric:tabular-nums}
.dg>div{display:flex;justify-content:space-between;align-items:center;
  flex-wrap:wrap;padding:2px 0;border-bottom:1px solid #f1f3f5}
.dg span:first-child{color:var(--mut)}
.dg b{font-weight:600}
h3{font-size:10px;margin:11px 0 3px;color:var(--acc);text-transform:uppercase;
  letter-spacing:.08em;grid-column:1/-1;font-weight:700}
.bar{height:3px;background:#e6e9ec;border-radius:2px;overflow:hidden;
  flex-basis:100%;margin-top:2px}
.bar i{display:block;height:100%;background:var(--acc)}
table{width:100%;border-collapse:collapse;font-size:13px}
th,td{text-align:left;padding:5px 6px;border-bottom:1px solid var(--line)}
th{font-size:10px;text-transform:uppercase;letter-spacing:.06em;color:var(--mut)}
.tool{background:#f4f6f8;border-radius:8px;padding:9px;margin-top:8px}
.tool .sl{grid-template-columns:1fr 1fr 1fr;margin-top:0}
</style></head><body>)HTML" NAV_HTML R"HTML(
<main>

<div class="tiles">
<div class="t"><small>Pressure</small><b id="psi">--</b></div>
<div class="t"><small>Target</small><b id="spa">--</b></div>
<div class="t"><small>State</small><b id="stt" style="font-size:15px">--</b></div>
<div class="t"><small>Cmd Hz</small><b id="cmdhz">--</b></div>
<div class="t"><small>Cap Hz</small><b id="caphz">--</b></div>
<div class="t"><small>Shutoff</small><b id="shz">--</b></div>
</div>
<div class="st" id="st">connecting...</div>

<section><h2>Run</h2>
<div class="row">
<button onclick="sendCmd('start')">Enable</button>
<button class="red" onclick="sendCmd('stop')">Stop</button>
<button class="grey" onclick="sendCmd('reset')">Fault reset</button>
<button class="grey" onclick="sendCmd('scan')">Rescan drives</button>
</div>
<div class="sl" style="margin-top:11px"><span>Setpoint psi</span>
<input type="range" id="qsp" min="5" max="100" step="1">
<input type="number" id="qspv" step="1"></div>
</section>

<section><h2>Drives</h2>
<table><thead><tr><th>#</th><th>Addr</th><th>Comms</th><th>Hz</th><th>A</th><th>State</th></tr></thead>
<tbody id="drv"></tbody></table>
</section>

<section><h2>Operating envelope</h2>
<div style="overflow-x:auto"><svg id="g" viewBox="0 0 520 300"
  style="width:100%;min-width:400px;height:auto"></svg></div>
<div class="note">
<span style="color:#b4531a">&#9473;</span> cavitation cap &nbsp;
<span style="color:#b4531a">&#9476;</span> <span id="lgq">--</span> gpm onset &nbsp;
<span style="color:#4a6fa5">&#9473;</span> shutoff &nbsp;
<span style="color:#17212e">&#9679;</span> now
<div id="marg" style="margin-top:3px"></div></div>
</section>

<section><h2>Diagnostics</h2><div class="dg" id="dg"></div></section>

<section id="simsec" style="display:none"><h2>Simulated plant</h2>
<div class="sl"><span>Drive by</span>
<select id="sm"><option value="0">Demand &mdash; plant finds its pressure</option>
<option value="1">Pressure &mdash; hold the header here</option></select><span></span></div>
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
<div class="note">Flow <b id="fl">--</b> gpm &middot; actual <b id="ha">--</b> Hz</div>
</section>

<form id="f" onsubmit="return save()">

<section><h2>Loop</h2>
<label>Kp Hz/psi<input type="number" step="0.01" name="kp"></label>
<label>Ki Hz/psi&middot;s<input type="number" step="0.01" name="ki"></label>
<label>Min Hz<input type="number" step="0.5" name="minHz"></label>
<label>Max Hz<input type="number" step="0.5" name="maxHz"></label>
<label>Setpoint psi<input type="number" step="0.5" name="setpoint"></label>
<label>Fill ramp psi/s<input type="number" step="0.5" name="spRampPsiS"></label>
<label>Fill preload psi<input type="number" step="1" name="spStepPsi"></label>
<label>Transducer span psi<input type="number" step="5" name="xdcrSpanPsi"></label>
</section>

<section><h2>Pump curve</h2>
<label>Shutoff psi at 60 Hz<input type="number" step="1" name="shutoffPsiAt60"></label>
<label>Free flow gpm at 60 Hz<input type="number" step="1" name="qMax60"></label>
<label>Cavitation onset gpm<input type="number" step="1" name="cavOnsetGPM"></label>
<div class="tool">
<div style="font-size:12px;font-weight:700;margin-bottom:5px">Solve free flow from one measured point</div>
<div class="sl"><input type="number" id="fq" placeholder="Hz"><input type="number" id="fp" placeholder="psi"><input type="number" id="fg" placeholder="gpm"></div>
<div class="row" style="margin-top:7px;align-items:center">
<button type="button" class="ghost" onclick="solveQ()">Compute</button>
<span id="fout" style="font-size:12px"></span></div>
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
<label>Margin above shutoff Hz<input type="number" step="0.05" name="sleepHzMargin"></label>
<label>Phase 1 Hz (fixed mode)<input type="number" step="0.5" name="sleepHz"></label>
<label>Phase 2 offset (fixed mode)<input type="number" step="0.5" name="sleepHz2"></label>
<label>Band psi<input type="number" step="0.5" name="sleepBand"></label>
<label>Charge psi (0 = off)<input type="number" step="0.5" name="sleepBoost"></label>
<label>Charge ramp s<input type="number" step="0.5" name="chargeRampS"></label>
<label>Charge max s<input type="number" step="1" name="boostMaxS"></label>
<label>Wake drop psi<input type="number" step="0.5" name="wakeDrop"></label>
<label>Min sleep s<input type="number" step="1" name="sleepMinS"></label>
<label>Flow meter fitted? 1/0<input type="number" min="0" max="1" name="useFlow"></label>
<label>Idle gpm<input type="number" step="0.5" name="idleGPM"></label>
<label>Wake gpm<input type="number" step="0.5" name="wakeGPM"></label>
</section>

<section><h2>Staging</h2>
<label>Stage up below SP-<input type="number" step="0.5" name="stageUpPsi"></label>
<label>Stage up delay s<input type="number" step="1" name="stageUpDlyS"></label>
<label>Stage down Hz<input type="number" step="0.5" name="stageDownHz"></label>
<label>Stage down delay s<input type="number" step="1" name="stageDownDlyS"></label>
<label>Lag min run s<input type="number" step="1" name="lagMinRunS"></label>
</section>

<button type="submit">Save settings</button>
</form>
<pre id="log"></pre>

</main><script>)HTML" NAV_JS R"HTML(
const f=document.getElementById('f');
const SN=['idle','fill','regulate','capped','charging','asleep','staged','FAULT'];
let SET=null,trail=[];

// ---- operating envelope -------------------------------------------------
// x = header pressure, y = speed.  Above the cap curve is forbidden; below
// the shutoff curve the pump is deadheaded and delivering nothing.  The
// dashed contour is the real constant-flow line at cavitation onset -- the
// cap table is a piecewise approximation of it, and the gap is the margin.
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
  o+='<path d="'+pth(cap)+'L'+px(xM)+' '+py(yM)+'L'+px(0)+' '+py(yM)+'Z" fill="#b4531a" opacity=".10"/>';
  o+='<path d="'+pth(sho)+'L'+px(xM)+' '+py(0)+'L'+px(0)+' '+py(0)+'Z" fill="#4a6fa5" opacity=".10"/>';
  for(let p=0;p<=xM;p+=10){o+='<line x1="'+px(p)+'" y1="'+py(0)+'" x2="'+px(p)+'" y2="'+py(yM)+'" stroke="#e0e4e8"/>'
    +'<text x="'+px(p)+'" y="'+(H-9)+'" font-size="9" fill="#6b7785" text-anchor="middle">'+p+'</text>';}
  for(let h=0;h<=yM;h+=10){o+='<line x1="'+px(0)+'" y1="'+py(h)+'" x2="'+px(xM)+'" y2="'+py(h)+'" stroke="#e0e4e8"/>'
    +'<text x="'+(ML-5)+'" y="'+(py(h)+3)+'" font-size="9" fill="#6b7785" text-anchor="end">'+h+'</text>';}
  o+='<text x="'+(W/2)+'" y="'+(H-1)+'" font-size="9" fill="#6b7785" text-anchor="middle">header psi</text>';
  o+='<text x="9" y="'+(MT+8)+'" font-size="9" fill="#6b7785">Hz</text>';
  o+='<line x1="'+px(0)+'" y1="'+py(s.minHz)+'" x2="'+px(xM)+'" y2="'+py(s.minHz)+'" stroke="#9aa4ae" stroke-dasharray="2 3"/>';
  o+='<line x1="'+px(s.setpoint)+'" y1="'+py(0)+'" x2="'+px(s.setpoint)+'" y2="'+py(yM)+'" stroke="#1f6f8b" stroke-dasharray="4 3" opacity=".7"/>';
  o+='<path d="'+pth(iso)+'" fill="none" stroke="#b4531a" stroke-width="1.4" stroke-dasharray="5 4"/>';
  o+='<path d="'+pth(sho)+'" fill="none" stroke="#4a6fa5" stroke-width="1.6"/>';
  o+='<path d="'+pth(cap)+'" fill="none" stroke="#b4531a" stroke-width="2.2"/>';
  if(trail.length>1)o+='<path d="'+pth(trail)+'" fill="none" stroke="#17212e" stroke-width="1" opacity=".35"/>';
  if(j.state!=0)o+='<circle cx="'+px(j.psi)+'" cy="'+py(j.hzCmd)+'" r="5" fill="#17212e"/>';
  document.getElementById('g').innerHTML=o;
  lgq.textContent=ql.toFixed(0);
  let worst=0,at=0;
  for(let i=0;i<=N;i++){const q=flowAt(cap[i][1],cap[i][0],q0,sh);if(q>worst){worst=q;at=cap[i][0];}}
  const live=flowAt(j.hzCmd,j.psi,q0,sh);
  marg.innerHTML='Cap peaks at <b>'+worst.toFixed(0)+' gpm</b> near '+at.toFixed(0)
    +' psi &mdash; '+(ql-worst).toFixed(0)+' gpm margin'
    +(s.capTableOK?'':' &middot; <b class="no">cap table invalid, parked at row 1</b>')
    +'<br>Now <b>'+live.toFixed(0)+' gpm</b> at '+j.hzCmd.toFixed(1)+' Hz, '+j.psi.toFixed(1)+' psi';
}

// ---- diagnostics --------------------------------------------------------
const R=(k,v,c)=>'<div><span>'+k+'</span><b class="'+(c||'')+'">'+v+'</b></div>';
const B=x=>x?'<span class="ok">yes</span>':'<span class="no">no</span>';
const T=(a,p)=>'<div><span>'+a[0]+'</span><b>'+a[1].toFixed(1)+' / '+p.toFixed(0)+
  's</b><div class="bar"><i style="width:'+
  Math.min(100,p>0?a[1]/p*100:0).toFixed(0)+'%"></i></div></div>';
function drawDiag(d){
  let o='<h3>Loop</h3>';
  o+=R('setpoint',d.sp.toFixed(1)+' psi')+R('chasing (spEff)',d.spEff.toFixed(2));
  o+=R('active target',d.spActive.toFixed(1))+R('pressure',d.psi.toFixed(2));
  o+=R('error',d.err.toFixed(2)+' psi',Math.abs(d.err)>5?'warn':'');
  o+=R('P term (Kp '+d.kp.toFixed(2)+')',d.p.toFixed(2)+' Hz');
  o+=R('I term (Ki '+d.ki.toFixed(2)+')',d.i.toFixed(2)+' Hz',(d.iAtFloor||d.iAtCap)?'warn':'');
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
}

// ---- poll ---------------------------------------------------------------
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

// Back-solve free flow from one measured point: Q = q0*r*sqrt(1-P/(S*r^2))
function solveQ(){const hz=+fq.value,p=+fp.value,g=+fg.value,sh=+f.elements.shutoffPsiAt60.value;
 const r=hz/60,h0=sh*r*r;
 if(!(hz>0&&g>0)){fout.innerHTML='<span class="no">need Hz and gpm</span>';return;}
 if(p>=h0){fout.innerHTML='<span class="no">'+p+' psi is at or above this speed shutoff ('
  +h0.toFixed(1)+' psi) - no flow is possible there</span>';return;}
 const q0=g/(r*Math.sqrt(1-p/h0));
 f.elements.qMax60.value=q0.toFixed(0);
 fout.innerHTML='<span class="ok">free flow &asymp; '+q0.toFixed(0)+' gpm - Save to keep</span>';}

async function loadS(){const j=await(await fetch('/settings')).json();SET=j;
for(const k in j){const el=f.elements[k];if(el)el.value=j[k];}
for(let i=0;i<4;i++){f.elements['cp'+i].value=j.capPsi[i];f.elements['ch'+i].value=j.capHz[i];}
sd.value=j.simDemandGPM;ts.value=j.simTimeScale;cg.value=j.simCapGalPsi;
sm.value=j.simMode;sp2.value=j.simPsi;
qsp.value=Math.round(j.setpoint);qspv.value=Math.round(j.setpoint);
simEcho();}

// ---- quick setpoint -----------------------------------------------------
// Same debounce shape as the sim sliders: live while dragging, POST on drop.
let qT=null;
function qPush(){clearTimeout(qT);qT=setTimeout(async()=>{
 const b=new URLSearchParams();b.set('setpoint',qsp.value);
 log.textContent=await(await fetch('/set',{method:'POST',body:b})).text();
 trail=[];await loadS();},300);}
qsp.addEventListener('input',()=>{qspv.value=qsp.value;});
qsp.addEventListener('change',()=>{qspv.value=qsp.value;qPush();});
qspv.addEventListener('change',()=>{qsp.value=qspv.value;qPush();});

// ---- sim sliders --------------------------------------------------------
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

async function save(){const d=new URLSearchParams(new FormData(f));
log.textContent=await(await fetch('/set',{method:'POST',body:d})).text();
trail=[];await loadS();return false;}
async function sendCmd(c){log.textContent=await(await fetch('/cmd?c='+c,{method:'POST'})).text();}
loadS();tick();setInterval(tick,1000);
</script></body></html>)HTML";
