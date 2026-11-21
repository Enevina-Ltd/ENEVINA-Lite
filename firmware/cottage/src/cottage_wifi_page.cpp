#ifndef COTTAGE_BENCH_MODE
#include <pgmspace.h>
#include "cottage_wifi.hpp"
// Phase 1 only: choose the home Wi-Fi. No pump, sensor, tariff or
// optimization questions here; they live in the dashboard on the home LAN.
const char cottage_wifi_setup_html[] PROGMEM = R"HTML(<!doctype html>
<html lang="fi"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Enevina Lite – Wi-Fi</title>
<style>
:root{--bg:#f6f7f4;--card:#fff;--ink:#1d2320;--mut:#5d6762;--acc:#1f6f54;--bad:#a2321f;--line:#d9ddd8}
@media (prefers-color-scheme:dark){:root{--bg:#101412;--card:#19201c;--ink:#e8ece9;--mut:#9aa59f;--acc:#5fc39a;--bad:#f08a76;--line:#2b342f}}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--ink);font:16px/1.45 system-ui,-apple-system,Segoe UI,Roboto,sans-serif}
main{max-width:460px;margin:0 auto;padding:20px 16px 40px}h1{font-size:1.35rem;margin:.2rem 0 1rem}
.card{background:var(--card);border:1px solid var(--line);border-radius:14px;padding:16px;margin-bottom:14px}
label{display:block;font-weight:600;margin:.6rem 0 .3rem}select,input{width:100%;font:inherit;padding:12px;border-radius:10px;border:1px solid var(--line);background:var(--bg);color:var(--ink)}
button{width:100%;font:inherit;font-weight:600;padding:13px;border:0;border-radius:10px;background:var(--acc);color:#fff;margin-top:14px}
button.sec{background:transparent;color:var(--acc);border:1px solid var(--line)}button:disabled{opacity:.5}
.lang{display:flex;gap:8px;justify-content:flex-end;margin-bottom:12px}.lang button{width:auto;margin:0;padding:6px 12px}.lang [aria-pressed=true]{border-color:var(--acc);font-weight:800}
.mut{color:var(--mut);font-size:.92rem}.bad{color:var(--bad)}.ok{color:var(--acc)}a{color:var(--acc);word-break:break-all}
</style></head><body><main>
<nav class="lang" aria-label="Suomi / English">
<button class="sec" data-lang="fi" lang="fi" aria-pressed="true" type="button">Suomi</button>
<button class="sec" data-lang="en" lang="en" aria-pressed="false" type="button">English</button>
</nav>
<h1 data-copy="heading">Enevina Lite – yhdistä kodin Wi-Fiin</h1>
<div class="card" id="form">
<p class="mut" data-copy="intro">Vaihe 1/2. Valitse kotisi 2,4 GHz Wi-Fi. Jatka laitteen käyttöönottoa kotiverkossa.</p>
<label for="net" data-copy="network">Wi-Fi-verkko</label>
<select id="net"><option value="">Haetaan verkkoja…</option></select>
<button class="sec" id="rescan" data-copy="scan" type="button">Hae verkot uudelleen</button>
<label for="ssid" data-copy="ssid">Verkon nimi</label>
<input id="ssid" maxlength="32" autocomplete="off" autocapitalize="none" spellcheck="false">
<label for="pw" data-copy="password">Wi-Fi-salasana</label>
<input id="pw" type="password" maxlength="64" autocomplete="current-password">
<label class="mut"><input id="show" type="checkbox" style="width:auto"> <span data-copy="show">Näytä salasana</span></label>
<button id="go" data-copy="connect" type="button">Yhdistä</button>
<button class="sec" id="retry-save" data-copy="retry" type="button" hidden>Yritä tallentaa uudelleen</button>
<p id="msg" class="mut" role="status" aria-live="polite"></p>
</div>
<div class="card" id="done" hidden>
<h2 class="ok" data-copy="done">Wi-Fi-yhteys tallennettu</h2>
<p><span data-copy="joined">Enevina Lite on nyt kotiverkossa</span> <b id="donenet"></b>. <span data-copy="open">Yhdistä puhelin takaisin kotiverkkoon ja avaa:</span></p>
<p id="urls"></p>
<p class="mut" data-copy="close">Tallenna osoite kirjanmerkiksi. Asennustukiasema sulkeutuu noin kahden minuutin kuluttua.</p>
</div>
<script>
const $=id=>document.getElementById(id);
const text={
 fi:{
  heading:'Enevina Lite – yhdistä kodin Wi-Fiin',
  intro:'Vaihe 1/2. Valitse kotisi 2,4 GHz Wi-Fi. Jatka laitteen käyttöönottoa kotiverkossa.',
  network:'Wi-Fi-verkko',scan:'Hae verkot uudelleen',ssid:'Verkon nimi',
  password:'Wi-Fi-salasana',show:'Näytä salasana',connect:'Yhdistä',
  retry:'Yritä tallentaa uudelleen',done:'Wi-Fi-yhteys tallennettu',
  joined:'Enevina Lite on nyt kotiverkossa',open:'Yhdistä puhelin takaisin kotiverkkoon ja avaa:',
  close:'Tallenna osoite kirjanmerkiksi. Asennustukiasema sulkeutuu noin kahden minuutin kuluttua.',
  scanning:'Haetaan verkkoja…',choose:'Valitse verkko…',other:'Muu (kirjoita nimi)',
  noNetworks:'Verkkoja ei löytynyt. Kirjoita verkon nimi.',openNet:'avoin',
  good:'hyvä',fair:'kohtalainen',weak:'heikko',
  joining:'Yhdistetään…',saving:'Wi-Fi-yhteys toimii. Tallennetaan asetuksia…',
  saveFailed:'Wi-Fi-yhteys toimii, mutta asetusten tallennus epäonnistui. Yritä tallentaa uudelleen.',
  saveOffline:'Asetusten tallennus epäonnistui ja Wi-Fi-yhteys katkesi. Odota yhteyden palautumista tai yhdistä verkkoon uudelleen.',
  reconnect:'Laitteen yhteys kotiverkkoon katkesi. Odota yhteyden palautumista tai valitse verkko.',
  lost:name=>'Puhelimen yhteys laitteeseen katkesi. Liity tarvittaessa uudelleen verkkoon '+name+'.',
  current:name=>'Nykyinen verkko on '+name+'. Voit vaihtaa sen tästä.',
  missing:'Valitse tai kirjoita verkon nimi.',short:'Wi-Fi-salasanassa on vähintään 8 merkkiä.',
  invalid:'Tarkista verkon nimi ja salasana.',again:'Yritä uudelleen.',
  WRONG_PASSWORD:'Salasana näyttää väärältä. Tarkista se ja yritä uudelleen.',
  NETWORK_NOT_FOUND:'Verkkoa ei löytynyt. Onko se 2,4 GHz:n verkko ja kantaman sisällä?',
  TIMEOUT:'Yhdistäminen ei onnistunut ajoissa. Yritä uudelleen lähempänä reititintä.',
  FAILED:'Yhdistäminen epäonnistui. Yritä uudelleen.',
 },
 en:{
  heading:'Enevina Lite – connect to home Wi-Fi',
  intro:'Step 1 of 2. Choose your home 2.4 GHz Wi-Fi. Continue setting up the device on your home network.',
  network:'Wi-Fi network',scan:'Scan again',ssid:'Network name',
  password:'Wi-Fi password',show:'Show password',connect:'Connect',
  retry:'Retry saving',done:'Wi-Fi connection saved',
  joined:'Enevina Lite is now on your home network',open:'Reconnect your phone to your home network and open:',
  close:'Bookmark the address. The setup access point will close in about two minutes.',
  scanning:'Scanning for networks…',choose:'Choose a network…',other:'Other (enter the name)',
  noNetworks:'No networks found. Enter the network name.',openNet:'open',
  good:'good',fair:'fair',weak:'weak',
  joining:'Connecting…',saving:'Wi-Fi is connected. Saving settings…',
  saveFailed:'Wi-Fi is connected, but the settings could not be saved. Retry saving.',
  saveOffline:'The settings could not be saved and Wi-Fi disconnected. Wait for it to reconnect or join the network again.',
  reconnect:'The device lost its home Wi-Fi connection. Wait for it to reconnect or choose a network.',
  lost:name=>'Your phone lost its connection to the device. Rejoin '+name+' if needed.',
  current:name=>'The current network is '+name+'. You can change it here.',
  missing:'Choose or enter a network name.',short:'The Wi-Fi password needs at least 8 characters.',
  invalid:'Check the network name and password.',again:'Try again.',
  WRONG_PASSWORD:'The password appears to be incorrect. Check it and try again.',
  NETWORK_NOT_FOUND:'Network not found. Check that it uses 2.4 GHz and is in range.',
  TIMEOUT:'The connection timed out. Try again closer to the router.',
  FAILED:'Could not connect. Try again.',
 }
};
const langKey='enevina_lite_language';
let language='fi';
try { if(localStorage.getItem(langKey)==='en') language='en'; } catch (_) {}
let token='',apName='Enevina-Lite',message='',messageArg='',messageClass='mut';
let networks=null,scanState='scanning',finishedStatus=null;
let attempt=0;
const tr=key=>text[language][key];
function say(key,style='mut',arg=''){
 message=key;messageClass=style;messageArg=arg;
 const value=tr(key);
 $('msg').textContent=typeof value==='function'?value(arg):value||'';
 $('msg').className=style;
}
function option(value,label){
 const node=document.createElement('option');node.value=value;node.textContent=label;
 return node;
}
function renderNetworks(){
 const select=$('net'),selected=select.value;
 select.replaceChildren(option('',tr(scanState)));
 if(!networks || !networks.length)return;
 for(const n of networks){
  const signal=n.rssi>-60?'good':n.rssi>-75?'fair':'weak';
  const suffix=n.secure?'':' ('+tr('openNet')+')';
  select.append(option(n.ssid,n.ssid+suffix+' · '+tr(signal)));
 }
 select.append(option('',tr('other')));
 if(networks.some(n=>n.ssid===selected))select.value=selected;
}
function renderLinks(s){
 $('urls').replaceChildren();
 for(const address of [s.url_primary,s.url_numeric]){
  if(!address)continue;
  const url=new URL(address);
  url.searchParams.set('ui_lang',language);
  if($('urls').children.length)$('urls').append(document.createElement('br'));
  const link=document.createElement('a');link.href=url.href;link.textContent=address;
  $('urls').append(link);
 }
}
function setLanguage(lang){
 language=lang;
 try{localStorage.setItem(langKey,lang)}catch(_){}
 document.documentElement.lang=lang;
 for(const node of document.querySelectorAll('[data-copy]'))node.textContent=tr(node.dataset.copy);
 for(const node of document.querySelectorAll('[data-lang]'))
  node.setAttribute('aria-pressed',String(node.dataset.lang===lang));
 say(message,messageClass,messageArg);
 renderNetworks();
 if(finishedStatus)renderLinks(finishedStatus);
}
for(const node of document.querySelectorAll('[data-lang]'))
 node.onclick=()=>setLanguage(node.dataset.lang);
async function scan(){
 $('rescan').disabled=true;networks=null;scanState='scanning';renderNetworks();
 for(let i=0;i<20;i++){
  try{
   const r=await fetch('/api/wifi/scan',{cache:'no-store'});
   const j=await r.json();
   if(Array.isArray(j.networks)){
    networks=j.networks.sort((a,b)=>b.rssi-a.rssi);
    scanState=networks.length?'choose':'noNetworks';
    renderNetworks();$('rescan').disabled=false;return;
   }
  }catch(_){}
  await new Promise(resolve=>setTimeout(resolve,1000));
 }
 scanState='noNetworks';renderNetworks();$('rescan').disabled=false;
}
$('net').onchange=()=>{if($('net').value)$('ssid').value=$('net').value};
$('rescan').onclick=scan;
$('show').onchange=()=>{$('pw').type=$('show').checked?'text':'password'};
function finished(s){
 finishedStatus=s;
 $('donenet').textContent=s.network;
 renderLinks(s);
 $('pw').value='';
 $('form').hidden=true;$('done').hidden=false;
}
function refreshAfterDelay(current){
 setTimeout(()=>{if(current===attempt)return refresh(true,current)},1500);
}
async function refresh(follow,current=attempt){
 try{
  const r=await fetch('/api/wifi/status',{cache:'no-store'});
  if(!r.ok)throw new Error('status');
  const s=await r.json();
  if(current!==attempt)return;
  token=s.token;apName=s.setup_ap_ssid||apName;
  const online=s.phase==='ONLINE';
  $('go').disabled=s.phase==='TRYING'||s.storage==='PENDING';
  $('retry-save').hidden=s.storage!=='FAILED';
  $('retry-save').disabled=!online;
  if(s.phase==='TRYING'||s.storage==='PENDING'){
   say(s.phase==='TRYING'?'joining':'saving');refreshAfterDelay(current);return;
  }
  if(s.storage==='FAILED'){
   say(online?'saveFailed':'saveOffline','bad');
   if(!online)refreshAfterDelay(current);
   return;
  }
  if(online && s.last_result==='CONNECTED' && s.storage==='SAVED' &&
     (follow || s.setup_ap_active)){
   finished(s);return;
  }
  if(s.last_result in text[language])say(s.last_result,'bad');
  else if(online && s.network)say('current','mut',s.network);
  else if(s.phase==='RECOVERY'||s.phase==='RECONNECTING')say('reconnect');
 }catch(_){
  if(current!==attempt)return;
  say('lost','mut',apName);
  if(follow)refreshAfterDelay(current);
 }
}
async function submit(url,values){
 const current=++attempt;
 $('go').disabled=true;$('retry-save').disabled=true;
 try{
  const r=await fetch(url,{method:'POST',body:new URLSearchParams({...values,token})});
  if(!r.ok){
   await refresh(false,current);
   if(current!==attempt)return;
   if(r.status===403)say('again','bad');
   else if(url.endsWith('/connect'))say('invalid','bad');
   return;
  }
 }catch(_){ /* the device may have accepted the request before the link changed */ }
 await refresh(true,current);
}
$('go').onclick=()=>{
 const ssid=$('ssid').value,pw=$('pw').value;
 if(!ssid.trim()){say('missing','bad');return}
 if(pw&&pw.length<8){say('short','bad');return}
 say('joining');
 return submit('/api/wifi/connect',{ssid,password:pw});
};
$('retry-save').onclick=()=>{
 say('saving');
 return submit('/api/wifi/retry-save',{});
};
setLanguage(language);
refresh(false);
scan();
</script></main></body></html>)HTML";
#endif
