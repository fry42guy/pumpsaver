#pragma once
#include "ui_common.h"

/*
  Screen 3 -- Simulation.

  Split out of the System card once it grew plant controls, a pump count,
  per-drive register readouts and fault injection.  System is about the box;
  this is about pretending to be a skid.

  What is simulated is the PLANT and the DRIVES.  The control block, the sleep
  state machine and the staging logic are the real ones, unmodified -- that is
  the entire value of the thing.  A simulator that reimplements the logic it is
  meant to be testing proves nothing.

  The register table matters more than it looks.  Before 0.13.0 the simulated
  drives reported a status word of 0x0000 forever, so trips, the ready bit and
  the trip code were dead on every screen and could not be exercised at all.
  Now the sim BUILDS a status word and decodes it through the same function the
  Modbus path uses, so what you see here is the same code that runs on a wire.
*/

const char PAGE_SIM[] PROGMEM = R"HTML(<!doctype html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<title>PumpSaver &middot; Sim</title><style>)HTML" CSS_BASE R"HTML(
.reg{display:grid;grid-template-columns:1fr auto;gap:2px 12px;font-size:12px;
  font-variant-numeric:tabular-nums;margin-top:4px}
.reg span{color:var(--mut)}
.reg b{font-weight:650;text-align:right}
.dcard{border:1px solid var(--line);border-radius:12px;padding:11px;margin-top:9px}
.dcard .hd{display:flex;align-items:center;gap:9px;margin-bottom:8px}
.dcard .hd b{font-size:14.5px;flex:1}
.bits{display:flex;gap:6px;flex-wrap:wrap;margin-top:7px}
.bit{font-size:10.5px;font-weight:700;letter-spacing:.04em;padding:3px 8px;
  border-radius:6px;background:#eef1f4;color:var(--mut);text-transform:uppercase}
.bit.on{background:var(--okw);color:var(--ok)}
.bit.bad{background:var(--badw);color:var(--bad)}
.inj{display:grid;grid-template-columns:1fr 92px auto;gap:8px;align-items:center;margin-top:9px}
.inj input{text-align:center}
</style></head><body>)HTML" NAV_HTML R"HTML(
<main>

<section><h2>Simulation</h2>
<div class="alert ok" id="simst" style="margin-bottom:11px">&hellip;</div>
<div class="fld"><span class="k">Simulate the plant<i>real control logic, modelled pump and drives</i></span>
<label class="sw"><input type="checkbox" id="simOn"><span></span></label></div>
<div class="fld"><span class="k">Pumps on the skid</span>
<select id="simDrives"><option value="1">1 &mdash; lead only</option>
<option value="2">2 &mdash; lead + lag</option></select></div>
<p class="note"><b>Turning simulation on stops the pumps</b> and writes stop / 0 Hz
to any real drive on the bus first, so a drive is never left running against
invented feedback, and never left to trip on its own comms watchdog. Turning it
off rescans the bus.</p>
</section>

<section id="simpanel"><h2>Plant</h2>
)HTML" SIM_PANEL_HTML R"HTML(
<p class="note">Nothing moves until the pump is enabled &mdash; with the pump
stopped the header correctly sits at 0 psi. Press <b>Enable</b> on
<a href="/">Home</a>, then watch the operating point track across the envelope
plot on <a href="/pump">Pump</a>.</p>
</section>

<section id="drivesec"><h2>What the drives are reporting</h2>
<div id="drives"></div>
<p class="note">These are the same fields a real E3 returns in registers 6, 7, 8,
20 and 24, decoded by the same code. Injecting a trip on the <b>lead</b> drive
also invalidates the pressure transducer &mdash; it lives on that drive's AI1
&mdash; so the loop freezes and stops the pumps, exactly as it would in the
field. <b>Fault reset</b> on Home clears it.</p>
</section>

</main><script>)HTML" NAV_JS R"HTML(
)HTML" SIM_PANEL_JS R"HTML(
let simDirty=false;

const hex4=n=>'0x'+(n&0xFFFF).toString(16).toUpperCase().padStart(4,'0');

function driveCard(d,st){
  const fitted=d.present;
  let cls='',pill='slp',lab;
  if(!fitted)            {lab='not fitted';}
  else if(d.tripped)     {cls='r';pill='bad';lab='TRIP '+d.tripCode;}
  else if(d.running)     {cls='g';pill='run';lab='running';}
  else                   {cls='';pill='slp';lab='stopped';}
  const b=(t,on,bad)=>'<span class="bit'+(on?(bad?' bad':' on'):'')+'">'+t+'</span>';
  return '<div class="dcard"><div class="hd"><span class="dot2 '+cls+'"></span>'+
    '<b>Pump '+d.n+'</b><span class="pill '+pill+'">'+esc(lab)+'</span></div>'+
    '<div class="bits">'+b('run',d.running)+b('trip',d.tripped,1)+b('ready',d.ready)+
      b('comms',d.commsOK)+b('writes',d.writeOK)+'</div>'+
    '<div class="reg">'+
      '<span>status word (reg 6)</span><b>'+hex4(d.status)+'</b>'+
      '<span>trip code</span><b>'+d.tripCode+'</b>'+
      '<span>speed (reg 7)</span><b>'+d.hz.toFixed(1)+' Hz</b>'+
      '<span>current (reg 8)</span><b>'+d.amps.toFixed(1)+' A</b>'+
      '<span>heatsink (reg 24)</span><b>'+d.tempC.toFixed(0)+' &deg;C</b>'+
      '<span>AI1 raw (reg 20)</span><b>'+d.ai+' / 1000</b>'+
      '<span>Modbus address</span><b>'+d.addr+'</b>'+
    '</div>'+
    (fitted&&st.sim?'<div class="inj">'+
      '<span class="note" style="margin:0">Inject trip code</span>'+
      '<input type="number" id="tc'+d.n+'" min="1" max="255" value="'+(d.tripCode||24)+'" inputmode="numeric">'+
      '<button class="ghost sm" onclick="inject('+d.n+')">'+
        (d.tripped?'Clear':'Trip')+'</button></div>':'')+
    '</div>';
}

async function inject(n){
  const d=(document.querySelector('#tc'+n)||{}).value||24;
  const cur=(window.LAST&&window.LAST.drives[n-1].tripped)?0:d;
  toast(await post('/fault',{drive:n,code:cur}));
  setTimeout(loadSim,300);
}

async function loadSim(){
  const st=await chrome();
  if(!st){$('simst').textContent='No link to the controller';
          $('simst').className='alert bad';return;}
  window.LAST=st;
  if(!simDirty){
    const s=await(await fetch('/settings',{cache:'no-store'})).json();
    $('simOn').checked=(+s.simOn)>0;
    $('simDrives').value=s.simDrives;
  }
  const on=$('simOn').checked;
  $('simpanel').style.display=on?'block':'none';

  let txt,cls='ok';
  if(!on)             txt='Simulation off — running against real drives over Modbus';
  else if(!st.psiValid){txt='Simulation ON — pressure INVALID (lead drive tripped). Loop frozen, pumps held.';cls='bad';}
  else if(!st.enable) {txt='Simulation ON, '+$('simDrives').value+' pump(s) — pump is STOPPED, so the header sits at 0 psi. Press Enable on Home.';cls='warn';}
  else                {txt='Simulation ON — '+st.psi.toFixed(1)+' psi, '+st.flow.toFixed(1)+
                           ' gpm, '+st.hzCmd.toFixed(1)+' Hz';cls='warn';}
  $('simst').textContent=txt;$('simst').className='alert '+cls;

  if(on)simTick(st);
  $('drives').innerHTML=st.drives.map(d=>driveCard(d,st)).join('');
}

async function saveSim(){
  simDirty=false;
  toast(await post('/sim',{on:swVal($('simOn')),drives:$('simDrives').value}));
  setTimeout(loadSim,600);
}
$('simOn').addEventListener('change',saveSim);
$('simDrives').addEventListener('change',saveSim);

simLoad();loadSim();setInterval(loadSim,1000);
</script></body></html>)HTML";
