#pragma once

/*
  Shared chrome for the four screens.

  These are #defines, not const char[], so the preprocessor pastes them INSIDE
  each page's raw string literal and adjacent-literal concatenation folds the
  whole page into one PROGMEM array at compile time.  One copy of the design
  system in the source, four in flash.

  Everything here lives in a .h for the same reason page.h does: Arduino scans
  .ino files for things that look like function definitions and does not
  understand raw string literals.  See DESIGN_NOTES.md.

  Design intent: this is configured from a phone, in a plant room, by someone
  wearing gloves.  So -- 44 px minimum tap targets, one column, real toggle
  switches instead of the 1/0 number boxes, a sticky save bar that follows the
  form, and numbers big enough to read at arm's length.  Booleans still POST
  as "1"/"0" so every existing endpoint is untouched.
*/

// ------------------------------------------------------------------ styles
#define CSS_BASE R"CSS(
:root{
  --bg:#eef1f5; --card:#fff; --ink:#141d29; --mut:#68737f; --dim:#93a0ac;
  --line:#e2e7ec; --line2:#eef1f4;
  --acc:#12657f; --acc2:#0d4d61; --accw:#e6f1f5;
  --ok:#12805c; --okw:#e2f2eb; --warn:#b9791c; --warnw:#faf0dd;
  --bad:#b4431a; --badw:#fae7df;
  --r:14px; --sh:0 1px 2px rgba(20,29,41,.05),0 1px 8px rgba(20,29,41,.04);
}
*{box-sizing:border-box}
html{-webkit-text-size-adjust:100%}
body{font-family:system-ui,-apple-system,"Segoe UI",sans-serif;margin:0;
  background:var(--bg);color:var(--ink);font-size:15px;line-height:1.45;
  padding-bottom:env(safe-area-inset-bottom)}
b,strong{font-weight:650}

/* ---- top bar ---------------------------------------------------------- */
.top{background:var(--acc2);color:#fff;position:sticky;top:0;z-index:20;
  padding-top:env(safe-area-inset-top);box-shadow:0 1px 12px rgba(13,77,97,.28)}
.top .bar{display:flex;align-items:center;gap:9px;padding:11px 14px 9px}
.top .brand{font-weight:700;font-size:16px;letter-spacing:-.01em}
.top .dot{width:8px;height:8px;border-radius:50%;background:#5fd6a4;flex:none;
  box-shadow:0 0 0 3px rgba(95,214,164,.22)}
.top .dot.off{background:#e08a63;box-shadow:0 0 0 3px rgba(224,138,99,.22)}
.top .meta{margin-left:auto;font-size:11px;opacity:.72;text-align:right;
  font-variant-numeric:tabular-nums;line-height:1.3}
nav{display:flex;overflow-x:auto;scrollbar-width:none;padding:0 6px}
nav::-webkit-scrollbar{display:none}
nav a{flex:1 0 auto;min-width:76px;text-align:center;color:#a9c9d5;
  text-decoration:none;font-size:13px;font-weight:600;padding:9px 12px 10px;
  border-bottom:3px solid transparent;white-space:nowrap}
nav a.on{color:#fff;border-bottom-color:#5fd6a4}

/* ---- banners ---------------------------------------------------------- */
.banner{padding:9px 14px;font-weight:700;font-size:12.5px;letter-spacing:.03em;
  text-align:center;color:#fff}
.banner.sim{background:#7d2352}
.banner.ovr{background:#8a1c1c}

/* ---- layout ----------------------------------------------------------- */
main{padding:13px;max-width:620px;margin:0 auto 78px}
section{background:var(--card);border:1px solid var(--line);border-radius:var(--r);
  padding:14px;margin-bottom:12px;box-shadow:var(--sh)}
h2{font-size:11px;margin:0 0 11px;color:var(--mut);text-transform:uppercase;
  letter-spacing:.09em;font-weight:700}
h3{font-size:10.5px;margin:14px 0 5px;color:var(--acc);text-transform:uppercase;
  letter-spacing:.085em;font-weight:700;grid-column:1/-1}
.note{font-size:12.5px;color:var(--mut);line-height:1.55;margin:8px 0 0}
.note a{color:var(--acc)}
.row{display:flex;gap:8px;flex-wrap:wrap}

/* ---- controls --------------------------------------------------------- */
button{font-family:inherit;font-size:15px;font-weight:650;padding:12px 16px;
  border:0;border-radius:11px;background:var(--acc);color:#fff;cursor:pointer;
  min-height:44px;-webkit-tap-highlight-color:transparent}
button:active{transform:translateY(1px)}
button.red{background:var(--bad)}
button.ghost{background:#eef2f5;color:var(--ink);border:1px solid var(--line)}
button.sm{font-size:13px;padding:8px 12px;min-height:38px;border-radius:9px}
input,select,textarea{font-family:inherit;font-size:16px;padding:10px 11px;
  border:1px solid #c6cfd7;border-radius:10px;background:#fff;color:var(--ink);
  width:100%;min-height:44px}
input:focus,select:focus,textarea:focus{outline:2px solid var(--acc);
  outline-offset:-1px;border-color:var(--acc)}
textarea{min-height:110px;font-family:ui-monospace,Menlo,Consolas,monospace;font-size:13px}

/* a labelled field: label left, control right, stacks on a narrow phone */
.fld{display:grid;grid-template-columns:1fr 116px;align-items:center;gap:10px;
  padding:9px 0;border-bottom:1px solid var(--line2);font-size:14px}
.fld:last-child{border-bottom:0}
.fld .k{min-width:0}
.fld .k i{display:block;font-style:normal;font-size:11.5px;color:var(--dim);
  line-height:1.35;margin-top:1px}
.fld input,.fld select{text-align:right;font-variant-numeric:tabular-nums}
.fld.wide{grid-template-columns:1fr}
.fld.wide input,.fld.wide select{text-align:left}

/* real switches instead of 1/0 number boxes */
.sw{position:relative;width:50px;height:30px;flex:none;justify-self:end}
.sw input{position:absolute;opacity:0;width:100%;height:100%;margin:0;
  min-height:0;z-index:2;cursor:pointer}
.sw span{position:absolute;inset:0;background:#c9d2da;border-radius:999px;
  transition:background .16s}
.sw span:after{content:"";position:absolute;top:3px;left:3px;width:24px;height:24px;
  background:#fff;border-radius:50%;transition:transform .16s;
  box-shadow:0 1px 3px rgba(0,0,0,.28)}
.sw input:checked+span{background:var(--ok)}
.sw input:checked+span:after{transform:translateX(20px)}
.sw input:focus-visible+span{outline:2px solid var(--acc);outline-offset:2px}

/* slider + numeric echo */
.sl{display:grid;grid-template-columns:1fr 82px;gap:10px;align-items:center;margin-top:6px}
.sl .lab{grid-column:1/-1;font-size:13px;color:var(--mut);display:flex;
  justify-content:space-between;align-items:baseline}
input[type=range]{width:100%;padding:0;border:0;min-height:0;height:30px;
  background:none;accent-color:var(--acc)}
.sl input[type=number]{padding:7px 8px;min-height:38px;font-size:15px}
.rng{display:flex;gap:6px;align-items:center;font-size:11px;color:var(--dim);margin-top:2px}
.rng input{width:64px;padding:3px 6px;font-size:11px;min-height:0;border-radius:6px;text-align:center}

/* ---- readouts --------------------------------------------------------- */
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(96px,1fr));gap:9px}
.m{background:#f5f7f9;border:1px solid var(--line2);border-radius:11px;padding:10px 11px}
.m small{display:block;color:var(--mut);font-size:10px;letter-spacing:.075em;
  text-transform:uppercase;font-weight:600;margin-bottom:3px}
.m b{font-size:21px;font-weight:650;font-variant-numeric:tabular-nums;letter-spacing:-.02em}
.m b i{font-size:11px;font-weight:600;font-style:normal;color:var(--dim);margin-left:2px}
.kv{display:grid;grid-template-columns:1fr 1fr;gap:0 16px;font-size:12.5px;
  font-variant-numeric:tabular-nums}
.kv>div{display:flex;justify-content:space-between;align-items:center;gap:8px;
  flex-wrap:wrap;padding:4px 0;border-bottom:1px solid var(--line2)}
.kv span:first-child{color:var(--mut)}
.kv b{font-weight:650;text-align:right;word-break:break-word}

.pill{display:inline-block;padding:4px 11px;border-radius:999px;font-size:12px;
  font-weight:700;letter-spacing:.02em;background:#e8edf1;color:var(--acc2)}
.pill.run{background:var(--okw);color:var(--ok)}
.pill.slp{background:#e9edf1;color:var(--mut)}
.pill.bad{background:var(--badw);color:var(--bad)}
.pill.warn{background:var(--warnw);color:var(--warn)}
.ok{color:var(--ok)}.no{color:var(--bad)}.warn{color:var(--warn);font-weight:700}
.dot2{width:12px;height:12px;border-radius:50%;background:#cbd3da;flex:none;
  box-shadow:inset 0 0 0 1px rgba(0,0,0,.14)}
.dot2.g{background:var(--ok)}.dot2.r{background:var(--bad)}.dot2.a{background:var(--warn)}

.alert{border-radius:11px;padding:11px 13px;margin-bottom:12px;font-size:14px;
  font-weight:650;display:flex;gap:9px;align-items:center}
.alert.ok{background:var(--okw);color:#0e6247;box-shadow:inset 3px 0 0 var(--ok)}
.alert.warn{background:var(--warnw);color:#8a5a12;box-shadow:inset 3px 0 0 var(--warn)}
.alert.bad{background:var(--badw);color:#8c3413;box-shadow:inset 3px 0 0 var(--bad)}

table{width:100%;border-collapse:collapse;font-size:13px}
th,td{text-align:left;padding:7px 6px;border-bottom:1px solid var(--line2)}
th{font-size:10px;text-transform:uppercase;letter-spacing:.07em;color:var(--mut);font-weight:700}
td{font-variant-numeric:tabular-nums}
pre{background:#f3f5f7;border:1px solid var(--line);border-radius:10px;padding:10px;
  font-size:12px;white-space:pre-wrap;word-break:break-word;margin:10px 0 0;color:#3a4653}
details{border-top:1px solid var(--line2);padding:9px 0}
details summary{cursor:pointer;font-size:14px;font-weight:650;list-style:none}
details summary::-webkit-details-marker{display:none}
details summary:before{content:"\25B8";color:var(--dim);margin-right:7px;display:inline-block}
details[open] summary:before{transform:rotate(90deg)}

/* ---- sticky save bar + toast ------------------------------------------ */
.savebar{position:fixed;left:0;right:0;bottom:0;z-index:30;background:rgba(255,255,255,.94);
  backdrop-filter:blur(8px);border-top:1px solid var(--line);
  padding:10px 13px calc(10px + env(safe-area-inset-bottom));display:flex;gap:9px;
  justify-content:center}
.savebar button{flex:1;max-width:300px}
.toast{position:fixed;left:50%;bottom:82px;transform:translate(-50%,14px);z-index:40;
  background:#141d29;color:#fff;padding:11px 16px;border-radius:11px;font-size:13.5px;
  font-weight:600;max-width:88vw;text-align:center;opacity:0;pointer-events:none;
  transition:opacity .18s,transform .18s;box-shadow:0 6px 24px rgba(0,0,0,.26)}
.toast.on{opacity:1;transform:translate(-50%,0)}
.toast.bad{background:#8c3413}
)CSS"

// The version, the link light and the active tab are filled in by JS so the
// same markup serves every page and nothing has to be templated on the device.
#define NAV_HTML R"NAV(
<div class="top">
  <div class="bar">
    <span class="dot off" id="link"></span>
    <span class="brand">PumpSaver</span>
    <span class="meta"><span id="devname">&nbsp;</span><br><span id="ver">&nbsp;</span></span>
  </div>
  <nav>
    <a href="/">Home</a><a href="/pump">Pump</a><a href="/network">Network</a><a href="/system">System</a>
  </nav>
</div>
<div class="banner sim" id="simbar" style="display:none">SIMULATED PLANT &mdash; NOT DRIVING REAL HARDWARE</div>
<div class="banner ovr" id="ovrbar" style="display:none">MAX-Hz OVERRIDE ENGAGED &mdash; CAVITATION CAP BYPASSED</div>
<div class="toast" id="toast"></div>
)NAV"

// ---------------------------------------------------------- simulated plant
// One source, two pages.  The controls belong next to the Simulation toggle on
// System (that is where you switch it on) AND next to the envelope plot on Pump
// (that is where you watch the operating point move while you drag).  Both bind
// to the same server state and both re-read it, so they cannot disagree --
// exactly like the setpoint appearing on both Home and Pump.
#define SIM_PANEL_HTML R"SIMH(
<div class="fld wide"><span class="k">Drive the plant by</span>
<select id="sm"><option value="0">Demand &mdash; set a draw, the plant finds its own pressure</option>
<option value="1">Pressure &mdash; hold the header where I put it</option></select></div>
<div id="psirow">
<div class="sl"><div class="lab"><span>Header psi</span><span class="mut">pressure mode</span></div>
<input type="range" id="sp2" min="0" max="90" step="0.5"><input type="number" id="sp2v" step="0.5"></div>
<div class="rng">range<input type="number" id="sp2min" value="0"><span>to</span><input type="number" id="sp2max" value="90"></div>
</div>
<div class="sl"><div class="lab"><span>Demand gpm</span><span class="mut">draw off the header</span></div>
<input type="range" id="sd" min="0" max="120" step="0.5"><input type="number" id="sdv" step="0.5"></div>
<div class="rng">range<input type="number" id="sdmin" value="0"><span>to</span><input type="number" id="sdmax" value="120"></div>
<div class="sl"><div class="lab"><span>Time scale &times;</span><span class="mut">compress the 60 s holds</span></div>
<input type="range" id="ts" min="1" max="60" step="1"><input type="number" id="tsv" step="1"></div>
<div class="rng">range<input type="number" id="tsmin" value="1"><span>to</span><input type="number" id="tsmax" value="60"></div>
<div class="sl"><div class="lab"><span>Tank gal/psi</span><span class="mut">system capacitance</span></div>
<input type="range" id="cg" min="0.2" max="10" step="0.1"><input type="number" id="cgv" step="0.1"></div>
<div class="rng">range<input type="number" id="cgmin" value="0.2"><span>to</span><input type="number" id="cgmax" value="10"></div>
<div class="grid" style="margin-top:11px">
<div class="m"><small>Header</small><b id="spsi">--<i>psi</i></b></div>
<div class="m"><small>Delivered</small><b id="fl">--<i>gpm</i></b></div>
<div class="m"><small>Actual speed</small><b id="ha">--<i>Hz</i></b></div>
</div>
)SIMH"

// Sliders apply while dragging (debounced) and immediately on release, so
// there is no Apply button to forget.
#define SIM_PANEL_JS R"SIMJS(
let simT=null;
function simEcho(){$('sdv').value=$('sd').value;$('tsv').value=$('ts').value;
 $('cgv').value=$('cg').value;$('sp2v').value=$('sp2').value;
 $('psirow').style.display=($('sm').value=='1')?'block':'none';}
function simDrag(){simEcho();clearTimeout(simT);simT=setTimeout(pushSim,150);}
function simDrop(){simEcho();clearTimeout(simT);pushSim();}
function simBox(){$('sd').value=$('sdv').value;$('ts').value=$('tsv').value;
 $('cg').value=$('cgv').value;$('sp2').value=$('sp2v').value;simDrop();}
function simRange(){$('sd').min=$('sdmin').value;$('sd').max=$('sdmax').value;
 $('ts').min=$('tsmin').value;$('ts').max=$('tsmax').value;
 $('cg').min=$('cgmin').value;$('cg').max=$('cgmax').value;
 $('sp2').min=$('sp2min').value;$('sp2').max=$('sp2max').value;simEcho();}
async function pushSim(){await post('/sim',{simMode:$('sm').value,simPsi:$('sp2').value,
 simDemandGPM:$('sd').value,simTimeScale:$('ts').value,simCapGalPsi:$('cg').value});}
async function simLoad(){const j=await(await fetch('/settings')).json();
 $('sd').value=j.simDemandGPM;$('ts').value=j.simTimeScale;$('cg').value=j.simCapGalPsi;
 $('sm').value=j.simMode;$('sp2').value=j.simPsi;simEcho();}
// Live readout, fed from a /status object the page already fetched.
function simTick(j){$('spsi').innerHTML=j.psi.toFixed(1)+'<i>psi</i>';
 $('fl').innerHTML=j.flow.toFixed(1)+'<i>gpm</i>';
 $('ha').innerHTML=j.hzAct.toFixed(1)+'<i>Hz</i>';}
for(const id of ['sd','ts','cg','sp2']){
  $(id).addEventListener('input',simDrag);$(id).addEventListener('change',simDrop);}
for(const id of ['sdv','tsv','cgv','sp2v'])$(id).addEventListener('change',simBox);
for(const id of ['sdmin','sdmax','tsmin','tsmax','cgmin','cgmax','sp2min','sp2max'])
  $(id).addEventListener('change',simRange);
$('sm').addEventListener('change',simDrop);
)SIMJS"

// Shared helpers.  Kept tiny and dependency-free -- this has to parse and run
// on whatever browser is on the phone in someone's pocket.
#define NAV_JS R"NAVJS(
for(const a of document.querySelectorAll('nav a'))
  if(a.getAttribute('href')==location.pathname)a.className='on';

const $=id=>document.getElementById(id);
const esc=s=>String(s).replace(/[&<>"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]));
let toastT=null;
function toast(msg,bad){const t=$('toast');t.textContent=msg;
  t.className='toast on'+(bad?' bad':'');clearTimeout(toastT);
  toastT=setTimeout(()=>{t.className='toast'+(bad?' bad':'');},2600);}
function linkUp(up){$('link').className='dot'+(up?'':' off');}
function hhmm(s){s=Math.max(0,s|0);const d=s/86400|0,h=s%86400/3600|0,m=s%3600/60|0;
  return d?d+'d '+h+'h':h?h+'h '+m+'m':m+'m '+(s%60)+'s';}
// POST a form-encoded body and return the response text.
async function post(url,obj){const b=new URLSearchParams();
  for(const k in obj)b.set(k,obj[k]);
  const r=await fetch(url,{method:'POST',body:b});return await r.text();}
// Booleans still go on the wire as 1/0 -- the switches are cosmetic, every
// endpoint keeps the contract it already had.
const swVal=el=>el.checked?1:0;
// The chrome (version, device name, banners) is identical on all four pages.
async function chrome(){try{const j=await(await fetch('/status')).json();
  linkUp(true);$('ver').textContent='v'+j.ver;
  $('simbar').style.display=j.sim?'block':'none';
  $('ovrbar').style.display=j.ovr?'block':'none';
  return j;}catch(e){linkUp(false);return null;}}
)NAVJS"
