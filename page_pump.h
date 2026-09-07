#pragma once
#include "ui_common.h"

/*
  Screen 2 -- Pump.  Every parameter the control block has, the operating
  envelope, the loop diagnostics, the diagnostic max-Hz override, and the sim.

  Ordered by how often it is touched: live readouts, run controls and the
  override, then the envelope, then diagnostics, then the settings form.

  The long explanatory paragraphs that used to sit between the fields are
  gone.  The reasoning they carried lives in DESIGN_NOTES.md, where it can be
  kept accurate; a one-line hint under a field is kept only where the field is
  genuinely unguessable.  What stays on the page is anything that is a TOOL
  rather than prose -- the free-flow solver, the cap-table validity flag, the
  sim sliders.

  Booleans are switches here but still POST as 1/0, so /set is untouched.
*/

const char PAGE_PUMP[] PROGMEM = R"HTML(<!doctype html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<title>PumpSaver &middot; Pump</title><style>)HTML" CSS_BASE R"HTML(
.tiles{display:grid;grid-template-columns:repeat(3,1fr);gap:9px;margin-bottom:12px}
.t{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:10px 11px;
  box-shadow:var(--sh)}
.t small{display:block;color:var(--mut);font-size:9.5px;letter-spacing:.075em;
  text-transform:uppercase;font-weight:700}
.t b{font-size:21px;font-variant-numeric:tabular-nums;font-weight:650;letter-spacing:-.02em}
.cap{display:grid;grid-template-columns:24px 1fr 1fr;gap:7px;align-items:center;font-size:13px}
.cap input{padding:8px;min-height:40px;text-align:right;font-variant-numeric:tabular-nums}
.cap i{font-style:normal;color:var(--mut);font-size:11px;text-align:center}
.dg{display:grid;grid-template-columns:1fr 1fr;gap:0 16px;font-size:12px;
  font-variant-numeric:tabular-nums}
.dg>div{display:flex;justify-content:space-between;align-items:center;gap:6px;
  flex-wrap:wrap;padding:3px 0;border-bottom:1px solid var(--line2)}
.dg span:first-child{color:var(--mut)}
.dg b{font-weight:650}
.bar{height:3px;background:#e6eaee;border-radius:2px;overflow:hidden;flex-basis:100%;margin-top:3px}
.bar i{display:block;height:100%;background:var(--acc)}
.tool{background:#f5f7f9;border:1px solid var(--line2);border-radius:11px;padding:11px;margin-top:10px}
.tool .three{display:grid;grid-template-columns:1fr 1fr 1fr;gap:7px}
.tool input{text-align:center;padding:8px;min-height:42px}
.inj3{display:grid;grid-template-columns:96px 1fr auto;gap:8px;align-items:center}
</style></head><body>)HTML" NAV_HTML R"HTML(
<main>

<div class="tiles">
<div class="t"><small>Pressure</small><b id="psi">--</b></div>
<div class="t"><small>Target</small><b id="spa">--</b></div>
<div class="t"><small>State</small><b id="stt" style="font-size:14px">--</b></div>
<div class="t"><small>Cmd Hz</small><b id="cmdhz">--</b></div>
<div class="t"><small>Cap Hz</small><b id="caphz">--</b></div>
<div class="t"><small>Shutoff</small><b id="shz">--</b></div>
</div>
<div class="alert ok" id="st">connecting&hellip;</div>

<section><h2>Run</h2>
<div class="row">
<button onclick="sendCmd('start')">Enable</button>
<button class="red" onclick="sendCmd('stop')">Stop</button>
<button class="ghost sm" onclick="sendCmd('reset')">Fault reset</button>
<button class="ghost sm" onclick="sendCmd('scan')">Rescan</button>
</div>
<div class="fld" style="margin-top:12px"><span class="k">Setpoint<i>psi the loop holds</i></span>
<input type="number" id="qspv" step="1" min="5" max="100" inputmode="numeric"></div>
<input type="range" id="qsp" min="5" max="100" step="1">

<h3>Max-Hz override &mdash; diagnostic</h3>
<div class="fld"><span class="k">Engage override<i>replaces the cavitation cap and Max&nbsp;Hz; floor drops to 0; sleep suspended; never saved</i></span>
<label class="sw"><input type="checkbox" id="ovon"><span></span></label></div>
<div class="sl"><div class="lab"><span>Ceiling</span><span id="ovhz">&mdash;</span></div>
<input type="range" id="ovs" min="0" max="100" step="1" value="100">
<input type="number" id="ovsv" min="0" max="100" step="1" value="100"></div>
</section>

<section><h2>Operating envelope</h2>
<div style="overflow-x:auto"><svg id="g" viewBox="0 0 520 300"
  style="width:100%;min-width:390px;height:auto"></svg></div>
<div class="note">
<span style="color:#b4431a">&#9473;</span> cavitation cap &nbsp;
<span style="color:#b4431a">&#9476;</span> <span id="lgq">--</span> gpm onset &nbsp;
<span style="color:#4a6fa5">&#9473;</span> shutoff &nbsp;
<span style="color:#141d29">&#9679;</span> now
<div id="marg" style="margin-top:4px"></div></div>
</section>

<section><h2>Diagnostics</h2><div class="dg" id="dg"></div></section>

<section><h2>Drives</h2>
<table><thead><tr><th>#</th><th>Addr</th><th>Read</th><th>Write</th><th>Hz</th><th>A</th><th>State</th></tr></thead>
<tbody id="drv"></tbody></table>
</section>

<section id="simsec" style="display:none"><h2>Simulated plant</h2>
<p class="note" style="margin-top:0">Switch simulation on and off, and pick 1 or 2 pumps,
on <a href="/sim">Sim</a>.</p>
)HTML" SIM_PANEL_HTML R"HTML(
</section>

<form id="f" onsubmit="return save()">

<section><h2>Loop</h2>
<div class="fld"><span class="k">Setpoint<i>psi</i></span><input type="number" step="0.5" name="setpoint"></div>
<div class="fld"><span class="k">Kp<i>Hz per psi</i></span><input type="number" step="0.01" name="kp"></div>
<div class="fld"><span class="k">Ki<i>Hz per psi&middot;s</i></span><input type="number" step="0.01" name="ki"></div>
<div class="fld"><span class="k">Min Hz</span><input type="number" step="0.5" name="minHz"></div>
<div class="fld"><span class="k">Max Hz</span><input type="number" step="0.5" name="maxHz"></div>
<div class="fld"><span class="k">Fill ramp<i>psi/s</i></span><input type="number" step="0.5" name="spRampPsiS"></div>
<div class="fld"><span class="k">Fill preload<i>psi above live pressure</i></span><input type="number" step="1" name="spStepPsi"></div>
<div class="fld"><span class="k">Transducer span<i>psi at 20&nbsp;mA</i></span><input type="number" step="5" name="xdcrSpanPsi"></div>
</section>

<section><h2>Pump profile</h2>
<div class="fld wide"><span class="k">Load a profile<i>templates are starting points, not measurements</i></span>
<select id="profSel"></select></div>
<div class="row" style="margin-top:9px">
<button type="button" onclick="profApplySel()">Load into settings</button>
<button type="button" class="ghost sm" onclick="profShow()">Export</button>
<button type="button" class="ghost sm" onclick="$('profIo').style.display='block'">Import</button>
</div>
<p class="note">Loading a profile sets the pump curve, speed limits, cap table and
nameplate. It deliberately leaves <b>setpoint, sleep and staging</b> alone &mdash;
those belong to the installation, not to the pump.</p>

<h3>This pump</h3>
<div class="fld"><span class="k">Name<i>e.g. Price 5 HP 460 V</i></span>
<input name="pumpName" maxlength="27"></div>
<div class="fld"><span class="k">Motor<i>hp</i></span><input type="number" step="0.5" name="motorHp"></div>
<div class="fld"><span class="k">Volts</span><input type="number" step="10" name="motorVolts"></div>
<div class="fld"><span class="k">Nameplate FLA<i>amps; measured current is checked against this</i></span>
<input type="number" step="0.1" name="motorFLA"></div>
<div class="fld"><span class="k">Rated rpm</span><input type="number" step="10" name="motorRPM"></div>

<h3>Save the current settings as a profile</h3>
<div class="inj3">
<select id="profSlot"></select>
<input id="profName" maxlength="27" placeholder="profile name">
<button type="button" class="ghost sm" onclick="profSave()">Save</button>
</div>

<div id="profIo" style="display:none">
<textarea id="profTa" placeholder="Paste a profile JSON here, then Apply" style="margin-top:9px"></textarea>
<div class="row"><button type="button" class="ghost sm" onclick="profApplyJson()">Apply pasted profile</button></div>
</div>
</section>

<section><h2>Pump curve</h2>
<div class="fld"><span class="k">Shutoff at 60 Hz<i>deadhead psi</i></span><input type="number" step="1" name="shutoffPsiAt60"></div>
<div class="fld"><span class="k">Free flow at 60 Hz<i>gpm at zero head</i></span><input type="number" step="1" name="qMax60"></div>
<div class="fld"><span class="k">Cavitation onset<i>gpm</i></span><input type="number" step="1" name="cavOnsetGPM"></div>
<div class="tool">
<div style="font-size:12.5px;font-weight:700;margin-bottom:7px">Solve free flow from one measured point</div>
<div class="three"><input type="number" id="fq" placeholder="Hz"><input type="number" id="fp" placeholder="psi"><input type="number" id="fg" placeholder="gpm"></div>
<div class="row" style="margin-top:9px;align-items:center">
<button type="button" class="ghost sm" onclick="solveQ()">Compute</button>
<span id="fout" style="font-size:12px"></span></div>
</div>
</section>

<section><h2>Cavitation cap</h2>
<div class="cap"><i>Pt</i><i>psi</i><i>max Hz</i>
<i>1</i><input type="number" name="cp0" step="1"><input type="number" name="ch0" step="0.5">
<i>2</i><input type="number" name="cp1" step="1"><input type="number" name="ch1" step="0.5">
<i>3</i><input type="number" name="cp2" step="1"><input type="number" name="ch2" step="0.5">
<i>4</i><input type="number" name="cp3" step="1"><input type="number" name="ch3" step="0.5">
</div>
<div class="fld" style="margin-top:8px"><span class="k">Points used</span><input type="number" name="capPts" min="2" max="4"></div>
<div id="captab" class="note"></div>
</section>

<section><h2>Sleep</h2>
<div class="fld"><span class="k">Threshold from shutoff<i>tracks the setpoint instead of a fixed Hz</i></span>
<label class="sw"><input type="checkbox" name="sleepRelShutoff"><span></span></label></div>
<div class="fld"><span class="k">Margin above shutoff<i>Hz</i></span><input type="number" step="0.05" name="sleepHzMargin"></div>
<div class="fld"><span class="k">Delay<i>s, both phases</i></span><input type="number" step="1" name="sleepDlyS"></div>
<div class="fld"><span class="k">Phase 1 Hz<i>fixed mode only</i></span><input type="number" step="0.5" name="sleepHz"></div>
<div class="fld"><span class="k">Phase 2 offset<i>fixed mode only</i></span><input type="number" step="0.5" name="sleepHz2"></div>
<div class="fld"><span class="k">Band<i>psi</i></span><input type="number" step="0.5" name="sleepBand"></div>
<div class="fld"><span class="k">Charge<i>psi above setpoint, 0 = off</i></span><input type="number" step="0.5" name="sleepBoost"></div>
<div class="fld"><span class="k">Charge ramp<i>s</i></span><input type="number" step="0.5" name="chargeRampS"></div>
<div class="fld"><span class="k">Charge max<i>s</i></span><input type="number" step="1" name="boostMaxS"></div>
<div class="fld"><span class="k">Wake drop<i>psi below setpoint</i></span><input type="number" step="0.5" name="wakeDrop"></div>
<div class="fld"><span class="k">Min sleep<i>s</i></span><input type="number" step="1" name="sleepMinS"></div>
<div class="fld"><span class="k">Flow meter fitted<i>uses gpm instead of speed to judge idle</i></span>
<label class="sw"><input type="checkbox" name="useFlow"><span></span></label></div>
<div class="fld"><span class="k">Idle<i>gpm</i></span><input type="number" step="0.5" name="idleGPM"></div>
<div class="fld"><span class="k">Wake<i>gpm</i></span><input type="number" step="0.5" name="wakeGPM"></div>
</section>

<section><h2>Staging</h2>
<div class="fld"><span class="k">Stage up below SP&minus;<i>psi</i></span><input type="number" step="0.5" name="stageUpPsi"></div>
<div class="fld"><span class="k">Stage up delay<i>s</i></span><input type="number" step="1" name="stageUpDlyS"></div>
<div class="fld"><span class="k">Stage down<i>Hz</i></span><input type="number" step="0.5" name="stageDownHz"></div>
<div class="fld"><span class="k">Stage down delay<i>s</i></span><input type="number" step="1" name="stageDownDlyS"></div>
<div class="fld"><span class="k">Lag min run<i>s</i></span><input type="number" step="1" name="lagMinRunS"></div>
</section>
</form>

<div class="savebar">
<button class="ghost" type="button" onclick="loadS()">Revert</button>
<button type="button" onclick="save()">Save settings</button>
</div>

</main><script>)HTML" NAV_JS R"HTML(
const f=$('f');
const SN=['idle','fill','regulate','capped','charging','asleep','staged','FAULT'];
let SET=null,trail=[];

// Checkboxes are cosmetic: serialise them back to the 1/0 the endpoint expects.
function formBody(el){const o={};
  for(const x of el.elements){if(!x.name)continue;
    o[x.name]=(x.type=='checkbox')?swVal(x):x.value;}
  return o;}

// ---- operating envelope -------------------------------------------------
// x = header pressure, y = speed. Above the cap curve is forbidden; below the
// shutoff curve the pump is deadheaded and delivering nothing. The dashed
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
  const px=p=>ML+p/xM*(W-ML-MR), py=h=>H-MB-Math.min(h,yM)/yM*(H-MT-MB);
  const pth=a=>a.map((v,i)=>(i?'L':'M')+px(v[0]).toFixed(1)+' '+py(v[1]).toFixed(1)).join('');
  let cap=[],sho=[],iso=[];
  for(let i=0;i<=N;i++){const p=xM*i/N;
    cap.push([p,capAt(p,cp,ch,n)]);
    sho.push([p,60*Math.sqrt(p/sh)]);
    iso.push([p,60*Math.sqrt(Math.pow(ql/q0,2)+p/sh)]);}
  let o='';
  o+='<path d="'+pth(cap)+'L'+px(xM)+' '+py(yM)+'L'+px(0)+' '+py(yM)+'Z" fill="#b4431a" opacity=".10"/>';
  o+='<path d="'+pth(sho)+'L'+px(xM)+' '+py(0)+'L'+px(0)+' '+py(0)+'Z" fill="#4a6fa5" opacity=".10"/>';
  for(let p=0;p<=xM;p+=10){o+='<line x1="'+px(p)+'" y1="'+py(0)+'" x2="'+px(p)+'" y2="'+py(yM)+'" stroke="#e4e8ec"/>'
    +'<text x="'+px(p)+'" y="'+(H-9)+'" font-size="9" fill="#68737f" text-anchor="middle">'+p+'</text>';}
  for(let h=0;h<=yM;h+=10){o+='<line x1="'+px(0)+'" y1="'+py(h)+'" x2="'+px(xM)+'" y2="'+py(h)+'" stroke="#e4e8ec"/>'
    +'<text x="'+(ML-5)+'" y="'+(py(h)+3)+'" font-size="9" fill="#68737f" text-anchor="end">'+h+'</text>';}
  o+='<text x="'+(W/2)+'" y="'+(H-1)+'" font-size="9" fill="#68737f" text-anchor="middle">header psi</text>';
  o+='<text x="9" y="'+(MT+8)+'" font-size="9" fill="#68737f">Hz</text>';
  o+='<line x1="'+px(0)+'" y1="'+py(s.minHz)+'" x2="'+px(xM)+'" y2="'+py(s.minHz)+'" stroke="#9aa4ae" stroke-dasharray="2 3"/>';
  o+='<line x1="'+px(s.setpoint)+'" y1="'+py(0)+'" x2="'+px(s.setpoint)+'" y2="'+py(yM)+'" stroke="#12657f" stroke-dasharray="4 3" opacity=".7"/>';
  o+='<path d="'+pth(iso)+'" fill="none" stroke="#b4431a" stroke-width="1.4" stroke-dasharray="5 4"/>';
  o+='<path d="'+pth(sho)+'" fill="none" stroke="#4a6fa5" stroke-width="1.6"/>';
  o+='<path d="'+pth(cap)+'" fill="none" stroke="#b4431a" stroke-width="2.2"/>';
  if(trail.length>1)o+='<path d="'+pth(trail)+'" fill="none" stroke="#141d29" stroke-width="1" opacity=".35"/>';
  if(j.state!=0)o+='<circle cx="'+px(j.psi)+'" cy="'+py(j.hzCmd)+'" r="5" fill="#141d29"/>';
  $('g').innerHTML=o;
  $('lgq').textContent=ql.toFixed(0);
  let worst=0,at=0;
  for(let i=0;i<=N;i++){const q=flowAt(cap[i][1],cap[i][0],q0,sh);if(q>worst){worst=q;at=cap[i][0];}}
  const live=flowAt(j.hzCmd,j.psi,q0,sh);
  $('marg').innerHTML='Cap peaks at <b>'+worst.toFixed(0)+' gpm</b> near '+at.toFixed(0)+
    ' psi &mdash; '+(ql-worst).toFixed(0)+' gpm margin<br>Now <b>'+live.toFixed(0)+
    ' gpm</b> at '+j.hzCmd.toFixed(1)+' Hz, '+j.psi.toFixed(1)+' psi';
}

// ---- diagnostics --------------------------------------------------------
const R=(k,v,c)=>'<div><span>'+k+'</span><b class="'+(c||'')+'">'+v+'</b></div>';
const B=x=>x?'<span class="ok">yes</span>':'<span class="no">no</span>';
const T=(a,p)=>'<div><span>'+a[0]+'</span><b>'+a[1].toFixed(1)+' / '+p.toFixed(0)+
  's</b><div class="bar"><i style="width:'+Math.min(100,p>0?a[1]/p*100:0).toFixed(0)+'%"></i></div></div>';
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
  o+=R('integrator pinned',d.iAtFloor?'at FLOOR':(d.iAtCap?'at CAP':'no'),(d.iAtFloor||d.iAtCap)?'warn':'ok');
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
  $('dg').innerHTML=o;
}

// ---- poll ---------------------------------------------------------------
let ovDrag=false;
async function tick(){
  const j=await chrome();
  if(!j){$('st').textContent='No link to the controller';$('st').className='alert bad';return;}
  if(j.sim){$('simsec').style.display='block';}
  $('psi').textContent=j.psi.toFixed(1);$('spa').textContent=j.spActive.toFixed(1);
  $('stt').textContent=SN[j.state];$('cmdhz').textContent=j.hzCmd.toFixed(1);
  $('caphz').textContent=j.capHz.toFixed(1);$('shz').textContent=j.shutoffHz.toFixed(1);

  let r='';
  for(const d of j.drives){
    const rd=d.present?(d.commsOK?'<span class="ok">ok</span>':'<span class="no">lost</span>'):'&mdash;';
    const wr=d.present?(d.writeOK?'<span class="ok">ok</span>':
        '<span class="no">no'+(d.writeFails?' ('+d.writeFails+')':'')+'</span>'):'&mdash;';
    r+='<tr><td>'+d.n+'</td><td>'+d.addr+'</td><td>'+rd+'</td><td>'+wr+'</td><td>'+
      d.hz.toFixed(1)+'</td><td>'+d.amps.toFixed(1)+'</td><td>'+
      (d.tripped?('TRIP '+d.tripCode):(d.running?'run':'stop'))+'</td></tr>';}
  $('drv').innerHTML=r;

  let s=j.enable?'Enabled':'Disabled',cls='ok';
  if(!j.psiValid){s='Pressure invalid — pumps held';cls='bad';}
  else if(j.state==7){s='FAULT';cls='bad';}
  else if(j.ovr){s='Override engaged at '+j.ovrPct+'% — cap bypassed';cls='warn';}
  else if(j.addr1){s+=' — uncommissioned drive at address 1';cls='warn';}
  else s+=' — sleep cycles '+j.sleepCycles;
  $('st').textContent=s;$('st').className='alert '+cls;

  if(!ovDrag){$('ovon').checked=j.ovr;$('ovs').value=j.ovrPct;$('ovsv').value=j.ovrPct;}
  $('ovhz').textContent=(j.ovrPct*0.6).toFixed(1)+' Hz';

  if(SET){if(j.state!=0&&j.psiValid){trail.push([j.psi,j.hzCmd]);if(trail.length>300)trail.shift();}
          drawGraph(j,SET);}
  if(j.sim)simTick(j);
  drawDiag(await(await fetch('/diag',{cache:'no-store'})).json());
}

// Back-solve free flow from one measured point: Q = q0*r*sqrt(1-P/(S*r^2))
function solveQ(){const hz=+$('fq').value,p=+$('fp').value,g=+$('fg').value;
 const sh=+f.elements.shutoffPsiAt60.value,r=hz/60,h0=sh*r*r;
 if(!(hz>0&&g>0)){$('fout').innerHTML='<span class="no">need Hz and gpm</span>';return;}
 if(p>=h0){$('fout').innerHTML='<span class="no">'+p+' psi is at or above this speed shutoff ('
  +h0.toFixed(1)+' psi) &mdash; no flow is possible there</span>';return;}
 const q0=g/(r*Math.sqrt(1-p/h0));
 f.elements.qMax60.value=q0.toFixed(0);
 $('fout').innerHTML='<span class="ok">free flow &asymp; '+q0.toFixed(0)+' gpm &mdash; Save to keep</span>';}

async function loadS(){const j=await(await fetch('/settings',{cache:'no-store'})).json();SET=j;
 for(const k in j){const el=f.elements[k];if(!el)continue;
   if(el.type=='checkbox')el.checked=(+j[k])>0;else el.value=j[k];}
 for(let i=0;i<4;i++){f.elements['cp'+i].value=j.capPsi[i];f.elements['ch'+i].value=j.capHz[i];}
 $('captab').innerHTML=j.capTableOK?'Table valid.':
   '<b class="no">Cap table not monotonic (or row 1 below Min Hz) — cap is parked at row 1.</b>';
 $('qsp').value=Math.round(j.setpoint);$('qspv').value=Math.round(j.setpoint);}

async function save(){toast(await post('/set',formBody(f)));trail=[];await loadS();return false;}
async function sendCmd(c){toast(await(await fetch('/cmd?c='+c,{method:'POST'})).text());}

// ---- quick setpoint ------------------------------------------------------
let qT=null;
function qPush(){clearTimeout(qT);qT=setTimeout(async()=>{
 toast(await post('/set',{setpoint:$('qsp').value}));trail=[];await loadS();},300);}
$('qsp').addEventListener('input',()=>{$('qspv').value=$('qsp').value;});
$('qsp').addEventListener('change',()=>{$('qspv').value=$('qsp').value;qPush();});
$('qspv').addEventListener('change',()=>{$('qsp').value=$('qspv').value;qPush();});

// ---- override ------------------------------------------------------------
// Applies on release, never while dragging: this defeats a protection, so it
// should not sweep the pump through every value on the way to the one meant.
function ovEcho(){$('ovsv').value=$('ovs').value;$('ovhz').textContent=(+$('ovs').value*0.6).toFixed(1)+' Hz';}
async function ovPush(){ovDrag=false;
 toast(await post('/ovr',{on:swVal($('ovon')),pct:$('ovs').value}));}
$('ovs').addEventListener('input',()=>{ovDrag=true;ovEcho();});
$('ovs').addEventListener('change',ovPush);
$('ovsv').addEventListener('change',()=>{$('ovs').value=$('ovsv').value;ovEcho();ovPush();});
$('ovon').addEventListener('change',ovPush);

// ---- simulated plant (shared with System) ---------------------------------
)HTML" SIM_PANEL_JS R"HTML(



// ---- pump profiles --------------------------------------------------------
// Built-ins and user slots come back as one list with ids like "b0" / "u2",
// so adding a built-in later can never repoint a saved reference at a
// different pump.
async function profLoadList(){
  const j=await(await fetch('/profiles',{cache:'no-store'})).json();
  let o='<optgroup label="Templates">';
  for(const p of j.builtin)o+='<option value="'+p.id+'">'+esc(p.name)+'</option>';
  o+='</optgroup>';
  if(j.user.length){o+='<optgroup label="Saved">';
    for(const p of j.user)o+='<option value="'+p.id+'">'+esc(p.name)+'</option>';
    o+='</optgroup>';}
  $('profSel').innerHTML=o;
  let sl='';
  for(let i=0;i<j.slots;i++){
    const u=j.user.find(x=>x.slot===i);
    sl+='<option value="'+i+'">Slot '+(i+1)+(u?' — '+esc(u.name):' — empty')+'</option>';}
  $('profSlot').innerHTML=sl;
}
async function profApplySel(){
  toast(await post('/profile/apply',{id:$('profSel').value}));
  await loadS();}
async function profApplyJson(){
  const t=$('profTa').value.trim();
  if(!t){toast('Paste a profile first.',1);return;}
  toast(await post('/profile/apply',{json:t}));
  await loadS();}
async function profSave(){
  const n=$('profName').value.trim()||f.elements.pumpName.value.trim();
  if(!n){toast('Give the profile a name.',1);return;}
  toast(await post('/profile/save',{slot:$('profSlot').value,name:n}));
  await profLoadList();}
async function profShow(){
  const t=await(await fetch('/profile',{cache:'no-store'})).text();
  $('profIo').style.display='block';$('profTa').value=t;
  toast('Copy this to save the profile elsewhere.');}

loadS();simLoad();profLoadList();tick();setInterval(tick,1000);
</script></body></html>)HTML";
