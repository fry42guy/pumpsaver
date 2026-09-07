#pragma once
#include "ui_common.h"

/*
  Screen 3 -- Network.  Identity, the board's own AP, joining a site network,
  addressing, and MQTT telemetry.

  Two things worth knowing while reading this:

  The AP is not a commissioning fallback, it is how a technician reaches a
  specific board while standing in front of it, whatever the site network is
  doing.  It stays up permanently.  Changing its SSID or password needs a
  reboot, because the softAP has to be torn down and raised again.

  Addressing has no opinionated default.  A guessed subnet that half-matches
  the site is worse than an empty box -- it produces a board that looks
  configured and answers nothing.  Blank means DHCP.
*/

const char PAGE_NET[] PROGMEM = R"HTML(<!doctype html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<title>PumpSaver &middot; Network</title><style>)HTML" CSS_BASE R"HTML(
.net{display:flex;justify-content:space-between;align-items:center;gap:10px;
  padding:11px 12px;border:1px solid var(--line);border-radius:11px;margin-top:7px;
  cursor:pointer;background:#fff;min-height:48px}
.net:active{background:var(--accw);border-color:var(--acc)}
.net b{font-weight:650;font-size:14px;word-break:break-all}
.net .r{color:var(--mut);font-size:11px;font-variant-numeric:tabular-nums;
  white-space:nowrap;display:flex;align-items:center;gap:6px}
.sig{display:inline-block;width:30px;height:7px;background:#e2e7ec;border-radius:2px;overflow:hidden}
.sig i{display:block;height:100%;background:var(--ok)}
fieldset{border:0;padding:0;margin:0;min-width:0}
fieldset[disabled]{opacity:.42}
.quad input{text-align:center;font-variant-numeric:tabular-nums}
</style></head><body>)HTML" NAV_HTML R"HTML(
<main>

<section><h2>Status</h2><div class="kv" id="netInfo"></div></section>

<section><h2>Identity</h2>
<div class="fld"><span class="k">Label<i>shown in the header; free text</i></span>
<input id="label" maxlength="23" placeholder="e.g. Skid 1"></div>
<div class="fld"><span class="k">Host name<i>mDNS address</i></span>
<input id="hostName" maxlength="23" placeholder="pumpsaver"></div>
<p class="note">Reachable at <b id="mdnsEcho">pumpsaver.local</b> once it has joined a
network. Letters, digits and hyphens only &mdash; anything else is folded, because
this becomes a hostname.</p>
</section>

<section><h2>This board's access point</h2>
<div class="fld"><span class="k">AP name<i>SSID broadcast by the board</i></span>
<input id="apSsid" maxlength="32"></div>
<div class="fld"><span class="k">AP password<i>blank keeps the current one; min 8 chars</i></span>
<input id="apPass" maxlength="64" placeholder="unchanged" autocomplete="new-password"></div>
<p class="note">Always on, including once the board has joined a site network, so it
can always be reached at the skid. <b>AP changes need a reboot.</b></p>
</section>

<section><h2>Join a network</h2>
<div class="fld"><span class="k">Network<i>SSID to join; blank = station off</i></span>
<input id="ssid" maxlength="32" placeholder="none"></div>
<div class="fld"><span class="k">Password<i>blank keeps the stored one</i></span>
<input id="pass" type="password" maxlength="64" placeholder="unchanged" autocomplete="new-password"></div>
<div class="row" style="margin-top:9px">
<button type="button" class="ghost sm" id="scanBtn" onclick="scanStart()">Scan for networks</button></div>
<div id="scanList"></div>
<p class="note">Scanning drops AP clients for a few seconds &mdash; one radio, and it
has to leave the AP channel to sweep.</p>
</section>

<section><h2>Addressing</h2>
<div class="fld"><span class="k">Static IP<i>off = DHCP</i></span>
<label class="sw"><input type="checkbox" id="useStatic"><span></span></label></div>
<fieldset id="ipFs" class="quad">
<div class="fld"><span class="k">Address</span><input id="sip" placeholder="DHCP" inputmode="decimal"></div>
<div class="fld"><span class="k">Gateway</span><input id="gw" placeholder="DHCP" inputmode="decimal"></div>
<div class="fld"><span class="k">Subnet mask</span><input id="mask" placeholder="255.255.255.0" inputmode="decimal"></div>
<div class="fld"><span class="k">DNS</span><input id="dns" placeholder="gateway" inputmode="decimal"></div>
</fieldset>
<p class="note">Each part of an address is 0&ndash;255. Anything outside that is
refused rather than truncated into something that looks plausible.</p>
</section>

<section><h2>MQTT telemetry</h2>
<div class="fld"><span class="k">Publish telemetry</span>
<label class="sw"><input type="checkbox" id="mqttOn"><span></span></label></div>
<fieldset id="mqFs">
<div class="fld"><span class="k">Broker host</span><input id="host" maxlength="63" placeholder="10.0.0.5"></div>
<div class="fld"><span class="k">Port</span><input type="number" id="port" min="1" max="65535" inputmode="numeric"></div>
<div class="fld"><span class="k">Username<i>blank if none</i></span><input id="user" maxlength="32"></div>
<div class="fld"><span class="k">Password<i>blank keeps the stored one</i></span>
<input id="mpass" type="password" maxlength="64" placeholder="unchanged" autocomplete="new-password"></div>
<div class="fld"><span class="k">Base topic</span><input id="topic" maxlength="39"></div>
<div class="fld"><span class="k">Publish every<i>ms</i></span><input type="number" id="pubMs" min="500" max="60000" inputmode="numeric"></div>
</fieldset>
<p class="note">Publish-only. Nothing is subscribed, so nothing on the network can
start, stop or re-tune a pump.</p>
</section>

<section><h2>Reboot</h2>
<div class="row"><button class="ghost" onclick="reboot()">Restart controller</button></div>
<p class="note">Needed after an AP name or password change. The pumps stop while it
restarts.</p>
</section>

</main>
<div class="savebar">
<button class="ghost" type="button" onclick="loadNet()">Revert</button>
<button type="button" onclick="saveNet()">Save network</button>
</div>
<script>)HTML" NAV_JS R"HTML(
let scanT=null;
const IDS=['label','hostName','apSsid','ssid','sip','gw','mask','dns','host','port',
           'user','topic','pubMs'];

function gate(){
  $('ipFs').disabled=!$('useStatic').checked;
  $('mqFs').disabled=!$('mqttOn').checked;
  $('mdnsEcho').textContent=($('hostName').value||'pumpsaver')+'.local';
}
for(const id of ['useStatic','mqttOn'])$(id).addEventListener('change',gate);
$('hostName').addEventListener('input',gate);

const sigBar=r=>'<span class="sig"><i style="width:'+
  Math.max(0,Math.min(100,2*(r+100)))+'%"></i></span>';

async function loadNet(){
  const j=await(await fetch('/net')).json();
  const up=j.sta;
  $('netInfo').innerHTML=
    '<div><span>unit</span><b>'+esc(j.id)+(j.label?' &middot; '+esc(j.label):'')+'</b></div>'+
    '<div><span>mDNS</span><b>'+(j.mdnsUp?esc(j.mdns):'not running')+'</b></div>'+
    '<div><span>AP</span><b>'+esc(j.apSsid)+'</b></div>'+
    '<div><span>AP address</span><b>'+j.apip+'</b></div>'+
    '<div><span>AP clients</span><b>'+j.apClients+'</b></div>'+
    '<div><span>channel</span><b>'+j.ch+'</b></div>'+
    '<div><span>station</span><b class="'+(up?'ok':(j.ssid?'no':''))+'">'+
      (up?'connected':(j.ssid?'not connected':'off'))+'</b></div>'+
    '<div><span>joined</span><b>'+(j.ssid?esc(j.ssid):'&mdash;')+'</b></div>'+
    '<div><span>IP</span><b>'+(j.ip||'&mdash;')+(j.useStatic?' (static)':'')+'</b></div>'+
    '<div><span>signal</span><b>'+(up?j.rssi+' dBm':'&mdash;')+'</b></div>'+
    '<div><span>MQTT</span><b class="'+(j.mqtt?'ok':(j.mqttOn?'no':''))+'">'+
      (j.mqttOn?(j.mqtt?'connected':'down ('+j.mqttFails+' fails)'):'off')+'</b></div>'+
    '<div><span>MAC</span><b>'+j.mac+'</b></div>';

  for(const k of IDS)if($(k)&&j[k]!==undefined)$(k).value=j[k];
  // 0.0.0.0 is the "not configured" sentinel -- show it as an empty box, not
  // as an address someone might think is real.
  for(const k of ['sip','gw','dns'])if($(k).value=='0.0.0.0')$(k).value='';
  $('useStatic').checked=j.useStatic;$('mqttOn').checked=j.mqttOn;
  $('pass').value='';$('mpass').value='';$('apPass').value='';
  gate();
  if(j.log)toast(j.log);
}

// ---- scan ----------------------------------------------------------------
// /scan kicks off an async sweep and reports "scanning" until it lands; a
// blocking scan is 2-4 s and would stall the control tick.
async function scanStart(){
  $('scanBtn').textContent='Scanning…';$('scanBtn').disabled=true;
  $('scanList').innerHTML='';
  await fetch('/scan');clearTimeout(scanT);scanT=setTimeout(scanPoll,1200);
}
async function scanPoll(){
  const j=await(await fetch('/scan')).json();
  if(j.state!='done'){scanT=setTimeout(scanPoll,900);return;}
  $('scanBtn').textContent='Scan for networks';$('scanBtn').disabled=false;
  if(!j.nets||!j.nets.length){$('scanList').innerHTML='<p class="note">Nothing found.</p>';return;}
  j.nets.sort((a,b)=>b.rssi-a.rssi);
  $('scanList').innerHTML=j.nets.map(n=>
    '<div class="net" onclick="pick(this.dataset.s)" data-s="'+esc(n.ssid)+'">'+
    '<b>'+(n.ssid?esc(n.ssid):'(hidden)')+'</b><span class="r">'+sigBar(n.rssi)+
    n.rssi+' dBm'+(n.lock?'':' &middot; open')+'</span></div>').join('');
}
function pick(s){$('ssid').value=s;$('pass').focus();toast('Selected '+s);}

// ---- save ----------------------------------------------------------------
const QUAD=/^(\d{1,3})\.(\d{1,3})\.(\d{1,3})\.(\d{1,3})$/;
function badQuad(v){const m=QUAD.exec(v.trim());return !m||m.slice(1).some(x=>+x>255);}

async function saveNet(){
  if($('apSsid').value.trim()==''){toast('AP name cannot be blank.',1);return;}
  if($('apPass').value&&$('apPass').value.length<8){
    toast('AP password must be 8+ characters, or blank to keep the current one.',1);return;}
  if($('useStatic').checked)
    for(const [n,id] of [['address','sip'],['gateway','gw'],['mask','mask'],['DNS','dns']]){
      const v=$(id).value.trim();
      if(id=='dns'&&!v)continue;                   // DNS may be left to the gateway
      if(badQuad(v)){toast('The '+n+' is not a valid IPv4 address.',1);return;}}
  if($('mqttOn').checked&&!$('host').value.trim()){toast('MQTT needs a broker host.',1);return;}

  const b={};
  for(const k of IDS)b[k]=$(k).value.trim();
  b.useStatic=swVal($('useStatic'));b.mqttOn=swVal($('mqttOn'));
  b.hostName=$('hostName').value.trim();
  if($('pass').value)b.pass=$('pass').value;
  if($('mpass').value)b.mpass=$('mpass').value;
  if($('apPass').value)b.apPass=$('apPass').value;

  toast(await post('/net',b));
  setTimeout(loadNet,1200);
}

async function reboot(){
  toast(await post('/reboot',{}));
  let n=14;const iv=setInterval(()=>{
    toast('Rebooting — back in about '+(--n)+' s');
    if(n<=0){clearInterval(iv);location.reload();}},1000);
}

chrome();loadNet();setInterval(loadNet,5000);setInterval(chrome,3000);
</script></body></html>)HTML";
