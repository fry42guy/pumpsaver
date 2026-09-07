#pragma once
#include "ui_common.h"

/*
  Screen 1 -- Easy mode.  The landing page, and what the captive portal opens.

  Everything an operator does day to day and nothing else: what the pressure
  is, what it is being asked to hold, whether the pumps are running, and what
  the drives are actually reporting back.  Every tuning parameter lives on the
  Advanced screen.

  This page polls /status only.  /diag is ~50 fields and belongs to the screen
  that displays them.

  Element ids are never given a name that a function here also uses: a bare id
  becomes a property of window, and an id/function collision is what stopped
  the old Cmd tile updating.  See DESIGN_NOTES.md.
*/

const char PAGE_EASY[] PROGMEM = R"HTML(<!doctype html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>PumpSaver</title><style>)HTML" CSS_BASE R"HTML(
.hero{background:linear-gradient(160deg,#1f6f8b,#17576d);color:#fff;border-radius:12px;
  padding:16px;margin-bottom:11px;box-shadow:0 2px 8px rgba(23,87,109,.25)}
.hero .lab{font-size:11px;letter-spacing:.09em;text-transform:uppercase;opacity:.75}
.big{font-size:60px;font-weight:300;line-height:1;font-variant-numeric:tabular-nums;
  display:flex;align-items:baseline;gap:8px;margin:2px 0 6px}
.big u{font-size:19px;font-weight:500;text-decoration:none;opacity:.8}
.hero .sub{font-size:14px;opacity:.92;display:flex;align-items:center;gap:9px;flex-wrap:wrap}
.hero .pill{background:rgba(255,255,255,.18);color:#fff}
.hero svg{width:100%;height:74px;display:block;margin-top:10px}
.spwrap{display:flex;align-items:center;gap:10px}
.spwrap input[type=number]{width:96px;font-size:26px;font-weight:600;text-align:center;
  padding:6px;font-variant-numeric:tabular-nums}
.stepb{width:46px;height:46px;flex:0 0 46px;font-size:24px;font-weight:400;
  padding:0;background:#eaeef1;color:var(--acc2);border-radius:10px}
input[type=range]{width:100%;padding:0;border:0;background:none;margin-top:12px;accent-color:var(--acc)}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(88px,1fr));gap:8px}
.m{background:#f4f6f8;border-radius:8px;padding:9px 10px}
.m small{display:block;color:var(--mut);font-size:10px;letter-spacing:.07em;
  text-transform:uppercase;margin-bottom:2px}
.m b{font-size:21px;font-weight:600;font-variant-numeric:tabular-nums}
.m b i{font-size:11px;font-weight:500;font-style:normal;color:var(--mut);margin-left:2px}
.vfd{border:1px solid var(--line);border-radius:9px;padding:10px;margin-top:8px}
.vfd .hd{display:flex;justify-content:space-between;align-items:center;margin-bottom:8px}
.vfd .hd b{font-size:14px}
.big2{display:flex;gap:8px}
.big2 button{flex:1;padding:15px;font-size:16px}
.alert{border-radius:9px;padding:11px 13px;margin-bottom:11px;font-size:14px;font-weight:600}
.alert.bad{background:#f8e2d9;color:#8f3f11;border-left:5px solid var(--bad)}
.alert.ok{background:#e4efe8;color:#166040;border-left:5px solid var(--ok)}
</style></head><body>)HTML" NAV_HTML R"HTML(
<main>

<div class="hero">
  <div class="lab">Header pressure</div>
  <div class="big"><span id="psi">--</span><u>psi</u></div>
  <div class="sub"><span class="pill" id="state">--</span>
    <span>target <b id="tgt">--</b> psi</span>
    <span id="node" style="opacity:.7"></span></div>
  <svg id="trend" viewBox="0 0 320 74" preserveAspectRatio="none"></svg>
</div>

<div class="alert ok" id="banner">connecting...</div>

<section><h2>Set pressure</h2>
  <div class="spwrap">
    <button class="stepb" onclick="bump(-1)">&minus;</button>
    <input type="number" id="spbox" min="5" max="100" step="1">
    <button class="stepb" onclick="bump(1)">+</button>
    <span class="mut" style="font-size:13px">psi</span>
  </div>
  <input type="range" id="spsl" min="5" max="100" step="1">
  <div class="note" id="spmsg">&nbsp;</div>
</section>

<section><h2>Run</h2>
  <div class="big2">
    <button onclick="cmd('start')">Enable</button>
    <button class="red" onclick="cmd('stop')">Stop</button>
  </div>
  <div class="row" style="margin-top:8px">
    <button class="ghost" onclick="cmd('reset')">Fault reset</button>
    <button class="ghost" onclick="cmd('scan')">Rescan drives</button>
  </div>
</section>

<section><h2>Drive feedback</h2>
  <div class="grid">
    <div class="m"><small>Commanded</small><b id="mhz">--<i>Hz</i></b></div>
    <div class="m"><small>Total amps</small><b id="mamp">--<i>A</i></b></div>
    <div class="m"><small>Speed cap</small><b id="mcap">--<i>Hz</i></b></div>
    <div class="m"><small>Flow</small><b id="mflow">--<i>gpm</i></b></div>
  </div>
  <div id="drives"></div>
</section>

<section><h2>Cycling</h2>
  <div class="grid">
    <div class="m"><small>Sleep cycles</small><b id="mcyc">--</b></div>
    <div class="m"><small>Shutoff speed</small><b id="mshz">--<i>Hz</i></b></div>
    <div class="m"><small>Uptime</small><b id="mup">--</b></div>
  </div>
  <p class="note">Full tuning, the operating-envelope plot and the loop
  diagnostics are on the <a href="/adv">Advanced</a> screen.</p>
</section>

</main><script>)HTML" NAV_JS R"HTML(
const SN=['Idle','Filling','Regulating','At cap','Charging','Asleep','Both pumps','FAULT'];
const CLS=['slp','run','run','bad','run','slp','run','bad'];
let hist=[],spSet=null,spT=null;

// ---- trend ---------------------------------------------------------------
// Two scales on purpose: pressure autoscales around the setpoint so small
// excursions are visible, speed is always drawn against a fixed 0-60 Hz so
// the eye can read absolute speed off the same picture every time.
function trendDraw(sp){
  if(hist.length<2){document.getElementById('trend').innerHTML='';return;}
  const W=320,H=74,n=hist.length;
  let lo=sp-8,hi=sp+8;
  for(const p of hist){lo=Math.min(lo,p[0]);hi=Math.max(hi,p[0]);}
  if(hi-lo<4)hi=lo+4;
  const x=i=>(i/(Math.max(n-1,1)))*W;
  const yp=v=>H-2-((v-lo)/(hi-lo))*(H-6);
  const yh=v=>H-2-(Math.min(v,60)/60)*(H-6);
  const path=(f,k)=>hist.map((p,i)=>(i?'L':'M')+x(i).toFixed(1)+' '+f(p[k]).toFixed(1)).join('');
  let o='';
  o+='<line x1="0" y1="'+yp(sp).toFixed(1)+'" x2="'+W+'" y2="'+yp(sp).toFixed(1)+
     '" stroke="rgba(255,255,255,.45)" stroke-dasharray="4 4" stroke-width="1"/>';
  o+='<path d="'+path(yh,1)+'" fill="none" stroke="rgba(255,255,255,.40)" stroke-width="1.2"/>';
  o+='<path d="'+path(yp,0)+'L'+W+' '+H+'L0 '+H+'Z" fill="rgba(255,255,255,.13)"/>';
  o+='<path d="'+path(yp,0)+'" fill="none" stroke="#fff" stroke-width="2"'+
     ' vector-effect="non-scaling-stroke" stroke-linejoin="round"/>';
  document.getElementById('trend').innerHTML=o;
}

// ---- setpoint ------------------------------------------------------------
// Debounced while dragging, immediate on release.  /set defaults every field
// it is not given to the value already held, so posting one key is safe.
function spShow(v){spbox.value=v;spsl.value=v;}
function bump(d){spShow(Math.min(100,Math.max(5,(+spbox.value||55)+d)));spPush();}
function spPush(){clearTimeout(spT);spT=setTimeout(async()=>{
  const v=Math.min(100,Math.max(5,+spbox.value||55));spShow(v);
  const b=new URLSearchParams();b.set('setpoint',v);
  await fetch('/set',{method:'POST',body:b});
  spSet=v;spmsg.textContent='Setpoint saved at '+v+' psi.';},350);}
spsl.addEventListener('input',()=>{spbox.value=spsl.value;spPush();});
spbox.addEventListener('change',()=>{spShow(spbox.value);spPush();});

async function cmd(c){
  const t=await(await fetch('/cmd?c='+c,{method:'POST'})).text();
  spmsg.textContent=t;}

function hhmm(s){const h=(s/3600)|0,m=((s%3600)/60)|0;
  return h?h+'h '+m+'m':m+'m '+((s%60)|0)+'s';}

async function tick(){try{
  const j=await(await fetch('/status')).json();
  ver.textContent='v'+j.ver;
  if(j.sim)simbar.style.display='block';
  if(spSet===null){const s=await(await fetch('/settings')).json();
    spSet=s.setpoint;spShow(Math.round(s.setpoint));spmsg.innerHTML='&nbsp;';}

  psi.textContent=j.psiValid?j.psi.toFixed(1):'--';
  tgt.textContent=j.spActive.toFixed(1);
  state.textContent=SN[j.state];state.className='pill '+CLS[j.state];

  let amps=0,rows='';
  for(const d of j.drives){
    if(!d.present)continue;
    amps+=d.amps;
    const bad=d.tripped||!d.commsOK;
    rows+='<div class="vfd"><div class="hd"><b>Pump '+d.n+'</b><span class="pill '+
      (bad?'bad':(d.running?'run':'slp'))+'">'+
      (!d.commsOK?'NO COMMS':(d.tripped?'TRIP '+d.tripCode:(d.running?'Running':'Stopped')))+
      '</span></div><div class="grid">'+
      '<div class="m"><small>Speed</small><b>'+d.hz.toFixed(1)+'<i>Hz</i></b></div>'+
      '<div class="m"><small>Current</small><b>'+d.amps.toFixed(1)+'<i>A</i></b></div>'+
      '<div class="m"><small>Address</small><b>'+d.addr+'</b></div></div></div>';}
  drives.innerHTML=rows||'<p class="note">No drives found. Try Rescan drives.</p>';

  mhz.innerHTML=j.hzCmd.toFixed(1)+'<i>Hz</i>';
  mamp.innerHTML=amps.toFixed(1)+'<i>A</i>';
  mcap.innerHTML=j.capHz.toFixed(1)+'<i>Hz</i>';
  mflow.innerHTML=j.flow.toFixed(1)+'<i>gpm</i>';
  mcyc.textContent=j.sleepCycles;
  mshz.innerHTML=j.shutoffHz.toFixed(1)+'<i>Hz</i>';
  mup.textContent=hhmm(j.up);

  let a=banner,txt,cls="ok";
  if(!j.psiValid){txt='PRESSURE READING INVALID — pumps held stopped';cls='bad';}
  else if(j.state==7){txt='FAULT — check the drives below';cls='bad';}
  else if(j.addr1){txt='Uncommissioned drive answering at address 1';cls='bad';}
  else if(!j.enable){txt='Stopped — press Enable to run';cls='ok';}
  else txt='Enabled — holding '+j.spActive.toFixed(1)+' psi';
  a.textContent=txt;a.className='alert '+cls;

  if(j.psiValid){hist.push([j.psi,j.hzCmd]);if(hist.length>160)hist.shift();}
  trendDraw(spSet||j.spActive);
}catch(e){banner.textContent='No link to the controller';banner.className='alert bad';}}

async function loadNode(){try{const n=await(await fetch('/net')).json();
  node.textContent='· '+n.node;}catch(e){}}
loadNode();tick();setInterval(tick,1000);
</script></body></html>)HTML";
