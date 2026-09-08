#pragma once

/*
  Calibration screen -- /cal

  Everything to do with characterising a pump lives here rather than being a
  card buried on System.  Two sweeps (deadhead and wide open) produce the data
  that `shutoffPsiAt60`, the cavitation cap table and the PI gains should all
  be derived from instead of estimated.

  The table is rendered ONLY when the data actually changes, and never while a
  cell inside it has focus.  A 1 Hz poll that rewrites innerHTML unconditionally
  destroys the input you are typing into on the next tick -- which is exactly
  what made the gpm column impossible to fill in.
*/

const char PAGE_CAL[] PROGMEM = R"HTML(<!doctype html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<title>PumpSaver &middot; Calibration</title><style>)HTML" CSS_BASE R"HTML(
</style></head><body>)HTML" NAV_HTML R"HTML(
<main>

<section><h2>Pump sweep &mdash; curve capture</h2>
<p class="note">Steps the pump 30&rarr;60&nbsp;Hz in 5&nbsp;Hz increments, averaging the settled
tail of each step. Run it <b>deadheaded</b> (valve shut) to measure shutoff head against
speed &mdash; that is where <code>shutoffPsiAt60</code> should come from instead of being
estimated &mdash; then <b>wide open</b>, typing the flow you read off the meter into each row.
Aborts above the pressure limit, on a drive trip, or if comms drop.</p>

<div class="row">
<button id="swDead" onclick="swStart(0)">Run deadhead</button>
<button id="swOpen" onclick="swStart(1)">Run wide open</button>
<button class="ghost" onclick="location.href='/sweepcsv'">Download CSV</button></div>

<div class="fld"><span class="k">Seconds per step<i>the settled tail of each step is averaged</i></span>
<input id="swDwell" type="number" min="2" max="300" step="1" style="max-width:90px"></div>

<div class="fld"><span class="k">Step through by hand<i>hold at each speed until you press Next &mdash; read the meter, type the gpm, then advance</i></span>
<label class="sw"><input type="checkbox" id="swStepMode"><span></span></label></div>

<div class="fld"><span class="k">Abort above<i>psi &mdash; must be inside the weakest fitting on the skid</i></span>
<input id="swPsiMax" type="number" min="10" max="300" step="5" style="max-width:90px"></div>

<div class="alert" id="swMsg">idle</div>

<div class="row" id="swLive" style="display:none">
<button class="red" id="swAbortBtn" onclick="swAbort()"
        style="font-size:17px;padding:14px 22px">STOP SWEEP</button>
<button id="swNextBtn" onclick="swNext()" style="display:none">Next step &rarr;</button></div>

<div style="overflow-x:auto">
<table><thead><tr><th>Mode</th><th>Hz set</th><th>Hz act</th><th>psi</th><th>A</th>
<th>V</th><th>Vbus</th><th>Trq&nbsp;%</th><th>kW</th><th>gpm</th><th>cav</th></tr></thead>
<tbody id="swRows"></tbody></table></div>
<p class="note"><b>gpm</b> and <b>cav</b> are yours, not the board's &mdash; a meter reading and
what you heard. They export as <code>flow_gpm_manual</code> and <code>cavitating_manual</code>
so nobody later mistakes them for something this controller measured.</p>
</section>

</main><script>)HTML" NAV_JS R"HTML(

// ---- pump sweep ----------------------------------------------------------
let swSig='';
const swOpts=()=>({psiMax:$('swPsiMax').value,dwell:$('swDwell').value,
                   stepMode:$('swStepMode').checked?1:0});
async function swStart(m){
  toast(await post('/sweep',Object.assign({a:'start',mode:m},swOpts())));swSig='';loadSweep();}
async function swAbort(){toast(await post('/sweep',{a:'abort'}));loadSweep();}
async function swNext(){await post('/sweep',{a:'next'});loadSweep();}
async function swFlow(m,s,v){await post('/sweep',{a:'flow',mode:m,step:s,gpm:v});}
async function swCav(m,s,on){await post('/sweep',{a:'cav',mode:m,step:s,on:on?1:0});}
// Pushed the moment it is toggled.  Without this the 1 Hz poll below reads the
// board's value straight back over the checkbox and it appears to flick off.
async function swMode(){await post('/sweep',{a:'opts',stepMode:$('swStepMode').checked?1:0});}
$('swStepMode').addEventListener('change',swMode);

async function loadSweep(){
  let j;try{j=await(await fetch('/sweep',{cache:'no-store'})).json();}catch(e){return;}
  const busy=id=>document.activeElement===$(id);
  if(!busy('swPsiMax'))$('swPsiMax').value=j.psiMax;
  if(!busy('swDwell'))$('swDwell').value=j.dwell;
  if(!busy('swStepMode'))$('swStepMode').checked=j.stepMode;

  $('swMsg').textContent=j.run
    ? (j.hold
        ? 'HOLDING at '+j.holdHz.toFixed(0)+' Hz — step '+(j.step+1)+' of '+j.steps
          +'. Read the meter, type the gpm below, then Next step.'
        : 'running '+(j.mode?'wide open':'deadhead')+' — step '+(j.step+1)+' of '
          +j.steps+', '+j.secLeft+' s left')
    : j.msg;
  $('swMsg').className='alert '+(j.run?'warn':(/abort/i.test(j.msg)?'bad':'ok'));
  $('swDead').disabled=$('swOpen').disabled=j.run;
  $('swLive').style.display=j.run?'':'none';
  $('swNextBtn').style.display=j.hold?'':'none';

  // Only rebuild the table when the measured data changed, and never while the
  // operator has a cell focused -- rewriting innerHTML under them destroys the
  // input mid-keystroke.  gpm and cav are excluded from the signature because
  // they are the operator's own edits coming back.
  let sig='';
  for(let m=0;m<2;m++)for(const r of j.rows[m])
    sig+=(r.done?1:0)+':'+r.hzAct+':'+r.psi+':'+r.amps+':'+r.volts+':'+r.torq+'|';
  if(sig===swSig)return;
  if($('swRows').contains(document.activeElement))return;
  swSig=sig;

  let h='';
  for(let m=0;m<2;m++)for(let i=0;i<j.rows[m].length;i++){
    const r=j.rows[m][i];if(!r.done)continue;
    h+='<tr><td>'+(m?'open':'dead')+'</td><td>'+r.hz.toFixed(0)+'</td><td>'
      +r.hzAct.toFixed(1)+'</td><td>'+r.psi.toFixed(1)+'</td><td>'+r.amps.toFixed(2)+'</td><td>'
      +r.volts+'</td><td>'+r.vbus+'</td><td>'+r.torq.toFixed(1)+'</td><td>'+r.kw.toFixed(1)
      +'</td><td><input type="number" step="0.1" min="0" value="'+r.flow.toFixed(1)
      +'" style="width:64px;padding:2px 4px;font-size:12px" '
      +'onchange="swFlow('+m+','+i+',this.value)"></td>'
      +'<td><input type="checkbox" '+(r.cav?'checked':'')
      +' onchange="swCav('+m+','+i+',this.checked)"></td></tr>';}
  $('swRows').innerHTML=h||'<tr><td colspan="11" style="color:#888">no steps recorded yet</td></tr>';
}
loadSweep();
setInterval(loadSweep,1000);
</script></body></html>)HTML";
