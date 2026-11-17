#pragma once
#include <Arduino.h>
// C2.5: entirely local self-contained page; no CDN, cloud, device writes or external assets.
// PROGMEM allows flash-backed streaming through WebServer::send_P.
static const char premium_dashboard_html[] PROGMEM = R"ENEVINA_UI(<!doctype html>
<html lang="fi">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<meta name="color-scheme" content="light">
<title>Enevina Lite | Energianhallinta</title>
<style>
:root{--bg:#f5f5f0;--paper:#ffffff;--ink:#17352e;--muted:#697a73;--edge:#e2e9e2;--green:#276b52;--deep:#103d31;--mint:#e5f2e7;--warning:#8c6418;--warnbg:#fff6e6;--red:#af4c3f;--shadow:0 7px 24px rgba(29,60,47,.05)}
*{box-sizing:border-box}html{scroll-behavior:smooth}body{margin:0;font:15px/1.5 system-ui,-apple-system,"Segoe UI",sans-serif;background:var(--bg);color:var(--ink)}
button,input{font:inherit}button{cursor:pointer}a{color:inherit;text-decoration:none}h1,h2,h3,p{margin-top:0}h1{font-size:30px;line-height:1.2;letter-spacing:-.8px}h2{font-size:18px;letter-spacing:-.3px}h3{font-size:15px}
.shell{max-width:1640px;min-height:100vh;margin:auto;display:grid;grid-template-columns:220px minmax(0,1fr)}
.sidebar{padding:27px 18px;background:#fdfefa;border-right:1px solid var(--edge);position:sticky;top:0;height:100vh;display:flex;flex-direction:column;gap:25px}
.brand{display:flex;align-items:center;gap:12px;padding:7px 9px}.brand-mark{background:var(--deep);color:#fff;border-radius:14px;padding:8px 12px;font-size:23px;font-weight:700}.brand-name{font-size:23px;font-weight:730;letter-spacing:-1px}.brand-name small{font-size:11px;display:block;text-transform:uppercase;letter-spacing:2px;color:var(--muted)}
.nav{display:grid;gap:7px}.nav button{width:100%;text-align:left;display:flex;align-items:center;gap:12px;border:0;background:transparent;border-radius:12px;padding:13px 15px;color:#52665d}.nav button.active{background:var(--mint);color:var(--deep);font-weight:700}.nav button:focus-visible,.mode button:focus-visible{outline:3px solid #589d80}.nav .i{width:23px;font-size:18px;text-align:center}.aside-foot{margin-top:auto;font-size:12px;color:var(--muted);padding:10px}
.main{min-width:0;padding:26px clamp(16px,3vw,48px) 60px}
.top{display:flex;justify-content:space-between;align-items:center;gap:14px;margin-bottom:20px}.eyebrow{font-size:11px;font-weight:760;letter-spacing:1.5px;text-transform:uppercase;color:var(--muted);margin-bottom:6px}
.chip{display:inline-flex;align-items:center;gap:7px;border:1px solid var(--edge);background:#fff;border-radius:99px;padding:7px 11px;color:#475c50;font-size:12px}.dot{width:8px;height:8px;background:#acb8b0;border-radius:50%}.dot.ok{background:#2c9e66}.dot.bad{background:#c77949}
.notice{display:flex;gap:12px;align-items:flex-start;padding:13px 16px;border:1px solid #f0dfb8;background:var(--warnbg);border-radius:14px;color:#744c13;margin-bottom:22px;font-size:13px}.notice strong{font-weight:800}
.view{display:none}.view.active{display:block}.heading{display:flex;align-items:flex-end;justify-content:space-between;gap:12px;margin-bottom:24px}.heading p{margin:8px 0 0;color:var(--muted)}
.grid{display:grid;grid-template-columns:repeat(12,minmax(0,1fr));gap:17px}.card{background:var(--paper);border:1px solid var(--edge);border-radius:20px;box-shadow:var(--shadow);padding:23px;min-width:0}.card h2{margin-bottom:15px}
.span7{grid-column:span 7}.span5{grid-column:span 5}.span4{grid-column:span 4}.span8{grid-column:span 8}.span12{grid-column:span 12}
.muted{color:var(--muted)}.tiny{font-size:12px}.metric{font-size:48px;line-height:1.12;font-weight:750;letter-spacing:-2px}.metric.small{font-size:32px;letter-spacing:-1px}.unit{font-size:17px;font-weight:500;letter-spacing:0}.flex{display:flex;align-items:center;justify-content:space-between;gap:12px}.pills{display:flex;flex-wrap:wrap;gap:8px;margin-top:17px}.pill{font-size:12px;color:var(--green);background:#e7f3e9;padding:7px 10px;border-radius:99px}
.temperature{background:linear-gradient(130deg,#1b5544,#103e33);color:#fff;border:0;min-height:245px;display:flex;flex-direction:column;justify-content:space-between}.temperature .muted{color:#c5ded1}.temperature .metric{font-size:67px}.temperature .pill{background:rgba(255,255,255,.13);color:#fff;display:inline-block}
.statline{display:flex;justify-content:space-between;gap:10px;border-top:1px solid var(--edge);padding:10px 0}.statline:first-of-type{border:0}.statline span:last-child{font-weight:650;text-align:right}
.mode{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:10px}.mode button{border:1px solid var(--edge);background:#fafcf9;border-radius:13px;padding:14px 8px;color:var(--ink)}.mode button.selected{border:2px solid var(--green);background:var(--mint);font-weight:700}.mode button:disabled{opacity:.5;cursor:not-allowed}
.fieldrow{display:flex;gap:10px;align-items:end;flex-wrap:wrap}.field{display:grid;gap:4px;font-size:12px;color:var(--muted)}.field input{width:110px;border:1px solid #c5d2c8;border-radius:9px;padding:9px;color:var(--ink);background:#fff}.primary{background:var(--deep);color:#fff;border:0;border-radius:10px;padding:11px 15px;font-weight:700}.primary:disabled{opacity:.5;cursor:not-allowed}
.subhead{margin:0 0 6px;font-size:13px;color:var(--muted)}.callout{background:#f3f8f2;border-radius:12px;padding:13px 15px;color:#365c4b;font-size:13px}.callout.warn{background:var(--warnbg);color:#744c13}
.chart{width:100%;overflow:hidden}.bars{height:180px;display:flex;align-items:flex-end;gap:2px;border-bottom:1px solid #c5d4cb;padding:8px 0 0;overflow:hidden}.bar{min-width:2px;flex:1;border:0;border-radius:3px 3px 0 0;background:#8dbea2}.bar.cheap{background:#2c855f}.bar.high{background:#c6a65a}.bar.current{outline:2px solid #17352e;outline-offset:-1px}.ticks{display:flex;justify-content:space-between;font-size:11px;color:var(--muted);margin-top:8px}.legend{display:flex;gap:18px;flex-wrap:wrap;color:var(--muted);font-size:12px;margin-top:8px}
.tablewrap{overflow:auto}table{width:100%;border-collapse:collapse;text-align:left;min-width:540px}td,th{border-bottom:1px solid var(--edge);padding:12px 8px}th{font-size:11px;letter-spacing:.7px;text-transform:uppercase;color:var(--muted)}td{font-size:13px}.heat{color:var(--green);font-weight:750}.off{color:#7a877b}
.spark{height:124px;width:100%;display:block}.empty{padding:38px 14px;text-align:center;color:var(--muted);font-size:13px;border:1px dashed #c7d4c9;border-radius:12px}
.foot{color:var(--muted);font-size:12px;margin-top:22px}#feedback{font-size:12px;margin-top:10px}.statusgrid{display:grid;grid-template-columns:1fr auto;gap:9px;font-size:13px}.statusgrid span:nth-child(2n){font-weight:650;text-align:right}
@media(max-width:1100px){.grid{gap:12px}.span7,.span5{grid-column:span 6}.span4{grid-column:span 4}.span8{grid-column:span 8}}
@media(max-width:850px){.shell{display:block}.sidebar{position:static;height:auto;padding:9px 14px;gap:5px;border-right:0;border-bottom:1px solid var(--edge)}.brand{padding:3px}.brand-mark{font-size:16px;padding:5px 9px;border-radius:9px}.brand-name{font-size:20px}.brand-name small{display:none}.nav{display:flex;overflow:auto;gap:4px}.nav button{width:auto;white-space:nowrap;padding:9px 11px;font-size:12px}.nav .i{font-size:13px;width:auto}.aside-foot{display:none}.main{padding:15px 14px 40px}.top{margin-bottom:12px}.heading{margin-bottom:18px}h1{font-size:23px}.card{padding:16px}}
@media(max-width:600px){.span7,.span5,.span4,.span8,.span12{grid-column:span 12}.metric{font-size:39px}.temperature .metric{font-size:56px}.mode{grid-template-columns:repeat(2,minmax(0,1fr))}.mode button{padding:10px 5px;font-size:12px}.chip{font-size:11px}.bars{height:155px}}
@media(prefers-reduced-motion:reduce){html{scroll-behavior:auto}}
</style>
</head>
<body>
<div class="shell">
<aside class="sidebar" aria-label="Päävalikko">
  <div class="brand"><span class="brand-mark">e</span><span class="brand-name">enevina<small>Lite energy</small></span></div>
  <nav class="nav" aria-label="Näkymät">
    <button class="active" data-view="home" aria-current="page"><span class="i">⌂</span>Etusivu</button>
    <button data-view="prices"><span class="i">▥</span>Sähkön hinta</button>
    <button data-view="plan"><span class="i">▤</span>Optimointi</button>
    <button data-view="settings"><span class="i">⚙</span>Asetukset</button>
  </nav>
  <div class="aside-foot">Enevina Lite · paikallinen ohjain<br><a href="/#settings">Laiteasetukset</a></div>
</aside>
<main class="main">
  <div class="top"><div><div class="eyebrow">Energia · Pihamökki</div><strong id="clock">Aikaa ei ole synkronoitu</strong></div><span class="chip"><span id="connection-dot" class="dot"></span><span id="connection">Yhdistetään</span></span></div>
  <div class="notice" role="status"><strong id="pilot-state-heading">OHJAUS POIS</strong><span id="pilot-state-text">Ota CN105-automaattiohjaus käyttöön Asetukset-näkymästä. Optimointiehdotus ei yksin lähetä käskyjä.</span></div>
  <section class="view active" id="view-home">
    <div class="heading"><div><div class="eyebrow">Yleisnäkymä</div><h1>Pihamökin olosuhteet</h1><p>Mittaukset, sähkön hinta ja tuleva lämmityssuunnitelma.</p></div></div>
    <div class="grid">
      <article class="card temperature span7"><div><div class="eyebrow" style="color:#c5ded1">Sisälämpötila · RuuviTag</div><div class="metric" id="home-temp">—</div></div><div><span class="pill" id="home-mode">Tila ei tiedossa</span><p class="tiny muted" id="home-sensor" style="margin:12px 0 0">Odotetaan mittausta</p></div></article>
      <article class="card span5"><div class="eyebrow">Sähkön hinta nyt</div><div class="metric small" id="home-price">—</div><p class="muted tiny" id="home-price-label">Spot-hinta · ilman arvonlisäveroa</p><div class="statline"><span>Arvio kokonaishinnasta</span><span id="home-total">—</span></div><div class="statline"><span>Ulkolämpötila / ennuste</span><span id="home-outside">—</span></div><div class="statline"><span>Mittauskulutus</span><span id="home-watts">—</span></div></article>
      <article class="card span4"><div class="eyebrow">Ilmankosteus</div><div class="metric small" id="home-rh">—</div><p class="muted tiny">RuuviTag · suhteellinen kosteus</p><div class="statline"><span>Kastepiste</span><span id="home-dew">—</span></div><div class="statline"><span>Anturin tila</span><span id="home-air-status">—</span></div></article>
      <article class="card span8"><div class="flex"><h2>Sähkön tulevat hinnat</h2><span class="muted tiny">15 minuutin jaksot</span></div><div id="home-chart" class="chart"><div class="empty">Hintadataa odotetaan</div></div><div class="flex" style="margin-top:12px"><span class="muted tiny" id="home-cheap">Halvinta jaksoa ei tiedossa</span><button class="primary" id="go-prices" type="button">Kaikki hinnat</button></div></article>
      <article class="card span12"><div class="flex"><h2>Optimoinnin seuraava toimenpide</h2><span class="chip" id="home-plan-state">Ei suunnitelmaa</span></div><p id="home-plan-next">Hyväksyttyä optimointisuunnitelmaa ei vielä ole.</p><div class="callout" id="home-plan-reason">Optimointi näkyy vasta, kun käytettävissä on kelvollinen, turvallinen ja kalibroitu laskentapohja.</div></article>
    </div>
  </section>
  <section class="view" id="view-prices">
    <div class="heading"><div><div class="eyebrow">Pörssisähkö</div><h1>Sähkön hinnat</h1><p>Suomen 15 minuutin hinnat. Kaavio näyttää vain ladatun tulevan datan.</p></div></div>
    <div class="grid">
      <article class="card span4"><div class="eyebrow">Nykyinen spot</div><div class="metric small" id="price-now">—</div><p class="tiny muted">snt/kWh, alv 0 %</p></article>
      <article class="card span4"><div class="eyebrow">Edullisin tuleva jakso</div><div class="metric small" id="price-low">—</div><p class="tiny muted" id="price-low-time">Ei tietoa</p></article>
      <article class="card span4"><div class="eyebrow">Saatavilla oleva ennuste</div><div class="metric small" id="price-count">—</div><p class="tiny muted" id="price-updated">Tietolähdettä ei ole päivitetty</p></article>
      <article class="card span12"><div class="flex"><h2>Hintakehitys</h2><span class="chip">Nord Pool · FI</span></div><div id="prices-chart" class="chart"><div class="empty">Hintoja ei ole saatavilla</div></div><p class="foot">Spot-hinta on ilman veroja. Kokonaishinnan arviossa sovelletaan laitteelle määriteltyjä sopimuskohtaisia vero- ja siirtomaksuja. Hintoja ei arvata puuttuville varttitunneille.</p></article>
      <article class="card span12"><h2>Seuraavat hintajaksot</h2><div class="tablewrap"><table><thead><tr><th>Alkaa</th><th>Spot snt/kWh</th><th>Kokonaishinta snt/kWh</th></tr></thead><tbody id="price-rows"><tr><td colspan="3">Odota hintatietoja.</td></tr></tbody></table></div></article>
    </div>
  </section>
  <section class="view" id="view-plan">
    <div class="heading"><div><div class="eyebrow">Ennakoiva lämmitys</div><h1>Optimointisuunnitelma</h1><p>Hintaohjaus tuottaa ehdotuksen; CN105-komennot edellyttävät erillistä käyttöönottoa.</p></div></div>
    <div class="grid"><article class="card span4"><div class="eyebrow">Suunnitelma</div><div class="metric small" id="plan-status">—</div><p class="tiny muted" id="plan-time">Ei kelvollista laskelmaa</p></article><article class="card span4"><div class="eyebrow">Ennustettu kustannus</div><div class="metric small" id="plan-cost">—</div><p class="tiny muted">Mallin kokonaisarvio, jos kelvollinen</p></article><article class="card span4"><div class="eyebrow">Ennustettu alin lämpötila</div><div class="metric small" id="plan-min">—</div><p class="tiny muted">Laskettu, ei anturimittaus</p></article>
      <article class="card span12"><div id="plan-warning" class="callout warn">Hyväksyttyä suunnitelmaa ei ole. Lämpöpumppu ei saa tältä käyttöliittymältä komentoja.</div><div class="tablewrap"><table><thead><tr><th>Alkaa</th><th>Ehdotus</th><th>Ennuste °C</th><th>Sähkö kWh</th><th>Arvio €</th></tr></thead><tbody id="plan-rows"><tr><td colspan="5">Suunnitelma ei saatavilla.</td></tr></tbody></table></div></article>
    </div>
  </section>
  <section class="view" id="view-settings">
    <div class="heading"><div><div class="eyebrow">Olosuhteet ja laite</div><h1>Asetukset ja diagnostiikka</h1><p>Paikallinen huonetavoite ja erikseen käyttöönotettava CN105-lämmitysohjaus.</p></div></div>
    <div class="grid">
      <article class="card span7"><h2>Pihamökin toimintatila</h2><p class="tiny muted">Ylläpitolämpö ja Asuttu käyttävät omia, toisistaan riippumattomia huonetavoitteitaan. Hintaohjaus voi säätää Mitsubishin pyyntilämpötilaa.</p><div class="mode" role="group" aria-label="Laskennallinen tila"><button type="button" data-mode="0">Kylmillään</button><button type="button" data-mode="1">Kuivanapito</button><button type="button" data-mode="2">Ylläpitolämpö</button><button type="button" data-mode="3">Asuttu</button></div><div class="fieldrow" style="margin-top:17px"><label class="field">Ylläpitotavoite °C<input id="setting-hold" type="number" min="5" max="24" step=".5" inputmode="decimal"></label><label class="field">Asuttu °C<input id="setting-occupied" type="number" min="16" max="26" step=".5" inputmode="decimal"></label><label class="field">Kuivanapito Δ°C<input id="setting-delta" type="number" min="3" max="5" step=".5" inputmode="decimal"></label><button class="primary" id="save-mode" type="button" disabled>Tallenna asetukset</button></div><div id="feedback" role="status"></div><p class="tiny muted">Valitse Ylläpitolämpö tai Asuttu sekä kyseisen tilan huonetavoite. Hintaehdotus laskee Mitsubishille 16–26 °C pyyntilämpötilan 0,5 °C askelin; komennot sallitaan vain erillisellä käyttöönotolla.</p></article>
      <article class="card span12"><h2>Mitsubishi · automaattinen lämpötilan säätö</h2><p class="tiny muted">Laskenta käyttää RuuviTagia ja sähkön kokonaishintaa. Lämmitystä ohjataan Ylläpitolämpö- ja Asuttu-tiloissa. Komento lähtee CN105:lle, ja tavoitelämpötila tarkistetaan takaisinlukemalla. Käyttöönotto on aina erillinen valinta eikä firmwarepäivitys käynnistä sitä.</p><div class="flex"><span class="chip" id="control-state">Ohjaus ei käytössä</span><span id="control-last">Ei komentoja</span><button type="button" id="control-enable" class="primary">Ota automaattiohjaus käyttöön</button><button type="button" id="control-disable">Poista automaattiohjaus käytöstä</button></div><p id="control-feedback" role="status"></p></article>
      <article class="card span5"><h2>Laitteen tila</h2><div class="statusgrid"><span>Ohjelmistoversio</span><span id="device-version">—</span><span>Wi-Fi RSSI</span><span id="device-wifi">—</span><span>CN105-telemetria</span><span id="device-cn105">—</span><span>Shelly-mittaus</span><span id="device-shelly">—</span><span>Sisäinen vapaa RAM</span><span id="device-ram">—</span><span>PSRAM vapaana</span><span id="device-psram">—</span><span>OTA-lataus käytössä</span><span id="device-ota">—</span></div><p class="foot"><a href="/#settings" style="text-decoration:underline">Avaa laiteasetukset</a></p></article>
      <article class="card span12"><h2>Mitsubishi CN105 · vastaanotettu data</h2><div class="statusgrid"><span>Viimeisin SSE-tilaviesti</span><span id="mitsubishi-read-status">—</span><span>Pumpun tila (SSE)</span><span id="mitsubishi-read-mode">—</span><span>Pumpun sisälämpötila (SSE)</span><span id="mitsubishi-read-temp">—</span><span>Asetuslämpötila (SSE)</span><span id="mitsubishi-read-setpoint">—</span><span>Kompressori (SSE)</span><span id="mitsubishi-read-hz">—</span><span>Kompressoriviestin ikä</span><span id="mitsubishi-compressor-age">—</span></div><p class="foot">ESPHome /events -rajapinnasta vastaanotettu viimeisin tilatieto. Viestin vastaanottoaika ei todista uutta CN105-UART-lukua, eikä näitä arvoja käytetä optimoinnin turvallisuushyväksyntään.</p></article>
      <article class="card span12"><h2>RuuviTag · anturit ja kosteusvaroitukset</h2>
      <p class="tiny muted">Anturin MAC-tunniste on sidottava tarkoituksella. Kirjoita tunniste tai <code>none</code> irrottaaksesi sidonnan. Asetukset ovat vain paikallisia.</p>
      <div class="statusgrid"><span>Sisäanturin sidonta</span><span id="ruuvi-bound">—</span><span>Havaittu tunniste</span><span id="ruuvi-discovered">—</span></div>
      <div class="fieldrow"><label class="field">Anturin rooli<select id="ruuvi-role"><option value="indoor">Mökin sisäilma</option><option value="outdoor">Ulkoilma</option></select></label>
      <label class="field">RuuviTag MAC<input id="ruuvi-mac" placeholder="XX:XX:XX:XX:XX:XX tai none" autocomplete="off"></label>
      <button type="button" id="ruuvi-bind" class="primary">Tallenna anturisidonta</button></div>
      <p id="ruuvi-feedback" role="status"></p>
      <div class="fieldrow"><label class="field">RH-varoitus %<input id="alert-rh70" type="number" min="55" max="80" step="1"></label>
      <label class="field">Korkea RH-varoitus %<input id="alert-rh80" type="number" min="75" max="95" step="1"></label>
      <label class="field">Paristovaroitus V<input id="alert-battery" type="number" min="2" max="3.2" step=".05"></label>
      <button type="button" id="alerts-save">Tallenna varoitusrajat</button></div>
      <p id="alerts-feedback" role="status"></p><div id="alerts-active" class="pills"></div>
      <p class="tiny muted">Ilman RH-arvot eivät yksin osoita rakenteiden kosteusturvallisuutta.</p></article>
      <article class="card span12"><h2>Sisäilman historia · RuuviTag</h2><div id="history-chart" class="chart"><div class="empty">Historiatietoja odotetaan</div></div><p class="foot">Viimeisimmät mittaukset, ei rakenteen kosteusmittaus. Offline-ajalta tallennettuja aikaleimoja ei esitetä UTC-aikana.</p></article>
    </div>
  </section>
  <div class="foot">Enevina Lite · Paikallinen toiminta · <a href="/#settings" style="text-decoration:underline">Asetukset</a></div>
</main>
</div>
<script>
'use strict';
const $=id=>document.getElementById(id);
const state={status:null,prices:null,plan:null,history:null,mode:0,settingsDirty:false,settingsRevision:0,settingsSaving:false,statusRequestSequence:0,alertSettingsLoaded:false};
const fmt=(v,d=1)=>Number.isFinite(v)?v.toLocaleString('fi-FI',{minimumFractionDigits:d,maximumFractionDigits:d}):'—';
const text=(id,value)=>{const e=$(id);if(e)e.textContent=value==null?'—':String(value);};
const time=sec=>Number.isFinite(sec)&&sec>1700000000?new Date(sec*1000).toLocaleString('fi-FI',{timeZone:'Europe/Helsinki',day:'2-digit',month:'2-digit',hour:'2-digit',minute:'2-digit'}):'Ei aikaa';
const number=v=>typeof v==='number'&&Number.isFinite(v)?v:null;
const modes=['Kylmillään','Kuivanapito','Ylläpitolämpö','Asuttu'];
const priceFmt=v=>number(v)===null?'—':fmt(v*100,2)+' snt/kWh';
function switchView(name){
  if(!['home','prices','plan','settings'].includes(name))return;
  document.querySelectorAll('.view').forEach(v=>v.classList.toggle('active',v.id==='view-'+name));
  document.querySelectorAll('[data-view]').forEach(b=>{const active=b.dataset.view===name;b.classList.toggle('active',active);if(active)b.setAttribute('aria-current','page');else b.removeAttribute('aria-current');});
  history.replaceState(null,'','#'+name);
}
document.querySelectorAll('[data-view]').forEach(b=>b.addEventListener('click',()=>switchView(b.dataset.view)));
window.addEventListener('hashchange',()=>switchView(location.hash.slice(1)));
$('go-prices').addEventListener('click',()=>switchView('prices'));
switchView(location.hash.slice(1)||'home');
function drawBars(target,items){
  const box=$(target);box.replaceChildren();
  const usable=items.filter(x=>x&&Number.isFinite(x.spot));
  if(!usable.length){box.innerHTML='<div class="empty">Voimassa olevaa hintadataa ei saatavilla</div>';return;}
  const list=usable.slice(0,96),lo=Math.min(...list.map(x=>x.spot)),hi=Math.max(...list.map(x=>x.spot)),range=Math.max(.002,hi-lo);
  const wrap=document.createElement('div');wrap.className='bars';wrap.setAttribute('role','img');wrap.setAttribute('aria-label','Tulevien 15 minuutin sähkönhintojen pylväskaavio');
  list.forEach((p,i)=>{const bar=document.createElement('div');bar.className='bar'+(p.spot<=lo+range*.33?' cheap':p.spot>=lo+range*.75?' high':'')+(i===0?' current':'');
    bar.style.height=Math.max(7,((p.spot-lo)/range)*155+8)+'px';bar.title=time(p.utc)+': '+priceFmt(p.spot);wrap.append(bar);});
  const ticks=document.createElement('div');ticks.className='ticks';
  [0,Math.floor((list.length-1)/3),Math.floor(2*(list.length-1)/3),list.length-1].forEach(i=>{let t=document.createElement('span');t.textContent=new Date(list[i].utc*1000).toLocaleTimeString('fi-FI',{hour:'2-digit',minute:'2-digit',timeZone:'Europe/Helsinki'});ticks.append(t);});
  box.append(wrap,ticks);
  const caption=document.createElement('div');caption.className='legend';caption.textContent='Vaihteluväli: '+priceFmt(lo)+' – '+priceFmt(hi);box.append(caption);
}
function showStatus(){
  const s=state.status;if(!s)return;
  text('connection',s.air&&s.air.sensor_fresh?'Mittaukset käytössä':'Yhteys muodostettu');
  $('connection-dot').className='dot '+(s.air&&s.air.sensor_fresh?'ok':'bad');
  text('clock',time(Math.floor(Date.now()/1000)));
  const a=s.air||{},fresh=a.sensor_fresh===true,mode=Number.isInteger(a.mode)?a.mode:0;
  text('home-temp',fresh?fmt(number(a.temperature_c),1)+' °C':'Ei tuoretta mittausta');
  text('home-mode',modes[mode]||'Ei tietoa');
  text('home-sensor',fresh?'RuuviTag · '+fmt(number(a.age_s),0)+' s vanha':'RuuviTag ei ole tuore tai anturia ei ole sidottu');
  text('home-rh',fresh&&number(a.rh_pct)!==null?fmt(a.rh_pct,1)+' %':'—');
  text('home-dew',fresh&&number(a.dewpoint_c)!==null?fmt(a.dewpoint_c,1)+' °C':'—');
  text('home-air-status',fresh?'Tuore':'Puuttuu / vanhentunut');
  text('home-price',priceFmt(s.spot));text('home-total',priceFmt(s.total_price));
  text('home-outside',number(a.outside_temperature_c)!==null?fmt(a.outside_temperature_c,1)+' °C':number(s.weather_c)!==null?fmt(s.weather_c,1)+' °C':'—');
  text('home-watts',number(s.shelly_w)!==null?fmt(s.shelly_w,0)+' W':'—');
  text('price-now',priceFmt(s.spot));text('price-updated','Viimeisin haku: '+time(s.market_last_fetch_utc));
  text('pilot-state-heading',s.control_armed===true?'CN105-OHJAUS SALLITTU':'OHJAUS POIS');
  text('pilot-state-text',s.control_armed===true?
    'Automaattiohjaus on kytketty. Käskyjä lähetetään Ylläpitolämpö- tai Asuttu-tilassa, kun mittaus- ja yhteysportit ovat kunnossa.':
    'Automaattiohjaus ei ole päällä. Laskenta näyttää ehdotuksia ilman fyysisiä käskyjä.');
  text('control-state',s.control_armed===true?
    'Automaattiohjaus valittu · '+(s.control_gate||'odottaa'): 'Automaattiohjaus pois');
  text('control-last', 'Komentoja '+(s.control_commands_sent||0)+
    ' · sillalta kuitattu '+(s.control_commands_confirmed||0)+
    ' · virheitä '+(s.control_commands_failed||0));
  $('control-enable').disabled=s.control_armed===true || !a.config_token;
  $('control-disable').disabled=s.control_armed!==true || !a.config_token;
  text('device-version',s.firmware_version||'—');text('device-wifi',number(s.wifi_rssi_dbm)!==null?fmt(s.wifi_rssi_dbm,0)+' dBm':'Ei Wi-Fiä');
  const hasSse=s.cn105_sse_climate_received===true;
  const climateAge=number(s.cn105_sse_climate_age_s);
  const compressorAge=number(s.cn105_sse_compressor_age_s);
  text('device-cn105',s.cn105_source_verified===true?'UART-lähde varmennettu':hasSse?'SSE-lukemia, UART-todiste puuttuu':'Ei CN105-lukemia');
  text('mitsubishi-read-status',s.cn105_source_verified===true?'UART-mittaus varmennettu':hasSse?
    'SSE vastaanotettu '+(climateAge===null?'(ikä ei tiedossa)':fmt(climateAge,0)+' s sitten')+' · ei UART-todistetta':'Ei tilatietoja');
  text('mitsubishi-read-mode',s.cn105_source_verified===true?s.pump_mode:s.cn105_sse_mode);
  text('mitsubishi-read-temp',(s.cn105_source_verified===true?number(s.pump_indoor_c):number(s.cn105_sse_indoor_c))===null?'—':
    fmt(s.cn105_source_verified===true?s.pump_indoor_c:s.cn105_sse_indoor_c,1)+' °C');
  text('mitsubishi-read-setpoint',number(s.cn105_sse_setpoint_c)===null?'—':fmt(s.cn105_sse_setpoint_c,1)+' °C (SSE)');
  text('mitsubishi-read-hz',(s.cn105_source_verified===true?number(s.compressor_hz):number(s.cn105_sse_compressor_hz))===null?'—':
    fmt(s.cn105_source_verified===true?s.compressor_hz:s.cn105_sse_compressor_hz,0)+' Hz');
  text('mitsubishi-compressor-age',compressorAge===null?'Ei vastaanottoa':fmt(compressorAge,0)+' s sitten (vain SSE)');
  text('device-shelly',number(s.shelly_w)!==null?'Käytössä':'Ei lukemaa');
  text('device-ram',number(s.free_internal_heap)!==null?fmt(s.free_internal_heap/1024,0)+' KiB':'—');
  text('device-psram',number(s.free_psram)!==null?fmt(s.free_psram/1048576,2)+' MiB':'—');
  text('device-ota',s.ota_signed_pull_enabled===true?'Kyllä · allekirjoitettu':'Ei');
  if(!state.settingsDirty) {
    state.mode=mode;
    document.querySelectorAll('[data-mode]').forEach(b=>b.classList.toggle('selected',Number(b.dataset.mode)===mode));
    $('setting-hold').value=number(a.hold_target_c)===null?'':a.hold_target_c;
    $('setting-occupied').value=number(a.occupied_target_c)===null?'21':a.occupied_target_c;
    $('setting-delta').value=number(a.drying_delta_c)===null?'':a.drying_delta_c;
  }
  document.querySelectorAll('[data-mode]').forEach(b=>b.disabled=false);
  $('save-mode').disabled=state.settingsSaving||!a.config_token;
  text('ruuvi-bound',a.bound_mac||'Ei sidottu');
  text('ruuvi-discovered',a.discovered_mac||'Ei havaintoa');
  if(!state.alertSettingsLoaded && a.config_token) {
    $('alert-rh70').value=a.warning_rh70??'';
    $('alert-rh80').value=a.warning_rh80??'';
    $('alert-battery').value=a.warning_battery_v??'';
    state.alertSettingsLoaded=true;
  }
  const host=$('alerts-active');host.replaceChildren();
  const alertNames=['RH koholla','RH erittäin korkea','RuuviTag ei vastaa',
    'RuuviTag-paristo heikko','Pakkaslämpötila'];
  for(const item of (a.alerts||[])) {
    if(!item.active)continue;
    const itemRow=document.createElement('span');itemRow.className='pill';
    itemRow.textContent=alertNames[item.kind]||'Varoitus';
    if(!item.ack && a.config_token) {
      const ack=document.createElement('button');ack.type='button';
      ack.textContent='Kuittaa';
      ack.addEventListener('click',async()=>{
        try {
          await airPostLocal('/api/air/ack',{alert:String(item.kind)});
          await reloadStatus();
        }catch(e){text('alerts-feedback','Kuittaus epäonnistui: '+e.message);}
      });itemRow.append(ack);
    }
    host.append(itemRow);
  }
}
function showPrices(){
  const p=state.prices||{},rows=p.valid===true&&Array.isArray(p.slots)?p.slots.filter(x=>Number.isFinite(x.spot)): [];
  text('price-count',rows.length+' jaksoa');
  if(rows.length){const cheapest=rows.reduce((a,b)=>b.spot<a.spot?b:a);
    text('price-low',priceFmt(cheapest.spot));text('price-low-time',time(cheapest.utc));text('home-cheap','Edullisin seuraava jakso '+time(cheapest.utc)+': '+priceFmt(cheapest.spot));
  }else{text('price-low','—');text('price-low-time','Ei kelvollista hintadataa');text('home-cheap','Hintatietoja ei saatavilla');}
  drawBars('home-chart',rows);drawBars('prices-chart',rows);
  const body=$('price-rows');body.replaceChildren();
  if(!rows.length){const tr=body.insertRow();tr.insertCell().colSpan=3;tr.cells[0].textContent='Ei voimassa olevia hintoja.';return;}
  rows.slice(0,48).forEach(x=>{const tr=body.insertRow();[time(x.utc),priceFmt(x.spot),priceFmt(x.total)].forEach(value=>tr.insertCell().textContent=value);});
}
function showPlan(){
  const p=state.plan||{},s=state.status||{},ok=p.valid===true&&Array.isArray(p.slots)&&p.slots.length>0;
  const bootstrap=s.bootstrap_action;
  const proposed=bootstrap==='HEAT_SETPOINT'&&number(s.bootstrap_heat_setpoint_c)!==null?
    'HEAT '+fmt(s.bootstrap_heat_setpoint_c,1)+' °C':
    bootstrap==='OFF'?'OFF':bootstrap==='HOLD'?'Pidä nykyinen tila':null;
  const hasFallback=!ok&&proposed!==null;
  text('home-plan-state',ok?'MPC-laskelma':hasFallback?'Hintaohjauksen ehdotus':'Ei hyväksyttyä suunnitelmaa');
  text('plan-status',ok?String(p.count)+' jaksoa':hasFallback?'Lähtötason hintaohjaus':'Ei käytössä');
  text('plan-time',ok?'Laskettu '+time(s.optimized_utc):'Ei kelvollista laskelmaa');
  text('plan-cost',ok&&number(p.cost_eur)!==null?fmt(p.cost_eur,2)+' €':'—');
  text('plan-min',ok&&number(p.minimum_c)!==null?fmt(p.minimum_c,1)+' °C':'—');
  text('home-plan-next',ok?'Seuraava jakso '+time(p.slots[0].utc)+': '+(p.slots[0].heat?'HEAT 16 °C':'OFF'):
    hasFallback?'Lähtötason ehdotus: '+proposed:'Hyväksytty optimointisuunnitelma puuttuu.');
  text('home-plan-reason',ok?'Simuloitu MPC-ehdotus. Automaattiohjaus toimii erillisellä hintaohjauspolulla.':
    hasFallback?'Ruuvi + 15 min kokonaishinnat; huonetavoite '+
      fmt(s.bootstrap_room_target_c,1)+' °C; syy: '+(s.bootstrap_reason||'—')+
      '. Ehdotus ei ole kalibroitu MPC; CN105-ohjaus erillisen käyttäjän valinnan takana.':
    'Ehdotus estetty: '+(s.bootstrap_reason||'puutteelliset lähtötiedot')+'.');
  text('plan-warning',ok?'Ehdotukset ovat laskennallisia. Fyysiset ohjauskäskyt ovat estettyjä.':
    hasFallback?'Lähtötason hintaohjaus: '+proposed+' (ehdotus, ei käsky). Kalibroitua MPC-suunnitelmaa ei ole vielä saatavilla.':
    'Optimointiehdotus ei ole saatavilla. Puuttuva lämpötila tai puutteellinen hintajakso estää laskennan.');
  const body=$('plan-rows');body.replaceChildren();
  if(!ok){
    const tr=body.insertRow();tr.insertCell().colSpan=5;
    tr.cells[0].textContent=hasFallback?'Nykyinen ehdotus: '+proposed+'. Mallinnettu tuntien lämpötila- ja kustannusennuste puuttuu.':
      'Suunnitelma ei saatavilla.';
    return;
  }
  p.slots.slice(0,96).forEach(x=>{const tr=body.insertRow();[time(x.utc),x.heat?'HEAT 16 °C':'OFF',fmt(number(x.indoor_c),1),fmt(number(x.kwh),3),fmt(number(x.eur),3)].forEach((v,i)=>{const cell=tr.insertCell();cell.textContent=v;if(i===1)cell.className=x.heat?'heat':'off';});});
}
function showHistory(){
  const points=state.history&&Array.isArray(state.history.hours)?state.history.hours.filter(x=>x.clock_valid&&Number.isFinite(x.temp)):[];
  const host=$('history-chart');host.replaceChildren();
  if(points.length<2){host.innerHTML='<div class="empty">Lämpötilahistoriaa ei vielä ole riittävästi</div>';return;}
  const vals=points.slice(-96),min=Math.min(...vals.map(x=>x.temp)),max=Math.max(...vals.map(x=>x.temp)),range=Math.max(1,max-min);
  const svg=document.createElementNS('http://www.w3.org/2000/svg','svg');svg.setAttribute('viewBox','0 0 640 124');svg.classList.add('spark');svg.setAttribute('role','img');svg.setAttribute('aria-label','Sisälämpötilan historia');
  const path=document.createElementNS('http://www.w3.org/2000/svg','path');
  const d=vals.map((x,i)=>(i?'L':'M')+(10+i*620/(vals.length-1)).toFixed(1)+','+(113-(x.temp-min)/range*100).toFixed(1)).join(' ');
  path.setAttribute('d',d);path.setAttribute('fill','none');path.setAttribute('stroke','#276b52');path.setAttribute('stroke-width','3');path.setAttribute('stroke-linejoin','round');svg.append(path);host.append(svg);
  const note=document.createElement('p');note.className='tiny muted';note.textContent='Näkyvät lämpötilat: '+fmt(min)+'–'+fmt(max)+' °C · '+vals.length+' tuntipistettä';host.append(note);
}
async function fetchJSON(path){const r=await fetch(path,{cache:'no-store'});if(!r.ok)throw Error('HTTP '+r.status);return r.json();}
async function reloadStatus(){
  // Latest-started request wins: an earlier periodic response, including an
  // error, must never replace the post-save status or its verified controls.
  const requestId=++state.statusRequestSequence;
  try{
    const next=await fetchJSON('/api/status');
    if(requestId!==state.statusRequestSequence)return;
    state.status=next;showStatus();
  }catch(e){
    if(requestId!==state.statusRequestSequence)return;
  // Never leave the last good readings looking live across a status failure.
  state.status=null;state.plan={valid:false};
  text('connection','Yhteys poikki');$('connection-dot').className='dot bad';
  const stale={
    'clock':'Ei yhteyttä','home-temp':'Ei yhteyttä','home-mode':'Tila ei tiedossa',
    'home-sensor':'Mittaukset eivät päivity','home-rh':'—','home-dew':'—',
    'home-air-status':'Yhteys poikki','home-outside':'—','home-watts':'—',
    'home-price':'—','home-total':'—','price-now':'—','price-updated':'Ei yhteyttä',
    'device-version':'—','device-wifi':'Ei yhteyttä','device-cn105':'Ei yhteyttä',
    'mitsubishi-read-status':'Ei yhteyttä','mitsubishi-read-mode':'—',
    'mitsubishi-read-temp':'—','mitsubishi-read-setpoint':'—',
    'mitsubishi-read-hz':'—','mitsubishi-compressor-age':'Ei yhteyttä',
    'device-shelly':'Ei yhteyttä','device-ram':'—','device-psram':'—',
    'device-ota':'Ei yhteyttä','control-state':'Yhteys poikki',
    'control-last':'Ei tuoretta tilaa'
  };
  Object.entries(stale).forEach(([id,value])=>text(id,value));
  showPlan();$('save-mode').disabled=true;
  document.querySelectorAll('[data-mode]').forEach(b=>b.disabled=true);
}}
async function reloadPrices(){try{state.prices=await fetchJSON('/api/prices');showPrices();}catch(e){state.prices={valid:false,slots:[]};showPrices();}}
async function reloadPlan(){try{state.plan=await fetchJSON('/api/plan');showPlan();}catch(e){state.plan={valid:false};showPlan();}}
async function reloadHistory(){try{state.history=await fetchJSON('/api/air/history');showHistory();}catch(e){state.history={hours:[]};showHistory();}}
async function setControlArmed(enabled) {
  const a=state.status&&state.status.air;
  if(!a||!a.config_token) {
    text('control-feedback','Laite ei vastaa tai vahvistustunniste puuttuu.');
    return;
  }
  $('control-enable').disabled=true;
  $('control-disable').disabled=true;
  text('control-feedback','Tallennetaan ohjauksen käyttöönottoa…');
  try {
    const body=new URLSearchParams({token:a.config_token,enabled:enabled?'true':'false'});
    const r=await fetch('/api/control/arm',{method:'POST',
      headers:{'Content-Type':'application/x-www-form-urlencoded'},body});
    const j=await r.json();
    if(!r.ok||j.saved!==true)throw Error('Asetuksen tallennus epäonnistui');
    text('control-feedback',enabled?
      'Automaattiohjaus sallittu. Ensimmäinen ohjaus odottaa tuoreita tietoja ja Ylläpitolämpö- tai Asuttu-tilaa.':
      'Automaattiohjaus poistettu käytöstä.');
    await reloadStatus();
  }catch(e) {
    text('control-feedback',e.message||'Ohjauksen asetusta ei tallennettu');
    await reloadStatus();
  }
}
$('control-enable').addEventListener('click',()=>setControlArmed(true));
$('control-disable').addEventListener('click',()=>setControlArmed(false));
async function airPostLocal(path,fields) {
  const token=state.status&&state.status.air&&state.status.air.config_token;
  if(!token)throw Error('Laite ei vastaa tai valtuutus puuttuu');
  const body=new URLSearchParams({...fields,token});
  const response=await fetch(path,{method:'POST',
    headers:{'Content-Type':'application/x-www-form-urlencoded'},body});
  if(!response.ok)throw Error('HTTP '+response.status);
  const result=await response.json();
  if(result.saved===false)throw Error('Tallennus epäonnistui');
  return result;
}
$('ruuvi-bind').addEventListener('click',async()=>{
  const mac=$('ruuvi-mac').value.trim();
  if(!/^(?:[a-fA-F0-9]{2}:){5}[a-fA-F0-9]{2}$/.test(mac)&&mac!=='none') {
    text('ruuvi-feedback','Anna kuusi tavuinen MAC-tunniste tai none');return;
  }
  $('ruuvi-bind').disabled=true;
  try {
    await airPostLocal('/api/air/bind',{which:$('ruuvi-role').value,mac});
    text('ruuvi-feedback','Anturisidonta tallennettu.');
    await reloadStatus();
  } catch(e){text('ruuvi-feedback',e.message);}
  finally{$('ruuvi-bind').disabled=false;}
});
$('alerts-save').addEventListener('click',async()=>{
  const rh70=Number($('alert-rh70').value),
    rh80=Number($('alert-rh80').value),
    battery=Number($('alert-battery').value);
  if(!Number.isFinite(rh70)||rh70<55||rh70>80 ||
     !Number.isFinite(rh80)||rh80<75||rh80>95||rh80<rh70+5||
     !Number.isFinite(battery)||battery<2||battery>3.2){
    text('alerts-feedback','Tarkista varoitusarvot');return;
  }
  $('alerts-save').disabled=true;
  try {
    await airPostLocal('/api/air/thresholds',{rh70:String(rh70),
      rh80:String(rh80),battery:String(battery)});
    text('alerts-feedback','Varoitusrajat tallennettu.');
    state.alertSettingsLoaded=false;await reloadStatus();
  }catch(e){text('alerts-feedback',e.message);}
  finally{$('alerts-save').disabled=false;}
});

document.querySelectorAll('[data-mode]').forEach(b=>b.addEventListener('click',()=>{state.settingsDirty=true;++state.settingsRevision;state.mode=Number(b.dataset.mode);document.querySelectorAll('[data-mode]').forEach(x=>x.classList.toggle('selected',x===b));}));
['setting-hold','setting-occupied','setting-delta'].forEach(id=>$(id).addEventListener('input',()=>{state.settingsDirty=true;++state.settingsRevision;}));
$('save-mode').addEventListener('click',async()=>{
  if(state.settingsSaving)return; // only one POST may be in flight
  const a=state.status&&state.status.air,btn=$('save-mode');if(!a||!a.config_token){text('feedback','Tiedot puuttuvat.');return;}
  const hold=Number($('setting-hold').value),occupied=Number($('setting-occupied').value),
        delta=Number($('setting-delta').value);
  if(!Number.isFinite(hold)||hold<5||hold>24||
     !Number.isFinite(occupied)||occupied<16||occupied>26||Math.abs(occupied*2-Math.round(occupied*2))>1e-7||
     !Number.isFinite(delta)||delta<3||delta>5){text('feedback','Tarkista lämpötilatavoitteet.');return;}
  const submittedRevision=state.settingsRevision;
  state.settingsSaving=true;
  btn.disabled=true;text('feedback','Tallennetaan paikallisia asetuksia…');
  try{const body=new URLSearchParams({token:a.config_token,mode:String(state.mode),hold:String(hold),
      occupied:String(occupied),delta:String(delta)});
    const r=await fetch('/api/air/config',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body});
    if(!r.ok)throw Error('HTTP '+r.status);
    if(state.settingsRevision===submittedRevision){
      state.settingsDirty=false;
      text('feedback','Asetukset tallennettu. Lämpöpumpulle ei lähetetty käskyä.');
    }else{
      // A newer unsaved draft must survive the older POST response.
      state.settingsDirty=true;
      text('feedback','Aiemmat asetukset tallennettu. Uusia muutoksia odottaa tallennusta.');
    }
    await reloadStatus();
  }catch(e){text('feedback','Tallennus ei onnistunut.');}finally{
    state.settingsSaving=false;
    btn.disabled=!(state.status&&state.status.air&&state.status.air.config_token);
  }
});
// One request at a time on first load: avoid opening four TCP connections to ESP32.
async function initialRefresh(){await reloadStatus();await reloadPrices();await reloadPlan();await reloadHistory();}
initialRefresh();
setInterval(reloadStatus,15000);
setInterval(async()=>{await reloadPrices();await reloadPlan();},60000);
setInterval(reloadHistory,180000);
</script>
</body>
</html>)ENEVINA_UI";
