#pragma once
#include "ui_common.h"

/*
  Screen 1 -- Home.  The landing page and what the captive portal opens.

  What an operator does day to day and nothing else: what the pressure is,
  what it is being asked to hold, whether the pumps are running, and what the
  drives are actually reporting back.  Every tuning parameter is on Pump.

  Polls /status only.  /diag is ~50 fields and belongs to the screen that
  displays them.

  The drive bubbles carry the v0.8.3 distinction and it is the whole point of
  them: green means the drive answers reads AND accepts writes.  Amber means
  it answers but refuses writes -- a wrong P-12 or a locked parameter set,
  which reads as perfectly healthy on every other indicator.  That failure
  looked identical to a working drive before writeOK existed.

  Element ids never shadow a window property: a bare id is reachable as a
  global, but a real window property wins, so id="alert" would resolve to
  window.alert.  See DESIGN_NOTES.md.
*/

const char PAGE_HOME[] PROGMEM = R"HTML(<!doctype html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<title>PumpSaver</title><style>)HTML" CSS_BASE R"HTML(
.hero{background:linear-gradient(158deg,#17758f 0%,#0d4d61 62%,#0b4254 100%);
  color:#fff;border-radius:16px;padding:16px 17px 12px;margin-bottom:12px;
  box-shadow:0 3px 16px rgba(13,77,97,.30)}
.hero .lab{font-size:10.5px;letter-spacing:.1em;text-transform:uppercase;
  opacity:.72;font-weight:700}
.big{font-size:66px;font-weight:250;line-height:1;letter-spacing:-.035em;
  font-variant-numeric:tabular-nums;display:flex;align-items:baseline;gap:9px;margin:3px 0 8px}
.big u{font-size:19px;font-weight:600;text-decoration:none;opacity:.75;letter-spacing:0}
.hero .sub{font-size:13.5px;opacity:.95;display:flex;align-items:center;gap:10px;flex-wrap:wrap}
.hero .pill{background:rgba(255,255,255,.17);color:#fff}
.hero svg{width:100%;height:78px;display:block;margin-top:9px}
.hero .axl{display:flex;justify-content:space-between;font-size:10px;opacity:.6;
  margin-top:1px;font-variant-numeric:tabular-nums}
.spwrap{display:flex;align-items:center;gap:11px}
.spwrap input{width:104px;font-size:29px;font-weight:650;text-align:center;padding:8px;
  font-variant-numeric:tabular-nums;letter-spacing:-.02em;min-height:56px}
.stepb{width:56px;height:56px;flex:0 0 56px;font-size:27px;font-weight:300;padding:0;
  background:#eef2f5;color:var(--acc);border:1px solid var(--line);border-radius:13px}
.big2{display:flex;gap:9px}
.big2 button{flex:1;padding:17px;font-size:16.5px;min-height:56px}
.dcard{border:1px solid var(--line);border-radius:12px;padding:11px;margin-top:9px}
.dcard .hd{display:flex;align-items:center;gap:9px;margin-bottom:9px}
.dcard .hd b{font-size:14.5px;flex:1}
</style></head><body>)HTML" NAV_HTML R"HTML(
<main>

<div class="hero">
  <div class="lab">Header pressure</div>
  <div class="big"><span id="psi">--</span><u>psi</u></div>
  <div class="sub"><span class="pill" id="state">--</span>
    <span>target <b id="tgt">--</b> psi</span></div>
  <svg id="trend" viewBox="0 0 320 78" preserveAspectRatio="none"></svg>
  <div class="axl"><span id="tspan">&nbsp;</span><span id="tnow">&nbsp;</span></div>
</div>

<div class="alert ok" id="banner">connecting&hellip;</div>

<section><h2>Set pressure</h2>
  <div class="spwrap">
    <button class="stepb" onclick="bump(-1)">&minus;</button>
    <input type="number" id="spbox" min="5" max="100" step="1" inputmode="numeric">
    <button class="stepb" onclick="bump(1)">+</button>
    <span class="note" style="margin:0">psi</span>
  </div>
  <input type="range" id="spsl" min="5" max="100" step="1" style="margin-top:10px">
</section>

<section><h2>Run</h2>
  <div class="big2">
    <button onclick="cmd('start')">Enable</button>
    <button class="red" onclick="cmd('stop')">Stop</button>
  </div>
  <div class="row" style="margin-top:9px">
    <button class="ghost sm" onclick="cmd('reset')">Fault reset</button>
    <button class="ghost sm" onclick="cmd('scan')">Rescan drives</button>
  </div>
</section>

<section><h2>Drives</h2>
  <div id="drives"></div>
</section>

<section><h2>At a glance</h2>
  <div class="grid">
    <div class="m"><small>Commanded</small><b id="mhz">--<i>Hz</i></b></div>
    <div class="m"><small>Total amps</small><b id="mamp">--<i>A</i></b></div>
    <div class="m"><small>Speed cap</small><b id="mcap">--<i>Hz</i></b></div>
    <div class="m"><small>Shutoff</small><b id="mshz">--<i>Hz</i></b></div>
    <div class="m"><small>Flow (est)</small><b id="mflow">--<i>gpm</i></b></div>
    <div class="m"><small>Sleep cycles</small><b id="mcyc">--</b></div>
    <div class="m"><small>Uptime</small><b id="mup">--</b></div>
  </div>
  <p class="note">Tuning, the operating envelope and the loop diagnostics are on
  <a href="/pump">Pump</a>.</p>
</section>

</main><script>)HTML" NAV_JS R"HTML(
const SN=['Idle','Filling','Regulating','At cap','Charging','Asleep','Both pumps','FAULT'];
const CLS=['slp','run','run','warn','run','slp','run','bad'];
let hist=[],spSet=null,spT=null;

// ---- trend ---------------------------------------------------------------
// Two scales on purpose: pressure autoscales around the setpoint so a small
// excursion is actually visible, speed is always drawn against a fixed 0-60 Hz
// so the eye reads absolute speed off the same picture every time.
function trendDraw(sp){
  const el=$('trend');
  if(hist.length<2){el.innerHTML='';return;}
  const W=320,H=78,n=hist.length;
  let lo=sp-6,hi=sp+6;
  for(const p of hist){lo=Math.min(lo,p[0]);hi=Math.max(hi,p[0]);}
  if(hi-lo<4){const m=(hi+lo)/2;lo=m-2;hi=m+2;}
  // Flow gets its own autoscale with a sane floor, so a 2 gpm trickle is still
  // a visible line rather than being flattened against the axis by one earlier
  // 90 gpm peak.
  let qhi=10; for(const p of hist) qhi=Math.max(qhi,p[2]);
  const x=i=>i/Math.max(n-1,1)*W;
  const yp=v=>H-3-(v-lo)/(hi-lo)*(H-8);
  const yh=v=>H-3-Math.min(Math.max(v,0),60)/60*(H-8);
  const yq=v=>H-3-Math.min(Math.max(v,0),qhi)/qhi*(H-8);
  const path=(f,k)=>hist.map((p,i)=>(i?'L':'M')+x(i).toFixed(1)+' '+f(p[k]).toFixed(1)).join('');
  let o='';
  o+='<line x1="0" y1="'+yp(sp).toFixed(1)+'" x2="'+W+'" y2="'+yp(sp).toFixed(1)+
     '" stroke="rgba(255,255,255,.42)" stroke-dasharray="4 4"/>';
  o+='<path d="'+path(yh,1)+'" fill="none" stroke="rgba(255,255,255,.34)" stroke-width="1.2"/>';
  o+='<path d="'+path(yq,2)+'" fill="none" stroke="rgba(150,235,200,.85)" stroke-width="1.4"'+
     ' stroke-dasharray="3 2.5"/>';
  o+='<path d="'+path(yp,0)+'L'+W+' '+H+'L0 '+H+'Z" fill="rgba(255,255,255,.12)"/>';
  o+='<path d="'+path(yp,0)+'" fill="none" stroke="#fff" stroke-width="2"'+
     ' stroke-linejoin="round" vector-effect="non-scaling-stroke"/>';
  el.innerHTML=o;
  $('tspan').innerHTML='<b>&#9473;</b> psi '+lo.toFixed(0)+'&ndash;'+hi.toFixed(0)+
    ' &nbsp; <span style="opacity:.6">&#9473;</span> Hz 0&ndash;60';
  $('tnow').innerHTML='<span style="color:#96ebc8">&#9476;</span> gpm 0&ndash;'+qhi.toFixed(0)+
    ' &nbsp; '+n+'s';
}

// ---- setpoint ------------------------------------------------------------
// Trailing debounce: dragging fires one save on the pause, not one per frame.
// /set defaults every field it is not given to the value already stored, so
// posting the single key is safe and cannot disturb the tuning.
function spShow(v){$('spbox').value=v;$('spsl').value=v;}
function bump(d){spShow(Math.min(100,Math.max(5,(+$('spbox').value||55)+d)));spPush();}
function spPush(){clearTimeout(spT);spT=setTimeout(async()=>{
  const v=Math.min(100,Math.max(5,+$('spbox').value||55));spShow(v);
  await post('/set',{setpoint:v});spSet=v;toast('Setpoint '+v+' psi');},350);}

async function cmd(c){toast(await(await fetch('/cmd?c='+c,{method:'POST'})).text());}

// ---- drive cards ---------------------------------------------------------
// green = answers reads AND accepts writes; amber = answers but refuses
// writes; red = present and silent; grey = not found.
function driveCard(d){
  let cls='',lab,pill='slp';
  if(!d.present){cls='';lab='not found';}
  else if(!d.commsOK){cls='r';lab='no comms';pill='bad';}
  else if(!d.writeOK){cls='a';lab='read only — writes refused';pill='warn';}
  else {cls='g';lab=d.tripped?('TRIP '+d.tripCode):(d.running?'running':'stopped');
        pill=d.tripped?'bad':(d.running?'run':'slp');}
  if(d.tripped&&d.present){cls='r';pill='bad';lab='TRIP '+d.tripCode;}
  return '<div class="dcard"><div class="hd"><span class="dot2 '+cls+'"></span>'+
    '<b>Pump '+d.n+'</b><span class="pill '+pill+'">'+esc(lab)+'</span></div>'+
    '<div class="grid">'+
    '<div class="m"><small>Speed</small><b>'+d.hz.toFixed(1)+'<i>Hz</i></b></div>'+
    '<div class="m"><small>Current</small><b>'+d.amps.toFixed(1)+'<i>A</i></b></div>'+
    '<div class="m"><small>Modbus addr</small><b>'+d.addr+'</b></div>'+
    (d.writeFails?'<div class="m"><small>Write fails</small><b class="no">'+d.writeFails+'</b></div>':'')+
    '</div></div>';
}

async function tick(){
  const j=await chrome();
  if(!j){$('banner').textContent='No link to the controller';
         $('banner').className='alert bad';return;}

  if(spSet===null){const s=await(await fetch('/settings',{cache:'no-store'})).json();
    spSet=s.setpoint;spShow(Math.round(s.setpoint));}

  $('psi').textContent=j.psiValid?j.psi.toFixed(1):'--';
  $('tgt').textContent=j.spActive.toFixed(1);
  $('state').textContent=SN[j.state];$('state').className='pill '+CLS[j.state];

  let amps=0,rows='';
  for(const d of j.drives){if(d.present)amps+=d.amps;rows+=driveCard(d);}
  $('drives').innerHTML=rows;

  $('mhz').innerHTML=j.hzCmd.toFixed(1)+'<i>Hz</i>';
  $('mamp').innerHTML=amps.toFixed(1)+'<i>A</i>';
  $('mcap').innerHTML=j.capHz.toFixed(1)+'<i>Hz</i>';
  $('mshz').innerHTML=j.shutoffHz.toFixed(1)+'<i>Hz</i>';
  $('mflow').innerHTML=j.flowEst.toFixed(1)+'<i>gpm</i>';
  $('mcyc').textContent=j.sleepCycles;
  $('mup').textContent=hhmm(j.up);

  let txt,cls='ok';
  if(!j.psiValid){txt='Pressure reading invalid — pumps held stopped';cls='bad';}
  else if(j.state==7){txt='Fault — check the drives above';cls='bad';}
  else if(j.ovr){txt='Max-Hz override engaged at '+j.ovrPct+'% — cap bypassed';cls='warn';}
  else if(j.addr1){txt='Uncommissioned drive answering at address 1';cls='warn';}
  else if(!j.enable){txt=j.sim?'Simulation ready — press Enable to start the pump'
                            :'Stopped — press Enable to run';cls='ok';}
  else txt='Enabled — holding '+j.spActive.toFixed(1)+' psi';
  $('banner').textContent=txt;$('banner').className='alert '+cls;

  if(j.psiValid){hist.push([j.psi,j.hzCmd,j.flowEst]);if(hist.length>150)hist.shift();}
  trendDraw(spSet||j.spActive);
}

$('spsl').addEventListener('input',()=>{$('spbox').value=$('spsl').value;spPush();});
$('spbox').addEventListener('change',()=>{spShow($('spbox').value);spPush();});
async function devName(){try{const n=await(await fetch('/net',{cache:'no-store'})).json();
  $('devname').textContent=n.id?('unit '+n.id):'';}catch(e){}}
devName();tick();setInterval(tick,1000);
</script></body></html>)HTML";
