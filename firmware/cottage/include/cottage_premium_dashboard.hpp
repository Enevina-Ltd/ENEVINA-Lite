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
        timeZone:(function(){try{return localStorage.getItem('enevina_tz')||'Europe/Helsinki';}catch(_){return 'Europe/Helsinki';}})(),hour:'2-digit',hourCycle:'h23'
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

.wz{position:fixed;inset:0;z-index:50;background:rgba(10,24,19,.55);display:flex;align-items:flex-start;justify-content:center;overflow:auto;padding:16px}
.wz[hidden]{display:none}body.wz-open{overflow:hidden}
.wz-card{background:var(--paper);color:var(--ink);border:1px solid var(--edge);border-radius:18px;box-shadow:var(--shadow);width:100%;max-width:560px;padding:18px 18px 16px;margin:auto}
.wz-head{display:flex;align-items:center;gap:10px;justify-content:space-between}.wz-x{border:0;background:transparent;color:var(--muted);font-size:26px;line-height:1;min-width:44px;min-height:44px;border-radius:10px;cursor:pointer}
.wz-barwrap{height:5px;background:var(--mint);border-radius:9px;margin:10px 0 14px;overflow:hidden}.wz-bar{height:100%;width:0;background:var(--green);transition:width .25s}
.wz-body h2{margin:.1rem 0 .5rem;font-size:1.25rem;outline:none}.wz-body p{margin:.3rem 0}
.wz-label{display:block;font-size:13px;font-weight:650;margin:.8rem 0 .3rem}
.wz-body input,.wz-body select{width:100%;font:inherit;min-height:46px;padding:10px 12px;border-radius:11px;border:1px solid #c5d2c8;background:var(--bg);color:var(--ink)}
.wz-row{display:flex;gap:8px;margin:.5rem 0}.wz-list{display:grid;gap:8px;margin:.6rem 0}
.wz-choice,.wz-result{font:inherit;text-align:left;min-height:48px;padding:11px 13px;border-radius:12px;border:1px solid #c5d2c8;background:var(--bg);color:var(--ink);cursor:pointer}
.wz-row .wz-choice{flex:1;text-align:center}.wz-choice.sel{border-color:var(--green);background:var(--mint);font-weight:650;box-shadow:inset 0 0 0 1px var(--green)}
.wz-results{display:grid;gap:6px;margin-top:8px}.wz-result:hover,.wz-result:focus-visible,.wz-choice:focus-visible{outline:2px solid var(--green);outline-offset:1px}
.wz-link{border:0;background:transparent;color:var(--green);text-decoration:underline;font:inherit;padding:12px 0;cursor:pointer;min-height:44px}
.wz-picked{background:var(--mint);border-radius:10px;padding:9px 11px;font-size:14px}
.wz-msg{min-height:1.3em;margin:.7rem 0 0;font-size:14px;color:var(--muted)}.wz-msg.bad{color:var(--red)}.wz-msg.ok{color:var(--green);font-weight:650}
.wz-foot{display:flex;gap:8px;flex-wrap:wrap;margin-top:12px}.wz-foot button{min-height:48px;border-radius:12px;font:inherit;font-weight:650;padding:0 16px;cursor:pointer}
.wz-foot .primary{margin-left:auto;flex:1 1 150px}.wz-foot .wz-sec{border:1px solid #c5d2c8;background:transparent;color:var(--ink)}.wz-foot button[hidden]{display:none}
.wz-sum{list-style:none;margin:.6rem 0;padding:0;display:grid;gap:8px}.wz-sum li{display:flex;gap:10px;align-items:flex-start;padding:10px 12px;border-radius:12px;background:var(--bg);font-size:14px}
.wz-mark{flex:none;width:22px;height:22px;border-radius:50%;display:inline-grid;place-items:center;font-weight:800;font-size:13px;background:#c5d2c8;color:#17352e}
.wz-ok .wz-mark{background:var(--green);color:#fff}.wz-warn .wz-mark{background:#e3b341;color:#3b2a00}.wz-todo .wz-mark{background:#d7a79f;color:#4a1710}
:root[data-theme="night"] .wz-card{background:#162a24}:root[data-theme="night"] .wz-choice,:root[data-theme="night"] .wz-result,:root[data-theme="night"] .wz-sum li,:root[data-theme="night"] .wz-body input,:root[data-theme="night"] .wz-body select{background:#0f1f1a;border-color:#2f4a40;color:#e3efe8}
:root[data-theme="night"] .wz-choice.sel{background:#1d4637;border-color:#5fc39a}:root[data-theme="night"] .wz-picked{background:#1d4637}:root[data-theme="night"] .wz-foot .wz-sec{border-color:#2f4a40;color:#e3efe8}
:root[data-theme="night"] .wz-msg.bad{color:#f08a76}:root[data-theme="night"] .wz-msg.ok,:root[data-theme="night"] .wz-link{color:#7fd6ad}:root[data-theme="night"] .wz-x{color:#b9cbc1}
@media(max-width:600px){.wz{padding:0}.wz-card{border-radius:0;min-height:100%;max-width:none}}
@media(prefers-reduced-motion:reduce){html{scroll-behavior:auto}}
</style>
</head>
<body>
<div id="wizard" class="wz" hidden role="dialog" aria-modal="true" aria-labelledby="wz-title">
<div class="wz-card">
  <div class="wz-head"><strong>Enevina Lite · käyttöönotto</strong><span id="wz-progress" class="muted tiny"></span><button type="button" id="wz-close" class="wz-x" aria-label="Sulje ja jatka myöhemmin">×</button></div>
  <div class="wz-barwrap"><div id="wz-bar" class="wz-bar"></div></div>
  <div id="wz-body" class="wz-body"></div>
  <p id="wz-msg" class="wz-msg" role="status" aria-live="polite"></p>
  <div class="wz-foot"><button type="button" id="wz-back" class="wz-sec">Takaisin</button><button type="button" id="wz-skip" class="wz-sec">Ohita tämä vaihe</button><button type="button" id="wz-next" class="primary">Jatka</button></div>
</div></div>
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
      <article class="card span12" id="wizard-card"><h2>Ohjattu käyttöönotto</h2><p class="tiny muted">Vaihda maa, sijainti, sähkömarkkina-alue tai lämpöpumppu vaihe vaiheelta. Nykyiset asetukset säilyvät, kunnes tallennat uuden valinnan.</p><button type="button" class="primary" id="wz-reopen">Avaa käyttöönotto uudelleen</button></article>
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
<script id="wizard-script">
// ---- First-start setup wizard (Wi-Fi is already done before this page loads) ----
// Every user-visible Finnish literal goes through WT(); tests/test_setup_wizard.js
// checks that each one has an English entry. Pure helpers (wz*) are unit tested
// in a VM; DOM rendering is exercised in a real browser.
const WT=s=>s;
const WZ={step:0,regions:null,country:'',language:'',zone:'',city:'',latlon:'',results:[],
  searchSeq:0,timer:null,busy:false,message:'',messageBad:false,
  tariff:{margin:'',tday:'',tnight:'',tax:'',vat:'',incl:'false'},adapter:'',cn105:'',shelly:'',
  manual:false,open:false,
  comfort:{target:'',min:'',max:'',profile:'balanced'}};
const WZ_STEPS=8;                 // region, city, zone, tariff, pump, comfort, devices, summary
const WZ_DONE=9;
const WZ_MINOR={EUR:'snt',SEK:'öre',NOK:'øre',DKK:'øre',PLN:'gr'};
function wzMinor(currency){return WZ_MINOR[currency]||'snt';}
function wzCountry(iso){return (WZ.regions||[]).find(c=>c.iso===iso)||null;}
function wzName(o,lang){return lang==='en'?o.name_en:o.name_fi;}
// 'Ruotsi – Tukholma (SE3)'; one-zone countries read 'Suomi (FI)'.
function wzZoneLabel(c,z,lang){
  return c.zones.length===1?wzName(c,lang)+' ('+z.id+')':
    wzName(c,lang)+' – '+wzName(z,lang)+' ('+z.id+')';
}
// "lat,lon" with '.' decimals only (a decimal comma would be ambiguous with the
// separator). Returns the canonical 4-decimal text or null.
function wzParseLatLon(v){
  const m=/^\s*(-?\d{1,2}(?:\.\d+)?)\s*[,;]\s*(-?\d{1,3}(?:\.\d+)?)\s*$/.exec(String(v||''));
  if(!m)return null;
  const lat=Number(m[1]),lon=Number(m[2]);
  if(!Number.isFinite(lat)||!Number.isFinite(lon)||Math.abs(lat)>90||Math.abs(lon)>180)return null;
  return lat.toFixed(4)+','+lon.toFixed(4);
}
function wzBlankTariff(){return {margin:'',tday:'',tnight:'',tax:'',vat:'',incl:'false'};}
function wzNum(v){
  const t=String(v==null?'':v).trim().replace(',','.');
  if(!/^\d+(\.\d+)?$/.test(t))return null;
  const n=Number(t);return Number.isFinite(n)?n:null;
}
// Contract amounts are entered in the minor unit of the zone's currency.
// Returns {ok:true,fields} or {ok:false,error}.
function wzTariffFields(v){
  const items=[['margin','margin_c_per_kwh'],['tday','transfer_day_c_per_kwh'],
    ['tnight','transfer_night_c_per_kwh'],['tax','electricity_tax_c_per_kwh'],['vat','vat_percent']];
  const fields={};
  for(const [k,key] of items){
    const n=wzNum(v[k]);
    if(n===null)return {ok:false,error:WT('Tarkista sopimuksen luvut: käytä vain numeroita.')};
    fields[key]=String(n);
  }
  if(Number(fields.vat_percent)>50)return {ok:false,error:WT('Arvonlisävero on enintään 50 %.')};
  fields.components_include_vat=v.incl==='true'?'true':'false';
  fields.tariff_confirmed='true';
  return {ok:true,fields};
}
const WZ_PRICE_ERRORS={1:WT('Hintapyyntö oli virheellinen.'),2:WT('Hintapalvelun vastaus ei ollut luettavissa.'),
  3:WT('Hintapalvelu palautti väärän alueen.'),4:WT('Hintapalvelu palautti väärän valuutan.'),
  5:WT('Hintapalvelun vastaus oli epätäydellinen.'),6:WT('Hintapalvelun vastaus sisälsi virheellisen arvon.'),
  7:WT('Hintapalvelun vastaus oli liian suuri.'),101:WT('Laitteen muisti ei riittänyt hintahakuun juuri nyt.'),
  102:WT('Laitteen muisti ei riittänyt hintahakuun juuri nyt.'),103:WT('Hintapalveluun ei saatu yhteyttä.'),
  104:WT('Hintapalvelu ei ole vielä julkaissut tämän päivän hintoja tälle alueelle.')};
// Honest checklist from live data only: ok / warn (needs attention) / todo.
function wzSummary(c){
  const site=c.site||{},region=site.region||{},st=c.status||{},air=st.air||{};
  const out=[];
  const add=(key,label,state,detail)=>out.push({key,label,state,detail});
  add('wifi',WT('Wi-Fi'),'ok',WT('Laite on kotiverkossa.'));
  const placed=!!site.fmi_latlon;
  const where=region.city||site.fmi_latlon;
  add('location',WT('Sijainti ja sääennuste'),
    placed&&c.weather&&c.weather.valid===true?'ok':placed?'warn':'todo',
    placed?(c.weather&&c.weather.valid===true?(region.city?region.city+' · ':'')+site.fmi_latlon:
      where+': '+WT('ennustetta ei ole vielä haettu.')):WT('Sijaintia ei ole valittu.'));
  let zoneState='todo',zoneDetail=WT('Sähkömarkkina-aluetta ei ole valittu.');
  if(region.price_region_resolved){
    const ok=st.spot!=null&&Number.isFinite(st.spot)&&!st.price_error;
    zoneState=ok?'ok':'warn';
    zoneDetail=region.nordpool_area+' · '+region.currency+(ok?'':' · '+(WZ_PRICE_ERRORS[st.price_error]||WT('Hintoja ei ole vielä haettu.')));
  }
  add('zone',WT('Sähkömarkkina-alue ja hinnat'),zoneState,zoneDetail);
  add('tariff',WT('Sähkösopimus'),region.tariff_confirmed?'ok':'warn',
    region.tariff_confirmed?WT('Sopimuksen hinnat on annettu.'):
      WT('Sopimustasi ei ole vahvistettu: kokonaishinta käyttää oletusarvoja.'));
  // CURRENT data only: source_verified is evaluated against the clock on the
  // device, and an observed SSE snapshot counts only while it is fresh (<= 180 s).
  // A bridge that delivered one snapshot and then went away is not "working".
  const verified=st.cn105_source_verified===true;
  const sseAge=typeof st.cn105_sse_climate_age_s==='number'&&Number.isFinite(st.cn105_sse_climate_age_s)?st.cn105_sse_climate_age_s:null;
  const observed=st.cn105_sse_climate_received===true&&sseAge!==null&&sseAge>=0&&sseAge<=180;
  let pumpState='todo',pumpDetail=WT('Lämpöpumppua ei ole valittu.');
  if(site.pump_adapter==='midea_lan'){
    pumpState='warn';pumpDetail=WT('Midean paikallinen yhteys tarkistetaan Asetukset-näkymän Midea-kortissa.');
  }else if(site.pump_adapter){
    if(verified){pumpState='ok';pumpDetail=WT('Yhteys toimii ja tietoa tulee.');}
    else if(observed){pumpState='warn';pumpDetail=WT('Tietoa tulee, mutta UART-lähdettä ei ole varmennettu.');}
    else{pumpState='warn';pumpDetail=WT('Valittu, mutta tuoretta tietoa ei tule.');}
  }
  add('pump',WT('Lämpöpumppu'),pumpState,pumpDetail);
  add('sensor',WT('Huonelämpötila-anturi'),air.sensor_fresh===true?'ok':'warn',
    air.sensor_fresh===true?WT('Tuore RuuviTag-mittaus.'):
      WT('Ei tuoretta anturia: optimointi ei käytä pumpun omaa lukemaa huonelämpötilana.'));
  add('meter',WT('Sähkönkulutusmittari (valinnainen)'),st.shelly_w!=null?'ok':'optional',
    st.shelly_w!=null?WT('Mittari antaa lukemia.'):WT('Ei mittaria. Voit lisätä sen myöhemmin.'));
  const opt=site.optimization||{};
  const profName=(WZ_PROFILES.find(p=>p.id===opt.profile)||{}).name;
  add('profile',WT('Optimointiprofiili'),'ok',(profName||WT('Tasapainoinen'))+
    (opt.comfort_min_c||opt.comfort_max_c?' · '+(opt.comfort_min_c||'–')+'…'+(opt.comfort_max_c||'–')+' °C':
      ' · '+WT('Ei omia lämpötilarajoja: käytetään tavoite ±0,5 °C.')));
  add('optimizer',WT('Optimoinnin valmius'),c.plan&&c.plan.valid===true?'ok':'warn',
    c.plan&&c.plan.valid===true?WT('Suunnitelma on laskettu.'):
      WT('Optimointi odottaa hintoja, ennustetta ja mittausta.'));
  return out;
}
function wzNeedsAction(list){return list.filter(x=>x.state==='todo'||x.state==='warn');}
function wzStartStep(site){
  const r=(site&&site.region)||{};
  const n=Number.isInteger(r.setup_step)?r.setup_step:0;
  return Math.max(0,Math.min(WZ_STEPS-1,n));
}
const WZ_GEO_ERRORS={BAD_QUERY:WT('Kirjoita vähintään kolme kirjainta.'),
  BUSY:WT('Haku on kesken. Odota hetki ja yritä uudelleen.'),
  OFFLINE:WT('Laite ei saanut yhteyttä karttapalveluun. Anna koordinaatit käsin.'),
  SERVER_ERROR:WT('Karttapalvelu ei vastannut oikein. Yritä myöhemmin tai anna koordinaatit käsin.'),
  BAD_RESPONSE:WT('Karttapalvelun vastaus ei kelvannut. Anna koordinaatit käsin.'),
  LOW_MEMORY:WT('Laitteen muisti ei riittänyt hakuun juuri nyt. Yritä hetken kuluttua.')};


// Comfort step. The target lives in the mode configuration (Ylläpito = hold,
// Asuttu = occupied); the limits and the profile live in the site settings.
const WZ_PROFILES=[
  {id:'comfort',name:WT('Mukavuus'),text:WT('Pitää lämpötilan tasaisena ja reagoi vain suuriin hintaeroihin.')},
  {id:'balanced',name:WT('Tasapainoinen'),text:WT('Käyttää osan sallitusta vaihteluvälistä: lämmittää hieman ennakkoon edullisella sähköllä ja venyttää kalliilla.')},
  {id:'saving',name:WT('Säästö'),text:WT('Käyttää sallitun vaihteluvälin tehokkaammin ja vertaa hintaa pidemmälle. Lämpötila vaihtelee enemmän, mutta pysyy asettamiesi rajojen sisällä.')}];
function wzHalfStep(v){return Math.abs(v*2-Math.round(v*2))<1e-9;}
// Target range follows the mode: Ylläpito 5..24, Asuttu 16..26 (device limits).
function wzTargetRange(mode){return mode===3?[16,26]:mode===2?[5,24]:null;}
// v: {target,min,max,profile} as typed; mode: current mode number.
// Returns {ok:true,target:number|null,fields} or {ok:false,error}.
function wzComfortFields(v,mode){
  const prof=WZ_PROFILES.find(p=>p.id===v.profile);
  if(!prof)return {ok:false,error:WT('Valitse optimointiprofiili.')};
  const range=wzTargetRange(mode);
  let target=null;
  if(String(v.target||'').trim()!==''){
    target=wzNum(v.target);
    if(!range)return {ok:false,error:WT('Valitse ensin lämmitystila (Ylläpitolämpö tai Asuttu) Asetuksista, jotta tavoitelämpötila voidaan asettaa.')};
    if(target===null||target<range[0]||target>range[1]||!wzHalfStep(target))
      return {ok:false,error:WT('Tavoitelämpötilan tulee olla ')+range[0]+'–'+range[1]+WT(' °C ja puolen asteen tarkkuudella.')};
  }
  const lim=k=>{
    const t=String(v[k]||'').trim();
    if(t==='')return 0;
    const n=wzNum(t);
    return n===null||n<5||n>26||!wzHalfStep(n)?NaN:n;
  };
  const lo=lim('min'),hi=lim('max');
  if(Number.isNaN(lo)||Number.isNaN(hi))
    return {ok:false,error:WT('Rajojen tulee olla 5–26 °C ja puolen asteen tarkkuudella.')};
  if(lo&&hi&&hi-lo<1)return {ok:false,error:WT('Pienimmän ja suurimman lämpötilan välissä tulee olla vähintään 1 °C.')};
  const t=target!==null?target:null;
  if(t!==null){
    if(lo&&lo>t-0.5)return {ok:false,error:WT('Pienimmän sallitun lämpötilan tulee olla vähintään 0,5 °C tavoitetta alempi.')};
    if(hi&&hi<t+0.5)return {ok:false,error:WT('Suurimman sallitun lämpötilan tulee olla vähintään 0,5 °C tavoitetta ylempi.')};
  }
  return {ok:true,target:t,fields:{optimization_profile:v.profile,
    comfort_min_c:lo?String(lo):'',comfort_max_c:hi?String(hi):''}};
}

// ---- DOM helpers -----------------------------------------------------------
function wzEl(tag,props,...kids){
  const e=document.createElement(tag);
  for(const [k,v] of Object.entries(props||{})){
    if(k==='class')e.className=v;else if(k==='on')for(const [ev,fn] of Object.entries(v))e.addEventListener(ev,fn);
    else if(v===true)e.setAttribute(k,'');else if(v!==false&&v!=null)e.setAttribute(k,String(v));
  }
  for(const kid of kids)e.append(kid&&kid.nodeType?kid:document.createTextNode(kid==null?'':String(kid)));
  return e;
}
function wzLang(){return typeof uiLanguage==='string'?uiLanguage:'fi';}
function wzCurrency(){const c=wzCountry(WZ.country);return c?c.currency:'EUR';}
function wzSay(message,bad){WZ.message=message||'';WZ.messageBad=!!bad;const m=document.getElementById('wz-msg');
  if(m){m.textContent=WZ.message;m.className='wz-msg'+(bad?' bad':'');}}
async function wzPost(fields){
  const token=state.status&&state.status.air&&state.status.air.config_token;
  if(!token)throw Error(WT('Laite ei vastaa tai valtuutus puuttuu. Päivitä sivu.'));
  const r=await fetch('/api/settings',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},
    body:new URLSearchParams({...fields,token})});
  const j=await r.json().catch(()=>({}));
  if(j.saved!==true)throw Error((siteErrors[j.error]?WT(siteErrors[j.error]):WT('Tallennus epäonnistui'))+(j.key?' ('+j.key+')':''));
  await reloadSite();
}
async function wzLoadRegions(){
  if(WZ.regions)return;
  const r=await fetch('/api/regions',{cache:'no-store'});
  if(!r.ok)throw Error('HTTP '+r.status);
  WZ.regions=(await r.json()).countries||[];
}

// ---- Steps -----------------------------------------------------------------
function wzStepRegion(body){
  body.append(wzEl('h2',{id:'wz-title'},WT('Kieli ja maa')),
    wzEl('p',{class:'muted'},WT('Valitse käyttökieli ja maa, jossa lämpöpumppu sijaitsee. Maa määrää sähkömarkkina-alueet ja valuutan.')));
  const lang=wzEl('div',{class:'wz-row',role:'group','aria-label':WT('Käyttöliittymän kieli')});
  for(const [code,label] of [['fi','Suomi'],['en','English']]){
    lang.append(wzEl('button',{type:'button','data-wz-lang':code,class:'wz-choice'+(wzLang()===code?' sel':''),
      'aria-pressed':String(wzLang()===code),on:{click:()=>{WZ.language=code;setDashboardLanguage(code);wzRender();}}},label));
  }
  const sel=wzEl('select',{id:'wz-country','aria-label':WT('Maa')},wzEl('option',{value:''},WT('Valitse maa…')));
  for(const c of (WZ.regions||[]).slice().sort((a,b)=>wzName(a,wzLang()).localeCompare(wzName(b,wzLang()),wzLang())))
    sel.append(wzEl('option',{value:c.iso,selected:c.iso===WZ.country},wzName(c,wzLang())));
  sel.addEventListener('change',()=>{
    // Amounts typed for another country are in another currency: never carry them over.
    if(sel.value!==WZ.country)WZ.tariff=wzBlankTariff();
    WZ.country=sel.value;WZ.zone='';wzSay('');wzRender();});
  body.append(lang,wzEl('label',{class:'wz-label','for':'wz-country'},WT('Maa')),sel);
  const c=wzCountry(WZ.country);
  if(c)body.append(wzEl('p',{class:'muted tiny'},WT('Valuutta: ')+c.currency+' · '+c.zones.length+' '+
    (c.zones.length===1?WT('sähkömarkkina-alue'):WT('sähkömarkkina-aluetta'))));
  body.append(wzEl('p',{class:'muted tiny'},WT('Maata ei näy listassa? Hintatietoa ei ole vielä saatavilla sieltä; muut toiminnot voi ottaa käyttöön ilman sitä.')));
}
async function wzSaveRegion(){
  if(!WZ.country)throw Error(WT('Valitse maa.'));
  const fields={country:WZ.country,setup_step:'1'};
  if(WZ.language)fields.language=WZ.language;
  await wzPost(fields);
}
function wzStepCity(body){
  body.append(wzEl('h2',{id:'wz-title'},WT('Sijainti')),
    wzEl('p',{class:'muted'},WT('Sääennuste haetaan sijainnillesi. Kirjoita kaupunki tai kunta ja valitse ehdotuksista.')));
  const input=wzEl('input',{id:'wz-city',type:'search',autocomplete:'off',spellcheck:'false',maxlength:'40',
    placeholder:WT('Esim. Kangasala'),'aria-label':WT('Kaupunki tai kunta'),value:WZ.query||''});
  input.addEventListener('input',()=>{WZ.query=input.value;wzScheduleSearch();});
  body.append(wzEl('label',{class:'wz-label','for':'wz-city'},WT('Kaupunki tai kunta')),input,
    wzEl('div',{id:'wz-results',class:'wz-results',role:'listbox','aria-label':WT('Hakutulokset')}));
  wzRenderResults();
  if(WZ.latlon)body.append(wzEl('p',{class:'wz-picked'},WT('Valittu: ')+(WZ.city||'')+' · '+WZ.latlon));
  const manual=wzEl('button',{type:'button',class:'wz-link',on:{click:()=>{WZ.manual=!WZ.manual;wzRender();}}},
    WT('Anna koordinaatit käsin'));
  body.append(manual);
  if(WZ.manual){
    const ll=wzEl('input',{id:'wz-latlon',autocomplete:'off',inputmode:'text',placeholder:WT('Leveys, pituus (esim. 61.4638, 24.0760)'),
      'aria-label':WT('Koordinaatit'),value:WZ.latlon||''});
    ll.addEventListener('input',()=>{const p=wzParseLatLon(ll.value);WZ.latlon=p||'';WZ.city='';});
    body.append(wzEl('label',{class:'wz-label','for':'wz-latlon'},WT('Koordinaatit')),ll,
      wzEl('p',{class:'muted tiny'},WT('Käytä pisteitä desimaalierottimena. Sijainnin voi muuttaa myöhemmin asetuksista.')));
  }
}
function wzRenderResults(){
  const box=document.getElementById('wz-results');if(!box)return;
  box.replaceChildren();
  for(const r of WZ.results){
    const label=r.name+(r.region?', '+r.region:'')+(r.country_name?' · '+r.country_name:'');
    box.append(wzEl('button',{type:'button',role:'option',class:'wz-result','data-latlon':r.latlon||'',
      on:{click:()=>{WZ.city=r.name;WZ.latlon=r.latlon;WZ.results=[];WZ.query=r.name;wzSay('');wzRender();}}},label));
  }
}
function wzScheduleSearch(){
  if(WZ.timer)clearTimeout(WZ.timer);
  const q=(WZ.query||'').trim();
  if([...q].length<3){WZ.results=[];wzRenderResults();wzSay('');return;}
  WZ.timer=setTimeout(()=>wzSearch(q),450);
}
async function wzSearch(q){
  const seq=++WZ.searchSeq;
  wzSay(WT('Haetaan…'));
  const url='/api/geocode?q='+encodeURIComponent(q)+'&country='+encodeURIComponent(WZ.country||'')+
    '&lang='+encodeURIComponent(wzLang());
  for(let i=0;i<20;i++){
    let j;
    try{const r=await fetch(url,{cache:'no-store'});j=await r.json();}
    catch(e){if(seq===WZ.searchSeq)wzSay(WT('Laite ei vastannut. Yritä uudelleen.'),true);return;}
    if(seq!==WZ.searchSeq)return;          // a newer keystroke owns the box
    if(j.state==='pending'){await new Promise(r=>setTimeout(r,700));continue;}
    if(j.state==='failed'){
      if(j.error==='BUSY'){await new Promise(r=>setTimeout(r,1500));continue;}
      WZ.results=[];wzRenderResults();wzSay(WZ_GEO_ERRORS[j.error]||WT('Haku epäonnistui.'),true);return;}
    WZ.results=j.results||[];wzRenderResults();
    wzSay(WZ.results.length?'':WT('Ei hakutuloksia. Tarkista kirjoitusasu tai anna koordinaatit käsin.'),!WZ.results.length);
    return;
  }
  if(seq===WZ.searchSeq)wzSay(WT('Haku kestää odotettua kauemmin. Yritä uudelleen.'),true);
}
async function wzSaveCity(){
  const ll=WZ.latlon&&wzParseLatLon(WZ.latlon);
  if(!ll)throw Error(WT('Valitse kaupunki hakutuloksista tai anna koordinaatit.'));
  await wzPost({city:WZ.city||'',fmi_latlon:ll,setup_step:'2'});
}
function wzStepZone(body){
  body.append(wzEl('h2',{id:'wz-title'},WT('Sähkömarkkina-alue')));
  const c=wzCountry(WZ.country);
  if(!c){body.append(wzEl('p',{class:'wz-msg bad'},WT('Valitse ensin maa.')));return;}
  body.append(wzEl('p',{class:'muted'},c.zones.length===1?
    WT('Maassasi on yksi sähkömarkkina-alue; se on valittu valmiiksi.'):
    WT('Maassasi on useita sähkömarkkina-alueita. Valitse sähkösopimuksesi alue (näkyy laskussa). Aluetta ei arvata kaupungista.')));
  const group=wzEl('div',{class:'wz-list',role:'radiogroup','aria-label':WT('Sähkömarkkina-alue')});
  for(const z of c.zones){
    const on=WZ.zone===z.id;
    group.append(wzEl('button',{type:'button',role:'radio','aria-checked':String(on),'data-zone':z.id,
      class:'wz-choice'+(on?' sel':''),on:{click:()=>{WZ.zone=z.id;wzRender();}}},wzZoneLabel(c,z,wzLang())));
  }
  body.append(group,wzEl('p',{class:'muted tiny'},WT('Hinnat haetaan valuutassa ')+c.currency+WT(' Nord Poolin päivänsisäisistä hinnoista.')));
}
async function wzSaveZone(){
  const c=wzCountry(WZ.country);
  if(!c)throw Error(WT('Valitse ensin maa.'));
  if(!WZ.zone)throw Error(WT('Valitse sähkömarkkina-alue.'));
  await wzPost({price_zone:WZ.zone,setup_step:'3'});
}
function wzStepTariff(body){
  const minor=wzMinor(wzCurrency());
  body.append(wzEl('h2',{id:'wz-title'},WT('Sähkösopimus')),
    wzEl('p',{class:'muted'},WT('Pörssihinta ei ole se hinta, jonka maksat. Anna sopimuksesi hinnat laskusta. Ilman niitä kokonaishinta käyttää oletusarvoja ja merkitään vahvistamattomaksi.')));
  const fields=[['margin',WT('Sähkönmyyjän marginaali')],['tday',WT('Siirto, päivä')],['tnight',WT('Siirto, yö')],
    ['tax',WT('Sähkövero')],['vat',WT('Arvonlisävero (%)')]];
  for(const [k,label] of fields){
    const unit=k==='vat'?'':' ('+minor+'/kWh)';
    const id='wz-t-'+k;
    const inp=wzEl('input',{id,inputmode:'decimal',autocomplete:'off',value:WZ.tariff[k]||''});
    inp.addEventListener('input',()=>{WZ.tariff[k]=inp.value;});
    body.append(wzEl('label',{class:'wz-label','for':id},label+unit),inp);
  }
  const incl=wzEl('select',{id:'wz-t-incl','aria-label':WT('Sisältävätkö hinnat arvonlisäveron')},
    wzEl('option',{value:'false',selected:WZ.tariff.incl!=='true'},WT('Hinnat ilman arvonlisäveroa')),
    wzEl('option',{value:'true',selected:WZ.tariff.incl==='true'},WT('Hinnat sisältävät arvonlisäveron')));
  incl.addEventListener('change',()=>{WZ.tariff.incl=incl.value;});
  body.append(wzEl('label',{class:'wz-label','for':'wz-t-incl'},WT('Verollisuus')),incl,
    wzEl('p',{class:'muted tiny'},WT('Kuukausimaksut eivät vaikuta tuntihintaan, joten niitä ei kysytä. Päivä- ja yöaikojen rajat sekä kausisiirto löytyvät Asetuksista.')));
}
async function wzSaveTariff(skip){
  if(skip){await wzPost({setup_step:'4'});return;}
  const r=wzTariffFields(WZ.tariff);
  if(!r.ok)throw Error(r.error);
  await wzPost({...r.fields,setup_step:'4'});
}
function wzStepPump(body){
  body.append(wzEl('h2',{id:'wz-title'},WT('Lämpöpumppu')),
    wzEl('p',{class:'muted'},WT('Valitse lämpöpumppusi. Näytetään vain ne yhteydet, jotka tässä ohjelmistossa on toteutettu; kokeelliset on merkitty.')));
  const list=wzEl('div',{class:'wz-list',role:'radiogroup','aria-label':WT('Lämpöpumppu')});
  const adapters=((state.site&&state.site.adapters)||[]).filter(a=>a.selectable);
  for(const a of adapters){
    const on=WZ.adapter===a.id;
    list.append(wzEl('button',{type:'button',role:'radio','aria-checked':String(on),'data-adapter':a.id,
      class:'wz-choice'+(on?' sel':''),on:{click:()=>{WZ.adapter=a.id;wzRender();}}},
      a.manufacturer+' · '+a.models+' — '+(maturityText[a.maturity]||a.maturity)));
  }
  if(!adapters.length)list.append(wzEl('p',{class:'muted'},WT('Tämä ohjelmisto ei vielä sisällä valittavia lämpöpumppuyhteyksiä.')));
  body.append(list);
  if(WZ.adapter==='mitsubishi_cn105_esphome'){
    const inp=wzEl('input',{id:'wz-cn105',autocomplete:'off',autocapitalize:'none',spellcheck:'false',value:WZ.cn105||'',
      placeholder:'http://192.168.1.20/events'});
    inp.addEventListener('input',()=>{WZ.cn105=inp.value;});
    body.append(wzEl('label',{class:'wz-label','for':'wz-cn105'},WT('CN105-sillan /events-osoite')),inp,
      wzEl('p',{class:'muted tiny'},WT('Osoite on kotiverkkosi paikallinen osoite. Lämpöpumppuun ei lähetetä komentoja ennen kuin otat automaattiohjauksen erikseen käyttöön Asetuksista.')));
  }else if(WZ.adapter==='midea_lan'){
    body.append(wzEl('p',{class:'muted'},WT('Midean paikallinen yhteys (V2/V3) viimeistellään Asetukset-näkymän Midea-kortissa, kun käyttöönotto on valmis.')));
  }
}
async function wzSavePump(skip){
  if(skip||!WZ.adapter){await wzPost({setup_step:'5'});return;}
  const f={pump_adapter:WZ.adapter,setup_step:'5'};
  if(WZ.adapter==='mitsubishi_cn105_esphome')f.cn105_url=(WZ.cn105||'').trim();
  await wzPost(f);
}
function wzStepComfort(body){
  const air=(state.status&&state.status.air)||{};
  const mode=Number.isInteger(air.mode)?air.mode:null;
  body.append(wzEl('h2',{id:'wz-title'},WT('Lämpötila ja optimointi')),
    wzEl('p',{class:'muted'},WT('Aseta haluttu lämpötila ja rajat, joiden sisällä Lite saa hyödyntää sähkön hinnanvaihtelua.')));
  const field=(id,label,key,placeholder)=>{
    const inp=wzEl('input',{id,inputmode:'decimal',autocomplete:'off',placeholder:placeholder||'',value:WZ.comfort[key]||''});
    inp.addEventListener('input',()=>{WZ.comfort[key]=inp.value;});
    body.append(wzEl('label',{class:'wz-label','for':id},label),inp);
  };
  field('wz-c-target',WT('Haluttu sisälämpötila (°C)'),'target','21');
  const range=wzTargetRange(mode);
  body.append(wzEl('p',{class:'muted tiny'},range?
    (mode===3?WT('Asuttu-tila: '):WT('Ylläpitolämpö-tila: '))+range[0]+'–'+range[1]+' °C':
    WT('Lämmitystilaa (Ylläpitolämpö tai Asuttu) ei ole valittu; tavoite asetetaan Asetukset-näkymässä.')));
  field('wz-c-min',WT('Pienin sallittu lämpötila (°C, valinnainen)'),'min','19');
  field('wz-c-max',WT('Suurin sallittu lämpötila (°C, valinnainen)'),'max','23');
  const group=wzEl('div',{class:'wz-list',role:'radiogroup','aria-label':WT('Optimointiprofiili')});
  for(const p of WZ_PROFILES){
    const on=WZ.comfort.profile===p.id;
    group.append(wzEl('button',{type:'button',role:'radio','aria-checked':String(on),'data-profile':p.id,
      class:'wz-choice'+(on?' sel':''),on:{click:()=>{WZ.comfort.profile=p.id;wzRender();}}},
      wzEl('strong',{},p.name),wzEl('br'),p.text));
  }
  body.append(wzEl('label',{class:'wz-label'},WT('Optimointiprofiili')),group,
    wzEl('p',{class:'muted tiny'},WT('Lite ei lupaa etukäteen tiettyä säästöä. Rajat pysyvät samoina kaikissa profiileissa; profiili määrää vain, kuinka paljon vaihteluväliä ja hintaeroa käytetään. Automaattiohjaus ei käynnisty tästä, vaan se otetaan erikseen käyttöön Asetuksista.')));
}
// A settings POST may persist successfully while its HTTP response is lost.
// The local site batch is persisted before RAM is updated. Accept an ambiguous
// response only when a new, direct GET confirms the exact durable owner band.
function wzComfortSiteSaved(site,fields){
  if(!site||site.stored_in_device!==true||!site.optimization||!site.region||
     Number(site.region.setup_step)<6)return false;
  const a=site.optimization;
  const bound=x=>x===null||x===''||x===undefined?null:Number(x);
  return a.profile===fields.optimization_profile &&
    bound(a.comfort_min_c)===bound(fields.comfort_min_c) &&
    bound(a.comfort_max_c)===bound(fields.comfort_max_c);
}
// A successful mode POST is NVS-verified by the device, but reloadStatus()
// may discard its own response if a periodic status request overlaps.
// Update the request's acknowledged target synchronously so Back->Comfort
// never compares against an earlier saved target. Invalidate older in-flight
// /api/status responses before they could restore that old target.
function wzAcceptAirTarget(mode,target,confirmedStatus){
  state.statusRequestSequence=(state.statusRequestSequence||0)+1;
  if(confirmedStatus){
    state.status=confirmedStatus; // independent, durable target readback
  }else{
    const previous=state.status||{};
    state.status={...previous,air:{...(previous.air||{}),
      [mode===3?'occupied_target_c':'hold_target_c']:target}};
  }
}
async function wzSaveComfort(skip){
  if(skip){await wzPost({setup_step:'6'});return;}
  const air=(state.status&&state.status.air)||{};
  const mode=Number.isInteger(air.mode)?air.mode:null;
  const r=wzComfortFields(WZ.comfort,mode);
  if(!r.ok)throw Error(r.error);
  const targetChanged=r.target!==null&&
    r.target!==(mode===3?air.occupied_target_c:air.hold_target_c);
  // Keep the previous durable site state for compensation if a second write
  // fails. The site write MUST come first: an NVS-full site error must never
  // change the active/persistent mode target (Codex P2 #57).
  const prior=(state.site&&state.site.optimization)||{};
  const oldFields={optimization_profile:prior.profile||'balanced',
    comfort_min_c:prior.comfort_min_c==null?'':String(prior.comfort_min_c),
    comfort_max_c:prior.comfort_max_c==null?'':String(prior.comfort_max_c),
    setup_step:String((state.site&&state.site.region&&state.site.region.setup_step)||0)};
  const newFields={...r.fields,setup_step:'6'};
  if(targetChanged && (mode===2||mode===3)) {
    // A single durable intent precedes the site write, allowing the firmware
    // to reconcile the mode target at boot after power loss between POSTs.
    newFields.comfort_txn_mode=String(mode);
    newFields.comfort_txn_target=String(r.target);
  }
  try {await wzPost(newFields);}
  catch(siteError) {
    // POST may have committed the whole site blob despite a dropped JSON
    // response. Do not skip the air write while durable new comfort limits
    // are already installed: reconcile with a fresh, request-local read.
    let persisted=null;
    try {persisted=await fetchJSON('/api/settings');}
    catch(_) {throw Error(WT('Tallennus epäonnistui')+' (site status unverified)');}
    const opt=persisted&&persisted.optimization;
    const step=persisted&&persisted.region&&persisted.region.setup_step;
    const matches=fields=>opt&&opt.profile===fields.optimization_profile&&
      (opt.comfort_min_c===null?0:Number(opt.comfort_min_c))===Number(fields.comfort_min_c||0)&&
      (opt.comfort_max_c===null?0:Number(opt.comfort_max_c))===Number(fields.comfort_max_c||0)&&
      Number(step)===Number(fields.setup_step);
    if(!matches(newFields)) {
      if(matches(oldFields))throw siteError; // verified no durable site change
      throw Error(WT('Tallennus epäonnistui')+' (site status unverified)');
    }
    // Mirror wzPost's normal successful reload. Back-navigation must use
    // the VERIFIED newly saved band as its next compensation baseline, not
    // the stale pre-save state.site snapshot (Codex P2 #57).
    state.site=persisted;
    // The site write succeeded; finish the target update even if its
    // response was lost. Do not roll back a successfully saved profile.
  }
  if(targetChanged){
    try {
      // Existing mode endpoint: mode, delta and the other target untouched.
      await airPostLocal('/api/air/config',{mode:String(mode),
        hold:String(mode===2?r.target:air.hold_target_c),delta:String(air.drying_delta_c),
        occupied:String(mode===3?r.target:air.occupied_target_c)});
      // The successful POST itself confirms a durable write. Update the
      // in-memory cache BEFORE any discardable status refresh is awaited.
      wzAcceptAirTarget(mode,r.target,null);
      await reloadStatus();
    } catch(error) {
      // A lost HTTP response is NOT proof that /api/air/config failed: it may
      // have committed the new target before the Wi-Fi link went away.
      // A normal reloadStatus() can silently discard a superseded response;
      // NEVER consult shared state.status for this transaction readback.
      // Use the fresh, request-local API response (or fail ambiguous).
      let confirmedStatus=null;
      try {confirmedStatus=await fetchJSON('/api/status');}
      catch(_) { /* unverified; do not compensate site */ }
      const observed=confirmedStatus&&confirmedStatus.air;
      // RAM readback alone cannot prove a target survived a partial NVS
      // failure. Do not undo the validated site limits unless durable air
      // settings have been explicitly verified by the device.
      if(observed&&observed.config_storage_verified===false)
        throw Error(WT('Tallennus epäonnistui')+' (target status unverified)');
      const currentTarget=observed&&
        (mode===3?observed.occupied_target_c:observed.hold_target_c);
      if(observed&&observed.mode===mode&&currentTarget===r.target) {
        // A successful air POST can lose its reply. A verified independent
        // GET is also the new local state for Back -> Comfort. Otherwise the
        // next save may skip a required target change against stale status.
        wzAcceptAirTarget(mode,r.target,confirmedStatus);
        return;
      }
      if(!observed||observed.mode!==mode||currentTarget!==
         (mode===3?air.occupied_target_c:air.hold_target_c)){
        // Ambiguous outcome: NEVER undo a possibly successful air write.
        // The UI must report uncertainty and refresh when connection returns.
        throw Error(WT('Tallennus epäonnistui')+' (target status unverified)');
      }
      // The device still reports its OLD target. Only now is restoring the
      // previous band/profile safe; if that fails, report the conflict.
      try {await wzPost(oldFields);}
      catch(undoError) {
        throw Error(WT('Tallennus epäonnistui')+
          ' ('+String(undoError)+')');
      }
      throw error;
    }
  }
}
function wzStepDevices(body){
  const air=(state.status&&state.status.air)||{};
  body.append(wzEl('h2',{id:'wz-title'},WT('Anturit ja kulutusmittari')),
    wzEl('p',{class:'muted'},WT('Huonelämpötila tulee RuuviTag-anturilta. Ilman sitä Lite ei pidä pumpun omaa lukemaa todistettuna huonelämpötilana.')),
    wzEl('p',{class:'wz-picked'},WT('Sidottu anturi: ')+(air.bound_mac||WT('ei sidottu'))+' · '+WT('Havaittu: ')+(air.discovered_mac||WT('ei havaintoa'))),
    wzEl('p',{class:'muted tiny'},WT('Anturin sidonta ja nimeäminen tehdään Asetukset-näkymän RuuviTag-kortissa.')));
  const inp=wzEl('input',{id:'wz-shelly',autocomplete:'off',autocapitalize:'none',spellcheck:'false',value:WZ.shelly||'',placeholder:'http://192.168.1.30'});
  inp.addEventListener('input',()=>{WZ.shelly=inp.value;});
  body.append(wzEl('label',{class:'wz-label','for':'wz-shelly'},WT('Shelly-mittarin osoite (valinnainen)')),inp,
    wzEl('p',{class:'muted tiny'},WT('Mittarin tulee mitata vain lämpöpumppua, ei koko taloa. Lite ei ohjaa Shellyn relettä.')));
}
async function wzSaveDevices(){
  const f={setup_step:'7'};
  if((WZ.shelly||'').trim()!==((state.site&&state.site.shelly_url)||''))f.shelly_url=(WZ.shelly||'').trim();
  await wzPost(f);
}
function wzStepSummary(body){
  body.append(wzEl('h2',{id:'wz-title'},WT('Yhteenveto')));
  const list=wzSummary({site:state.site,status:state.status,prices:state.prices,weather:state.weather,plan:state.plan});
  const need=wzNeedsAction(list);
  body.append(wzEl('p',{class:need.length?'wz-msg bad':'wz-msg ok'},need.length?
    WT('Seuraavat asiat vaativat vielä toimenpiteitä:')+' '+need.length:WT('Kaikki on kunnossa.')));
  const ul=wzEl('ul',{class:'wz-sum'});
  const mark={ok:'✓',warn:'!',todo:'○',optional:'–'};
  for(const it of list)ul.append(wzEl('li',{class:'wz-'+it.state,'data-key':it.key},
    wzEl('span',{class:'wz-mark','aria-hidden':'true'},mark[it.state]),wzEl('span',{},wzEl('strong',{},it.label),' ',it.detail)));
  body.append(ul,wzEl('p',{class:'muted tiny'},WT('Dashboardiin pääsee, vaikka kaikki ei olisi valmista. Jatka asetuksia myöhemmin Asetukset-näkymässä.')));
}
const WZ_STEP_FNS=[wzStepRegion,wzStepCity,wzStepZone,wzStepTariff,wzStepPump,wzStepComfort,wzStepDevices,wzStepSummary];
const WZ_STEP_TITLES=[WT('Kieli ja maa'),WT('Sijainti'),WT('Sähkömarkkina-alue'),WT('Sähkösopimus'),WT('Lämpöpumppu'),
  WT('Lämpötila ja optimointi'),WT('Anturit ja mittari'),WT('Yhteenveto')];

function wzRender(){
  const body=document.getElementById('wz-body');if(!body)return;
  body.replaceChildren();
  WZ_STEP_FNS[WZ.step](body);
  document.getElementById('wz-progress').textContent=(wzLang()==='en'?'Step ':'Vaihe ')+(WZ.step+1)+' / '+WZ_STEPS;
  const bar=document.getElementById('wz-bar');if(bar)bar.style.width=Math.round((WZ.step+1)/WZ_STEPS*100)+'%';
  const last=WZ.step===WZ_STEPS-1;
  document.getElementById('wz-back').hidden=WZ.step===0;
  document.getElementById('wz-next').textContent=last?WT('Valmis, avaa dashboard'):WT('Jatka');
  const skippable=[1,3,4,5,6].includes(WZ.step);
  document.getElementById('wz-skip').hidden=!skippable;
  wzSay(WZ.message,WZ.messageBad);
  const h=document.getElementById('wz-title');if(h)h.focus&&h.setAttribute('tabindex','-1');
}
async function wzNext(skip){
  if(WZ.busy)return;
  WZ.busy=true;document.getElementById('wz-next').disabled=true;wzSay('');
  try{
    const s=WZ.step;
    if(s===0)await wzSaveRegion();
    else if(s===1){if(skip)await wzPost({setup_step:'2'});else await wzSaveCity();}
    else if(s===2){
      // One-zone countries are implied; the server already stored the zone.
      const c=wzCountry(WZ.country);
      if(c&&c.zones.length===1)WZ.zone=c.zones[0].id;
      await wzSaveZone();
    }
    else if(s===3)await wzSaveTariff(skip);
    else if(s===4)await wzSavePump(skip);
    else if(s===5)await wzSaveComfort(skip);
    else if(s===6){if(skip)await wzPost({setup_step:'7'});else await wzSaveDevices();}
    else{
      await wzPost({setup_complete:'true',setup_step:String(WZ_DONE)});
      wzClose();switchView('home');await reloadStatus();return;
    }
    WZ.message='';WZ.step=Math.min(WZ_STEPS-1,WZ.step+1);
    if(WZ.step===WZ_STEPS-1){await reloadStatus();await reloadPrices();await reloadWeather();await reloadPlan();}
    wzRender();
  }catch(e){wzSay(e.message,true);}
  finally{WZ.busy=false;document.getElementById('wz-next').disabled=false;}
}
function wzClose(){WZ.open=false;const w=document.getElementById('wizard');if(w)w.hidden=true;document.body.classList.remove('wz-open');}
async function wzOpen(fromSettings){
  const w=document.getElementById('wizard');if(!w)return;
  try{await wzLoadRegions();}catch(e){
    // Without the region table the wizard cannot offer correct choices; say so.
    w.hidden=false;w.querySelector('#wz-body').textContent=WT('Maalistaa ei voitu lukea laitteelta. Päivitä sivu.');return;}
  const site=state.site||{},r=site.region||{};
  WZ.country=r.country||'';WZ.zone=r.price_zone||'';WZ.city=r.city||'';WZ.latlon=site.fmi_latlon||'';
  WZ.query=r.city||'';WZ.adapter=site.pump_adapter||'';WZ.cn105=site.cn105_url||'';WZ.shelly=site.shelly_url||'';
  const opt=site.optimization||{},air=(state.status&&state.status.air)||{};
  WZ.comfort={target:'',min:opt.comfort_min_c?String(opt.comfort_min_c):'',max:opt.comfort_max_c?String(opt.comfort_max_c):'',
    profile:opt.profile||'balanced'};
  const tgt=air.mode===3?air.occupied_target_c:air.mode===2?air.hold_target_c:null;
  if(typeof tgt==='number')WZ.comfort.target=String(tgt);
  WZ.step=fromSettings?0:wzStartStep(site);
  const c=wzCountry(WZ.country);
  if(c&&c.zones.length===1&&!WZ.zone)WZ.zone=c.zones[0].id;
  WZ.open=true;w.hidden=false;document.body.classList.add('wz-open');
  wzRender();
}
function wizardMaybeOpen(){
  const r=state.site&&state.site.region;
  if(r&&r.setup_complete===false&&!WZ.open&&!WZ.dismissed)wzOpen(false);
}
</script>
<script>
'use strict';

// Finnish and English are bundled in flash. No browser translation service,
// external network calls, firmware settings or heat-pump writes are involved.
let uiLanguage=(()=>{
  try {return localStorage.getItem('enevina_lite_language')==='en'?'en':'fi';}
  catch(_){return 'fi';}
})();
const UI_EN=Object.freeze({"Enevina Lite | Energianhallinta":"Enevina Lite | Energy Management","Päävalikko":"Main menu","Näkymät":"Views","Etusivu":"Home","Sähkön hinta":"Electricity price","Sääennuste":"Weather forecast","Optimointi":"Optimization","Asetukset":"Settings","Enevina Lite · paikallinen ohjain":"Enevina Lite · local controller","Laiteasetukset":"Device settings","Energia · Lämpöpumppu":"Energy · Heat pump","Aikaa ei ole synkronoitu":"Time not synchronized","Yhdistetään":"Connecting","OHJAUS POIS":"CONTROL OFF","Ota CN105-automaattiohjaus käyttöön Asetukset-näkymästä. Optimointiehdotus ei yksin lähetä käskyjä.":"Enable CN105 automatic control in Settings. An optimization suggestion alone never sends commands.","Yleisnäkymä":"Overview","Sisäilman olosuhteet":"Indoor conditions","Mittaukset, sähkön hinta ja tuleva lämmityssuunnitelma.":"Measurements, electricity prices and upcoming heating plan.","Sisälämpötila · RuuviTag":"Indoor temperature · RuuviTag","Tila ei tiedossa":"Status unknown","Odotetaan mittausta":"Waiting for measurement","Sähkön hinta nyt":"Electricity price now","Spot-hinta · ilman arvonlisäveroa":"Spot price · excluding VAT","Arvio kokonaishinnasta":"Estimated total price","Ulkolämpötila / ennuste":"Outdoor temperature / forecast","Mittauskulutus":"Measured power","Ilmankosteus":"Humidity","RuuviTag · suhteellinen kosteus":"RuuviTag · relative humidity","Kastepiste":"Dew point","Anturin tila":"Sensor status","Sähkön tulevat hinnat":"Upcoming electricity prices","15 minuutin jaksot":"15-minute periods","Hintadataa odotetaan":"Waiting for price data","Halvinta jaksoa ei tiedossa":"Cheapest period unknown","Kaikki hinnat":"All prices","Näytä 48 tunnin ennuste":"View 48-hour forecast","Odotetaan lämpötilaennustetta.":"Waiting for temperature forecast.","Oma energiankulutusluokka":"Personal energy consumption grade","Epävirallinen":"Unofficial","Vuoden kulutustieto puuttuu":"Annual consumption data missing","Lämmitetty pinta-ala m²":"Heated floor area m²","Vuotuinen sähkönkulutus kWh":"Annual electricity use kWh","Syötä vuosikulutus":"Enter annual consumption","Laske luokka":"Calculate grade","Oma havainnollistava A–G-asteikko: alle 60 / 100 / 150 / 200 / 250 / 350 kWh/m²/v. G yli 350. Ei virallinen energiatodistus tai E-luku; ei huomioi lämmitysmuotoja tai sääkorjausta. Tiedot säilyvät vain tässä selaimessa.":"Illustrative A–G scale: below 60 / 100 / 150 / 200 / 250 / 350 kWh/m²/year; G above 350. Not an official energy certificate or Finnish E-value. Heating type and weather adjustments are not included. Data is stored only in this browser.","Optimoinnin seuraava toimenpide":"Next optimization action","Ei suunnitelmaa":"No plan","Hyväksyttyä optimointisuunnitelmaa ei vielä ole.":"No validated optimization plan is available yet.","Optimointi näkyy vasta, kun käytettävissä on kelvollinen, turvallinen ja kalibroitu laskentapohja.":"Optimization is shown once valid, safe and calibrated model data is available.","Pörssisähkö":"Spot electricity","Sähkön hinnat":"Electricity prices","Suomen 15 minuutin hinnat. Kaavio näyttää vain ladatun tulevan datan.":"Finnish 15-minute prices. The chart displays only available future data.","Nykyinen spot":"Current spot price","snt/kWh, alv 0 %":"c/kWh, VAT 0%","Edullisin tuleva jakso":"Cheapest upcoming period","Ei tietoa":"No data","Saatavilla oleva ennuste":"Available forecast","Tietolähdettä ei ole päivitetty":"Source has not been updated","Hintakehitys":"Price trend","Hintoja ei ole saatavilla":"No prices available","Spot-hinta on ilman veroja. Kokonaishinnan arviossa sovelletaan laitteelle määriteltyjä sopimuskohtaisia vero- ja siirtomaksuja. Hintoja ei arvata puuttuville varttitunneille.":"Spot price excludes taxes. Estimated total price includes the configured electricity tax, grid charges and VAT. Missing 15-minute prices are never estimated.","Seuraavat hintajaksot":"Upcoming price periods","Alkaa":"Starts","Spot snt/kWh":"Spot c/kWh","Kokonaishinta snt/kWh":"Total price c/kWh","Odota hintatietoja.":"Waiting for prices.","Seuraava ennustelämpötila":"Next forecast temperature","Alin seuraavan 24 tunnin aikana":"Lowest in the next 24 hours","Ylin seuraavan 24 tunnin aikana":"Highest in the next 24 hours","Ennustettu ulkolämpötila":"Forecast outdoor temperature","Ulkolämpötila · seuraavat 48 tuntia":"Outdoor temperature · next 48 hours","Ennustetietoja odotetaan.":"Waiting for forecast data.","Ennusteen päivitysaikaa ei tiedetä.":"Forecast update time unknown.","Näytetään vain Ilmatieteen laitokselta vastaanotettuja kelvollisia tuntipisteitä. Ennuste voi poiketa toteutuneesta säästä; sade- ja tuulitiedot eivät sisälly tähän syötteeseen.":"Only validated hourly readings received from MET Norway are shown. Actual weather may differ; precipitation and wind data are not part of this feed.","Tuntiennuste":"Hourly forecast","Ajankohta":"Time","Lämpötila °C":"Temperature °C","Ladataan sääennustetta…":"Loading weather forecast…","Ennakoiva lämmitys":"Predictive heating","Optimointisuunnitelma":"Optimization plan","Hintaohjaus tuottaa ehdotuksen; CN105-komennot edellyttävät erillistä käyttöönottoa.":"Price-based control generates a proposal; CN105 commands require separate authorization.","Suunnitelma":"Plan","Ei kelvollista laskelmaa":"No valid calculation","Ennustettu kustannus":"Forecast cost","Mallin kokonaisarvio, jos kelvollinen":"Model total estimate, if valid","Ennustettu alin lämpötila":"Predicted lowest temperature","Laskettu, ei anturimittaus":"Calculated, not measured by a sensor","Hyväksyttyä suunnitelmaa ei ole. Lämpöpumppu ei saa tältä käyttöliittymältä komentoja.":"No validated plan is available. The heat pump receives no commands from this interface.","Ehdotus":"Suggestion","Ennuste °C":"Forecast °C","Sähkö kWh":"Electricity kWh","Arvio €":"Estimate €","Suunnitelma ei saatavilla.":"Plan unavailable.","Olosuhteet ja laite":"Conditions and device","Paikallinen huonetavoite ja erikseen käyttöönotettava CN105-lämmitysohjaus.":"Local room temperature target and independently enabled CN105 heating control.","Lämmityksen toimintatila":"Heating operating mode","Ylläpitolämpö ja Asuttu käyttävät omia, toisistaan riippumattomia huonetavoitteitaan. Hintaohjaus voi säätää Mitsubishin pyyntilämpötilaa.":"Maintenance and Occupied modes have separate room temperature targets. Price-based control may adjust the Mitsubishi setpoint.","Laskennallinen tila":"Operating mode","Kylmillään":"Unheated","Kuivanapito":"Drying","Ylläpitolämpö":"Maintenance","Asuttu":"Occupied","Ylläpitotavoite °C":"Maintenance target °C","Asuttu °C":"Occupied °C","Kuivanapito Δ°C":"Drying Δ°C","Tallenna asetukset":"Save settings","Valitse Ylläpitolämpö tai Asuttu sekä kyseisen tilan huonetavoite. Hintaehdotus laskee Mitsubishille 16–26 °C pyyntilämpötilan 0,5 °C askelin; komennot sallitaan vain erillisellä käyttöönotolla.":"Choose Maintenance or Occupied and its room target. Price-based control calculates Mitsubishi setpoints from 16 to 26 °C in 0.5 °C steps. Commands require separate authorization.","Mitsubishi · automaattinen lämpötilan säätö":"Mitsubishi · automatic temperature control","Laskenta käyttää RuuviTagia ja sähkön kokonaishintaa. Lämmitystä ohjataan Ylläpitolämpö- ja Asuttu-tiloissa. Komento lähtee CN105:lle, ja tavoitelämpötila tarkistetaan takaisinlukemalla. Käyttöönotto on aina erillinen valinta eikä firmwarepäivitys käynnistä sitä.":"Control uses RuuviTag measurements and total electricity price. Heating is controlled in Maintenance and Occupied modes. CN105 commands are verified by readback. Authorization is always separate; firmware updates do not enable control.","Ohjaus ei käytössä":"Control disabled","Ei komentoja":"No commands","Ota automaattiohjaus käyttöön":"Enable automatic control","Poista automaattiohjaus käytöstä":"Disable automatic control","Sähkösopimus ja siirto":"Electricity contract and distribution","Kokonaishinta = spot + marginaali + siirto + sähkövero, arvonlisävero lisätään kerran. Kuukausimaksut eivät vaikuta 15 minuutin ohjaukseen eikä niitä lasketa mukaan.":"Total price = spot + margin + grid charges + electricity tax, with VAT added once. Monthly fees are excluded from 15-minute control calculations.","Marginaali snt/kWh":"Margin c/kWh","Siirto päivä snt/kWh":"Daytime grid charge c/kWh","Siirto yö / muu aika snt/kWh":"Night/other grid charge c/kWh","Sähkövero snt/kWh":"Electricity tax c/kWh","ALV %":"VAT %","Siirtotuote":"Grid tariff","Yleissiirto":"Flat-rate distribution","Aikasiirto (päivä/yö)":"Time-of-use (day/night)","Kausisiirto (talviarkipäivä)":"Seasonal (winter weekdays)","Päivä alkaa klo":"Daytime starts at","Yö alkaa klo":"Nighttime starts at","Hinnat sisältävät ALV:n":"Prices include VAT","Kyllä (marginaali, siirto, vero)":"Yes (margin, grid, tax)","Ei, alv lisätään":"No, VAT is added","Esilämmityksen hintaetu %":"Preheating price advantage %","Spot-hinta on aina ilman arvonlisäveroa. Esilämmitys tehdään vain, kun hinta on vähintään annetun prosentin verran halvempi kuin seuraavien kahden tunnin mediaani.":"Spot price always excludes VAT. Preheating is considered only when the price is at least the specified percentage below the median of the next two hours.","Lämpöpumppu ja yhteydet":"Heat pump and connections","Lämpöpumppu ja yhteys":"Heat pump and connection","CN105-sillan /events-osoite":"CN105 bridge /events URL","Shelly-mittarin osoite":"Shelly meter URL","Valmistaja / malli":"Manufacturer / model","Yhteys":"Connection","Tila":"Status","Mittaukset":"Measurements","Komennot":"Commands","Ladataan…":"Loading…","Vain tällä laitteella todennetut sovittimet voi valita. Muut on lueteltu tutkimustietona, ei tukilupauksena. Sääpaikan muutos tulee voimaan seuraavassa virran kytkennässä. Laitteen valinta ei itsessään ota ohjausta käyttöön.":"Only adapters verified on this device can be selected. Other entries are research information, not guaranteed support. Weather location changes take effect on the next power cycle. Selecting a device does not enable control.","Tallenna sopimus ja yhteydet":"Save contract and connections","Vaihda Wi-Fi-verkko":"Change Wi-Fi network","Laitteen tila":"Device status","Ohjelmistoversio":"Firmware version","Wi-Fi RSSI":"Wi-Fi RSSI","CN105-telemetria":"CN105 telemetry","Shelly-mittaus":"Shelly measurement","Sisäinen vapaa RAM":"Free internal RAM","PSRAM vapaana":"Free PSRAM","OTA-lataus käytössä":"OTA downloads enabled","Avaa laiteasetukset":"Open device settings","Mitsubishi CN105 · vastaanotettu data":"Mitsubishi CN105 · received data","Viimeisin SSE-tilaviesti":"Latest SSE status event","Pumpun tila (SSE)":"Pump mode (SSE)","Pumpun sisälämpötila (SSE)":"Pump indoor temperature (SSE)","Asetuslämpötila (SSE)":"Target temperature (SSE)","Kompressori (SSE)":"Compressor (SSE)","Kompressoriviestin ikä":"Age of compressor event","ESPHome /events -rajapinnasta vastaanotettu viimeisin tilatieto. Viestin vastaanottoaika ei todista uutta CN105-UART-lukua, eikä näitä arvoja käytetä optimoinnin turvallisuushyväksyntään.":"Latest status data received via ESPHome /events. Event reception does not prove a fresh CN105 UART poll; these values are not treated as verified optimization-safety data.","RuuviTag · anturit ja kosteusvaroitukset":"RuuviTag · sensors and humidity alerts","Anturin MAC-tunniste on sidottava tarkoituksella. Kirjoita tunniste tai":"Sensor MAC must be explicitly bound. Enter an address or","irrottaaksesi sidonnan. Asetukset ovat vain paikallisia.":"to unbind it. Settings are local only.","Sisäanturin sidonta":"Indoor sensor binding","Havaittu tunniste":"Detected identifier","Anturin rooli":"Sensor role","Sisäilma":"Indoor air","Ulkoilma":"Outdoor air","XX:XX:XX:XX:XX:XX tai none":"XX:XX:XX:XX:XX:XX or none","Tallenna anturisidonta":"Save sensor binding","RH-varoitus %":"RH warning %","Korkea RH-varoitus %":"High RH warning %","Paristovaroitus V":"Battery warning V","Tallenna varoitusrajat":"Save alert thresholds","Ilman RH-arvot eivät yksin osoita rakenteiden kosteusturvallisuutta.":"Air humidity alone does not establish whether building structures are safe from moisture.","Sisäilman historia · RuuviTag":"Indoor climate history · RuuviTag","Historiatietoja odotetaan":"Waiting for history data","Viimeisimmät mittaukset, ei rakenteen kosteusmittaus. Offline-ajalta tallennettuja aikaleimoja ei esitetä UTC-aikana.":"Recent measurements, not structural moisture measurements. Offline timestamps are not shown as UTC.","Enevina Lite · Paikallinen toiminta ·":"Enevina Lite · Local operation ·","Ei aikaa":"No time available"," kWh/m² vuodessa":" kWh/m² per year","Syötä todellinen 12 kuukauden kulutus":"Enter actual consumption for the last 12 months","Syötä yli nollan oleva lämmitetty pinta-ala ja täyden vuoden kulutus.":"Enter a heated floor area above zero and full-year consumption.","Vertailuluokka tallennettu tähän selaimeen. Ei virallinen energiatodistus tai E-luku.":"Comparison grade saved in this browser. Not an official energy certificate or E-value.","Vertailuluokka laskettu; selain ei sallinut paikallista tallennusta.":"Comparison grade calculated; the browser did not permit local storage.","Tulevien 15 minuutin sähkönhintojen pylväskaavio":"Bar chart of upcoming 15-minute electricity prices","Vaihteluväli: ":"Price range: ","Mittaukset käytössä":"Measurements active","Yhteys muodostettu":"Connected","Ei tuoretta mittausta":"No recent measurement","RuuviTag ei ole tuore tai anturia ei ole sidottu":"RuuviTag is stale or no sensor is bound","Tuore":"Fresh","Puuttuu / vanhentunut":"Missing / stale","Viimeisin haku: ":"Last fetched: ","CN105-OHJAUS SALLITTU":"CN105 CONTROL ENABLED","Automaattiohjaus on kytketty. Käskyjä lähetetään Ylläpitolämpö- tai Asuttu-tilassa, kun mittaus- ja yhteysportit ovat kunnossa.":"Automatic control is enabled. Commands are sent in Maintenance or Occupied mode when sensor and connection checks pass.","Automaattiohjaus ei ole päällä. Laskenta näyttää ehdotuksia ilman fyysisiä käskyjä.":"Automatic control is off. Calculations show suggestions without physical commands.","Automaattiohjaus valittu · ":"Automatic control enabled · ","Automaattiohjaus pois":"Automatic control off","Komentoja ":"Commands "," · sillalta kuitattu ":" · acknowledged by bridge "," · virheitä ":" · errors ","Ei Wi-Fiä":"No Wi-Fi","UART-lähde varmennettu":"UART source verified","SSE-lukemia, UART-todiste puuttuu":"SSE readings, no UART proof","Ei CN105-lukemia":"No CN105 readings","UART-mittaus varmennettu":"UART reading verified","SSE vastaanotettu ":"SSE received ","(ikä ei tiedossa)":"(age unknown)"," s sitten":" s ago"," · ei UART-todistetta":" · no UART proof","Ei tilatietoja":"No status data"," (SSE)":" (SSE)","Ei vastaanottoa":"Not received"," s sitten (vain SSE)":" s ago (SSE only)","Käytössä":"Available","Ei lukemaa":"No measurement","Kyllä · allekirjoitettu":"Yes · signed","Ei":"No","Ei sidottu":"Not bound","Ei havaintoa":"No data","RH koholla":"High RH","RH erittäin korkea":"Very high RH","RuuviTag ei vastaa":"RuuviTag not responding","RuuviTag-paristo heikko":"Low RuuviTag battery","Pakkaslämpötila":"Freezing temperature","Varoitus":"Warning","Kuittaa":"Acknowledge","Kuittaus epäonnistui: ":"Acknowledgment failed: "," jaksoa":" periods","Edullisin seuraava jakso ":"Cheapest upcoming period ","Ei kelvollista hintadataa":"No valid price data","Hintatietoja ei saatavilla":"Price data unavailable","Ei voimassa olevia hintoja.":"No currently valid prices.","Pidä nykyinen tila":"Maintain current state","Hintaohjauksen ehdotus":"Price-based control proposal","Huonetermostaatin ehdotus":"Room thermostat proposal","MPC-laskelma":"MPC calculation","Ei hyväksyttyä suunnitelmaa":"No validated plan","Hintaohjaus":"Price-based control","Termostaattiohjaus":"Thermostat control","Ei käytössä":"Not active","Laskettu ":"Calculated ","Seuraava jakso ":"Next period ","Lähtötason ehdotus: ":"Fallback proposal: ","Hyväksytty optimointisuunnitelma puuttuu.":"No validated optimization plan is available.","Simuloitu MPC-ehdotus. Automaattiohjaus toimii erillisellä hintaohjauspolulla.":"Simulated MPC proposal. Automatic control operates through a separate price-based control path.","Ruuvi + 15 min kokonaishinnat; ":"Ruuvi + 15-minute total prices; ","Hintadata puuttuu: Ruuvi-huonetermostaatti; ":"Price data missing: Ruuvi room thermostat; ","huonetavoite ":"room target ","; syy: ":"; reason: ",". CN105-ohjaus edellyttää käyttäjän valintaa.":". CN105 control requires explicit user authorization.","puutteelliset lähtötiedot":"incomplete inputs","Ehdotus estetty: ":"Proposal blocked: ","Ehdotukset ovat laskennallisia. Fyysiset ohjauskäskyt ovat estettyjä.":"Proposals are calculated. Physical commands are blocked.","Lähtötason hintaohjaus: ":"Fallback price-based control: ","Hintojen puuttuessa termostaatti: ":"Thermostat fallback without prices: "," (ehdotus; automaattiohjauksen käynnistys valitaan Asetuksissa).":" (proposal; automatic control must be enabled in Settings).","Optimointiehdotus ei ole saatavilla. Puuttuva lämpötila tai puutteellinen hintajakso estää laskennan.":"No optimization proposal available. A missing temperature or incomplete price period prevents calculation.","Nykyinen ehdotus: ":"Current proposal: ",". Mallinnettu tuntien lämpötila- ja kustannusennuste puuttuu.":". Modeled hourly temperature and cost forecast is missing.","Lämpötilahistoriaa ei vielä ole riittävästi":"Not enough temperature history yet","Sisälämpötilan historia":"Indoor temperature history","Näkyvät lämpötilat: ":"Visible temperatures: "," tuntipistettä":" hourly points","Yhteys poikki":"Disconnected","Ei yhteyttä":"No connection","Mittaukset eivät päivity":"Measurements are not updating","Ei tuoretta tilaa":"No recent status","Laite ei vastaa tai vahvistustunniste puuttuu.":"Device unavailable or authorization token missing.","Tallennetaan ohjauksen käyttöönottoa…":"Saving control authorization…","Asetuksen tallennus epäonnistui":"Could not save setting","Automaattiohjaus sallittu. Ensimmäinen ohjaus odottaa tuoreita tietoja ja Ylläpitolämpö- tai Asuttu-tilaa.":"Automatic control authorized. First command awaits fresh measurements and Maintenance or Occupied mode.","Automaattiohjaus poistettu käytöstä.":"Automatic control disabled.","Ohjauksen asetusta ei tallennettu":"Control setting was not saved","Laite ei vastaa tai valtuutus puuttuu":"Device unavailable or authorization missing","Tallennus epäonnistui":"Save failed","Anna kuusi tavuinen MAC-tunniste tai none":"Enter a six-byte MAC address or none","Anturisidonta tallennettu.":"Sensor binding saved.","Tarkista varoitusarvot":"Check warning thresholds","Varoitusrajat tallennettu.":"Alert thresholds saved.","Aiemmat varoitusrajat tallennettu. Uudet muutokset odottavat tallennusta.":"Earlier alert thresholds saved. New changes still await saving.","Tiedot puuttuvat.":"Data missing.","Tarkista lämpötilatavoitteet.":"Check the temperature targets.","Tallennetaan paikallisia asetuksia…":"Saving local settings…","Asetukset tallennettu. Lämpöpumpulle ei lähetetty käskyä.":"Settings saved. No command was sent to the heat pump.","Aiemmat asetukset tallennettu. Uusia muutoksia odottaa tallennusta.":"Earlier settings saved. New changes are pending.","Tallennus ei onnistunut.":"Save failed.","Tarkista numero":"Check number","Arvo on sallitun alueen ulkopuolella":"Value is out of range","Osoite ei kelpaa (vain paikallinen http-osoite)":"Invalid URL (local HTTP URL only)","Liian pitkä arvo":"Value too long","Virheellinen valinta":"Invalid selection","Laitetta ei vielä tueta":"Device is not yet supported","Asetukset eivät ole kelvolliset":"Settings are invalid","Tallennus laitteen muistiin epäonnistui":"Saving to device memory failed","Päivitä sivu ja yritä uudelleen":"Refresh the page and try again","Kenttäpilotti":"Field pilot","Vain PC-testattu":"PC-tested only","Vain selvitetty":"Documented only","Ei valittu":"Not selected","Asetuksia ei voitu lukea laitteelta.":"Could not read settings from device.","Tallennettu. Lämpöpumpun yhteys vaihtui, joten automaattiohjaus poistettiin käytöstä: ota se uudelleen käyttöön uudelle laitteelle.":"Saved. Heat pump connection changed, so automatic control was disabled. Re-enable it for the new device.","Tallennettu. Uudet hinnat ja osoitteet ovat heti käytössä.":"Saved. New prices and addresses take effect immediately.","Tarvittava laite ei vastannut.":"Required device did not respond.","MET Norwayn ennuste haettu: ":"MET Norway forecast fetched: ","Kelvollista MET Norwayn ennustetta ei ole saatavilla.":"No valid MET Norway forecast available.","Kelvollista lämpötilaennustetta ei ole saatavilla":"No valid temperature forecast available","Seuraava ulkolämpötila ":"Next outdoor temperature "," klo ":" at "," ennustetuntia saatavilla.":" forecast hours available.","Ennustetta ei ole saatavilla. Lämmityksen ohjaus ei käytä keksittyjä sääarvoja.":"Forecast unavailable. Heating control does not use invented weather readings.","Lämpötilan käyrään tarvitaan vähintään kaksi tuntipistettä.":"A temperature curve requires at least two hourly points.","MET Norwayn ulkolämpötilaennusteen kuvaaja":"Chart of MET Norway outdoor temperature forecast","Ennustetunteja ei ole saatavilla.":"No hourly forecast available.","Käyttöliittymän kieli":"Interface language","Sääennuste · MET Norway":"Weather forecast · MET Norway","Norjan ilmatieteen laitos · MET Norway":"Norwegian Meteorological Institute · MET Norway","Paikallinen ulkolämpötilaennuste MET Norwayn maailmanlaajuisesta sääpalvelusta. Puuttuvia arvoja ei arvata.":"Local outdoor temperature forecast from MET Norway's worldwide weather service. Missing values are not estimated.","Odotetaan MET Norwayn ennustetta":"Waiting for MET Norway forecast","Sääpaikka (leveysaste,pituusaste)":"Weather location (latitude,longitude)","Sääpaikan muutos tulee voimaan heti tallennuksen jälkeen.":"Weather location changes take effect immediately after saving.","Näytetään vain MET Norwaylta vastaanotettuja kelvollisia tuntipisteitä.":"Only validated hourly values from MET Norway are shown.","MET Norwayn ennustetta ei ole saatavilla.":"No MET Norway forecast available.","Lähde:":"Source:",".\n      Ennustetiedot on muunnettu Enevina Liten lämpötilakäyräksi.\n      Näytetään vain MET Norwaylta vastaanotettuja kelvollisia tuntipisteitä. Ennuste voi poiketa toteutuneesta säästä; sade- ja tuulitiedot eivät sisälly tähän syötteeseen.":".\n      Forecast data has been converted into the Enevina Lite temperature chart.\n      Only validated hourly points from MET Norway are shown. Actual weather may vary; wind and precipitation are not included in this view.","Vain tällä laitteella todennetut sovittimet voi valita. Muut on lueteltu tutkimustietona, ei tukilupauksena. Sääpaikan muutos tulee voimaan heti tallennuksen jälkeen. Laitteen valinta ei itsessään ota ohjausta käyttöön.":"Only adapters verified on this device can be selected. Other entries are research information, not guaranteed support. The weather location changes immediately after saving. Selecting a device does not enable control.","Mitsubishi on CN105-kenttäpilotti ja Midea V2/V3 voidaan valita yhteyden tilanlukuun ja manuaaliseen ohjaustestiin. Muut on lueteltu tutkimustietona, ei tukilupauksena. Sääpaikan muutos tulee voimaan heti tallennuksen jälkeen. Laitteen valinta ei itsessään ota ohjausta käyttöön.":"Mitsubishi CN105 is a field pilot; Midea V3 is selectable for read-only telemetry. Other entries are research only, not guaranteed compatibility. Weather location changes immediately after saving. Device selection does not enable control.","Midea · paikallinen V3 Wi-Fi -yhteys":"Midea · local V3 Wi-Fi connection","V3-luku tapahtuu suoraan lämpöpumpun alkuperäisestä Wi-Fi-moduulista ilman Home Assistantia tai jatkuvaa pilviyhteyttä. Midean manuaalinen lämpötilan ja toimintatilan ohjaus on nyt kokeellinen; automaattisen optimoinnin komentoja ei vielä lähetetä. Yhdistä vain omassa luotetussa lähiverkossa; kehitysvaiheen HTTP-asetussivu ei salaa tunnisteiden siirtoa.":"V3 status is read directly from the heat pump's original Wi-Fi module without Home Assistant or a continuous cloud connection. This is a read-only test: no automatic temperature commands are sent. Use only a trusted local network; this development HTTP setup page does not encrypt the credentials in transit.","Midean paikallinen IP":"Midea local IP address","Midea V3 device ID":"Midea V3 device ID","V3 token (128 heksamerkkiä)":"V3 token (128 hex characters)","V3 key (64 heksamerkkiä)":"V3 key (64 hex characters)","Tallenna Midea-yhteys":"Save Midea connection","Ei mittausta":"No measurement","Yhteyden tila":"Connection status","Toimintatila":"Operating mode","Pyyntilämpötila":"Target temperature","Pumpun sisälämpötila":"Heat pump indoor temperature","V3 tunnisteet tallennetaan laitteen paikalliseen NVS-muistiin. Niitä ei palauteta selaimeen. DHCP-IP:n automaattinen tunnistus ja automaattinen Midea-optimointiohjaus eivät vielä sisälly tähän testitoteutukseen.":"V3 credentials are stored in the device's local NVS memory and are not returned to the browser. Automatic DHCP IP rediscovery and heat pump control are not included in this read-only version.","Lukuyhteys (testivaihe)":"Read-only connection (experimental)","Midea V3 on tässä versiossa vain tilanluku. Ohjaus ei ole käytössä.":"Midea V3 is read-only in this version. Control is not available.","Midea ei valittu":"Midea not selected","Ei asetuksia":"Not configured","Paikallinen tilanluku toimii":"Local status polling works","V3-yhteys onnistui":"V3 connection succeeded","Midea-yhteyttä ei ole asetettu":"Midea connection not configured","Lämmitys":"Heating","Tila ":"Mode ","Pois päältä":"Off","Laite ei vastaa.":"Device not responding.","Tarkista device ID, V3 token (128) ja key (64 heksamerkkiä).":"Check device ID, V3 token (128) and key (64 hex characters).","Midea-yhteys tallennettu. Ensimmäinen luku tapahtuu verkossa.":"Midea connection saved. The first status poll will take place over the network.","Tallennus ei onnistunut (":"Save failed (","Yhteyttä ei ole":"Not connected","Ulkolämpötila":"Outdoor temperature","Testaa yhteys nyt":"Test connection now","Ei vielä testattu.":"Not tested yet.","Valitse Midea lämpöpumpun yhteydeksi.":"Select Midea as the heat pump connection.","Midean asetukset puuttuvat.":"Midea connection settings are missing.","V3-tilanluku onnistui.":"V3 status polling succeeded.","Tarkista pumpun IP-osoite.":"Check the heat pump IP address.","Midean tunnistetiedot ovat virheelliset.":"Midea device credentials are invalid.","Ei TCP-yhteyttä Mideaan (portti 6444).":"Cannot connect to Midea on TCP port 6444.","V3-tunnistautuminen epäonnistui: tarkista token ja key.":"V3 authentication failed: check token and key.","Midean V3-protokollavastaus ei kelpaa.":"Midea V3 protocol response is invalid.","Pumpun tilavastausta ei voitu tulkita.":"Cannot parse the heat pump state response.","Laitteen kellonaika ei ole vielä synkronoitunut.":"Device clock has not yet synchronized.","Tuntematon Midea-yhteysvirhe.":"Unknown Midea connection error.","Yhteystesti käynnissä. ":"Connection test running. "," Viimeisin yritys: ":" Last attempt: ","Yhteystesti käynnistetty. Pumppua ei ohjata.":"Connection test started. Heat pump control remains disabled.","Yhteystestiä ei voitu käynnistää.":"Could not start the connection test.","Firmwarepäivitys":"Firmware update","Päivitä Lite suoraan dashboardista. Laite tarkistaa varmennetulta HTTPS-päivityspalvelimelta uuden allekirjoitetun version ja asentaa sen palautettavalle OTA-osiolle. Pumpun asetuksia ei muuteta. Tavallisen .bin-tiedoston lataus selaimesta ei ole käytössä.":"Update Lite directly from the dashboard. The device checks a verified HTTPS update server for a signed release and installs it to the rollback-capable OTA partition. Heat pump settings are unchanged. Uploading an ordinary .bin file from the browser is not supported.","Nykyinen firmware":"Current firmware","Hyväksytty OTA-versio":"Confirmed OTA version","Viimeksi tarkistettu julkaisu":"Latest checked release","Viimeisin tarkistus":"Last check","Päivityksen tila":"Update status","Ladattu":"Downloaded","Tarkista ja asenna päivitys":"Check and install update","Päivitä tilatiedot":"Refresh status","Ladataan OTA-tilaa…":"Loading OTA status…","Ei uutta hyväksyttyä versiota":"No newer eligible release","Päivityskanava pois käytöstä":"Update channel disabled","Odottaa Wi-Fi-yhteyttä tai kellonaikaa":"Waiting for Wi-Fi or correct time","Tilapäinen resurssi- tai tallennusvirhe":"Temporary resource or storage error","Julkaisun tarkistus tai lataus epäonnistui":"Release verification or download failed","Allekirjoitettu päivitys latautuu":"Downloading signed update","Päivitys valmis, laite käynnistyy uudelleen":"Update ready, device restarting","Ei vielä vahvistettua julkaisua":"No confirmed release yet","Ei vielä tarkistettu":"Not checked yet","Tarkistus jonossa":"Check queued","Tuntematon tila":"Unknown status","Päivitys käyttää vain allekirjoitettua julkaisua ja säilyttää edellisen toimivan OTA-version.":"Only signed releases are installed, preserving the previous working OTA image.","Allekirjoitettua OTA-päivityskanavaa ei ole tällä laitteella. Päivitystä ei voi käynnistää ennen sen käyttöönottoa.":"A signed OTA update channel is not configured on this device. Updates cannot be started until it is provisioned.","Tarkistetaanko allekirjoitettu firmware ja asennetaanko uusi versio, jos sellainen löytyy? Laite voi käynnistyä uudelleen.":"Check signed firmware and install a new release if available? The device may restart.","Päivitystarkistus lähetetään laitteelle…":"Requesting firmware update check…","Tarkistus käynnistetty. Uusi allekirjoitettu julkaisu asennetaan, jos se on saatavilla.":"Update check started. A newer signed release will be installed if available.","Päivitystarkistuksen käynnistys epäonnistui: ":"Could not start firmware update check: ","Midea · paikallinen V2 / V3 Wi-Fi -yhteys":"Midea · local V2 / V3 Wi-Fi connection","V2- tai V3-tilanluku tapahtuu suoraan lämpöpumpun Wi-Fi-moduulista ilman Home Assistantia tai jatkuvaa pilviyhteyttä.":"V2 or V3 status is read directly from the heat pump Wi-Fi module without Home Assistant or a persistent cloud connection.","Midea-protokolla":"Midea protocol","V2 (ei tokenia)":"V2 (no token)","Midean automaattinen optimointiohjaus ei ole vielä käytössä; manuaalinen ohjaus löytyy Midea-asetuksista.":"Midea automatic optimizer control is not enabled yet; manual control is available in Midea settings.","V3-tunnisteet tallennetaan paikalliseen NVS-muistiin, eikä niitä palauteta selaimeen. V2 ei tarvitse tokenia tai keytä, mutta sen yhteys käyttää yhteistä kiinteää salausavainta eikä todenna pumppua samalla tavalla kuin V3. Valitse protokolla laitteen todetun version mukaan. Käytä vain luotetussa lähiverkossa. DHCP-IP:n automaattinen tunnistus ja automaattinen Midea-optimointiohjaus eivät vielä sisälly tähän testitoteutukseen.":"V3 credentials are stored in local NVS and are not returned to the browser. V2 does not need a token or key, but uses a shared static encryption key and does not authenticate the device like V3. Select the device’s verified protocol. Use only on a trusted LAN. Automatic DHCP IP rediscovery and heat pump control are not included.","Midean tilanluku onnistui.":"Midea state query succeeded.","V2-yhteys onnistui":"V2 connection successful","Tarkista Midean device ID. V2 ei tarvitse tokenia eikä keytä.":"Check the Midea device ID. V2 needs no token or key.","V2- tai V3-tilanluku tapahtuu suoraan lämpöpumpun Wi-Fi-moduulista ilman Home Assistantia tai jatkuvaa pilviyhteyttä. Midean manuaalinen lämpötilan ja toimintatilan ohjaus on nyt kokeellinen; automaattisen optimoinnin komentoja ei vielä lähetetä. Yhdistä vain omassa luotetussa lähiverkossa; kehitysvaiheen HTTP-asetussivu ei salaa tunnisteiden siirtoa.":"V2/V3 status is read directly from the heat pump Wi-Fi module without Home Assistant or persistent cloud connection. Manual Midea setpoint and mode control is experimental; automatic optimizer commands are not sent yet. Connect only on your own trusted local network; this development HTTP setup does not encrypt the credentials in transit.","Midean paikallinen ohjaus · V2 / V3":"Midea local control · V2 / V3","Yksittäiset muutokset lähetetään suoraan pumpulle alkuperäistä Wi-Fiä käyttäen. Tunnetut muut asetukset säilytetään viimeksi luetusta tilasta. Käsky on manuaalinen; pörssisähköoptimoinnin automaattista Midea-käskynlähetystä ei ole vielä kytketty.":"Individual changes are sent directly to the pump using its original Wi-Fi. Other known settings are preserved from the latest state. Commands are manual; Midea automation by spot-price optimizer is not yet connected.","Virta":"Power","Säilytä nykyinen":"Keep current","Päälle":"On","Pois":"Off","Jäähdytys":"Cool","Kuivaus":"Dry","Puhallus":"Fan","Pyynti °C":"Setpoint °C","Säilytä":"Keep current","Hiljainen":"Silent","Matala":"Low","Keskitaso":"Medium","Korkea":"High","Täysi":"Full","Pysty":"Vertical","Vaaka":"Horizontal","Molemmat":"Both","Lähetä ohjaus":"Send control","Odota ensin onnistunutta tilanlukua.":"Wait for a successful state reading first.","Midea 0x40 -komento on koko asetustilan viesti. Tämä toteutus käyttää viimeisintä laitteelta luettua tilaa, lähettää yhden komennon ja tarkistaa tuloksen uudella tilanluvulla. Laitteen omat ajastukset ja mallikohtaiset lisäominaisuudet saattavat käyttäytyä eri tavoin, eikä vielä ole vahvistettua Lite-kenttätestiä.":"Midea 0x40 is a full-state command. The controller reads the pump state, sends one command and verifies it by a fresh query. Native timers and model-specific extras may behave differently; Lite field testing is pending.","Midean manuaalinen lämpötilan ja toimintatilan ohjaus on nyt kokeellinen; automaattisen optimoinnin komentoja ei vielä lähetetä.":"Manual Midea setpoint and mode control is experimental; automatic optimization commands are not yet sent.","DHCP-IP:n automaattinen tunnistus ja automaattinen Midea-optimointiohjaus eivät vielä sisälly tähän testitoteutukseen.":"Automatic DHCP IP discovery and automatic Midea optimization are not yet included in this test implementation.","Ohjausta ei vielä lähetetty.":"No control command has been sent.","Pumppu ei vahvistanut muutosta takaisinluvussa.":"Pump did not confirm the requested change in readback.","Ohjauksen suorittamiseen ei ollut resursseja.":"Resources unavailable to execute control.","Pumpun ohjaus vahvistettu takaisinluvulla.":"Pump control confirmed by a fresh readback.","Ohjaus odottaa suorittamista.":"Control queued.","Ohjausta suoritetaan.":"Control in progress.","Asetustunniste puuttuu.":"Settings token unavailable.","Valitse ensin vähintään yksi muutettava asetus.":"Select at least one setting to change.","Lähetetään manuaalinen Midea-ohjaus…":"Sending manual Midea control…","Ohjaus jonossa. Pumpun vastaus ja takaisinluku tarkistetaan.":"Control queued; verifying pump response and readback.","Ohjausta ei voitu käynnistää: ":"Could not queue control: ","Manuaalinen":"Manual","Kokeellinen manuaaliohjaus":"Experimental manual control","Midean mallikohtainen koekäyttöhyväksyntä":"Model-specific Midea pilot approval","Käyttöönotto koskee vain yhtä asetettua laitetta ja käynnistyskertaa. Anna pumpun tarkka mallimerkintä sekä mallille varmistetut pyynnin rajat. Tämä ei luo salasanaa tai poista ohjausominaisuuksia.":"Approval applies only to one configured device and one boot session. Enter the exact pump model and the model-verified setpoint limits. No password or feature removal is introduced.","Pumpun tarkka malli":"Exact pump model","Mallimerkintä":"Model identifier","Mallin minimipyynti °C":"Model minimum setpoint °C","Mallin maksimipyynti °C":"Model maximum setpoint °C","Mallin minimi":"Model minimum","Mallin maksimi":"Model maximum","Hyväksy mallikohtainen ohjaustesti":"Approve model-specific control test","Peru koekäyttöhyväksyntä":"Revoke pilot approval","Mallikohtaista ohjaustestiä ei ole hyväksytty.":"Model-specific control test not approved.","Mallikohtainen ohjaustesti hyväksytty. Pyyntirajat: ":"Model pilot approved. Setpoint limits: ","Tarkista malli, device ID ja mallikohtaiset lämpötilarajat.":"Check model, device ID, and model-specific setpoint limits.","Hyväksytkö Midea ":"Do you approve a manual control pilot for Midea "," -pumpun manuaalisen ohjauskokeen tälle laitteelle?":" on this configured device?","Mallin manuaaliohjaus hyväksytty.":"Manual control pilot approved.","Koekäyttöhyväksyntä peruttu.":"Pilot approval revoked.","Koekäytön asetusta ei voitu muuttaa: ":"Could not change pilot approval: ","Valitun alueen 15 minuutin hinnat. Kaavio näyttää vain ladatun tulevan datan.":"15-minute prices for the selected area. The chart shows only downloaded future data.","Tarkista sopimuksen luvut: käytä vain numeroita.":"Check the contract numbers: use digits only.","Arvonlisävero on enintään 50 %.":"VAT can be at most 50 %.","Hintapyyntö oli virheellinen.":"The price request was invalid.","Hintapalvelun vastaus ei ollut luettavissa.":"The price service response could not be read.","Hintapalvelu palautti väärän alueen.":"The price service returned the wrong area.","Hintapalvelu palautti väärän valuutan.":"The price service returned the wrong currency.","Hintapalvelun vastaus oli epätäydellinen.":"The price service response was incomplete.","Hintapalvelun vastaus sisälsi virheellisen arvon.":"The price service response contained an invalid value.","Hintapalvelun vastaus oli liian suuri.":"The price service response was too large.","Laitteen muisti ei riittänyt hintahakuun juuri nyt.":"The device did not have enough free memory for the price request right now.","Hintapalveluun ei saatu yhteyttä.":"Could not reach the price service.","Hintapalvelu ei ole vielä julkaissut tämän päivän hintoja tälle alueelle.":"The price service has not published today's prices for this area yet.","Laite on kotiverkossa.":"The device is on your home network.","Sijainti ja sääennuste":"Location and weather forecast","ennustetta ei ole vielä haettu.":"the forecast has not been fetched yet.","Sijaintia ei ole valittu.":"No location has been chosen.","Sähkömarkkina-aluetta ei ole valittu.":"No electricity market area has been chosen.","Hintoja ei ole vielä haettu.":"Prices have not been fetched yet.","Sähkömarkkina-alue ja hinnat":"Market area and prices","Sähkösopimus":"Electricity contract","Sopimuksen hinnat on annettu.":"The contract prices have been entered.","Sopimustasi ei ole vahvistettu: kokonaishinta käyttää oletusarvoja.":"Your contract is not confirmed: the total price uses default values.","Lämpöpumppu":"Heat pump","Yhteys toimii ja tietoa tulee.":"The connection works and data is arriving.","Valittu, mutta yhteyttä ei ole vielä todennettu.":"Selected, but the connection has not been verified yet.","Lämpöpumppua ei ole valittu.":"No heat pump has been selected.","Huonelämpötila-anturi":"Room temperature sensor","Tuore RuuviTag-mittaus.":"Fresh RuuviTag measurement.","Ei tuoretta anturia: optimointi ei käytä pumpun omaa lukemaa huonelämpötilana.":"No fresh sensor: optimization does not treat the pump's own reading as room temperature.","Sähkönkulutusmittari (valinnainen)":"Power meter (optional)","Mittari antaa lukemia.":"The meter is reporting readings.","Ei mittaria. Voit lisätä sen myöhemmin.":"No meter. You can add one later.","Optimoinnin valmius":"Optimization readiness","Suunnitelma on laskettu.":"A plan has been calculated.","Optimointi odottaa hintoja, ennustetta ja mittausta.":"Optimization is waiting for prices, a forecast and a measurement.","Kirjoita vähintään kolme kirjainta.":"Type at least three letters.","Haku on kesken. Odota hetki ja yritä uudelleen.":"A search is already running. Wait a moment and try again.","Laite ei saanut yhteyttä karttapalveluun. Anna koordinaatit käsin.":"The device could not reach the map service. Enter the coordinates by hand.","Karttapalvelu ei vastannut oikein. Yritä myöhemmin tai anna koordinaatit käsin.":"The map service did not answer correctly. Try later or enter the coordinates by hand.","Karttapalvelun vastaus ei kelvannut. Anna koordinaatit käsin.":"The map service response was not usable. Enter the coordinates by hand.","Laitteen muisti ei riittänyt hakuun juuri nyt. Yritä hetken kuluttua.":"The device did not have enough free memory for the search right now. Try again shortly.","Laite ei vastaa tai valtuutus puuttuu. Päivitä sivu.":"The device is not responding or authorization is missing. Reload the page.","Kieli ja maa":"Language and country","Valitse käyttökieli ja maa, jossa lämpöpumppu sijaitsee. Maa määrää sähkömarkkina-alueet ja valuutan.":"Choose the language and the country where the heat pump is located. The country determines the electricity market areas and the currency.","Valitse maa…":"Choose a country…","Valuutta: ":"Currency: ","sähkömarkkina-alue":"market area","sähkömarkkina-aluetta":"market areas","Maata ei näy listassa? Hintatietoa ei ole vielä saatavilla sieltä; muut toiminnot voi ottaa käyttöön ilman sitä.":"Your country is not listed? No price data is available there yet; the other functions can be used without it.","Valitse maa.":"Choose a country.","Sijainti":"Location","Sääennuste haetaan sijainnillesi. Kirjoita kaupunki tai kunta ja valitse ehdotuksista.":"The weather forecast is fetched for your location. Type a city or municipality and pick from the suggestions.","Kaupunki tai kunta":"City or municipality","Hakutulokset":"Search results","Valittu: ":"Selected: ","Anna koordinaatit käsin":"Enter coordinates by hand","Leveys, pituus (esim. 61.4638, 24.0760)":"Latitude, longitude (e.g. 61.4638, 24.0760)","Koordinaatit":"Coordinates","Käytä pisteitä desimaalierottimena. Sijainnin voi muuttaa myöhemmin asetuksista.":"Use a dot as the decimal separator. You can change the location later in settings.","Haetaan…":"Searching…","Laite ei vastannut. Yritä uudelleen.":"The device did not respond. Try again.","Haku epäonnistui.":"The search failed.","Ei hakutuloksia. Tarkista kirjoitusasu tai anna koordinaatit käsin.":"No results. Check the spelling or enter the coordinates by hand.","Haku kestää odotettua kauemmin. Yritä uudelleen.":"The search is taking longer than expected. Try again.","Valitse kaupunki hakutuloksista tai anna koordinaatit.":"Pick a city from the results or enter the coordinates.","Sähkömarkkina-alue":"Electricity market area","Valitse ensin maa.":"Choose a country first.","Maassasi on yksi sähkömarkkina-alue; se on valittu valmiiksi.":"Your country has one market area; it is already selected.","Maassasi on useita sähkömarkkina-alueita. Valitse sähkösopimuksesi alue (näkyy laskussa). Aluetta ei arvata kaupungista.":"Your country has several market areas. Choose the area of your electricity contract (shown on your bill). The area is not guessed from the city.","Hinnat haetaan valuutassa ":"Prices are fetched in "," Nord Poolin päivänsisäisistä hinnoista.":" from the Nord Pool day-ahead market.","Valitse sähkömarkkina-alue.":"Choose a market area.","Pörssihinta ei ole se hinta, jonka maksat. Anna sopimuksesi hinnat laskusta. Ilman niitä kokonaishinta käyttää oletusarvoja ja merkitään vahvistamattomaksi.":"The spot price is not the price you pay. Enter your contract prices from your bill. Without them the total price uses default values and is marked unconfirmed.","Sähkönmyyjän marginaali":"Retailer margin","Siirto, päivä":"Transfer, day","Siirto, yö":"Transfer, night","Sähkövero":"Electricity tax","Arvonlisävero (%)":"VAT (%)","Sisältävätkö hinnat arvonlisäveron":"Whether prices include VAT","Hinnat ilman arvonlisäveroa":"Prices excluding VAT","Hinnat sisältävät arvonlisäveron":"Prices including VAT","Verollisuus":"VAT treatment","Kuukausimaksut eivät vaikuta tuntihintaan, joten niitä ei kysytä. Päivä- ja yöaikojen rajat sekä kausisiirto löytyvät Asetuksista.":"Monthly fees do not affect the hourly price, so they are not asked. Day/night limits and seasonal transfer are in Settings.","Valitse lämpöpumppusi. Näytetään vain ne yhteydet, jotka tässä ohjelmistossa on toteutettu; kokeelliset on merkitty.":"Choose your heat pump. Only connections implemented in this software are shown; experimental ones are marked.","Tämä ohjelmisto ei vielä sisällä valittavia lämpöpumppuyhteyksiä.":"This software does not include any selectable heat pump connections yet.","Osoite on kotiverkkosi paikallinen osoite. Lämpöpumppuun ei lähetetä komentoja ennen kuin otat automaattiohjauksen erikseen käyttöön Asetuksista.":"The address is a local address on your home network. No commands are sent to the heat pump until you separately enable automatic control in Settings.","Midean paikallinen yhteys (V2/V3) viimeistellään Asetukset-näkymän Midea-kortissa, kun käyttöönotto on valmis.":"The local Midea connection (V2/V3) is finished in the Midea card in Settings once setup is complete.","Anturit ja kulutusmittari":"Sensors and power meter","Huonelämpötila tulee RuuviTag-anturilta. Ilman sitä Lite ei pidä pumpun omaa lukemaa todistettuna huonelämpötilana.":"Room temperature comes from a RuuviTag sensor. Without it Lite does not treat the pump's own reading as a proven room temperature.","Sidottu anturi: ":"Bound sensor: ","ei sidottu":"not bound","Havaittu: ":"Detected: ","ei havaintoa":"no detection","Anturin sidonta ja nimeäminen tehdään Asetukset-näkymän RuuviTag-kortissa.":"Sensor binding and naming is done in the RuuviTag card in Settings.","Shelly-mittarin osoite (valinnainen)":"Shelly meter address (optional)","Mittarin tulee mitata vain lämpöpumppua, ei koko taloa. Lite ei ohjaa Shellyn relettä.":"The meter must measure only the heat pump, not the whole house. Lite does not control the Shelly relay.","Yhteenveto":"Summary","Seuraavat asiat vaativat vielä toimenpiteitä:":"These items still need action:","Kaikki on kunnossa.":"Everything is in order.","Dashboardiin pääsee, vaikka kaikki ei olisi valmista. Jatka asetuksia myöhemmin Asetukset-näkymässä.":"You can enter the dashboard even if not everything is ready. Continue setting up later in Settings.","Anturit ja mittari":"Sensors and meter","Valmis, avaa dashboard":"Done, open the dashboard","Jatka":"Continue","Maalistaa ei voitu lukea laitteelta. Päivitä sivu.":"The country list could not be read from the device. Reload the page.","Ohjattu käyttöönotto":"Guided setup","Vaihda maa, sijainti, sähkömarkkina-alue tai lämpöpumppu vaihe vaiheelta. Nykyiset asetukset säilyvät, kunnes tallennat uuden valinnan.":"Change the country, location, market area or heat pump step by step. Current settings stay until you save a new choice.","Avaa käyttöönotto uudelleen":"Open setup again","Enevina Lite · käyttöönotto":"Enevina Lite · setup","Sulje ja jatka myöhemmin":"Close and continue later","Ohita tämä vaihe":"Skip this step","Hintoja ei voitu hakea.":"Prices could not be fetched."," Viimeisin onnistunut haku: ":" Last successful fetch: ","Maa":"Country","Takaisin":"Back","Esim. Kangasala":"E.g. Kangasala","Midean paikallinen yhteys tarkistetaan Asetukset-näkymän Midea-kortissa.":"The local Midea connection is checked in the Midea card in Settings.","Tietoa tulee, mutta UART-lähdettä ei ole varmennettu.":"Data is arriving, but the UART source has not been verified.","Valittu, mutta tuoretta tietoa ei tule.":"Selected, but no fresh data is arriving.","Mukavuus":"Comfort","Pitää lämpötilan tasaisena ja reagoi vain suuriin hintaeroihin.":"Keeps the temperature steady and reacts only to large price differences.","Tasapainoinen":"Balanced","Käyttää osan sallitusta vaihteluvälistä: lämmittää hieman ennakkoon edullisella sähköllä ja venyttää kalliilla.":"Uses part of the allowed range: pre-heats a little when electricity is cheap and coasts when it is expensive.","Säästö":"Saving","Käyttää sallitun vaihteluvälin tehokkaammin ja vertaa hintaa pidemmälle. Lämpötila vaihtelee enemmän, mutta pysyy asettamiesi rajojen sisällä.":"Uses the allowed range more fully and compares prices over a longer window. The temperature varies more but stays within the limits you set.","Valitse optimointiprofiili.":"Choose an optimization profile.","Valitse ensin lämmitystila (Ylläpitolämpö tai Asuttu) Asetuksista, jotta tavoitelämpötila voidaan asettaa.":"First choose a heating mode (Maintenance or Occupied) in Settings so the target temperature can be set.","Tavoitelämpötilan tulee olla ":"The target temperature must be "," °C ja puolen asteen tarkkuudella.":" °C in steps of 0.5.","Rajojen tulee olla 5–26 °C ja puolen asteen tarkkuudella.":"Limits must be 5–26 °C in steps of 0.5.","Pienimmän ja suurimman lämpötilan välissä tulee olla vähintään 1 °C.":"There must be at least 1 °C between the minimum and maximum temperature.","Pienimmän sallitun lämpötilan tulee olla vähintään 0,5 °C tavoitetta alempi.":"The minimum allowed temperature must be at least 0.5 °C below the target.","Suurimman sallitun lämpötilan tulee olla vähintään 0,5 °C tavoitetta ylempi.":"The maximum allowed temperature must be at least 0.5 °C above the target.","Optimointiprofiili":"Optimization profile","Ei omia lämpötilarajoja: käytetään tavoite ±0,5 °C.":"No own temperature limits: using target ±0.5 °C.","Lämpötila ja optimointi":"Temperature and optimization","Aseta haluttu lämpötila ja rajat, joiden sisällä Lite saa hyödyntää sähkön hinnanvaihtelua.":"Set the desired temperature and the limits within which Lite may use electricity price variations.","Haluttu sisälämpötila (°C)":"Desired indoor temperature (°C)","Asuttu-tila: ":"Occupied mode: ","Ylläpitolämpö-tila: ":"Maintenance mode: ","Lämmitystilaa (Ylläpitolämpö tai Asuttu) ei ole valittu; tavoite asetetaan Asetukset-näkymässä.":"No heating mode (Maintenance or Occupied) is selected; the target is set in Settings.","Pienin sallittu lämpötila (°C, valinnainen)":"Minimum allowed temperature (°C, optional)","Suurin sallittu lämpötila (°C, valinnainen)":"Maximum allowed temperature (°C, optional)","Lite ei lupaa etukäteen tiettyä säästöä. Rajat pysyvät samoina kaikissa profiileissa; profiili määrää vain, kuinka paljon vaihteluväliä ja hintaeroa käytetään. Automaattiohjaus ei käynnisty tästä, vaan se otetaan erikseen käyttöön Asetuksista.":"Lite does not promise a specific saving in advance. The limits are the same in every profile; the profile only decides how much of the range and of the price difference is used. Automatic control is not started from here; it is enabled separately in Settings.","Lämpötilaraja on ristiriidassa valitun tavoitelämpötilan kanssa.":"The temperature limit conflicts with the selected target temperature.","Mukavuusasetuksia ei voitu vahvistaa laitteen muistista.":"Comfort settings could not be verified in device storage.","Keskeytynyt tallennus odottaa turvallista palautusta. Yritä tallentaa asetukset uudelleen.":"An interrupted save needs safe recovery. Retry saving the settings.","Keskeytyneen tallennuksen muistitietoa ei voida todentaa.":"An interrupted save could not be verified from device storage.","Tallenna muut asetukset erikseen lämpötilatavoitteesta.":"Save unrelated settings separately from the temperature target."});
const UI_EN_FRAGMENTS=Object.entries(UI_EN)
  .filter(([key,value])=>key.length>=8 && key!==value)
  .sort((a,b)=>b[0].length-a[0].length);
function uiLocale(){return uiLanguage==='en'?'en-GB':'fi-FI';}
// Civil time zone and price currency follow the configured country.
let uiTz='Europe/Helsinki',uiCurrency='EUR';
function uiTimeZone(){return regionTimeZone();}
function uiMinorUnit(){
  const m={SEK:'öre',NOK:'øre',DKK:'øre',PLN:'gr'}[uiCurrency];
  return m||(uiLanguage==='en'?'c':'snt');
}
function setRegionDisplay(tz,currency){
  const changed=(tz&&tz!==uiTz)||(currency&&currency!==uiCurrency);
  if(tz&&/^[A-Za-z0-9_\/+-]{3,39}$/.test(tz)){
    try{new Intl.DateTimeFormat('en-GB',{timeZone:tz});uiTz=tz;
      try{localStorage.setItem('enevina_tz',tz);}catch(_){}}catch(_){}
  }
  if(currency&&/^[A-Z]{3}$/.test(currency))uiCurrency=currency;
  if(changed&&typeof translateDashboard==='function'){
    if(state.status)showStatus();if(state.prices)showPrices();if(state.weather)showWeather();translateDashboard();
  }
}
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
function unitize(value){
  const unit=uiMinorUnit();
  if(unit==='snt'&&uiLanguage!=='en')return value;
  return value.replace(/\b(?:snt|c)\/kWh/g,unit+'/kWh');
}
function translateTextNode(node){
  let current=node.nodeValue,record=uiOriginalText.get(node);
  if(!record||record.rendered!==current)
    record={fi:current,rendered:current};
  const next=unitize(uiLanguage==='en'?translateFi(record.fi):record.fi);
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
  return fallback[r.country]||(typeof uiTz==='string'?uiTz:'Europe/Helsinki');
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
  text('price-area-chip','Nord Pool · '+(s.price_area||'—')+(s.price_currency?' · '+s.price_currency:''));
  if(s.price_error&&typeof WZ_PRICE_ERRORS!=='undefined')
    text('price-updated',(WZ_PRICE_ERRORS[s.price_error]||'Hintoja ei voitu hakea.')+' Viimeisin onnistunut haku: '+time(s.market_last_fetch_utc));
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
const siteErrors={BAD_NUMBER:'Tarkista numero',OUT_OF_RANGE:'Arvo on sallitun alueen ulkopuolella',BAD_URL:'Osoite ei kelpaa (vain paikallinen http-osoite)',TOO_LONG:'Liian pitkä arvo',BAD_CHOICE:'Virheellinen valinta',ADAPTER_NOT_SUPPORTED:'Laitetta ei vielä tueta',INVALID:'Asetukset eivät ole kelvolliset',NVS:'Tallennus laitteen muistiin epäonnistui',
COMFORT_TARGET_CONFLICT:'Lämpötilaraja on ristiriidassa valitun tavoitelämpötilan kanssa.',
COMFORT_NVS:'Mukavuusasetuksia ei voitu vahvistaa laitteen muistista.',
COMFORT_RECOVERY_REQUIRED:'Keskeytynyt tallennus odottaa turvallista palautusta. Yritä tallentaa asetukset uudelleen.',
COMFORT_JOURNAL_UNVERIFIED:'Keskeytyneen tallennuksen muistitietoa ei voida todentaa.',
COMFORT_FIELDS:'Tallenna muut asetukset erikseen lämpötilatavoitteesta.',
TOKEN:'Päivitä sivu ja yritä uudelleen'};
const maturityText={FIELD_PILOT:'Kenttäpilotti',HOST_TESTED_ONLY:'Vain PC-testattu',FIRMWARE_READ_ONLY:'Lukuyhteys (testivaihe)',FIRMWARE_CONTROL_EXPERIMENTAL:'Kokeellinen manuaaliohjaus',DOCUMENTED_ONLY:'Vain selvitetty'};
async function reloadSite(){try{const s=await fetchJSON('/api/settings');state.site=s;const t=s.tariff;
 if(s.region)setRegionDisplay(s.region.timezone,s.region.currency);
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
async function initialRefresh(){await reloadStatus();await reloadPrices();await reloadWeather();await reloadPlan();await reloadHistory();await reloadSite();await reloadMidea();
  if(typeof wizardMaybeOpen==='function')wizardMaybeOpen();}
document.getElementById('wz-reopen').addEventListener('click',()=>{WZ.dismissed=false;wzOpen(true);});
document.getElementById('wz-next').addEventListener('click',()=>wzNext(false));
document.getElementById('wz-skip').addEventListener('click',()=>wzNext(true));
document.getElementById('wz-back').addEventListener('click',()=>{if(WZ.step>0){WZ.step--;WZ.message='';wzRender();}});
document.getElementById('wz-close').addEventListener('click',()=>{WZ.dismissed=true;wzClose();});
window.addEventListener('keydown',e=>{if(e.key==='Escape'&&WZ.open){WZ.dismissed=true;wzClose();}});
translateDashboard();
initialRefresh();
setInterval(reloadStatus,15000);
setInterval(reloadMidea,30000);
setInterval(async()=>{await reloadPrices();await reloadWeather();await reloadPlan();},60000);
setInterval(reloadHistory,180000);
</script>
</body>
</html>)ENEVINA_UI";