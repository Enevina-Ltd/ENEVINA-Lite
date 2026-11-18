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
.mut{color:var(--mut);font-size:.92rem}.bad{color:var(--bad)}.ok{color:var(--acc)}a{color:var(--acc);word-break:break-all}
</style></head><body><main>
<h1>Enevina Lite – yhdistä kodin Wi-Fiin</h1>
<div class="card" id="form">
<p class="mut">Vaihe 1/2: valitse vain kotisi 2,4 GHz Wi-Fi. Lämpöpumppu ja muut asetukset tehdään seuraavaksi kotiverkossa.</p>
<label for="net">Wi-Fi-verkko</label>
<select id="net"><option value="">Haetaan verkkoja…</option></select>
<button class="sec" id="rescan" type="button">Hae verkot uudelleen</button>
<label for="ssid">Verkon nimi</label>
<input id="ssid" maxlength="32" autocomplete="off" autocapitalize="none" spellcheck="false">
<label for="pw">Wi-Fi-salasana</label>
<input id="pw" type="password" maxlength="64" autocomplete="current-password">
<label class="mut"><input id="show" type="checkbox" style="width:auto"> Näytä salasana</label>
<button id="go" type="button">Yhdistä</button>
<p id="msg" class="mut" role="status" aria-live="polite"></p>
</div>
<div class="card" id="done" hidden>
<h2 class="ok">Yhteys toimii</h2>
<p>Enevina Lite on nyt kotiverkossa <b id="donenet"></b>. Yhdistä puhelin takaisin kotiverkkoon ja avaa:</p>
<p id="urls"></p>
<p class="mut">Tallenna osoite kirjanmerkiksi. Asennustukiasema sulkeutuu noin kahden minuutin kuluttua.</p>
</div>
<script>
const $=id=>document.getElementById(id);let token='';let polling=null;
const say=(t,c)=>{$('msg').textContent=t;$('msg').className=c||'mut'};
const reasons={WRONG_PASSWORD:'Salasana näyttää väärältä. Tarkista se ja yritä uudelleen.',
NETWORK_NOT_FOUND:'Verkkoa ei löytynyt. Onko se 2,4 GHz:n verkko ja kantaman sisällä?',
TIMEOUT:'Yhdistäminen ei onnistunut ajoissa. Yritä uudelleen lähempänä reititintä.',
FAILED:'Yhdistäminen epäonnistui. Yritä uudelleen.'};
async function status(){const r=await fetch('/api/wifi/status',{cache:'no-store'});return r.json()}
async function scan(){$('net').innerHTML='<option value="">Haetaan verkkoja…</option>';
 for(let i=0;i<20;i++){try{const r=await fetch('/api/wifi/scan',{cache:'no-store'});const j=await r.json();
  if(j.networks){$('net').innerHTML='<option value="">Valitse verkko…</option>'+
   j.networks.sort((a,b)=>b.rssi-a.rssi).map(n=>{const o=document.createElement('option');o.value=n.ssid;
    o.textContent=n.ssid+(n.secure?'':' (avoin)')+' · '+(n.rssi>-60?'hyvä':n.rssi>-75?'kohtalainen':'heikko');return o.outerHTML}).join('')+
   '<option value="">Muu (kirjoita nimi)</option>';return}}catch(e){}
  await new Promise(r=>setTimeout(r,1000))}
 $('net').innerHTML='<option value="">Verkkoja ei löytynyt – kirjoita nimi</option>'}
$('net').onchange=()=>{if($('net').value)$('ssid').value=$('net').value};
$('rescan').onclick=scan;$('show').onchange=()=>{$('pw').type=$('show').checked?'text':'password'};
function finished(s){$('form').hidden=true;$('done').hidden=false;$('donenet').textContent=s.network;
 const u=[s.url_primary,s.url_numeric].filter(Boolean);
 $('urls').innerHTML=u.map(x=>'<a href="'+x+'">'+x+'</a>').join('<br>');}
async function poll(){try{const s=await status();
 if(s.phase==='TRYING'){say('Yhdistetään verkkoon '+$('ssid').value+'…');return}
 clearInterval(polling);polling=null;$('go').disabled=false;
 if(s.last_result==='CONNECTED'&&s.phase==='ONLINE'){finished(s);return}
 say(reasons[s.last_result]||reasons.FAILED,'bad')}catch(e){
 say('Puhelimen yhteys laitteeseen katkesi hetkeksi. Yhdistä tarvittaessa uudelleen verkkoon '+(window.apName||'Enevina-Lite')+'.','mut')}}
$('go').onclick=async()=>{const ssid=$('ssid').value.trim(),pw=$('pw').value;
 if(!ssid){say('Valitse tai kirjoita verkon nimi.','bad');return}
 if(pw&&pw.length<8){say('Wi-Fi-salasanassa on vähintään 8 merkkiä.','bad');return}
 $('go').disabled=true;say('Yhdistetään…');
 try{const r=await fetch('/api/wifi/connect',{method:'POST',body:new URLSearchParams({ssid,password:pw,token})});
  if(r.status===403){const s=await status();token=s.token;$('go').disabled=false;say('Yritä uudelleen.','bad');return}
  if(!r.ok){$('go').disabled=false;say('Tarkista verkon nimi ja salasana.','bad');return}
  polling=setInterval(poll,1500)}catch(e){polling=setInterval(poll,1500)}};
status().then(s=>{token=s.token;window.apName=s.setup_ap_ssid;
 if(s.phase==='ONLINE'&&s.network)say('Nykyinen verkko: '+s.network+'. Voit vaihtaa sen tästä.')}).catch(()=>{});
scan();
</script></main></body></html>)HTML";
#endif
