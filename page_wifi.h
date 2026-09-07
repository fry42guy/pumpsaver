#pragma once
#include "ui_common.h"

/*
  Screen 3 -- Wi-Fi and node identity.

  Two independent radios-worth of configuration on one page:

    ACCESS POINT   the board own network, how you reach it standing at the
                   skid.  On by default and meant to stay on -- see net.h.
    STATION        joining a site network, which is what makes several of
                   these reachable from one place.  See ECOSYSTEM.md.

  Identity (node name / role / id) is here rather than on Advanced because it
  is a network property: it is the mDNS name, and the id is the last octet of
  the static address.

  Saving reboots.  Changing Wi-Fi mode on a live ESP32 while an async web
  server holds sockets is a good way to produce a board that answers nothing;
  a restart is one second and is unambiguous.
*/

const char PAGE_WIFI[] PROGMEM = R"HTML(<!doctype html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>PumpSaver - Wi-Fi</title><style>)HTML" CSS_BASE R"HTML(
label{display:grid;grid-template-columns:132px 1fr;align-items:center;gap:9px;
  margin:7px 0;font-size:13px}
label.chk{grid-template-columns:132px auto;font-size:13px}
label input[type=checkbox]{width:20px;height:20px;accent-color:var(--acc)}
.kv{display:grid;grid-template-columns:1fr 1fr;gap:0 14px;font-size:12px;
  font-variant-numeric:tabular-nums}
.kv>div{display:flex;justify-content:space-between;padding:3px 0;border-bottom:1px solid #f1f3f5}
.kv span{color:var(--mut)}
.kv b{font-weight:600;text-align:right;word-break:break-all}
.net{display:flex;justify-content:space-between;align-items:center;gap:8px;
  padding:8px 9px;border:1px solid var(--line);border-radius:7px;margin-top:6px;
  cursor:pointer;font-size:13px;background:#fff}
.net:hover{border-color:var(--acc);background:#f4f9fb}
.net b{font-weight:600}
.net .r{color:var(--mut);font-size:11px;font-variant-numeric:tabular-nums;white-space:nowrap}
.sig{display:inline-block;width:34px;height:8px;background:#e6e9ec;border-radius:2px;
  overflow:hidden;vertical-align:middle;margin-right:5px}
.sig i{display:block;height:100%;background:var(--ok)}
fieldset{border:0;padding:0;margin:0}
fieldset[disabled]{opacity:.45}
.msg{border-radius:8px;padding:10px 12px;font-size:13px;font-weight:600;margin-top:9px}
.msg.ok{background:#e4efe8;color:#166040}
.msg.bad{background:#f8e2d9;color:#8f3f11}
</style></head><body>)HTML" NAV_HTML R"HTML(
<main>

<section><h2>Current connection</h2>
<div class="kv" id="netInfo"></div>
</section>

<section><h2>Identity</h2>
<label>Node name<input id="nName" maxlength="23" placeholder="pump-1"></label>
<label>Role<select id="nRole">
<option value="pump">pump</option><option value="tank">tank</option>
<option value="flow">flow</option><option value="dosing">dosing</option>
<option value="gateway">gateway</option><option value="other">other</option>
</select></label>
<label>Node id<input type="number" id="nId" min="1" max="250" step="1"></label>
<p class="note">The node name is the mDNS address &mdash; <b id="mdnsEcho">pump-1.local</b>.
The id is the last octet when a static address is used.</p>
</section>

<section><h2>Access point</h2>
<label class="chk">Enabled<input type="checkbox" id="apOn"></label>
<fieldset id="apFs">
<label>AP SSID<input id="apSsid" maxlength="32"></label>
<label>AP password<input id="apPass" maxlength="64" placeholder="unchanged" autocomplete="new-password"></label>
</fieldset>
<p class="note">8 characters minimum, or the AP comes up open. Leave the
password blank to keep the one already stored.</p>
</section>

<section><h2>Join a network</h2>
<label class="chk">Enabled<input type="checkbox" id="stOn"></label>
<fieldset id="stFs">
<label>SSID<input id="stSsid" maxlength="32"></label>
<label>Password<input id="stPass" type="password" maxlength="64" placeholder="unchanged" autocomplete="new-password"></label>
<div class="row" style="margin-top:8px"><button type="button" class="ghost" id="scanBtn"
  onclick="scanStart()">Scan for networks</button></div>
<div id="scanList"></div>

<label class="chk" style="margin-top:12px">Static IP<input type="checkbox" id="useStatic"></label>
<fieldset id="ipFs">
<label>Address<input id="ipA" placeholder="10.10.42.1"></label>
<label>Gateway<input id="gwA" placeholder="10.10.42.254"></label>
<label>Subnet mask<input id="mkA" placeholder="255.255.255.0"></label>
<label>DNS<input id="dnA" placeholder="10.10.42.254"></label>
<div class="row"><button type="button" class="ghost" onclick="suggest()">Fill from node id</button></div>
</fieldset>
</fieldset>
</section>

<section><h2>Apply</h2>
<div class="row">
<button onclick="saveNet()">Save &amp; reboot</button>
<button class="ghost" onclick="loadNet()">Discard changes</button>
</div>
<div id="msg" class="note">The board restarts to apply. If you are on the AP it
will drop for about ten seconds and come back.</div>
</section>

<section><h2>Several of these on one site</h2>
<p class="note">Each node keeps its own AP so it can always be reached at the
skid, and joins the site network so it can be reached from one place. Give
every node a distinct name and id, and a static address out of one block.
Nothing in the control loop depends on the radio &mdash; a node that loses the
network keeps pumping. See <b>ECOSYSTEM.md</b> in the repo for the plan.</p>
</section>

</main><script>)HTML" NAV_JS R"HTML(
let CUR=null,scanT=null;

const esc=s=>String(s).replace(/[&<>"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]));

function say(t,bad){const m=document.getElementById('msg');
  m.textContent=t;m.className='msg '+(bad?'bad':'ok');}

function gate(){
  apFs.disabled=!apOn.checked;
  stFs.disabled=!stOn.checked;
  ipFs.disabled=!useStatic.checked||!stOn.checked;
  mdnsEcho.textContent=(nName.value||'pump-1')+'.local';
}
for(const el of [apOn,stOn,useStatic])el.addEventListener('change',gate);
nName.addEventListener('input',gate);

// ---- current state -------------------------------------------------------
function sigBar(r){const p=Math.max(0,Math.min(100,2*(r+100)));
  return '<span class="sig"><i style="width:'+p+'%"></i></span>';}

async function loadNet(){
  const j=await(await fetch('/net')).json();CUR=j;
  const up=j.staUp;
  netInfo.innerHTML=
    '<div><span>node</span><b>'+esc(j.node)+' ('+esc(j.role)+' #'+j.id+')</b></div>'+
    '<div><span>mDNS</span><b>'+(j.mdnsUp?esc(j.mdns):'not running')+'</b></div>'+
    '<div><span>AP</span><b>'+(j.apOn?esc(j.apSsid):'off')+'</b></div>'+
    '<div><span>AP address</span><b>'+j.apIp+'</b></div>'+
    '<div><span>AP clients</span><b>'+j.apClients+'</b></div>'+
    '<div><span>channel</span><b>'+j.ch+'</b></div>'+
    '<div><span>station</span><b class="'+(up?'ok':(j.staOn?'no':''))+'">'+esc(j.staState)+'</b></div>'+
    '<div><span>joined</span><b>'+(j.ssid?esc(j.ssid):'--')+'</b></div>'+
    '<div><span>IP</span><b>'+j.staIp+(j.useStatic?' (static)':'')+'</b></div>'+
    '<div><span>signal</span><b>'+(up?j.rssi+' dBm':'--')+'</b></div>'+
    '<div><span>MAC</span><b>'+j.mac+'</b></div>'+
    '<div><span>AP password</span><b>'+(j.apPassSet?'set':'OPEN')+'</b></div>';

  nName.value=j.node;nId.value=j.id;
  // A role stored by an older build (or by hand) that is not in the list must
  // not silently blank the select and get saved back as empty.
  nRole.value=[...nRole.options].some(o=>o.value==j.role)?j.role:'other';
  apOn.checked=j.apOn;apSsid.value=j.apSsid;apPass.value='';
  stOn.checked=j.staOn;stSsid.value=j.ssid;stPass.value='';
  useStatic.checked=j.useStatic;
  ipA.value=j.ip;gwA.value=j.gw;mkA.value=j.mask;dnA.value=j.dns;
  gate();
}

// ---- scan ---------------------------------------------------------------
// The scan is async on the device because a blocking one takes 2-4 s and
// would stall the control tick.  Poll until it reports done.
async function scanStart(){
  scanBtn.textContent='Scanning...';scanBtn.disabled=true;
  scanList.innerHTML='<p class="note">The AP drops briefly while the radio sweeps channels.</p>';
  await fetch('/wifi/scan?start=1');
  clearTimeout(scanT);scanT=setTimeout(scanPoll,1200);
}
async function scanPoll(){
  const j=await(await fetch('/wifi/scan')).json();
  if(j.scanning){scanT=setTimeout(scanPoll,900);return;}
  scanBtn.textContent='Scan for networks';scanBtn.disabled=false;
  if(!j.nets.length){scanList.innerHTML='<p class="note">Nothing found. Try again.</p>';return;}
  j.nets.sort((a,b)=>b.rssi-a.rssi);
  scanList.innerHTML=j.nets.map(n=>
    '<div class="net" onclick="pick(this.dataset.s)" data-s="'+esc(n.ssid)+'">'+
    '<b>'+(n.ssid?esc(n.ssid):'(hidden)')+'</b>'+
    '<span class="r">'+sigBar(n.rssi)+n.rssi+' dBm &middot; ch'+n.ch+
    (n.open?' &middot; open':'')+'</span></div>').join('');
}
function pick(s){stSsid.value=s;stOn.checked=true;gate();stPass.focus();
  say('Selected '+s+' - enter the password and Save.');}

// ---- static address ------------------------------------------------------
// 10.10.42.x is a suggestion, not a rule: it is a private block unlikely to
// collide with a site LAN, and one node per last octet keeps the mapping
// between "node 3" and its address obvious.  See ECOSYSTEM.md.
function suggest(){
  const n=Math.max(1,Math.min(250,+nId.value||1));
  ipA.value='10.10.42.'+n;gwA.value='10.10.42.254';
  mkA.value='255.255.255.0';dnA.value='10.10.42.254';
  useStatic.checked=true;gate();
  say('Filled 10.10.42.'+n+' - this must match the router the node joins.');
}

// ---- save ----------------------------------------------------------------
const QUAD=/^(\d{1,3})\.(\d{1,3})\.(\d{1,3})\.(\d{1,3})$/;
function badQuad(v){const m=QUAD.exec(v.trim());
  return !m||m.slice(1).some(x=>+x>255);}

async function saveNet(){
  if(!nName.value.trim()){say('Node name cannot be blank.',1);return;}
  if(apOn.checked&&!apSsid.value.trim()){say('AP SSID cannot be blank.',1);return;}
  if(stOn.checked&&!stSsid.value.trim()){say('Pick or type an SSID to join.',1);return;}
  if(apOn.checked&&apPass.value&&apPass.value.length<8){
    say('AP password must be 8 characters or more, or blank to keep the current one.',1);return;}
  if(stOn.checked&&useStatic.checked)
    for(const [n,el] of [['address',ipA],['gateway',gwA],['mask',mkA],['DNS',dnA]])
      if(badQuad(el.value)){say('The '+n+' is not a valid IPv4 address.',1);return;}

  const b=new URLSearchParams();
  b.set('node',nName.value.trim());b.set('role',nRole.value);b.set('id',nId.value||1);
  b.set('apOn',apOn.checked?1:0);b.set('apSsid',apSsid.value.trim());
  if(apPass.value)b.set('apPass',apPass.value);
  b.set('staOn',stOn.checked?1:0);b.set('ssid',stSsid.value.trim());
  if(stPass.value)b.set('pass',stPass.value);
  b.set('useStatic',useStatic.checked?1:0);
  b.set('ip',ipA.value.trim());b.set('gw',gwA.value.trim());
  b.set('mask',mkA.value.trim());b.set('dns',dnA.value.trim());

  say(await(await fetch('/wifi/set',{method:'POST',body:b})).text());
  let n=12;const iv=setInterval(()=>{
    say('Saved. Rebooting - back in about '+(--n)+' s'+
        (stOn.checked?', then look for '+(nName.value.trim()||'pump-1')+'.local':''));
    if(n<=0){clearInterval(iv);location.reload();}},1000);
}

// The version banner comes from /status like every other screen, so all three
// agree about what is running on the board.
async function loadVer(){try{const s=await(await fetch('/status')).json();
  ver.textContent='v'+s.ver;if(s.sim)simbar.style.display='block';}catch(e){}}

loadVer();loadNet();
</script></body></html>)HTML";
