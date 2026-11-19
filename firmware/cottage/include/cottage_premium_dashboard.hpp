#pragma once
#include <Arduino.h>
// C2.5: entirely local self-contained page; no CDN, cloud, device writes or external assets.
// PROGMEM allows flash-backed streaming through WebServer::send_P.
static const char premium_dashboard_html[] PROGMEM = R"ENEVINA_UI(<!doctype html>
<html lang="fi">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<meta name="color-scheme" content="light dark">
<meta name="theme-color" content="#f5f5f0">
<script>
// Automatic Finnish-time day/night UI. Visual only: never changes heating.
(function(){
  function applyAutomaticNightTheme() {
    let night=false;
    try {
      const now=new Date();
      if(now.getTime()<1700000000000)throw Error('Device clock unavailable');
      const h=Number(new Intl.DateTimeFormat('en-GB',{
        timeZone:'Europe/Helsinki',hour:'2-digit',hourCycle:'h23'
      }).format(now))%24;
      if(!Number.isFinite(h))throw Error('No clock hour');
      night=h>=21||h<7;
    } catch(_) {
      night=!!(window.matchMedia &&
               window.matchMedia('(prefers-color-scheme: dark)').matches);
    }
    document.documentElement.dataset.theme=night?'night':'day';
    const themeColor=document.querySelector('meta[name="theme-color"]');
    if(themeColor)themeColor.setAttribute('content',night?'#0c1a17':'#f5f5f0');
  }
  applyAutomaticNightTheme();
  // Recheck while a dashboard remains open and whenever a sleeping iPad wakes.
  setInterval(applyAutomaticNightTheme,60000);
  window.addEventListener('focus',applyAutomaticNightTheme);
  document.addEventListener('visibilitychange',()=>{
    if(!document.hidden)applyAutomaticNightTheme();
  });
})();
</script>
<title>Enevina Lite | Energianhallinta</title>
<style>
:root{--bg:#f5f5f0;--paper:#ffffff;--ink:#17352e;--muted:#697a73;--edge:#e2e9e2;--green:#276b52;--deep:#103d31;--mint:#e5f2e7;--warning:#8c6418;--warnbg:#fff6e6;--red:#af4c3f;--shadow:0 7px 24px rgba(29,60,47,.05)}
/* Night colors override presentation only. Timezone and DST come from
   Intl.DateTimeFormat('Europe/Helsinki'), independent of iPad settings. */
:root[data-theme="night"]{
  color-scheme:dark;
  --bg:#0c1a17;--paper:#172925;--ink:#e6f2ea;--muted:#adbbb4;
  --edge:#314740;--green:#8ad4aa;--deep:#225f4b;--mint:#234237;
  --warning:#f5cf8b;--warnbg:#372b1d;--red:#f2a69d;
  --shadow:0 8px 28px rgba(0,0,0,.24)
}
:root[data-theme="night"] .sidebar{background:#12231f}
:root[data-theme="night"] .brand-mark{background:#2d7459}
:root[data-theme="night"] .nav button{color:#b9cbc1}
:root[data-theme="night"] .nav button.active{color:#d8f5e4}
:root[data-theme="night"] .chip{background:#20352e;color:#d8e7df}
:root[data-theme="night"] .notice{border-color:#665031;color:#f0d1a0}
:root[data-theme="night"] .mode button{background:#20352e;color:#e6f2ea}
:root[data-theme="night"] .mode button.selected{background:#275a42;color:#f2fff6}
:root[data-theme="night"] .field input,
:root[data-theme="night"] .field select{background:#11211c;border-color:#4b655b;color:#e6f2ea}
:root[data-theme="night"] .primary{background:#317859;color:#f8fff9}
:root[data-theme="night"] .pill{background:#244735;color:#cef0d8}
:root[data-theme="night"] .callout{background:#213d30;color:#d0e8d9}
:root[data-theme="night"] .callout.warn{background:#3d311f;color:#f6d8a4}
:root[data-theme="night"] .empty{border-color:#4d685e}
:root[data-theme="night"] .bar{background:#669e84}
:root[data-theme="night"] .bar.cheap{background:#54b98a}
:root[data-theme="night"] .bar.high{background:#d2a35f}
:root[data-theme="night"] .bar.current{outline-color:#dbf9e9}
:root[data-theme="night"] .temperature{background:linear-gradient(130deg,#20634e,#103c31)}
.language-switch{display:inline-flex;align-items:center;border:1px solid var(--edge);border-radius:10px;padding:3px;gap:2px;background:var(--paper)}
.language-switch button{border:0;border-radius:7px;padding:6px 9px;background:transparent;color:var(--muted);font-size:12px;font-weight:700}
.language-switch button[aria-pressed="true"]{background:var(--mint);color:var(--ink)}
.top-actions{display:flex;align-items:center;gap:10px;flex-wrap:wrap;justify-content:flex-end}
@media(max-width:600px){.top{flex-wrap:wrap}.top-actions{width:100%;justify-content:space-between}.card .flex{flex-wrap:wrap;justify-content:flex-start}#control-enable,#control-disable{flex:1 1 145px;min-width:0}}
.language-switch button:focus-visible{outline:2px solid var(--green)}

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
.fieldrow{display:flex;gap:10px;align-items:end;flex-wrap:wrap}.field{display:grid;gap:4px;font-size:12px;color:var(--muted)}.field[hidden]{display:none!important}.field input{width:110px;border:1px solid #c5d2c8;border-radius:9px;padding:9px;color:var(--ink);background:#fff}.primary{background:var(--deep);color:#fff;border:0;border-radius:10px;padding:11px 15px;font-weight:700}.primary:disabled{opacity:.5;cursor:not-allowed}
.subhead{margin:0 0 6px;font-size:13px;color:var(--muted)}.callout{background:#f3f8f2;border-radius:12px;padding:13px 15px;color:#365c4b;font-size:13px}.callout.warn{background:var(--warnbg);color:#744c13}
.chart{width:100%;overflow:hidden}.bars{height:180px;display:flex;align-items:flex-end;gap:2px;border-bottom:1px solid #c5d4cb;padding:8px 0 0;overflow:hidden}.bar{min-width:2px;flex:1;border:0;border-radius:3px 3px 0 0;background:#8dbea2}.bar.cheap{background:#2c855f}.bar.high{background:#c6a65a}.bar.current{outline:2px solid #17352e;outline-offset:-1px}.ticks{display:flex;justify-content:space-between;font-size:11px;color:var(--muted);margin-top:8px}.legend{display:flex;gap:18px;flex-wrap:wrap;color:var(--muted);font-size:12px;margin-top:8px}
.tablewrap{overflow:auto}table{width:100%;border-collapse:collapse;text-align:left;min-width:540px}td,th{border-bottom:1px solid var(--edge);padding:12px 8px}th{font-size:11px;letter-spacing:.7px;text-transform:uppercase;color:var(--muted)}td{font-size:13px}.heat{color:var(--green);font-weight:750}.off{color:#7a877b}
.spark{height:124px;width:100%;display:block}.empty{padding:38px 14px;text-align:center;color:var(--muted);font-size:13px;border:1px dashed #c7d4c9;border-radius:12px}
.foot{color:var(--muted);font-size:12px;margin-top:22px}#feedback{font-size:12px;margin-top:10px}.statusgrid{display:grid;grid-template-columns:1fr auto;gap:9px;font-size:13px}.statusgrid span:nth-child(2n){font-weight:650;text-align:right}
@media(max-width:1100px){.grid{gap:12px}.span7,.span5{grid-column:span 6}.span4{grid-column:span 4}.span8{grid-column:span 8}}
@media(max-width:850px){.shell{display:block}.sidebar{position:static;height:auto;padding:9px 14px;gap:5px;border-right:0;border-bottom:1px solid var(--edge)}.brand{padding:3px}.brand-mark{font-size:16px;padding:5px 9px;border-radius:9px}.brand-name{font-size:20px}.brand-name small{display:none}.nav{display:flex;overflow:auto;gap:4px}.nav button{width:auto;white-space:nowrap;padding:9px 11px;font-size:12px}.nav .i{font-size:13px;width:auto}.aside-foot{display:none}.main{padding:15px 14px 40px}.top{margin-bottom:12px}.heading{margin-bottom:18px}h1{font-size:23px}.card{padding:16px}}
@media(max-width:600px){.span7,.span5,.span4,.span8,.span12{grid-column:span 12}.metric{font-size:39px}.temperature .metric{font-size:56px}.mode{grid-template-columns:repeat(2,minmax(0,1fr))}.mode button{padding:10px 5px;font-size:12px}.chip{font-size:11px}.bars{height:155px}}
.weather-line{width:100%;height:auto;max-height:220px;display:block;overflow:visible}.weather-line .axis{stroke:var(--edge);stroke-width:1}.weather-line .curve{stroke:var(--green);fill:none;stroke-width:3;stroke-linecap:round;stroke-linejoin:round}.weather-line .point{fill:var(--green)}.weather-ticks{display:flex;justify-content:space-between;font-size:12px;color:var(--muted);gap:12px;padding:0 7px}
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
    <button data-view="weather"><span class="i">☀</span>Sääennuste</button>
    <button data-view="plan"><span class="i">▤</span>Optimointi</button>
    <button data-view="settings"><span class="i">⚙</span>Asetukset</button>
  </nav>
  <div class="aside-foot">Enevina Lite · paikallinen ohjain<br><a href="/#settings">Laiteasetukset</a></div>
</aside>
<main class="main">
  <div class="top"><div><div class="eyebrow">Energia · Lämpöpumppu</div><strong id="clock">Aikaa ei ole synkronoitu</strong></div><div class="top-actions"><div class="language-switch" role="group" aria-label="Käyttöliittymän kieli">
  <button type="button" data-lang="fi" lang="fi" aria-pressed="true">FI</button>
  <button type="button" data-lang="en" lang="en" aria-pressed="false">EN</button>
</div><span class="chip"><span id="connection-dot" class="dot"></span><span id="connection">Yhdistetään</span></span></div></div>
  <div class="notice" role="status"><strong id="pilot-state-heading">OHJAUS POIS</strong><span id="pilot-state-text">Ota CN105-automaattiohjaus käyttöön Asetukset-näkymästä. Optimointiehdotus ei yksin lähetä käskyjä.</span></div>
  <section class="view active" id="view-home">
    <div class="heading"><div><div class="eyebrow">Yleisnäkymä</div><h1>Sisäilman olosuhteet</h1><p>Mittaukset, sähkön hinta ja tuleva lämmityssuunnitelma.</p></div></div>
    <div class="grid">
      <article class="card temperature span7"><div><div class="eyebrow" style="color:#c5ded1">Sisälämpötila · RuuviTag</div><div class="metric" id="home-temp">—</div></div><div><span class="pill" id="home-mode">Tila ei tiedossa</span><p class="tiny muted" id="home-sensor" style="margin:12px 0 0">Odotetaan mittausta</p></div></article>
      <article class="card span5"><div class="eyebrow">Sähkön hinta nyt</div><div class="metric small" id="home-price">—</div><p class="muted tiny" id="home-price-label">Spot-hinta · ilman arvonlisäveroa</p><div class="statline"><span>Arvio kokonaishinnasta</span><span id="home-total">—</span></div><div class="statline"><span>Ulkolämpötila / ennuste</span><span id="home-outside">—</span></div><div class="statline"><span>Mittauskulutus</span><span id="home-watts">—</span></div></article>
      <article class="card span4"><div class="eyebrow">Ilmankosteus</div><div class="metric small" id="home-rh">—</div><p class="muted tiny">RuuviTag · suhteellinen kosteus</p><div class="statline"><span>Kastepiste</span><span id="home-dew">—</span></div><div class="statline"><span>Anturin tila</span><span id="home-air-status">—</span></div></article>
      <article class="card span8"><div class="flex"><h2>Sähkön tulevat hinnat</h2><span class="muted tiny">15 minuutin jaksot</span></div><div id="home-chart" class="chart"><div class="empty">Hintadataa odotetaan</div></div><div class="flex" style="margin-top:12px"><span class="muted tiny" id="home-cheap">Halvinta jaksoa ei tiedossa</span><button class="primary" id="go-prices" type="button">Kaikki hinnat</button></div></article>
      <article class="card span12" id="home-weather-card"><div class="flex"><h2>Sääennuste · MET Norway</h2><button class="primary" id="go-weather" type="button">Näytä 48 tunnin ennuste</button></div><p class="muted tiny" id="home-weather-summary">Odotetaan lämpötilaennustetta.</p></article>
      <article class="card span12"><div class="flex"><h2>Oma energiankulutusluokka</h2><span class="chip">Epävirallinen</span></div>
      <div class="fieldrow">
        <div><div class="metric small" id="energy-grade">—</div><p class="muted tiny" id="energy-intensity">Vuoden kulutustieto puuttuu</p></div>
        <label class="field">Lämmitetty pinta-ala m²<input id="energy-area" type="number" min="1" step=".5" inputmode="decimal" value="17"></label>
        <label class="field">Vuotuinen sähkönkulutus kWh<input id="energy-annual" type="number" min="1" step="1" inputmode="numeric" placeholder="Syötä vuosikulutus"></label>
        <button id="energy-calculate" class="primary" type="button">Laske luokka</button>
      </div>
      <p class="tiny muted" id="energy-feedback" role="status">Oma havainnollistava A–G-asteikko: alle 60 / 100 / 150 / 200 / 250 / 350 kWh/m²/v. G yli 350. Ei virallinen energiatodistus tai E-luku; ei huomioi lämmitysmuotoja tai sääkorjausta. Tiedot säilyvät vain tässä selaimessa.</p></article>
      <article class="card span12"><div class="flex"><h2>Optimoinnin seuraava toimenpide</h2><span class="chip" id="home-plan-state">Ei suunnitelmaa</span></div><p id="home-plan-next">Hyväksyttyä optimointisuunnitelmaa ei vielä ole.</p><div class="callout" id="home-plan-reason">Optimointi näkyy vasta, kun käytettävissä on kelvollinen, turvallinen ja kalibroitu laskentapohja.</div></article>
    </div>
  </section>
  <section class="view" id="view-prices">
    <div class="heading"><div><div class="eyebrow">Pörssisähkö</div><h1>Sähkön hinnat</h1><p id="price-region-description">Valitun alueen 15 minuutin hinnat. Kaavio näyttää vain ladatun tulevan datan.</p></div></div>
    <div class="grid">
      <article class="card span4"><div class="eyebrow">Nykyinen spot</div><div class="metric small" id="price-now">—</div><p class="tiny muted" id="price-spot-unit">snt/kWh, alv 0 %</p></article>
      <article class="card span4"><div class="eyebrow">Edullisin tuleva jakso</div><div class="metric small" id="price-low">—</div><p class="tiny muted" id="price-low-time">Ei tietoa</p></article>
      <article class="card span4"><div class="eyebrow">Saatavilla oleva ennuste</div><div class="metric small" id="price-count">—</div><p class="tiny muted" id="price-updated">Tietolähdettä ei ole päivitetty</p></article>
      <article class="card span12"><div class="flex"><h2>Hintakehitys</h2><span class="chip" id="price-area-chip">Nord Pool</span></div><div id="prices-chart" class="chart"><div class="empty">Hintoja ei ole saatavilla</div></div><p class="foot">Spot-hinta on ilman veroja. Kokonaishinnan arviossa sovelletaan laitteelle määriteltyjä sopimuskohtaisia vero- ja siirtomaksuja. Hintoja ei arvata puuttuville varttitunneille.</p></article>
      <article class="card span12"><h2>Seuraavat hintajaksot</h2><div class="tablewrap"><table><thead><tr><th>Alkaa</th><th id="price-spot-heading">Spot snt/kWh</th><th id="price-total-heading">Kokonaishinta snt/kWh</th></tr></thead><tbody id="price-rows"><tr><td colspan="3">Odota hintatietoja.</td></tr></tbody></table></div></article>
    </div>
  </section>
  <section class="view" id="view-weather">
    <div class="heading"><div><div class="eyebrow">Norjan ilmatieteen laitos · MET Norway</div><h1>Sääennuste</h1><p>Paikallinen ulkolämpötilaennuste MET Norwayn maailmanlaajuisesta sääpalvelusta. Puuttuvia arvoja ei arvata.</p></div></div>
    <div class="grid">
      <article class="card span4"><div class="eyebrow">Seuraava ennustelämpötila</div><div class="metric small" id="weather-next">—</div><p class="muted tiny" id="weather-next-time">Odotetaan MET Norwayn ennustetta</p></article>
      <article class="card span4"><div class="eyebrow">Alin seuraavan 24 tunnin aikana</div><div class="metric small" id="weather-min">—</div><p class="muted tiny">Ennustettu ulkolämpötila</p></article>
      <article class="card span4"><div class="eyebrow">Ylin seuraavan 24 tunnin aikana</div><div class="metric small" id="weather-max">—</div><p class="muted tiny">Ennustettu ulkolämpötila</p></article>
      <article class="card span12"><div class="flex"><h2>Ulkolämpötila · seuraavat 48 tuntia</h2><span class="chip" id="weather-source">MET Norway</span></div><div id="weather-chart" class="chart"><div class="empty">Ennustetietoja odotetaan.</div></div><p id="weather-updated" class="foot">Ennusteen päivitysaikaa ei tiedetä.</p><p class="tiny muted">Lähde: <a href="https://api.met.no/" target="_blank"
      rel="noopener noreferrer">MET Norway</a> ·
      <a href="https://creativecommons.org/licenses/by/4.0/"
      target="_blank" rel="noopener noreferrer">CC BY 4.0</a>.
      Ennustetiedot on muunnettu Enevina Liten lämpötilakäyräksi.
      Näytetään vain MET Norwaylta vastaanotettuja kelvollisia tuntipisteitä. Ennuste voi poiketa toteutuneesta säästä; sade- ja tuulitiedot eivät sisälly tähän syötteeseen.</p></article>
      <article class="card span12"><h2>Tuntiennuste</h2><div class="tablewrap"><table><thead><tr><th>Ajankohta</th><th>Lämpötila °C</th></tr></thead><tbody id="weather-rows"><tr><td colspan="2">Ladataan sääennustetta…</td></tr></tbody></table></div></article>
    </div>
  </section>
  <section class="view" id="view-plan">
    <div class="heading"><div><div class="eyebrow">Ennakoiva lämmitys</div><h1>Optimointisuunnitelma</h1><p>Hintaohjaus tuottaa ehdotuksen; CN105-komennot edellyttävät erillistä käyttöönottoa.</p></div></div>
    <div class="grid"><article class="card span4"><div class="eyebrow">Suunnitelma</div><div class="metric small" id="plan-status">—</div><p class="tiny muted" id="plan-time">Ei kelvollista laskelmaa</p></article><article class="card span4"><div class="eyebrow">Ennustettu kustannus</div><div class="metric small" id="plan-cost">—</div><p class="tiny muted">Mallin kokonaisarvio, jos kelvollinen</p></article><article class="card span4"><div class="eyebrow">Ennustettu alin lämpötila</div><div class="metric small" id="plan-min">—</div><p class="tiny muted">Laskettu, ei anturimittaus</p></article>
      <article class="card span12"><div id="plan-warning" class="callout warn">Hyväksyttyä suunnitelmaa ei ole. Lämpöpumppu ei saa tältä käyttöliittymältä komentoja.</div><div class="tablewrap"><table><thead><tr><th>Alkaa</th><th>Ehdotus</th><th>Ennuste °C</th><th>Sähkö kWh</th><th>Arvio €</th></tr></thead><tbody id="plan-rows"><tr><td colspan="5">Suunnitelma ei saatavilla.</td></tr></tbody></table></div></article>
    </div>
  </section>
  <section class="view" id="view-settings">
    <div class="heading"><div><div class="eyebrow">Olosuhteet ja laite</div><h1>Asetukset</h1><p>Paikallinen huonetavoite ja erikseen käyttöönotettava CN105-lämmitysohjaus.</p></div></div>
    <div class="grid">
      <article class="card span7"><h2>Lämmityksen toimintatila</h2><p class="tiny muted">Ylläpitolämpö ja Asuttu käyttävät omia, toisistaan riippumattomia huonetavoitteitaan. Hintaohjaus voi säätää Mitsubishin pyyntilämpötilaa.</p><div class="mode" role="group" aria-label="Laskennallinen tila"><button type="button" data-mode="0">Kylmillään</button><button type="button" data-mode="1">Kuivanapito</button><button type="button" data-mode="2">Ylläpitolämpö</button><button type="button" data-mode="3">Asuttu</button></div><div class="fieldrow" style="margin-top:17px"><label class="field">Ylläpitotavoite °C<input id="setting-hold" type="number" min="5" max="24" step=".5" inputmode="decimal"></label><label class="field">Asuttu °C<input id="setting-occupied" type="number" min="16" max="26" step=".5" inputmode="decimal"></label><label class="field">Kuivanapito Δ°C<input id="setting-delta" type="number" min="3" max="5" step=".5" inputmode="decimal"></label><button class="primary" id="save-mode" type="button" disabled>Tallenna asetukset</button></div><div id="feedback" role="status"></div><p class="tiny muted">Valitse Ylläpitolämpö tai Asuttu sekä kyseisen tilan huonetavoite. Hintaehdotus laskee Mitsubishille 16–26 °C pyyntilämpötilan 0,5 °C askelin; komennot sallitaan vain erillisellä käyttöönotolla.</p></article>
      <article class="card span12"><h2>Mitsubishi · automaattinen lämpötilan säätö</h2><p class="tiny muted">Laskenta käyttää RuuviTagia ja sähkön kokonaishintaa. Lämmitystä ohjataan Ylläpitolämpö- ja Asuttu-tiloissa. Komento lähtee CN105:lle, ja tavoitelämpötila tarkistetaan takaisinlukemalla. Käyttöönotto on aina erillinen valinta eikä firmwarepäivitys käynnistä sitä.</p><div class="flex"><span class="chip" id="control-state">Ohjaus ei käytössä</span><span id="control-last">Ei komentoja</span><button type="button" id="control-enable" class="primary">Ota automaattiohjaus käyttöön</button><button type="button" id="control-disable">Poista automaattiohjaus käytöstä</button></div><p id="control-feedback" role="status"></p></article>
      <article class="card span12" id="site-card"><h2>Sähkösopimus ja siirto</h2><p class="tiny muted">Kokonaishinta = spot + marginaali + siirto + sähkövero, arvonlisävero lisätään kerran. Kuukausimaksut eivät vaikuta 15 minuutin ohjaukseen eikä niitä lasketa mukaan.</p><div class="fieldrow"><label class="field">Marginaali snt/kWh<input id="site-margin" inputmode="decimal"></label><label class="field">Siirto päivä snt/kWh<input id="site-tday" inputmode="decimal"></label><label class="field">Siirto yö / muu aika snt/kWh<input id="site-tnight" inputmode="decimal"></label><label class="field">Sähkövero snt/kWh<input id="site-tax" inputmode="decimal"></label><label class="field">ALV %<input id="site-vat" inputmode="decimal"></label></div><div class="fieldrow"><label class="field">Siirtotuote<select id="site-kind"><option value="general">Yleissiirto</option><option value="time_of_day">Aikasiirto (päivä/yö)</option><option value="seasonal">Kausisiirto (talviarkipäivä)</option></select></label><label class="field">Päivä alkaa klo<input id="site-dstart" type="number" min="0" max="23" step="1"></label><label class="field">Yö alkaa klo<input id="site-nstart" type="number" min="0" max="23" step="1"></label><label class="field">Hinnat sisältävät ALV:n<select id="site-incl"><option value="true">Kyllä (marginaali, siirto, vero)</option><option value="false">Ei, alv lisätään</option></select></label><label class="field">Esilämmityksen hintaetu %<input id="site-adv" inputmode="decimal"></label></div><p class="tiny muted">Spot-hinta on aina ilman arvonlisäveroa. Esilämmitys tehdään vain, kun hinta on vähintään annetun prosentin verran halvempi kuin seuraavien kahden tunnin mediaani.</p></article>
      <article class="card span12" id="devices-card"><h2>Lämpöpumppu ja yhteydet</h2><div class="fieldrow"><label class="field">Lämpöpumppu ja yhteys<select id="site-adapter"></select></label><label class="field">CN105-sillan /events-osoite<input id="site-cn105" autocomplete="off" autocapitalize="none" spellcheck="false"></label><label class="field">Shelly-mittarin osoite<input id="site-shelly" autocomplete="off" autocapitalize="none" spellcheck="false"></label><label class="field">Sääpaikka (leveysaste,pituusaste)<input id="site-fmi" autocomplete="off" placeholder="61.40,24.10"></label></div><div class="tablewrap"><table><thead><tr><th>Valmistaja / malli</th><th>Yhteys</th><th>Tila</th><th>Mittaukset</th><th>Komennot</th></tr></thead><tbody id="adapter-rows"><tr><td colspan="5">Ladataan…</td></tr></tbody></table></div><p class="tiny muted">Mitsubishi on CN105-kenttäpilotti ja Midea V2/V3 voidaan valita yhteyden tilanlukuun ja manuaaliseen ohjaustestiin. Muut on lueteltu tutkimustietona, ei tukilupauksena. Sääpaikan muutos tulee voimaan heti tallennuksen jälkeen. Laitteen valinta ei itsessään ota ohjausta käyttöön.</p><div class="flex"><button class="primary" id="site-save" type="button">Tallenna sopimus ja yhteydet</button><a href="/setup" id="wifi-change">Vaihda Wi-Fi-verkko</a></div><p id="site-feedback" role="status"></p></article>
      <article class="card span12" id="midea-v3-card" hidden><h2>Midea · paikallinen V2 / V3 Wi-Fi -yhteys</h2><p class="tiny muted">V2- tai V3-tilanluku tapahtuu suoraan lämpöpumpun Wi-Fi-moduulista ilman Home Assistantia tai jatkuvaa pilviyhteyttä. Midean manuaalinen lämpötilan ja toimintatilan ohjaus on nyt kokeellinen; automaattisen optimoinnin komentoja ei vielä lähetetä. Yhdistä vain omassa luotetussa lähiverkossa; kehitysvaiheen HTTP-asetussivu ei salaa tunnisteiden siirtoa.</p><div class="fieldrow"><label class="field">Midea-protokolla<select id="midea-protocol"><option value="3">V3 (token + key)</option><option value="2">V2 (ei tokenia)</option></select></label><label class="field">Midean paikallinen IP<input id="midea-ip" placeholder="10.0.0.15" autocomplete="off" spellcheck="false"></label><label class="field">Midea V3 device ID<input id="midea-id" autocomplete="off" spellcheck="false"></label><label class="field" id="midea-v3-token-field">V3 token (128 heksamerkkiä)<input id="midea-token" type="password" autocomplete="new-password" spellcheck="false"></label><label class="field" id="midea-v3-key-field">V3 key (64 heksamerkkiä)<input id="midea-key" type="password" autocomplete="new-password" spellcheck="false"></label></div><div class="flex"><button class="primary" id="midea-save" type="button">Tallenna Midea-yhteys</button><button id="midea-test" type="button">Testaa yhteys nyt</button><span id="midea-state">Ei mittausta</span></div><p id="midea-feedback" role="status"></p><p id="midea-diagnostic" class="tiny muted" role="status">Ei vielä testattu.</p><div class="statusgrid"><span>Yhteyden tila</span><span id="midea-read">—</span><span>Toimintatila</span><span id="midea-mode">—</span><span>Pyyntilämpötila</span><span id="midea-target">—</span><span>Pumpun sisälämpötila</span><span id="midea-indoor">—</span><span>Ulkolämpötila</span><span id="midea-outdoor">—</span></div><p class="tiny muted">V3-tunnisteet tallennetaan paikalliseen NVS-muistiin, eikä niitä palauteta selaimeen. V2 ei tarvitse tokenia tai keytä, mutta sen yhteys käyttää yhteistä kiinteää salausavainta eikä todenna pumppua samalla tavalla kuin V3. Valitse protokolla laitteen todetun version mukaan. Käytä vain luotetussa lähiverkossa. DHCP-IP:n automaattinen tunnistus ja automaattinen Midea-optimointiohjaus eivät vielä sisälly tähän testitoteutukseen.</p>
        <h3>Midean mallikohtainen koekäyttöhyväksyntä</h3>
        <p class="tiny muted">Käyttöönotto koskee vain yhtä asetettua laitetta ja käynnistyskertaa. Anna pumpun tarkka mallimerkintä sekä mallille varmistetut pyynnin rajat. Tämä ei luo salasanaa tai poista ohjausominaisuuksia.</p>
        <div class="fieldrow">
          <label class="field">Pumpun tarkka malli<input id="midea-pilot-model" autocomplete="off" placeholder="Mallimerkintä"></label>
          <label class="field">Mallin minimipyynti °C<input id="midea-pilot-min" type="number" step=".5" min="12" max="40" placeholder="Mallin minimi"></label>
          <label class="field">Mallin maksimipyynti °C<input id="midea-pilot-max" type="number" step=".5" min="12" max="40" placeholder="Mallin maksimi"></label>
          <button id="midea-pilot-enable" type="button">Hyväksy mallikohtainen ohjaustesti</button>
          <button id="midea-pilot-disable" type="button">Peru koekäyttöhyväksyntä</button>
        </div>
        <p id="midea-pilot-feedback" role="status">Mallikohtaista ohjaustestiä ei ole hyväksytty.</p>
        <h3>Midean paikallinen ohjaus · V2 / V3</h3>
        <p class="tiny muted">Yksittäiset muutokset lähetetään suoraan pumpulle alkuperäistä Wi-Fiä käyttäen. Tunnetut muut asetukset säilytetään viimeksi luetusta tilasta. Käsky on manuaalinen; pörssisähköoptimoinnin automaattista Midea-käskynlähetystä ei ole vielä kytketty.</p>
        <div class="fieldrow">
          <label class="field">Virta<select id="midea-command-power"><option value="">Säilytä nykyinen</option><option value="true">Päälle</option><option value="false">Pois</option></select></label>
          <label class="field">Toimintatila<select id="midea-command-mode"><option value="">Säilytä nykyinen</option><option value="1">Auto</option><option value="2">Jäähdytys</option><option value="3">Kuivaus</option><option value="4">Lämmitys</option><option value="5">Puhallus</option></select></label>
          <label class="field">Pyynti °C<input id="midea-command-temperature" type="number" step=".5" min="12" max="40" placeholder="Säilytä" inputmode="decimal"></label>
          <label class="field">Puhallus<select id="midea-command-fan"><option value="">Säilytä nykyinen</option><option value="102">Auto</option><option value="20">Hiljainen</option><option value="40">Matala</option><option value="60">Keskitaso</option><option value="80">Korkea</option><option value="100">Täysi</option></select></label>
          <label class="field">Swing<select id="midea-command-swing"><option value="">Säilytä nykyinen</option><option value="0">Pois</option><option value="12">Pysty</option><option value="3">Vaaka</option><option value="15">Molemmat</option></select></label>
          <button class="primary" id="midea-command-send" type="button" disabled>Lähetä ohjaus</button>
        </div>
        <p id="midea-control-feedback" role="status">Odota ensin onnistunutta tilanlukua.</p>
        <p class="tiny muted">Midea 0x40 -komento on koko asetustilan viesti. Tämä toteutus käyttää viimeisintä laitteelta luettua tilaa, lähettää yhden komennon ja tarkistaa tuloksen uudella tilanluvulla. Laitteen omat ajastukset ja mallikohtaiset lisäominaisuudet saattavat käyttäytyä eri tavoin, eikä vielä ole vahvistettua Lite-kenttätestiä.</p></article>
      <article class="card span12" id="firmware-update-card">
        <h2>Firmwarepäivitys</h2>
        <p class="tiny muted">Päivitä Lite suoraan dashboardista. Laite tarkistaa varmennetulta HTTPS-päivityspalvelimelta uuden allekirjoitetun version ja asentaa sen palautettavalle OTA-osiolle. Pumpun asetuksia ei muuteta. Tavallisen .bin-tiedoston lataus selaimesta ei ole käytössä.</p>
        <div class="statusgrid">
          <span>Nykyinen firmware</span><span id="ota-current">—</span>
          <span>Hyväksytty OTA-versio</span><span id="ota-good">—</span>
          <span>Viimeksi tarkistettu julkaisu</span><span id="ota-candidate">—</span>
          <span>Viimeisin tarkistus</span><span id="ota-last-check">—</span>
          <span>Päivityksen tila</span><span id="ota-stage">—</span>
          <span>Ladattu</span><span id="ota-downloaded">—</span>
        </div>
        <div class="flex" style="margin-top:16px;flex-wrap:wrap">
          <button type="button" class="primary" id="ota-install" disabled>Tarkista ja asenna päivitys</button>
          <button type="button" id="ota-refresh">Päivitä tilatiedot</button>
        </div>
        <p id="ota-feedback" class="tiny muted" role="status">Ladataan OTA-tilaa…</p>
      </article>
      <article class="card span5"><h2>Laitteen tila</h2><div class="statusgrid"><span>Ohjelmistoversio</span><span id="device-version">—</span><span>Wi-Fi RSSI</span><span id="device-wifi">—</span><span>CN105-telemetria</span><span id="device-cn105">—</span><span>Shelly-mittaus</span><span id="device-shelly">—</span><span>Sisäinen vapaa RAM</span><span id="device-ram">—</span><span>PSRAM vapaana</span><span id="device-psram">—</span><span>OTA-lataus käytössä</span><span id="device-ota">—</span></div><p class="foot"><a href="/#settings" style="text-decoration:underline">Avaa laiteasetukset</a></p></article>
      <article class="card span12"><h2>Mitsubishi CN105 · vastaanotettu data</h2><div class="statusgrid"><span>Viimeisin SSE-tilaviesti</span><span id="mitsubishi-read-status">—</span><span>Pumpun tila (SSE)</span><span id="mitsubishi-read-mode">—</span><span>Pumpun sisälämpötila (SSE)</span><span id="mitsubishi-read-temp">—</span><span>Asetuslämpötila (SSE)</span><span id="mitsubishi-read-setpoint">—</span><span>Kompressori (SSE)</span><span id="mitsubishi-read-hz">—</span><span>Kompressoriviestin ikä</span><span id="mitsubishi-compressor-age">—</span></div><p class="foot">ESPHome /events -rajapinnasta vastaanotettu viimeisin tilatieto. Viestin vastaanottoaika ei todista uutta CN105-UART-lukua, eikä näitä arvoja käytetä optimoinnin turvallisuushyväksyntään.</p></article>
      <article class="card span12"><h2>RuuviTag · anturit ja kosteusvaroitukset</h2>
      <p class="tiny muted">Anturin MAC-tunniste on sidottava tarkoituksella. Kirjoita tunniste tai <code>none</code> irrottaaksesi sidonnan. Asetukset ovat vain paikallisia.</p>
      <div class="statusgrid"><span>Sisäanturin sidonta</span><span id="ruuvi-bound">—</span><span>Havaittu tunniste</span><span id="ruuvi-discovered">—</span></div>
      <div class="fieldrow"><label class="field">Anturin rooli<select id="ruuvi-role"><option value="indoor">Sisäilma</option><option value="outdoor">Ulkoilma</option></select></label>
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

// Finnish and English are bundled in flash. No browser translation service,
// external network calls, firmware settings or heat-pump writes are involved.
let uiLanguage=(()=>{
  try {return localStorage.getItem('enevina_lite_language')==='en'?'en':'fi';}
  catch(_){return 'fi';}
})();
const UI_EN=Object.freeze({"Enevina Lite | Energianhallinta":"Enevina Lite | Energy Management","Päävalikko":"Main menu","Näkymät":"Views","Etusivu":"Home","Sähkön hinta":"Electricity price","Sääennuste":"Weather forecast","Optimointi":"Optimization","Asetukset":"Settings","Enevina Lite · paikallinen ohjain":"Enevina Lite · local controller","Laiteasetukset":"Device settings","Energia · Lämpöpumppu":"Energy · Heat pump","Aikaa ei ole synkronoitu":"Time not synchronized","Yhdistetään":"Connecting","OHJAUS POIS":"CONTROL OFF","Ota CN105-automaattiohjaus käyttöön Asetukset-näkymästä. Optimointiehdotus ei yksin lähetä käskyjä.":"Enable CN105 automatic control in Settings. An optimization suggestion alone never sends commands.","Yleisnäkymä":"Overview","Sisäilman olosuhteet":"Indoor conditions","Mittaukset, sähkön hinta ja tuleva lämmityssuunnitelma.":"Measurements, electricity prices and upcoming heating plan.","Sisälämpötila · RuuviTag":"Indoor temperature · RuuviTag","Tila ei tiedossa":"Status unknown","Odotetaan mittausta":"Waiting for measurement","Sähkön hinta nyt":"Electricity price now","Spot-hinta · ilman arvonlisäveroa":"Spot price · excluding VAT","Arvio kokonaishinnasta":"Estimated total price","Ulkolämpötila / ennuste":"Outdoor temperature / forecast","Mittauskulutus":"Measured power","Ilmankosteus":"Humidity","RuuviTag · suhteellinen kosteus":"RuuviTag · relative humidity","Kastepiste":"Dew point","Anturin tila":"Sensor status","Sähkön tulevat hinnat":"Upcoming electricity prices","15 minuutin jaksot":"15-minute periods","Hintadataa odotetaan":"Waiting for price data","Halvinta jaksoa ei tiedossa":"Cheapest period unknown","Kaikki hinnat":"All prices","Näytä 48 tunnin ennuste":"View 48-hour forecast","Odotetaan lämpötilaennustetta.":"Waiting for temperature forecast.","Oma energiankulutusluokka":"Personal energy consumption grade","Epävirallinen":"Unofficial","Vuoden kulutustieto puuttuu":"Annual consumption data missing","Lämmitetty pinta-ala m²":"Heated floor area m²","Vuotuinen sähkönkulutus kWh":"Annual electricity use kWh","Syötä vuosikulutus":"Enter annual consumption","Laske luokka":"Calculate grade","Oma havainnollistava A–G-asteikko: alle 60 / 100 / 150 / 200 / 250 / 350 kWh/m²/v. G yli 350. Ei virallinen energiatodistus tai E-luku; ei huomioi lämmitysmuotoja tai sääkorjausta. Tiedot säilyvät vain tässä selaimessa.":"Illustrative A–G scale: below 60 / 100 / 150 / 200 / 250 / 350 kWh/m²/year; G above 350. Not an official energy certificate or Finnish E-value. Heating type and weather adjustments are not included. Data is stored only in this browser.","Optimoinnin seuraava toimenpide":"Next optimization action","Ei suunnitelmaa":"No plan","Hyväksyttyä optimointisuunnitelmaa ei vielä ole.":"No validated optimization plan is available yet.","Optimointi näkyy vasta, kun käytettävissä on kelvollinen, turvallinen ja kalibroitu laskentapohja.":"Optimization is shown once valid, safe and calibrated model data is available.","Pörssisähkö":"Spot electricity","Sähkön hinnat":"Electricity prices","Suomen 15 minuutin hinnat. Kaavio näyttää vain ladatun tulevan datan.":"Finnish 15-minute prices. The chart displays only available future data.","Nykyinen spot":"Current spot price","snt/kWh, alv 0 %":"c/kWh, VAT 0%","Edullisin tuleva jakso":"Cheapest upcoming period","Ei tietoa":"No data","Saatavilla oleva ennuste":"Available forecast","Tietolähdettä ei ole päivitetty":"Source has not been updated","Hintakehitys":"Price trend","Hintoja ei ole saatavilla":"No prices available","Spot-hinta on ilman veroja. Kokonaishinnan arviossa sovelletaan laitteelle määriteltyjä sopimuskohtaisia vero- ja siirtomaksuja. Hintoja ei arvata puuttuville varttitunneille.":"Spot price excludes taxes. Estimated total price includes the configured electricity tax, grid charges and VAT. Missing 15-minute prices are never estimated.","Seuraavat hintajaksot":"Upcoming price periods","Alkaa":"Starts","Spot snt/kWh":"Spot c/kWh","Kokonaishinta snt/kWh":"Total price c/kWh","Odota hintatietoja.":"Waiting for prices.","Seuraava ennustelämpötila":"Next forecast temperature","Alin seuraavan 24 tunnin aikana":"Lowest in the next 24 hours","Ylin seuraavan 24 tunnin aikana":"Highest in the next 24 hours","Ennustettu ulkolämpötila":"Forecast outdoor temperature","Ulkolämpötila · seuraavat 48 tuntia":"Outdoor temperature · next 48 hours","Ennustetietoja odotetaan.":"Waiting for forecast data.","Ennusteen päivitysaikaa ei tiedetä.":"Forecast update time unknown.","Näytetään vain Ilmatieteen laitokselta vastaanotettuja kelvollisia tuntipisteitä. Ennuste voi poiketa toteutuneesta säästä; sade- ja tuulitiedot eivät sisälly tähän syötteeseen.":"Only validated hourly readings received from MET Norway are shown. Actual weather may differ; precipitation and wind data are not part of this feed.","Tuntiennuste":"Hourly forecast","Ajankohta":"Time","Lämpötila °C":"Temperature °C","Ladataan sääennustetta…":"Loading weather forecast…","Ennakoiva lämmitys":"Predictive heating","Optimointisuunnitelma":"Optimization plan","Hintaohjaus tuottaa ehdotuksen; CN105-komennot edellyttävät erillistä käyttöönottoa.":"Price-based control generates a proposal; CN105 commands require separate authorization.","Suunnitelma":"Plan","Ei kelvollista laskelmaa":"No valid calculation","Ennustettu kustannus":"Forecast cost","Mallin kokonaisarvio, jos kelvollinen":"Model total estimate, if valid","Ennustettu alin lämpötila":"Predicted lowest temperature","Laskettu, ei anturimittaus":"Calculated, not measured by a sensor","Hyväksyttyä suunnitelmaa ei ole. Lämpöpumppu ei saa tältä käyttöliittymältä komentoja.":"No validated plan is available. The heat pump receives no commands from this interface.","Ehdotus":"Suggestion","Ennuste °C":"Forecast °C","Sähkö kWh":"Electricity kWh","Arvio €":"Estimate €","Suunnitelma ei saatavilla.":"Plan unavailable.","Olosuhteet ja laite":"Conditions and device","Paikallinen huonetavoite ja erikseen käyttöönotettava CN105-lämmitysohjaus.":"Local room temperature target and independently enabled CN105 heating control.","Lämmityksen toimintatila":"Heating operating mode","Ylläpitolämpö ja Asuttu käyttävät omia, toisistaan riippumattomia huonetavoitteitaan. Hintaohjaus voi säätää Mitsubishin pyyntilämpötilaa.":"Maintenance and Occupied modes have separate room temperature targets. Price-based control may adjust the Mitsubishi setpoint.","Laskennallinen tila":"Operating mode","Kylmillään":"Unheated","Kuivanapito":"Drying","Ylläpitolämpö":"Maintenance","Asuttu":"Occupied","Ylläpitotavoite °C":"Maintenance target °C","Asuttu °C":"Occupied °C","Kuivanapito Δ°C":"Drying Δ°C","Tallenna asetukset":"Save settings","Valitse Ylläpitolämpö tai Asuttu sekä kyseisen tilan huonetavoite. Hintaehdotus laskee Mitsubishille 16–26 °C pyyntilämpötilan 0,5 °C askelin; komennot sallitaan vain erillisellä käyttöönotolla.":"Choose Maintenance or Occupied and its room target. Price-based control calculates Mitsubishi setpoints from 16 to 26 °C in 0.5 °C steps. Commands require separate authorization.","Mitsubishi · automaattinen lämpötilan säätö":"Mitsubishi · automatic temperature control","Laskenta käyttää RuuviTagia ja sähkön kokonaishintaa. Lämmitystä ohjataan Ylläpitolämpö- ja Asuttu-tiloissa. Komento lähtee CN105:lle, ja tavoitelämpötila tarkistetaan takaisinlukemalla. Käyttöönotto on aina erillinen valinta eikä firmwarepäivitys käynnistä sitä.":"Control uses RuuviTag measurements and total electricity price. Heating is controlled in Maintenance and Occupied modes. CN105 commands are verified by readback. Authorization is always separate; firmware updates do not enable control.","Ohjaus ei käytössä":"Control disabled","Ei komentoja":"No commands","Ota automaattiohjaus käyttöön":"Enable automatic control","Poista automaattiohjaus käytöstä":"Disable automatic control","Sähkösopimus ja siirto":"Electricity contract and distribution","Kokonaishinta = spot + marginaali + siirto + sähkövero, arvonlisävero lisätään kerran. Kuukausimaksut eivät vaikuta 15 minuutin ohjaukseen eikä niitä lasketa mukaan.":"Total price = spot + margin + grid charges + electricity tax, with VAT added once. Monthly fees are excluded from 15-minute control calculations.","Marginaali snt/kWh":"Margin c/kWh","Siirto päivä snt/kWh":"Daytime grid charge c/kWh","Siirto yö / muu aika snt/kWh":"Night/other grid charge c/kWh","Sähkövero snt/kWh":"Electricity tax c/kWh","ALV %":"VAT %","Siirtotuote":"Grid tariff","Yleissiirto":"Flat-rate distribution","Aikasiirto (päivä/yö)":"Time-of-use (day/night)","Kausisiirto (talviarkipäivä)":"Seasonal (winter weekdays)","Päivä alkaa klo":"Daytime starts at","Yö alkaa klo":"Nighttime starts at","Hinnat sisältävät ALV:n":"Prices include VAT","Kyllä (marginaali, siirto, vero)":"Yes (margin, grid, tax)","Ei, alv lisätään":"No, VAT is added","Esilämmityksen hintaetu %":"Preheating price advantage %","Spot-hinta on aina ilman arvonlisäveroa. Esilämmitys tehdään vain, kun hinta on vähintään annetun prosentin verran halvempi kuin seuraavien kahden tunnin mediaani.":"Spot price always excludes VAT. Preheating is considered only when the price is at least the specified percentage below the median of the next two hours.","Lämpöpumppu ja yhteydet":"Heat pump and connections","Lämpöpumppu ja yhteys":"Heat pump and connection","CN105-sillan /events-osoite":"CN105 bridge /events URL","Shelly-mittarin osoite":"Shelly meter URL","Valmistaja / malli":"Manufacturer / model","Yhteys":"Connection","Tila":"Status","Mittaukset":"Measurements","Komennot":"Commands","Ladataan…":"Loading…","Vain tällä laitteella todennetut sovittimet voi valita. Muut on lueteltu tutkimustietona, ei tukilupauksena. Sääpaikan muutos tulee voimaan seuraavassa virran kytkennässä. Laitteen valinta ei itsessään ota ohjausta käyttöön.":"Only adapters verified on this device can be selected. Other entries are research information, not guaranteed support. Weather location changes take effect on the next power cycle. Selecting a device does not enable control.","Tallenna sopimus ja yhteydet":"Save contract and connections","Vaihda Wi-Fi-verkko":"Change Wi-Fi network","Laitteen tila":"Device status","Ohjelmistoversio":"Firmware version","Wi-Fi RSSI":"Wi-Fi RSSI","CN105-telemetria":"CN105 telemetry","Shelly-mittaus":"Shelly measurement","Sisäinen vapaa RAM":"Free internal RAM","PSRAM vapaana":"Free PSRAM","OTA-lataus käytössä":"OTA downloads enabled","Avaa laiteasetukset":"Open device settings","Mitsubishi CN105 · vastaanotettu data":"Mitsubishi CN105 · received data","Viimeisin SSE-tilaviesti":"Latest SSE status event","Pumpun tila (SSE)":"Pump mode (SSE)","Pumpun sisälämpötila (SSE)":"Pump indoor temperature (SSE)","Asetuslämpötila (SSE)":"Target temperature (SSE)","Kompressori (SSE)":"Compressor (SSE)","Kompressoriviestin ikä":"Age of compressor event","ESPHome /events -rajapinnasta vastaanotettu viimeisin tilatieto. Viestin vastaanottoaika ei todista uutta CN105-UART-lukua, eikä näitä arvoja käytetä optimoinnin turvallisuushyväksyntään.":"Latest status data received via ESPHome /events. Event reception does not prove a fresh CN105 UART poll; these values are not treated as verified optimization-safety data.","RuuviTag · anturit ja kosteusvaroitukset":"RuuviTag · sensors and humidity alerts","Anturin MAC-tunniste on sidottava tarkoituksella. Kirjoita tunniste tai":"Sensor MAC must be explicitly bound. Enter an address or","irrottaaksesi sidonnan. Asetukset ovat vain paikallisia.":"to unbind it. Settings are local only.","Sisäanturin sidonta":"Indoor sensor binding","Havaittu tunniste":"Detected identifier","Anturin rooli":"Sensor role","Sisäilma":"Indoor air","Ulkoilma":"Outdoor air","XX:XX:XX:XX:XX:XX tai none":"XX:XX:XX:XX:XX:XX or none","Tallenna anturisidonta":"Save sensor binding","RH-varoitus %":"RH warning %","Korkea RH-varoitus %":"High RH warning %","Paristovaroitus V":"Battery warning V","Tallenna varoitusrajat":"Save alert thresholds","Ilman RH-arvot eivät yksin osoita rakenteiden kosteusturvallisuutta.":"Air humidity alone does not establish whether building structures are safe from moisture.","Sisäilman historia · RuuviTag":"Indoor climate history · RuuviTag","Historiatietoja odotetaan":"Waiting for history data","Viimeisimmät mittaukset, ei rakenteen kosteusmittaus. Offline-ajalta tallennettuja aikaleimoja ei esitetä UTC-aikana.":"Recent measurements, not structural moisture measurements. Offline timestamps are not shown as UTC.","Enevina Lite · Paikallinen toiminta ·":"Enevina Lite · Local operation ·","Ei aikaa":"No time available"," kWh/m² vuodessa":" kWh/m² per year","Syötä todellinen 12 kuukauden kulutus":"Enter actual consumption for the last 12 months","Syötä yli nollan oleva lämmitetty pinta-ala ja täyden vuoden kulutus.":"Enter a heated floor area above zero and full-year consumption.","Vertailuluokka tallennettu tähän selaimeen. Ei virallinen energiatodistus tai E-luku.":"Comparison grade saved in this browser. Not an official energy certificate or E-value.","Vertailuluokka laskettu; selain ei sallinut paikallista tallennusta.":"Comparison grade calculated; the browser did not permit local storage.","Tulevien 15 minuutin sähkönhintojen pylväskaavio":"Bar chart of upcoming 15-minute electricity prices","Vaihteluväli: ":"Price range: ","Mittaukset käytössä":"Measurements active","Yhteys muodostettu":"Connected","Ei tuoretta mittausta":"No recent measurement","RuuviTag ei ole tuore tai anturia ei ole sidottu":"RuuviTag is stale or no sensor is bound","Tuore":"Fresh","Puuttuu / vanhentunut":"Missing / stale","Viimeisin haku: ":"Last fetched: ","CN105-OHJAUS SALLITTU":"CN105 CONTROL ENABLED","Automaattiohjaus on kytketty. Käskyjä lähetetään Ylläpitolämpö- tai Asuttu-tilassa, kun mittaus- ja yhteysportit ovat kunnossa.":"Automatic control is enabled. Commands are sent in Maintenance or Occupied mode when sensor and connection checks pass.","Automaattiohjaus ei ole päällä. Laskenta näyttää ehdotuksia ilman fyysisiä käskyjä.":"Automatic control is off. Calculations show suggestions without physical commands.","Automaattiohjaus valittu · ":"Automatic control enabled · ","Automaattiohjaus pois":"Automatic control off","Komentoja ":"Commands "," · sillalta kuitattu ":" · acknowledged by bridge "," · virheitä ":" · errors ","Ei Wi-Fiä":"No Wi-Fi","UART-lähde varmennettu":"UART source verified","SSE-lukemia, UART-todiste puuttuu":"SSE readings, no UART proof","Ei CN105-lukemia":"No CN105 readings","UART-mittaus varmennettu":"UART reading verified","SSE vastaanotettu ":"SSE received ","(ikä ei tiedossa)":"(age unknown)"," s sitten":" s ago"," · ei UART-todistetta":" · no UART proof","Ei tilatietoja":"No status data"," (SSE)":" (SSE)","Ei vastaanottoa":"Not received"," s sitten (vain SSE)":" s ago (SSE only)","Käytössä":"Available","Ei lukemaa":"No measurement","Kyllä · allekirjoitettu":"Yes · signed","Ei":"No","Ei sidottu":"Not bound","Ei havaintoa":"No data","RH koholla":"High RH","RH erittäin korkea":"Very high RH","RuuviTag ei vastaa":"RuuviTag not responding","RuuviTag-paristo heikko":"Low RuuviTag battery","Pakkaslämpötila":"Freezing temperature","Varoitus":"Warning","Kuittaa":"Acknowledge","Kuittaus epäonnistui: ":"Acknowledgment failed: "," jaksoa":" periods","Edullisin seuraava jakso ":"Cheapest upcoming period ","Ei kelvollista hintadataa":"No valid price data","Hintatietoja ei saatavilla":"Price data unavailable","Ei voimassa olevia hintoja.":"No currently valid prices.","Pidä nykyinen tila":"Maintain current state","Hintaohjauksen ehdotus":"Price-based control proposal","Huonetermostaatin ehdotus":"Room thermostat proposal","MPC-laskelma":"MPC calculation","Ei hyväksyttyä suunnitelmaa":"No validated plan","Hintaohjaus":"Price-based control","Termostaattiohjaus":"Thermostat control","Ei käytössä":"Not active","Laskettu ":"Calculated ","Seuraava jakso ":"Next period ","Lähtötason ehdotus: ":"Fallback proposal: ","Hyväksytty optimointisuunnitelma puuttuu.":"No validated optimization plan is available.","Simuloitu MPC-ehdotus. Automaattiohjaus toimii erillisellä hintaohjauspolulla.":"Simulated MPC proposal. Automatic control operates through a separate price-based control path.","Ruuvi + 15 min kokonaishinnat; ":"Ruuvi + 15-minute total prices; ","Hintadata puuttuu: Ruuvi-huonetermostaatti; ":"Price data missing: Ruuvi room thermostat; ","huonetavoite ":"room target ","; syy: ":"; reason: ",". CN105-ohjaus edellyttää käyttäjän valintaa.":". CN105 control requires explicit user authorization.","puutteelliset lähtötiedot":"incomplete inputs","Ehdotus estetty: ":"Proposal blocked: ","Ehdotukset ovat laskennallisia. Fyysiset ohjauskäskyt ovat estettyjä.":"Proposals are calculated. Physical commands are blocked.","Lähtötason hintaohjaus: ":"Fallback price-based control: ","Hintojen puuttuessa termostaatti: ":"Thermostat fallback without prices: "," (ehdotus; automaattiohjauksen käynnistys valitaan Asetuksissa).":" (proposal; automatic control must be enabled in Settings).","Optimointiehdotus ei ole saatavilla. Puuttuva lämpötila tai puutteellinen hintajakso estää laskennan.":"No optimization proposal available. A missing temperature or incomplete price period prevents calculation.","Nykyinen ehdotus: ":"Current proposal: ",". Mallinnettu tuntien lämpötila- ja kustannusennuste puuttuu.":". Modeled hourly temperature and cost forecast is missing.","Lämpötilahistoriaa ei vielä ole riittävästi":"Not enough temperature history yet","Sisälämpötilan historia":"Indoor temperature history","Näkyvät lämpötilat: ":"Visible temperatures: "," tuntipistettä":" hourly points","Yhteys poikki":"Disconnected","Ei yhteyttä":"No connection","Mittaukset eivät päivity":"Measurements are not updating","Ei tuoretta tilaa":"No recent status","Laite ei vastaa tai vahvistustunniste puuttuu.":"Device unavailable or authorization token missing.","Tallennetaan ohjauksen käyttöönottoa…":"Saving control authorization…","Asetuksen tallennus epäonnistui":"Could not save setting","Automaattiohjaus sallittu. Ensimmäinen ohjaus odottaa tuoreita tietoja ja Ylläpitolämpö- tai Asuttu-tilaa.":"Automatic control authorized. First command awaits fresh measurements and Maintenance or Occupied mode.","Automaattiohjaus poistettu käytöstä.":"Automatic control disabled.","Ohjauksen asetusta ei tallennettu":"Control setting was not saved","Laite ei vastaa tai valtuutus puuttuu":"Device unavailable or authorization missing","Tallennus epäonnistui":"Save failed","Anna kuusi tavuinen MAC-tunniste tai none":"Enter a six-byte MAC address or none","Anturisidonta tallennettu.":"Sensor binding saved.","Tarkista varoitusarvot":"Check warning thresholds","Varoitusrajat tallennettu.":"Alert thresholds saved.","Aiemmat varoitusrajat tallennettu. Uudet muutokset odottavat tallennusta.":"Earlier alert thresholds saved. New changes still await saving.","Tiedot puuttuvat.":"Data missing.","Tarkista lämpötilatavoitteet.":"Check the temperature targets.","Tallennetaan paikallisia asetuksia…":"Saving local settings…","Asetukset tallennettu. Lämpöpumpulle ei lähetetty käskyä.":"Settings saved. No command was sent to the heat pump.","Aiemmat asetukset tallennettu. Uusia muutoksia odottaa tallennusta.":"Earlier settings saved. New changes are pending.","Tallennus ei onnistunut.":"Save failed.","Tarkista numero":"Check number","Arvo on sallitun alueen ulkopuolella":"Value is out of range","Osoite ei kelpaa (vain paikallinen http-osoite)":"Invalid URL (local HTTP URL only)","Liian pitkä arvo":"Value too long","Virheellinen valinta":"Invalid selection","Laitetta ei vielä tueta":"Device is not yet supported","Asetukset eivät ole kelvolliset":"Settings are invalid","Tallennus laitteen muistiin epäonnistui":"Saving to device memory failed","Päivitä sivu ja yritä uudelleen":"Refresh the page and try again","Kenttäpilotti":"Field pilot","Vain PC-testattu":"PC-tested only","Vain selvitetty":"Documented only","Ei valittu":"Not selected","Asetuksia ei voitu lukea laitteelta.":"Could not read settings from device.","Tallennettu. Lämpöpumpun yhteys vaihtui, joten automaattiohjaus poistettiin käytöstä: ota se uudelleen käyttöön uudelle laitteelle.":"Saved. Heat pump connection changed, so automatic control was disabled. Re-enable it for the new device.","Tallennettu. Uudet hinnat ja osoitteet ovat heti käytössä.":"Saved. New prices and addresses take effect immediately.","Tarvittava laite ei vastannut.":"Required device did not respond.","MET Norwayn ennuste haettu: ":"MET Norway forecast fetched: ","Kelvollista MET Norwayn ennustetta ei ole saatavilla.":"No valid MET Norway forecast available.","Kelvollista lämpötilaennustetta ei ole saatavilla":"No valid temperature forecast available","Seuraava ulkolämpötila ":"Next outdoor temperature "," klo ":" at "," ennustetuntia saatavilla.":" forecast hours available.","Ennustetta ei ole saatavilla. Lämmityksen ohjaus ei käytä keksittyjä sääarvoja.":"Forecast unavailable. Heating control does not use invented weather readings.","Lämpötilan käyrään tarvitaan vähintään kaksi tuntipistettä.":"A temperature curve requires at least two hourly points.","MET Norwayn ulkolämpötilaennusteen kuvaaja":"Chart of MET Norway outdoor temperature forecast","Ennustetunteja ei ole saatavilla.":"No hourly forecast available.","Käyttöliittymän kieli":"Interface language","Sääennuste · MET Norway":"Weather forecast · MET Norway","Norjan ilmatieteen laitos · MET Norway":"Norwegian Meteorological Institute · MET Norway","Paikallinen ulkolämpötilaennuste MET Norwayn maailmanlaajuisesta sääpalvelusta. Puuttuvia arvoja ei arvata.":"Local outdoor temperature forecast from MET Norway's worldwide weather service. Missing values are not estimated.","Odotetaan MET Norwayn ennustetta":"Waiting for MET Norway forecast","Sääpaikka (leveysaste,pituusaste)":"Weather location (latitude,longitude)","Sääpaikan muutos tulee voimaan heti tallennuksen jälkeen.":"Weather location changes take effect immediately after saving.","Näytetään vain MET Norwaylta vastaanotettuja kelvollisia tuntipisteitä.":"Only validated hourly values from MET Norway are shown.","MET Norwayn ennustetta ei ole saatavilla.":"No MET Norway forecast available.","Lähde:":"Source:",".\n      Ennustetiedot on muunnettu Enevina Liten lämpötilakäyräksi.\n      Näytetään vain MET Norwaylta vastaanotettuja kelvollisia tuntipisteitä. Ennuste voi poiketa toteutuneesta säästä; sade- ja tuulitiedot eivät sisälly tähän syötteeseen.":".\n      Forecast data has been converted into the Enevina Lite temperature chart.\n      Only validated hourly points from MET Norway are shown. Actual weather may vary; wind and precipitation are not included in this view.","Vain tällä laitteella todennetut sovittimet voi valita. Muut on lueteltu tutkimustietona, ei tukilupauksena. Sääpaikan muutos tulee voimaan heti tallennuksen jälkeen. Laitteen valinta ei itsessään ota ohjausta käyttöön.":"Only adapters verified on this device can be selected. Other entries are research information, not guaranteed support. The weather location changes immediately after saving. Selecting a device does not enable control.","Mitsubishi on CN105-kenttäpilotti ja Midea V2/V3 voidaan valita yhteyden tilanlukuun ja manuaaliseen ohjaustestiin. Muut on lueteltu tutkimustietona, ei tukilupauksena. Sääpaikan muutos tulee voimaan heti tallennuksen jälkeen. Laitteen valinta ei itsessään ota ohjausta käyttöön.":"Mitsubishi CN105 is a field pilot; Midea V3 is selectable for read-only telemetry. Other entries are research only, not guaranteed compatibility. Weather location changes immediately after saving. Device selection does not enable control.","Midea · paikallinen V3 Wi-Fi -yhteys":"Midea · local V3 Wi-Fi connection","V3-luku tapahtuu suoraan lämpöpumpun alkuperäisestä Wi-Fi-moduulista ilman Home Assistantia tai jatkuvaa pilviyhteyttä. Midean manuaalinen lämpötilan ja toimintatilan ohjaus on nyt kokeellinen; automaattisen optimoinnin komentoja ei vielä lähetetä. Yhdistä vain omassa luotetussa lähiverkossa; kehitysvaiheen HTTP-asetussivu ei salaa tunnisteiden siirtoa.":"V3 status is read directly from the heat pump's original Wi-Fi module without Home Assistant or a continuous cloud connection. This is a read-only test: no automatic temperature commands are sent. Use only a trusted local network; this development HTTP setup page does not encrypt the credentials in transit.","Midean paikallinen IP":"Midea local IP address","Midea V3 device ID":"Midea V3 device ID","V3 token (128 heksamerkkiä)":"V3 token (128 hex characters)","V3 key (64 heksamerkkiä)":"V3 key (64 hex characters)","Tallenna Midea-yhteys":"Save Midea connection","Ei mittausta":"No measurement","Yhteyden tila":"Connection status","Toimintatila":"Operating mode","Pyyntilämpötila":"Target temperature","Pumpun sisälämpötila":"Heat pump indoor temperature","V3 tunnisteet tallennetaan laitteen paikalliseen NVS-muistiin. Niitä ei palauteta selaimeen. DHCP-IP:n automaattinen tunnistus ja automaattinen Midea-optimointiohjaus eivät vielä sisälly tähän testitoteutukseen.":"V3 credentials are stored in the device's local NVS memory and are not returned to the browser. Automatic DHCP IP rediscovery and heat pump control are not included in this read-only version.","Lukuyhteys (testivaihe)":"Read-only connection (experimental)","Midea V3 on tässä versiossa vain tilanluku. Ohjaus ei ole käytössä.":"Midea V3 is read-only in this version. Control is not available.","Midea ei valittu":"Midea not selected","Ei asetuksia":"Not configured","Paikallinen tilanluku toimii":"Local status polling works","V3-yhteys onnistui":"V3 connection succeeded","Midea-yhteyttä ei ole asetettu":"Midea connection not configured","Lämmitys":"Heating","Tila ":"Mode ","Pois päältä":"Off","Laite ei vastaa.":"Device not responding.","Tarkista device ID, V3 token (128) ja key (64 heksamerkkiä).":"Check device ID, V3 token (128) and key (64 hex characters).","Midea-yhteys tallennettu. Ensimmäinen luku tapahtuu verkossa.":"Midea connection saved. The first status poll will take place over the network.","Tallennus ei onnistunut (":"Save failed (","Yhteyttä ei ole":"Not connected","Ulkolämpötila":"Outdoor temperature","Testaa yhteys nyt":"Test connection now","Ei vielä testattu.":"Not tested yet.","Valitse Midea lämpöpumpun yhteydeksi.":"Select Midea as the heat pump connection.","Midean asetukset puuttuvat.":"Midea connection settings are missing.","V3-tilanluku onnistui.":"V3 status polling succeeded.","Tarkista pumpun IP-osoite.":"Check the heat pump IP address.","Midean tunnistetiedot ovat virheelliset.":"Midea device credentials are invalid.","Ei TCP-yhteyttä Mideaan (portti 6444).":"Cannot connect to Midea on TCP port 6444.","V3-tunnistautuminen epäonnistui: tarkista token ja key.":"V3 authentication failed: check token and key.","Midean V3-protokollavastaus ei kelpaa.":"Midea V3 protocol response is invalid.","Pumpun tilavastausta ei voitu tulkita.":"Cannot parse the heat pump state response.","Laitteen kellonaika ei ole vielä synkronoitunut.":"Device clock has not yet synchronized.","Tuntematon Midea-yhteysvirhe.":"Unknown Midea connection error.","Yhteystesti käynnissä. ":"Connection test running. "," Viimeisin yritys: ":" Last attempt: ","Yhteystesti käynnistetty. Pumppua ei ohjata.":"Connection test started. Heat pump control remains disabled.","Yhteystestiä ei voitu käynnistää.":"Could not start the connection test.","Firmwarepäivitys":"Firmware update","Päivitä Lite suoraan dashboardista. Laite tarkistaa varmennetulta HTTPS-päivityspalvelimelta uuden allekirjoitetun version ja asentaa sen palautettavalle OTA-osiolle. Pumpun asetuksia ei muuteta. Tavallisen .bin-tiedoston lataus selaimesta ei ole käytössä.":"Update Lite directly from the dashboard. The device checks a verified HTTPS update server for a signed release and installs it to the rollback-capable OTA partition. Heat pump settings are unchanged. Uploading an ordinary .bin file from the browser is not supported.","Nykyinen firmware":"Current firmware","Hyväksytty OTA-versio":"Confirmed OTA version","Viimeksi tarkistettu julkaisu":"Latest checked release","Viimeisin tarkistus":"Last check","Päivityksen tila":"Update status","Ladattu":"Downloaded","Tarkista ja asenna päivitys":"Check and install update","Päivitä tilatiedot":"Refresh status","Ladataan OTA-tilaa…":"Loading OTA status…","Ei uutta hyväksyttyä versiota":"No newer eligible release","Päivityskanava pois käytöstä":"Update channel disabled","Odottaa Wi-Fi-yhteyttä tai kellonaikaa":"Waiting for Wi-Fi or correct time","Tilapäinen resurssi- tai tallennusvirhe":"Temporary resource or storage error","Julkaisun tarkistus tai lataus epäonnistui":"Release verification or download failed","Allekirjoitettu päivitys latautuu":"Downloading signed update","Päivitys valmis, laite käynnistyy uudelleen":"Update ready, device restarting","Ei vielä vahvistettua julkaisua":"No confirmed release yet","Ei vielä tarkistettu":"Not checked yet","Tarkistus jonossa":"Check queued","Tuntematon tila":"Unknown status","Päivitys käyttää vain allekirjoitettua julkaisua ja säilyttää edellisen toimivan OTA-version.":"Only signed releases are installed, preserving the previous working OTA image.","Allekirjoitettua OTA-päivityskanavaa ei ole tällä laitteella. Päivitystä ei voi käynnistää ennen sen käyttöönottoa.":"A signed OTA update channel is not configured on this device. Updates cannot be started until it is provisioned.","Tarkistetaanko allekirjoitettu firmware ja asennetaanko uusi versio, jos sellainen löytyy? Laite voi käynnistyä uudelleen.":"Check signed firmware and install a new release if available? The device may restart.","Päivitystarkistus lähetetään laitteelle…":"Requesting firmware update check…","Tarkistus käynnistetty. Uusi allekirjoitettu julkaisu asennetaan, jos se on saatavilla.":"Update check started. A newer signed release will be installed if available.","Päivitystarkistuksen käynnistys epäonnistui: ":"Could not start firmware update check: ","Midea · paikallinen V2 / V3 Wi-Fi -yhteys":"Midea · local V2 / V3 Wi-Fi connection","V2- tai V3-tilanluku tapahtuu suoraan lämpöpumpun Wi-Fi-moduulista ilman Home Assistantia tai jatkuvaa pilviyhteyttä.":"V2 or V3 status is read directly from the heat pump Wi-Fi module without Home Assistant or a persistent cloud connection.","Midea-protokolla":"Midea protocol","V2 (ei tokenia)":"V2 (no token)","Midean automaattinen optimointiohjaus ei ole vielä käytössä; manuaalinen ohjaus löytyy Midea-asetuksista.":"Midea automatic optimizer control is not enabled yet; manual control is available in Midea settings.","V3-tunnisteet tallennetaan paikalliseen NVS-muistiin, eikä niitä palauteta selaimeen. V2 ei tarvitse tokenia tai keytä, mutta sen yhteys käyttää yhteistä kiinteää salausavainta eikä todenna pumppua samalla tavalla kuin V3. Valitse protokolla laitteen todetun version mukaan. Käytä vain luotetussa lähiverkossa. DHCP-IP:n automaattinen tunnistus ja automaattinen Midea-optimointiohjaus eivät vielä sisälly tähän testitoteutukseen.":"V3 credentials are stored in local NVS and are not returned to the browser. V2 does not need a token or key, but uses a shared static encryption key and does not authenticate the device like V3. Select the device’s verified protocol. Use only on a trusted LAN. Automatic DHCP IP rediscovery and heat pump control are not included.","Midean tilanluku onnistui.":"Midea state query succeeded.","V2-yhteys onnistui":"V2 connection successful","Tarkista Midean device ID. V2 ei tarvitse tokenia eikä keytä.":"Check the Midea device ID. V2 needs no token or key.","V2- tai V3-tilanluku tapahtuu suoraan lämpöpumpun Wi-Fi-moduulista ilman Home Assistantia tai jatkuvaa pilviyhteyttä. Midean manuaalinen lämpötilan ja toimintatilan ohjaus on nyt kokeellinen; automaattisen optimoinnin komentoja ei vielä lähetetä. Yhdistä vain omassa luotetussa lähiverkossa; kehitysvaiheen HTTP-asetussivu ei salaa tunnisteiden siirtoa.":"V2/V3 status is read directly from the heat pump Wi-Fi module without Home Assistant or persistent cloud connection. Manual Midea setpoint and mode control is experimental; automatic optimizer commands are not sent yet. Connect only on your own trusted local network; this development HTTP setup does not encrypt the credentials in transit.","Midean paikallinen ohjaus · V2 / V3":"Midea local control · V2 / V3","Yksittäiset muutokset lähetetään suoraan pumpulle alkuperäistä Wi-Fiä käyttäen. Tunnetut muut asetukset säilytetään viimeksi luetusta tilasta. Käsky on manuaalinen; pörssisähköoptimoinnin automaattista Midea-käskynlähetystä ei ole vielä kytketty.":"Individual changes are sent directly to the pump using its original Wi-Fi. Other known settings are preserved from the latest state. Commands are manual; Midea automation by spot-price optimizer is not yet connected.","Virta":"Power","Säilytä nykyinen":"Keep current","Päälle":"On","Pois":"Off","Jäähdytys":"Cool","Kuivaus":"Dry","Puhallus":"Fan","Pyynti °C":"Setpoint °C","Säilytä":"Keep current","Hiljainen":"Silent","Matala":"Low","Keskitaso":"Medium","Korkea":"High","Täysi":"Full","Pysty":"Vertical","Vaaka":"Horizontal","Molemmat":"Both","Lähetä ohjaus":"Send control","Odota ensin onnistunutta tilanlukua.":"Wait for a successful state reading first.","Midea 0x40 -komento on koko asetustilan viesti. Tämä toteutus käyttää viimeisintä laitteelta luettua tilaa, lähettää yhden komennon ja tarkistaa tuloksen uudella tilanluvulla. Laitteen omat ajastukset ja mallikohtaiset lisäominaisuudet saattavat käyttäytyä eri tavoin, eikä vielä ole vahvistettua Lite-kenttätestiä.":"Midea 0x40 is a full-state command. The controller reads the pump state, sends one command and verifies it by a fresh query. Native timers and model-specific extras may behave differently; Lite field testing is pending.","Midean manuaalinen lämpötilan ja toimintatilan ohjaus on nyt kokeellinen; automaattisen optimoinnin komentoja ei vielä lähetetä.":"Manual Midea setpoint and mode control is experimental; automatic optimization commands are not yet sent.","DHCP-IP:n automaattinen tunnistus ja automaattinen Midea-optimointiohjaus eivät vielä sisälly tähän testitoteutukseen.":"Automatic DHCP IP discovery and automatic Midea optimization are not yet included in this test implementation.","Ohjausta ei vielä lähetetty.":"No control command has been sent.","Pumppu ei vahvistanut muutosta takaisinluvussa.":"Pump did not confirm the requested change in readback.","Ohjauksen suorittamiseen ei ollut resursseja.":"Resources unavailable to execute control.","Pumpun ohjaus vahvistettu takaisinluvulla.":"Pump control confirmed by a fresh readback.","Ohjaus odottaa suorittamista.":"Control queued.","Ohjausta suoritetaan.":"Control in progress.","Asetustunniste puuttuu.":"Settings token unavailable.","Valitse ensin vähintään yksi muutettava asetus.":"Select at least one setting to change.","Lähetetään manuaalinen Midea-ohjaus…":"Sending manual Midea control…","Ohjaus jonossa. Pumpun vastaus ja takaisinluku tarkistetaan.":"Control queued; verifying pump response and readback.","Ohjausta ei voitu käynnistää: ":"Could not queue control: ","Manuaalinen":"Manual","Kokeellinen manuaaliohjaus":"Experimental manual control","Midean mallikohtainen koekäyttöhyväksyntä":"Model-specific Midea pilot approval","Käyttöönotto koskee vain yhtä asetettua laitetta ja käynnistyskertaa. Anna pumpun tarkka mallimerkintä sekä mallille varmistetut pyynnin rajat. Tämä ei luo salasanaa tai poista ohjausominaisuuksia.":"Approval applies only to one configured device and one boot session. Enter the exact pump model and the model-verified setpoint limits. No password or feature removal is introduced.","Pumpun tarkka malli":"Exact pump model","Mallimerkintä":"Model identifier","Mallin minimipyynti °C":"Model minimum setpoint °C","Mallin maksimipyynti °C":"Model maximum setpoint °C","Mallin minimi":"Model minimum","Mallin maksimi":"Model maximum","Hyväksy mallikohtainen ohjaustesti":"Approve model-specific control test","Peru koekäyttöhyväksyntä":"Revoke pilot approval","Mallikohtaista ohjaustestiä ei ole hyväksytty.":"Model-specific control test not approved.","Mallikohtainen ohjaustesti hyväksytty. Pyyntirajat: ":"Model pilot approved. Setpoint limits: ","Tarkista malli, device ID ja mallikohtaiset lämpötilarajat.":"Check model, device ID, and model-specific setpoint limits.","Hyväksytkö Midea ":"Do you approve a manual control pilot for Midea "," -pumpun manuaalisen ohjauskokeen tälle laitteelle?":" on this configured device?","Mallin manuaaliohjaus hyväksytty.":"Manual control pilot approved.","Koekäyttöhyväksyntä peruttu.":"Pilot approval revoked.","Koekäytön asetusta ei voitu muuttaa: ":"Could not change pilot approval: ","Valitun alueen 15 minuutin hinnat. Kaavio näyttää vain ladatun tulevan datan.":"15-minute prices for the selected area. The chart shows only downloaded future data."});
const UI_EN_FRAGMENTS=Object.entries(UI_EN)
  .filter(([key,value])=>key.length>=8 && key!==value)
  .sort((a,b)=>b[0].length-a[0].length);
function uiLocale(){return uiLanguage==='en'?'en-GB':'fi-FI';}
function translateFi(value){
  const raw=String(value??''),trim=raw.trim();
  if(uiLanguage!=='en'||!trim)return raw;
  const leading=raw.match(/^\s*/)[0],trailing=raw.match(/\s*$/)[0];
  if(Object.prototype.hasOwnProperty.call(UI_EN,trim))
    return leading+UI_EN[trim]+trailing;
  // Composite live status strings reuse the same translated Finnish clauses.
  // Longest phrase first avoids partial-prefix collisions.
  let out=trim;
  for(const [fi,en] of UI_EN_FRAGMENTS)
    if(out.includes(fi))out=out.split(fi).join(en);
  out=out.replace(/snt\/kWh/g,'c/kWh');
  return leading+out+trailing;
}
// The dashboard updates data-driven text every 15 seconds. Save the original
// Finnish per text node, including new nodes and changed live measurements,
// so switching back to Finnish never requires a page reload or API mutation.
const uiOriginalText=new WeakMap();
const uiOriginalAttr=new WeakMap();
function translateTextNode(node){
  let current=node.nodeValue,record=uiOriginalText.get(node);
  if(!record||record.rendered!==current)
    record={fi:current,rendered:current};
  const next=uiLanguage==='en'?translateFi(record.fi):record.fi;
  record.rendered=next;uiOriginalText.set(node,record);
  if(current!==next)node.nodeValue=next;
}
function translateAttributes(element){
  if(!element.getAttribute)return;
  let previous=uiOriginalAttr.get(element);
  if(!previous){previous={};uiOriginalAttr.set(element,previous);}
  for(const attr of ['aria-label','placeholder','title']) {
    const value=element.getAttribute(attr);
    if(value===null)continue;
    let record=previous[attr];
    if(!record||record.rendered!==value)
      record={fi:value,rendered:value};
    const next=uiLanguage==='en'?translateFi(record.fi):record.fi;
    record.rendered=next;previous[attr]=record;
    if(value!==next)element.setAttribute(attr,next);
  }
}
function translateDashboard(){
  if(!document.body||typeof document.createTreeWalker!=='function'||
     typeof NodeFilter==='undefined')return;
  const walker=document.createTreeWalker(document.body,NodeFilter.SHOW_TEXT);
  let node;
  while((node=walker.nextNode())){
    const parent=node.parentElement;
    if(parent&&parent.closest('script,style'))continue;
    translateTextNode(node);
  }
  document.querySelectorAll('[aria-label],[placeholder],[title]')
    .forEach(translateAttributes);
  document.documentElement.lang=uiLanguage;
  document.title=uiLanguage==='en'?'Enevina Lite | Energy Management':
    'Enevina Lite | Energianhallinta';
  document.querySelectorAll('[data-lang]').forEach(button=>{
    button.setAttribute('aria-pressed',String(button.dataset.lang===uiLanguage));
  });
}
let uiTranslatePending=false;
function queueUiTranslation(){
  if(uiTranslatePending)return;
  uiTranslatePending=true;
  Promise.resolve().then(()=>{
    uiTranslatePending=false;
    translateDashboard();
  });
}
if(typeof MutationObserver!=='undefined'&&document.body){
  const observer=new MutationObserver(queueUiTranslation);
  observer.observe(document.body,{subtree:true,childList:true,characterData:true});
}
function setDashboardLanguage(language){
  if(language!=='fi'&&language!=='en')return;
  uiLanguage=language;
  try{localStorage.setItem('enevina_lite_language',language);}catch(_){}
  document.querySelectorAll('[data-lang]').forEach(button=>{
    button.setAttribute('aria-pressed',String(button.dataset.lang===language));
  });
  // Re-render times and numbers in the requested locale without rewriting
  // unsaved forms, sensor bindings, local settings or heating authorization.
  if(state.status)showStatus();
  if(state.prices)showPrices();
  if(state.weather)showWeather();
  if(state.plan)showPlan();
  if(state.history)showHistory();
  showEnergyGrade();
  translateDashboard();
}
document.querySelectorAll('[data-lang]').forEach(button=>
  button.addEventListener('click',()=>setDashboardLanguage(button.dataset.lang)));

const $=id=>document.getElementById(id);
const state={status:null,prices:null,weather:null,plan:null,history:null,mode:0,settingsDirty:false,settingsRevision:0,settingsSaving:false,statusRequestSequence:0,alertSettingsLoaded:false,
  alertSettingsDirty:false,alertSettingsSaving:false,alertSettingsRevision:0};
const fmt=(v,d=1)=>Number.isFinite(v)?v.toLocaleString(uiLocale(),{minimumFractionDigits:d,maximumFractionDigits:d}):'—';
const text=(id,value)=>{const e=$(id);if(e)e.textContent=value==null?'—':String(value);};
// Civil timestamps follow the home's selected city timezone, not the
// browser device's timezone or an assumed Finland-only market.
function regionTimeZone(){
  const r=state.site&&state.site.region||{};
  if(r.timezone){
    try{new Intl.DateTimeFormat('en-GB',{timeZone:r.timezone});return r.timezone;}catch(_){}
  }
  const fallback={FI:'Europe/Helsinki',SE:'Europe/Stockholm',NO:'Europe/Oslo',
    DK:'Europe/Copenhagen',EE:'Europe/Tallinn',LV:'Europe/Riga',
    LT:'Europe/Vilnius',DE:'Europe/Berlin',NL:'Europe/Amsterdam',
    BE:'Europe/Brussels',FR:'Europe/Paris',PL:'Europe/Warsaw',
    AT:'Europe/Vienna',BG:'Europe/Sofia'};
  return fallback[r.country]||'Europe/Helsinki';
}
const time=sec=>Number.isFinite(sec)&&sec>1700000000?new Date(sec*1000).toLocaleString(uiLocale(),{timeZone:regionTimeZone(),day:'2-digit',month:'2-digit',hour:'2-digit',minute:'2-digit'}):'Ei aikaa';
const number=v=>typeof v==='number'&&Number.isFinite(v)?v:null;
const modes=['Kylmillään','Kuivanapito','Ylläpitolämpö','Asuttu'];
function priceDisplayCurrency(){
  const c=state.status&&state.status.price_currency;
  return typeof c==='string'&&/^[A-Z]{3}$/.test(c)?c:'EUR';
}
function priceDisplayMinorUnit(){
  return ({SEK:'öre',NOK:'øre',DKK:'øre',PLN:'gr'})[priceDisplayCurrency()]||
    (uiLanguage==='en'?'c':'snt');
}
const priceFmt=v=>number(v)===null?'—':fmt(v*100,2)+' '+priceDisplayMinorUnit()+'/kWh';

// Illustrative electrical-consumption intensity: NOT the Finnish energy
// certificate E-value. Never annualize a lifetime meter total of unknown age.
function ownEnergyClass(annualKwh,heatedM2) {
  if(!Number.isFinite(annualKwh)||annualKwh<=0||
     !Number.isFinite(heatedM2)||heatedM2<=0)return null;
  const intensity=annualKwh/heatedM2;
  const limits=[60,100,150,200,250,350];
  const labels=['A','B','C','D','E','F','G'];
  let index=0;
  while(index<limits.length && intensity>=limits[index])++index;
  return {grade:labels[index],intensity};
}
function restoreEnergyForm() {
  try {
    const raw=localStorage.getItem('enevina_lite_own_energy_v1');
    if(raw) {
      const settings=JSON.parse(raw);
      if(typeof settings.area==='number'&&settings.area>0)
        $('energy-area').value=settings.area;
      if(typeof settings.annual==='number'&&settings.annual>0)
        $('energy-annual').value=settings.annual;
    }
  } catch(_) {} // Storage is optional, e.g. Safari private windows.
  showEnergyGrade();
}
function showEnergyGrade() {
  const calculated=ownEnergyClass(Number($('energy-annual').value),
                                 Number($('energy-area').value));
  text('energy-grade',calculated?calculated.grade:'—');
  text('energy-intensity',calculated?
      fmt(calculated.intensity,1)+' kWh/m² vuodessa':
      'Syötä todellinen 12 kuukauden kulutus');
  return calculated;
}
$('energy-calculate').addEventListener('click',()=>{
  const calculated=showEnergyGrade();
  if(!calculated) {
    text('energy-feedback','Syötä yli nollan oleva lämmitetty pinta-ala ja täyden vuoden kulutus.');
    return;
  }
  try {
    localStorage.setItem('enevina_lite_own_energy_v1',JSON.stringify({
      area:Number($('energy-area').value),
      annual:Number($('energy-annual').value)
    }));
    text('energy-feedback','Vertailuluokka tallennettu tähän selaimeen. Ei virallinen energiatodistus tai E-luku.');
  } catch(_) {
    text('energy-feedback','Vertailuluokka laskettu; selain ei sallinut paikallista tallennusta.');
  }
});
restoreEnergyForm();

function switchView(name){
  if(!['home','prices','weather','plan','settings'].includes(name))return;
  document.querySelectorAll('.view').forEach(v=>v.classList.toggle('active',v.id==='view-'+name));
  document.querySelectorAll('[data-view]').forEach(b=>{const active=b.dataset.view===name;b.classList.toggle('active',active);if(active)b.setAttribute('aria-current','page');else b.removeAttribute('aria-current');});
  history.replaceState(null,'','#'+name);
}
document.querySelectorAll('[data-view]').forEach(b=>b.addEventListener('click',()=>switchView(b.dataset.view)));
window.addEventListener('hashchange',()=>switchView(location.hash.slice(1)));
$('go-prices').addEventListener('click',()=>switchView('prices'));
$('go-weather').addEventListener('click',()=>switchView('weather'));
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
  [0,Math.floor((list.length-1)/3),Math.floor(2*(list.length-1)/3),list.length-1].forEach(i=>{let t=document.createElement('span');t.textContent=new Date(list[i].utc*1000).toLocaleTimeString(uiLocale(),{hour:'2-digit',minute:'2-digit',timeZone:regionTimeZone()});ticks.append(t);});
  box.append(wrap,ticks);
  const caption=document.createElement('div');caption.className='legend';caption.textContent='Vaihteluväli: '+priceFmt(lo)+' – '+priceFmt(hi);box.append(caption);
}
const otaStates={
  0:'Ei uutta hyväksyttyä versiota',
  1:'Päivityskanava pois käytöstä',
  2:'Odottaa Wi-Fi-yhteyttä tai kellonaikaa',
  3:'Tilapäinen resurssi- tai tallennusvirhe',
  4:'Julkaisun tarkistus tai lataus epäonnistui',
  5:'Allekirjoitettu päivitys latautuu',
  6:'Päivitys valmis, laite käynnistyy uudelleen'
};
function showFirmwareUpdate(s){
  const available=s.ota_signed_pull_enabled===true;
  const active=s.ota_network_maintenance===true||s.ota_signed_status===5||s.ota_signed_status===6;
  const token=s.air&&s.air.config_token;
  text('ota-current',s.firmware_version||'—');
  text('ota-good',Number(s.ota_signed_good_version||0)>0?String(s.ota_signed_good_version):'Ei vielä vahvistettua julkaisua');
  text('ota-candidate',Number(s.ota_signed_candidate||0)>0?String(s.ota_signed_candidate):'Ei havaintoa');
  text('ota-last-check',Number(s.ota_last_check_utc||0)>0?time(s.ota_last_check_utc):'Ei vielä tarkistettu');
  text('ota-stage',s.ota_dashboard_pending?'Tarkistus jonossa':otaStates[s.ota_signed_status]||'Tuntematon tila');
  text('ota-downloaded',Number(s.ota_downloaded_bytes||0)>0?fmt(s.ota_downloaded_bytes/1024,0)+' KiB':'—');
  $('ota-install').disabled=!available||!token||active||s.ota_dashboard_pending===true;
  text('ota-feedback',available?'Päivitys käyttää vain allekirjoitettua julkaisua ja säilyttää edellisen toimivan OTA-version.':'Allekirjoitettua OTA-päivityskanavaa ei ole tällä laitteella. Päivitystä ei voi käynnistää ennen sen käyttöönottoa.');
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
  text('price-spot-unit',priceDisplayMinorUnit()+'/kWh, alv 0 %');
  text('price-spot-heading','Spot '+priceDisplayMinorUnit()+'/kWh');
  text('price-total-heading','Kokonaishinta '+priceDisplayMinorUnit()+'/kWh');
  text('price-area-chip','Nord Pool · '+(s.price_area||'—')+' · '+priceDisplayCurrency());
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
  $('control-enable').disabled=s.control_armed===true || !a.config_token || (state.site&&state.site.pump_adapter==='midea_lan');
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
  showFirmwareUpdate(s);
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
  if(!state.alertSettingsLoaded && !state.alertSettingsDirty &&
     !state.alertSettingsSaving && a.config_token) {
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
function showWeather() {
  const w=state.weather||{},slots=w.valid===true&&Array.isArray(w.slots)?
    w.slots.filter(p=>p&&Number.isFinite(p.utc)&&p.utc>1700000000 &&
       Number.isFinite(p.temperature_c)&&p.temperature_c>=-60&&
       p.temperature_c<=60).slice(0,48):[];
  const valid=slots.length>0;
  const upcoming=slots.filter(p=>p.utc<=Date.now()/1000+24*3600);
  text('weather-next',valid?fmt(slots[0].temperature_c,1)+' °C':'—');
  text('weather-next-time',valid?time(slots[0].utc):'Kelvollista lämpötilaennustetta ei ole saatavilla');
  text('weather-min',upcoming.length?fmt(Math.min(...upcoming.map(p=>p.temperature_c)),1)+' °C':'—');
  text('weather-max',upcoming.length?fmt(Math.max(...upcoming.map(p=>p.temperature_c)),1)+' °C':'—');
  text('weather-updated',valid?'MET Norwayn ennuste haettu: '+time(w.fetched_utc):
      'Kelvollista MET Norwayn ennustetta ei ole saatavilla.');
  text('home-weather-summary',valid?
    'Seuraava ulkolämpötila '+fmt(slots[0].temperature_c,1)+' °C klo '+
      new Date(slots[0].utc*1000).toLocaleTimeString(uiLocale(),{
       timeZone:regionTimeZone(),hour:'2-digit',minute:'2-digit'})+
      '. '+slots.length+' ennustetuntia saatavilla.':
    'Ennustetta ei ole saatavilla. Lämmityksen ohjaus ei käytä keksittyjä sääarvoja.');
  const box=$('weather-chart');box.replaceChildren();
  if(slots.length<2) {
    const empty=document.createElement('div');empty.className='empty';
    empty.textContent='Lämpötilan käyrään tarvitaan vähintään kaksi tuntipistettä.';
    box.append(empty);
  } else {
    const data=slots,temperatures=data.map(p=>p.temperature_c);
    const lo=Math.floor(Math.min(...temperatures)-1),
          hi=Math.ceil(Math.max(...temperatures)+1),
          span=Math.max(2,hi-lo);
    const first=data[0].utc,last=data.at(-1).utc;
    const svg=document.createElementNS('http://www.w3.org/2000/svg','svg');
    svg.setAttribute('viewBox','0 0 640 180');
    svg.setAttribute('class','weather-line');
    svg.setAttribute('role','img');
    svg.setAttribute('aria-label','MET Norwayn ulkolämpötilaennusteen kuvaaja');
    for(const val of [lo,(lo+hi)/2,hi]) {
      const y=157-(val-lo)/span*142;
      const line=document.createElementNS('http://www.w3.org/2000/svg','line');
      line.setAttribute('x1','10');line.setAttribute('x2','630');
      line.setAttribute('y1',String(y));line.setAttribute('y2',String(y));
      line.setAttribute('class','axis');svg.append(line);
    }
    const poly=document.createElementNS('http://www.w3.org/2000/svg','polyline');
    poly.setAttribute('points',data.map(p=>{
      const x=10+(p.utc-first)/Math.max(1,last-first)*620;
      const y=157-(p.temperature_c-lo)/span*142;
      return x.toFixed(1)+','+y.toFixed(1);
    }).join(' '));
    poly.setAttribute('class','curve');svg.append(poly);
    box.append(svg);
    const ticks=document.createElement('div');ticks.className='weather-ticks';
    [data[0],data[Math.floor((data.length-1)/2)],data.at(-1)].forEach(p=>{
      const span=document.createElement('span');
      span.textContent=time(p.utc);
      ticks.append(span);
    });box.append(ticks);
  }
  const body=$('weather-rows');body.replaceChildren();
  if(!valid){
    const row=body.insertRow();row.insertCell().colSpan=2;
    row.cells[0].textContent='Ennustetunteja ei ole saatavilla.';
    return;
  }
  slots.filter((_,i)=>i%3===0).slice(0,16).forEach(p=>{
    const row=body.insertRow();
    row.insertCell().textContent=time(p.utc);
    row.insertCell().textContent=fmt(p.temperature_c,1)+' °C';
  });
}
function showPlan(){
  const p=state.plan||{},s=state.status||{},ok=p.valid===true&&Array.isArray(p.slots)&&p.slots.length>0;
  const bootstrap=s.bootstrap_action;
  const proposed=bootstrap==='HEAT_SETPOINT'&&number(s.bootstrap_heat_setpoint_c)!==null?
    'HEAT '+fmt(s.bootstrap_heat_setpoint_c,1)+' °C':
    bootstrap==='OFF'?'OFF':bootstrap==='HOLD'?'Pidä nykyinen tila':null;
  const hasFallback=!ok&&proposed!==null;
  const priced=s.bootstrap_price_data_valid===true;
  const policyLabel=priced?'Hintaohjauksen ehdotus':'Huonetermostaatin ehdotus';
  text('home-plan-state',ok?'MPC-laskelma':hasFallback?policyLabel:'Ei hyväksyttyä suunnitelmaa');
  text('plan-status',ok?String(p.count)+' jaksoa':hasFallback?(priced?'Hintaohjaus':'Termostaattiohjaus'):'Ei käytössä');
  text('plan-time',ok?'Laskettu '+time(s.optimized_utc):'Ei kelvollista laskelmaa');
  text('plan-cost',ok&&number(p.cost_eur)!==null?fmt(p.cost_eur,2)+' '+priceDisplayCurrency():'—');
  text('plan-min',ok&&number(p.minimum_c)!==null?fmt(p.minimum_c,1)+' °C':'—');
  text('home-plan-next',ok?'Seuraava jakso '+time(p.slots[0].utc)+': '+(p.slots[0].heat?'HEAT 16 °C':'OFF'):
    hasFallback?'Lähtötason ehdotus: '+proposed:'Hyväksytty optimointisuunnitelma puuttuu.');
  text('home-plan-reason',ok?'Simuloitu MPC-ehdotus. Automaattiohjaus toimii erillisellä hintaohjauspolulla.':
    hasFallback?(priced?'Ruuvi + 15 min kokonaishinnat; ':'Hintadata puuttuu: Ruuvi-huonetermostaatti; ')+
      'huonetavoite '+fmt(s.bootstrap_room_target_c,1)+' °C; syy: '+(s.bootstrap_reason||'—')+
      '. CN105-ohjaus edellyttää käyttäjän valintaa.':
    'Ehdotus estetty: '+(s.bootstrap_reason||'puutteelliset lähtötiedot')+'.');
  text('plan-warning',ok?'Ehdotukset ovat laskennallisia. Fyysiset ohjauskäskyt ovat estettyjä.':
    hasFallback?(priced?'Lähtötason hintaohjaus: ':'Hintojen puuttuessa termostaatti: ')+
       proposed+' (ehdotus; automaattiohjauksen käynnistys valitaan Asetuksissa).':
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
$('ota-refresh').addEventListener('click',()=>reloadStatus());
$('ota-install').addEventListener('click',async()=>{
  const s=state.status||{},token=s.air&&s.air.config_token;
  if(!token||s.ota_signed_pull_enabled!==true)return;
  if(!window.confirm(translateFi('Tarkistetaanko allekirjoitettu firmware ja asennetaanko uusi versio, jos sellainen löytyy? Laite voi käynnistyä uudelleen.')))return;
  $('ota-install').disabled=true;
  text('ota-feedback','Päivitystarkistus lähetetään laitteelle…');
  try{
    const r=await fetch('/api/ota/install',{method:'POST',
      headers:{'Content-Type':'application/x-www-form-urlencoded'},
      body:new URLSearchParams({token})});
    const j=await r.json().catch(()=>({}));
    if(!r.ok||j.queued!==true)throw Error('HTTP '+r.status);
    text('ota-feedback','Tarkistus käynnistetty. Uusi allekirjoitettu julkaisu asennetaan, jos se on saatavilla.');
    await reloadStatus();
  }catch(e){text('ota-feedback','Päivitystarkistuksen käynnistys epäonnistui: '+e.message);}
  finally{$('ota-install').disabled=false;}
});
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
async function reloadWeather(){try{state.weather=await fetchJSON('/api/weather');showWeather();}catch(e){state.weather={valid:false,slots:[]};showWeather();}}
async function reloadHistory(){try{state.history=await fetchJSON('/api/air/history');showHistory();}catch(e){state.history={hours:[]};showHistory();}}
async function setControlArmed(enabled) {
  if(enabled && state.site&&state.site.pump_adapter==='midea_lan'){text('control-feedback','Midean automaattinen optimointiohjaus ei ole vielä käytössä; manuaalinen ohjaus löytyy Midea-asetuksista.');return;}
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
['alert-rh70','alert-rh80','alert-battery'].forEach(id=>
  $(id).addEventListener('input',()=>{
    state.alertSettingsDirty=true;
    ++state.alertSettingsRevision;
  }));
$('alerts-save').addEventListener('click',async()=>{
  if(state.alertSettingsSaving)return;
  const rh70=Number($('alert-rh70').value),
    rh80=Number($('alert-rh80').value),
    battery=Number($('alert-battery').value);
  if(!Number.isFinite(rh70)||rh70<55||rh70>80 ||
     !Number.isFinite(rh80)||rh80<75||rh80>95||rh80<rh70+5||
     !Number.isFinite(battery)||battery<2||battery>3.2){
    text('alerts-feedback','Tarkista varoitusarvot');return;
  }
  const revision=state.alertSettingsRevision;
  state.alertSettingsSaving=true;
  $('alerts-save').disabled=true;
  try {
    await airPostLocal('/api/air/thresholds',{rh70:String(rh70),
      rh80:String(rh80),battery:String(battery)});
    if(state.alertSettingsRevision===revision){
      state.alertSettingsDirty=false;
      state.alertSettingsLoaded=false;
      text('alerts-feedback','Varoitusrajat tallennettu.');
    } else {
      // A new edit made while POST was in flight must remain in the form.
      state.alertSettingsDirty=true;
      text('alerts-feedback','Aiemmat varoitusrajat tallennettu. Uudet muutokset odottavat tallennusta.');
    }
    await reloadStatus();
  }catch(e){text('alerts-feedback',e.message);}
  finally{
    state.alertSettingsSaving=false;
    $('alerts-save').disabled=false;
  }
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
const siteErrors={BAD_NUMBER:'Tarkista numero',OUT_OF_RANGE:'Arvo on sallitun alueen ulkopuolella',BAD_URL:'Osoite ei kelpaa (vain paikallinen http-osoite)',TOO_LONG:'Liian pitkä arvo',BAD_CHOICE:'Virheellinen valinta',ADAPTER_NOT_SUPPORTED:'Laitetta ei vielä tueta',INVALID:'Asetukset eivät ole kelvolliset',NVS:'Tallennus laitteen muistiin epäonnistui',TOKEN:'Päivitä sivu ja yritä uudelleen'};
const maturityText={FIELD_PILOT:'Kenttäpilotti',HOST_TESTED_ONLY:'Vain PC-testattu',FIRMWARE_READ_ONLY:'Lukuyhteys (testivaihe)',FIRMWARE_CONTROL_EXPERIMENTAL:'Kokeellinen manuaaliohjaus',DOCUMENTED_ONLY:'Vain selvitetty'};
async function reloadSite(){try{const s=await fetchJSON('/api/settings');state.site=s;const t=s.tariff;
  if(state.status)showStatus();if(state.prices)showPrices();if(state.weather)showWeather();
 const put=(id,v)=>{if(document.activeElement!==$(id))$(id).value=v};
 const c=v=>(Math.round(v*1000)/1000).toString().replace('.',',');
 put('site-margin',c(t.margin_c_per_kwh));put('site-tday',c(t.transfer_day_c_per_kwh));put('site-tnight',c(t.transfer_night_c_per_kwh));
 put('site-tax',c(t.electricity_tax_c_per_kwh));put('site-vat',c(t.vat_percent));put('site-kind',t.transfer_kind);
 put('site-dstart',t.day_start_hour);put('site-nstart',t.night_start_hour);put('site-incl',t.components_include_vat?'true':'false');
 put('site-adv',c(s.min_price_advantage_percent));put('site-cn105',s.cn105_url);put('site-shelly',s.shelly_url);put('site-fmi',s.fmi_latlon);
 const sel=$('site-adapter');sel.replaceChildren();const none=document.createElement('option');none.value='';none.textContent='Ei valittu';sel.append(none);
 const rows=$('adapter-rows');rows.replaceChildren();
 for(const a of s.adapters||[]){if(a.selectable){const o=document.createElement('option');o.value=a.id;o.textContent=a.manufacturer+' · '+a.models;sel.append(o);}
  const r=rows.insertRow();[a.manufacturer+' · '+a.models,a.link,maturityText[a.maturity]||a.maturity,a.measurements,a.commands].forEach(v=>{r.insertCell().textContent=v;});}
 sel.value=s.pump_adapter||'';$('midea-v3-card').hidden=s.pump_adapter!=='midea_lan';
 $('control-enable').disabled=s.pump_adapter==='midea_lan'||!state.status||!!state.status.control_armed;
 }catch(e){text('site-feedback','Asetuksia ei voitu lukea laitteelta.');}}
$('site-adapter').addEventListener('change',()=>{$('midea-v3-card').hidden=$('site-adapter').value!=='midea_lan';});
$('site-save').addEventListener('click',async()=>{const token=state.status&&state.status.air&&state.status.air.config_token;
 if(!token){text('site-feedback','Laite ei vastaa tai valtuutus puuttuu');return;}
 const fields={margin_c_per_kwh:$('site-margin').value,transfer_day_c_per_kwh:$('site-tday').value,transfer_night_c_per_kwh:$('site-tnight').value,
  electricity_tax_c_per_kwh:$('site-tax').value,vat_percent:$('site-vat').value,transfer_kind:$('site-kind').value,day_start_hour:$('site-dstart').value,
  night_start_hour:$('site-nstart').value,components_include_vat:$('site-incl').value,min_price_advantage_percent:$('site-adv').value,
  tariff_confirmed:'true',pump_adapter:$('site-adapter').value,cn105_url:$('site-cn105').value.trim(),shelly_url:$('site-shelly').value.trim(),fmi_latlon:$('site-fmi').value.trim()};
 $('site-save').disabled=true;
 try{const r=await fetch('/api/settings',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams({...fields,token})});
  const j=await r.json().catch(()=>({}));
  if(j.saved===true){text('site-feedback',j.control_disarmed?'Tallennettu. Lämpöpumpun yhteys vaihtui, joten automaattiohjaus poistettiin käytöstä: ota se uudelleen käyttöön uudelle laitteelle.':'Tallennettu. Uudet hinnat ja osoitteet ovat heti käytössä.');await reloadSite();await reloadPrices();await reloadStatus();}
  else text('site-feedback',(siteErrors[j.error]||'Tallennus epäonnistui')+(j.key?' ('+j.key+')':''));}
 catch(e){text('site-feedback','Laite ei vastannut.');}finally{$('site-save').disabled=false;}});
function showMideaProtocolFields(){
 const isV3=$('midea-protocol').value==='3';
 $('midea-v3-token-field').hidden=!isV3;
 $('midea-v3-key-field').hidden=!isV3;
}
$('midea-protocol').addEventListener('change',()=>{
 $('midea-protocol').dataset.dirty='true';
 showMideaProtocolFields();
});
const mideaControlErrors={not_requested:'Ohjausta ei vielä lähetetty.',control_not_confirmed:'Pumppu ei vahvistanut muutosta takaisinluvussa.',resource_busy:'Ohjauksen suorittamiseen ei ollut resursseja.',ok:'Pumpun ohjaus vahvistettu takaisinluvulla.'};
const mideaReadErrors={not_selected:'Valitse Midea lämpöpumpun yhteydeksi.',not_configured:'Midean asetukset puuttuvat.',not_tested:'Ei vielä testattu.',ok:'Midean tilanluku onnistui.',invalid_address:'Tarkista pumpun IP-osoite.',invalid_credentials:'Midean tunnistetiedot ovat virheelliset.',connection_failed:'Ei TCP-yhteyttä Mideaan (portti 6444).',authentication_failed:'V3-tunnistautuminen epäonnistui: tarkista token ja key.',protocol_failed:'Midean V3-protokollavastaus ei kelpaa.',unsupported_state:'Pumpun tilavastausta ei voitu tulkita.',time_not_ready:'Laitteen kellonaika ei ole vielä synkronoitunut.'};
async function reloadMidea(){try{const m=await fetchJSON('/api/midea/status');
 if(!$('midea-protocol').dataset.dirty &&
    (m.protocol===2||m.protocol===3)) {
   $('midea-protocol').value=String(m.protocol);
   showMideaProtocolFields();
 }
 const reason=mideaReadErrors[m.read_code]||'Tuntematon Midea-yhteysvirhe.';
 const last=m.last_attempt_utc?new Date(m.last_attempt_utc*1000).toLocaleTimeString(uiLocale(),{timeZone:regionTimeZone(),hour:'2-digit',minute:'2-digit',second:'2-digit'}):'—';
 text('midea-diagnostic',(m.polling?'Yhteystesti käynnissä. ':reason)+' Viimeisin yritys: '+last);
 text('midea-state',!m.selected?'Midea ei valittu':!m.configured?'Ei asetuksia':m.fresh?'Paikallinen tilanluku toimii':'Ei tuoretta mittausta');
 text('midea-read',m.fresh?(m.protocol===2?'V2-yhteys onnistui':'V3-yhteys onnistui'):m.configured?'Yhteyttä ei ole':'Midea-yhteyttä ei ole asetettu');
 text('midea-mode',m.fresh?(m.power?(m.mode===4?'Lämmitys':'Tila '+m.mode):'Pois päältä'):'—');
 text('midea-target',m.fresh?fmt(m.target_c)+' °C':'—');
 text('midea-indoor',m.fresh&&m.indoor_available?fmt(m.indoor_c)+' °C':'—');
 text('midea-outdoor',m.fresh&&m.outdoor_available?fmt(m.outdoor_c)+' °C':'—');
 const canCommand=m.selected && m.configured && m.fresh && m.pilot_approved && !m.control_busy && !m.control_pending && !!(state.status&&state.status.air&&state.status.air.config_token);
 $('midea-command-send').disabled=!canCommand;
 text('midea-pilot-feedback',m.pilot_approved?
  'Mallikohtainen ohjaustesti hyväksytty. Pyyntirajat: '+m.pilot_min_c+'…'+m.pilot_max_c+' °C.':
  'Mallikohtaista ohjaustestiä ei ole hyväksytty.');
 const status=m.control_pending?'Ohjaus odottaa suorittamista.':m.control_busy?'Ohjausta suoritetaan.':mideaControlErrors[m.control_code]||mideaReadErrors[m.control_code]||'Ohjausvirhe: '+(m.control_code||'tuntematon');
 const verified=m.control_last_confirmed_utc?' Vahvistettu '+time(m.control_last_confirmed_utc):'';
 text('midea-control-feedback',status+verified);
 }catch(e){text('midea-state','Yhteys poikki');['midea-read','midea-mode','midea-target','midea-indoor','midea-outdoor'].forEach(id=>text(id,'—'));}}
$('midea-test').addEventListener('click',async()=> {
 const token=state.status&&state.status.air&&state.status.air.config_token;
 if(!token){text('midea-feedback','Laite ei vastaa.');return;}
 $('midea-test').disabled=true;
 try{
   const r=await fetch('/api/midea/test',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams({token})});
   const j=await r.json().catch(()=>({}));
   text('midea-feedback',j.queued?'Yhteystesti käynnistetty. Pumppua ei ohjata.':'Yhteystestiä ei voitu käynnistää.');
   await reloadMidea();
 }catch(_){text('midea-feedback','Laite ei vastannut.');}
 finally{$('midea-test').disabled=false;}
});
async function setMideaPilot(approve){
 const token=state.status&&state.status.air&&state.status.air.config_token;
 if(!token){text('midea-pilot-feedback','Asetustunniste puuttuu.');return;}
 const body=new URLSearchParams({token,approve:String(approve)});
 if(approve){
  const model=$('midea-pilot-model').value.trim();
  const min=$('midea-pilot-min').value.trim();
  const max=$('midea-pilot-max').value.trim();
  const id=$('midea-id').value.trim();
  if(!model || !id || !min || !max){text('midea-pilot-feedback','Tarkista malli, device ID ja mallikohtaiset lämpötilarajat.');return;}
  if(!confirm('Hyväksytkö Midea '+model+' -pumpun manuaalisen ohjauskokeen tälle laitteelle?'))return;
  body.set('model',model);body.set('device_id',id);
  body.set('min_c',min);body.set('max_c',max);
 }
 $('midea-pilot-enable').disabled=true;
 $('midea-pilot-disable').disabled=true;
 try{
  const r=await fetch('/api/midea/pilot',{method:'POST',
   headers:{'Content-Type':'application/x-www-form-urlencoded'},body});
  const j=await r.json().catch(()=>({}));
  if(!r.ok)throw Error(j.error||String(r.status));
  text('midea-pilot-feedback',j.approved?'Mallin manuaaliohjaus hyväksytty.':'Koekäyttöhyväksyntä peruttu.');
  await reloadMidea();
 }catch(e){text('midea-pilot-feedback','Koekäytön asetusta ei voitu muuttaa: '+(e.message||'yhteysvirhe'));}
 finally{
  $('midea-pilot-enable').disabled=false;
  $('midea-pilot-disable').disabled=false;
 }
}
$('midea-pilot-enable').addEventListener('click',()=>setMideaPilot(true));
$('midea-pilot-disable').addEventListener('click',()=>setMideaPilot(false));
$('midea-command-send').addEventListener('click',async()=>{
 const token=state.status&&state.status.air&&state.status.air.config_token;
 if(!token){text('midea-control-feedback','Asetustunniste puuttuu.');return;}
 const body=new URLSearchParams({token});
 const select=(id,name)=>{const value=$(id).value;if(value!=='')body.set(name,value);};
 select('midea-command-power','power');
 select('midea-command-mode','mode');
 select('midea-command-fan','fan');
 select('midea-command-swing','swing');
 const temp=$('midea-command-temperature').value.trim();
 if(temp!=='')body.set('target_c',temp);
 if(Array.from(body.keys()).length<2){text('midea-control-feedback','Valitse ensin vähintään yksi muutettava asetus.');return;}
 $('midea-command-send').disabled=true;
 text('midea-control-feedback','Lähetetään manuaalinen Midea-ohjaus…');
 try{
  const r=await fetch('/api/midea/control',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body});
  const j=await r.json().catch(()=>({}));
  if(!r.ok || !j.queued)throw Error(j.error||String(r.status));
  text('midea-control-feedback','Ohjaus jonossa. Pumpun vastaus ja takaisinluku tarkistetaan.');
  ['midea-command-power','midea-command-mode','midea-command-fan','midea-command-swing'].forEach(id=>$(id).value='');
  $('midea-command-temperature').value='';
  await reloadMidea();
 }catch(e){text('midea-control-feedback','Ohjausta ei voitu käynnistää: '+(e.message||'yhteysvirhe'));}
});
$('midea-save').addEventListener('click',async()=>{
 const token=state.status&&state.status.air&&state.status.air.config_token;
 if(!token){text('midea-feedback','Laite ei vastaa.');return;}
 const protocol=$('midea-protocol').value;
 const fields={host:$('midea-ip').value.trim(),device_id:$('midea-id').value.trim(),
   protocol,v3_token:protocol==='3' ? $('midea-token').value.trim() : '',
   v3_key:protocol==='3' ? $('midea-key').value.trim() : '',token};
 if(!/^[0-9]+$/.test(fields.device_id)||
    (protocol==='3'&&(!/^[0-9a-f]{128}$/i.test(fields.v3_token)||
      !/^[0-9a-f]{64}$/i.test(fields.v3_key)))){
   text('midea-feedback',protocol==='2'?
     'Tarkista Midean device ID. V2 ei tarvitse tokenia eikä keytä.':
     'Tarkista device ID, V3 token (128) ja key (64 heksamerkkiä).');return;
 }
 $('midea-save').disabled=true;
 try {const r=await fetch('/api/midea/config',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(fields)});
  const saved=await r.json().catch(()=>({}));
  text('midea-feedback',saved.saved?'Midea-yhteys tallennettu. Ensimmäinen luku tapahtuu verkossa.':'Tallennus ei onnistunut ('+(saved.error||r.status)+').');
  if(saved.saved){
    delete $('midea-protocol').dataset.dirty;
    $('midea-token').value='';$('midea-key').value='';
    await reloadMidea();
  }}
 catch(e){text('midea-feedback','Laite ei vastannut.');}
 finally {$('midea-save').disabled=false;}
});
async function initialRefresh(){await reloadStatus();await reloadPrices();await reloadWeather();await reloadPlan();await reloadHistory();await reloadSite();await reloadMidea();}
translateDashboard();
initialRefresh();
setInterval(reloadStatus,15000);
setInterval(reloadMidea,30000);
setInterval(async()=>{await reloadPrices();await reloadWeather();await reloadPlan();},60000);
setInterval(reloadHistory,180000);
</script>
</body>
</html>)ENEVINA_UI";