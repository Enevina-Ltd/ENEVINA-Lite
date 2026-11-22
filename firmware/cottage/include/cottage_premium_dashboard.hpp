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
.span7{grid-column:span 7}.span6{grid-column:span 6}.span5{grid-column:span 5}.span4{grid-column:span 4}.span8{grid-column:span 8}.span12{grid-column:span 12}
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
@media(max-width:600px){.span7,.span6,.span5,.span4,.span8,.span12{grid-column:span 12}.metric{font-size:39px}.temperature .metric{font-size:56px}.mode{grid-template-columns:repeat(2,minmax(0,1fr))}.mode button{padding:10px 5px;font-size:12px}.chip{font-size:11px}.bars{height:155px}}
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
.wz-body h3{font-size:1rem;margin:1.1rem 0 .3rem}.wz-sensor{border:1px solid #c5d2c8;border-radius:12px;padding:10px 12px;margin:8px 0;background:var(--bg);display:grid;gap:4px}
.wz-sensor.stale{opacity:.82;border-style:dashed}.wz-sensor input{min-height:42px}.wz-srow{display:flex;gap:8px;align-items:center;justify-content:space-between}
.wz-badge{background:var(--green);color:#fff;border-radius:99px;padding:2px 9px;font-size:12px;white-space:nowrap}
:root[data-theme="night"] .wz-sensor{background:#0f1f1a;border-color:#2f4a40}:root[data-theme="night"] .wz-badge{background:#2d7459}
@media(max-width:600px){.wz{padding:0}.wz-card{border-radius:0;min-height:100%;max-width:none}}
.tl-chart{position:relative;margin:10px 0 4px}.tl-svg{width:100%;height:auto;display:block;touch-action:pan-y}
.tl-bar.tl-cheap,.tl-sw.tl-cheap,.tl-lane.tl-cheap{fill:#2f9e6e;background:#2f9e6e}.tl-bar.tl-neutral,.tl-sw.tl-neutral,.tl-lane.tl-neutral{fill:#a7b4ac;background:#a7b4ac}
.tl-bar.tl-expensive,.tl-sw.tl-expensive,.tl-lane.tl-expensive{fill:#d9822b;background:#d9822b}.tl-bar.tl-unknown{fill:#d6ded9}
.tl-grid{stroke:var(--edge);stroke-width:1;stroke-dasharray:3 4}.tl-axis{font-size:11px;fill:var(--muted)}.tl-day{font-size:11px;fill:var(--ink);font-weight:650}
.tl-outdoor-dot{fill:#3b6fb6}:root[data-theme="night"] .tl-outdoor-dot{fill:#7fb0f0}.tl-outdoor{stroke:#3b6fb6;stroke-width:2.2;stroke-linejoin:round}.tl-outdoor-text{fill:#3b6fb6}.tl-now{stroke:var(--ink);stroke-width:1.5}.tl-now-text{font-size:11px;fill:var(--ink);font-weight:650}
.tl-cursor{stroke:var(--ink);stroke-width:1;opacity:.6}.tl-lane-n{font-size:9px;fill:#fff;font-weight:700}.tl-hit{cursor:crosshair}
.tl-tip{position:absolute;top:6px;width:220px;background:var(--paper);color:var(--ink);border:1px solid var(--edge);border-radius:10px;padding:8px 10px;font-size:12px;box-shadow:var(--shadow);pointer-events:none;display:grid;gap:3px}.tl-tip[hidden]{display:none}
.tl-legend{display:flex;gap:14px;flex-wrap:wrap;font-size:12px;color:var(--muted);margin:4px 0}.tl-sw{display:inline-block;width:12px;height:12px;border-radius:3px;margin-right:5px;vertical-align:-1px}.tl-sw.tl-line{height:3px;background:#3b6fb6;vertical-align:3px}
.tl-h3{font-size:14px;margin:12px 0 4px}.tl-reasons{margin:0;padding-left:1.3rem;display:grid;gap:7px;font-size:13px}
:root[data-theme="night"] .tl-bar.tl-cheap,:root[data-theme="night"] .tl-lane.tl-cheap{fill:#4fc08c}:root[data-theme="night"] .tl-bar.tl-expensive,:root[data-theme="night"] .tl-lane.tl-expensive{fill:#f0a04d}
:root[data-theme="night"] .tl-bar.tl-neutral{fill:#5d6f66}:root[data-theme="night"] .tl-bar.tl-unknown{fill:#2b3a34}:root[data-theme="night"] .tl-outdoor{stroke:#7fb0f0}:root[data-theme="night"] .tl-outdoor-text{fill:#7fb0f0}
.hs-ranges{display:flex;gap:6px}.hs-ranges button{font:inherit;min-height:44px;padding:0 14px;border-radius:99px;border:1px solid #c5d2c8;background:var(--paper);color:var(--ink);cursor:pointer}
.hs-ranges button[aria-checked="true"]{background:var(--mint);border-color:var(--green);font-weight:650}
.hs-line{stroke-width:2.2;stroke-linejoin:round;stroke-linecap:round}.hs-in{stroke:var(--green)}.hs-out{stroke:#3b6fb6}.hs-sp{stroke:#c0662b;stroke-width:1.8}.hs-dash{stroke-dasharray:5 4}
.hs-dot.hs-in{fill:var(--green)}.hs-dot.hs-out{fill:#3b6fb6}.hs-dot.hs-sp{fill:#c0662b}.hs-bar{fill:#6f8f80}.hs-bar.hs-power{fill:#4a79c9}
.hs-lane.hs-a0{fill:#d6ded9}.hs-lane.hs-a1,.hs-a1{fill:#a7b4ac;background:#a7b4ac}.hs-lane.hs-a2,.hs-a2{fill:#d9822b;background:#d9822b}.hs-lane.hs-a3,.hs-a3{fill:#c9d8cf;background:#c9d8cf}.hs-armed{fill:#1d6b4f}
.hs-sw-in{background:var(--green)}.hs-sw-out{background:#3b6fb6}.hs-sw-fc{background:repeating-linear-gradient(90deg,#3b6fb6 0 4px,transparent 4px 7px);height:3px;vertical-align:3px}.hs-sw-sp{background:#c0662b}
.hs-armed-dot{display:inline-block;width:8px;height:8px;border-radius:50%;background:#1d6b4f;margin-right:5px}
.hs-events{list-style:none;margin:8px 0;padding:0;display:grid;gap:6px;max-height:340px;overflow:auto}.hs-events li{display:flex;gap:10px;padding:8px 10px;border-radius:10px;background:var(--bg);font-size:13px}
.hs-events li.bad{border-left:4px solid var(--red)}.hs-events li.ok{border-left:4px solid var(--green)}.hs-events li.info{border-left:4px solid #a7b4ac}.hs-events time{color:var(--muted);white-space:nowrap}
:root[data-theme="night"] .hs-in{stroke:#4fc08c}:root[data-theme="night"] .hs-out{stroke:#7fb0f0}:root[data-theme="night"] .hs-sp{stroke:#f0a04d}:root[data-theme="night"] .hs-events li{background:#0f1f1a}
:root[data-theme="night"] .hs-ranges button{background:#162a24;border-color:#2f4a40;color:#e3efe8}:root[data-theme="night"] .hs-ranges button[aria-checked="true"]{background:#1d4637;border-color:#5fc39a}
.fr-list{list-style:none;margin:10px 0;padding:0;display:grid;gap:8px}.fr-list li{display:flex;gap:10px;align-items:flex-start;padding:9px 11px;border-radius:12px;background:var(--bg);font-size:13px}
.fr-dot{flex:none;width:11px;height:11px;border-radius:50%;margin-top:4px;background:#a7b4ac}.fr-dot.ok{background:#2f9e6e}.fr-dot.warn{background:#e3b341}.fr-dot.bad{background:var(--red)}
.callout.bad{background:#fbeceb;border-color:#e5b9b4;color:#7c2a20}.callout.ok{background:var(--mint);border-color:#b9d9c3;color:var(--deep)}.callout.busy,.callout.info{background:#eef3fa;border-color:#c8d6ea;color:#25446f}
:root[data-theme="night"] .fr-list li{background:#0f1f1a}:root[data-theme="night"] .callout.bad{background:#3a1f1c;border-color:#7a3a32;color:#f3c1b9}:root[data-theme="night"] .callout.ok{background:#1d4637;border-color:#2f6d54;color:#d8f5e4}
:root[data-theme="night"] .callout.busy,:root[data-theme="night"] .callout.info{background:#1a2a3f;border-color:#2f4a6d;color:#c9dcf5}
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
    <button data-view="history"><span class="i">◔</span>Historia</button>
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
      <article class="card temperature span7"><div><div class="eyebrow" style="color:#c5ded1">Sisälämpötila</div><div class="metric" id="home-temp">—</div></div><div><span class="pill" id="home-mode">Tila ei tiedossa</span><p class="tiny muted" id="home-sensor" style="margin:12px 0 0">Odotetaan mittausta</p></div></article>
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
      <article class="card span12" id="fresh-card"><div class="flex"><h2>Tietojen tuoreus ja toiminta ilman verkkoa</h2><span class="chip" id="fresh-contact">—</span></div>
        <div class="callout" id="fresh-overall" role="status"></div>
        <ul id="fresh-list" class="fr-list"></ul>
        <p class="tiny muted">Vanhentunutta hintaa tai ennustetta ei käytetä uutena tietona. Kun verkko katkeaa, laite jatkaa tallennetuilla tiedoilla niiden voimassaoloajan loppuun ja pitää sen jälkeen huoneen termostaattina; pumpun oma säädin ja turvatoiminnot toimivat aina.</p></article>
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
    <div class="grid"><article class="card span12" id="timeline-card"><div class="flex"><h2>Hintojen luokittelu ja ohjauksen liikkumavara</h2><span class="chip" id="tl-profile"></span></div>
        <p class="tiny muted">Hinnat luokitellaan samalla säännöllä kuin ohjaus: jakso on edullinen tai kallis, kun se poikkeaa vertailuikkunan mediaanista vähintään kynnyksen verran.</p>
        <div class="callout" id="tl-now" role="status"></div>
        <div id="tl-empty" class="empty" hidden>Aikajanaa ei voida piirtää: tuoreita ja yhtenäisiä hintoja ei ole saatavilla.</div>
        <div id="tl-chart" class="tl-chart"></div>
        <div class="tl-legend" aria-hidden="true"><span><i class="tl-sw tl-cheap"></i>Edullinen</span><span><i class="tl-sw tl-neutral"></i>Normaali</span><span><i class="tl-sw tl-expensive"></i>Kallis</span><span><i class="tl-sw tl-line"></i>Ulkolämpötilaennuste (oikea asteikko)</span></div>
        <h3 class="tl-h3">Jaksot</h3><ol id="tl-reasons" class="tl-reasons"></ol>
        <p class="tiny muted">Kaavio näyttää, miten ohjaus luokittelee hinnat ja paljonko liikkumavaraa profiili antaa. Huonelämpötilan ennustetta ja kustannusarviota ei näytetä, koska rakennuksen lämpömallia ei ole vielä kalibroitu.</p>
        <p class="tiny muted">Ulkolämpötilaennuste on taustatieto: nykyinen ohjaus ei käytä sitä päätöksessään.</p>
        <p class="tiny muted">Päätös tehdään 15 minuutin välein mitatun huonelämpötilan perusteella. Tulevat jaksot kuvaavat aikomusta, eivät sitoumusta.</p></article>
      <article class="card span4"><div class="eyebrow">Suunnitelma</div><div class="metric small" id="plan-status">—</div><p class="tiny muted" id="plan-time">Ei kelvollista laskelmaa</p></article><article class="card span4"><div class="eyebrow">Ennustettu kustannus</div><div class="metric small" id="plan-cost">—</div><p class="tiny muted">Mallin kokonaisarvio, jos kelvollinen</p></article><article class="card span4"><div class="eyebrow">Ennustettu alin lämpötila</div><div class="metric small" id="plan-min">—</div><p class="tiny muted">Laskettu, ei anturimittaus</p></article>
      <article class="card span12"><div id="plan-warning" class="callout warn">Hyväksyttyä suunnitelmaa ei ole. Lämpöpumppu ei saa tältä käyttöliittymältä komentoja.</div><div class="tablewrap"><table><thead><tr><th>Alkaa</th><th>Ehdotus</th><th>Ennuste °C</th><th>Sähkö kWh</th><th>Arvio €</th></tr></thead><tbody id="plan-rows"><tr><td colspan="5">Suunnitelma ei saatavilla.</td></tr></tbody></table></div></article>
    </div>
  </section>
  <section class="view" id="view-history">
    <div class="heading"><div><div class="eyebrow">Mitatut tiedot</div><h1>Historia</h1><p>Lämpötilat, hinnat, kulutus ja tapahtumat laitteen tallentamista mittauksista.</p></div></div>
    <div class="grid">
      <article class="card span12"><div class="flex"><h2>Aikaväli</h2><div class="hs-ranges" role="radiogroup" aria-label="Aikaväli"><button type="button" role="radio" data-hrange="24h" aria-checked="true">24 h</button><button type="button" role="radio" data-hrange="7d" aria-checked="false">7 vrk</button><button type="button" role="radio" data-hrange="30d" aria-checked="false">30 vrk</button></div></div>
        <p id="hs-coverage" class="tiny muted" role="status"></p><div id="hs-warn" class="callout warn" hidden></div></article>
      <article class="card span12" id="hs-emptycard" hidden><div class="empty">Historiaa ei ole vielä saatavilla: laite tallentaa mittauksia vasta, kun kello on synkronoitu ja ensimmäinen 15 minuutin jakso on päättynyt.</div></article>
      <article class="card span12" id="hs-tempcard"><h2>Lämpötilat ja ohjausehdotus</h2><div id="hs-temps" class="tl-chart"></div>
        <div class="tl-legend" aria-hidden="true"><span><i class="tl-sw hs-sw-in"></i>Sisälämpötila</span><span><i class="tl-sw hs-sw-out"></i>Ulkolämpötila (mitattu)</span><span><i class="tl-sw hs-sw-fc"></i>Ulkolämpötila (ennuste)</span><span><i class="tl-sw hs-sw-sp"></i>Pumpun asetusarvo</span></div>
        <div class="tl-legend" aria-hidden="true"><span><i class="tl-sw hs-a2"></i>Ehdotus: lämmitys</span><span><i class="tl-sw hs-a1"></i>Ehdotus: pois</span><span><i class="tl-sw hs-a3"></i>Ehdotus: pidä tila</span><span><i class="hs-armed-dot"></i>Automaattiohjaus päällä</span></div>
        <p class="tiny muted">Viiva katkeaa, kun mittausta ei ole: puuttuvia arvoja ei täydennetä. Ehdotusraita näyttää ohjauksen ehdotuksen; komentoja lähetetään vain, kun automaattiohjaus on päällä.</p></article>
      <article class="card span6"><h2>Sähkön kokonaishinta</h2><div id="hs-price" class="tl-chart"></div></article>
      <article class="card span6"><h2>Mitattu teho</h2><div id="hs-power" class="tl-chart"></div><p id="hs-energy" class="tiny muted"></p></article>
      <article class="card span12"><h2>Yhteenveto</h2><div class="statusgrid" id="hs-summary"></div></article>
      <article class="card span12"><div class="flex"><h2>Tapahtumat</h2><button type="button" class="primary" id="hs-csv">Lataa CSV</button></div><ul id="hs-events" class="hs-events"></ul>
        <p class="tiny muted">Tapahtumat ja 30 päivän jaksot tallentuvat laitteen omaan muistiin tunnin välein; äkillinen sähkökatko voi menettää enintään viimeisen tunnin.</p></article>
    </div>
  </section>
  <section class="view" id="view-settings">
    <div class="heading"><div><div class="eyebrow">Olosuhteet ja laite</div><h1>Asetukset</h1><p>Paikallinen huonetavoite ja erikseen käyttöönotettava CN105-lämmitysohjaus.</p></div></div>
    <div class="grid">
      <article class="card span7"><h2>Lämmityksen toimintatila</h2><p class="tiny muted">Ylläpitolämpö ja Asuttu käyttävät omia, toisistaan riippumattomia huonetavoitteitaan. Hintaohjaus voi säätää Mitsubishin pyyntilämpötilaa.</p><div class="mode" role="group" aria-label="Laskennallinen tila"><button type="button" data-mode="0">Kylmillään</button><button type="button" data-mode="1">Kuivanapito</button><button type="button" data-mode="2">Ylläpitolämpö</button><button type="button" data-mode="3">Asuttu</button></div><div class="fieldrow" style="margin-top:17px"><label class="field">Ylläpitotavoite °C<input id="setting-hold" type="number" min="5" max="24" step=".5" inputmode="decimal"></label><label class="field">Asuttu °C<input id="setting-occupied" type="number" min="16" max="26" step=".5" inputmode="decimal"></label><label class="field">Kuivanapito Δ°C<input id="setting-delta" type="number" min="3" max="5" step=".5" inputmode="decimal"></label><button class="primary" id="save-mode" type="button" disabled>Tallenna asetukset</button></div><div id="feedback" role="status"></div><p class="tiny muted">Valitse Ylläpitolämpö tai Asuttu sekä kyseisen tilan huonetavoite. Hintaehdotus laskee Mitsubishille 16–26 °C pyyntilämpötilan 0,5 °C askelin; komennot sallitaan vain erillisellä käyttöönotolla.</p></article>
      <article class="card span12"><h2>Lämpöpumpun automaattiohjaus</h2><p class="tiny muted">Lite ohjaa valittua pumppua valitun huonelämpötilalähteen ja sähkön kokonaishinnan perusteella. Lämpötilapyyntö tarkistetaan takaisinluvulla. Automaattiohjaus valitaan käyttöönotossa tai tässä näkymässä.</p><div class="flex"><span class="chip" id="control-state">Ohjaus ei käytössä</span><span id="control-last">Ei komentoja</span><button type="button" id="control-enable" class="primary">Ota automaattiohjaus käyttöön</button><button type="button" id="control-disable">Poista automaattiohjaus käytöstä</button></div><p id="control-feedback" role="status"></p></article>
      <article class="card span12" id="site-card"><h2>Sähkösopimus ja siirto</h2><p class="tiny muted">Kokonaishinta = spot + marginaali + siirto + sähkövero, arvonlisävero lisätään kerran. Kuukausimaksut eivät vaikuta 15 minuutin ohjaukseen eikä niitä lasketa mukaan.</p><div class="fieldrow"><label class="field">Marginaali snt/kWh<input id="site-margin" inputmode="decimal"></label><label class="field">Siirto päivä snt/kWh<input id="site-tday" inputmode="decimal"></label><label class="field">Siirto yö / muu aika snt/kWh<input id="site-tnight" inputmode="decimal"></label><label class="field">Sähkövero snt/kWh<input id="site-tax" inputmode="decimal"></label><label class="field">ALV %<input id="site-vat" inputmode="decimal"></label></div><div class="fieldrow"><label class="field">Siirtotuote<select id="site-kind"><option value="general">Yleissiirto</option><option value="time_of_day">Aikasiirto (päivä/yö)</option><option value="seasonal">Kausisiirto (talviarkipäivä)</option></select></label><label class="field">Päivä alkaa klo<input id="site-dstart" type="number" min="0" max="23" step="1"></label><label class="field">Yö alkaa klo<input id="site-nstart" type="number" min="0" max="23" step="1"></label><label class="field">Hinnat sisältävät ALV:n<select id="site-incl"><option value="true">Kyllä (marginaali, siirto, vero)</option><option value="false">Ei, alv lisätään</option></select></label><label class="field">Esilämmityksen hintaetu %<input id="site-adv" inputmode="decimal"></label></div><p class="tiny muted">Spot-hinta on aina ilman arvonlisäveroa. Esilämmitys tehdään vain, kun hinta on vähintään annetun prosentin verran halvempi kuin seuraavien kahden tunnin mediaani.</p></article>
      <article class="card span12" id="wizard-card"><h2>Ohjattu käyttöönotto</h2><p class="tiny muted">Vaihda maa, sijainti, sähkömarkkina-alue tai lämpöpumppu vaihe vaiheelta. Nykyiset asetukset säilyvät, kunnes tallennat uuden valinnan.</p><button type="button" class="primary" id="wz-reopen">Avaa käyttöönotto uudelleen</button></article>
      <article class="card span12" id="devices-card"><h2>Lämpöpumppu ja yhteydet</h2><div class="fieldrow"><label class="field">Lämpöpumppu ja yhteys<select id="site-adapter"></select></label><label class="field">CN105-sillan /events-osoite<input id="site-cn105" autocomplete="off" autocapitalize="none" spellcheck="false"></label><label class="field">Shelly-mittarin osoite<input id="site-shelly" autocomplete="off" autocapitalize="none" spellcheck="false"></label><label class="field">Sääpaikka (leveysaste,pituusaste)<input id="site-fmi" autocomplete="off" placeholder="61.40,24.10"></label></div><div class="tablewrap"><table><thead><tr><th>Valmistaja / malli</th><th>Yhteys</th><th>Tila</th><th>Mittaukset</th><th>Komennot</th></tr></thead><tbody id="adapter-rows"><tr><td colspan="5">Ladataan…</td></tr></tbody></table></div><p class="tiny muted">Mitsubishi on CN105-kenttäpilotti ja Midea V2/V3 voidaan valita yhteyden tilanlukuun ja manuaaliseen ohjaustestiin. Muut on lueteltu tutkimustietona, ei tukilupauksena. Sääpaikan muutos tulee voimaan heti tallennuksen jälkeen. Laitteen valinta ei itsessään ota ohjausta käyttöön.</p><div class="flex"><button class="primary" id="site-save" type="button">Tallenna sopimus ja yhteydet</button><a href="/setup" id="wifi-change">Vaihda Wi-Fi-verkko</a></div><p id="site-feedback" role="status"></p></article>
      <article class="card span12" id="wf-rac-card" hidden><div id="wf-rac-connection">
        <h2>WF-RAC · Mitsubishi Heavy Industries</h2>
        <p class="tiny muted">Anna alkuperäisen HTTP-moduulin paikallinen IPv4-osoite.</p>
        <div class="fieldrow">
          <label class="field">Pumpun IP-osoite<input id="wf-rac-address" autocomplete="off" spellcheck="false"></label>
          <button id="wf-rac-pair" class="primary" type="button">Yhdistä WF-RAC</button>
          <button id="wf-rac-test" type="button">Testaa yhteys nyt</button>
        </div>
        <p id="wf-rac-feedback" role="status">WF-RAC-yhteys odottaa lukutestiä.</p>
        <div class="statusgrid"><span>Toimintatila</span><span id="wf-rac-mode">—</span>
          <span>Pyyntilämpötila</span><span id="wf-rac-target">—</span>
          <span>Pumpun sisälämpötila</span><span id="wf-rac-temperature">—</span></div>
      </div><div id="wf-rac-pilot-anchor"></div><section id="wf-rac-pilot">
        <h3>WF-RAC · mallikohtainen koekäyttö</h3>
        <p class="tiny muted">Tarkista pumpun malli, rajat ja askel laitteen ohjeesta.</p>
        <div class="fieldrow">
          <label class="field">Lämpöpumpun malli<input id="wf-rac-pilot-model" maxlength="47" autocomplete="off"></label>
          <label class="field">Minimi °C<input id="wf-rac-pilot-min" type="number" min="10" max="33" step=".5"></label>
          <label class="field">Maksimi °C<input id="wf-rac-pilot-max" type="number" min="10" max="33" step=".5"></label>
          <label class="field">Moduuliin jo rekisteröity paikallinen tunniste<input id="wf-rac-operator" type="password" maxlength="36" autocomplete="off" spellcheck="false"></label>
          <button id="wf-rac-approve" type="button" disabled>Hyväksy WF-RAC-ohjauskoe</button>
          <button id="wf-rac-revoke" type="button" disabled>Poista WF-RAC-koelupa</button>
        </div><p id="wf-rac-pilot-result" role="status"></p>
      </section><div class="fieldrow">
        <label class="field">Pyyntilämpötila<input id="wf-rac-command-target" type="number" step=".5"></label>
        <button id="wf-rac-setpoint" type="button" disabled>Tallenna pyynti</button>
        <button id="wf-rac-heat" type="button" disabled>Lämmitys</button>
        <button id="wf-rac-off" type="button" disabled>Pois päältä</button>
      </div></article>
      <article class="card span12" id="daikin-dsiot-card" hidden><div id="daikin-dsiot-connection">
        <h2>Daikin · paikallinen DSIoT-yhteys</h2>
        <p class="tiny muted">Anna Daikinin DSIoT-moduulin paikallinen IPv4-osoite.</p>
        <div class="fieldrow">
          <label class="field">Pumpun IP-osoite<input id="daikin-dsiot-address" autocomplete="off" spellcheck="false"></label>
          <button id="daikin-dsiot-pair" class="primary" type="button">Yhdistä Daikin DSIoT</button>
          <button id="daikin-dsiot-test" type="button">Testaa yhteys nyt</button>
        </div>
        <p id="daikin-dsiot-feedback" role="status">Daikin DSIoT odottaa lukutestiä.</p>
        <div class="statusgrid"><span>Toimintatila</span><span id="daikin-dsiot-mode">—</span>
          <span>Pyyntilämpötila</span><span id="daikin-dsiot-target">—</span>
          <span>Pumpun sisälämpötila</span><span id="daikin-dsiot-temperature">—</span></div>
      </div></article>
      <article class="card span12" id="daikin-card" hidden><div id="daikin-connection">
        <h2>Daikin · paikallinen HTTP-yhteys</h2>
        <p class="tiny muted">Anna Daikinin Wi-Fi-moduulin IP-osoite kotiverkossa.</p>
        <div class="fieldrow">
          <label class="field">Pumpun IP-osoite<input id="daikin-ip" autocomplete="off" spellcheck="false"></label>
          <button id="daikin-pair" class="primary" type="button">Yhdistä Daikin</button>
          <button id="daikin-test" type="button">Testaa yhteys nyt</button>
        </div>
        <p id="daikin-feedback" role="status">Daikin-yhteys odottaa lukutestiä.</p>
        <div class="statusgrid"><span>Toimintatila</span><span id="daikin-mode">—</span>
          <span>Pyyntilämpötila</span><span id="daikin-target">—</span>
          <span>Pumpun sisälämpötila</span><span id="daikin-temperature">—</span></div>
      </div><div id="daikin-pilot-anchor"></div><section id="daikin-pilot">
        <h3>Daikin · mallikohtainen koekäyttö</h3>
        <p class="tiny muted">Tarkista pumpun malli, rajat ja askel laitteen ohjeesta.</p>
        <div class="fieldrow">
          <label class="field">Lämpöpumpun malli<input id="daikin-pilot-model" maxlength="47" autocomplete="off"></label>
          <label class="field">Minimi °C<input id="daikin-pilot-min" type="number" min="10" max="32" step=".5"></label>
          <label class="field">Maksimi °C<input id="daikin-pilot-max" type="number" min="10" max="32" step=".5"></label>
          <label class="field">Asetusarvon askel<select id="daikin-pilot-step"><option value="0.5">0.5 °C</option><option value="1">1 °C</option></select></label>
          <button id="daikin-approve" type="button" disabled>Hyväksy Daikinin ohjauskoe</button>
          <button id="daikin-revoke" type="button" disabled>Poista Daikinin koelupa</button>
        </div><p id="daikin-pilot-status" role="status"></p>
      </section><div class="fieldrow">
        <label class="field">Pyyntilämpötila<input id="daikin-command-target" type="number" step=".5"></label>
        <button id="daikin-setpoint" type="button" disabled>Tallenna pyynti</button>
        <button id="daikin-heat" type="button" disabled>Lämmitys</button>
        <button id="daikin-off" type="button" disabled>Pois päältä</button>
      </div></article>
      <article class="card span12" id="gree-card" hidden><div id="gree-connection">
        <h2>Gree / EWPE Smart · paikallinen yhteys</h2>
        <p class="tiny muted">Hae Gree-laitteet kotiverkosta tai anna pumpun IP-osoite ja laitetunniste.</p>
        <div class="fieldrow">
          <label class="field">Pumpun IP-osoite<input id="gree-ip" autocomplete="off" spellcheck="false"></label>
          <label class="field">Pumpun laitteen tunniste<input id="gree-id" autocomplete="off" spellcheck="false"></label>
          <label class="field">Gree-salausversio<select id="gree-cipher"><option value="1">V1 (AES-ECB)</option><option value="2">V2 (AES-GCM)</option></select></label>
        </div>
        <div class="flex">
          <button id="gree-scan" type="button">Etsi Gree</button>
          <select id="gree-discovered" hidden aria-label="Gree-laitteet"></select>
          <button id="gree-pair" type="button" class="primary">Yhdistä Gree</button>
          <button id="gree-test" type="button">Testaa yhteys nyt</button>
        </div>
        <p id="gree-status" role="status">Gree-yhteys odottaa lukutestiä.</p>
        <div class="statusgrid"><span>Toimintatila</span><span id="gree-mode">—</span>
          <span>Pyyntilämpötila</span><span id="gree-target">—</span>
          <span>Pumpun sisälämpötila</span><span id="gree-temperature">—</span></div>
      </div>
      <span id="gree-pilot-anchor" hidden></span><div id="gree-pilot">
        <h3>Gree · mallikohtainen koekäyttö</h3>
        <p class="tiny muted">Gree-laitteen rajat vahvistetaan valitulle mallille kokonaisina Celsius-asteina.</p>
        <div class="fieldrow">
          <label class="field">Pumpun tarkka malli<input id="gree-model" autocomplete="off"></label>
          <label class="field">Mallin minimipyynti °C<input id="gree-min" type="number" min="16" max="30" step="1"></label>
          <label class="field">Mallin maksimipyynti °C<input id="gree-max" type="number" min="16" max="30" step="1"></label>
          <button id="gree-approve" type="button">Hyväksy mallikohtainen ohjaustesti</button>
          <button id="gree-revoke" type="button">Peru koekäyttöhyväksyntä</button>
        </div>
        <p id="gree-pilot-status" role="status">Mallikohtaista ohjaustestiä ei ole hyväksytty.</p>
      </div>
      <div class="fieldrow">
        <label class="field">Pyynti °C<input id="gree-command-target" type="number" min="16" max="30" step="1"></label>
        <button id="gree-setpoint" type="button" disabled>Tallenna pyynti</button>
        <button id="gree-heat" type="button" disabled>Lämmitys</button>
        <button id="gree-off" type="button" disabled>Pois päältä</button>
      </div></article>
      <article class="card span12" id="midea-v3-card" hidden><div id="midea-connection"><h2>Midea · paikallinen V2 / V3 Wi-Fi -yhteys</h2><p class="tiny muted">V2- tai V3-tilanluku tapahtuu suoraan lämpöpumpun Wi-Fi-moduulista ilman Home Assistantia tai jatkuvaa pilviyhteyttä. Midean käsiohjaus ja automaattinen hinta- ja termostaattiohjaus ovat kokeellisia. Yhdistä vain omassa luotetussa lähiverkossa; kehitysvaiheen HTTP-asetussivu ei salaa tunnisteiden siirtoa.</p><p class="tiny muted"><a href="https://github.com/Enevina-Ltd/ENEVINA-Lite/blob/main/docs/integrations/MIDEA_MODEL_CATALOG.md" target="_blank" rel="noopener noreferrer">Tuetut Midea-mallit ja lähteet</a></p><div class="fieldrow"><label class="field">Midea-protokolla<select id="midea-protocol"><option value="3">V3 (token + key)</option><option value="2">V2 (ei tokenia)</option></select></label><label class="field">Midean paikallinen IP<input id="midea-ip" placeholder="10.0.0.15" autocomplete="off" spellcheck="false"></label><label class="field">Midea V3 device ID<input id="midea-id" autocomplete="off" spellcheck="false"></label><label class="field" id="midea-v3-token-field">V3 token (128 heksamerkkiä)<input id="midea-token" type="password" autocomplete="new-password" spellcheck="false"></label><label class="field" id="midea-v3-key-field">V3 key (64 heksamerkkiä)<input id="midea-key" type="password" autocomplete="new-password" spellcheck="false"></label></div><div class="flex"><button id="midea-discover" type="button">Etsi Midea lähiverkosta</button><select id="midea-discover-select" aria-label="Löydetyt Midea-laitteet" hidden></select><button class="primary" id="midea-save" type="button">Tallenna Midea-yhteys</button><button id="midea-test" type="button">Testaa yhteys nyt</button><span id="midea-state">Ei mittausta</span></div><p id="midea-feedback" role="status"></p><p id="midea-diagnostic" class="tiny muted" role="status">Ei vielä testattu.</p><div class="statusgrid"><span>Yhteyden tila</span><span id="midea-read">—</span><span>Toimintatila</span><span id="midea-mode">—</span><span>Pyyntilämpötila</span><span id="midea-target">—</span><span>Pumpun sisälämpötila</span><span id="midea-indoor">—</span><span>Ulkolämpötila</span><span id="midea-outdoor">—</span></div><p class="tiny muted">V3-tunnisteet tallennetaan paikalliseen NVS-muistiin, eikä niitä palauteta selaimeen. V2 ei tarvitse tokenia tai keytä, mutta sen yhteys käyttää yhteistä kiinteää salausavainta eikä todenna pumppua samalla tavalla kuin V3. Valitse protokolla laitteen todetun version mukaan. Käytä vain luotetussa lähiverkossa. Lähiverkkohaku täyttää IP-osoitteen ja device ID:n, mutta ei tallenna yhteyttä eikä ohjaa pumppua. V3 vaatii lisäksi omat tunnukset. Päivitä IP-osoite yhteysasetuksiin, jos osoite vaihtuu.</p></div>
        <span id="midea-pilot-anchor" hidden></span><div id="midea-pilot-panel"><h3>Midean mallikohtainen koekäyttöhyväksyntä</h3>
        <p class="tiny muted">Käyttöönotto koskee vain yhtä asetettua laitetta ja käynnistyskertaa. Anna pumpun tarkka mallimerkintä sekä mallille varmistetut pyynnin rajat. Tämä ei luo salasanaa tai poista ohjausominaisuuksia.</p>
        <div class="fieldrow">
          <label class="field">Pumpun laitteen tunniste<input id="midea-pilot-id" inputmode="numeric" autocomplete="off"></label>
          <label class="field">Pumpun tarkka malli<input id="midea-pilot-model" autocomplete="off" placeholder="Mallimerkintä"></label>
          <label class="field">Mallin minimipyynti °C<input id="midea-pilot-min" type="number" step=".5" min="12" max="40" placeholder="Mallin minimi"></label>
          <label class="field">Mallin maksimipyynti °C<input id="midea-pilot-max" type="number" step=".5" min="12" max="40" placeholder="Mallin maksimi"></label>
          <button id="midea-pilot-enable" type="button">Hyväksy mallikohtainen ohjaustesti</button>
          <button id="midea-pilot-disable" type="button">Peru koekäyttöhyväksyntä</button>
        </div>
        <p id="midea-pilot-feedback" role="status">Mallikohtaista ohjaustestiä ei ole hyväksytty.</p></div>
        <h3>Midean paikallinen ohjaus · V2 / V3</h3>
        <p class="tiny muted">Käsikäskyt lähetetään suoraan pumpulle ja tarkistetaan uudella tilanluvulla. Poista automaattiohjaus käytöstä ennen käsikäskyn lähettämistä.</p>
        <p id="midea-capability-summary" class="tiny muted" role="status">Odotetaan laitteen ominaisuuksia.</p>
        <div class="fieldrow">
          <label class="field" id="midea-field-power">Virta<select id="midea-command-power"><option value="">Säilytä nykyinen</option><option value="true">Päälle</option><option value="false">Pois</option></select></label>
          <label class="field" id="midea-field-mode">Toimintatila<select id="midea-command-mode"><option value="">Säilytä nykyinen</option><option value="1">Auto</option><option value="2">Jäähdytys</option><option value="3">Kuivaus</option><option value="4">Lämmitys</option><option value="5">Puhallus</option></select></label>
          <label class="field" id="midea-field-target_c">Pyynti °C<input id="midea-command-temperature" type="number" step=".5" min="12" max="40" placeholder="Säilytä" inputmode="decimal"></label>
          <label class="field" id="midea-field-fan">Puhallus<select id="midea-command-fan"><option value="">Säilytä nykyinen</option><option value="102">Auto</option><option value="20">Hiljainen</option><option value="40">Matala</option><option value="60">Keskitaso</option><option value="80">Korkea</option><option value="100">Täysi</option></select></label>
          <label class="field" id="midea-field-swing">Swing<select id="midea-command-swing"><option value="">Säilytä nykyinen</option><option value="0">Pois</option><option value="12">Pysty</option><option value="3">Vaaka</option><option value="15">Molemmat</option></select></label>
        </div>
        <div class="fieldrow">
          <label class="field" id="midea-field-eco" hidden>Eco<select id="midea-command-eco"></select><span class="tiny muted" id="midea-current-eco">—</span></label>
          <label class="field" id="midea-field-turbo" hidden>Turbo<select id="midea-command-turbo"></select><span class="tiny muted" id="midea-current-turbo">—</span></label>
          <label class="field" id="midea-field-sleep" hidden>Yötila<select id="midea-command-sleep"></select><span class="tiny muted" id="midea-current-sleep">—</span></label>
          <label class="field" id="midea-field-freeze_protection" hidden>8 °C ylläpitolämmitys<select id="midea-command-freeze_protection"></select><span class="tiny muted" id="midea-current-freeze_protection">—</span></label>
        </div>
        <details id="midea-properties" hidden><summary>Mallin lisätoiminnot</summary><div class="fieldrow">
          <label class="field" id="midea-field-display" hidden>Näyttö<select id="midea-command-display"></select><span class="tiny muted" id="midea-current-display">—</span></label>
          <label class="field" id="midea-field-vertical_angle" hidden>Pystyläpän asento<select id="midea-command-vertical_angle"></select><span class="tiny muted" id="midea-current-vertical_angle">—</span></label>
          <label class="field" id="midea-field-horizontal_angle" hidden>Vaakaläpän asento<select id="midea-command-horizontal_angle"></select><span class="tiny muted" id="midea-current-horizontal_angle">—</span></label>
          <label class="field" id="midea-field-rate_select" hidden>Tehotaso<select id="midea-command-rate_select"></select><span class="tiny muted" id="midea-current-rate_select">—</span></label>
          <label class="field" id="midea-field-anion" hidden>Ionisointi<select id="midea-command-anion"></select><span class="tiny muted" id="midea-current-anion">—</span></label>
          <label class="field" id="midea-field-breezeless" hidden>Vedoton puhallus<select id="midea-command-breezeless"></select><span class="tiny muted" id="midea-current-breezeless">—</span></label>
          <label class="field" id="midea-field-indirect_wind" hidden>Epäsuora puhallus<select id="midea-command-indirect_wind"></select><span class="tiny muted" id="midea-current-indirect_wind">—</span></label>
          <label class="field" id="midea-field-self_clean" hidden>Itsepuhdistus<select id="midea-command-self_clean"></select><span class="tiny muted" id="midea-current-self_clean">—</span></label>
          <label class="field" id="midea-field-sound" hidden>Merkkiääni<select id="midea-command-sound"></select><span class="tiny muted" id="midea-current-sound">—</span></label>
        </div></details>
        <p id="midea-control-availability" class="tiny muted" role="status"></p>
        <div class="flex">
          <button class="primary" id="midea-command-send" type="button" disabled>Lähetä ohjaus</button>
        </div>
        <p id="midea-control-feedback" role="status">Odota ensin onnistunutta tilanlukua.</p>
        <p class="tiny muted">Ohjaustapa ja käytettävissä olevat asetukset valitaan laitteen ilmoittamien ominaisuuksien ja luetun tilan perusteella. Lite lähettää yhden komennon ja tarkistaa tuloksen uudella tilanluvulla. Lisätoiminnot voidaan joutua lähettämään erillään perusasetuksista.</p></article>
      <article class="card span12" id="firmware-update-card">
        <h2>Firmwarepäivitys</h2>
        <p class="tiny muted">Päivitä Lite suoraan dashboardista. Laite tarkistaa varmennetulta HTTPS-päivityspalvelimelta uuden allekirjoitetun version ja asentaa sen palautettavalle OTA-osiolle. Pumpun asetuksia ei muuteta. Tavallisen .bin-tiedoston lataus selaimesta ei ole käytössä.</p>
        <div class="callout" id="ota-headline" role="status"><strong id="ota-headline-title"></strong><br><span id="ota-headline-detail"></span></div>
        <div class="statusgrid">
          <span>Nykyinen firmware</span><span id="ota-current">—</span>
          <span>Viimeisin onnistunut päivitys</span><span id="ota-last-update">—</span>
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
        <p class="tiny muted">Tarkistus ja asennus ovat yksi toiminto: kelvollinen allekirjoitettu julkaisu asennetaan heti, kun se löytyy. Julkaisutietoja ei kuljeteta allekirjoitetussa päivitysmanifestissa; ne löytyvät julkaisusivulta: <a href="https://github.com/Enevina-Ltd/ENEVINA-Lite/releases" target="_blank" rel="noopener noreferrer">Enevina Lite -julkaisut GitHubissa</a>.</p>
      </article>
      <article class="card span5"><h2>Laitteen tila</h2><div class="statusgrid"><span>Ohjelmistoversio</span><span id="device-version">—</span><span>Wi-Fi RSSI</span><span id="device-wifi">—</span><span>CN105-telemetria</span><span id="device-cn105">—</span><span>Shelly-mittaus</span><span id="device-shelly">—</span><span>Sisäinen vapaa RAM</span><span id="device-ram">—</span><span>PSRAM vapaana</span><span id="device-psram">—</span><span>OTA-lataus käytössä</span><span id="device-ota">—</span></div><p class="foot"><a href="/#settings" style="text-decoration:underline">Avaa laiteasetukset</a></p></article>
      <article class="card span12"><h2>Mitsubishi CN105 · vastaanotettu data</h2><div class="statusgrid"><span>Viimeisin SSE-tilaviesti</span><span id="mitsubishi-read-status">—</span><span>Pumpun tila (SSE)</span><span id="mitsubishi-read-mode">—</span><span>Pumpun sisälämpötila (SSE)</span><span id="mitsubishi-read-temp">—</span><span>Asetuslämpötila (SSE)</span><span id="mitsubishi-read-setpoint">—</span><span>Kompressori (SSE)</span><span id="mitsubishi-read-hz">—</span><span>Kompressoriviestin ikä</span><span id="mitsubishi-compressor-age">—</span></div><p class="foot">ESPHome /events -rajapinnasta vastaanotettu viimeisin tilatieto. Viestin vastaanottoaika ei todista uutta CN105-UART-lukua, eikä näitä arvoja käytetä optimoinnin turvallisuushyväksyntään.</p></article>
      <article class="card span12" id="room-temperature-settings"></article>
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
  manual:false,open:false,controlChoice:null,controlBasis:'',
  comfort:{target:'',min:'',max:'',profile:'balanced'},
  sensors:null,sensorTimer:null,confirmBind:'',shellyScan:null,shellyTimer:null,shellyScope:'unknown',shellyScopeInitialized:false,shellyScopeFor:''};
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
function wzMideaReadOk(m){
  return !!m && m.selected===true && m.configured===true && m.fresh===true &&
    m.read_code==='ok' && (m.protocol===2||m.protocol===3);
}
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
    const ok=wzMideaReadOk(c.midea);
    pumpState=ok?'ok':'warn';
    pumpDetail=ok?'Midea V'+c.midea.protocol+' · '+WT('Yhteys toimii ja tietoa tulee.'):
      WT('Valittu, mutta tuoretta tietoa ei tule.');
  }else if(site.pump_adapter==='mhi_wf_rac_lan'){
    const ok=c.wfRac?.configured===true&&c.wfRac?.fresh===true;
    pumpState=ok?'ok':'warn';
    pumpDetail=ok?'WF-RAC · '+WT('Yhteys toimii ja tietoa tulee.'):WT('Valittu, mutta tuoretta tietoa ei tule.');
  }else if(site.pump_adapter==='daikin_dsiot'){
    const ok=c.dsiot?.configured===true&&c.dsiot?.fresh===true;
    pumpState=ok?'ok':'warn';
    pumpDetail=ok?'Daikin DSIoT · '+WT('Yhteys toimii ja tietoa tulee.'):WT('Valittu, mutta tuoretta tietoa ei tule.');
  }else if(site.pump_adapter==='daikin_local_api'){
    const ok=c.daikin?.configured===true&&c.daikin?.fresh===true;
    pumpState=ok?'ok':'warn';
    pumpDetail=ok?'Daikin · '+WT('Yhteys toimii ja tietoa tulee.'):WT('Valittu, mutta tuoretta tietoa ei tule.');
  }else if(site.pump_adapter==='gree_ewpe_lan'){
    const ok=c.gree?.configured===true&&c.gree?.fresh===true;
    pumpState=ok?'ok':'warn';
    pumpDetail=ok?'Gree V'+c.gree.cipher+' · '+WT('Yhteys toimii ja tietoa tulee.'):
      WT('Valittu, mutta tuoretta tietoa ei tule.');
  }else if(site.pump_adapter){
    if(verified){pumpState='ok';pumpDetail=WT('Yhteys toimii ja tietoa tulee.');}
    else if(observed){pumpState='warn';pumpDetail=WT('Tietoa tulee, mutta UART-lähdettä ei ole varmennettu.');}
    else{pumpState='warn';pumpDetail=WT('Valittu, mutta tuoretta tietoa ei tule.');}
  }
  add('pump',WT('Lämpöpumppu'),pumpState,pumpDetail);
  const pumpTemperature=st.room_temperature_source==='pump';
  const roomFresh=st.room_temperature_c===undefined?air.sensor_fresh===true:
    typeof st.room_temperature_c==='number'&&Number.isFinite(st.room_temperature_c);
  add('sensor',WT('Huonelämpötila-anturi'),roomFresh?'ok':'warn',
    roomFresh?(pumpTemperature?WT('Tuore mittaus pumpun omalta anturilta.'):WT('Tuore RuuviTag-mittaus.')):
    WT('Ei tuoretta mittausta valitusta lähteestä.'));
  const scope=site.shelly_scope||'unknown';
  add('meter',WT('Sähkönkulutusmittari (valinnainen)'),
    st.shelly_w==null?'optional':scope==='heat_pump'?'ok':'warn',
    st.shelly_w==null?WT('Ei mittaria. Voit lisätä sen myöhemmin.'):
    scope==='heat_pump'?WT('Mittari antaa lukemia ja mittaa vain lämpöpumppua.'):
    WT('Mittari antaa lukemia, mutta sen kohdetta ei ole varmistettu lämpöpumpuksi: lukua ei käytetä pumpun kulutusmalliin.'));
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
    if(!range)return {ok:false,error:WT('Valitse Ylläpitolämpö tai Asuttu, jotta voit asettaa tavoitelämpötilan.')};
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


// Devices step: RuuviTag list, names, roles, Shelly discovery and meter scope.
function wzAgeText(age){
  if(!Number.isFinite(age)||age<0)return '—';
  if(age<60)return Math.round(age)+' s';
  if(age<3600)return Math.round(age/60)+' min';
  return Math.round(age/3600)+' h';
}
function wzSensorLine(x){
  const t=Number.isFinite(x.temperature_c)?x.temperature_c.toFixed(1).replace('.',',')+' °C':'—';
  const h=Number.isFinite(x.humidity_pct)?Math.round(x.humidity_pct)+' %':'—';
  const b=Number.isFinite(x.battery_v)?x.battery_v.toFixed(2).replace('.',',')+' V':'—';
  return t+' · '+h+' · '+b+' · '+(Number.isFinite(x.rssi_dbm)?x.rssi_dbm+' dBm':'—');
}
// 'live' = advertised within the last 2 minutes (device decides); never assume.
function wzSensorState(x){return x.live===true?'live':'stale';}
function wzSignalText(rssi){
  if(!Number.isFinite(rssi))return WT('Signaali tuntematon');
  return rssi>=-65?WT('Hyvä signaali'):rssi>=-80?WT('Kohtalainen signaali'):WT('Heikko signaali');
}
// Replacing a bound INDOOR sensor discards the previous sensor's history, so it
// needs a second, explicit tap. Returns true when the bind may go ahead now.
function wzBindNeedsConfirm(sensors,role,mac){
  const b=(sensors&&sensors.bound)||{};
  return role==='indoor'&&!!b.indoor&&b.indoor!==mac;
}
const WZ_SHELLY_REASONS={GEN1_NOT_SUPPORTED:WT('Gen1-laitetta ei vielä tueta; tarvitaan Gen2 tai uudempi.'),
  AUTH_REQUIRED:WT('Laitteessa on salasana. Lite ei vielä tue salasanallisia Shelly-laitteita.'),
  NOT_SHELLY:WT('Ei tunnistettu Shelly-laitteeksi.'),
  POWER_NOT_VERIFIED:WT('Switch:0-tehomittausta ei voitu vahvistaa. Tarkista malli/yhteys ja etsi uudelleen.')};
function wzShellyText(d){
  const name=(d.app||d.model||d.id||d.host||'Shelly');
  if(d.support==='SUPPORTED')return name+' · '+(Number.isFinite(d.power_w)?Math.round(d.power_w)+' W':WT('teholukemaa ei saatu'));
  return name+' · '+(WZ_SHELLY_REASONS[d.support]||WZ_SHELLY_REASONS.NOT_SHELLY);
}
const WZ_SCOPES=[{id:'heat_pump',label:WT('Mittari mittaa vain lämpöpumppua')},
  {id:'whole_house',label:WT('Mittari mittaa koko talon (tai muuta kuin pelkkää pumppua)')},
  {id:'unknown',label:WT('En tiedä vielä')}];
// Fields to save for the meter. URL first: a changed address resets the scope
// on the device, so the confirmed scope has to follow it.
// A different meter must never inherit the old device's confirmed scope.
function wzSetShellyAddress(value){
  const next=String(value||'');
  if(next.trim()!==String(WZ.shelly||'').trim()){
    WZ.shellyScope='unknown';
    WZ.shellyScopeFor=next.trim();
  }
  WZ.shelly=next;
}
function wzDevicesFields(v,current){
  const f={setup_step:'7'};
  const url=String(v.shelly||'').trim();
  if(url!==String(current.shelly_url||''))f.shelly_url=url;
  const scope=WZ_SCOPES.some(s=>s.id===v.scope)?v.scope:'unknown';
  if(url&&(url!==String(current.shelly_url||'')||scope!==(current.shelly_scope||'unknown')))f.shelly_scope=scope;
  return f;
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
    const panel=document.getElementById('midea-connection');
    if(panel)body.append(panel);
    body.append(wzEl('p',{class:'muted'},WT('Tallenna Midea-yhteys ja odota tuoretta tilanlukua ennen jatkamista.')));
  }else if(WZ.adapter==='mhi_wf_rac_lan'){
    const panel=document.getElementById('wf-rac-connection');
    if(panel)body.append(panel);
    body.append(wzEl('p',{class:'muted'},WT('WF-RAC lukee tilan ja ohjaa hyväksyttyä lämmitysmallia.')));
  }else if(WZ.adapter==='daikin_dsiot'){
    const panel=document.getElementById('daikin-dsiot-connection');
    if(panel)body.append(panel);
    body.append(wzEl('p',{class:'muted'},WT('Tämä Daikin DSIoT -yhteys lukee pumpun tilan.')));
  }else if(WZ.adapter==='daikin_local_api'){
    const panel=document.getElementById('daikin-connection');
    if(panel)body.append(panel);
    body.append(wzEl('p',{class:'muted'},WT('Daikin lukee tilan ja ohjaa hyväksyttyä lämmitysmallia.')));
  }else if(WZ.adapter==='gree_ewpe_lan'){
    const panel=document.getElementById('gree-connection');
    if(panel)body.append(panel);
    body.append(wzEl('p',{class:'muted'},WT('Gree lukee tilan ja ohjaa hyväksyttyä lämmitysmallia.')));
  }
}
function wzRestoreWfRacPanel(clearOperator=false){
  const operator=document.getElementById('wf-rac-operator');
  if(clearOperator&&operator)operator.value='';
  const panel=document.getElementById('wf-rac-connection');
  const card=document.getElementById('wf-rac-card');
  if(panel&&card&&panel.parentElement!==card)card.prepend(panel);
  const pilot=document.getElementById('wf-rac-pilot');
  const anchor=document.getElementById('wf-rac-pilot-anchor');
  if(pilot&&anchor&&anchor.nextElementSibling!==pilot)anchor.after(pilot);
}
function wzRestoreDsiotPanel(){
  const panel=document.getElementById('daikin-dsiot-connection');
  const card=document.getElementById('daikin-dsiot-card');
  if(panel&&card&&panel.parentElement!==card)card.prepend(panel);
}
function wzRestoreDaikinPanel(){
  const panel=document.getElementById('daikin-connection');
  const card=document.getElementById('daikin-card');
  if(panel&&card&&panel.parentElement!==card)card.prepend(panel);
  const pilot=document.getElementById('daikin-pilot');
  const anchor=document.getElementById('daikin-pilot-anchor');
  if(pilot&&anchor&&anchor.nextElementSibling!==pilot)anchor.after(pilot);
}
function wzRestoreGreePanel(){
  const panel=document.getElementById('gree-connection');
  const card=document.getElementById('gree-card');
  if(panel&&card&&panel.parentElement!==card)card.prepend(panel);
  const pilot=document.getElementById('gree-pilot');
  const anchor=document.getElementById('gree-pilot-anchor');
  if(pilot&&anchor&&anchor.nextElementSibling!==pilot)anchor.after(pilot);
}
function wzMideaStep(){
  return WZ.open && WZ.step===4 && WZ.adapter==='midea_lan';
}
function wzRestoreMideaPanel(clearSecrets){
  const panel=document.getElementById('midea-connection');
  const card=document.getElementById('midea-v3-card');
  if(!panel||!card||panel.parentElement===card)return;
  if(clearSecrets){
    WZ.mideaChanged=false;
    document.getElementById('midea-token').value='';
    document.getElementById('midea-key').value='';
  }
  card.prepend(panel);
}
let wzMideaTimer=null,wzPollingMidea=false;
async function wzPollMidea(){
  if(wzPollingMidea)return;
  wzPollingMidea=true;
  try{await reloadMidea();}finally{wzPollingMidea=false;}
}
function wzUpdateMideaPolling(){
  if(wzMideaStep()){
    if(!wzMideaTimer){wzPollMidea();wzMideaTimer=setInterval(wzPollMidea,2500);}
  }else if(wzMideaTimer){
    clearInterval(wzMideaTimer);wzMideaTimer=null;
  }
}
async function wzSavePump(skip){
  if(skip||!WZ.adapter){await wzPost({setup_step:'5'});return;}
  if(WZ.adapter==='midea_lan'){
    const status=await fetchJSON('/api/midea/status');
    if(WZ.mideaChanged || !wzMideaReadOk(status))
      throw Error(WT('Tallenna Midea-yhteys ja odota tuoretta tilanlukua ennen jatkamista.'));
    await wzPost({setup_step:'5'});return;
  }
  if(WZ.adapter==='mhi_wf_rac_lan'){
    const s=await fetchJSON('/api/wf-rac/status?draft=true');
    if(wfRacDirty||wfRacBusy||!s.configured||!s.fresh)
      throw Error(WT('Yhdistä WF-RAC ja odota tuoretta tilanlukua ennen jatkamista.'));
    if(s.draft_id)await wfRacCommit(s.draft_id);
    await wzPost({pump_adapter:'mhi_wf_rac_lan',setup_step:'5'});return;
  }
  if(WZ.adapter==='daikin_dsiot'){
    const s=await fetchJSON('/api/daikin-dsiot/status?draft=true');
    if(dsiotDirty||dsiotBusy||!s.configured||!s.fresh)
      throw Error(WT('Yhdistä Daikin DSIoT ja odota tuoretta tilanlukua ennen jatkamista.'));
    if(s.draft_id)await dsiotCommit(s.draft_id);
    await wzPost({pump_adapter:'daikin_dsiot',setup_step:'5'});return;
  }
  if(WZ.adapter==='daikin_local_api'){
    const s=await fetchJSON('/api/daikin/status?draft=true');
    if(daikinDirty || daikinBusy || s.configured!==true || s.fresh!==true)
      throw Error(WT('Yhdistä Daikin ja odota tuoretta tilanlukua ennen jatkamista.'));
    if(s.draft_id)await daikinCommit(s.draft_id);
    await wzPost({pump_adapter:'daikin_local_api',setup_step:'5'});return;
  }
  if(WZ.adapter==='gree_ewpe_lan'){
    const s=await fetchJSON('/api/gree/status?draft=true');
    if(greeDraft || greeBusy || s.configured!==true || s.fresh!==true)
      throw Error(WT('Yhdistä Gree ja odota tuoretta tilanlukua ennen jatkamista.'));
    if(s.draft_id)await greeCommit(s.draft_id);
    await wzPost({pump_adapter:'gree_ewpe_lan',setup_step:'5'});return;
  }
  const f={pump_adapter:WZ.adapter,setup_step:'5'};
  if(WZ.adapter==='mitsubishi_cn105_esphome')f.cn105_url=(WZ.cn105||'').trim();
  await wzPost(f);
}
function roomTempFields(source,offset){
  const raw=String(offset).trim().replace('−','-').replace(',','.');
  const value=raw===''?0:/^[+-]?\d+(\.\d{1,2})?$/.test(raw)?Number(raw):NaN;
  if(!['ruuvi','pump'].includes(source)||!Number.isFinite(value)||Math.abs(value)>5)
    throw Error(WT('Korjauksen tulee olla −5…+5 °C, enintään kaksi desimaalia.'));
  return {source,correction_c:value.toFixed(2)};
}
let refreshRoomTempSettings=null;
async function saveRoomTemp(draft){
  const status=state.status||{};
  const fields=roomTempFields(draft.source||status.room_temperature_source||'ruuvi',
    draft.offset??String(status.room_temperature_correction_c??0));
  const sourceEdit=draft.source,offsetEdit=draft.offset;
  await airPostLocal('/api/temperature-source',fields);
  if(draft.source===sourceEdit)delete draft.source;
  if(draft.offset===offsetEdit)delete draft.offset;
  draft.pending=draft.source!==undefined || draft.offset!==undefined;
  // The response confirms durable NVS. Keep an older in-flight status GET
  // from restoring the previous source before the other form sees this save.
  state.statusRequestSequence=(state.statusRequestSequence||0)+1;
  const current=state.status||{};
  const correction=Number(fields.correction_c);
  const changed=current.room_temperature_source!==fields.source ||
    current.room_temperature_correction_c!==correction;
  state.status={...current,room_temperature_source:fields.source,
    room_temperature_correction_c:correction,
    room_temperature_c:changed?null:current.room_temperature_c};
  if(refreshRoomTempSettings)refreshRoomTempSettings(state.status);
}
function roomTempForm(body,scope,draft){
  const id='room-temp-'+scope;
  const status=state.status||{};
  const source=wzEl('select',{id});
  source.append(wzEl('option',{value:'ruuvi'},'RuuviTag'),
    wzEl('option',{value:'pump'},WT('Pumpun oma anturi')));
  source.value=draft.source||status.room_temperature_source||'ruuvi';
  const offset=wzEl('input',{id:id+'-offset',inputmode:'decimal',
    value:draft.offset??String(status.room_temperature_correction_c??0)});
  const feedback=wzEl('p',{role:'status',class:'muted tiny'});
  source.addEventListener('change',()=>{
    draft.source=source.value;draft.pending=true;feedback.textContent='';
  });
  offset.addEventListener('input',()=>{
    draft.offset=offset.value;draft.pending=true;feedback.textContent='';
  });
  const refresh=latest=>{
    if(draft.source===undefined)source.value=latest.room_temperature_source||'ruuvi';
    if(draft.offset===undefined)offset.value=String(latest.room_temperature_correction_c??0);
  };
  const submit=wzEl('button',{type:'button',on:{click:async()=>{
    if(scope==='wizard'&&WZ.busy)return;
    if(scope==='wizard')WZ.busy=true;
    submit.disabled=true;
    try{
      await saveRoomTemp(draft);
      refresh(state.status);
      if(!draft.pending)feedback.textContent=WT('Tallennettu.');
      await reloadStatus();
    }catch(error){feedback.textContent=translateFi(error.message);}
    finally {
      submit.disabled=false;
      if(scope==='wizard')WZ.busy=false;
    }
  }}},WT('Tallenna lämpötilalähde'));
  body.append(
    wzEl('div',{class:'fieldrow'},
      wzEl('label',{class:'field','for':id},WT('Huonelämpötilan lähde'),source),
      wzEl('label',{class:'field','for':id+'-offset'},WT('Mittauskorjaus (°C)'),offset),submit),
    wzEl('p',{class:'muted tiny'},WT('Pumpun anturi mittaa laitteen lähellä. Voit verrata lukemaa huoneen mittariin ja lisätä enintään ±5 °C:n korjauksen.')),
    feedback);
  return refresh;
}
function wzStepComfort(body){
  const air=(state.status&&state.status.air)||{};
  const mode=Number.isInteger(WZ.comfort.mode)?WZ.comfort.mode:air.mode;
  body.append(wzEl('h2',{id:'wz-title'},WT('Lämpötila ja optimointi')),
    wzEl('p',{class:'muted'},WT('Aseta haluttu lämpötila ja rajat, joiden sisällä Lite saa hyödyntää sähkön hinnanvaihtelua.')));
  WZ.roomTemperature=WZ.roomTemperature||{};
  roomTempForm(body,'wizard',WZ.roomTemperature);
  const choices=[WT('Kylmillään'),WT('Kuivanapito'),WT('Ylläpitolämpö'),WT('Asuttu')];
  const select=wzEl('select',{id:'wz-mode'});
  for(const [i,name] of choices.entries())select.append(wzEl('option',{value:String(i)},name));
  select.value=String(mode);
  select.addEventListener('change',()=>{
    WZ.comfort.mode=Number(select.value);
    const target=WZ.comfort.mode===3?air.occupied_target_c:
      WZ.comfort.mode===2?air.hold_target_c:null;
    WZ.comfort.target=typeof target==='number'?String(target):'';
    wzRender();
  });
  body.append(wzEl('label',{class:'wz-label','for':'wz-mode'},WT('Lämmitystila')),select);
  const field=(id,label,key,placeholder)=>{
    const inp=wzEl('input',{id,inputmode:'decimal',autocomplete:'off',placeholder:placeholder||'',value:WZ.comfort[key]||''});
    inp.addEventListener('input',()=>{WZ.comfort[key]=inp.value;});
    body.append(wzEl('label',{class:'wz-label','for':id},label),inp);
  };
  if(wzTargetRange(mode))field('wz-c-target',WT('Haluttu sisälämpötila (°C)'),'target','21');
  const range=wzTargetRange(mode);
  body.append(wzEl('p',{class:'muted tiny'},range?
    (mode===3?WT('Asuttu-tila: '):WT('Ylläpitolämpö-tila: '))+range[0]+'–'+range[1]+' °C':
    WT('Tässä tilassa ei ole kiinteää huonetavoitetta.')));
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
    wzEl('p',{class:'muted tiny'},WT('Lite ei lupaa etukäteen tiettyä säästöä. Rajat pysyvät samoina kaikissa profiileissa; profiili määrää vain, kuinka paljon vaihteluväliä ja hintaeroa käytetään. Valitse automaattiohjaus yhteenvedossa.')));
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
    const air={...(previous.air||{}),mode};
    if(mode===2||mode===3)air[mode===3?'occupied_target_c':'hold_target_c']=target;
    state.status={...previous,air};
  }
}
async function wzSaveComfort(skip){
  if(skip){await wzPost({setup_step:'6'});return;}
  const air=(state.status&&state.status.air)||{};
  const mode=Number.isInteger(WZ.comfort.mode)?WZ.comfort.mode:air.mode;
  if(!Number.isInteger(air.mode))throw Error(WT('Laitteen tilaa ei voitu lukea.'));
  const r=wzComfortFields(WZ.comfort,mode);
  if(!r.ok)throw Error(r.error);
  const oldTarget=mode===3?air.occupied_target_c:air.hold_target_c;
  const target=r.target===null?oldTarget:r.target;
  const airChanged=mode!==air.mode || target!==oldTarget;
  // Keep the previous durable site state for compensation if a second write
  // fails. The site write MUST come first: an NVS-full site error must never
  // change the active/persistent mode target (Codex P2 #57).
  const prior=(state.site&&state.site.optimization)||{};
  const oldFields={optimization_profile:prior.profile||'balanced',
    comfort_min_c:prior.comfort_min_c==null?'':String(prior.comfort_min_c),
    comfort_max_c:prior.comfort_max_c==null?'':String(prior.comfort_max_c),
    setup_step:String((state.site&&state.site.region&&state.site.region.setup_step)||0)};
  const newFields={...r.fields,setup_step:'6'};
  if(airChanged) {
    // A durable intent allows the firmware to reconcile the mode and target
    // with the saved band after power loss between POSTs.
    newFields.comfort_txn_mode=String(mode);
    newFields.comfort_txn_target=String(target);
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
  if(airChanged){
    try {
      const request={mode:String(mode),delta:String(air.drying_delta_c),
        hold:String(mode===2?target:air.hold_target_c),
        occupied:String(mode===3?target:air.occupied_target_c)};
      await airPostLocal('/api/air/config',request);
      // The successful POST itself confirms a durable write. Update the
      // in-memory cache BEFORE any discardable status refresh is awaited.
      wzAcceptAirTarget(mode,target,null);
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
      if(observed&&observed.mode===mode&&currentTarget===target) {
        // A successful air POST can lose its reply. A verified independent
        // GET is also the new local state for Back -> Comfort. Otherwise the
        // next save may skip a required target change against stale status.
        wzAcceptAirTarget(mode,target,confirmedStatus);
        return;
      }
      if(!observed||observed.mode!==air.mode||currentTarget!==oldTarget){
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
function wzStopPolling(){
  if(WZ.sensorTimer){clearInterval(WZ.sensorTimer);WZ.sensorTimer=null;}
  if(WZ.shellyTimer){clearInterval(WZ.shellyTimer);WZ.shellyTimer=null;}
}
async function wzPollSensors(){
  try{
    const r=await fetch('/api/air/sensors',{cache:'no-store'});
    if(!r.ok)throw Error('HTTP '+r.status);
    WZ.sensors=await r.json();
  }catch(e){WZ.sensors={error:true,sensors:[]};}
  if(WZ.open&&WZ.step===6)wzRenderSensors();
}
async function wzBind(role,mac){
  const token=state.status&&state.status.air&&state.status.air.config_token;
  if(!token){wzSay(WT('Laite ei vastaa tai valtuutus puuttuu. Päivitä sivu.'),true);return;}
  try{
    await airPostLocal('/api/air/bind',{which:role,mac});
    WZ.confirmBind='';wzSay('');
    await reloadStatus();await wzPollSensors();
  }catch(e){wzSay(WT('Anturin sidonta epäonnistui: ')+e.message,true);}
}
async function wzNameSensor(mac,name){
  const token=state.status&&state.status.air&&state.status.air.config_token;
  if(!token)return;
  try{
    const r=await fetch('/api/air/sensor-name',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},
      body:new URLSearchParams({mac,name:name.trim(),token})});
    const j=await r.json().catch(()=>({}));
    if(j.saved!==true)wzSay(j.error==='TABLE_FULL'?WT('Nimitaulukko on täynnä (8 anturia).'):
      j.error==='BAD_NAME'?WT('Nimessä on kiellettyjä merkkejä tai se on liian pitkä (enintään 23 merkkiä).'):WT('Nimen tallennus epäonnistui.'),true);
    else wzSay('');
  }catch(e){wzSay(WT('Laite ei vastannut.'),true);}
}
function wzRenderSensors(){
  const box=document.getElementById('wz-sensors');if(!box)return;
  const info=document.getElementById('wz-scan-info');
  const data=WZ.sensors||{sensors:[]};
  // Never rebuild while the owner is typing a name.
  const act=document.activeElement;
  if(act&&act.dataset&&act.dataset.nameFor)return;
  if(info){
    info.textContent=data.error?WT('Anturilistaa ei voitu lukea laitteelta.'):
      data.ble_ready===false?WT('Bluetooth-skannaus ei ole vielä käynnissä.'):
      (data.scan_memory_denials>0&&!data.sensors.length)?WT('Skannaus on viivästynyt, koska laitteen muisti on käytössä muualla. Yritetään uudelleen.'):
      data.sensors.length?'':WT('Haetaan RuuviTageja… Anturi ilmestyy listaan, kun se lähettää mittauksen (noin minuutin välein).');
  }
  box.replaceChildren();
  const bound=data.bound||{};
  for(const x of data.sensors||[]){
    const row=wzEl('div',{class:'wz-sensor '+wzSensorState(x),'data-mac':x.mac});
    const nameInp=wzEl('input',{type:'text',maxlength:'23','data-name-for':x.mac,value:x.name||'',
      placeholder:WT('Nimi, esim. Olohuone'),'aria-label':WT('Anturin nimi')});
    nameInp.addEventListener('change',()=>wzNameSensor(x.mac,nameInp.value));
    row.append(wzEl('div',{class:'wz-srow'},wzEl('strong',{},x.name||x.mac),
        x.role?wzEl('span',{class:'wz-badge'},x.role==='indoor'?WT('Sisäanturi'):WT('Ulkoanturi')):''),
      wzEl('div',{class:'muted tiny'},x.mac+' · '+wzSignalText(x.rssi_dbm)),
      wzEl('div',{},wzSensorLine(x)),
      wzEl('div',{class:'muted tiny'},x.live?WT('Mittaus tuore: ')+wzAgeText(x.age_s):WT('Ei kuulu: viimeisin mittaus ')+wzAgeText(x.age_s)+(wzLang()==='en'?' ago':' sitten')),
      nameInp);
    const btns=wzEl('div',{class:'wz-row'});
    for(const [role,label] of [['indoor',WT('Käytä sisäanturina')],['outdoor',WT('Käytä ulkoanturina')]]){
      if(x.role===role)continue;
      const confirm=wzBindNeedsConfirm(data,role,x.mac);
      const asking=confirm&&WZ.confirmBind===x.mac+role;
      btns.append(wzEl('button',{type:'button',class:'wz-choice'+(asking?' sel':''),'data-bind':role,
        on:{click:()=>{if(confirm&&!asking){WZ.confirmBind=x.mac+role;wzRenderSensors();return;}wzBind(role,x.mac);}}},
        asking?WT('Vahvista: sisälämpötilahistoria tyhjennetään'):label));
    }
    if(x.role)btns.append(wzEl('button',{type:'button',class:'wz-choice','data-unbind':x.role,
      on:{click:()=>wzBind(x.role,'none')}},WT('Poista sidonta')));
    row.append(btns);
    box.append(row);
  }
  // A bound sensor that is not advertising must stay visible, not vanish.
  for(const role of ['indoor','outdoor']){
    const mac=bound[role];
    if(!mac||(data.sensors||[]).some(x=>x.mac===mac))continue;
    box.append(wzEl('div',{class:'wz-sensor stale','data-mac':mac},
      wzEl('div',{class:'wz-srow'},wzEl('strong',{},bound[role+'_name']||mac),
        wzEl('span',{class:'wz-badge'},role==='indoor'?WT('Sisäanturi'):WT('Ulkoanturi'))),
      wzEl('div',{class:'muted tiny'},WT('Sidottu anturi ei lähetä mittauksia juuri nyt. Optimointi ei käytä sen vanhaa lukemaa.')),
      wzEl('button',{type:'button',class:'wz-choice',on:{click:()=>wzBind(role,'none')}},WT('Poista sidonta'))));
  }
}
async function wzShellyScan(){
  const box=document.getElementById('wz-shelly-list');
  const btn=document.getElementById('wz-shelly-scan');
  if(btn)btn.disabled=true;
  if(box)box.textContent=WT('Etsitään Shelly-laitteita kotiverkosta…');
  for(let i=0;i<30;i++){
    let j;
    try{const r=await fetch('/api/shelly/discover',{cache:'no-store'});j=await r.json();}
    catch(e){if(box)box.textContent=WT('Laite ei vastannut. Yritä uudelleen.');break;}
    if(j.state==='pending'){await new Promise(r=>setTimeout(r,1000));continue;}
    if(j.state==='failed'){if(box)box.textContent=WT('Haku epäonnistui. Anna osoite käsin.');break;}
    WZ.shellyScan=j;wzRenderShelly();break;
  }
  if(btn)btn.disabled=false;
}
function wzRenderShelly(){
  const box=document.getElementById('wz-shelly-list');if(!box)return;
  box.replaceChildren();
  const list=(WZ.shellyScan&&WZ.shellyScan.devices)||[];
  if(!list.length){box.textContent=WT('Shelly-laitteita ei löytynyt. Tarkista, että laite on samassa verkossa, tai anna osoite käsin.');return;}
  for(const d of list){
    const ok=d.support==='SUPPORTED'&&Number.isFinite(d.power_w);
    const row=wzEl('div',{class:'wz-sensor '+(ok?'live':'stale'),'data-shelly':d.url},
      wzEl('div',{class:'wz-srow'},wzEl('strong',{},d.id||d.host),WZ.shelly===d.url?wzEl('span',{class:'wz-badge'},WT('Valittu')):''),
      wzEl('div',{class:'muted tiny'},d.url),wzEl('div',{},wzShellyText(d)));
    if(ok)row.append(wzEl('button',{type:'button',class:'wz-choice','data-use-shelly':d.url,
      on:{click:()=>{wzSetShellyAddress(d.url);wzRender();}}},WT('Käytä tätä mittaria')));
    box.append(row);
  }
}
function wzStepDevices(body){
  const site=state.site||{};
  if(!WZ.shellyScopeInitialized){WZ.shellyScope=site.shelly_scope||'unknown';WZ.shellyScopeFor=site.shelly_url||'';WZ.shellyScopeInitialized=true;}
  body.append(wzEl('h2',{id:'wz-title'},WT('Anturit ja kulutusmittari')),
    wzEl('h3',{},WT('Huonelämpötila-anturit (RuuviTag)')),
    wzEl('p',{class:'muted'},WT('Valitse listasta sisäanturi ja halutessasi ulkoanturi. Ilman tuoretta sisäanturia Lite ei pidä pumpun omaa lukemaa todistettuna huonelämpötilana.')),
    wzEl('p',{id:'wz-scan-info',class:'muted tiny',role:'status'}),
    wzEl('div',{id:'wz-sensors','aria-label':WT('Löydetyt anturit')}),
    wzEl('h3',{},WT('Sähkönkulutusmittari (valinnainen)')),
    wzEl('p',{class:'muted'},WT('Lite lukee Shelly-mittarin tehoa; se ei koskaan ohjaa Shellyn relettä.')),
    wzEl('button',{type:'button',id:'wz-shelly-scan',class:'wz-choice',on:{click:wzShellyScan}},WT('Etsi Shelly-mittarit kotiverkosta')),
    wzEl('div',{id:'wz-shelly-list',class:'muted tiny'}));
  const inp=wzEl('input',{id:'wz-shelly',autocomplete:'off',autocapitalize:'none',spellcheck:'false',value:WZ.shelly||'',placeholder:'http://192.168.1.30'});
  inp.addEventListener('input',()=>wzSetShellyAddress(inp.value));
  inp.addEventListener('change',()=>wzRender());
  body.append(wzEl('label',{class:'wz-label','for':'wz-shelly'},WT('Mittarin osoite (tai valitse yllä)')),inp,
    wzEl('p',{class:'wz-label'},WT('Mitä mittari mittaa?')));
  const group=wzEl('div',{class:'wz-list',role:'radiogroup','aria-label':WT('Mittarin mittauskohde')});
  for(const sc of WZ_SCOPES){
    const on=WZ.shellyScope===sc.id;
    group.append(wzEl('button',{type:'button',role:'radio','aria-checked':String(on),'data-scope':sc.id,
      class:'wz-choice'+(on?' sel':''),on:{click:()=>{WZ.shellyScope=sc.id;wzRender();}}},sc.label));
  }
  body.append(group,wzEl('p',{class:'muted tiny'},WT('Tarkistus: kytke lämpöpumppu päälle ja pois ja katso, muuttuuko teholuku vastaavasti. Vain lämpöpumppuun varmistettua mittaria käytetään pumpun kulutusmalliin; muuten luku näytetään vain tietona.')));
  wzRenderSensors();
  if(WZ.shellyScan)wzRenderShelly();
}
async function wzSaveDevices(){
  const f=wzDevicesFields({shelly:WZ.shelly,scope:WZ.shellyScope},{shelly_url:state.site&&state.site.shelly_url,
    shelly_scope:state.site&&state.site.shelly_scope});
  await wzPost(f);
}
function wzStepSummary(body){
  body.append(wzEl('h2',{id:'wz-title'},WT('Yhteenveto')));
  const list=wzSummary({site:state.site,status:state.status,prices:state.prices,weather:state.weather,plan:state.plan,midea:mideaStatus,gree:greeStatus,daikin:daikinStatus,wfRac:wfRacStatus,dsiot:dsiotStatus});
  const need=wzNeedsAction(list);
  body.append(wzEl('p',{class:need.length?'wz-msg bad':'wz-msg ok'},need.length?
    WT('Seuraavat asiat vaativat vielä toimenpiteitä:')+' '+need.length:WT('Kaikki on kunnossa.')));
  const ul=wzEl('ul',{class:'wz-sum'});
  const mark={ok:'✓',warn:'!',todo:'○',optional:'–'};
  for(const it of list)ul.append(wzEl('li',{class:'wz-'+it.state,'data-key':it.key},
    wzEl('span',{class:'wz-mark','aria-hidden':'true'},mark[it.state]),wzEl('span',{},wzEl('strong',{},it.label),' ',it.detail)));
  body.append(ul);
  wzSummaryControl(body);
  body.append(wzEl('p',{class:'muted tiny'},WT('Dashboardiin pääsee, vaikka kaikki ei olisi valmista. Jatka asetuksia myöhemmin Asetukset-näkymässä.')));
}
function wzControlSupported(adapter){
  return adapter==='midea_lan'||adapter==='mitsubishi_cn105_esphome'||
    adapter==='gree_ewpe_lan'||adapter==='daikin_local_api'||adapter==='mhi_wf_rac_lan';
}
async function wzFinish(){
  const choice=WZ.controlChoice;
  if(choice!==null){
    const s=await fetchJSON('/api/status');
    const basis=s.control_connection;
    if(!wzControlSupported(state.site?.pump_adapter)||!WZ.controlBasis||WZ.controlBasis!==basis)
      throw Error(translateFi(WT('Pumpun yhteysasetukset ovat muuttuneet. Tarkista yhteys ja valitse ohjaustapa uudelleen.')));
    if(choice && s.air?.mode!==2 && s.air?.mode!==3)
      throw Error(translateFi(WT('Automaattiohjaus on valittavissa Ylläpitolämpö- ja Asuttu-tilassa.')));
    if(choice!==s.control_armed)await requestControlPermission(choice,s.air?.config_token,basis);
  }
  await wzPost({setup_complete:'true',setup_step:String(WZ_DONE)});
}
function wzRestoreMideaPilot(){
  const p=document.getElementById('midea-pilot-panel');
  const anchor=document.getElementById('midea-pilot-anchor');
  if(p&&anchor&&anchor.nextElementSibling!==p)anchor.after(p);
}
function wzSummaryControl(body){
  const adapter=state.site?.pump_adapter;
  if(!wzControlSupported(adapter))return;
  const status=state.status||{};
  const selected=WZ.controlChoice??(status.control_armed===true);
  const title=WT('Lämpöpumpun ohjaus');
  const group=wzEl('div',{class:'wz-row',role:'group','aria-label':title});
  for(const [enabled,label] of [[false,WT('Käsiohjaus')],[true,WT('Automaattiohjaus')]]){
    group.append(wzEl('button',{type:'button',class:'wz-choice'+(selected===enabled?' sel':''),
      'aria-pressed':String(selected===enabled),on:{click:()=>{
        if(WZ.busy)return;
        WZ.controlChoice=enabled;WZ.controlBasis=state.status?.control_connection||'';
        wzSay('');wzRender();}}},label));
  }
  body.append(wzEl('h3',{},title),group,wzEl('p',{class:'muted tiny'},WT('Käsiohjaus jättää pumpun kaukosäätimen käyttöön. Tuetut käsikäskyt voi lähettää myös Liten Asetuksista.')));
  if(adapter==='gree_ewpe_lan'){
    const panel=document.getElementById('gree-pilot');
    if(panel)body.append(panel);
  }
  if(adapter==='mhi_wf_rac_lan'){
    const panel=document.getElementById('wf-rac-pilot');
    if(panel)body.append(panel);
  }
  if(adapter==='daikin_local_api'){
    const panel=document.getElementById('daikin-pilot');
    if(panel)body.append(panel);
  }
  if(adapter==='midea_lan'){
    const panel=document.getElementById('midea-pilot-panel');
    const id=document.getElementById('midea-pilot-id');
    if(id&&!id.value)id.value=document.getElementById('midea-id')?.value||'';
    if(panel)body.append(panel);
  }
}
const WZ_STEP_FNS=[wzStepRegion,wzStepCity,wzStepZone,wzStepTariff,wzStepPump,wzStepComfort,wzStepDevices,wzStepSummary];
const WZ_STEP_TITLES=[WT('Kieli ja maa'),WT('Sijainti'),WT('Sähkömarkkina-alue'),WT('Sähkösopimus'),WT('Lämpöpumppu'),
  WT('Lämpötila ja optimointi'),WT('Anturit ja mittari'),WT('Yhteenveto')];

function wzRender(){
  const body=document.getElementById('wz-body');if(!body)return;
  if(WZ.step===6&&WZ.open){if(!WZ.sensorTimer){wzPollSensors();WZ.sensorTimer=setInterval(wzPollSensors,3000);}}else wzStopPolling();
  wzRestoreMideaPanel(!wzMideaStep());
  wzRestoreMideaPilot();
  wzRestoreGreePanel();
  wzRestoreDaikinPanel();
  wzRestoreDsiotPanel();
  wzRestoreWfRacPanel();
  body.replaceChildren();
  WZ_STEP_FNS[WZ.step](body);
  wzUpdateMideaPolling();
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
    else if(s===5){
      if(!skip&&WZ.roomTemperature?.pending)await saveRoomTemp(WZ.roomTemperature);
      await wzSaveComfort(skip);
    }
    else if(s===6){if(skip)await wzPost({setup_step:'7'});else await wzSaveDevices();}
    else{
      await wzFinish();
      wzClose();switchView('home');await reloadStatus();return;
    }
    WZ.message='';WZ.step=Math.min(WZ_STEPS-1,WZ.step+1);
    if(WZ.step===WZ_STEPS-1){await reloadStatus();await reloadPrices();await reloadWeather();await reloadPlan();await reloadMidea();await reloadGree();await reloadDaikin();await reloadWfRac();await reloadDsiot();}
    wzRender();
  }catch(e){wzSay(e.message,true);}
  finally{WZ.busy=false;document.getElementById('wz-next').disabled=false;}
}
function wzClose(){
  WZ.open=false;
  wzStopPolling();
  wzUpdateMideaPolling();
  wzRestoreMideaPanel(true);
  wzRestoreMideaPilot();
  wzRestoreGreePanel();
  wzRestoreDaikinPanel();
  wzRestoreDsiotPanel();
  wzRestoreWfRacPanel(true);
  const w=document.getElementById('wizard');
  if(w)w.hidden=true;
  document.body.classList.remove('wz-open');
}
async function wzOpen(fromSettings){
  const w=document.getElementById('wizard');if(!w)return;
  try{await wzLoadRegions();}catch(e){
    // Without the region table the wizard cannot offer correct choices; say so.
    w.hidden=false;w.querySelector('#wz-body').textContent=WT('Maalistaa ei voitu lukea laitteelta. Päivitä sivu.');return;}
  const site=state.site||{},r=site.region||{};
  WZ.country=r.country||'';WZ.zone=r.price_zone||'';WZ.city=r.city||'';WZ.latlon=site.fmi_latlon||'';
  WZ.query=r.city||'';WZ.adapter=site.pump_adapter||'';WZ.cn105=site.cn105_url||'';WZ.shelly=site.shelly_url||'';WZ.shellyScopeInitialized=false;WZ.shellyScopeFor='';
  WZ.roomTemperature={};
  WZ.controlChoice=null;WZ.controlBasis='';
  const opt=site.optimization||{},air=(state.status&&state.status.air)||{};
  WZ.comfort={mode:air.mode,target:'',min:opt.comfort_min_c?String(opt.comfort_min_c):'',max:opt.comfort_max_c?String(opt.comfort_max_c):'',
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
<script id="timeline-script">
// ---- Plan timeline (price classes, controller headroom, outdoor forecast) ----
// Pure helpers (tl*) are unit tested in a VM; SVG rendering runs in the browser.
// Every text goes through TT() so the English dictionary test can find it.
// Honesty rules: no indoor-temperature prediction and no cost estimate (there is
// no calibrated building model); the live controller does not use the outdoor
// forecast, so the chart shows it as context and says so.
const TT=s=>s;
const TL_NS='http://www.w3.org/2000/svg';
const TL_CLASS_LABEL={cheap:TT('Edullinen'),neutral:TT('Normaali'),expensive:TT('Kallis'),unknown:TT('Ei vertailua')};
const TL_ACTIONS={HEAT_SETPOINT:TT('Ehdotus: lämmitys'),OFF:TT('Ehdotus: lämmitys pois'),HOLD:TT('Ehdotus: nykyinen tila pidetään'),UNAVAILABLE:TT('Ei päätöstä')};
const TL_REASONS={RUUVI_STALE:TT('huonelämpötila-anturin lukema ei ole tuore'),PRICES_UNAVAILABLE:TT('hintatietoa ei ole'),
  NO_HEATING_REQUESTED:TT('lämmitystä ei ole pyydetty tässä tilassa'),BELOW_ROOM_TARGET:TT('huone on tavoitetta kylmempi'),
  ABOVE_ROOM_TARGET:TT('huone on tavoitetta lämpimämpi'),LOW_COST_WINDOW:TT('sähkö on nyt edullista'),
  HIGH_COST_WINDOW:TT('sähkö on nyt kallista'),THERMOSTAT_NO_PRICES:TT('termostaatti ilman hintatietoa'),
  NEUTRAL_COST_WINDOW:TT('hinta ei poikkea merkittävästi'),HOLD_IN_DEADBAND:TT('huone on sallitulla alueella'),UNKNOWN:TT('syy ei tiedossa')};
function tlNum(v){return typeof v==='number'&&Number.isFinite(v)?v:null;}
// Strict shape check: anything malformed is "no timeline", never a half chart.
function tlValidate(d){
  if(!d||d.valid!==true||!Array.isArray(d.slots)||d.slots.length<2||!Array.isArray(d.segments))return null;
  let prev=null;
  for(const s of d.slots){
    if(tlNum(s.utc)===null||tlNum(s.total)===null||typeof s.class!=='string')return null;
    if(prev!==null&&s.utc!==prev+900)return null;
    prev=s.utc;
  }
  return d;
}
// A country/market change must invalidate cached prices before any re-render.
// Currency alone is insufficient (FI and EE both use EUR).
function tlMarketKey(region){
  return region&&region.price_region_resolved===true&&
    typeof region.nordpool_area==='string'&&region.nordpool_area.length>0&&
    typeof region.currency==='string'&&/^[A-Z]{3}$/.test(region.currency)?
    region.nordpool_area+'|'+region.currency:null;
}
function tlMatchesMarket(timeline,region){
  const key=tlMarketKey(region);
  return key!==null&&timeline&&timeline.area+'|'+timeline.currency===key;
}
// Layout in a W x H box. Price bars from 0 (or the negative minimum) to the max.
function tlLayout(slots,W,H,m){
  const left=m.left,right=m.right,top=m.top,bottom=m.bottom;
  const plotW=W-left-right,plotH=H-top-bottom;
  const t0=slots[0].utc,t1=slots[slots.length-1].utc+900;
  let lo=0,hi=0;
  for(const s of slots){if(s.total<lo)lo=s.total;if(s.total>hi)hi=s.total;}
  if(hi-lo<1e-9)hi=lo+0.01;
  const x=t=>left+(t-t0)/(t1-t0)*plotW;
  const y=v=>top+plotH-(v-lo)/(hi-lo)*plotH;
  return {left,right,top,bottom,plotW,plotH,t0,t1,lo,hi,x,y,barW:Math.max(1,plotW/slots.length-0.4)};
}
function tlSlotIndex(layout,slots,px){
  const f=(px-layout.left)/layout.plotW;
  if(!(f>=0&&f<=1))return -1;
  return Math.min(slots.length-1,Math.max(0,Math.floor(f*slots.length)));
}
// Linear interpolation inside the hourly forecast; no extrapolation beyond 90 min.
function tlOutdoorAt(weather,utc){
  const pts=(weather&&weather.valid===true&&Array.isArray(weather.slots)?weather.slots:[])
    .filter(p=>tlNum(p.utc)!==null&&tlNum(p.temperature_c)!==null).sort((a,b)=>a.utc-b.utc);
  if(!pts.length)return null;
  if(utc<pts[0].utc)return pts[0].utc-utc<=5400?pts[0].temperature_c:null;
  const last=pts[pts.length-1];
  if(utc>last.utc)return utc-last.utc<=5400?last.temperature_c:null;
  for(let i=1;i<pts.length;i++){
    if(utc<=pts[i].utc){
      const a=pts[i-1],b=pts[i],dt=b.utc-a.utc;
      if(dt<=0||dt>7200)return null;                       // gap in the forecast: no bridging
      return a.temperature_c+(b.temperature_c-a.temperature_c)*(utc-a.utc)/dt;
    }
  }
  return null;
}
// Contiguous runs of forecast samples (every second slot, mid-slot). A missing
// sample ends the run, so a forecast gap is drawn as a gap, never as a line.
function tlOutdoorRuns(weather,slots){
  const runs=[];let cur=[];
  for(let i=0;i<slots.length;i+=2){
    const t=slots[i].utc+450,v=tlOutdoorAt(weather,t);
    if(v===null){if(cur.length)runs.push(cur);cur=[];}
    else cur.push([t,v]);
  }
  if(cur.length)runs.push(cur);
  return runs;
}
function tlDeg(v){return (Math.round(v*10)/10).toFixed(1).replace('.',',');}
// What the controller does in a slot of this class (its profile headroom).
function tlControlText(cls,meta){
  // Modes without a room target (Cold) never request heating: describe only the
  // price class there, never what the controller "would" do.
  if(tlNum(meta&&meta.target_c)===null){
    if(cls==='cheap')return TT('Edullinen hinta. Tässä tilassa ei ohjata lämmitystä.');
    if(cls==='expensive')return TT('Kallis hinta. Tässä tilassa ei ohjata lämmitystä.');
    if(cls==='neutral')return TT('Normaali hinta. Tässä tilassa ei ohjata lämmitystä.');
    return TT('Vertailuikkunaa ei ole riittävästi (hintoja on alle 2 tunnin ajalle).');
  }
  if(meta&&meta.indoor_fresh===false)
    return TT('Huoneanturin mittaus ei ole tuore; lämmitysehdotusta ei tehdä ennen uutta mittausta.');
  if(meta&&meta.heating_permitted===false)
    return TT('Kuivanapidon kosteusehto estää lämmitysehdotuksen; sähkönhinta ei muuta sitä.');
  const pre=tlNum(meta.preheat_c)||0,coast=tlNum(meta.coast_c)||0;
  if(cls==='cheap')return pre>0.05?TT('Edullinen: lämmitys sallittu tavoitteen yläpuolelle asti, enintään +')+tlDeg(pre)+' °C.':
    TT('Edullinen: lämmitetään vain, jos huone on tavoitetta kylmempi.');
  if(cls==='expensive')return coast>0.05?TT('Kallis: lämmitys pidetään pois, kunnes huone on laskenut tavoitteen alle enintään ')+tlDeg(coast)+' °C.':
    TT('Kallis: ei lämmitetä, kun huone on tavoitteessa tai sen yli.');
  if(cls==='neutral')return TT('Normaali hinta: termostaatti tavoitteen ±0,5 °C sisällä.');
  return TT('Vertailuikkunaa ei ole riittävästi (hintoja on alle 2 tunnin ajalle).');
}
function tlNowText(now){
  if(!now)return TT('Ei päätöstä');
  const act=TL_ACTIONS[now.action]||TL_ACTIONS.UNAVAILABLE;
  const sp=now.action==='HEAT_SETPOINT'&&tlNum(now.setpoint_c)!==null?' '+tlDeg(now.setpoint_c)+' °C':'';
  // MPC uses an internal thermostat reason as an implementation detail.
  // Do not tell the owner it is a price/temperature decision.
  if(now.source==='MPC')return TT('MPC-ohjaus:')+' '+act+sp;
  return act+sp+' — '+(TL_REASONS[now.reason]||TL_REASONS.UNKNOWN);
}
// One sentence per period from the real numbers of that period.
function tlSegmentText(seg,meta,fmtPrice,fmtRange){
  const dev=Math.abs(seg.avg_median)>1e-9?Math.round(Math.abs(seg.avg_total-seg.avg_median)/Math.abs(seg.avg_median)*100):null;
  const head=(seg.class==='cheap'?TT('Edullinen jakso '):TT('Kallis jakso '))+fmtRange(seg.start,seg.end)+': '+
    TT('keskihinta ')+fmtPrice(seg.avg_total)+(dev===null?'':' ('+dev+' %'+(seg.class==='cheap'?TT(' vertailumediaania alempi)'):TT(' vertailumediaania korkeampi)')));
  return head+'. '+tlControlText(seg.class,meta);
}
function tlTicks(layout,slots,tzHour){
  // Every 6 local hours; tzHour(utc) returns the local hour 0..23.
  const out=[];
  for(let t=layout.t0;t<layout.t1;t+=900){
    if(tzHour(t)%6===0&&tzHour(t-900)!==tzHour(t))out.push(t);
  }
  return out;
}

// ---- Rendering (browser only) ------------------------------------------------
function tlSvg(tag,attrs,...kids){
  const e=document.createElementNS(TL_NS,tag);
  for(const [k,v] of Object.entries(attrs||{}))e.setAttribute(k,String(v));
  for(const kid of kids)e.append(kid&&kid.nodeType?kid:document.createTextNode(String(kid)));
  return e;
}
// ctx: {fmtPrice(v), fmtTime(utc), fmtRange(a,b), tzHour(utc), dayLabel(utc), lang}
function tlRender(host,data,weather,ctx){
  host.replaceChildren();
  const d=tlValidate(data);
  if(!d){return false;}
  const W=ctx.W||720,H=ctx.H||270,m={left:W<560?40:46,right:W<560?38:42,top:16,bottom:46};
  const L=tlLayout(d.slots,W,H,m);
  const svg=tlSvg('svg',{viewBox:'0 0 '+W+' '+H,class:'tl-svg',role:'img',
    'aria-label':ctx.label,preserveAspectRatio:'xMidYMid meet'});
  // price bars, coloured by the controller's own class
  const zero=L.y(0);
  d.slots.forEach(s=>{
    const y=L.y(s.total),top=Math.min(y,zero),h=Math.max(1,Math.abs(zero-y));
    svg.append(tlSvg('rect',{x:L.x(s.utc).toFixed(2),y:top.toFixed(2),width:L.barW.toFixed(2),height:h.toFixed(2),
      class:'tl-bar tl-'+(['cheap','neutral','expensive'].includes(s.class)?s.class:'unknown')}));
  });
  // grid + time axis (every 6 local hours, day change marked)
  for(const t of tlTicks(L,d.slots,ctx.tzHour)){
    const x=L.x(t);
    svg.append(tlSvg('line',{x1:x,x2:x,y1:L.top,y2:L.top+L.plotH,class:'tl-grid'}));
    svg.append(tlSvg('text',{x:x,y:H-26,class:'tl-axis','text-anchor':'middle'},ctx.fmtTime(t)));
    if(ctx.tzHour(t)===0)svg.append(tlSvg('text',{x:x+3,y:L.top+10,class:'tl-day'},ctx.dayLabel(t)));
  }
  // price axis (left): 0 and max
  svg.append(tlSvg('text',{x:L.left-4,y:L.y(L.hi)+4,class:'tl-axis','text-anchor':'end'},ctx.fmtAxis(L.hi)));
  svg.append(tlSvg('text',{x:L.left-4,y:zero+4,class:'tl-axis','text-anchor':'end'},ctx.fmtAxis(0)));
  // outdoor forecast (right axis) - context only
  const runs=tlOutdoorRuns(weather,d.slots);
  const out=[].concat(...runs);
  if(out.length>=2){
    let lo=Infinity,hi=-Infinity;out.forEach(p=>{lo=Math.min(lo,p[1]);hi=Math.max(hi,p[1]);});
    if(hi-lo<2){const c=(hi+lo)/2;lo=c-1;hi=c+1;}
    const oy=v=>L.top+L.plotH-(v-lo)/(hi-lo)*L.plotH;
    for(const run of runs){
      if(run.length>=2)svg.append(tlSvg('polyline',{class:'tl-outdoor',fill:'none',points:run.map(p=>L.x(p[0]).toFixed(1)+','+oy(p[1]).toFixed(1)).join(' ')}));
      else svg.append(tlSvg('circle',{class:'tl-outdoor-dot',cx:L.x(run[0][0]).toFixed(1),cy:oy(run[0][1]).toFixed(1),r:2}));
    }
    svg.append(tlSvg('text',{x:W-m.right+4,y:oy(hi)+4,class:'tl-axis tl-outdoor-text'},tlDeg(hi)+' °C'));
    svg.append(tlSvg('text',{x:W-m.right+4,y:oy(lo)+4,class:'tl-axis tl-outdoor-text'},tlDeg(lo)+' °C'));
  }
  // period lane with numbers matching the reasons list
  const laneY=L.top+L.plotH+6;
  d.segments.forEach((g,i)=>{
    const x0=L.x(g.start),x1=L.x(g.end);
    svg.append(tlSvg('rect',{x:x0,y:laneY,width:Math.max(3,x1-x0),height:12,rx:3,class:'tl-lane tl-'+g.class}));
    if(x1-x0>12)svg.append(tlSvg('text',{x:(x0+x1)/2,y:laneY+10,class:'tl-lane-n','text-anchor':'middle'},i+1));
  });
  // "now" marker at the left edge of the horizon
  svg.append(tlSvg('line',{x1:L.left,x2:L.left,y1:L.top-4,y2:L.top+L.plotH,class:'tl-now'}));
  svg.append(tlSvg('text',{x:L.left+3,y:L.top-4,class:'tl-now-text'},ctx.nowLabel));
  const hit=tlSvg('rect',{x:L.left,y:L.top,width:L.plotW,height:L.plotH,fill:'transparent',class:'tl-hit'});
  svg.append(hit);
  const mark=tlSvg('line',{class:'tl-cursor',y1:L.top,y2:L.top+L.plotH,x1:-10,x2:-10});
  svg.append(mark);
  host.append(svg);
  const tip=document.createElement('div');tip.className='tl-tip';tip.hidden=true;tip.setAttribute('role','status');
  host.append(tip);
  const show=ev=>{
    const box=svg.getBoundingClientRect();
    const px=(ev.clientX-box.left)/box.width*W;
    const i=tlSlotIndex(L,d.slots,px);
    if(i<0){tip.hidden=true;mark.setAttribute('x1',-10);mark.setAttribute('x2',-10);return;}
    const s=d.slots[i],o=tlOutdoorAt(weather,s.utc+450);
    const x=L.x(s.utc)+L.barW/2;
    mark.setAttribute('x1',x);mark.setAttribute('x2',x);
    tip.replaceChildren();
    const line=(t,strong)=>{const p=document.createElement(strong?'strong':'div');p.textContent=t;tip.append(p);};
    line(ctx.fmtTime(s.utc)+' · '+ctx.fmtPrice(s.total),true);
    line(ctx.classLabel(s.class)+(s.class==='unknown'?'':' · '+ctx.medianLabel+' '+ctx.fmtPrice(s.median)));
    line(tlControlText(s.class,d));
    line(o===null?ctx.noOutdoor:ctx.outdoorLabel+' '+tlDeg(o)+' °C');
    tip.hidden=false;
    const left=Math.min(Math.max(0,(x/W)*box.width-110),Math.max(0,box.width-230));
    tip.style.left=left+'px';
  };
  hit.addEventListener('pointermove',show);
  hit.addEventListener('pointerdown',show);
  hit.addEventListener('pointerleave',()=>{tip.hidden=true;mark.setAttribute('x1',-10);mark.setAttribute('x2',-10);});
  return true;
}
</script>
<script id="history-script">
// ---- History view (measured data only; gaps stay gaps) -------------------------
// Pure helpers (hs*) are unit tested in a VM; SVG rendering runs in the browser.
// Every text goes through HT() so the English dictionary test can find it.
const HT=s=>s;
const HS_RANGES=[{id:'24h',label:HT('24 h')},{id:'7d',label:HT('7 vrk')},{id:'30d',label:HT('30 vrk')}];
const HS_SOURCES={1:HT('Wi-Fi'),2:HT('Lämpöpumpun tiedonsiirto'),3:HT('Huonelämpötila-anturi'),4:HT('Hintatiedot')};
function hsNum(v){return typeof v==='number'&&Number.isFinite(v)?v:null;}
// Strict shape check: malformed data is "no history", never a half chart.
function hsValidate(d){
  if(!d||d.valid!==true||!Array.isArray(d.points)||d.points.length<2||!Array.isArray(d.events))return null;
  const step=hsNum(d.step_s);if(!step||step<900)return null;
  let prev=null;
  for(const p of d.points){
    if(hsNum(p.t)===null)return null;
    if(prev!==null&&p.t!==prev+step)return null;
    prev=p.t;
  }
  return d;
}
// Polylines that BREAK at missing values and at forecast/measurement changes:
// the line is never drawn across a gap.
function hsSegments(points,key,splitKey){
  const segs=[];let cur=null;
  for(const p of points){
    const v=hsNum(p[key]);
    if(v===null){cur=null;continue;}
    const tag=splitKey?p[splitKey]:0;
    if(!cur||cur.tag!==tag){cur={tag,pts:[]};segs.push(cur);}
    cur.pts.push({t:p.t,v});
  }
  return segs;
}
function hsExtent(points,keys){
  let lo=Infinity,hi=-Infinity;
  for(const p of points)for(const k of keys){const v=hsNum(p[k]);if(v!==null){lo=Math.min(lo,v);hi=Math.max(hi,v);}}
  if(!Number.isFinite(lo))return null;
  if(hi-lo<1){const c=(hi+lo)/2;lo=c-0.5;hi=c+0.5;}
  const pad=(hi-lo)*0.08;
  return {lo:lo-pad,hi:hi+pad};
}
function hsDuration(s){
  if(!Number.isFinite(s)||s<0)return '—';
  if(s<90)return Math.round(s)+' s';
  if(s<5400)return Math.round(s/60)+' min';
  if(s<172800)return (Math.round(s/360)/10).toString().replace('.',',')+' h';
  return Math.round(s/86400)+' vrk';
}
// One sentence per event, from the event's own numbers.
function hsEventText(e,fmtDuration){
  const src=HS_SOURCES[e.s]||'?';
  switch(e.k){
    case 1:return HT('Laite käynnistyi (uudelleenkäynnistyksen syykoodi ')+e.v+')';
    case 2:return src+HT(' katkesi');
    case 3:return src+HT(' palasi, katko kesti ')+fmtDuration(e.v);
    case 4:return HT('Lite lähetti lämpöpumpulle ohjauskomentoja: ')+e.v;
    case 5:return HT('Pumppu kuittasi ohjauskomentoja: ')+e.v;
    case 6:return HT('Ohjauskomentoja epäonnistui: ')+e.v;
    case 8:return HT('Huonelämpötilan lähde vaihdettu, anturi vaihdettu tai mittauskorjausta muutettu.')+
      ' '+(e.a===1?HT('Pumpun oma anturi'):'RuuviTag')+' · '+(e.v/100)+' °C';
    default:return HT('Tuntematon tapahtuma');
  }
}
function hsEventClass(e){return e.k===2||e.k===6?'bad':e.k===3||e.k===5?'ok':'info';}
// Composite sentences are translated as ONE template, then filled in, so short
// words never end up half-translated.
function hsT(template,vars){
  let t=typeof translateFi==='function'?translateFi(template):template;
  for(const [k,v] of Object.entries(vars||{}))t=t.split('{'+k+'}').join(String(v));
  return t;
}
function hsCoverageText(d){
  const total=hsNum(d.slots_total)||0,present=hsNum(d.slots_present)||0;
  if(!total)return '—';
  return hsT(HT('Tietoa on tallennettu {pct} % ajasta ({present} / {total} jaksoa)'),
    {pct:Math.round(present/total*100),present,total});
}
function hsLowCoverage(d){const t=hsNum(d.slots_total)||0;return t>0&&(hsNum(d.slots_present)||0)/t<0.8;}
// CSV export of exactly what is shown; missing values are empty cells.
function hsCsv(d){
  const rows=['utc_start,records,indoor_c,humidity_pct,outdoor_c,outdoor_source,pump_setpoint_c,power_w,power_scope,power_coverage_min,price_per_kwh,price_currency,proposal,control_armed'];
  const acts=['unknown','off','heat','hold'];
  const cell=v=>hsNum(v)===null?'':String(v);
  for(const p of d.points){
    rows.push([new Date(p.t*1000).toISOString(),p.n,cell(p.in),cell(p.rh),cell(p.out),
      p.out==null?'':p.os===3?'mixed':p.os===2?'forecast':p.os===1?'measured':'unknown',
      cell(p.sp),cell(p.w),p.w==null?'':p.scope||'unknown',p.w==null?'':cell(p.power_coverage_minutes),
      cell(p.p),p.p==null?'':p.pc||'unknown',acts[p.a]||'unknown',p.r?1:0].join(','));
  }
  return rows.join('\n')+'\n';
}
function hsEnergyText(d,fmtKwh){
  // The meter's target changed inside this range: readings taken under different
  // targets are never added together or labelled with today's setting.
  if(d.meter_scope==='mixed')return HT('Mittarin mittauskohde on vaihtunut tällä jaksolla: energiaa ei lasketa yhteen eri mittauskohteiden kesken.');
  const kwh=hsNum(d.kwh);
  if(kwh===null)return d.power_coverage_minutes===0&&d.slots_present>0?HT('Mittauskattavuus ei riitä energian laskentaan.'):HT('Ei kulutuslukemia tältä ajalta.');
  const scope=d.meter_scope==='heat_pump'?HT('Mittari mittaa vain lämpöpumppua'):
    d.meter_scope==='whole_house'?HT('Mittari mittaa koko talon: lukua ei voi pitää lämpöpumpun kulutuksena'):
    HT('Mittarin mittauskohdetta ei ole varmistettu: lukua ei voi pitää lämpöpumpun kulutuksena');
  return fmtKwh(kwh)+' kWh · '+(hsNum(d.kwh_slots)||0)+' '+HT('mitattua jaksoa')+' · '+hsT(HT('Tehon mittauskattavuus {n} / {total} min'),{n:d.power_coverage_minutes??0,total:(d.slots_total||0)*15})+' · '+scope;
}

// ---- Rendering (browser only) -------------------------------------------------
// ctx: {W,H,fmtTime,dayLabel,tzHour,fmtVal(key,v),label}
// Long views label days (local midnights, found where the local hour wraps);
// short views label every 6th local hour.
function hsAxisDaily(t0,t1,step){return (t1-t0)>3*86400||step>=10800;}
function hsAxisTicks(t0,t1,step,tzHour){
  const out=[],daily=hsAxisDaily(t0,t1,step);
  for(let t=t0+step;t<t1;t+=step){
    const h=tzHour(t),hp=tzHour(t-step);
    if(daily?h<hp:(h!==hp&&h%6===0))out.push(t);
  }
  return out;
}
function hsLineChart(host,d,ctx,spec){
  const W=ctx.W,H=spec.H||ctx.H,m={left:W<560?38:46,right:W<560?10:14,top:12,bottom:30};
  const pw=W-m.left-m.right,ph=H-m.top-m.bottom;
  const ext=hsExtent(d.points,spec.keys);
  const svg=tlSvg('svg',{viewBox:'0 0 '+W+' '+H,class:'tl-svg',role:'img','aria-label':spec.label,preserveAspectRatio:'xMidYMid meet'});
  if(!ext){host.append(Object.assign(document.createElement('div'),{className:'empty',textContent:spec.empty}));return;}
  const t0=d.points[0].t,t1=d.points[d.points.length-1].t+d.step_s;
  const x=t=>m.left+(t-t0)/(t1-t0)*pw,y=v=>m.top+ph-(v-ext.lo)/(ext.hi-ext.lo)*ph;
  for(const t of hsAxisTicks(t0,t1,d.step_s,ctx.tzHour)){
    svg.append(tlSvg('line',{x1:x(t),x2:x(t),y1:m.top,y2:m.top+ph,class:'tl-grid'}));
    svg.append(tlSvg('text',{x:x(t),y:H-12,class:'tl-axis','text-anchor':'middle'},hsAxisDaily(t0,t1,d.step_s)||ctx.tzHour(t)===0?ctx.dayLabel(t):ctx.fmtTime(t)));
  }
  svg.append(tlSvg('text',{x:m.left-4,y:y(ext.hi)+10,class:'tl-axis','text-anchor':'end'},spec.fmtAxis(ext.hi)));
  svg.append(tlSvg('text',{x:m.left-4,y:y(ext.lo),class:'tl-axis','text-anchor':'end'},spec.fmtAxis(ext.lo)));
  if(spec.bars){
    const bw=Math.max(1,pw/d.points.length-0.5);
    const base=ext.lo<0&&ext.hi>0?y(0):m.top+ph;
    for(const p of d.points){const v=hsNum(p[spec.bars]);if(v===null)continue;
      const top=Math.min(y(v),base);
      svg.append(tlSvg('rect',{x:x(p.t),y:top,width:bw,height:Math.max(1,Math.abs(base-y(v))),class:'hs-bar '+(spec.barClass||'')}));}
  }
  for(const s of spec.lines||[]){
    for(const seg of hsSegments(d.points,s.key,s.split)){
      const cls='hs-line '+s.cls+(s.dashWhen&&(seg.tag===s.dashWhen||seg.tag===3)?' hs-dash':'');
      if(seg.pts.length===1){svg.append(tlSvg('circle',{cx:x(seg.pts[0].t+d.step_s/2),cy:y(seg.pts[0].v),r:2.5,class:'hs-dot '+s.cls}));continue;}
      const pts=seg.pts.map(q=>x(q.t+d.step_s/2).toFixed(1)+','+y(q.v).toFixed(1));
      if(s.step){ // pump setpoint: stepped, it only changes when commanded
        const stepped=[];seg.pts.forEach((q,i)=>{const xx=x(q.t).toFixed(1),yy=y(q.v).toFixed(1);
          if(i)stepped.push(xx+','+stepped[stepped.length-1].split(',')[1]);stepped.push(xx+','+yy);});
        stepped.push(x(seg.pts[seg.pts.length-1].t+d.step_s).toFixed(1)+','+y(seg.pts[seg.pts.length-1].v).toFixed(1));
        svg.append(tlSvg('polyline',{points:stepped.join(' '),fill:'none',class:cls}));
      } else svg.append(tlSvg('polyline',{points:pts.join(' '),fill:'none',class:cls}));
    }
  }
  if(spec.lane){ // controller proposal lane: heat / off / hold, armed marker above
    const ly=m.top+ph+3;
    for(const p of d.points){
      if(!p.n)continue;
      svg.append(tlSvg('rect',{x:x(p.t),y:ly,width:Math.max(1,pw/d.points.length-0.3),height:5,class:'hs-lane hs-a'+(p.a||0)}));
      if(p.r)svg.append(tlSvg('circle',{cx:x(p.t)+pw/d.points.length/2,cy:ly-1.5,r:1.6,class:'hs-armed'}));
    }
  }
  const mark=tlSvg('line',{class:'tl-cursor',y1:m.top,y2:m.top+ph,x1:-10,x2:-10});
  const hit=tlSvg('rect',{x:m.left,y:m.top,width:pw,height:ph,fill:'transparent',class:'tl-hit'});
  svg.append(mark,hit);
  host.append(svg);
  const tip=document.createElement('div');tip.className='tl-tip';tip.hidden=true;host.append(tip);
  const show=ev=>{
    const box=svg.getBoundingClientRect();
    const px=(ev.clientX-box.left)/box.width*W;
    const f=(px-m.left)/pw;
    if(!(f>=0&&f<=1)){tip.hidden=true;return;}
    const p=d.points[Math.min(d.points.length-1,Math.floor(f*d.points.length))];
    const xx=x(p.t)+pw/d.points.length/2;
    mark.setAttribute('x1',xx);mark.setAttribute('x2',xx);
    tip.replaceChildren();
    const line=(t,strong)=>{const e=document.createElement(strong?'strong':'div');e.textContent=t;tip.append(e);};
    line(ctx.fmtTime(p.t),true);
    if(!p.n){line(spec.noData);}
    else for(const row of spec.tip(p))line(row);
    tip.hidden=false;
    tip.style.left=Math.min(Math.max(0,xx/W*box.width-100),Math.max(0,box.width-210))+'px';
  };
  hit.addEventListener('pointermove',show);hit.addEventListener('pointerdown',show);
  hit.addEventListener('pointerleave',()=>{tip.hidden=true;mark.setAttribute('x1',-10);mark.setAttribute('x2',-10);});
}
</script>
<script id="health-script">
// ---- Data freshness, offline behaviour and firmware-update summary ----------------
// Pure helpers (fr*, ota*) are unit tested in a VM. Every text goes through FT() so
// the English dictionary test can find it. Facts come from the device only; nothing
// here decides or predicts anything.
const FT=s=>s;
function frNum(v){return typeof v==='number'&&Number.isFinite(v)?v:null;}
// Strict shape check: anything malformed is "no report", never half a report.
function frValidate(f){
  if(!f||typeof f.fallback!=='string'||!Array.isArray(f.sources)||f.sources.length!==4)return null;
  const ids=f.sources.map(s=>s&&s.id).join(',');
  if(ids!=='prices,weather,sensor,pump')return null;
  return f;
}
function frSource(f,id){return f.sources.find(s=>s.id===id);}
// {dot:'ok'|'warn'|'bad'|'off', title, detail} for one source. fmt: {time(utc), age(sec), error(code)}.
function frSourceText(src,fmt){
  const last=frNum(src.last_ok_utc)>0?fmt.time(src.last_ok_utc):null;
  const until=frNum(src.valid_until_utc)>0?fmt.time(src.valid_until_utc):null;
  const age=frNum(src.age_s)!==null&&src.age_s>=0?fmt.age(src.age_s):null;
  const st=src.state;
  switch(src.id){
  case 'prices':
    return st==='ok'?{dot:'ok',title:FT('Sähkön hinnat'),detail:FT('Voimassa ')+until}:
      st==='short'?{dot:'warn',title:FT('Sähkön hinnat'),detail:FT('Hinnat päättyvät ')+until+FT(': alle 2 tuntia jäljellä, ohjaus siirtyy termostaattiin.')}:
      st==='stale'?{dot:'bad',title:FT('Sähkön hinnat'),detail:FT('Haettu ')+(last||'—')+FT(', liian vanhoja (yli 36 h): niitä ei käytetä.')}:
      st==='expired'?{dot:'bad',title:FT('Sähkön hinnat'),detail:FT('Voimassaolo päättyi ')+(until||'—')+FT(': vanhoja hintoja ei käytetä uutena tietona.')}:
      st==='error'?{dot:'bad',title:FT('Sähkön hinnat'),detail:fmt.error(src.error)+(last?' '+FT('Viimeisin onnistunut haku ')+last+'.':'')}:
      st==='not_configured'?{dot:'off',title:FT('Sähkön hinnat'),detail:FT('Sähkömarkkina-aluetta ei ole valittu.')}:
      {dot:'bad',title:FT('Sähkön hinnat'),detail:FT('Hintoja ei ole vielä haettu.')};
  case 'weather':
    return st==='ok'?{dot:'ok',title:FT('Sääennuste'),detail:FT('Voimassa ')+until+(last?FT(' · haettu ')+last:'')}:
      st==='expired'?{dot:'warn',title:FT('Sääennuste'),detail:FT('Voimassaolo päättyi ')+(until||'—')+FT(': ennustetta ei käytetä.')}:
      st==='stale'?{dot:'warn',title:FT('Sääennuste'),detail:FT('Kellonaikaa ei ole synkronoitu: ennustetta ei voi käyttää.')}:
      st==='not_configured'?{dot:'off',title:FT('Sääennuste'),detail:FT('Sijaintia ei ole valittu.')}:
      {dot:'warn',title:FT('Sääennuste'),detail:FT('Ennustetta ei ole vielä haettu.')};
  case 'sensor':
    return st==='ok'?{dot:'ok',title:FT('Huonelämpötila-anturi'),detail:FT('Tuore mittaus')+(age?' ('+age+')':'')}:
      st==='not_configured'?{dot:'off',title:FT('Huonelämpötila-anturi'),detail:FT('Anturia ei ole sidottu.')}:
      {dot:'bad',title:FT('Huonelämpötila-anturi'),detail:FT('Ei tuoretta mittausta')+(age?FT(': viimeisin ')+age:'')+FT('. Lämmitysehdotusta ei tehdä.')};
  default:
    return st==='ok'?{dot:'ok',title:FT('Lämpöpumpun tiedot'),detail:FT('Tietoa tulee')+(age?' ('+age+')':'')}:
      st==='not_configured'?{dot:'off',title:FT('Lämpöpumpun tiedot'),detail:FT('Lämpöpumppua ei ole valittu.')}:
      {dot:'warn',title:FT('Lämpöpumpun tiedot'),detail:FT('Ei tuoretta tietoa pumpulta')+(age?FT(': viimeisin ')+age:'')+'.'};
  }
}
// Overall statement: what the controller does with the data it has right now.
function frOverall(f,fmt){
  const prices=frSource(f,'prices'),expiry=frNum(f.next_expiry_utc)>0?fmt.time(f.next_expiry_utc):null;
  const out=[];
  if(!f.clock_ok)out.push(FT('Kellonaikaa ei ole synkronoitu: hintoja ei käytetä, ennen kuin aika on tiedossa.'));
  if(f.offline)out.push(f.fallback==='price_control'&&expiry?
    FT('Ei Wi-Fi-yhteyttä. Tallennetut hinnat ovat voimassa ')+expiry+FT(' asti, mutta Wi-Fi-pumppuyhteyttä ei voi käyttää.'):
    FT('Ei Wi-Fi-yhteyttä: ulkoiset tiedot eivät päivity eikä Wi-Fi-pumppuyhteyttä voi käyttää.'));
  if(f.fallback==='no_heating_requested')return {kind:'warn',
    headline:FT('Lämmitystä ei pyydetä tässä tilassa'),
    detail:[FT('Kylmillään-tila tai kuivaustilan kosteusraja estää lämmitysehdotuksen. Tämä ei todista, että lämpöpumppu on jo pysähtynyt.'),...out].join(' ')};
  if(f.fallback==='blocked')return {kind:'bad',headline:FT('Lämmitysehdotusta ei tehdä'),
    detail:[FT('Tuoretta huonelämpötilamittausta ei ole. Lämpöpumppu toimii omalla säätimellään; Lite ei lähetä komentoja.')].concat(out).join(' ')};
  // Fallback describes the controller's *proposal*. It is not proof that
  // any pump commands are being sent. CN105 and Midea Wi-Fi require an actual
  // working local link, and an explicit write authorization.
  const pump=frSource(f,'pump');
  const gate=typeof f.control_gate==='string'?f.control_gate:'UNKNOWN';
  // Direct CN105 HTTP readback gates physical control even when the optional
  // SSE/UI pump telemetry is stale. Only the actual controller ControlBlock
  // can establish a command-capable path, not pump freshness by inference.
  const commandsPossible=f.control_armed===true&&f.wifi_ok===true&&
    ['READY','ALREADY_APPLIED'].includes(gate);
  if(!commandsPossible){
    const why=!f.control_armed?FT('Automaattiohjaus on pois käytöstä.'):
      !f.wifi_ok?FT('Wi-Fi-yhteys puuttuu.'):
      gate==='MODE_DWELL'?FT('Ohjaus odottaa lämmitystilan vähimmäiskestoa, eikä lähetä uutta komentoa.') :
      FT('Ohjausportti ei tällä hetkellä salli komentoja.');
    return {kind:'warn',headline:FT('Vain lämmitysehdotus – pumppua ei ohjata'),
      detail:[why,FT('Lämpöpumppu toimii omilla asetuksillaan; Lite ei lähetä komentoja.'),...out].join(' ')};
  }
  if(pump&&pump.state!=='ok')return {kind:'warn',
    headline:FT('Ohjausportti sallii ohjauksen'),
    detail:[FT('Pumpun lisätelemetria on vanhentunut, mutta ohjaimen suora yhteysportti sallii komennot.'),...out].join(' ')};
  if(f.fallback==='thermostat')return {kind:'warn',headline:FT('Huonetermostaatin ehdotus'),
    detail:[FT('Hintatietoa ei ole: ohjain tavoittelee ±0,5 °C termostaattialuetta. Käskyt riippuvat edelleen laiteturvaporteista.')].concat(out).join(' ')};
  return {kind:out.length?'warn':'ok',headline:out.length?FT('Tallennetut tiedot käytössä'):FT('Ohjaustiedot ovat tuoreita'),
    detail:out.length?out.join(' '):FT('Hinnat ja huonelämpötila ovat käytettävissä. Käskyt riippuvat edelleen laiteturvaporteista.')};
}
// ---- firmware update summary ----------------------------------------------------
// One honest sentence from the device's own OTA fields. The signed worker checks
// the manifest and installs an eligible release in the same pass, so "available"
// is not a state the device can hold; the candidate is only what the last check saw.
function otaSummary(s,fmt){
  const enabled=s.ota_signed_pull_enabled===true;
  const good=frNum(s.ota_signed_good_version)||0,cand=frNum(s.ota_signed_candidate)||0;
  const checked=(frNum(s.ota_last_check_utc)||0)>0;
  if(!enabled)return {kind:'bad',headline:FT('Päivityskanavaa ei ole tällä laitteella'),
    detail:FT('Tämä ohjelmisto on käännetty ilman allekirjoitettua päivityskanavaa, joten päivitystä ei voi tehdä dashboardista.')};
  if(s.ota_dashboard_pending)return {kind:'busy',headline:FT('Tarkistus on jonossa'),detail:FT('Laite tarkistaa julkaisun heti, kun se on valmis.')};
  switch(s.ota_signed_status){
  case 5:return {kind:'busy',headline:FT('Päivitys latautuu'),
    detail:FT('Ladattu ')+fmt.kib(s.ota_downloaded_bytes||0)+FT('. Allekirjoitus ja tarkistussumma varmistetaan ennen asennusta; edellinen toimiva versio säilyy.')};
  case 6:return {kind:'busy',headline:FT('Päivitys asennettu'),detail:FT('Laite käynnistyy uudelleen ja vahvistaa uuden version vasta, kun käynnistystarkistukset ovat läpi.')};
  case 2:return {kind:'warn',headline:FT('Odottaa verkkoa tai kellonaikaa'),detail:FT('Päivityksiä ei voi tarkistaa nyt. Laite toimii normaalisti ilman niitä.')};
  case 3:return {kind:'warn',headline:FT('Tilapäinen resurssi- tai tallennusvirhe'),detail:FT('Tarkistus yritetään myöhemmin uudelleen. Mitään ei ole asennettu.')};
  case 4:return {kind:'bad',headline:FT('Julkaisun tarkistus tai lataus epäonnistui'),
    detail:FT('Tarkista, että laite pääsee internetiin ja päivityspalvelin on tavoitettavissa. Hylättyä tai vioittunutta julkaisua ei asenneta.')};
  default:
    if(!checked)return {kind:'info',headline:FT('Ei vielä tarkistettu'),detail:FT('Paina "Tarkista ja asenna päivitys". Kelvollinen allekirjoitettu julkaisu asennetaan samalla kertaa.')};
    if(cand>good&&good>0)return {kind:'info',headline:FT('Havaittu julkaisu ei ole asennettavissa'),
      detail:FT('Viimeisin havaittu julkaisu ')+cand+FT(' on jo asennettu, aiemmin hylätty tai odottaa vahvistusta.')};
    return {kind:'ok',headline:FT('Ei uutta hyväksyttyä versiota'),
      detail:good>0?FT('Asennettuna on vahvistettu julkaisu ')+good+'.':FT('Vahvistettua julkaisua ei vielä ole kirjattu.')};
  }
}
function otaLastUpdateText(s,fmt){
  const t=frNum(s.ota_last_update_utc),v=frNum(s.ota_last_update_version);
  if(t===null||t<=0)return FT('Ei tiedossa (laite ei ole kirjannut päivitystä)');
  return fmt.time(t)+(v?' · '+FT('julkaisu ')+v:'');
}
</script>
<script>
'use strict';

// Finnish and English are bundled in flash. No browser translation service,
// external network calls, firmware settings or heat-pump writes are involved.
let uiLanguage=(()=>{
  const query=new URLSearchParams(location.search);
  const fromSetup=query.get('ui_lang');
  if(fromSetup==='fi'||fromSetup==='en'){
    try{
      localStorage.setItem('enevina_lite_language',fromSetup);
      query.delete('ui_lang');
      history.replaceState(null,'',location.pathname+(query.size?'?'+query:'')+location.hash);
    }catch(_){}
    return fromSetup;
  }
  try {return localStorage.getItem('enevina_lite_language')==='en'?'en':'fi';}
  catch(_){return 'fi';}
})();
const UI_EN=Object.freeze({"Daikin · paikallinen DSIoT-yhteys":"Daikin · local DSIoT connection","Anna Daikinin DSIoT-moduulin paikallinen IPv4-osoite.":"Enter the Daikin DSIoT module's local IPv4 address.","Yhdistä Daikin DSIoT":"Pair Daikin DSIoT","Yhdistä Daikin DSIoT ja odota tuoretta tilanlukua ennen jatkamista.":"Pair Daikin DSIoT and wait for a fresh status read before continuing.","Tämä Daikin DSIoT -yhteys lukee pumpun tilan.":"This Daikin DSIoT connection reads the heat pump's state.","Daikin DSIoT -yhteys toimii.":"The Daikin DSIoT connection is working.","Daikin DSIoT odottaa lukutestiä.":"Daikin DSIoT is waiting for a read test.","Daikin DSIoT -yhteys ei vastaa.":"The Daikin DSIoT connection is not responding.","Moduuli ei vastannut tuetulla DSIoT-protokollalla.":"The module did not respond with the supported DSIoT protocol.","Daikin DSIoT -moduulin tunniste muuttui.":"The Daikin DSIoT module identity changed.","Daikin DSIoT -moduulin osoite ei kelpaa tässä kotiverkossa.":"The Daikin DSIoT module address is invalid on this local network.","Daikin DSIoT -yhteyden tallennus epäonnistui.":"Could not save the Daikin DSIoT connection.","WF-RAC · mallikohtainen koekäyttö":"WF-RAC · model-specific trial","Moduuliin jo rekisteröity paikallinen tunniste":"Existing operator ID already registered in the module","Hyväksy WF-RAC-ohjauskoe":"Approve WF-RAC control trial","Poista WF-RAC-koelupa":"Revoke WF-RAC trial permission","WF-RAC-mallin ohjauskoe on hyväksytty.":"The WF-RAC model control trial is approved.","WF-RAC-mallin ohjauskoe odottaa hyväksyntää.":"The WF-RAC model control trial is waiting for approval.","WF-RAC-ohjaus tarvitsee tuoreen tilan ja mallikohtaisen hyväksynnän.":"WF-RAC control needs a fresh state read and model-specific approval.","Tarkista WF-RAC-malli, lämpötilarajat ja paikallinen tunniste.":"Check the WF-RAC model, setpoint limits and local operator ID.","WF-RAC-ohjaus on jonossa.":"WF-RAC command is queued.","WF-RAC-pumppu vahvisti komennon.":"The WF-RAC heat pump confirmed the command.","WF-RAC-pumppu ei vahvistanut komentoa.":"The WF-RAC heat pump did not confirm the command.","WF-RAC-asetus muuttui. Ohjaus arvioidaan uudelleen.":"The WF-RAC setting changed. Control will be evaluated again.","WF-RAC ei hyväksynyt komentoa tai yhteys katkesi.":"WF-RAC rejected the command or the connection was interrupted.","Komento ei sovi WF-RAC-pumpun nykyiseen tilaan.":"The command is unavailable in the current WF-RAC state.","WF-RAC-komento peruttiin yhteyden tai ohjausasetusten muuttuessa.":"The WF-RAC command was cancelled after a connection or control setting changed.","WF-RAC-automaattiohjaus":"WF-RAC automatic control","WF-RAC lukee tilan ja ohjaa hyväksyttyä lämmitysmallia.":"WF-RAC reads status and controls supported heating on the approved model.","WF-RAC · Mitsubishi Heavy Industries":"WF-RAC · Mitsubishi Heavy Industries","Yhdistä WF-RAC":"Pair WF-RAC","Anna alkuperäisen HTTP-moduulin paikallinen IPv4-osoite.":"Enter the original HTTP module's local IPv4 address.","WF-RAC-yhteys odottaa lukutestiä.":"WF-RAC is waiting for a read test.","WF-RAC-yhteys toimii.":"The WF-RAC connection is working.","WF-RAC-yhteys ei vastaa.":"The WF-RAC connection is not responding.","WF-RAC-yhteyden tallennus epäonnistui.":"Could not save the WF-RAC connection.","Yhdistä WF-RAC ja odota tuoretta tilanlukua ennen jatkamista.":"Pair WF-RAC and wait for a fresh status read before continuing.","Moduuli ei vastannut tuetulla WF-RAC HTTP -protokollalla.":"The module did not respond with the supported WF-RAC HTTP protocol.","WF-RAC-moduulin tunniste muuttui.":"The WF-RAC module identity changed.","WF-RAC-moduulin osoite ei kelpaa tässä kotiverkossa.":"The WF-RAC module address is invalid on this local network.","WF-RAC odottaa laitteen kellon synkronointia.":"WF-RAC is waiting for the controller clock to synchronize.","WF-RAC-tilavastausta ei voitu varmistaa.":"The WF-RAC state response could not be verified.","Tämä WF-RAC-yhteys lukee pumpun tilan.":"This WF-RAC connection reads the heat pump's state.","Daikin · mallikohtainen koekäyttö":"Daikin · model-specific trial","Tarkista pumpun malli, rajat ja askel laitteen ohjeesta.":"Check the model, setpoint range and increment in the heat pump manual.","Asetusarvon askel":"Setpoint increment","Lämpöpumpun malli":"Heat pump model","Hyväksy Daikinin ohjauskoe":"Approve Daikin control trial","Poista Daikinin koelupa":"Revoke Daikin trial permission","Daikin-mallin ohjauskoe on hyväksytty.":"The Daikin model control trial is approved.","Daikin-mallin ohjauskoe odottaa hyväksyntää.":"The Daikin model control trial is waiting for approval.","Daikin-automaattiohjaus":"Daikin automatic control","Daikinin automaattiohjaus edellyttää mallikohtaista hyväksyntää ja tuoretta tilanlukua.":"Daikin automatic control needs model-specific approval and a fresh state read.","Daikin-ohjaus on jonossa.":"Daikin command is queued.","Daikin-pumppu vahvisti komennon.":"The Daikin heat pump confirmed the command.","Daikin-pumppu ei vahvistanut komentoa.":"The Daikin heat pump did not confirm the command.","Daikinin asetus muuttui. Ohjaus arvioidaan uudelleen.":"The Daikin setting changed. Control will be evaluated again.","Daikin ei hyväksynyt komentoa tai yhteys katkesi.":"Daikin rejected the command or the connection was interrupted.","Daikin-ohjaus odottaa tuoretta tilanlukua ja mallikohtaista hyväksyntää.":"Daikin control requires a fresh state read and model-specific approval.","Daikinin malli, rajat tai lämpötila-askel puuttuvat.":"The Daikin model, range or setpoint increment is missing.","Komento ei sovi Daikinin nykyiseen tilaan tai mallin rajoihin.":"The command is unavailable in the current Daikin mode or falls outside the model limits.","Daikin-komento peruttiin yhteyden tai ohjausasetusten muuttuessa.":"The Daikin command was cancelled after a connection or control setting changed.","Daikin · paikallinen HTTP-yhteys":"Daikin · local HTTP connection","Anna Daikinin Wi-Fi-moduulin IP-osoite kotiverkossa.":"Enter the Daikin Wi-Fi module's local IP address.","Yhdistä Daikin":"Pair Daikin","Daikin-yhteys toimii.":"Daikin connection is working.","Daikin-yhteys odottaa lukutestiä.":"Daikin is waiting for a read test.","Daikin-yhteys ei vastaa.":"The Daikin connection is not responding.","Moduuli ei palauttanut vanhan Daikin-rajapinnan tilaa.":"The module did not return state through the legacy Daikin API.","Daikin-moduulin tunniste muuttui. Yhdistä oikea laite uudelleen.":"The Daikin module identity changed. Reconnect the intended device.","Daikin-yhteyden tallennus epäonnistui.":"Could not save the Daikin connection.","Yhdistä Daikin ja odota tuoretta tilanlukua ennen jatkamista.":"Pair Daikin and wait for a fresh state read before continuing.","Daikin lukee tilan ja ohjaa hyväksyttyä lämmitysmallia.":"Daikin reads state and controls supported heating on the approved model.","Daikin-moduulin osoite ei kelpaa tässä kotiverkossa.":"The Daikin module address is invalid on this local network.","Gree · mallikohtainen koekäyttö":"Gree · model-specific trial","Tallenna pyynti":"Save setpoint","Gree-laitteen rajat vahvistetaan valitulle mallille kokonaisina Celsius-asteina.":"Confirm the Celsius setpoint range, in whole degrees, for the selected Gree model.","Hyväksytkö mallikohtaisen ohjauskokeen tälle Gree-pumpulle?":"Approve a model-specific control trial for this Gree heat pump?","Gree-automaattiohjaus":"Gree automatic control","Green automaattiohjaus tarvitsee mallikohtaisen hyväksynnän ja tuoreen tilanluvun.":"Automatic Gree control needs model-specific approval and a fresh state read.","Gree-ohjaus on jonossa.":"A Gree command is queued.","Gree-pumppu vahvisti komennon.":"The Gree heat pump confirmed the command.","Gree-pumppu ei vahvistanut komentoa.":"The Gree heat pump did not confirm the command.","Gree-pumpun asetus muuttui. Ohjaus arvioidaan uudelleen.":"The Gree settings changed. The control proposal will be reevaluated.","Gree-komennon lähetys epäonnistui.":"Could not send the Gree command.","Gree lukee tilan ja ohjaa hyväksyttyä lämmitysmallia.":"Lite reads Gree state and controls approved heating models.","Käsiohjaus jättää pumpun kaukosäätimen käyttöön. Tuetut käsikäskyt voi lähettää myös Liten Asetuksista.":"Manual control leaves the heat pump remote available. Supported manual commands can also be sent in Lite Settings.","Valittu asetus on jo käytössä.":"The selected setting is already applied.","Gree / EWPE Smart · paikallinen yhteys":"Gree / EWPE Smart · local connection","Hae Gree-laitteet kotiverkosta tai anna pumpun IP-osoite ja laitetunniste.":"Discover Gree devices on your local network or enter the pump's IP address and device ID.","Gree-laitteet":"Gree devices","Pumpun IP-osoite":"Heat pump IP address","Gree-salausversio":"Gree encryption version","Etsi Gree":"Find Gree","Yhdistä Gree":"Pair Gree","Valitse löydetty Gree…":"Select a discovered Gree…","Gree-yhteys toimii.":"Gree connection is working.","Gree-laitteita ei löytynyt.":"No Gree devices were found.","Gree-laitteen sidonta epäonnistui. Tarkista osoite, laitetunniste ja salausversio.":"Gree pairing failed. Check the address, device ID and encryption version.","Gree-pumpulta ei saatu tuoretta tilaa.":"Could not get fresh state from the Gree heat pump.","Gree-yhteyden tiedot eivät kelpaa tässä kotiverkossa.":"The Gree connection details are not valid on this local network.","Gree-yhteyden tallennus epäonnistui.":"Could not save the Gree connection.","Gree-yhteys odottaa lukutestiä.":"Gree connection is waiting for a read test.","Yhdistä Gree ja odota tuoretta tilanlukua ennen jatkamista.":"Pair the Gree heat pump and wait for a fresh state read before continuing.","Gree-yhteydessä on käytössä laitteen tilanluku.":"State reading is available for this Gree connection.","Valitse automaattiohjaus yhteenvedossa.":"Choose automatic control in the summary.","Lämpöpumpun ohjaus":"Heat pump control","Lähetys aloitettu":"Dispatch started","Lähetys- tai kuittausvirheet":"Dispatch or acknowledgement errors","Vahvistettu pumpulta":"Confirmed from pump","Sillalta kuitattu":"Acknowledged by bridge","Käsiohjaus":"Manual control","Automaattiohjaus":"Automatic control","Automaattiohjaus on valittavissa Ylläpitolämpö- ja Asuttu-tilassa.":"Automatic control is available in Maintenance and Occupied modes.","Pumpun yhteysasetukset ovat muuttuneet. Tarkista yhteys ja valitse ohjaustapa uudelleen.":"The pump connection settings changed. Check the connection and select the control method again.","Pumpun laitteen tunniste":"Heat pump device ID","Hyväksytkö mallikohtaisen ohjauskokeen tälle Midea-pumpulle?":"Approve a model-specific control trial for this Midea heat pump?","Lite ohjaa valittua pumppua valitun huonelämpötilalähteen ja sähkön kokonaishinnan perusteella. Lämpötilapyyntö tarkistetaan takaisinluvulla. Automaattiohjaus valitaan käyttöönotossa tai tässä näkymässä.":"Lite controls the selected pump using the chosen room temperature source and total electricity price. It checks the requested setpoint by reading it back. Choose automatic control during setup or in this view.","Käsiohjaus vapauttaa pumpun omalle säätimelle ja kaukosäätimelle. Midean tuetut komennot voi lähettää myös Liten Asetuksista.":"Manual control leaves the pump to its own controller and remote. Supported Midea commands can also be sent from Lite Settings.","Käsikäskyt lähetetään suoraan pumpulle ja tarkistetaan uudella tilanluvulla. Poista automaattiohjaus käytöstä ennen käsikäskyn lähettämistä.":"Manual commands go directly to the pump and are checked with a fresh state read. Disable automatic control before sending a manual command.","Huonelämpötilan lähde":"Room temperature source","Tallennettu.":"Saved.","Pumpun oma anturi":"Heat pump sensor","Mittauskorjaus (°C)":"Sensor correction (°C)","Pumpun anturi mittaa laitteen lähellä. Voit verrata lukemaa huoneen mittariin ja lisätä enintään ±5 °C:n korjauksen.":"The heat pump sensor measures near the unit. Compare it with a room thermometer and apply a correction of up to ±5 °C.","Tallenna lämpötilalähde":"Save temperature source","Korjauksen tulee olla −5…+5 °C, enintään kaksi desimaalia.":"Enter a correction from −5 to +5 °C, with up to two decimal places.","Ohjauksen huonelämpötila":"Room temperature used for control","Ei tuoretta mittausta valitusta lähteestä.":"No fresh reading from the selected source.","Tuore mittaus pumpun omalta anturilta.":"Fresh reading from the heat pump sensor.","Huonelämpötilan lähde vaihdettu, anturi vaihdettu tai mittauskorjausta muutettu.":"Room temperature source, sensor or correction changed.","Lämmitystila":"Heating mode","Laitteen tilaa ei voitu lukea.":"Could not read device status.","Tässä tilassa ei ole kiinteää huonetavoitetta.":"This mode has no fixed room temperature target.","Tallenna Midea-yhteys ja odota tuoretta tilanlukua ennen jatkamista.":"Save the Midea connection and wait for a fresh state read before continuing.","Midea-yhteys on tallennettu, mutta lämpöpumpun valintaa ei voitu tallentaa.":"The Midea connection was saved, but the heat pump selection could not be saved.","Poista automaattiohjaus käytöstä ennen käsiohjausta.":"Disable automatic control before sending manual commands.","Midea-automaattiohjaus":"Midea automatic control","Pyydetty":"Requested","Lähetetty":"Sent","Viimeksi vahvistettu":"Last confirmed","Automaattinen MPC":"Automatic MPC","Automaattinen hintaohjaus":"Automatic price control","Automaattinen termostaattiohjaus":"Automatic thermostat control","Midea · hintaohjaus tai termostaatti":"Midea · price control or thermostat","Tuetut Midea-mallit ja lähteet":"Supported Midea models and sources","Laitteen tyyppiä ei tueta tällä Midea-ohjaimella. Katso tuetut mallit malliluettelosta.":"This appliance type is not supported by the Lite Midea controller. See the model catalogue for supported models.","Ei tuettu":"Unsupported","Valitse tuettu puhallusnopeus kuivaustilasta poistumista varten.":"Choose a supported fan speed before leaving Dry mode.","Valitse vain yksi lisätila kerrallaan: Eco, Turbo, yötila tai 8 °C ylläpitolämmitys.":"Select only one preset at a time: Eco, Turbo, Sleep or 8 °C heat preservation.","Laitteen ilmoittamat toiminnot ovat käytössä.":"The device-reported features are in use.","8 °C ylläpitolämmityksen (jäätymissuoja) tila puuttuu, eikä laite ilmoita tämän asetuksen ohjaustapaa. Perusasetukset odottavat täydentävää tilanlukua.":"The 8 °C heat-preservation state is unknown and the device has not reported how to control it. Basic settings are waiting for a more complete state read.","Odotetaan laitteen ominaisuuksia.":"Waiting for device capabilities.","Yötila":"Sleep mode","8 °C ylläpitolämmitys":"8 °C heat preservation","Mallin lisätoiminnot":"Device features","Näyttö":"Display","Pystyläpän asento":"Vertical louver position","Vaakaläpän asento":"Horizontal louver position","Tehotaso":"Power level","Ionisointi":"Ionization","Vedoton puhallus":"Breezeless airflow","Epäsuora puhallus":"Indirect airflow","Itsepuhdistus":"Self-cleaning","Merkkiääni":"Sound","Päällä":"On","Ohjaustapa ja käytettävissä olevat asetukset valitaan laitteen ilmoittamien ominaisuuksien ja luetun tilan perusteella. Lite lähettää yhden komennon ja tarkistaa tuloksen uudella tilanluvulla. Lisätoiminnot voidaan joutua lähettämään erillään perusasetuksista.":"The command format and available settings follow the device capabilities and observed state. Lite sends one command and verifies the result with a fresh state read. Additional features may need to be sent separately from basic settings.","Laitteen ilmoittamat toiminnot ja lämpötilarajat ovat käytössä.":"The device-reported features and temperature limits are in use.","Kaikkia ominaisuuksia ei ilmoitettu. Käytössä ovat perustoiminnot ja laitteelta luetut lisätoiminnot.":"The device did not report every capability. Basic controls and additional features observed in its state are available.","Ohjaus on jo jonossa tai käynnissä.":"A command is already queued or running.","Lähetä perusasetukset ja lisätoiminnot erillisinä komentoina.":"Send basic settings and additional features as separate commands.","Pyyntilämpötila ei vastaa laitteen rajoja tai askelkokoa.":"The target temperature does not match the device limits or step size.","Valitun lisätoiminnon ohjaustapaa ei ole vahvistettu laitteen tilasta.":"The command format for this feature has not been confirmed from the device state.","8 °C ylläpitolämmityksen (jäätymissuoja) tila puuttuu. Valitse sen tila ennen perusasetuksen lähettämistä.":"The 8 °C heat-preservation setting is unknown. Select its state before sending a basic setting.","Laitteen luettu tila ei vielä riitä perusasetusten turvalliseen lähettämiseen.":"The observed device state is not yet complete enough to send basic settings safely.","Valitut asetukset tarkistetaan vielä tuoreesta tilasta ennen lähetystä.":"The selected settings will be checked against fresh device state before sending.","Laite ei tue valittua asetusta tai arvoa.":"The device does not support the selected setting or value.","Valitse löydetty Midea...":"Select a discovered Midea...","V3-tunnisteet tallennetaan paikalliseen NVS-muistiin, eikä niitä palauteta selaimeen. V2 ei tarvitse tokenia tai keytä, mutta sen yhteys käyttää yhteistä kiinteää salausavainta eikä todenna pumppua samalla tavalla kuin V3. Valitse protokolla laitteen todetun version mukaan. Käytä vain luotetussa lähiverkossa. Lähiverkkohaku täyttää IP-osoitteen ja device ID:n, mutta ei tallenna yhteyttä eikä ohjaa pumppua. V3 vaatii lisäksi omat tunnukset. Päivitä IP-osoite yhteysasetuksiin, jos osoite vaihtuu.":"V3 credentials are stored locally in NVS and are not returned to the browser. V2 does not require a token or key, but uses a shared fixed encryption key and does not authenticate the pump as V3 does. Select the protocol based on the verified device version. Use only on a trusted local network. LAN discovery fills in the IP address and device ID, but neither saves the connection nor controls the pump. V3 also requires your existing credentials. Update the connection settings if the device IP address changes.","Etsi Midea lähiverkosta":"Find Midea on local network","Löydetyt Midea-laitteet":"Discovered Midea devices","MPC-ohjaus:":"MPC control:","Enevina Lite | Energianhallinta":"Enevina Lite | Energy Management","Päävalikko":"Main menu","Näkymät":"Views","Etusivu":"Home","Sähkön hinta":"Electricity price","Sääennuste":"Weather forecast","Optimointi":"Optimization","Asetukset":"Settings","Enevina Lite · paikallinen ohjain":"Enevina Lite · local controller","Laiteasetukset":"Device settings","Energia · Lämpöpumppu":"Energy · Heat pump","Aikaa ei ole synkronoitu":"Time not synchronized","Yhdistetään":"Connecting","OHJAUS POIS":"CONTROL OFF","Ota CN105-automaattiohjaus käyttöön Asetukset-näkymästä. Optimointiehdotus ei yksin lähetä käskyjä.":"Enable CN105 automatic control in Settings. An optimization suggestion alone never sends commands.","Yleisnäkymä":"Overview","Sisäilman olosuhteet":"Indoor conditions","Mittaukset, sähkön hinta ja tuleva lämmityssuunnitelma.":"Measurements, electricity prices and upcoming heating plan.","Sisälämpötila · RuuviTag":"Indoor temperature · RuuviTag","Tila ei tiedossa":"Status unknown","Odotetaan mittausta":"Waiting for measurement","Sähkön hinta nyt":"Electricity price now","Spot-hinta · ilman arvonlisäveroa":"Spot price · excluding VAT","Arvio kokonaishinnasta":"Estimated total price","Ulkolämpötila / ennuste":"Outdoor temperature / forecast","Mittauskulutus":"Measured power","Ilmankosteus":"Humidity","RuuviTag · suhteellinen kosteus":"RuuviTag · relative humidity","Kastepiste":"Dew point","Anturin tila":"Sensor status","Sähkön tulevat hinnat":"Upcoming electricity prices","15 minuutin jaksot":"15-minute periods","Hintadataa odotetaan":"Waiting for price data","Halvinta jaksoa ei tiedossa":"Cheapest period unknown","Kaikki hinnat":"All prices","Näytä 48 tunnin ennuste":"View 48-hour forecast","Odotetaan lämpötilaennustetta.":"Waiting for temperature forecast.","Oma energiankulutusluokka":"Personal energy consumption grade","Epävirallinen":"Unofficial","Vuoden kulutustieto puuttuu":"Annual consumption data missing","Lämmitetty pinta-ala m²":"Heated floor area m²","Vuotuinen sähkönkulutus kWh":"Annual electricity use kWh","Syötä vuosikulutus":"Enter annual consumption","Laske luokka":"Calculate grade","Oma havainnollistava A–G-asteikko: alle 60 / 100 / 150 / 200 / 250 / 350 kWh/m²/v. G yli 350. Ei virallinen energiatodistus tai E-luku; ei huomioi lämmitysmuotoja tai sääkorjausta. Tiedot säilyvät vain tässä selaimessa.":"Illustrative A–G scale: below 60 / 100 / 150 / 200 / 250 / 350 kWh/m²/year; G above 350. Not an official energy certificate or Finnish E-value. Heating type and weather adjustments are not included. Data is stored only in this browser.","Optimoinnin seuraava toimenpide":"Next optimization action","Ei suunnitelmaa":"No plan","Hyväksyttyä optimointisuunnitelmaa ei vielä ole.":"No validated optimization plan is available yet.","Optimointi näkyy vasta, kun käytettävissä on kelvollinen, turvallinen ja kalibroitu laskentapohja.":"Optimization is shown once valid, safe and calibrated model data is available.","Pörssisähkö":"Spot electricity","Sähkön hinnat":"Electricity prices","Suomen 15 minuutin hinnat. Kaavio näyttää vain ladatun tulevan datan.":"Finnish 15-minute prices. The chart displays only available future data.","Nykyinen spot":"Current spot price","snt/kWh, alv 0 %":"c/kWh, VAT 0%","Edullisin tuleva jakso":"Cheapest upcoming period","Ei tietoa":"No data","Saatavilla oleva ennuste":"Available forecast","Tietolähdettä ei ole päivitetty":"Source has not been updated","Hintakehitys":"Price trend","Hintoja ei ole saatavilla":"No prices available","Spot-hinta on ilman veroja. Kokonaishinnan arviossa sovelletaan laitteelle määriteltyjä sopimuskohtaisia vero- ja siirtomaksuja. Hintoja ei arvata puuttuville varttitunneille.":"Spot price excludes taxes. Estimated total price includes the configured electricity tax, grid charges and VAT. Missing 15-minute prices are never estimated.","Seuraavat hintajaksot":"Upcoming price periods","Alkaa":"Starts","Spot snt/kWh":"Spot c/kWh","Kokonaishinta snt/kWh":"Total price c/kWh","Odota hintatietoja.":"Waiting for prices.","Seuraava ennustelämpötila":"Next forecast temperature","Alin seuraavan 24 tunnin aikana":"Lowest in the next 24 hours","Ylin seuraavan 24 tunnin aikana":"Highest in the next 24 hours","Ennustettu ulkolämpötila":"Forecast outdoor temperature","Ulkolämpötila · seuraavat 48 tuntia":"Outdoor temperature · next 48 hours","Ennustetietoja odotetaan.":"Waiting for forecast data.","Ennusteen päivitysaikaa ei tiedetä.":"Forecast update time unknown.","Näytetään vain Ilmatieteen laitokselta vastaanotettuja kelvollisia tuntipisteitä. Ennuste voi poiketa toteutuneesta säästä; sade- ja tuulitiedot eivät sisälly tähän syötteeseen.":"Only validated hourly readings received from MET Norway are shown. Actual weather may differ; precipitation and wind data are not part of this feed.","Tuntiennuste":"Hourly forecast","Ajankohta":"Time","Lämpötila °C":"Temperature °C","Ladataan sääennustetta…":"Loading weather forecast…","Ennakoiva lämmitys":"Predictive heating","Optimointisuunnitelma":"Optimization plan","Hintaohjaus tuottaa ehdotuksen; CN105-komennot edellyttävät erillistä käyttöönottoa.":"Price-based control generates a proposal; CN105 commands require separate authorization.","Suunnitelma":"Plan","Ei kelvollista laskelmaa":"No valid calculation","Ennustettu kustannus":"Forecast cost","Mallin kokonaisarvio, jos kelvollinen":"Model total estimate, if valid","Ennustettu alin lämpötila":"Predicted lowest temperature","Laskettu, ei anturimittaus":"Calculated, not measured by a sensor","Hyväksyttyä suunnitelmaa ei ole. Lämpöpumppu ei saa tältä käyttöliittymältä komentoja.":"No validated plan is available. The heat pump receives no commands from this interface.","Ehdotus":"Suggestion","Ennuste °C":"Forecast °C","Sähkö kWh":"Electricity kWh","Arvio €":"Estimate €","Suunnitelma ei saatavilla.":"Plan unavailable.","Olosuhteet ja laite":"Conditions and device","Paikallinen huonetavoite ja erikseen käyttöönotettava CN105-lämmitysohjaus.":"Local room temperature target and independently enabled CN105 heating control.","Lämmityksen toimintatila":"Heating operating mode","Ylläpitolämpö ja Asuttu käyttävät omia, toisistaan riippumattomia huonetavoitteitaan. Hintaohjaus voi säätää Mitsubishin pyyntilämpötilaa.":"Maintenance and Occupied modes have separate room temperature targets. Price-based control may adjust the Mitsubishi setpoint.","Laskennallinen tila":"Operating mode","Kylmillään":"Unheated","Kuivanapito":"Drying","Ylläpitolämpö":"Maintenance","Asuttu":"Occupied","Ylläpitotavoite °C":"Maintenance target °C","Asuttu °C":"Occupied °C","Kuivanapito Δ°C":"Drying Δ°C","Tallenna asetukset":"Save settings","Valitse Ylläpitolämpö tai Asuttu sekä kyseisen tilan huonetavoite. Hintaehdotus laskee Mitsubishille 16–26 °C pyyntilämpötilan 0,5 °C askelin; komennot sallitaan vain erillisellä käyttöönotolla.":"Choose Maintenance or Occupied and its room target. Price-based control calculates Mitsubishi setpoints from 16 to 26 °C in 0.5 °C steps. Commands require separate authorization.","Lämpöpumpun automaattiohjaus":"Automatic heat pump control","Laskenta käyttää RuuviTagia ja sähkön kokonaishintaa. Lämmitystä ohjataan Ylläpitolämpö- ja Asuttu-tiloissa. Komento lähtee CN105:lle, ja tavoitelämpötila tarkistetaan takaisinlukemalla. Käyttöönotto on aina erillinen valinta eikä firmwarepäivitys käynnistä sitä.":"Control uses RuuviTag measurements and total electricity price. Heating is controlled in Maintenance and Occupied modes. CN105 commands are verified by readback. Authorization is always separate; firmware updates do not enable control.","Ohjaus ei käytössä":"Control disabled","Ei komentoja":"No commands","Ota automaattiohjaus käyttöön":"Enable automatic control","Poista automaattiohjaus käytöstä":"Disable automatic control","Sähkösopimus ja siirto":"Electricity contract and distribution","Kokonaishinta = spot + marginaali + siirto + sähkövero, arvonlisävero lisätään kerran. Kuukausimaksut eivät vaikuta 15 minuutin ohjaukseen eikä niitä lasketa mukaan.":"Total price = spot + margin + grid charges + electricity tax, with VAT added once. Monthly fees are excluded from 15-minute control calculations.","Marginaali snt/kWh":"Margin c/kWh","Siirto päivä snt/kWh":"Daytime grid charge c/kWh","Siirto yö / muu aika snt/kWh":"Night/other grid charge c/kWh","Sähkövero snt/kWh":"Electricity tax c/kWh","ALV %":"VAT %","Siirtotuote":"Grid tariff","Yleissiirto":"Flat-rate distribution","Aikasiirto (päivä/yö)":"Time-of-use (day/night)","Kausisiirto (talviarkipäivä)":"Seasonal (winter weekdays)","Päivä alkaa klo":"Daytime starts at","Yö alkaa klo":"Nighttime starts at","Hinnat sisältävät ALV:n":"Prices include VAT","Kyllä (marginaali, siirto, vero)":"Yes (margin, grid, tax)","Ei, alv lisätään":"No, VAT is added","Esilämmityksen hintaetu %":"Preheating price advantage %","Spot-hinta on aina ilman arvonlisäveroa. Esilämmitys tehdään vain, kun hinta on vähintään annetun prosentin verran halvempi kuin seuraavien kahden tunnin mediaani.":"Spot price always excludes VAT. Preheating is considered only when the price is at least the specified percentage below the median of the next two hours.","Lämpöpumppu ja yhteydet":"Heat pump and connections","Lämpöpumppu ja yhteys":"Heat pump and connection","CN105-sillan /events-osoite":"CN105 bridge /events URL","Shelly-mittarin osoite":"Shelly meter URL","Valmistaja / malli":"Manufacturer / model","Yhteys":"Connection","Tila":"Status","Mittaukset":"Measurements","Komennot":"Commands","Ladataan…":"Loading…","Vain tällä laitteella todennetut sovittimet voi valita. Muut on lueteltu tutkimustietona, ei tukilupauksena. Sääpaikan muutos tulee voimaan seuraavassa virran kytkennässä. Laitteen valinta ei itsessään ota ohjausta käyttöön.":"Only adapters verified on this device can be selected. Other entries are research information, not guaranteed support. Weather location changes take effect on the next power cycle. Selecting a device does not enable control.","Tallenna sopimus ja yhteydet":"Save contract and connections","Vaihda Wi-Fi-verkko":"Change Wi-Fi network","Laitteen tila":"Device status","Ohjelmistoversio":"Firmware version","Wi-Fi RSSI":"Wi-Fi RSSI","CN105-telemetria":"CN105 telemetry","Shelly-mittaus":"Shelly measurement","Sisäinen vapaa RAM":"Free internal RAM","PSRAM vapaana":"Free PSRAM","OTA-lataus käytössä":"OTA downloads enabled","Avaa laiteasetukset":"Open device settings","Mitsubishi CN105 · vastaanotettu data":"Mitsubishi CN105 · received data","Viimeisin SSE-tilaviesti":"Latest SSE status event","Pumpun tila (SSE)":"Pump mode (SSE)","Pumpun sisälämpötila (SSE)":"Pump indoor temperature (SSE)","Asetuslämpötila (SSE)":"Target temperature (SSE)","Kompressori (SSE)":"Compressor (SSE)","Kompressoriviestin ikä":"Age of compressor event","ESPHome /events -rajapinnasta vastaanotettu viimeisin tilatieto. Viestin vastaanottoaika ei todista uutta CN105-UART-lukua, eikä näitä arvoja käytetä optimoinnin turvallisuushyväksyntään.":"Latest status data received via ESPHome /events. Event reception does not prove a fresh CN105 UART poll; these values are not treated as verified optimization-safety data.","RuuviTag · anturit ja kosteusvaroitukset":"RuuviTag · sensors and humidity alerts","Anturin MAC-tunniste on sidottava tarkoituksella. Kirjoita tunniste tai":"Sensor MAC must be explicitly bound. Enter an address or","irrottaaksesi sidonnan. Asetukset ovat vain paikallisia.":"to unbind it. Settings are local only.","Sisäanturin sidonta":"Indoor sensor binding","Havaittu tunniste":"Detected identifier","Anturin rooli":"Sensor role","Sisäilma":"Indoor air","Ulkoilma":"Outdoor air","XX:XX:XX:XX:XX:XX tai none":"XX:XX:XX:XX:XX:XX or none","Tallenna anturisidonta":"Save sensor binding","RH-varoitus %":"RH warning %","Korkea RH-varoitus %":"High RH warning %","Paristovaroitus V":"Battery warning V","Tallenna varoitusrajat":"Save alert thresholds","Ilman RH-arvot eivät yksin osoita rakenteiden kosteusturvallisuutta.":"Air humidity alone does not establish whether building structures are safe from moisture.","Sisäilman historia · RuuviTag":"Indoor climate history · RuuviTag","Historiatietoja odotetaan":"Waiting for history data","Viimeisimmät mittaukset, ei rakenteen kosteusmittaus. Offline-ajalta tallennettuja aikaleimoja ei esitetä UTC-aikana.":"Recent measurements, not structural moisture measurements. Offline timestamps are not shown as UTC.","Enevina Lite · Paikallinen toiminta ·":"Enevina Lite · Local operation ·","Ei aikaa":"No time available"," kWh/m² vuodessa":" kWh/m² per year","Syötä todellinen 12 kuukauden kulutus":"Enter actual consumption for the last 12 months","Syötä yli nollan oleva lämmitetty pinta-ala ja täyden vuoden kulutus.":"Enter a heated floor area above zero and full-year consumption.","Vertailuluokka tallennettu tähän selaimeen. Ei virallinen energiatodistus tai E-luku.":"Comparison grade saved in this browser. Not an official energy certificate or E-value.","Vertailuluokka laskettu; selain ei sallinut paikallista tallennusta.":"Comparison grade calculated; the browser did not permit local storage.","Tulevien 15 minuutin sähkönhintojen pylväskaavio":"Bar chart of upcoming 15-minute electricity prices","Vaihteluväli: ":"Price range: ","Mittaukset käytössä":"Measurements active","Yhteys muodostettu":"Connected","Ei tuoretta mittausta":"No recent measurement","RuuviTag ei ole tuore tai anturia ei ole sidottu":"RuuviTag is stale or no sensor is bound","Tuore":"Fresh","Puuttuu / vanhentunut":"Missing / stale","Viimeisin haku: ":"Last fetched: ","CN105-OHJAUS SALLITTU":"CN105 CONTROL ENABLED","Automaattiohjaus on kytketty. Käskyjä lähetetään Ylläpitolämpö- tai Asuttu-tilassa, kun mittaus- ja yhteysportit ovat kunnossa.":"Automatic control is enabled. Commands are sent in Maintenance or Occupied mode when sensor and connection checks pass.","Automaattiohjaus ei ole päällä. Laskenta näyttää ehdotuksia ilman fyysisiä käskyjä.":"Automatic control is off. Calculations show suggestions without physical commands.","Automaattiohjaus valittu · ":"Automatic control enabled · ","Automaattiohjaus pois":"Automatic control off","Komentoja ":"Commands "," · sillalta kuitattu ":" · acknowledged by bridge "," · virheitä ":" · errors ","Ei Wi-Fiä":"No Wi-Fi","UART-lähde varmennettu":"UART source verified","SSE-lukemia, UART-todiste puuttuu":"SSE readings, no UART proof","Ei CN105-lukemia":"No CN105 readings","UART-mittaus varmennettu":"UART reading verified","SSE vastaanotettu ":"SSE received ","(ikä ei tiedossa)":"(age unknown)"," s sitten":" s ago"," · ei UART-todistetta":" · no UART proof","Ei tilatietoja":"No status data"," (SSE)":" (SSE)","Ei vastaanottoa":"Not received"," s sitten (vain SSE)":" s ago (SSE only)","Käytössä":"Available","Ei lukemaa":"No measurement","Kyllä · allekirjoitettu":"Yes · signed","Ei":"No","Ei sidottu":"Not bound","Ei havaintoa":"No data","RH koholla":"High RH","RH erittäin korkea":"Very high RH","RuuviTag ei vastaa":"RuuviTag not responding","RuuviTag-paristo heikko":"Low RuuviTag battery","Pakkaslämpötila":"Freezing temperature","Varoitus":"Warning","Kuittaa":"Acknowledge","Kuittaus epäonnistui: ":"Acknowledgment failed: "," jaksoa":" periods","Edullisin seuraava jakso ":"Cheapest upcoming period ","Ei kelvollista hintadataa":"No valid price data","Hintatietoja ei saatavilla":"Price data unavailable","Ei voimassa olevia hintoja.":"No currently valid prices.","Pidä nykyinen tila":"Maintain current state","Hintaohjauksen ehdotus":"Price-based control proposal","Huonetermostaatin ehdotus":"Room thermostat proposal","MPC-laskelma":"MPC calculation","Ei hyväksyttyä suunnitelmaa":"No validated plan","Hintaohjaus":"Price-based control","Termostaattiohjaus":"Thermostat control","Ei käytössä":"Not active","Laskettu ":"Calculated ","Seuraava jakso ":"Next period ","Lähtötason ehdotus: ":"Fallback proposal: ","Hyväksytty optimointisuunnitelma puuttuu.":"No validated optimization plan is available.","Simuloitu MPC-ehdotus. Automaattiohjaus toimii erillisellä hintaohjauspolulla.":"Simulated MPC proposal. Automatic control operates through a separate price-based control path.","Ruuvi + 15 min kokonaishinnat; ":"Ruuvi + 15-minute total prices; ","Hintadata puuttuu: Ruuvi-huonetermostaatti; ":"Price data missing: Ruuvi room thermostat; ","huonetavoite ":"room target ","; syy: ":"; reason: ",". CN105-ohjaus edellyttää käyttäjän valintaa.":". CN105 control requires explicit user authorization.","puutteelliset lähtötiedot":"incomplete inputs","Ehdotus estetty: ":"Proposal blocked: ","Ehdotukset ovat laskennallisia. Fyysiset ohjauskäskyt ovat estettyjä.":"Proposals are calculated. Physical commands are blocked.","Lähtötason hintaohjaus: ":"Fallback price-based control: ","Hintojen puuttuessa termostaatti: ":"Thermostat fallback without prices: "," (ehdotus; automaattiohjauksen käynnistys valitaan Asetuksissa).":" (proposal; automatic control must be enabled in Settings).","Optimointiehdotus ei ole saatavilla. Puuttuva lämpötila tai puutteellinen hintajakso estää laskennan.":"No optimization proposal available. A missing temperature or incomplete price period prevents calculation.","Nykyinen ehdotus: ":"Current proposal: ",". Mallinnettu tuntien lämpötila- ja kustannusennuste puuttuu.":". Modeled hourly temperature and cost forecast is missing.","Lämpötilahistoriaa ei vielä ole riittävästi":"Not enough temperature history yet","Sisälämpötilan historia":"Indoor temperature history","Näkyvät lämpötilat: ":"Visible temperatures: "," tuntipistettä":" hourly points","Yhteys poikki":"Disconnected","Ei yhteyttä":"No connection","Mittaukset eivät päivity":"Measurements are not updating","Ei tuoretta tilaa":"No recent status","Laite ei vastaa tai vahvistustunniste puuttuu.":"Device unavailable or authorization token missing.","Tallennetaan ohjauksen käyttöönottoa…":"Saving control authorization…","Asetuksen tallennus epäonnistui":"Could not save setting","Automaattiohjaus sallittu. Ensimmäinen ohjaus odottaa tuoreita tietoja ja Ylläpitolämpö- tai Asuttu-tilaa.":"Automatic control authorized. First command awaits fresh measurements and Maintenance or Occupied mode.","Automaattiohjaus poistettu käytöstä.":"Automatic control disabled.","Ohjauksen asetusta ei tallennettu":"Control setting was not saved","Laite ei vastaa tai valtuutus puuttuu":"Device unavailable or authorization missing","Tallennus epäonnistui":"Save failed","Anna kuusi tavuinen MAC-tunniste tai none":"Enter a six-byte MAC address or none","Anturisidonta tallennettu.":"Sensor binding saved.","Tarkista varoitusarvot":"Check warning thresholds","Varoitusrajat tallennettu.":"Alert thresholds saved.","Aiemmat varoitusrajat tallennettu. Uudet muutokset odottavat tallennusta.":"Earlier alert thresholds saved. New changes still await saving.","Tiedot puuttuvat.":"Data missing.","Tarkista lämpötilatavoitteet.":"Check the temperature targets.","Tallennetaan paikallisia asetuksia…":"Saving local settings…","Asetukset tallennettu. Lämpöpumpulle ei lähetetty käskyä.":"Settings saved. No command was sent to the heat pump.","Aiemmat asetukset tallennettu. Uusia muutoksia odottaa tallennusta.":"Earlier settings saved. New changes are pending.","Tallennus ei onnistunut.":"Save failed.","Tarkista numero":"Check number","Arvo on sallitun alueen ulkopuolella":"Value is out of range","Osoite ei kelpaa (vain paikallinen http-osoite)":"Invalid URL (local HTTP URL only)","Liian pitkä arvo":"Value too long","Virheellinen valinta":"Invalid selection","Laitetta ei vielä tueta":"Device is not yet supported","Asetukset eivät ole kelvolliset":"Settings are invalid","Tallennus laitteen muistiin epäonnistui":"Saving to device memory failed","Päivitä sivu ja yritä uudelleen":"Refresh the page and try again","Kenttäpilotti":"Field pilot","Vain PC-testattu":"PC-tested only","Vain selvitetty":"Documented only","Ei valittu":"Not selected","Asetuksia ei voitu lukea laitteelta.":"Could not read settings from device.","Tallennettu. Lämpöpumpun yhteys vaihtui, joten automaattiohjaus poistettiin käytöstä: ota se uudelleen käyttöön uudelle laitteelle.":"Saved. Heat pump connection changed, so automatic control was disabled. Re-enable it for the new device.","Tallennettu. Uudet hinnat ja osoitteet ovat heti käytössä.":"Saved. New prices and addresses take effect immediately.","Tarvittava laite ei vastannut.":"Required device did not respond.","MET Norwayn ennuste haettu: ":"MET Norway forecast fetched: ","Kelvollista MET Norwayn ennustetta ei ole saatavilla.":"No valid MET Norway forecast available.","Kelvollista lämpötilaennustetta ei ole saatavilla":"No valid temperature forecast available","Seuraava ulkolämpötila ":"Next outdoor temperature "," klo ":" at "," ennustetuntia saatavilla.":" forecast hours available.","Ennustetta ei ole saatavilla. Lämmityksen ohjaus ei käytä keksittyjä sääarvoja.":"Forecast unavailable. Heating control does not use invented weather readings.","Lämpötilan käyrään tarvitaan vähintään kaksi tuntipistettä.":"A temperature curve requires at least two hourly points.","MET Norwayn ulkolämpötilaennusteen kuvaaja":"Chart of MET Norway outdoor temperature forecast","Ennustetunteja ei ole saatavilla.":"No hourly forecast available.","Käyttöliittymän kieli":"Interface language","Sääennuste · MET Norway":"Weather forecast · MET Norway","Norjan ilmatieteen laitos · MET Norway":"Norwegian Meteorological Institute · MET Norway","Paikallinen ulkolämpötilaennuste MET Norwayn maailmanlaajuisesta sääpalvelusta. Puuttuvia arvoja ei arvata.":"Local outdoor temperature forecast from MET Norway's worldwide weather service. Missing values are not estimated.","Odotetaan MET Norwayn ennustetta":"Waiting for MET Norway forecast","Sääpaikka (leveysaste,pituusaste)":"Weather location (latitude,longitude)","Sääpaikan muutos tulee voimaan heti tallennuksen jälkeen.":"Weather location changes take effect immediately after saving.","Näytetään vain MET Norwaylta vastaanotettuja kelvollisia tuntipisteitä.":"Only validated hourly values from MET Norway are shown.","MET Norwayn ennustetta ei ole saatavilla.":"No MET Norway forecast available.","Lähde:":"Source:",".\n      Ennustetiedot on muunnettu Enevina Liten lämpötilakäyräksi.\n      Näytetään vain MET Norwaylta vastaanotettuja kelvollisia tuntipisteitä. Ennuste voi poiketa toteutuneesta säästä; sade- ja tuulitiedot eivät sisälly tähän syötteeseen.":".\n      Forecast data has been converted into the Enevina Lite temperature chart.\n      Only validated hourly points from MET Norway are shown. Actual weather may vary; wind and precipitation are not included in this view.","Vain tällä laitteella todennetut sovittimet voi valita. Muut on lueteltu tutkimustietona, ei tukilupauksena. Sääpaikan muutos tulee voimaan heti tallennuksen jälkeen. Laitteen valinta ei itsessään ota ohjausta käyttöön.":"Only adapters verified on this device can be selected. Other entries are research information, not guaranteed support. The weather location changes immediately after saving. Selecting a device does not enable control.","Mitsubishi on CN105-kenttäpilotti ja Midea V2/V3 voidaan valita yhteyden tilanlukuun ja manuaaliseen ohjaustestiin. Muut on lueteltu tutkimustietona, ei tukilupauksena. Sääpaikan muutos tulee voimaan heti tallennuksen jälkeen. Laitteen valinta ei itsessään ota ohjausta käyttöön.":"Mitsubishi CN105 is a field pilot; Midea V3 is selectable for read-only telemetry. Other entries are research only, not guaranteed compatibility. Weather location changes immediately after saving. Device selection does not enable control.","Midea · paikallinen V3 Wi-Fi -yhteys":"Midea · local V3 Wi-Fi connection","V3-luku tapahtuu suoraan lämpöpumpun alkuperäisestä Wi-Fi-moduulista ilman Home Assistantia tai jatkuvaa pilviyhteyttä. Midean manuaalinen lämpötilan ja toimintatilan ohjaus on nyt kokeellinen; automaattisen optimoinnin komentoja ei vielä lähetetä. Yhdistä vain omassa luotetussa lähiverkossa; kehitysvaiheen HTTP-asetussivu ei salaa tunnisteiden siirtoa.":"V3 status is read directly from the heat pump's original Wi-Fi module without Home Assistant or a continuous cloud connection. This is a read-only test: no automatic temperature commands are sent. Use only a trusted local network; this development HTTP setup page does not encrypt the credentials in transit.","Midean paikallinen IP":"Midea local IP address","Midea V3 device ID":"Midea V3 device ID","V3 token (128 heksamerkkiä)":"V3 token (128 hex characters)","V3 key (64 heksamerkkiä)":"V3 key (64 hex characters)","Tallenna Midea-yhteys":"Save Midea connection","Ei mittausta":"No measurement","Yhteyden tila":"Connection status","Toimintatila":"Operating mode","Pyyntilämpötila":"Target temperature","Pumpun sisälämpötila":"Heat pump indoor temperature","V3 tunnisteet tallennetaan laitteen paikalliseen NVS-muistiin. Niitä ei palauteta selaimeen. Päivitä yhteysasetukset, jos laitteen IP-osoite vaihtuu.":"V3 credentials are stored in the device's local NVS memory and are not returned to the browser. Automatic DHCP IP rediscovery and heat pump control are not included in this read-only version.","Lukuyhteys (testivaihe)":"Read-only connection (experimental)","Midea V3 on tässä versiossa vain tilanluku. Ohjaus ei ole käytössä.":"Midea V3 is read-only in this version. Control is not available.","Midea ei valittu":"Midea not selected","Ei asetuksia":"Not configured","Paikallinen tilanluku toimii":"Local status polling works","V3-yhteys onnistui":"V3 connection succeeded","Midea-yhteyttä ei ole asetettu":"Midea connection not configured","Lämmitys":"Heating","Tila ":"Mode ","Pois päältä":"Off","Laite ei vastaa.":"Device not responding.","Tarkista device ID, V3 token (128) ja key (64 heksamerkkiä).":"Check device ID, V3 token (128) and key (64 hex characters).","Midea-yhteys tallennettu. Ensimmäinen luku tapahtuu verkossa.":"Midea connection saved. The first status poll will take place over the network.","Tallennus ei onnistunut (":"Save failed (","Yhteyttä ei ole":"Not connected","Ulkolämpötila":"Outdoor temperature","Testaa yhteys nyt":"Test connection now","Ei vielä testattu.":"Not tested yet.","Valitse Midea lämpöpumpun yhteydeksi.":"Select Midea as the heat pump connection.","Midean asetukset puuttuvat.":"Midea connection settings are missing.","V3-tilanluku onnistui.":"V3 status polling succeeded.","Tarkista pumpun IP-osoite.":"Check the heat pump IP address.","Midean tunnistetiedot ovat virheelliset.":"Midea device credentials are invalid.","Ei TCP-yhteyttä Mideaan (portti 6444).":"Cannot connect to Midea on TCP port 6444.","V3-tunnistautuminen epäonnistui: tarkista token ja key.":"V3 authentication failed: check token and key.","Midean V3-protokollavastaus ei kelpaa.":"Midea V3 protocol response is invalid.","Pumpun tilavastausta ei voitu tulkita.":"Cannot parse the heat pump state response.","Laitteen kellonaika ei ole vielä synkronoitunut.":"Device clock has not yet synchronized.","Tuntematon Midea-yhteysvirhe.":"Unknown Midea connection error.","Yhteystesti käynnissä. ":"Connection test running. "," Viimeisin yritys: ":" Last attempt: ","Yhteystesti käynnistetty. Pumppua ei ohjata.":"Connection test started. Heat pump control remains disabled.","Yhteystestiä ei voitu käynnistää.":"Could not start the connection test.","Firmwarepäivitys":"Firmware update","Päivitä Lite suoraan dashboardista. Laite tarkistaa varmennetulta HTTPS-päivityspalvelimelta uuden allekirjoitetun version ja asentaa sen palautettavalle OTA-osiolle. Pumpun asetuksia ei muuteta. Tavallisen .bin-tiedoston lataus selaimesta ei ole käytössä.":"Update Lite directly from the dashboard. The device checks a verified HTTPS update server for a signed release and installs it to the rollback-capable OTA partition. Heat pump settings are unchanged. Uploading an ordinary .bin file from the browser is not supported.","Nykyinen firmware":"Current firmware","Hyväksytty OTA-versio":"Confirmed OTA version","Viimeksi tarkistettu julkaisu":"Latest checked release","Viimeisin tarkistus":"Last check","Päivityksen tila":"Update status","Ladattu":"Downloaded","Tarkista ja asenna päivitys":"Check and install update","Päivitä tilatiedot":"Refresh status","Ladataan OTA-tilaa…":"Loading OTA status…","Ei uutta hyväksyttyä versiota":"No newer eligible release","Päivityskanava pois käytöstä":"Update channel disabled","Odottaa Wi-Fi-yhteyttä tai kellonaikaa":"Waiting for Wi-Fi or correct time","Tilapäinen resurssi- tai tallennusvirhe":"Temporary resource or storage error","Julkaisun tarkistus tai lataus epäonnistui":"Release verification or download failed","Allekirjoitettu päivitys latautuu":"Downloading signed update","Päivitys valmis, laite käynnistyy uudelleen":"Update ready, device restarting","Ei vielä vahvistettua julkaisua":"No confirmed release yet","Ei vielä tarkistettu":"Not checked yet","Tarkistus jonossa":"Check queued","Tuntematon tila":"Unknown status","Päivitys käyttää vain allekirjoitettua julkaisua ja säilyttää edellisen toimivan OTA-version.":"Only signed releases are installed, preserving the previous working OTA image.","Allekirjoitettua OTA-päivityskanavaa ei ole tällä laitteella. Päivitystä ei voi käynnistää ennen sen käyttöönottoa.":"A signed OTA update channel is not configured on this device. Updates cannot be started until it is provisioned.","Tarkistetaanko allekirjoitettu firmware ja asennetaanko uusi versio, jos sellainen löytyy? Laite voi käynnistyä uudelleen.":"Check signed firmware and install a new release if available? The device may restart.","Päivitystarkistus lähetetään laitteelle…":"Requesting firmware update check…","Tarkistus käynnistetty. Uusi allekirjoitettu julkaisu asennetaan, jos se on saatavilla.":"Update check started. A newer signed release will be installed if available.","Päivitystarkistuksen käynnistys epäonnistui: ":"Could not start firmware update check: ","Midea · paikallinen V2 / V3 Wi-Fi -yhteys":"Midea · local V2 / V3 Wi-Fi connection","V2- tai V3-tilanluku tapahtuu suoraan lämpöpumpun Wi-Fi-moduulista ilman Home Assistantia tai jatkuvaa pilviyhteyttä.":"V2 or V3 status is read directly from the heat pump Wi-Fi module without Home Assistant or a persistent cloud connection.","Midea-protokolla":"Midea protocol","V2 (ei tokenia)":"V2 (no token)","Midean automaattiohjaus edellyttää hyväksyttyä mallikohtaista ohjauskoetta ja tuoretta tilanlukua.":"Midea automatic control needs an approved model-specific pilot and a fresh state read.","V3-tunnisteet tallennetaan paikalliseen NVS-muistiin, eikä niitä palauteta selaimeen. V2 ei tarvitse tokenia tai keytä, mutta sen yhteys käyttää yhteistä kiinteää salausavainta eikä todenna pumppua samalla tavalla kuin V3. Valitse protokolla laitteen todetun version mukaan. Käytä vain luotetussa lähiverkossa. Päivitä yhteysasetukset, jos laitteen IP-osoite vaihtuu.":"V3 credentials are stored in local NVS and are not returned to the browser. V2 does not need a token or key, but uses a shared static encryption key and does not authenticate the device like V3. Select the device’s verified protocol. Use only on a trusted LAN. Automatic DHCP IP rediscovery and heat pump control are not included.","Midean tilanluku onnistui.":"Midea state query succeeded.","V2-yhteys onnistui":"V2 connection successful","Tarkista Midean device ID. V2 ei tarvitse tokenia eikä keytä.":"Check the Midea device ID. V2 needs no token or key.","V2- tai V3-tilanluku tapahtuu suoraan lämpöpumpun Wi-Fi-moduulista ilman Home Assistantia tai jatkuvaa pilviyhteyttä. Midean käsiohjaus ja automaattinen hinta- ja termostaattiohjaus ovat kokeellisia. Yhdistä vain omassa luotetussa lähiverkossa; kehitysvaiheen HTTP-asetussivu ei salaa tunnisteiden siirtoa.":"V2/V3 status is read directly from the heat pump Wi-Fi module without Home Assistant or persistent cloud connection. Manual Midea setpoint and mode control is experimental; automatic optimizer commands are not sent yet. Connect only on your own trusted local network; this development HTTP setup does not encrypt the credentials in transit.","Midean paikallinen ohjaus · V2 / V3":"Midea local control · V2 / V3","Yksittäiset muutokset lähetetään suoraan pumpulle alkuperäistä Wi-Fiä käyttäen. Tunnetut muut asetukset säilytetään viimeksi luetusta tilasta. Käsky on manuaalinen; pörssisähköoptimoinnin automaattista Midea-käskynlähetystä ei ole vielä kytketty.":"Individual changes are sent directly to the pump using its original Wi-Fi. Other known settings are preserved from the latest state. Commands are manual; Midea automation by spot-price optimizer is not yet connected.","Virta":"Power","Säilytä nykyinen":"Keep current","Päälle":"On","Pois":"Off","Jäähdytys":"Cool","Kuivaus":"Dry","Puhallus":"Fan","Pyynti °C":"Setpoint °C","Säilytä":"Keep current","Hiljainen":"Silent","Matala":"Low","Keskitaso":"Medium","Korkea":"High","Täysi":"Full","Pysty":"Vertical","Vaaka":"Horizontal","Molemmat":"Both","Lähetä ohjaus":"Send control","Odota ensin onnistunutta tilanlukua.":"Wait for a successful state reading first.","Midea 0x40 -komento on koko asetustilan viesti. Tämä toteutus käyttää viimeisintä laitteelta luettua tilaa, lähettää yhden komennon ja tarkistaa tuloksen uudella tilanluvulla. Laitteen omat ajastukset ja mallikohtaiset lisäominaisuudet saattavat käyttäytyä eri tavoin, eikä vielä ole vahvistettua Lite-kenttätestiä.":"Midea 0x40 is a full-state command. The controller reads the pump state, sends one command and verifies it by a fresh query. Native timers and model-specific extras may behave differently; Lite field testing is pending.","Midean manuaalinen lämpötilan ja toimintatilan ohjaus on nyt kokeellinen; automaattisen optimoinnin komentoja ei vielä lähetetä.":"Manual Midea control and automatic price and thermostat control are experimental.","Päivitä yhteysasetukset, jos laitteen IP-osoite vaihtuu.":"Update the connection settings if the device IP address changes.","Ohjausta ei vielä lähetetty.":"No control command has been sent.","Pumppu ei vahvistanut muutosta takaisinluvussa.":"Pump did not confirm the requested change in readback.","Ohjauksen suorittamiseen ei ollut resursseja.":"Resources unavailable to execute control.","Pumpun ohjaus vahvistettu takaisinluvulla.":"Pump control confirmed by a fresh readback.","Ohjaus odottaa suorittamista.":"Control queued.","Ohjausta suoritetaan.":"Control in progress.","Asetustunniste puuttuu.":"Settings token unavailable.","Valitse ensin vähintään yksi muutettava asetus.":"Select at least one setting to change.","Lähetetään manuaalinen Midea-ohjaus…":"Sending manual Midea control…","Ohjaus jonossa. Pumpun vastaus ja takaisinluku tarkistetaan.":"Control queued; verifying pump response and readback.","Ohjausta ei voitu käynnistää: ":"Could not queue control: ","Manuaalinen":"Manual","Kokeellinen manuaaliohjaus":"Experimental manual control","Midean mallikohtainen koekäyttöhyväksyntä":"Model-specific Midea pilot approval","Käyttöönotto koskee vain yhtä asetettua laitetta ja käynnistyskertaa. Anna pumpun tarkka mallimerkintä sekä mallille varmistetut pyynnin rajat. Tämä ei luo salasanaa tai poista ohjausominaisuuksia.":"Approval applies only to one configured device and one boot session. Enter the exact pump model and the model-verified setpoint limits. No password or feature removal is introduced.","Pumpun tarkka malli":"Exact pump model","Mallimerkintä":"Model identifier","Mallin minimipyynti °C":"Model minimum setpoint °C","Mallin maksimipyynti °C":"Model maximum setpoint °C","Mallin minimi":"Model minimum","Mallin maksimi":"Model maximum","Hyväksy mallikohtainen ohjaustesti":"Approve model-specific control test","Peru koekäyttöhyväksyntä":"Revoke pilot approval","Mallikohtaista ohjaustestiä ei ole hyväksytty.":"Model-specific control test not approved.","Mallikohtainen ohjaustesti hyväksytty. Pyyntirajat: ":"Model pilot approved. Setpoint limits: ","Tarkista malli, device ID ja mallikohtaiset lämpötilarajat.":"Check model, device ID, and model-specific setpoint limits.","Hyväksytkö Midea ":"Do you approve a manual control pilot for Midea "," -pumpun manuaalisen ohjauskokeen tälle laitteelle?":" on this configured device?","Mallin manuaaliohjaus hyväksytty.":"Manual control pilot approved.","Koekäyttöhyväksyntä peruttu.":"Pilot approval revoked.","Koekäytön asetusta ei voitu muuttaa: ":"Could not change pilot approval: ","Valitun alueen 15 minuutin hinnat. Kaavio näyttää vain ladatun tulevan datan.":"15-minute prices for the selected area. The chart shows only downloaded future data.","Tarkista sopimuksen luvut: käytä vain numeroita.":"Check the contract numbers: use digits only.","Arvonlisävero on enintään 50 %.":"VAT can be at most 50 %.","Hintapyyntö oli virheellinen.":"The price request was invalid.","Hintapalvelun vastaus ei ollut luettavissa.":"The price service response could not be read.","Hintapalvelu palautti väärän alueen.":"The price service returned the wrong area.","Hintapalvelu palautti väärän valuutan.":"The price service returned the wrong currency.","Hintapalvelun vastaus oli epätäydellinen.":"The price service response was incomplete.","Hintapalvelun vastaus sisälsi virheellisen arvon.":"The price service response contained an invalid value.","Hintapalvelun vastaus oli liian suuri.":"The price service response was too large.","Laitteen muisti ei riittänyt hintahakuun juuri nyt.":"The device did not have enough free memory for the price request right now.","Hintapalveluun ei saatu yhteyttä.":"Could not reach the price service.","Hintapalvelu ei ole vielä julkaissut tämän päivän hintoja tälle alueelle.":"The price service has not published today's prices for this area yet.","Laite on kotiverkossa.":"The device is on your home network.","Sijainti ja sääennuste":"Location and weather forecast","ennustetta ei ole vielä haettu.":"the forecast has not been fetched yet.","Sijaintia ei ole valittu.":"No location has been chosen.","Sähkömarkkina-aluetta ei ole valittu.":"No electricity market area has been chosen.","Hintoja ei ole vielä haettu.":"Prices have not been fetched yet.","Sähkömarkkina-alue ja hinnat":"Market area and prices","Sähkösopimus":"Electricity contract","Sopimuksen hinnat on annettu.":"The contract prices have been entered.","Sopimustasi ei ole vahvistettu: kokonaishinta käyttää oletusarvoja.":"Your contract is not confirmed: the total price uses default values.","Lämpöpumppu":"Heat pump","Yhteys toimii ja tietoa tulee.":"The connection works and data is arriving.","Valittu, mutta yhteyttä ei ole vielä todennettu.":"Selected, but the connection has not been verified yet.","Lämpöpumppua ei ole valittu.":"No heat pump has been selected.","Huonelämpötila-anturi":"Room temperature sensor","Tuore RuuviTag-mittaus.":"Fresh RuuviTag measurement.","Ei tuoretta anturia: optimointi ei käytä pumpun omaa lukemaa huonelämpötilana.":"No fresh sensor: optimization does not treat the pump's own reading as room temperature.","Sähkönkulutusmittari (valinnainen)":"Power meter (optional)","Mittari antaa lukemia.":"The meter is reporting readings.","Ei mittaria. Voit lisätä sen myöhemmin.":"No meter. You can add one later.","Optimoinnin valmius":"Optimization readiness","Suunnitelma on laskettu.":"A plan has been calculated.","Optimointi odottaa hintoja, ennustetta ja mittausta.":"Optimization is waiting for prices, a forecast and a measurement.","Kirjoita vähintään kolme kirjainta.":"Type at least three letters.","Haku on kesken. Odota hetki ja yritä uudelleen.":"A search is already running. Wait a moment and try again.","Laite ei saanut yhteyttä karttapalveluun. Anna koordinaatit käsin.":"The device could not reach the map service. Enter the coordinates by hand.","Karttapalvelu ei vastannut oikein. Yritä myöhemmin tai anna koordinaatit käsin.":"The map service did not answer correctly. Try later or enter the coordinates by hand.","Karttapalvelun vastaus ei kelvannut. Anna koordinaatit käsin.":"The map service response was not usable. Enter the coordinates by hand.","Laitteen muisti ei riittänyt hakuun juuri nyt. Yritä hetken kuluttua.":"The device did not have enough free memory for the search right now. Try again shortly.","Laite ei vastaa tai valtuutus puuttuu. Päivitä sivu.":"The device is not responding or authorization is missing. Reload the page.","Kieli ja maa":"Language and country","Valitse käyttökieli ja maa, jossa lämpöpumppu sijaitsee. Maa määrää sähkömarkkina-alueet ja valuutan.":"Choose the language and the country where the heat pump is located. The country determines the electricity market areas and the currency.","Valitse maa…":"Choose a country…","Valuutta: ":"Currency: ","sähkömarkkina-alue":"market area","sähkömarkkina-aluetta":"market areas","Maata ei näy listassa? Hintatietoa ei ole vielä saatavilla sieltä; muut toiminnot voi ottaa käyttöön ilman sitä.":"Your country is not listed? No price data is available there yet; the other functions can be used without it.","Valitse maa.":"Choose a country.","Sijainti":"Location","Sääennuste haetaan sijainnillesi. Kirjoita kaupunki tai kunta ja valitse ehdotuksista.":"The weather forecast is fetched for your location. Type a city or municipality and pick from the suggestions.","Kaupunki tai kunta":"City or municipality","Hakutulokset":"Search results","Valittu: ":"Selected: ","Anna koordinaatit käsin":"Enter coordinates by hand","Leveys, pituus (esim. 61.4638, 24.0760)":"Latitude, longitude (e.g. 61.4638, 24.0760)","Koordinaatit":"Coordinates","Käytä pisteitä desimaalierottimena. Sijainnin voi muuttaa myöhemmin asetuksista.":"Use a dot as the decimal separator. You can change the location later in settings.","Haetaan…":"Searching…","Laite ei vastannut. Yritä uudelleen.":"The device did not respond. Try again.","Haku epäonnistui.":"The search failed.","Ei hakutuloksia. Tarkista kirjoitusasu tai anna koordinaatit käsin.":"No results. Check the spelling or enter the coordinates by hand.","Haku kestää odotettua kauemmin. Yritä uudelleen.":"The search is taking longer than expected. Try again.","Valitse kaupunki hakutuloksista tai anna koordinaatit.":"Pick a city from the results or enter the coordinates.","Sähkömarkkina-alue":"Electricity market area","Valitse ensin maa.":"Choose a country first.","Maassasi on yksi sähkömarkkina-alue; se on valittu valmiiksi.":"Your country has one market area; it is already selected.","Maassasi on useita sähkömarkkina-alueita. Valitse sähkösopimuksesi alue (näkyy laskussa). Aluetta ei arvata kaupungista.":"Your country has several market areas. Choose the area of your electricity contract (shown on your bill). The area is not guessed from the city.","Hinnat haetaan valuutassa ":"Prices are fetched in "," Nord Poolin päivänsisäisistä hinnoista.":" from the Nord Pool day-ahead market.","Valitse sähkömarkkina-alue.":"Choose a market area.","Pörssihinta ei ole se hinta, jonka maksat. Anna sopimuksesi hinnat laskusta. Ilman niitä kokonaishinta käyttää oletusarvoja ja merkitään vahvistamattomaksi.":"The spot price is not the price you pay. Enter your contract prices from your bill. Without them the total price uses default values and is marked unconfirmed.","Sähkönmyyjän marginaali":"Retailer margin","Siirto, päivä":"Transfer, day","Siirto, yö":"Transfer, night","Sähkövero":"Electricity tax","Arvonlisävero (%)":"VAT (%)","Sisältävätkö hinnat arvonlisäveron":"Whether prices include VAT","Hinnat ilman arvonlisäveroa":"Prices excluding VAT","Hinnat sisältävät arvonlisäveron":"Prices including VAT","Verollisuus":"VAT treatment","Kuukausimaksut eivät vaikuta tuntihintaan, joten niitä ei kysytä. Päivä- ja yöaikojen rajat sekä kausisiirto löytyvät Asetuksista.":"Monthly fees do not affect the hourly price, so they are not asked. Day/night limits and seasonal transfer are in Settings.","Valitse lämpöpumppusi. Näytetään vain ne yhteydet, jotka tässä ohjelmistossa on toteutettu; kokeelliset on merkitty.":"Choose your heat pump. Only connections implemented in this software are shown; experimental ones are marked.","Tämä ohjelmisto ei vielä sisällä valittavia lämpöpumppuyhteyksiä.":"This software does not include any selectable heat pump connections yet.","Osoite on kotiverkkosi paikallinen osoite. Lämpöpumppuun ei lähetetä komentoja ennen kuin otat automaattiohjauksen erikseen käyttöön Asetuksista.":"The address is a local address on your home network. No commands are sent to the heat pump until you separately enable automatic control in Settings.","Midean paikallinen yhteys (V2/V3) viimeistellään Asetukset-näkymän Midea-kortissa, kun käyttöönotto on valmis.":"The local Midea connection (V2/V3) is finished in the Midea card in Settings once setup is complete.","Anturit ja kulutusmittari":"Sensors and power meter","Huonelämpötila tulee RuuviTag-anturilta. Ilman sitä Lite ei pidä pumpun omaa lukemaa todistettuna huonelämpötilana.":"Room temperature comes from a RuuviTag sensor. Without it Lite does not treat the pump's own reading as a proven room temperature.","Sidottu anturi: ":"Bound sensor: ","ei sidottu":"not bound","Havaittu: ":"Detected: ","ei havaintoa":"no detection","Anturin sidonta ja nimeäminen tehdään Asetukset-näkymän RuuviTag-kortissa.":"Sensor binding and naming is done in the RuuviTag card in Settings.","Shelly-mittarin osoite (valinnainen)":"Shelly meter address (optional)","Mittarin tulee mitata vain lämpöpumppua, ei koko taloa. Lite ei ohjaa Shellyn relettä.":"The meter must measure only the heat pump, not the whole house. Lite does not control the Shelly relay.","Yhteenveto":"Summary","Seuraavat asiat vaativat vielä toimenpiteitä:":"These items still need action:","Kaikki on kunnossa.":"Everything is in order.","Dashboardiin pääsee, vaikka kaikki ei olisi valmista. Jatka asetuksia myöhemmin Asetukset-näkymässä.":"You can enter the dashboard even if not everything is ready. Continue setting up later in Settings.","Anturit ja mittari":"Sensors and meter","Valmis, avaa dashboard":"Done, open the dashboard","Jatka":"Continue","Maalistaa ei voitu lukea laitteelta. Päivitä sivu.":"The country list could not be read from the device. Reload the page.","Ohjattu käyttöönotto":"Guided setup","Vaihda maa, sijainti, sähkömarkkina-alue tai lämpöpumppu vaihe vaiheelta. Nykyiset asetukset säilyvät, kunnes tallennat uuden valinnan.":"Change the country, location, market area or heat pump step by step. Current settings stay until you save a new choice.","Avaa käyttöönotto uudelleen":"Open setup again","Enevina Lite · käyttöönotto":"Enevina Lite · setup","Sulje ja jatka myöhemmin":"Close and continue later","Ohita tämä vaihe":"Skip this step","Hintoja ei voitu hakea.":"Prices could not be fetched."," Viimeisin onnistunut haku: ":" Last successful fetch: ","Maa":"Country","Takaisin":"Back","Esim. Kangasala":"E.g. Kangasala","Midean paikallinen yhteys tarkistetaan Asetukset-näkymän Midea-kortissa.":"The local Midea connection is checked in the Midea card in Settings.","Tietoa tulee, mutta UART-lähdettä ei ole varmennettu.":"Data is arriving, but the UART source has not been verified.","Valittu, mutta tuoretta tietoa ei tule.":"Selected, but no fresh data is arriving.","Mukavuus":"Comfort","Pitää lämpötilan tasaisena ja reagoi vain suuriin hintaeroihin.":"Keeps the temperature steady and reacts only to large price differences.","Tasapainoinen":"Balanced","Käyttää osan sallitusta vaihteluvälistä: lämmittää hieman ennakkoon edullisella sähköllä ja venyttää kalliilla.":"Uses part of the allowed range: pre-heats a little when electricity is cheap and coasts when it is expensive.","Säästö":"Saving","Käyttää sallitun vaihteluvälin tehokkaammin ja vertaa hintaa pidemmälle. Lämpötila vaihtelee enemmän, mutta pysyy asettamiesi rajojen sisällä.":"Uses the allowed range more fully and compares prices over a longer window. The temperature varies more but stays within the limits you set.","Valitse optimointiprofiili.":"Choose an optimization profile.","Valitse Ylläpitolämpö tai Asuttu, jotta voit asettaa tavoitelämpötilan.":"Select Maintenance or Occupied to set a room target.","Tavoitelämpötilan tulee olla ":"The target temperature must be "," °C ja puolen asteen tarkkuudella.":" °C in steps of 0.5.","Rajojen tulee olla 5–26 °C ja puolen asteen tarkkuudella.":"Limits must be 5–26 °C in steps of 0.5.","Pienimmän ja suurimman lämpötilan välissä tulee olla vähintään 1 °C.":"There must be at least 1 °C between the minimum and maximum temperature.","Pienimmän sallitun lämpötilan tulee olla vähintään 0,5 °C tavoitetta alempi.":"The minimum allowed temperature must be at least 0.5 °C below the target.","Suurimman sallitun lämpötilan tulee olla vähintään 0,5 °C tavoitetta ylempi.":"The maximum allowed temperature must be at least 0.5 °C above the target.","Optimointiprofiili":"Optimization profile","Ei omia lämpötilarajoja: käytetään tavoite ±0,5 °C.":"No own temperature limits: using target ±0.5 °C.","Lämpötila ja optimointi":"Temperature and optimization","Aseta haluttu lämpötila ja rajat, joiden sisällä Lite saa hyödyntää sähkön hinnanvaihtelua.":"Set the desired temperature and the limits within which Lite may use electricity price variations.","Haluttu sisälämpötila (°C)":"Desired indoor temperature (°C)","Asuttu-tila: ":"Occupied mode: ","Ylläpitolämpö-tila: ":"Maintenance mode: ","Lämmitystilaa (Ylläpitolämpö tai Asuttu) ei ole valittu; tavoite asetetaan Asetukset-näkymässä.":"No heating mode (Maintenance or Occupied) is selected; the target is set in Settings.","Pienin sallittu lämpötila (°C, valinnainen)":"Minimum allowed temperature (°C, optional)","Suurin sallittu lämpötila (°C, valinnainen)":"Maximum allowed temperature (°C, optional)","Lite ei lupaa etukäteen tiettyä säästöä. Rajat pysyvät samoina kaikissa profiileissa; profiili määrää vain, kuinka paljon vaihteluväliä ja hintaeroa käytetään. Valitse automaattiohjaus yhteenvedossa.":"Lite does not promise a specific saving in advance. The limits are the same in every profile; the profile only decides how much of the range and of the price difference is used. Choose automatic control in the summary.","Signaali tuntematon":"Signal unknown","Hyvä signaali":"Good signal","Kohtalainen signaali":"Fair signal","Heikko signaali":"Weak signal","Gen1-laitetta ei vielä tueta; tarvitaan Gen2 tai uudempi.":"Gen1 devices are not supported yet; Gen2 or newer is required.","Laitteessa on salasana. Lite ei vielä tue salasanallisia Shelly-laitteita.":"The device has a password. Lite does not support password-protected Shelly devices yet.","Ei tunnistettu Shelly-laitteeksi.":"Not recognized as a Shelly device.","teholukemaa ei saatu":"no power reading","Mittari mittaa vain lämpöpumppua":"The meter measures only the heat pump","Mittari mittaa koko talon (tai muuta kuin pelkkää pumppua)":"The meter measures the whole house (or more than just the pump)","En tiedä vielä":"I don't know yet","Anturin sidonta epäonnistui: ":"Sensor binding failed: ","Nimitaulukko on täynnä (8 anturia).":"The name table is full (8 sensors).","Nimessä on kiellettyjä merkkejä tai se on liian pitkä (enintään 23 merkkiä).":"The name contains forbidden characters or is too long (at most 23 characters).","Nimen tallennus epäonnistui.":"Saving the name failed.","Anturilistaa ei voitu lukea laitteelta.":"The sensor list could not be read from the device.","Bluetooth-skannaus ei ole vielä käynnissä.":"Bluetooth scanning is not running yet.","Skannaus on viivästynyt, koska laitteen muisti on käytössä muualla. Yritetään uudelleen.":"Scanning was delayed because the device memory is in use elsewhere. Retrying.","Haetaan RuuviTageja… Anturi ilmestyy listaan, kun se lähettää mittauksen (noin minuutin välein).":"Searching for RuuviTags… A sensor appears in the list when it sends a measurement (about once a minute).","Nimi, esim. Olohuone":"Name, e.g. Living room","Anturin nimi":"Sensor name","Sisäanturi":"Indoor sensor","Ulkoanturi":"Outdoor sensor","Mittaus tuore: ":"Fresh measurement: ","Ei kuulu: viimeisin mittaus ":"Not heard: last measurement "," sitten":" ago","Käytä sisäanturina":"Use as indoor sensor","Käytä ulkoanturina":"Use as outdoor sensor","Vahvista: sisälämpötilahistoria tyhjennetään":"Confirm: the indoor temperature history will be cleared","Poista sidonta":"Remove binding","Sidottu anturi ei lähetä mittauksia juuri nyt. Optimointi ei käytä sen vanhaa lukemaa.":"The bound sensor is not sending measurements right now. Optimization does not use its old reading.","Etsitään Shelly-laitteita kotiverkosta…":"Looking for Shelly devices on your home network…","Haku epäonnistui. Anna osoite käsin.":"The search failed. Enter the address by hand.","Shelly-laitteita ei löytynyt. Tarkista, että laite on samassa verkossa, tai anna osoite käsin.":"No Shelly devices found. Check that the device is on the same network, or enter the address by hand.","Valittu":"Selected","Käytä tätä mittaria":"Use this meter","Huonelämpötila-anturit (RuuviTag)":"Room temperature sensors (RuuviTag)","Valitse listasta sisäanturi ja halutessasi ulkoanturi. Ilman tuoretta sisäanturia Lite ei pidä pumpun omaa lukemaa todistettuna huonelämpötilana.":"Pick an indoor sensor from the list and, if you like, an outdoor sensor. Without a fresh indoor sensor Lite does not treat the pump's own reading as a proven room temperature.","Löydetyt anturit":"Sensors found","Lite lukee Shelly-mittarin tehoa; se ei koskaan ohjaa Shellyn relettä.":"Lite reads the Shelly meter's power; it never controls the Shelly relay.","Etsi Shelly-mittarit kotiverkosta":"Find Shelly meters on the home network","Mittarin osoite (tai valitse yllä)":"Meter address (or choose above)","Mitä mittari mittaa?":"What does the meter measure?","Mittarin mittauskohde":"What the meter measures","Tarkistus: kytke lämpöpumppu päälle ja pois ja katso, muuttuuko teholuku vastaavasti. Vain lämpöpumppuun varmistettua mittaria käytetään pumpun kulutusmalliin; muuten luku näytetään vain tietona.":"Check: switch the heat pump on and off and see whether the power figure changes accordingly. Only a meter confirmed to measure the pump is used for the pump consumption model; otherwise the figure is shown for information only.","Mittari antaa lukemia ja mittaa vain lämpöpumppua.":"The meter reports readings and measures only the heat pump.","Mittari antaa lukemia, mutta sen kohdetta ei ole varmistettu lämpöpumpuksi: lukua ei käytetä pumpun kulutusmalliin.":"The meter reports readings, but it is not confirmed to measure the heat pump: the figure is not used for the pump consumption model.","Laite ei vastannut.":"The device did not respond.","Edullinen":"Cheap","Normaali":"Normal","Kallis":"Expensive","Ei vertailua":"No comparison","Lämmitys päällä":"Heating on","Lämmitys pois":"Heating off","Pidetään nykyinen tila":"Keeping the current state","Ei päätöstä":"No decision","huonelämpötila-anturin lukema ei ole tuore":"the room sensor reading is not fresh","hintatietoa ei ole":"there is no price data","lämmitystä ei ole pyydetty tässä tilassa":"heating is not requested in this mode","huone on tavoitetta kylmempi":"the room is colder than the target","huone on tavoitetta lämpimämpi":"the room is warmer than the target","sähkö on nyt edullista":"electricity is cheap now","sähkö on nyt kallista":"electricity is expensive now","termostaatti ilman hintatietoa":"thermostat without price data","hinta ei poikkea merkittävästi":"the price does not differ significantly","huone on sallitulla alueella":"the room is within the allowed range","syy ei tiedossa":"reason unknown","Edullinen: lämmitys sallittu tavoitteen yläpuolelle asti, enintään +":"Cheap: heating is allowed above the target, up to +","Edullinen: lämmitetään vain, jos huone on tavoitetta kylmempi.":"Cheap: heating only if the room is colder than the target.","Kallis: lämmitys pidetään pois, kunnes huone on laskenut tavoitteen alle enintään ":"Expensive: heating stays off until the room has dropped below the target by at most ","Kallis: ei lämmitetä, kun huone on tavoitteessa tai sen yli.":"Expensive: no heating while the room is at or above the target.","Normaali hinta: termostaatti tavoitteen ±0,5 °C sisällä.":"Normal price: thermostat within the target ±0.5 °C.","Vertailuikkunaa ei ole riittävästi (hintoja on alle 2 tunnin ajalle).":"Not enough of a comparison window (prices cover less than 2 hours).","Edullinen jakso ":"Cheap period ","Kallis jakso ":"Expensive period ","keskihinta ":"average price "," vertailumediaania alempi)":" below the comparison median)"," vertailumediaania korkeampi)":" above the comparison median)","Hintojen luokittelu ja ohjauksen liikkumavara":"Price classes and the controller's room to move","Päätös nyt":"Decision now","Hinnat luokitellaan samalla säännöllä kuin ohjaus: jakso on edullinen tai kallis, kun se poikkeaa vertailuikkunan mediaanista vähintään kynnyksen verran.":"Prices are classed by the same rule as the controller: a period is cheap or expensive when it differs from the comparison window's median by at least the threshold.","Ulkolämpötilaennuste (oikea asteikko)":"Outdoor forecast (right axis)","Jaksot":"Periods","Aikajanaa ei voida piirtää: tuoreita ja yhtenäisiä hintoja ei ole saatavilla.":"The timeline cannot be drawn: no fresh, continuous prices are available.","Kaavio näyttää, miten ohjaus luokittelee hinnat ja paljonko liikkumavaraa profiili antaa. Huonelämpötilan ennustetta ja kustannusarviota ei näytetä, koska rakennuksen lämpömallia ei ole vielä kalibroitu.":"The chart shows how the controller classes prices and how much room the profile gives it. No indoor temperature forecast or cost estimate is shown because the building's thermal model has not been calibrated yet.","Ulkolämpötilaennuste on taustatieto: nykyinen ohjaus ei käytä sitä päätöksessään.":"The outdoor forecast is background information: the current controller does not use it in its decision.","Päätös tehdään 15 minuutin välein mitatun huonelämpötilan perusteella. Tulevat jaksot kuvaavat aikomusta, eivät sitoumusta.":"The decision is made every 15 minutes from the measured room temperature. Future periods describe intent, not a commitment.","vertailumediaani":"comparison median","ulkolämpötila":"outdoor temperature","Ulkolämpötilaennustetta ei ole tälle ajalle.":"There is no outdoor forecast for this time.","Nyt":"Now","Sähkön hinta ja ohjauksen luokitus seuraaville tunneille":"Electricity price and controller classification for the coming hours","Profiili: ":"Profile: ","Tavoite: ":"Target: ","Rajat: ":"Limits: ","Rajoja ei ole asetettu (tavoite ±0,5 °C)":"No limits set (target ±0.5 °C)","Tavoitelämpötilaa ei ole tässä tilassa":"There is no target temperature in this mode","Ei jaksoja, joissa hinta poikkeaisi vertailumediaanista riittävästi.":"No periods where the price differs enough from the comparison median.","Hintatiedot kattavat vain osan ajasta: kaavio päättyy hintapuutteeseen.":"Prices cover only part of the time: the chart ends where prices are missing.","Profiili":"Profile","Tavoite":"Target","Rajat":"Limits","Edullinen hinta. Tässä tilassa ei ohjata lämmitystä.":"Cheap price. Heating is not controlled in this mode.","Kallis hinta. Tässä tilassa ei ohjata lämmitystä.":"Expensive price. Heating is not controlled in this mode.","Normaali hinta. Tässä tilassa ei ohjata lämmitystä.":"Normal price. Heating is not controlled in this mode.","Ehdotus: lämmitys":"Proposal: heating","Ehdotus: lämmitys pois":"Proposal: heating off","Ehdotus: nykyinen tila pidetään":"Proposal: keep current mode","Kuivanapidon kosteusehto estää lämmitysehdotuksen; sähkönhinta ei muuta sitä.":"Drying humidity conditions prohibit heating proposals; electricity prices do not override this.","Huoneanturin mittaus ei ole tuore; lämmitysehdotusta ei tehdä ennen uutta mittausta.":"The room sensor reading is stale; heating is not proposed until a fresh measurement is available.","Lämpötilaraja on ristiriidassa valitun tavoitelämpötilan kanssa.":"The temperature limit conflicts with the selected target temperature.","Mukavuusasetuksia ei voitu vahvistaa laitteen muistista.":"Comfort settings could not be verified in device storage.","Keskeytynyt tallennus odottaa turvallista palautusta. Yritä tallentaa asetukset uudelleen.":"An interrupted save needs safe recovery. Retry saving the settings.","Keskeytyneen tallennuksen muistitietoa ei voida todentaa.":"An interrupted save could not be verified from device storage.","Tallenna muut asetukset erikseen lämpötilatavoitteesta.":"Save unrelated settings separately from the temperature target.","Switch:0-tehomittausta ei voitu vahvistaa. Tarkista malli/yhteys ja etsi uudelleen.":"Switch:0 power measurement could not be verified. Check the model/connection and scan again.","24 h":"24 h","7 vrk":"7 days","30 vrk":"30 days","Lämpöpumpun tiedonsiirto":"Heat pump data link","Hintatiedot":"Price data","Laite käynnistyi (uudelleenkäynnistyksen syykoodi ":"The device started (reset reason code "," katkesi":" was lost"," palasi, katko kesti ":" came back; the outage lasted ","Lite lähetti lämpöpumpulle ohjauskomentoja: ":"Lite sent control commands to the heat pump: ","Pumppu kuittasi ohjauskomentoja: ":"The pump acknowledged control commands: ","Ohjauskomentoja epäonnistui: ":"Control commands failed: ","Tuntematon tapahtuma":"Unknown event","Ei kulutuslukemia tältä ajalta.":"No consumption readings for this period.","Mittari mittaa koko talon: lukua ei voi pitää lämpöpumpun kulutuksena":"The meter measures the whole house: the figure cannot be treated as heat pump consumption","Mittarin mittauskohdetta ei ole varmistettu: lukua ei voi pitää lämpöpumpun kulutuksena":"What the meter measures is not confirmed: the figure cannot be treated as heat pump consumption","mitattua jaksoa":"measured periods","Historia":"History","Mitatut tiedot":"Measured data","Lämpötilat, hinnat, kulutus ja tapahtumat laitteen tallentamista mittauksista.":"Temperatures, prices, consumption and events from the measurements the device has recorded.","Aikaväli":"Time range","Historiaa ei ole vielä saatavilla: laite tallentaa mittauksia vasta, kun kello on synkronoitu ja ensimmäinen 15 minuutin jakso on päättynyt.":"No history is available yet: the device only records measurements once its clock is synchronized and the first 15-minute period has ended.","Lämpötilat ja ohjausehdotus":"Temperatures and control proposal","Sisälämpötila":"Indoor temperature","Ulkolämpötila (mitattu)":"Outdoor temperature (measured)","Ulkolämpötila (ennuste)":"Outdoor temperature (forecast)","Pumpun asetusarvo":"Pump setpoint","Ehdotus: pois":"Proposal: off","Ehdotus: pidä tila":"Proposal: keep state","Automaattiohjaus päällä":"Automatic control on","Viiva katkeaa, kun mittausta ei ole: puuttuvia arvoja ei täydennetä. Ehdotusraita näyttää ohjauksen ehdotuksen; komentoja lähetetään vain, kun automaattiohjaus on päällä.":"The line breaks where there is no measurement: missing values are not filled in. The proposal lane shows what the controller proposed; commands are sent only when automatic control is on.","Sähkön kokonaishinta":"Total electricity price","Mitattu teho":"Measured power","Tapahtumat":"Events","Lataa CSV":"Download CSV","Tapahtumat ja 30 päivän jaksot tallentuvat laitteen omaan muistiin tunnin välein; äkillinen sähkökatko voi menettää enintään viimeisen tunnin.":"Events and the 30-day records are saved to the device's own memory once an hour; a sudden power loss can lose at most the last hour.","Tietoja puuttuu merkittävästi: laite on ollut pois päältä, ilman kellonaikaa tai mittaus on katkennut.":"A significant amount of data is missing: the device was off, had no clock, or the measurement was interrupted.","Historia tallentuu vain laitteen muistiin ja nollautuu uudelleenkäynnistyksessä (tallennusosiota ei saatu käyttöön).":"History is kept only in the device's memory and resets on restart (the storage partition could not be used).","Lämpötilamittauksia ei ole tältä ajalta.":"No temperature measurements for this period.","Ei tallennettua mittausta tältä jaksolta.":"No recorded measurement for this period.","Sisä- ja ulkolämpötila sekä pumpun asetusarvo":"Indoor and outdoor temperature and the pump setpoint","Sisälämpötila: ":"Indoor temperature: ","Ulkolämpötila (ennuste): ":"Outdoor temperature (forecast): ","Ulkolämpötila: ":"Outdoor temperature: ","Pumpun asetusarvo: ":"Pump setpoint: ","Ehdotus: ":"Proposal: "," · automaattiohjaus päällä":" · automatic control on","tallennettu jakso":"recorded period","tallennettua jaksoa":"recorded periods","Hintatietoja ei ole tallennettu tältä ajalta.":"No prices were recorded for this period.","Ei tallennettua hintaa.":"No recorded price.","Kokonaishinta: ":"Total price: ","Kulutusmittarin lukemia ei ole tältä ajalta.":"No consumption meter readings for this period.","Ei tallennettua mittausta.":"No recorded measurement.","Teho: ":"Power: ","Sisälämpötila (min … max · keskiarvo)":"Indoor temperature (min … max · average)","Ulkolämpötila (min … max · keskiarvo)":"Outdoor temperature (min … max · average)","Kokonaishinta (keskiarvo)":"Total price (average)","Ohjausehdotuksen aikana automaattiohjaus päällä":"Periods with automatic control on","Ei tapahtumia tällä aikavälillä.":"No events in this time range.","Tietoa on tallennettu {pct} % ajasta ({present} / {total} jaksoa)":"Data has been recorded for {pct} % of the time ({present} / {total} periods)","{n} jaksoa":"{n} periods","Mittarin mittauskohde on vaihtunut tällä jaksolla: energiaa ei lasketa yhteen eri mittauskohteiden kesken.":"The meter's measurement target changed during this period: energy is not added up across different targets.","Mittauskattavuus ei riitä energian laskentaan.":"Insufficient meter coverage to calculate energy.","Tehon mittauskattavuus {n} / {total} min":"Verified power measurement coverage {n} / {total} min","Tehomittauksessa on puuttuvia jaksoja.":"Power meter coverage contains gaps.","Eri valuuttoja ei voi verrata samalla asteikolla.":"Different historical currencies cannot be compared on one axis.","Hintavaluutta ei ole tiedossa vanhoille mittauksille.":"The currency of older price observations is unknown.","Ulkolämpötila (mitattu ja ennuste): ":"Outdoor temperature (measured and forecast): ","Voimassa ":"Valid until ","Hinnat päättyvät ":"Prices end ",": alle 2 tuntia jäljellä, ohjaus siirtyy termostaattiin.":": less than 2 hours left, control switches to the thermostat.","Haettu ":"Fetched ",", liian vanhoja (yli 36 h): niitä ei käytetä.":", too old (over 36 h): they are not used.","Voimassaolo päättyi ":"Validity ended ",": vanhoja hintoja ei käytetä uutena tietona.":": old prices are not used as new data.","Viimeisin onnistunut haku ":"Last successful fetch ","haettu ":"fetched ",": ennustetta ei käytetä.":": the forecast is not used.","Kellonaikaa ei ole synkronoitu: ennustetta ei voi käyttää.":"The clock is not synchronized: the forecast cannot be used.","Ennustetta ei ole vielä haettu.":"The forecast has not been fetched yet.","Tuore mittaus":"Fresh measurement","Anturia ei ole sidottu.":"No sensor is bound.",": viimeisin ":": last ",". Lämmitysehdotusta ei tehdä.":". No heating proposal is made.","Lämpöpumpun tiedot":"Heat pump data","Tietoa tulee":"Data is arriving","Ei tuoretta tietoa pumpulta":"No fresh data from the pump","Kellonaikaa ei ole synkronoitu: hintoja ei käytetä, ennen kuin aika on tiedossa.":"The clock is not synchronized: prices are not used until the time is known.","Ei verkkoyhteyttä. Laite toimii paikallisesti tallennetuilla hinnoilla ":"No network connection. The device works locally on stored prices until "," asti; sen jälkeen huone pidetään termostaattina.":"; after that the room is held by the thermostat.","Ei verkkoyhteyttä: ulkoiset tiedot eivät päivity. Laite toimii paikallisesti.":"No network connection: external data is not updating. The device works locally.","Lämmitysehdotusta ei tehdä":"No heating proposal is made","Tuoretta huonelämpötilamittausta ei ole. Lämpöpumppu toimii omalla säätimellään; Lite ei lähetä komentoja.":"There is no fresh room temperature measurement. The heat pump runs on its own controller; Lite sends no commands.","Huone pidetään termostaattina":"The room is held by the thermostat","Käyttökelpoista hintatietoa ei ole: huone pidetään tavoitteen ±0,5 °C sisällä ilman hintaoptimointia.":"No usable price data: the room is kept within the target ±0.5 °C without price optimization.","Toimii tallennetuilla tiedoilla":"Working on stored data","Kaikki tiedot ovat tuoreita":"All data is fresh","Hinnat ja huonelämpötila ovat käytettävissä: hintaohjaus on toiminnassa.":"Prices and room temperature are available: price-based control is working.","Päivityskanavaa ei ole tällä laitteella":"This device has no update channel","Tämä ohjelmisto on käännetty ilman allekirjoitettua päivityskanavaa, joten päivitystä ei voi tehdä dashboardista.":"This software was built without a signed update channel, so it cannot be updated from the dashboard.","Tarkistus on jonossa":"The check is queued","Laite tarkistaa julkaisun heti, kun se on valmis.":"The device will check for a release as soon as it is ready.","Päivitys latautuu":"Update downloading","Ladattu ":"Downloaded ",". Allekirjoitus ja tarkistussumma varmistetaan ennen asennusta; edellinen toimiva versio säilyy.":". The signature and checksum are verified before installation; the previous working version is kept.","Päivitys asennettu":"Update installed","Laite käynnistyy uudelleen ja vahvistaa uuden version vasta, kun käynnistystarkistukset ovat läpi.":"The device restarts and confirms the new version only after the boot checks pass.","Odottaa verkkoa tai kellonaikaa":"Waiting for the network or the clock","Päivityksiä ei voi tarkistaa nyt. Laite toimii normaalisti ilman niitä.":"Updates cannot be checked right now. The device works normally without them.","Tarkistus yritetään myöhemmin uudelleen. Mitään ei ole asennettu.":"The check will be retried later. Nothing has been installed.","Tarkista, että laite pääsee internetiin ja päivityspalvelin on tavoitettavissa. Hylättyä tai vioittunutta julkaisua ei asenneta.":"Check that the device can reach the internet and the update server is reachable. A rejected or damaged release is never installed.","Paina \"Tarkista ja asenna päivitys\". Kelvollinen allekirjoitettu julkaisu asennetaan samalla kertaa.":"Press \"Check and install update\". A valid signed release is installed at the same time.","Havaittu julkaisu ei ole asennettavissa":"The detected release cannot be installed","Viimeisin havaittu julkaisu ":"The latest detected release "," on jo asennettu, aiemmin hylätty tai odottaa vahvistusta.":" is already installed, was rejected earlier, or is awaiting confirmation.","Asennettuna on vahvistettu julkaisu ":"The confirmed release installed is ","Vahvistettua julkaisua ei vielä ole kirjattu.":"No confirmed release has been recorded yet.","julkaisu ":"release ","Ei tiedossa (laite ei ole kirjannut päivitystä)":"Unknown (the device has not recorded an update)","Tietojen tuoreus ja toiminta ilman verkkoa":"Data freshness and behaviour without a network","Vanhentunutta hintaa tai ennustetta ei käytetä uutena tietona. Kun verkko katkeaa, laite jatkaa tallennetuilla tiedoilla niiden voimassaoloajan loppuun ja pitää sen jälkeen huoneen termostaattina; pumpun oma säädin ja turvatoiminnot toimivat aina.":"An outdated price or forecast is never used as new data. When the network drops, the device continues on stored data until it expires and then holds the room by thermostat; the pump's own controller and safety functions always work.","Viimeisin onnistunut päivitys":"Last successful update","Tarkistus ja asennus ovat yksi toiminto: kelvollinen allekirjoitettu julkaisu asennetaan heti, kun se löytyy. Julkaisutietoja ei kuljeteta allekirjoitetussa päivitysmanifestissa; ne löytyvät julkaisusivulta: ":"Checking and installing are one action: a valid signed release is installed as soon as it is found. Release notes are not carried in the signed update manifest; they are on the releases page: ","Enevina Lite -julkaisut GitHubissa":"Enevina Lite releases on GitHub","Tietojen tuoreutta ei voitu lukea laitteelta.":"Data freshness could not be read from the device.","Viimeisin onnistunut tiedonhaku: ":"Last successful data fetch: ","Ei vielä onnistunutta tiedonhakua":"No successful data fetch yet","Hintojen haku epäonnistui.":"Fetching prices failed.","Päivitys käynnistyy uudelleen":"Update restarting","Päivitystapahtuma vahvistettu (julkaisu ":"Update confirmed (release ","Tarkistus ja asennus ovat yksi toiminto: kelvollinen allekirjoitettu julkaisu asennetaan heti, kun se löytyy. Julkaisutietoja ei kuljeteta allekirjoitetussa päivitysmanifestissa; ne löytyvät julkaisusivulta:":"Checking and installing are one action: a valid signed release is installed as soon as it is found. Release notes are not carried in the signed update manifest; they are on the releases page:"," · haettu ":" · fetched ","Ei Wi-Fi-yhteyttä. Tallennetut hinnat ovat voimassa ":"Wi-Fi is disconnected. Cached prices remain valid until "," asti, mutta Wi-Fi-pumppuyhteyttä ei voi käyttää.":", but the Wi-Fi heat pump connection is unavailable.","Ei Wi-Fi-yhteyttä: ulkoiset tiedot eivät päivity eikä Wi-Fi-pumppuyhteyttä voi käyttää.":"Wi-Fi is disconnected: external data is not refreshing and the Wi-Fi heat pump connection is unavailable.","Automaattiohjaus on pois käytöstä.":"Automatic control is disabled.","Wi-Fi-yhteys puuttuu.":"Wi-Fi connection is unavailable.","Lämpöpumpun yhteys ei ole todennetusti kunnossa.":"A working heat pump connection has not been verified.","Vain lämmitysehdotus – pumppua ei ohjata":"Heating proposal only – pump is not controlled","Lämpöpumppu toimii omilla asetuksillaan; Lite ei lähetä komentoja.":"The heat pump follows its own settings; Lite sends no commands.","Hintatietoa ei ole: ohjain tavoittelee ±0,5 °C termostaattialuetta. Käskyt riippuvat edelleen laiteturvaporteista.":"Price data is unavailable: the controller targets a ±0.5 °C thermostat band. Commands remain subject to device safety gates.","Tallennetut tiedot käytössä":"Using cached data","Ohjaustiedot ovat tuoreita":"Control inputs are fresh","Hinnat ja huonelämpötila ovat käytettävissä. Käskyt riippuvat edelleen laiteturvaporteista.":"Prices and room temperature are available. Commands remain subject to device safety gates.","Ohjausportti ei tällä hetkellä salli komentoja.":"The live control gate does not currently permit commands.","Ohjausportti sallii ohjauksen":"The control gate permits operation","Pumpun lisätelemetria on vanhentunut, mutta ohjaimen suora yhteysportti sallii komennot.":"Supplemental pump telemetry is stale, but the direct controller gate permits commands.","Ohjaus odottaa lämmitystilan vähimmäiskestoa, eikä lähetä uutta komentoa.":"Control is waiting for the minimum time in the current heating mode and is not sending a new command.","Lämmitystä ei pyydetä tässä tilassa":"No heating requested in this mode","Kylmillään-tila tai kuivaustilan kosteusraja estää lämmitysehdotuksen. Tämä ei todista, että lämpöpumppu on jo pysähtynyt.":"Cold mode or the Drying humidity gate prevents a heating proposal. This does not prove the heat pump has already stopped."});
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
    if(state.status)showStatus();if(state.prices)showPrices();if(state.weather)showWeather();if(state.timeline)showTimeline();translateDashboard();
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
  if(state.timeline)showTimeline();
  if(state.fresh)showFreshness();
  if(state.hist)showHistoryView();
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
  if(!['home','prices','weather','plan','history','settings'].includes(name))return;
  document.querySelectorAll('.view').forEach(v=>v.classList.toggle('active',v.id==='view-'+name));
  document.querySelectorAll('[data-view]').forEach(b=>{const active=b.dataset.view===name;b.classList.toggle('active',active);if(active)b.setAttribute('aria-current','page');else b.removeAttribute('aria-current');});
  history.replaceState(null,'','#'+name);
  if(name==='history'&&typeof reloadHistoryView==='function')reloadHistoryView();
}
document.querySelectorAll('[data-view]').forEach(b=>b.addEventListener('click',()=>switchView(b.dataset.view)));
window.addEventListener('hashchange',()=>switchView(location.hash.slice(1)));
$('go-prices').addEventListener('click',()=>switchView('prices'));
$('go-weather').addEventListener('click',()=>switchView('weather'));
// Initialize the active history range BEFORE the initial hash route can load it.
state.histRange='24h';
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
  const sum=otaSummary(s,{kib:v=>fmt(v/1024,0)+' KiB'});
  $('ota-headline').className='callout '+sum.kind;
  text('ota-headline-title',sum.headline);text('ota-headline-detail',sum.detail);
  text('ota-last-update',otaLastUpdateText(s,{time:u=>time(u)}));
  $('ota-install').disabled=!available||!token||active||s.ota_dashboard_pending===true;
  text('ota-feedback',available?'Päivitys käyttää vain allekirjoitettua julkaisua ja säilyttää edellisen toimivan OTA-version.':'Allekirjoitettua OTA-päivityskanavaa ei ole tällä laitteella. Päivitystä ei voi käynnistää ennen sen käyttöönottoa.');
}
function showStatus(){
  const s=state.status;if(!s)return;
  const a=s.air||{},fresh=a.sensor_fresh===true,mode=Number.isInteger(a.mode)?a.mode:0;
  const room=s.room_temperature_c===undefined?(fresh?number(a.temperature_c):null):
    number(s.room_temperature_c);
  const age=s.room_temperature_age_s===undefined?number(a.age_s):number(s.room_temperature_age_s);
  const sensor=s.room_temperature_source==='pump'?translateFi('Pumpun oma anturi'):'RuuviTag';
  text('connection',room!==null?'Mittaukset käytössä':'Yhteys muodostettu');
  $('connection-dot').className='dot '+(room!==null?'ok':'bad');
  text('clock',time(Math.floor(Date.now()/1000)));
  text('home-temp',room!==null?fmt(room,1)+' °C':'Ei tuoretta mittausta');
  text('home-mode',modes[mode]||'Ei tietoa');
  text('home-sensor',room!==null&&age!==null?sensor+' · '+fmt(age,0)+' s':sensor);
  text('home-rh',fresh&&number(a.rh_pct)!==null?fmt(a.rh_pct,1)+' %':'—');
  text('home-dew',fresh&&number(a.dewpoint_c)!==null?fmt(a.dewpoint_c,1)+' °C':'—');
  text('home-air-status',fresh?'Tuore':'Puuttuu / vanhentunut');
  text('price-spot-unit',priceDisplayMinorUnit()+'/kWh, alv 0 %');
  text('price-spot-heading','Spot '+priceDisplayMinorUnit()+'/kWh');
  text('price-total-heading','Kokonaishinta '+priceDisplayMinorUnit()+'/kWh');
  text('price-area-chip','Nord Pool · '+(s.price_area||'—')+' · '+priceDisplayCurrency());
  if(refreshRoomTempSettings)refreshRoomTempSettings(s);
  text('room-temperature-value',room===null?'—':fmt(room,2)+' °C · '+sensor);
  text('home-price',priceFmt(s.spot));text('home-total',priceFmt(s.total_price));
  text('home-outside',number(a.outside_temperature_c)!==null?fmt(a.outside_temperature_c,1)+' °C':number(s.weather_c)!==null?fmt(s.weather_c,1)+' °C':'—');
  text('home-watts',number(s.shelly_w)!==null?fmt(s.shelly_w,0)+' W':'—');
  text('price-now',priceFmt(s.spot));text('price-updated','Viimeisin haku: '+time(s.market_last_fetch_utc));
  text('price-area-chip','Nord Pool · '+(s.price_area||'—')+(s.price_currency?' · '+s.price_currency:''));
  if(s.price_error&&typeof WZ_PRICE_ERRORS!=='undefined')
    text('price-updated',(WZ_PRICE_ERRORS[s.price_error]||'Hintoja ei voitu hakea.')+' Viimeisin onnistunut haku: '+time(s.market_last_fetch_utc));
  const isMidea=state.site&&state.site.pump_adapter==='midea_lan';
  const isGree=state.site?.pump_adapter==='gree_ewpe_lan';
  const isDaikin=state.site?.pump_adapter==='daikin_local_api';
  const isDsiot=state.site?.pump_adapter==='daikin_dsiot';
  const isWfRac=state.site?.pump_adapter==='mhi_wf_rac_lan';
  const sourceLabels={MPC:'Automaattinen MPC',PRICE_CONTROL:'Automaattinen hintaohjaus',
    THERMOSTAT:'Automaattinen termostaattiohjaus'};
  text('pilot-state-heading',s.control_armed===true?
    (sourceLabels[s.control_source]||(isMidea?'Midea-automaattiohjaus':isGree?'Gree-automaattiohjaus':isDaikin?'Daikin-automaattiohjaus':isWfRac?'WF-RAC-automaattiohjaus':'CN105-OHJAUS SALLITTU')):'OHJAUS POIS');
  text('pilot-state-text',s.control_armed===true?
    'Automaattiohjaus on kytketty. Käskyjä lähetetään Ylläpitolämpö- tai Asuttu-tilassa, kun mittaus- ja yhteysportit ovat kunnossa.':
    'Automaattiohjaus ei ole päällä. Laskenta näyttää ehdotuksia ilman fyysisiä käskyjä.');
  text('control-state',s.control_armed===true?
    'Automaattiohjaus valittu · '+(s.control_gate||'odottaa'): 'Automaattiohjaus pois');
  if(isDsiot)text('control-last','Tämä Daikin DSIoT -yhteys lukee pumpun tilan.');
  if(!isMidea&&!isGree&&!isDaikin&&!isWfRac&&!isDsiot){
    const phase=p=>!p?'—':p.step==='OFF'?translateFi('Pois päältä')+' '+time(p.utc):
      Number.isFinite(p.target_c)?fmt(p.target_c,1)+' °C '+time(p.utc):'—';
    text('control-last',translateFi('Pyydetty')+' '+phase(s.cn105_control_requested)+
      ' · '+translateFi('Lähetys aloitettu')+' '+phase(s.cn105_control_sent)+
      ' · '+translateFi('Vahvistettu pumpulta')+' '+phase(s.cn105_control_confirmed)+
      ' · '+translateFi('Sillalta kuitattu')+' '+(s.control_commands_confirmed||0)+
      ' · '+translateFi('Lähetys- tai kuittausvirheet')+' '+(s.control_commands_failed||0));
  }
  $('control-enable').disabled=s.control_armed===true || !a.config_token ||
    (isDaikin && daikinStatus?.pilot_approved!==true) ||
    (isWfRac && wfRacStatus?.pilot_approved!==true) || isDsiot;
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
  text('home-plan-next',ok?'Seuraava jakso '+time(p.slots[0].utc)+': '+(p.slots[0].heat?'HEAT '+fmt(p.slots[0].setpoint_c??16,1)+' °C':'OFF'):
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
  p.slots.slice(0,96).forEach(x=>{const tr=body.insertRow();[time(x.utc),x.heat?'HEAT '+fmt(x.setpoint_c??16,1)+' °C':'OFF',fmt(number(x.indoor_c),1),fmt(number(x.kwh),3),fmt(number(x.eur),3)].forEach((v,i)=>{const cell=tr.insertCell();cell.textContent=v;if(i===1)cell.className=x.heat?'heat':'off';});});
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

let tlFormatters={};
function tlHourOf(utc){
  const tz=uiTimeZone();
  const f=tlFormatters[tz]||(tlFormatters[tz]=new Intl.DateTimeFormat('en-GB',{timeZone:tz,hour:'2-digit',hourCycle:'h23'}));
  return Number(f.format(new Date(utc*1000)))%24;
}
let tlResizeTimer=null;
window.addEventListener('resize',()=>{clearTimeout(tlResizeTimer);tlResizeTimer=setTimeout(()=>{if(state.timeline)showTimeline();},250);});
let timelineRequestSequence=0;
async function reloadTimeline(){
  const requestId=++timelineRequestSequence;
  const region=state.site&&state.site.region;
  const expectedMarket=tlMarketKey(region);
  let response=null;
  try{response=await fetchJSON('/api/timeline');}catch(e){}
  // An earlier request may complete after a country/price-zone change.
  if(requestId!==timelineRequestSequence||
     expectedMarket!==tlMarketKey(state.site&&state.site.region))return;
  state.timeline=tlMatchesMarket(response,state.site&&state.site.region)?response:null;
  showTimeline();
}
function showTimeline(){
  const host=$('tl-chart'),
    d=tlMatchesMarket(state.timeline,state.site&&state.site.region)?
      tlValidate(state.timeline):null,weather=state.weather;
  $('tl-empty').hidden=!!d;
  const reasons=$('tl-reasons');reasons.replaceChildren();
  host.replaceChildren();
  if(!d){text('tl-now','—');$('tl-profile').replaceChildren();return;}
  const prof=(WZ_PROFILES.find(p=>p.id===d.profile)||{}).name||d.profile;
  text('tl-now',tlNowText(d.now));
  const tz=uiTimeZone(),tf=new Intl.DateTimeFormat(uiLocale(),{timeZone:tz,hour:'2-digit',minute:'2-digit'}),
    df=new Intl.DateTimeFormat(uiLocale(),{timeZone:tz,weekday:'short',day:'numeric',month:'numeric'});
  // A phone gets a taller, narrower drawing so bars and labels stay legible.
  const narrow=(host.clientWidth||700)<520;
  const ctx={W:narrow?420:720,H:narrow?330:270,label:'Sähkön hinta ja ohjauksen luokitus seuraaville tunneille',lang:uiLanguage,
    fmtPrice:priceFmt,fmtAxis:v=>fmt(v*100,0)+' '+uiMinorUnit(),
    fmtTime:utc=>tf.format(new Date(utc*1000)),dayLabel:utc=>df.format(new Date(utc*1000)),
    fmtRange:(a,b)=>time(a)+' – '+tf.format(new Date(b*1000)),tzHour:tlHourOf,
    classLabel:c=>TL_CLASS_LABEL[c]||TL_CLASS_LABEL.unknown,medianLabel:'vertailumediaani',
    outdoorLabel:'ulkolämpötila',noOutdoor:'Ulkolämpötilaennustetta ei ole tälle ajalle.',nowLabel:'Nyt'};
  tlRender(host,d,weather,ctx);
  d.segments.forEach(g=>{
    const li=document.createElement('li');
    li.textContent=tlSegmentText(g,d,priceFmt,ctx.fmtRange);
    reasons.append(li);
  });
  if(!d.segments.length){const li=document.createElement('li');li.textContent='Ei jaksoja, joissa hinta poikkeaisi vertailumediaanista riittävästi.';li.style.listStyle='none';reasons.append(li);}
  if(d.truncated_at_gap){const li=document.createElement('li');li.textContent='Hintatiedot kattavat vain osan ajasta: kaavio päättyy hintapuutteeseen.';li.style.listStyle='none';reasons.append(li);}
  // Chip built from separate spans so each short label is translated on its own.
  const chip=$('tl-profile');chip.replaceChildren();
  const span=t=>{const e=document.createElement('span');e.textContent=t;return e;};
  const sep=()=>span(' · ');
  chip.append(span('Profiili'),span(': '),span(prof),sep());
  if(d.target_c!=null)chip.append(span('Tavoite'),span(': '+tlDeg(d.target_c)+' °C'));
  else chip.append(span('Tavoitelämpötilaa ei ole tässä tilassa'));
  chip.append(sep());
  if(d.comfort_min_c!=null||d.comfort_max_c!=null)
    chip.append(span('Rajat'),span(': '+(d.comfort_min_c!=null?tlDeg(d.comfort_min_c):'–')+'…'+(d.comfort_max_c!=null?tlDeg(d.comfort_max_c):'–')+' °C'));
  else chip.append(span('Rajoja ei ole asetettu (tavoite ±0,5 °C)'));
}

async function reloadHistoryView(){
  try{state.hist=await fetchJSON('/api/history?range='+state.histRange);}catch(e){state.hist=null;}
  showHistoryView();
}
function showHistoryView(){
  document.querySelectorAll('[data-hrange]').forEach(b=>b.setAttribute('aria-checked',String(b.dataset.hrange===state.histRange)));
  const d=hsValidate(state.hist);
  for(const id of ['hs-temps','hs-price','hs-power','hs-events','hs-summary'])$(id).replaceChildren();
  $('hs-emptycard').hidden=!!d;$('hs-tempcard').hidden=!d;
  text('hs-coverage','');$('hs-warn').hidden=true;text('hs-energy','');
  if(!d)return;
  const tz=uiTimeZone(),tf=new Intl.DateTimeFormat(uiLocale(),{timeZone:tz,hour:'2-digit',minute:'2-digit'}),
    df=new Intl.DateTimeFormat(uiLocale(),{timeZone:tz,weekday:'short',day:'numeric',month:'numeric'}),
    dtf=new Intl.DateTimeFormat(uiLocale(),{timeZone:tz,day:'numeric',month:'numeric',hour:'2-digit',minute:'2-digit'});
  const narrow=($('hs-temps').clientWidth||700)<520;
  const base={W:narrow?420:720,H:narrow?250:240,fmtTime:u=>tf.format(new Date(u*1000)),dayLabel:u=>df.format(new Date(u*1000)),tzHour:tlHourOf};
  text('hs-coverage',hsCoverageText(d)+' · '+hsT(HT('Tehon mittauskattavuus {n} / {total} min'),
    {n:d.power_coverage_minutes??0,total:(d.slots_total||0)*15}));
  const warn=[];
  if(hsLowCoverage(d))warn.push('Tietoja puuttuu merkittävästi: laite on ollut pois päältä, ilman kellonaikaa tai mittaus on katkennut.');
  if((d.power_coverage_minutes||0)<(d.slots_total||0)*15*0.8 &&
     d.points.some(p=>p.w!=null))warn.push(HT('Tehomittauksessa on puuttuvia jaksoja.'));
  if(d.currency==='mixed')warn.push(HT('Eri valuuttoja ei voi verrata samalla asteikolla.'));
  if(d.currency==='unknown')warn.push(HT('Hintavaluutta ei ole tiedossa vanhoille mittauksille.'));
  if(d.persistent===false)warn.push('Historia tallentuu vain laitteen muistiin ja nollautuu uudelleenkäynnistyksessä (tallennusosiota ei saatu käyttöön).');
  if(warn.length){text('hs-warn',warn.join(' '));$('hs-warn').hidden=false;}
  const deg=v=>v==null?'—':fmt(v,1)+' °C';
  hsLineChart($('hs-temps'),d,{...base},{keys:['in','out','sp'],label:'Sisä- ja ulkolämpötila sekä pumpun asetusarvo',
    lines:[{key:'out',cls:'hs-out',split:'os',dashWhen:2},{key:'in',cls:'hs-in'},{key:'sp',cls:'hs-sp',step:true}],lane:true,
    fmtAxis:v=>fmt(v,0)+' °',empty:'Lämpötilamittauksia ei ole tältä ajalta.',noData:'Ei tallennettua mittausta tältä jaksolta.',
    tip:p=>[ 'Sisälämpötila: '+deg(p.in),(p.os===3?'Ulkolämpötila (mitattu ja ennuste): ':p.f?'Ulkolämpötila (ennuste): ':'Ulkolämpötila: ')+deg(p.out),'Pumpun asetusarvo: '+deg(p.sp),
      'Ehdotus: '+(TL_ACTIONS[['UNAVAILABLE','OFF','HEAT_SETPOINT','HOLD'][p.a||0]]||'—')+(p.r?' · automaattiohjaus päällä':''),
      p.n+' '+(p.n===1?'tallennettu jakso':'tallennettua jaksoa')]});
  const currencyOk=['EUR','SEK','NOK','DKK','PLN'].includes(d.currency);
  const historyPriceData=currencyOk?d:{...d,points:d.points.map(p=>({...p,p:null}))};
  const priceUnit=d.currency==='EUR'?'snt/kWh':d.currency+'/kWh';
  const historyPriceFmt=v=>v==null?'—':fmt(d.currency==='EUR'?v*100:v,d.currency==='EUR'?1:3)+' '+priceUnit;
  hsLineChart($('hs-price'),historyPriceData,{...base,H:narrow?200:170},{keys:['p'],bars:'p',barClass:'',label:'Sähkön kokonaishinta',H:narrow?200:170,
    fmtAxis:v=>historyPriceFmt(v),empty:!currencyOk?HT('Eri valuuttoja ei voi verrata samalla asteikolla.'):'Hintatietoja ei ole tallennettu tältä ajalta.',noData:'Ei tallennettua hintaa.',
    tip:p=>['Kokonaishinta: '+(p.p==null?'—':historyPriceFmt(p.p))]});
  hsLineChart($('hs-power'),d,{...base,H:narrow?200:170},{keys:['w'],bars:'w',barClass:'hs-power',label:'Mitattu teho',H:narrow?200:170,
    fmtAxis:v=>Math.round(v)+' W',empty:'Kulutusmittarin lukemia ei ole tältä ajalta.',noData:'Ei tallennettua mittausta.',
    tip:p=>['Teho: '+(p.w==null?'—':Math.round(p.w)+' W')]});
  text('hs-energy',hsEnergyText(d,v=>fmt(v,1)));
  const sum=$('hs-summary');
  const row=(a,b)=>{sum.append(Object.assign(document.createElement('span'),{textContent:a}),Object.assign(document.createElement('span'),{textContent:b}));};
  const mm=r=>r&&r.min!=null?fmt(r.min,1)+' … '+fmt(r.max,1)+' °C · '+fmt(r.avg,1)+' °C':'—';
  row('Sisälämpötila (min … max · keskiarvo)',mm(d.indoor));row('Ulkolämpötila (min … max · keskiarvo)',mm(d.outdoor));
  row('Kokonaishinta (keskiarvo)',d.price&&d.price.avg!=null?historyPriceFmt(d.price.avg):'—');
  row('Ohjausehdotuksen aikana automaattiohjaus päällä',hsT('{n} jaksoa',{n:d.armed_slots||0}));
  const list=$('hs-events');
  if(!d.events.length){const li=document.createElement('li');li.className='info';li.textContent='Ei tapahtumia tällä aikavälillä.';list.append(li);}
  for(const e of d.events){
    const li=document.createElement('li');li.className=hsEventClass(e);
    const t=document.createElement('time');t.textContent=dtf.format(new Date(e.t*1000));
    const span=document.createElement('span');span.textContent=hsEventText(e,hsDuration);
    li.append(t,span);list.append(li);
  }
}
document.querySelectorAll('[data-hrange]').forEach(b=>b.addEventListener('click',()=>{state.histRange=b.dataset.hrange;reloadHistoryView();}));
$('hs-csv').addEventListener('click',()=>{
  const d=hsValidate(state.hist);if(!d)return;
  const blob=new Blob([hsCsv(d)],{type:'text/csv'});
  const a=document.createElement('a');a.href=URL.createObjectURL(blob);a.download='enevina-history-'+state.histRange+'.csv';
  document.body.append(a);a.click();a.remove();setTimeout(()=>URL.revokeObjectURL(a.href),2000);
});

async function reloadFreshness(){
  try{state.fresh=await fetchJSON('/api/freshness');}catch(e){state.fresh=null;}
  showFreshness();
}
function showFreshness(){
  const f=frValidate(state.fresh);
  const list=$('fresh-list');list.replaceChildren();
  const callout=$('fresh-overall');
  if(!f){callout.className='callout';callout.textContent='Tietojen tuoreutta ei voitu lukea laitteelta.';text('fresh-contact','—');return;}
  const fmtc={time:u=>time(u),age:sec=>hsDuration(sec),
    error:code=>(typeof WZ_PRICE_ERRORS!=='undefined'&&WZ_PRICE_ERRORS[code])||'Hintojen haku epäonnistui.'};
  const o=frOverall(f,fmtc);
  callout.className='callout '+o.kind;callout.replaceChildren();
  const h=document.createElement('strong');h.textContent=o.headline;callout.append(h,document.createElement('br'),document.createTextNode(o.detail));
  text('fresh-contact',f.last_contact_utc>0?'Viimeisin onnistunut tiedonhaku: '+time(f.last_contact_utc):'Ei vielä onnistunutta tiedonhakua');
  for(const id of ['prices','weather','sensor','pump']){
    const t=frSourceText(frSource(f,id),fmtc);
    const li=document.createElement('li');
    const dot=document.createElement('i');dot.className='fr-dot '+t.dot;
    const box=document.createElement('span');
    const b=document.createElement('strong');b.textContent=t.title;
    box.append(b,document.createElement('br'),document.createTextNode(t.detail));
    li.append(dot,box);list.append(li);
  }
}
async function reloadPlan(){try{state.plan=await fetchJSON('/api/plan');showPlan();}catch(e){state.plan={valid:false};showPlan();}}
async function reloadWeather(){try{state.weather=await fetchJSON('/api/weather');showWeather();}catch(e){state.weather={valid:false,slots:[]};showWeather();}}
async function reloadHistory(){try{state.history=await fetchJSON('/api/air/history');showHistory();}catch(e){state.history={hours:[]};showHistory();}}
async function requestControlPermission(enabled,token,basis){
  if(!token)throw Error(translateFi('Laite ei vastaa tai vahvistustunniste puuttuu.'));
  const body=new URLSearchParams({token,enabled:String(enabled)});
  if(basis)body.set('connection',basis);
  const r=await fetch('/api/control/arm',{method:'POST',
    headers:{'Content-Type':'application/x-www-form-urlencoded'},body});
  const j=await r.json().catch(()=>({}));
  if(!r.ok||j.saved!==true)throw Error(translateFi(j.error==='MIDEA_NOT_READY'?
    'Midean automaattiohjaus edellyttää hyväksyttyä mallikohtaista ohjauskoetta ja tuoretta tilanlukua.':
    j.error==='GREE_NOT_READY'?
      'Green automaattiohjaus tarvitsee mallikohtaisen hyväksynnän ja tuoreen tilanluvun.':
    j.error==='WF_RAC_NOT_READY'?
       'WF-RAC-ohjaus tarvitsee tuoreen tilan ja mallikohtaisen hyväksynnän.':
    j.error==='DAIKIN_NOT_READY'?
       'Daikinin automaattiohjaus edellyttää mallikohtaista hyväksyntää ja tuoretta tilanlukua.':
    j.error==='CONNECTION_CHANGED'?
      'Pumpun yhteysasetukset ovat muuttuneet. Tarkista yhteys ja valitse ohjaustapa uudelleen.':
      'Asetuksen tallennus epäonnistui'));
}
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
    await requestControlPermission(enabled,a.config_token);
    text('control-feedback',enabled?
      'Automaattiohjaus sallittu. Ensimmäinen ohjaus odottaa tuoreita tietoja ja Ylläpitolämpö- tai Asuttu-tilaa.':
      'Automaattiohjaus poistettu käytöstä.');
    await reloadStatus();
    if(state.site&&state.site.pump_adapter==='midea_lan')await reloadMidea();
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
async function reloadSite(){try{
 const s=await fetchJSON('/api/settings');
 const previousMarket=tlMarketKey(state.site&&state.site.region);
 const nextMarket=tlMarketKey(s.region);
 const marketChanged=previousMarket!==nextMarket;
 state.site=s;const t=s.tariff;
 if(marketChanged){
   ++timelineRequestSequence; // invalidate any in-flight old-market response
   state.timeline=null;state.prices={valid:false,slots:[]};
   showTimeline();showPrices();
 }
 if(s.region)setRegionDisplay(s.region.timezone,s.region.currency);
 if(marketChanged){await reloadPrices();await reloadTimeline();}
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
 $('gree-card').hidden=s.pump_adapter!=='gree_ewpe_lan';
 $('daikin-card').hidden=s.pump_adapter!=='daikin_local_api';
 $('daikin-dsiot-card').hidden=s.pump_adapter!=='daikin_dsiot';
 $('wf-rac-card').hidden=s.pump_adapter!=='mhi_wf_rac_lan';
 $('control-enable').disabled=!state.status||!!state.status.control_armed||
    (s.pump_adapter==='daikin_local_api' && daikinStatus?.pilot_approved!==true)||
    (s.pump_adapter==='mhi_wf_rac_lan' && wfRacStatus?.pilot_approved!==true) ||
    s.pump_adapter==='daikin_dsiot';
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
const mideaOptionalNames=['eco','turbo','sleep','freeze_protection','display','vertical_angle','horizontal_angle','rate_select','anion','breezeless','indirect_wind','self_clean','sound'];
const mideaPresetNames=['eco','turbo','sleep','freeze_protection'];
const mideaNumericNames=new Set(['vertical_angle','horizontal_angle','rate_select']);
const mideaPropertyNames=new Set(['display','vertical_angle','horizontal_angle','rate_select','breezeless','indirect_wind','self_clean','sound']);
const mideaControlNames=['power','mode','target_c','fan','swing',...mideaOptionalNames];
const mideaInput=name=>$('midea-command-'+(name==='target_c'?'temperature':name));
let mideaStatus=null,mideaSubmitBusy=false,mideaRequestSequence=0;
function mideaOptions(name,values,labels={}){
 const select=mideaInput(name),previous=select.value;
 select.replaceChildren();
 for(const value of ['',...values.map(String)]){
  const option=document.createElement('option');option.value=value;
  option.textContent=translateFi(value===''?'Säilytä nykyinen':labels[value]||value);
  select.appendChild(option);
 }
 select.value=values.some(value=>String(value)===previous)?previous:'';
}
function mideaTemperatureValid(value){
 const input=mideaInput('target_c'),number=Number(value),min=Number(input.min),max=Number(input.max),step=Number(input.step);
 return value!=='' && Number.isFinite(number) && number>=min && number<=max && step>0 &&
  Math.abs((number-min)/step-Math.round((number-min)/step))<0.00001;
}
function updateMideaTemperature(clearInvalid){
 const m=mideaStatus,input=mideaInput('target_c'),caps=m&&m.capabilities;
 const mode=mideaInput('mode').value||String(m&&m.mode);
 const range=caps&&caps.temp_limits&&caps.temp_limits[mode];
 if(!range || !Number.isFinite(range.min_c) || !Number.isFinite(range.max_c) || !(caps.temperature_step>0)){
  input.disabled=true;if(clearInvalid)input.value='';return;
 }
 const approved=m.pilot_approved && Number.isFinite(m.pilot_min_c) && Number.isFinite(m.pilot_max_c);
 const minimum=approved?Math.max(range.min_c,m.pilot_min_c):range.min_c;
 const maximum=approved?Math.min(range.max_c,m.pilot_max_c):range.max_c;
 input.min=String(Math.ceil(minimum/caps.temperature_step-0.00001)*caps.temperature_step);
 input.max=String(Math.floor(maximum/caps.temperature_step+0.00001)*caps.temperature_step);
 input.step=String(caps.temperature_step);
 input.disabled=Number(input.min)>Number(input.max)||!caps.controls.includes('target_c');
 if(clearInvalid && input.value!=='' && (input.disabled||!mideaTemperatureValid(input.value)))input.value='';
 if(!input.disabled && input.value==='' && mideaInput('mode').value!=='' &&
    Number.isFinite(m.target_c) && !mideaTemperatureValid(String(m.target_c))){
  const nearest=Math.round(m.target_c/caps.temperature_step)*caps.temperature_step;
  input.value=String(Math.min(Number(input.max),Math.max(Number(input.min),nearest)));
 }
}
function mideaDryExitNeedsFan(mode){
 const m=mideaStatus,fans=m&&m.capabilities&&m.capabilities.fans;
 return !!(m&&m.mode===3&&mode!==undefined&&mode!==''&&Number(mode)!==3&&
  (!Array.isArray(fans)||!fans.includes(102)));
}
function updateMideaFan(){
 if(!mideaDryExitNeedsFan(mideaInput('mode').value))return;
 const m=mideaStatus,input=mideaInput('fan');
 const fans=Array.isArray(m.capabilities.fans)?m.capabilities.fans.filter(value=>Number.isInteger(value)&&value>=0&&value<=102):[];
 if(fans.some(value=>String(value)===input.value))return;
 const value=fans.includes(m.fan)?m.fan:fans[0];
 input.value=value===undefined?'':String(value);
}
function renderMideaControls(){
 const m=mideaStatus,caps=m&&m.capabilities;
 const controls=new Set(caps&&Array.isArray(caps.controls)?caps.controls:[]);
 const numbers=values=>Array.isArray(values)?values.filter(value=>Number.isInteger(value)&&value>=0&&value<=255):[];
 mideaOptions('power',controls.has('power')?['true','false']:[],{true:'Päälle',false:'Pois'});
 mideaOptions('mode',numbers(caps&&caps.modes),{1:'Auto',2:'Jäähdytys',3:'Kuivaus',4:'Lämmitys',5:'Puhallus'});
 mideaOptions('fan',numbers(caps&&caps.fans),{20:'Hiljainen',40:'Matala',60:'Keskitaso',80:'Korkea',100:'Täysi',102:'Auto'});
 mideaOptions('swing',numbers(caps&&caps.swings),{0:'Pois',3:'Vaaka',12:'Pysty',15:'Molemmat'});
 for(const name of mideaControlNames){
  const available=controls.has(name);
  $('midea-field-'+name).hidden=!available;mideaInput(name).disabled=!available;
  if(!available)mideaInput(name).value='';
 }
 const known=new Set(m&&Array.isArray(m.state_known)?m.state_known:[]);
 for(const name of mideaOptionalNames){
  const available=controls.has(name),numeric=mideaNumericNames.has(name);
  const choices=numeric?numbers(caps&&caps.property_options&&caps.property_options[name]):['true','false'];
  mideaOptions(name,available?choices:[],numeric?{}:{true:'Päälle',false:'Pois'});
  const value=m&&m.fresh&&known.has(name)?m[name]:null;
  text('midea-current-'+name,value==null?'—':numeric?String(value):value?'Päällä':'Pois');
 }
 $('midea-properties').hidden=!['display','vertical_angle','horizontal_angle','rate_select','anion','breezeless','indirect_wind','self_clean','sound'].some(name=>controls.has(name));
 text('midea-capability-summary',!m||!m.fresh?'Odotetaan laitteen ominaisuuksia.':caps&&caps.reported?
  'Laitteen ilmoittamat toiminnot ovat käytössä.':
  'Kaikkia ominaisuuksia ei ilmoitettu. Käytössä ovat perustoiminnot ja laitteelta luetut lisätoiminnot.');
 updateMideaTemperature(true);
 updateMideaFan();
 updateMideaControlAvailability();
}
function mideaDraft(){
 const draft={};
 const controls=new Set(mideaStatus&&mideaStatus.capabilities&&Array.isArray(mideaStatus.capabilities.controls)?mideaStatus.capabilities.controls:[]);
 for(const name of mideaControlNames){
  const value=mideaInput(name).value.trim();
  if(value!==''&&controls.has(name))draft[name]=value;
 }
 return draft;
}
function updateMideaControlAvailability(){
 const m=mideaStatus,draft=mideaDraft(),names=Object.keys(draft),policy=m&&m.control_policy||{};
 let reason='';
 const ready=m&&m.selected&&m.configured&&m.fresh&&m.pilot_approved&&!m.control_busy&&!m.control_pending&&!mideaSubmitBusy&&!!(state.status&&state.status.air&&state.status.air.config_token);
 const property=names.some(name=>mideaPropertyNames.has(name)),general=names.some(name=>!mideaPropertyNames.has(name));
 if(m&&m.automatic_enabled)reason='Poista automaattiohjaus käytöstä ennen käsiohjausta.';
 else if(!ready)reason=!m||!m.fresh?'Odota ensin onnistunutta tilanlukua.':!m.pilot_approved?'Mallikohtaista ohjaustestiä ei ole hyväksytty.':'Ohjaus on jo jonossa tai käynnissä.';
 else if(!names.length)reason='Valitse ensin vähintään yksi muutettava asetus.';
 else if(property&&general)reason='Lähetä perusasetukset ja lisätoiminnot erillisinä komentoina.';
 else if(mideaPresetNames.filter(name=>draft[name]==='true').length>1)reason='Valitse vain yksi lisätila kerrallaan: Eco, Turbo, yötila tai 8 °C ylläpitolämmitys.';
 else if(draft.target_c!==undefined&&!mideaTemperatureValid(draft.target_c))reason='Pyyntilämpötila ei vastaa laitteen rajoja tai askelkokoa.';
 else if(draft.mode!==undefined&&draft.target_c===undefined&&!mideaTemperatureValid(String(m.target_c)))reason='Pyyntilämpötila ei vastaa laitteen rajoja tai askelkokoa.';
 else if(mideaDryExitNeedsFan(draft.mode)&&(draft.fan===undefined||!Array.isArray(m.capabilities.fans)||!m.capabilities.fans.some(value=>String(value)===draft.fan)))reason='Valitse tuettu puhallusnopeus kuivaustilasta poistumista varten.';
 else if(property&&(!policy.property_available||Array.isArray(policy.property_controls)&&names.some(name=>!policy.property_controls.includes(name))))reason='Valitun lisätoiminnon ohjaustapaa ei ole vahvistettu laitteen tilasta.';
 else if(general&&!policy.general_available){
  if(policy.requires_freeze_choice && draft.freeze_protection===undefined)reason='8 °C ylläpitolämmityksen (jäätymissuoja) tila puuttuu. Valitse sen tila ennen perusasetuksen lähettämistä.';
  else if(!policy.requires_freeze_choice&&policy.general_block==='missing_frost_state')reason='8 °C ylläpitolämmityksen (jäätymissuoja) tila puuttuu, eikä laite ilmoita tämän asetuksen ohjaustapaa. Perusasetukset odottavat täydentävää tilanlukua.';
  else if(!policy.requires_freeze_choice)reason='Laitteen luettu tila ei vielä riitä perusasetusten turvalliseen lähettämiseen.';
 }
 $('midea-command-send').disabled=!!reason;
 text('midea-control-availability',reason||'Valitut asetukset tarkistetaan vielä tuoreesta tilasta ennen lähetystä.');
 return !reason;
}
for(const name of mideaControlNames){
 const input=mideaInput(name);
 input.addEventListener(name==='target_c'?'input':'change',()=>{
  if(mideaPresetNames.includes(name)&&input.value==='true')
   for(const other of mideaPresetNames)if(other!==name&&mideaInput(other).value==='true')mideaInput(other).value='';
  if(name==='mode'){updateMideaTemperature(true);updateMideaFan();}
  updateMideaControlAvailability();
 });
}
async function reloadMidea(){const request=++mideaRequestSequence;try{const m=await fetchJSON('/api/midea/status');
 if(request!==mideaRequestSequence)return;
 mideaStatus=m;
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
 renderMideaControls();
 text('midea-pilot-feedback',m.pilot_approved?
  'Mallikohtainen ohjaustesti hyväksytty. Pyyntirajat: '+m.pilot_min_c+'…'+m.pilot_max_c+' °C.':
  'Mallikohtaista ohjaustestiä ei ole hyväksytty.');
 const status=m.control_pending?'Ohjaus odottaa suorittamista.':m.control_busy?'Ohjausta suoritetaan.':mideaControlErrors[m.control_code]||mideaReadErrors[m.control_code]||'Ohjausvirhe: '+(m.control_code||'tuntematon');
 const verified=m.control_last_confirmed_utc?' Vahvistettu '+time(m.control_last_confirmed_utc):'';
 text('midea-control-feedback',status+verified);
 if(m.selected) {
  const value=c=>c&&c.power===false?translateFi('Pois päältä'):
    c&&Number.isFinite(c.target_c)?fmt(c.target_c)+' °C':'—';
  const req=value(m.control_requested);
  const sent=m.control_last_sent_utc>0?req+' '+time(m.control_last_sent_utc):'—';
  text('control-last',translateFi('Pyydetty')+' '+req+' · '+translateFi('Lähetetty')+' '+sent+
    ' · '+translateFi('Viimeksi vahvistettu')+' '+value(m.control_confirmed));
 }
 }catch(e){if(request!==mideaRequestSequence)return;mideaStatus=null;updateMideaControlAvailability();text('midea-state','Yhteys poikki');['midea-read','midea-mode','midea-target','midea-indoor','midea-outdoor',...mideaOptionalNames.map(name=>'midea-current-'+name)].forEach(id=>text(id,'—'));}}
$('midea-test').addEventListener('click',async()=> {
 const token=state.status&&state.status.air&&state.status.air.config_token;
 if(!token){text('midea-feedback','Laite ei vastaa.');return;}
 $('midea-test').disabled=true;
 try{
   if(wzMideaStep() && (!state.site||state.site.pump_adapter!=='midea_lan')){
     const status=await fetchJSON('/api/midea/status');
     if(!status.configured)throw Error(WT('Tallenna Midea-yhteys ja odota tuoretta tilanlukua ennen jatkamista.'));
     if(!wzMideaStep())return;
     await wzPost({pump_adapter:'midea_lan'});
   }
   const r=await fetch('/api/midea/test',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams({token})});
   const j=await r.json().catch(()=>({}));
   text('midea-feedback',j.queued?'Yhteystesti käynnistetty. Pumppua ei ohjata.':'Yhteystestiä ei voitu käynnistää.');
   await reloadMidea();
 }catch(e){text('midea-feedback',e.message||'Laite ei vastannut.');}
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
  const id=$('midea-pilot-id').value.trim()||((WZ.open&&WZ.step===7)?'':$('midea-id').value.trim());
  if(!model || !id || !min || !max){text('midea-pilot-feedback','Tarkista malli, device ID ja mallikohtaiset lämpötilarajat.');return;}
  if(!confirm(translateFi('Hyväksytkö mallikohtaisen ohjauskokeen tälle Midea-pumpulle?')+' '+model+' · '+id))return;
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
 if(!updateMideaControlAvailability())return;
 const draft=mideaDraft();
 const body=new URLSearchParams({token});
 for(const [name,value] of Object.entries(draft))body.set(name,value);
 mideaSubmitBusy=true;
 $('midea-command-send').disabled=true;
 text('midea-control-feedback','Lähetetään manuaalinen Midea-ohjaus…');
 try{
  const r=await fetch('/api/midea/control',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body});
  const j=await r.json().catch(()=>({}));
  if(!r.ok || !j.queued){
   const errors={MISSING_FROST_STATE:'8 °C ylläpitolämmityksen (jäätymissuoja) tila puuttuu. Valitse sen tila ennen perusasetuksen lähettämistä.',MIXED_COMMAND_FAMILIES:'Lähetä perusasetukset ja lisätoiminnot erillisinä komentoina.',UNSUPPORTED_CONTROL:'Laite ei tue valittua asetusta tai arvoa.',UNSUPPORTED_STATE:'Laitteen luettu tila ei vielä riitä perusasetusten turvalliseen lähettämiseen.'};
   throw Error(errors[j.error]||j.error||String(r.status));
  }
  text('midea-control-feedback','Ohjaus jonossa. Pumpun vastaus ja takaisinluku tarkistetaan.');
  for(const [name,value] of Object.entries(draft))if(mideaInput(name).value===value)mideaInput(name).value='';
  await reloadMidea();
 }catch(e){text('midea-control-feedback','Ohjausta ei voitu käynnistää: '+(e.message||'yhteysvirhe'));}
 finally{mideaSubmitBusy=false;updateMideaControlAvailability();}
});
let mideaDiscoveryResults=[];
for(const id of ['midea-ip','midea-id','midea-token','midea-key','midea-protocol']){
 $(id).addEventListener('input',()=>{if(wzMideaStep())WZ.mideaChanged=true;});
}
function mideaDiscoveryType(device){
 return device && device.device_type_known===true && Number.isInteger(device.device_type) &&
   device.device_type>=0 && device.device_type<=255?device.device_type:null;
}
function mideaDiscoveryUnsupported(device){
 const type=mideaDiscoveryType(device);
 return type!==null && type!==0xAC;
}
const mideaUnsupportedTypeMessage='Laitteen tyyppiä ei tueta tällä Midea-ohjaimella. Katso tuetut mallit malliluettelosta.';
function selectDiscoveredMidea(device){
 if(!device || !/^(2|3)$/.test(String(device.protocol)) ||
    !/^[0-9]+$/.test(String(device.device_id)))return;
 if(mideaDiscoveryUnsupported(device)){text('midea-feedback',mideaUnsupportedTypeMessage);return;}
 // A discovery reply is untrusted; filling fields never saves, arms or sends
 // any physical command. The owner must explicitly provide V3 credentials.
 if(wzMideaStep())WZ.mideaChanged=true;
 $('midea-ip').value=String(device.ip||'');
 $('midea-id').value=String(device.device_id);
 $('midea-protocol').value=String(device.protocol);
 $('midea-protocol').dataset.dirty='1';
 showMideaProtocolFields();
 $('midea-token').value='';
 $('midea-key').value='';
 text('midea-feedback',device.protocol===3?
   'Midea V3 löytyi. Lisää sen jo olemassa olevat V3 token ja key, sitten tallenna yhteys.':
   'Midea V2 löytyi. Tarkista laite ja tallenna yhteys.');
}
$('midea-discover-select').addEventListener('change',()=>{
 const selected=$('midea-discover-select').value;
 if(selected==='')return;
 const i=Number(selected);
 if(Number.isInteger(i) && i>=0 && i<mideaDiscoveryResults.length)
   selectDiscoveredMidea(mideaDiscoveryResults[i]);
});
$('midea-discover').addEventListener('click',async()=>{
 const token=state.status&&state.status.air&&state.status.air.config_token;
 if(!token){text('midea-feedback','Laite ei vastaa.');return;}
 const button=$('midea-discover'),select=$('midea-discover-select');
 button.disabled=true;
 text('midea-feedback','Haetaan Midea-laitteita omasta lähiverkosta...');
 try {
  const r=await fetch('/api/midea/discover',{
    method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},
    body:new URLSearchParams({token})});
  const data=await r.json().catch(()=>({}));
  if(!r.ok || !Array.isArray(data.devices))
    throw Error(data.error||'HTTP '+r.status);
  const entries=data.devices.filter(d=>d && [2,3].includes(d.protocol) &&
    /^[0-9]+$/.test(String(d.device_id)) &&
    /^(?:\d{1,3}\.){3}\d{1,3}$/.test(String(d.ip)));
  mideaDiscoveryResults=entries;
  select.replaceChildren();
  select.hidden=entries.length<=1 && !(entries.length===1 && mideaDiscoveryType(entries[0])!==null);
  if(!entries.length){text('midea-feedback','Mideaa ei löytynyt lähiverkosta.');return;}
  if(entries.length>1){
    const placeholder=document.createElement('option');
    placeholder.value='';
    placeholder.textContent='Valitse löydetty Midea...';
    placeholder.disabled=true;
    placeholder.selected=true;
    select.appendChild(placeholder);
  }
  entries.forEach((d,i)=>{
    const option=document.createElement('option');
    option.value=String(i);
    const type=mideaDiscoveryType(d);
    const model=d.model_known===true && /^[A-Za-z0-9]{8}$/.test(d.model)?' · '+d.model:'';
    const subtype=d.subtype_known===true && Number.isInteger(d.subtype) && d.subtype>=0 && d.subtype<=65535?
      ' · '+d.subtype:'';
    option.disabled=mideaDiscoveryUnsupported(d);
    option.textContent='Midea V'+d.protocol+(type===null?'':' · 0x'+type.toString(16).toUpperCase())+
      model+subtype+' · '+d.ip+' · ID '+d.device_id+(option.disabled?' · Ei tuettu':'');
    select.appendChild(option);
  });
  if(entries.every(mideaDiscoveryUnsupported))text('midea-feedback',mideaUnsupportedTypeMessage);
  else if(entries.length===1)selectDiscoveredMidea(entries[0]);
  else text('midea-feedback','Löytyi '+entries.length+' laitetta. Valitse oikea Midea luettelosta. Tunnukset eivät siirry haussa.');
 }catch(e){text('midea-feedback','Lähiverkkohaku epäonnistui: '+(e.message||'yhteysvirhe'));}
 finally{button.disabled=false;}
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
    WZ.mideaChanged=false;
    delete $('midea-protocol').dataset.dirty;
    $('midea-token').value='';$('midea-key').value='';
    if(wzMideaStep() && (!state.site||state.site.pump_adapter!=='midea_lan')){
      try{await wzPost({pump_adapter:'midea_lan'});}
      catch(_){text('midea-feedback',WT('Midea-yhteys on tallennettu, mutta lämpöpumpun valintaa ei voitu tallentaa.'));return;}
    }
    await reloadMidea();
  }}
 catch(e){text('midea-feedback','Laite ei vastannut.');}
 finally {$('midea-save').disabled=false;}
});
let greeStatus=null,greeDraft=false,greeBusy=false,greeSequence=0,greeDiscovered=[];
const greeTexts={
  paired:'Gree-yhteys toimii.',ok:'Gree-yhteys toimii.',scan_ok:'Valitse löydetty Gree…',
  no_devices:'Gree-laitteita ei löytynyt.',
  bind_failed:'Gree-laitteen sidonta epäonnistui. Tarkista osoite, laitetunniste ja salausversio.',
  invalid_device:'Gree-yhteyden tiedot eivät kelpaa tässä kotiverkossa.',
  read_failed:'Gree-pumpulta ei saatu tuoretta tilaa.',
  store_failed:'Gree-yhteyden tallennus epäonnistui.',
  pilot_required:'Green automaattiohjaus tarvitsee mallikohtaisen hyväksynnän ja tuoreen tilanluvun.',
  model_limits:'Tarkista malli, device ID ja mallikohtaiset lämpötilarajat.',
  write_queued:'Gree-ohjaus on jonossa.',
  already_applied:'Valittu asetus on jo käytössä.',
  confirmed:'Gree-pumppu vahvisti komennon.',
  unconfirmed:'Gree-pumppu ei vahvistanut komentoa.',
  external_change:'Gree-pumpun asetus muuttui. Ohjaus arvioidaan uudelleen.',
  send_failed:'Gree-komennon lähetys epäonnistui.',
  mode_dwell:'Ohjaus odottaa lämmitystilan vähimmäiskestoa, eikä lähetä uutta komentoa.'
};
function showGree(s){
  const result=!s.fresh&&(s.result==='ok'||s.result==='paired')?'':s.result;
  text('gree-status',greeTexts[result]||(s.fresh?'Gree-yhteys toimii.':'Gree-yhteys odottaa lukutestiä.'));
  text('gree-mode',s.fresh?(s.power?(s.mode===4?'Lämmitys':'Tila '+s.mode):'Pois päältä'):'—');
  text('gree-target',s.fresh&&Number.isFinite(s.target_c)?fmt(s.target_c,0)+' °C':'—');
  text('gree-temperature',s.fresh&&Number.isFinite(s.indoor_c)?fmt(s.indoor_c,0)+' °C':'—');
  if(!greeDraft&&s.configured){
    $('gree-ip').value=s.host;
    $('gree-id').value=s.device;
    $('gree-cipher').value=String(s.cipher);
  }
  if(state.site?.pump_adapter==='gree_ewpe_lan'){
    const command=p=>!p?'—':p.power===false?translateFi('Pois päältä'):
      Number.isFinite(p.target_c)?fmt(p.target_c,0)+' °C':'—';
    text('control-last',translateFi('Pyydetty')+' '+command(s.requested)+' · '+
      translateFi('Lähetetty')+' '+command(s.sent)+' · '+
      translateFi('Vahvistettu pumpulta')+' '+command(s.confirmed));
  }
  text('gree-pilot-status',s.pilot_approved?'Mallin manuaaliohjaus hyväksytty.':
    'Mallikohtaista ohjaustestiä ei ole hyväksytty.');
  const target=$('gree-command-target');
  target.min=s.minimum_c??16;target.max=s.maximum_c??30;
  const writesDisabled=s.write_available!==true||s.busy===true;
  $('gree-setpoint').disabled=writesDisabled || (s.power!==false && s.mode!==4);
  $('gree-heat').disabled=writesDisabled;
  $('gree-off').disabled=writesDisabled;
  const list=Array.isArray(s.discovered)?s.discovered:[];
  if(JSON.stringify(list)!==JSON.stringify(greeDiscovered)){
    greeDiscovered=list;
    const menu=$('gree-discovered');
    const blank=document.createElement('option');
    blank.value='';blank.textContent=translateFi('Valitse löydetty Gree…');
    menu.replaceChildren(blank);
    list.forEach((device,index)=>{
      const opt=document.createElement('option');
      opt.value=String(index);
      opt.textContent=device.host+' · '+device.device+' · V'+device.cipher;
      menu.append(opt);
    });
    menu.hidden=!list.length;
  }
}
function greeDraftView(){
  return WZ.open&&WZ.step===4&&WZ.adapter==='gree_ewpe_lan';
}
function greePanelActive(){
  return state.site?.pump_adapter==='gree_ewpe_lan'||greeDraftView();
}
async function reloadGree(){
  if(!greePanelActive()){greeStatus=null;return null;}
  const seq=++greeSequence;
  try {
    const draft=greeDraftView();
    const s=await fetchJSON('/api/gree/status'+(draft?'?draft=true':''));
    if(seq!==greeSequence||!greePanelActive()||draft!==greeDraftView())return null;
    greeStatus=s;showGree(s);return s;
  } catch(e) {
    if(seq===greeSequence){greeStatus=null;text('gree-status','Gree-pumpulta ei saatu tuoretta tilaa.');}
    return null;
  }
}
for(const id of ['gree-ip','gree-id','gree-cipher'])
  $(id).addEventListener('input',()=>{greeDraft=true;});
$('gree-discovered').addEventListener('change',()=>{
  const index=$('gree-discovered').value;
  if(index==='')return;
  const device=greeDiscovered[Number(index)];
  if(!device)return;
  $('gree-ip').value=device.host;$('gree-id').value=device.device;
  $('gree-cipher').value=String(device.cipher);greeDraft=true;
});
async function greeRequest(action,fields){
  const token=state.status?.air?.config_token;
  if(!token)throw Error('authorization');
  const r=await fetch('/api/gree/'+action,{method:'POST',
    headers:{'Content-Type':'application/x-www-form-urlencoded'},
    body:new URLSearchParams({...fields,token})});
  const j=await r.json().catch(()=>({}));
  if(!r.ok)throw Error(j.error||'read_failed');
  return j;
}
async function greeJob(action,fieldsFromCommand={}){
  if(greeBusy)return;
  greeBusy=true;
  if(action==='pair')greeDraft=true;
  const draft=greeDraftView();
  const fields=['gree-ip','gree-id','gree-cipher','gree-discovered','gree-scan','gree-pair','gree-test'];
  fields.forEach(id=>{$(id).disabled=true;});
  try {
    const params=action==='pair'?{host:$('gree-ip').value.trim(),device:$('gree-id').value.trim(),cipher:$('gree-cipher').value}:fieldsFromCommand;
    const reply=await greeRequest(action,{...params,draft:String(draft)});
    if(reply.queued!==true)throw Error(reply.error||'read_failed');
    for(let i=0;i<15;i++){
      await new Promise(resolve=>setTimeout(resolve,600));
      const s=await reloadGree();
      if(!s||s.busy)continue;
      if(action==='pair'&&s.result==='paired'&&s.configured&&s.fresh){greeDraft=false;showGree(s);}
      return;
    }
    text('gree-status','Gree-yhteys odottaa lukutestiä.');
  }catch(e){text('gree-status',greeTexts[e.message]||'Gree-yhteys odottaa lukutestiä.');}
  finally{greeBusy=false;fields.forEach(id=>{$(id).disabled=false;});}
}
async function greeCommit(id){
  try{
    const j=await greeRequest('commit',{draft_id:String(id)});
    if(j.saved!==true)throw Error(j.error);
  }catch(e){throw Error(translateFi(greeTexts[e.message]||'Gree-yhteyden tallennus epäonnistui.'));}
}
async function greePilot(enabled){
  const device=greeStatus?.device;
  const model=$('gree-model').value.trim();
  if(enabled && (!device||!model||
      !confirm(translateFi('Hyväksytkö mallikohtaisen ohjauskokeen tälle Gree-pumpulle?')+' '+model+' · '+device)))return;
  try{
    await greeRequest('pilot',{enabled:String(enabled),device:device||'',model,
      min:$('gree-min').value,max:$('gree-max').value});
    await reloadGree();await reloadStatus();
  }catch(e){text('gree-pilot-status',greeTexts[e.message]||'Gree-yhteyden tallennus epäonnistui.');}
}
for(const [button,action] of [['gree-setpoint','target'],['gree-heat','heat'],['gree-off','off']])
  $(button).addEventListener('click',()=>greeJob('control',{action,
    target:$('gree-command-target').value,revision:String(greeStatus?.revision??'')}));
$('gree-approve').addEventListener('click',()=>greePilot(true));
$('gree-revoke').addEventListener('click',()=>greePilot(false));
$('gree-scan').addEventListener('click',()=>greeJob('discover'));
$('gree-pair').addEventListener('click',()=>greeJob('pair'));
$('gree-test').addEventListener('click',()=>greeJob('test'));
let daikinStatus=null,daikinDirty=false,daikinBusy=false,daikinRequestSequence=0;
const daikinMessages={unsupported:'Moduuli ei palauttanut vanhan Daikin-rajapinnan tilaa.',
  no_legacy_control:'Moduuli ei palauttanut vanhan Daikin-rajapinnan tilaa.',
  device_changed:'Daikin-moduulin tunniste muuttui. Yhdistä oikea laite uudelleen.',
  storage:'Daikin-yhteyden tallennus epäonnistui.',
  invalid_address:'Daikin-moduulin osoite ei kelpaa tässä kotiverkossa.',
  connection_failed:'Daikin-yhteys ei vastaa.',
  pilot_required:'Daikin-ohjaus odottaa tuoretta tilanlukua ja mallikohtaista hyväksyntää.',
  model_limits:'Daikinin malli, rajat tai lämpötila-askel puuttuvat.',
  mode_dwell:greeTexts.mode_dwell,already_applied:greeTexts.already_applied,
  write_queued:'Daikin-ohjaus on jonossa.',confirmed:'Daikin-pumppu vahvisti komennon.',
  unconfirmed:'Daikin-pumppu ei vahvistanut komentoa.',
  external_change:'Daikinin asetus muuttui. Ohjaus arvioidaan uudelleen.',
  send_failed:'Daikin ei hyväksynyt komentoa tai yhteys katkesi.',
  unsupported_command:'Komento ei sovi Daikinin nykyiseen tilaan tai mallin rajoihin.',
  write_cancelled:'Daikin-komento peruttiin yhteyden tai ohjausasetusten muuttuessa.'};
function daikinDraftView(){return WZ.open&&WZ.step===4&&WZ.adapter==='daikin_local_api';}
function daikinPanelActive(){return daikinDraftView()||state.site?.pump_adapter==='daikin_local_api';}
function showDaikin(s){
  text('daikin-feedback',daikinMessages[s.result]||
    (s.fresh?'Daikin-yhteys toimii.':'Daikin-yhteys odottaa lukutestiä.'));
  if(!daikinDirty&&s.configured)$('daikin-ip').value=s.host;
  text('daikin-mode',s.fresh?(s.power?(s.mode===4?'Lämmitys':'Tila '+s.mode):'Pois päältä'):'—');
  text('daikin-target',s.fresh&&Number.isFinite(s.target_c)?fmt(s.target_c,1)+' °C':'—');
  text('daikin-temperature',s.fresh&&Number.isFinite(s.indoor_c)?fmt(s.indoor_c,1)+' °C':'—');
  if(daikinDraftView())return;
  const approved=s.pilot_approved===true;
  text('daikin-pilot-status',approved?'Daikin-mallin ohjauskoe on hyväksytty.':
    'Daikin-mallin ohjauskoe odottaa hyväksyntää.');
  if(!$('daikin-pilot-model').value&&s.model)$('daikin-pilot-model').value=s.model;
  $('daikin-approve').disabled=!s.fresh||s.busy;
  $('daikin-revoke').disabled=!approved||s.busy;
  $('control-enable').disabled=!approved||!s.fresh||!state.status?.air?.config_token||
    state.status?.control_armed===true;
  const target=$('daikin-command-target');
  target.min=s.minimum_c??10;target.max=s.maximum_c??32;target.step=s.step_c??0.5;
  if(!target.value&&s.fresh&&Number.isFinite(s.target_c))target.value=s.target_c;
  const unavailable=s.write_available!==true||s.busy;
  $('daikin-setpoint').disabled=unavailable||s.mode!==4;
  $('daikin-heat').disabled=unavailable;
  $('daikin-off').disabled=unavailable;
  const command=p=>!p?'—':p.power===false?translateFi('Pois päältä')+' '+time(p.utc):
    Number.isFinite(p.target_c)?fmt(p.target_c,1)+' °C '+time(p.utc):'—';
  text('control-last',translateFi('Pyydetty')+' '+command(s.requested)+' · '+
    translateFi('Lähetys aloitettu')+' '+command(s.sent)+' · '+
    translateFi('Vahvistettu pumpulta')+' '+command(s.confirmed));
}
async function reloadDaikin(){
  if(!daikinPanelActive()){daikinStatus=null;return null;}
  const sequence=++daikinRequestSequence;
  const draft=daikinDraftView();
  try{
    const s=await fetchJSON('/api/daikin/status'+(draft?'?draft=true':''));
    if(sequence!==daikinRequestSequence||!daikinPanelActive()||draft!==daikinDraftView())return null;
    daikinStatus=s;showDaikin(s);return s;
  }catch(e){if(sequence===daikinRequestSequence){daikinStatus=null;text('daikin-feedback','Daikin-yhteys ei vastaa.');}return null;}
}
async function localLanPost(driver,action,fields,messages,failure){
  const token=state.status?.air?.config_token;
  if(!token)throw Error(translateFi('Laite ei vastaa tai vahvistustunniste puuttuu.'));
  const response=await fetch('/api/'+driver+'/'+action,{method:'POST',
    headers:{'Content-Type':'application/x-www-form-urlencoded'},
    body:new URLSearchParams({...fields,token})});
  const value=await response.json().catch(()=>({}));
  if(!response.ok)throw Error(translateFi(messages[value.error]||failure));
  return value;
}
function daikinPost(action,fields){
  return localLanPost('daikin',action,fields,daikinMessages,'Daikin-yhteys ei vastaa.');
}
async function daikinCommit(id){
  const reply=await daikinPost('commit',{draft_id:String(id)});
  if(reply.saved!==true)throw Error(translateFi('Daikin-yhteyden tallennus epäonnistui.'));
}
async function daikinJob(action,fields={}){
  if(daikinBusy)return;
  daikinBusy=true;
  if(action==='identify')daikinDirty=true;
  const ids=['daikin-ip','daikin-pair','daikin-test'];
  ids.forEach(id=>{$(id).disabled=true;});
  try{
    const reply=await daikinPost(action,{host:$('daikin-ip').value.trim(),draft:String(daikinDraftView()),...fields});
    if(action==='pilot'&&reply.saved===true){await reloadStatus();await reloadDaikin();return;}
    if(reply.queued!==true)throw Error(translateFi('Daikin-yhteys ei vastaa.'));
    for(let i=0;i<(action==='control'?45:15);i++){
      await new Promise(resolve=>setTimeout(resolve,600));
      const s=await reloadDaikin();
      if(!s||s.busy)continue;
      if(action==='identify'&&s.result==='ok'&&s.configured&&s.fresh){daikinDirty=false;showDaikin(s);}
      return;
    }
    text('daikin-feedback','Daikin-yhteys odottaa lukutestiä.');
  }catch(e){text(action==='pilot'?'daikin-pilot-status':'daikin-feedback',e.message);}
  finally{daikinBusy=false;ids.forEach(id=>{$(id).disabled=false;});}
}
$('daikin-ip').addEventListener('input',()=>{daikinDirty=true;});
$('daikin-pair').addEventListener('click',()=>daikinJob('identify'));
$('daikin-test').addEventListener('click',()=>daikinJob('test'));
for(const [id,enabled] of [['daikin-approve',true],['daikin-revoke',false]])
  $(id).addEventListener('click',()=>daikinJob('pilot',{enabled:String(enabled),
    device:daikinStatus?.device??'',revision:String(daikinStatus?.revision??''),
    model:$('daikin-pilot-model').value.trim(),min:$('daikin-pilot-min').value,
    max:$('daikin-pilot-max').value,step:$('daikin-pilot-step').value}));
for(const [id,action] of [['daikin-setpoint','target'],['daikin-heat','heat'],['daikin-off','off']])
  $(id).addEventListener('click',()=>daikinJob('control',{action,
    revision:String(daikinStatus?.revision??''),target:$('daikin-command-target').value}));
let wfRacStatus=null,wfRacDirty=false,wfRacBusy=false,wfRacSequence=0;
const wfRacMessages={unsupported:'Moduuli ei vastannut tuetulla WF-RAC HTTP -protokollalla.',
  device_changed:'WF-RAC-moduulin tunniste muuttui.',
  invalid_address:'WF-RAC-moduulin osoite ei kelpaa tässä kotiverkossa.',
  clock_unavailable:'WF-RAC odottaa laitteen kellon synkronointia.',
  invalid_status:'WF-RAC-tilavastausta ei voitu varmistaa.',
  storage:'WF-RAC-yhteyden tallennus epäonnistui.',connection_failed:'WF-RAC-yhteys ei vastaa.',
  pilot_required:'WF-RAC-ohjaus tarvitsee tuoreen tilan ja mallikohtaisen hyväksynnän.',
  model_limits:'Tarkista WF-RAC-malli, lämpötilarajat ja paikallinen tunniste.',
  mode_dwell:greeTexts.mode_dwell,already_applied:greeTexts.already_applied,
  write_queued:'WF-RAC-ohjaus on jonossa.',confirmed:'WF-RAC-pumppu vahvisti komennon.',
  unconfirmed:'WF-RAC-pumppu ei vahvistanut komentoa.',
  external_change:'WF-RAC-asetus muuttui. Ohjaus arvioidaan uudelleen.',
  send_failed:'WF-RAC ei hyväksynyt komentoa tai yhteys katkesi.',
  unsupported_command:'Komento ei sovi WF-RAC-pumpun nykyiseen tilaan.',
  write_cancelled:'WF-RAC-komento peruttiin yhteyden tai ohjausasetusten muuttuessa.'};
function wfRacDraft(){return WZ.open&&WZ.step===4&&WZ.adapter==='mhi_wf_rac_lan';}
function wfRacActive(){return wfRacDraft()||state.site?.pump_adapter==='mhi_wf_rac_lan';}
function showWfRac(s){
  text('wf-rac-feedback',wfRacMessages[s.result]||
    (s.fresh?'WF-RAC-yhteys toimii.':'WF-RAC-yhteys odottaa lukutestiä.'));
  if(!wfRacDirty&&s.configured)$('wf-rac-address').value=s.address;
  text('wf-rac-mode',s.fresh?(s.power?(s.mode===4?'Lämmitys':Number.isInteger(s.mode)?'Tila '+s.mode:'—'):'Pois päältä'):'—');
  text('wf-rac-target',s.fresh&&Number.isFinite(s.target_c)?fmt(s.target_c,1)+' °C':'—');
  text('wf-rac-temperature',s.fresh&&Number.isFinite(s.indoor_c)?fmt(s.indoor_c,1)+' °C':'—');
  if(wfRacDraft())return;
  const approved=s.pilot_approved===true;
  text('wf-rac-pilot-result',approved?'WF-RAC-mallin ohjauskoe on hyväksytty.':
    'WF-RAC-mallin ohjauskoe odottaa hyväksyntää.');
  $('wf-rac-approve').disabled=!s.fresh||s.busy;
  $('wf-rac-revoke').disabled=!approved||s.busy;
  $('control-enable').disabled=!approved||!s.fresh||!state.status?.air?.config_token||
    state.status?.control_armed===true;
  const target=$('wf-rac-command-target');
  target.min=s.minimum_c??10;target.max=s.maximum_c??33;target.step=.5;
  if(!target.value&&s.fresh&&Number.isFinite(s.target_c))target.value=s.target_c;
  const unavailable=s.write_available!==true||s.busy;
  $('wf-rac-setpoint').disabled=unavailable||s.mode!==4;
  $('wf-rac-heat').disabled=unavailable;
  $('wf-rac-off').disabled=unavailable;
  const command=p=>!p?'—':p.power===false?translateFi('Pois päältä')+' '+time(p.utc):
    Number.isFinite(p.target_c)?fmt(p.target_c,1)+' °C '+time(p.utc):'—';
  text('control-last',translateFi('Pyydetty')+' '+command(s.requested)+' · '+
    translateFi('Lähetys aloitettu')+' '+command(s.sent)+' · '+
    translateFi('Vahvistettu pumpulta')+' '+command(s.confirmed));
}
async function reloadWfRac(){
  if(!wfRacActive()){wfRacStatus=null;return null;}
  const sequence=++wfRacSequence;
  const draft=wfRacDraft();
  try{
    const s=await fetchJSON('/api/wf-rac/status'+(draft?'?draft=true':''));
    if(sequence!==wfRacSequence||!wfRacActive()||draft!==wfRacDraft())return null;
    wfRacStatus=s;showWfRac(s);return s;
  }catch(e){if(sequence===wfRacSequence){wfRacStatus=null;text('wf-rac-feedback','WF-RAC-yhteys ei vastaa.');}return null;}
}
function wfRacPost(action,fields){
  return localLanPost('wf-rac',action,fields,wfRacMessages,'WF-RAC-yhteys ei vastaa.');
}
async function wfRacCommit(id){
  const response=await wfRacPost('commit',{draft_id:String(id)});
  if(response.saved!==true)throw Error(translateFi('WF-RAC-yhteyden tallennus epäonnistui.'));
}
async function wfRacJob(action,fields={}){
  if(wfRacBusy)return;
  wfRacBusy=true;
  if(action==='identify')wfRacDirty=true;
  const ids=['wf-rac-address','wf-rac-pair','wf-rac-test'];
  ids.forEach(id=>{$(id).disabled=true;});
  try{
    const response=await wfRacPost(action,{address:$('wf-rac-address').value.trim(),draft:String(wfRacDraft()),...fields});
    if(action==='pilot'&&response.saved===true){await reloadStatus();await reloadWfRac();return;}
    if(response.queued!==true)throw Error(translateFi('WF-RAC-yhteys ei vastaa.'));
    for(let i=0;i<(action==='control'?45:15);i++){
      await new Promise(resolve=>setTimeout(resolve,600));
      const s=await reloadWfRac();
      if(!s||s.busy)continue;
      if(action==='identify'&&s.result==='ok'&&s.configured&&s.fresh){wfRacDirty=false;showWfRac(s);}
      return;
    }
    text('wf-rac-feedback','WF-RAC-yhteys odottaa lukutestiä.');
  }catch(e){text(action==='pilot'?'wf-rac-pilot-result':'wf-rac-feedback',e.message);}
  finally{wfRacBusy=false;ids.forEach(id=>{$(id).disabled=false;});}
}
$('wf-rac-address').addEventListener('input',()=>{wfRacDirty=true;});
$('wf-rac-pair').addEventListener('click',()=>wfRacJob('identify'));
$('wf-rac-test').addEventListener('click',()=>wfRacJob('test'));
for(const [id,enabled] of [['wf-rac-approve',true],['wf-rac-revoke',false]])
  $(id).addEventListener('click',()=>{
    const operator=$('wf-rac-operator').value.trim();
    $('wf-rac-operator').value='';
    wfRacJob('pilot',{enabled:String(enabled),operator,device:wfRacStatus?.device??'',
      revision:String(wfRacStatus?.revision??''),model:$('wf-rac-pilot-model').value.trim(),
      min:$('wf-rac-pilot-min').value,max:$('wf-rac-pilot-max').value});
  });
for(const [id,action] of [['wf-rac-setpoint','target'],['wf-rac-heat','heat'],['wf-rac-off','off']])
  $(id).addEventListener('click',()=>wfRacJob('control',{action,
    revision:String(wfRacStatus?.revision??''),target:$('wf-rac-command-target').value}));
let dsiotStatus=null,dsiotDirty=false,dsiotBusy=false,dsiotSequence=0;
const dsiotMessages={unsupported:'Moduuli ei vastannut tuetulla DSIoT-protokollalla.',
  device_changed:'Daikin DSIoT -moduulin tunniste muuttui.',
  invalid_address:'Daikin DSIoT -moduulin osoite ei kelpaa tässä kotiverkossa.',
  storage:'Daikin DSIoT -yhteyden tallennus epäonnistui.',connection_failed:'Daikin DSIoT -yhteys ei vastaa.'};
function dsiotDraft(){return WZ.open&&WZ.step===4&&WZ.adapter==='daikin_dsiot';}
function dsiotActive(){return dsiotDraft()||state.site?.pump_adapter==='daikin_dsiot';}
function showDsiot(s){
  text('daikin-dsiot-feedback',dsiotMessages[s.result]||
    (s.fresh?'Daikin DSIoT -yhteys toimii.':'Daikin DSIoT odottaa lukutestiä.'));
  if(!dsiotDirty&&s.configured)$('daikin-dsiot-address').value=s.host;
  text('daikin-dsiot-mode',s.fresh?(s.power?(s.mode===1?'Lämmitys':Number.isInteger(s.mode)?'Tila '+s.mode:'—'):'Pois päältä'):'—');
  text('daikin-dsiot-target',s.fresh&&Number.isFinite(s.target_c)?fmt(s.target_c,1)+' °C':'—');
  text('daikin-dsiot-temperature',s.fresh&&Number.isFinite(s.indoor_c)?fmt(s.indoor_c,1)+' °C':'—');
}
async function reloadDsiot(){
  if(!dsiotActive()){dsiotStatus=null;return null;}
  const sequence=++dsiotSequence;
  const draft=dsiotDraft();
  try{
    const s=await fetchJSON('/api/daikin-dsiot/status'+(draft?'?draft=true':''));
    if(sequence!==dsiotSequence||!dsiotActive()||draft!==dsiotDraft())return null;
    dsiotStatus=s;showDsiot(s);return s;
  }catch(e){if(sequence===dsiotSequence){dsiotStatus=null;text('daikin-dsiot-feedback','Daikin DSIoT -yhteys ei vastaa.');}return null;}
}
function dsiotPost(action,fields){
  return localLanPost('daikin-dsiot',action,fields,dsiotMessages,'Daikin DSIoT -yhteys ei vastaa.');
}
async function dsiotCommit(id){
  const response=await dsiotPost('commit',{draft_id:String(id)});
  if(response.saved!==true)throw Error(translateFi('Daikin DSIoT -yhteyden tallennus epäonnistui.'));
}
async function dsiotJob(action){
  if(dsiotBusy)return;
  dsiotBusy=true;
  if(action==='identify')dsiotDirty=true;
  const ids=['daikin-dsiot-address','daikin-dsiot-pair','daikin-dsiot-test'];
  ids.forEach(id=>{$(id).disabled=true;});
  try{
    const response=await dsiotPost(action,{host:$('daikin-dsiot-address').value.trim(),draft:String(dsiotDraft())});
    if(response.queued!==true)throw Error(translateFi('Daikin DSIoT -yhteys ei vastaa.'));
    for(let i=0;i<15;i++){
      await new Promise(resolve=>setTimeout(resolve,600));
      const s=await reloadDsiot();
      if(!s||s.busy)continue;
      if(action==='identify'&&s.result==='ok'&&s.configured&&s.fresh){dsiotDirty=false;showDsiot(s);}
      return;
    }
    text('daikin-dsiot-feedback','Daikin DSIoT odottaa lukutestiä.');
  }catch(e){text('daikin-dsiot-feedback',e.message);}
  finally{dsiotBusy=false;ids.forEach(id=>{$(id).disabled=false;});}
}
$('daikin-dsiot-address').addEventListener('input',()=>{dsiotDirty=true;});
$('daikin-dsiot-pair').addEventListener('click',()=>dsiotJob('identify'));
$('daikin-dsiot-test').addEventListener('click',()=>dsiotJob('test'));
async function initialRefresh(){await reloadStatus();await reloadPrices();await reloadWeather();await reloadPlan();await reloadHistory();await reloadSite();await reloadMidea();await reloadGree();await reloadDaikin();await reloadWfRac();await reloadDsiot();await reloadTimeline();await reloadFreshness();
  const roomSettings=$('room-temperature-settings');
  if(roomSettings){
    roomSettings.append(wzEl('h2',{},WT('Huonelämpötilan lähde')),
      wzEl('div',{class:'statusgrid'},wzEl('span',{},WT('Ohjauksen huonelämpötila')),
        wzEl('span',{id:'room-temperature-value'})));
    refreshRoomTempSettings=roomTempForm(roomSettings,'settings',{});
    if(state.status)showStatus();
    translateDashboard();
  }
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
setInterval(()=>{if(!greeBusy)reloadGree();if(!daikinBusy)reloadDaikin();if(!wfRacBusy)reloadWfRac();if(!dsiotBusy)reloadDsiot();},10000);
setInterval(async()=>{await reloadPrices();await reloadWeather();await reloadPlan();await reloadTimeline();await reloadFreshness();
  if(document.getElementById('view-history').classList.contains('active'))await reloadHistoryView();},60000);
setInterval(reloadHistory,180000);
</script>
<script data-part="mpc">
'use strict';
// Predictive control (MPC) card on the Optimization view. Read-only: it
// only GETs /api/mpc. FI+EN texts are bundled here. Predicted energy and
// cost appear only when the backend marks them trustworthy (a calibrated
// model AND an owner-confirmed heat-pump-only meter). No savings are
// calculated or claimed. Colours come from the theme variables, so the
// automatic night theme applies.
(function(){
const MT={
fi:{title:'Ennakoiva säätö (MPC)',active:'Käytössä',calibrating:'Kalibroidaan',fallback:'Varatila',
 source:'Ohjauksen lähde nyt',MPC:'MPC-suunnitelma',PRICE_CONTROL:'Hintaohjaus',THERMOSTAT:'Termostaatti',NONE:'Ei ehdotusta',
 calib:'Rakennusmallin kalibrointi',samples:'Mittausjaksoja',windows:'Ennustetestit',model:'Tunnistettu malli',
 tau:'Aikavakio',lift:'Lämmityksen nosto',delay:'Käynnistysviive',after:'Jälkilämpö',valid:'Ennustevirhe (testi)',
 bias:'harha',margin:'Turvamarginaali',nomodel:'Mallia ei vielä ole: säätö käyttää hintaohjausta ja termostaattia, ja malli oppii samalla mittauksista.',
 cop:'COP:ta ei arvioida: se vaatisi lämpöenergian mittauksen.',bounds:'Alarajat (COP ≥ 1)',
 plan:'Lämmityssuunnitelma',updated:'Päivitetty',horizon:'Ennustejakso',next:'Seuraava päätös',
 heat:'HEAT 16 °C',off:'OFF',confirmed:'pumppu vahvisti tilan',unconfirmed:'ei vielä vahvistettu pumpulta',
 shadow:'varjotila: komentoja ei lähetetä',noplan:'Voimassa olevaa MPC-suunnitelmaa ei ole tälle varttitunnille.',
 energy:'Ennustettu sähkö ja kustannus',energyno:'Luotettavaa pumppukohtaista kulutusmallia ei ole saatavilla. Optimointi käyttää suhteellista energiaa.',
 meter:'Mitattu kulutus nyt (Shelly)',scopeU:'mittauskohde vahvistamaton – voi sisältää muuta kulutusta',scopeP:'vain lämpöpumppu',
 measured:'Mitattu sisälämpö',predicted:'Mallin ennuste (testi)',planned:'Suunniteltu sisälämpö',outdoor:'Ulkolämpö (MET Norway)',
 price:'Kokonaishinta',heating:'Lämmitys (suunnitelma)',band:'Mukavuusalue',now:'nyt',h:'h',
 err:'MPC-tietoja ei saatu laitteelta.',sim:'Kaikki luvut ovat laitteen omia laskelmia; säästöjä ei väitetä.',
 limit_MAX_48H:'48 h',limit_PRICES_END:'hinnat loppuvat',limit_PRICE_GAP:'hinnoissa aukko',limit_BAD_PRICE:'hinta ei kelpaa',
 limit_WEATHER_END:'sääennuste loppuu',limit_WEATHER_GAP:'sääennusteessa aukko',limit_NONE:'—',
 rejected:'Aiempi suunnitelma ei ole käytössä: ',
 unverified:'Pumpulta ei ole tuoretta tilalukemaa. MPC odottaa seuraavaa kelvollista lukemaa.'},
en:{title:'Predictive control (MPC)',active:'Active',calibrating:'Calibrating',fallback:'Fallback',
 source:'Control source now',MPC:'MPC plan',PRICE_CONTROL:'Price control',THERMOSTAT:'Thermostat',NONE:'No proposal',
 calib:'Building model calibration',samples:'Measured slots',windows:'Prediction tests',model:'Identified model',
 tau:'Time constant',lift:'Heating lift',delay:'Start delay',after:'After-heat',valid:'Prediction error (test)',
 bias:'bias',margin:'Safety margin',nomodel:'No model yet: control uses price control and the thermostat while the model learns from measurements.',
 cop:'COP is not estimated: that would need a heat-energy measurement.',bounds:'Lower bounds (COP ≥ 1)',
 plan:'Heating plan',updated:'Updated',horizon:'Horizon',next:'Next decision',
 heat:'HEAT 16 °C',off:'OFF',confirmed:'confirmed by the pump',unconfirmed:'not yet confirmed by the pump',
 shadow:'shadow mode: no commands are sent',noplan:'There is no valid MPC plan for this quarter-hour.',
 energy:'Predicted electricity and cost',energyno:'A reliable heat-pump consumption model is unavailable. Optimization uses relative energy.',
 meter:'Measured consumption now (Shelly)',scopeU:'measurement scope unconfirmed – may include other loads',scopeP:'heat pump only',
 measured:'Measured indoor',predicted:'Model prediction (test)',planned:'Planned indoor',outdoor:'Outdoor (MET Norway)',
 price:'Total price',heating:'Heating (plan)',band:'Comfort band',now:'now',h:'h',
 err:'MPC data could not be read from the device.',sim:'All numbers are the device’s own calculations; no savings are claimed.',
 limit_MAX_48H:'48 h',limit_PRICES_END:'prices end',limit_PRICE_GAP:'gap in prices',limit_BAD_PRICE:'invalid price',
 limit_WEATHER_END:'weather forecast ends',limit_WEATHER_GAP:'gap in weather forecast',limit_NONE:'—',
 rejected:'The earlier plan is not being executed: ',
 unverified:'A fresh pump reading is unavailable. MPC waits for the next valid reading.'}};
const WHY={
fi:{NONE:'',MODEL_LEARNING:'malli oppii vielä mittauksista',MODEL_VALIDATING:'mallin ennusteita testataan',
 MODEL_DEGRADED:'malli ei enää ennusta riittävän tarkasti',NO_HEATING_MODE:'valittu tila ei lämmitä',
 TARGET_OUTSIDE_MODEL:'tavoite ei ole lämpömallin alueella',OUTDOOR_OUTSIDE_LEARNED_RANGE:'ennustettu ulkolämpö on opitun alueen ulkopuolella',
 FORECAST_UNAVAILABLE:'hinta- tai sääennuste puuttuu tai on vanhentunut',HORIZON_TOO_SHORT:'ennustejakso on liian lyhyt',
 PUMP_STATE_UNKNOWN:'pumpun tilaa ei ole luettu',TEMPERATURE_STALE:'sisälämpötila puuttuu tai on vanha',
 SOLVER_FAILED:'suunnitelmaa ei löytynyt rajoissa',SOLVER_TIMEOUT:'laskenta-aika ylittyi',PLAN_STALE:'suunnitelma on vanhentunut',
 PLAN_INPUTS_CHANGED:'tila tai asetukset muuttuivat',BELOW_COMFORT_FLOOR:'huone on mukavuusalueen alapuolella',
 ABOVE_COMFORT_CEILING:'huone on mukavuusalueen yläpuolella',RESOURCE_LIMIT:'muistiraja',SYNTHETIC_MODEL_ONLY:'vain testimalli',
 COMFORT_LIMITS_INVALID:'omat lämpötilarajat ovat ristiriitaiset tai liian lähellä tavoitetta'},
en:{NONE:'',MODEL_LEARNING:'the model is still learning from measurements',MODEL_VALIDATING:'the model’s predictions are being tested',
 MODEL_DEGRADED:'the model no longer predicts accurately enough',NO_HEATING_MODE:'the selected mode does not heat',
 TARGET_OUTSIDE_MODEL:'the target is outside the thermal model range',OUTDOOR_OUTSIDE_LEARNED_RANGE:'forecast outdoor temperature is outside the learned range',
 FORECAST_UNAVAILABLE:'price or weather forecast missing or stale',HORIZON_TOO_SHORT:'forecast horizon too short',
 PUMP_STATE_UNKNOWN:'pump state has not been read',TEMPERATURE_STALE:'indoor temperature missing or stale',
 SOLVER_FAILED:'no plan within the limits',SOLVER_TIMEOUT:'computation time exceeded',PLAN_STALE:'plan is stale',
 PLAN_INPUTS_CHANGED:'mode or settings changed',BELOW_COMFORT_FLOOR:'room is below the comfort band',
 ABOVE_COMFORT_CEILING:'room is above the comfort band',RESOURCE_LIMIT:'memory limit',SYNTHETIC_MODEL_ONLY:'test model only',
 COMFORT_LIMITS_INVALID:'your temperature limits are contradictory or too close to the target'}};
const STATE={fi:{NO_DATA:'ei mittauksia',LEARNING:'oppii',VALIDATING:'testataan',CALIBRATED:'kalibroitu',DEGRADED:'heikentynyt'},
 en:{NO_DATA:'no measurements',LEARNING:'learning',VALIDATING:'testing',CALIBRATED:'calibrated',DEGRADED:'degraded'}};
const lang=()=>uiLanguage==='en'?'en':'fi';
const t=k=>MT[lang()][k]??MT.fi[k]??k;
const num=v=>typeof v==='number'&&Number.isFinite(v)?v:null;
const f=(v,d)=>{const n=num(v);return n===null?'—':n.toLocaleString(uiLocale(),{minimumFractionDigits:d,maximumFractionDigits:d});};
const hm=u=>new Intl.DateTimeFormat(uiLocale(),{timeZone:uiTimeZone(),hour:'2-digit',minute:'2-digit'}).format(new Date(u*1000));
const day=u=>new Intl.DateTimeFormat(uiLocale(),{timeZone:uiTimeZone(),weekday:'short',hour:'2-digit',minute:'2-digit'}).format(new Date(u*1000));
const NS='http://www.w3.org/2000/svg';
let data=null;
const st=document.createElement('style');
st.textContent='#mpc-card .mpc-grid{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:12px;margin:12px 0}'+
 '#mpc-card .mpc-box{border:1px solid var(--edge);border-radius:14px;padding:12px;min-width:0}'+
 '#mpc-card .mpc-k{font-size:12px;color:var(--muted)}#mpc-card .mpc-v{font-size:18px;font-weight:650;color:var(--ink);overflow-wrap:anywhere}'+
 '#mpc-card svg{width:100%;height:auto;display:block}#mpc-card .mpc-legend{display:flex;flex-wrap:wrap;gap:10px;font-size:12px;color:var(--muted);margin-top:6px}'+
 '#mpc-card .mpc-sw{display:inline-block;width:14px;height:4px;border-radius:2px;margin-right:5px;vertical-align:middle}'+
 '@media(max-width:600px){#mpc-card .mpc-grid{grid-template-columns:repeat(1,minmax(0,1fr))}}';
document.head.appendChild(st);
function node(tag,cls,text){const e=document.createElement(tag);if(cls)e.className=cls;if(text!=null)e.textContent=text;return e;}
function box(k,v,sub){const b=node('div','mpc-box');b.append(node('div','mpc-k',k),node('div','mpc-v',v));if(sub)b.append(node('div','tiny muted',sub));return b;}
function svg(tag,a){const e=document.createElementNS(NS,tag);for(const k in a)e.setAttribute(k,String(a[k]));return e;}
function card(){
  let c=document.getElementById('mpc-card');
  if(c)return c;
  const grid=document.querySelector('#view-plan .grid');
  if(!grid)return null;
  c=document.createElement('article');c.className='card span12';c.id='mpc-card';
  grid.prepend(c);return c;
}
function chart(d){
  const W=720,H=300,L=44,R=44,T=12,TB=196,PB0=214,PB1=286;
  const past=(d.trend||[]).filter(p=>num(p.measured_c)!==null);
  const plan=d.plan&&d.plan.valid?(d.plan.slots||[]):[];
  if(!past.length&&!plan.length)return null;
  const t0=past.length?past[0].utc-900:d.now_utc;
  const t1=plan.length?plan[plan.length-1].utc+900:d.now_utc+3600;
  const x=u=>L+(u-t0)/(t1-t0)*(W-L-R);
  const temps=[];past.forEach(p=>{temps.push(p.measured_c);if(num(p.open_loop_c)!==null)temps.push(p.open_loop_c);});
  plan.forEach(p=>temps.push(p.indoor_c));
  if(d.plan&&d.plan.valid){temps.push(d.plan.lower_c,d.plan.upper_c);}
  let lo=Math.min(...temps)-0.5,hi=Math.max(...temps)+0.5;if(!(hi>lo)){lo-=1;hi+=1;}
  const y=v=>TB-(v-lo)/(hi-lo)*(TB-T);
  const outs=plan.map(p=>p.outdoor_c);
  const olo=outs.length?Math.min(...outs)-1:0,ohi=outs.length?Math.max(...outs)+1:1;
  const yo=v=>TB-(v-olo)/(ohi-olo)*(TB-T);
  const s=svg('svg',{viewBox:'0 0 '+W+' '+H,role:'img','aria-label':t('plan')});
  s.append(svg('line',{x1:L,y1:TB,x2:W-R,y2:TB,stroke:'var(--edge)'}));
  for(let i=0;i<=4;i++){const v=lo+(hi-lo)*i/4;
    s.append(svg('line',{x1:L,y1:y(v),x2:W-R,y2:y(v),stroke:'var(--edge)','stroke-dasharray':'2 4'}));
    const tx=svg('text',{x:L-6,y:y(v)+4,'text-anchor':'end','font-size':11,fill:'var(--muted)'});tx.textContent=f(v,1);s.append(tx);}
  if(plan.length){
    const xs=x(plan[0].utc),xe=x(t1);
    s.append(svg('rect',{x:xs,y:y(d.plan.upper_c),width:xe-xs,height:Math.max(1,y(d.plan.lower_c)-y(d.plan.upper_c)),fill:'var(--mint)',opacity:0.7}));
    plan.forEach(p=>{if(p.heat)s.append(svg('rect',{x:x(p.utc),y:TB+4,width:Math.max(1,x(p.utc+900)-x(p.utc)),height:10,fill:'var(--green)'}));});
    const prices=plan.map(p=>p.price),pmax=Math.max(0.0001,...prices.map(Math.abs));
    const base=prices.some(v=>v<0)?(PB0+PB1)/2:PB1;
    plan.forEach(p=>{const hgt=Math.abs(p.price)/pmax*(base-PB0);
      s.append(svg('rect',{x:x(p.utc),y:p.price>=0?base-hgt:base,width:Math.max(1,x(p.utc+900)-x(p.utc)-0.5),height:Math.max(0.5,hgt),fill:p.price<0?'var(--red)':'var(--muted)',opacity:0.55}));});
    const op=plan.map((p,i)=>(i?'L':'M')+x(p.utc+450)+' '+yo(p.outdoor_c)).join(' ');
    s.append(svg('path',{d:op,fill:'none',stroke:'var(--warning)','stroke-width':1.5,'stroke-dasharray':'5 4'}));
    for(let i=0;i<=2;i++){const v=olo+(ohi-olo)*i/2;
      const tx=svg('text',{x:W-R+6,y:yo(v)+4,'font-size':11,fill:'var(--warning)'});tx.textContent=f(v,0);s.append(tx);}
    const pp=plan.map((p,i)=>(i?'L':'M')+x(p.utc+900)+' '+y(p.indoor_c)).join(' ');
    s.append(svg('path',{d:pp,fill:'none',stroke:'var(--green)','stroke-width':2.2}));
  }
  if(past.length){
    const mp=past.map((p,i)=>(i?'L':'M')+x(p.utc)+' '+y(p.measured_c)).join(' ');
    s.append(svg('path',{d:mp,fill:'none',stroke:'var(--ink)','stroke-width':2}));
    const pred=past.filter(p=>num(p.open_loop_c)!==null);
    if(pred.length>1){
      const pr=pred.map((p,i)=>(i?'L':'M')+x(p.utc)+' '+y(p.open_loop_c)).join(' ');
      s.append(svg('path',{d:pr,fill:'none',stroke:'var(--green)','stroke-width':1.6,'stroke-dasharray':'4 3'}));
    }
  }
  const xn=x(d.now_utc);
  s.append(svg('line',{x1:xn,y1:T,x2:xn,y2:PB1,stroke:'var(--ink)','stroke-width':1,opacity:0.5}));
  const nt=svg('text',{x:xn+4,y:T+10,'font-size':11,fill:'var(--muted)'});nt.textContent=t('now');s.append(nt);
  for(let u=Math.ceil(t0/21600)*21600;u<t1;u+=21600){
    const tx=svg('text',{x:x(u),y:H-2,'text-anchor':'middle','font-size':10,fill:'var(--muted)'});tx.textContent=hm(u);s.append(tx);}
  return s;
}
function legend(){
  const l=node('div','mpc-legend');
  [['var(--ink)','measured'],['var(--green)','predicted'],['var(--green)','planned'],['var(--warning)','outdoor'],
   ['var(--muted)','price'],['var(--green)','heating'],['var(--mint)','band']].forEach(([c,k])=>{
    const s=node('span');const sw=node('span','mpc-sw');sw.style.background=c;s.append(sw,document.createTextNode(t(k)));l.append(s);});
  return l;
}
function render(){
  const c=card();if(!c)return;
  c.replaceChildren();
  const head=node('div','flex');head.append(node('h2',null,t('title')));
  if(!data||data.error){head.append(node('span','chip',t('fallback')));c.append(head,node('p','callout warn',t('err')));return;}
  const d=data,why=WHY[lang()][d.fallback]||d.fallback||'';
  const label=d.state==='ACTIVE'?t('active'):d.state==='CALIBRATING'?t('calibrating'):t('fallback');
  head.append(node('span','chip',why&&d.state!=='ACTIVE'?label+': '+why:label));
  c.append(head);
  const cal=d.calibration||{},m=cal.model||{};
  const g=node('div','mpc-grid');
  g.append(box(t('source'),t(d.source||'NONE')));
  g.append(box(t('calib'),STATE[lang()][cal.state]||cal.state||'—',
    t('samples')+' '+(cal.samples??0)+' / 192 · '+t('windows')+' '+Math.min(cal.windows??0,cal.windows_needed??12)+' / '+(cal.windows_needed??12)));
  g.append(box(t('valid'),num(cal.validation_rms_c)===null?'—':f(cal.validation_rms_c,2)+' °C',
    num(cal.validation_bias_c)===null?null:t('bias')+' '+f(cal.validation_bias_c,2)+' °C · '+t('margin')+' '+f(cal.planning_uncertainty_c,2)+' °C'));
  c.append(g);
  if(m.plausible){
    const g2=node('div','mpc-grid');
    g2.append(box(t('tau'),f(m.tau_h,1)+' ± '+f(m.tau_std_h,1)+' '+t('h')));
    g2.append(box(t('lift'),f(m.lift_k,1)+' ± '+f(m.lift_std_k,1)+' °C',(lang()==='en'?'at outdoor ':'ulkona ')+f(m.lift_outdoor_c,1)+' °C'));
    g2.append(box(t('delay')+' · '+t('after'),f(m.start_delay_min,1)+' min · '+f(m.afterheat_k,2)+' °C'));
    c.append(g2);
    const extra=[t('cop')];
    if(num(m.ua_lower_kw_per_c)!==null)extra.push(t('bounds')+': UA ≥ '+f(m.ua_lower_kw_per_c,3)+' kW/K, C ≥ '+f(m.capacity_lower_kwh_per_c,2)+' kWh/K');
    c.append(node('p','tiny muted',extra.join(' ')));
  } else c.append(node('p','callout',t('nomodel')));
  if((d.pump_connection_ready??d.pump_source_verified)===false)c.append(node('p','callout warn',t('unverified')));
  const ch=chart(d);
  if(ch){c.append(ch,legend());}
  const p=d.plan||{};
  const g3=node('div','mpc-grid');
  if(p.valid&&p.slots&&p.slots.length){
    const first=p.slots[0];
    let status=d.decision_confirmed?t('confirmed'):d.armed?t('unconfirmed'):t('shadow');
    g3.append(box(t('next'),(first.heat&&num(first.setpoint_c)!==null?'HEAT '+f(first.setpoint_c,1)+' °C':first.heat?t('heat'):t('off'))+' · '+hm(first.utc),status));
    g3.append(box(t('updated'),day(p.computed_utc),t('horizon')+' '+f(p.horizon_slots/4,1)+' '+t('h')+' ('+t('limit_'+p.horizon_limit)+')'));
    g3.append(box(t('energy'),p.energy_valid?f(p.energy_kwh,2)+' kWh · '+f(p.cost,2)+' '+(p.currency||''):'—',p.energy_valid?null:t('energyno')));
  } else {
    // A plan the controller has rejected is never shown as in force: say why
    // (the same reason the controller used), not the plan's old decisions.
    const reason=WHY[lang()][p.plan_block]||WHY[lang()][d.fallback]||WHY[lang()][p.planning_fallback]||null;
    g3.append(box(t('plan'),'—',t('noplan')+(reason&&p.had_plan?' '+t('rejected')+reason+'.':'')));
    g3.append(box(t('updated'),p.computed_utc?day(p.computed_utc):'—',reason));
  }
  const meter=d.meter||{};
  g3.append(box(t('meter'),num(meter.watts)===null?'—':f(meter.watts,0)+' W',meter.scope==='HEAT_PUMP_ONLY'?t('scopeP'):t('scopeU')));
  c.append(g3,node('p','tiny muted',t('sim')));
}
async function load(){
  try{const r=await fetch('/api/mpc',{cache:'no-store'});if(!r.ok)throw Error('HTTP '+r.status);data=await r.json();}
  catch(_){data={error:true};}
  render();
}
document.querySelectorAll('[data-lang]').forEach(b=>b.addEventListener('click',()=>setTimeout(render,0)));
load();
setInterval(load,60000);
})();
</script>
</body>
</html>)ENEVINA_UI";
