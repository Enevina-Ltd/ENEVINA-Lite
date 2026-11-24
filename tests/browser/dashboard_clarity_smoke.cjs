'use strict';
const fs=require('node:fs'),http=require('node:http'),os=require('node:os'),path=require('node:path'),{spawn}=require('node:child_process'),assert=require('node:assert/strict');
const root=path.resolve(__dirname,'../..');
const browser=process.env.CHROMIUM_BIN||'chromium';
const out=process.env.CLARITY_SCREENSHOTS;
const h=fs.readFileSync(root+'/firmware/cottage/include/cottage_premium_dashboard.hpp','utf8');
const html=h.slice(h.indexOf('<!doctype html>'),h.indexOf('</html>')+7);
const T=Math.floor(Date.now()/900000)*900,now=T+300;
const region={price_region_resolved:true,nordpool_area:'FI',currency:'EUR',timezone:'Europe/Helsinki',country:'FI',setup_complete:true,city:'Test locality'};
let mode='learning',offline=false,mutations=0;
const status={air:{sensor_fresh:true,mode:2,temperature_c:11.83,age_s:6,rh_pct:52,dewpoint_c:3.1,outside_temperature_c:-4,hold_target_c:12,occupied_target_c:21,drying_delta_c:4,config_token:'fixture-only',warning_rh70:70,warning_rh80:85,warning_battery_v:2.5,alerts:[]},room_temperature_source:'ruuvi',room_temperature_c:11.83,room_temperature_age_s:6,spot:-.03,total_price:-.012,price_area:'FI',price_currency:'EUR',control_armed:true,control_gate:'NO_PROPOSAL',control_source:'PRICE_CONTROL',bootstrap_reason:'HOLD_IN_DEADBAND',bootstrap_action:'HOLD',bootstrap_room_target_c:12,comfort_min_c:11,comfort_max_c:13,bootstrap_price_data_valid:true,pump_requested_mode:'OFF',cn105_sse_climate_recent:true,cn105_sse_mode:'OFF',cn105_sse_compressor_recent:true,cn105_sse_compressor_hz:0,shelly_meter_pump_only:true,shelly_w:0,weather_c:-4,market_last_fetch_utc:T,wifi_rssi_dbm:-55,firmware_version:'TEST',free_internal_heap:75000,free_psram:7000000,mpc_fallback:'MODEL_LEARNING',calibration_state:'LEARNING'};
const slots=Array.from({length:192},(_,i)=>({utc:T+i*900,spot:i===0?-.03:(i%12)*.02,total:i===0?-.012:(i%12)*.02+.02,class:i%12<4?'cheap':i%12<8?'neutral':'expensive',median:.14}));
const weather={valid:true,fetched_utc:T,slots:Array.from({length:48},(_,i)=>({utc:Math.ceil(now/3600)*3600+i*3600,temperature_c:-6+4*Math.sin(i/5)})).filter((_,i)=>i!==13)};
const history={valid:true,range:'24h',step_s:900,persistent:true,currency:'EUR',meter_scope:'heat_pump',current_meter_scope:'heat_pump',slots_total:96,slots_present:90,kwh_slots:90,kwh:3.2,power_coverage_minutes:1350,pump_cost:.43,pump_cost_currency:'EUR',pump_cost_minutes:1320,indoor:{min:11.7,max:12.4,avg:12},outdoor:{min:-5,max:0,avg:-3},price:{min:-.02,max:.3,avg:.13},points:Array.from({length:96},(_,i)=>({t:T-86400+i*900,n:i===15?0:1,a:i%9<4?2:1,r:1,os:1,in:i===15?null:12+.25*Math.sin(i/8),out:-3,w:i===15?null:i%9<4?450:0,scope:'heat_pump',power_coverage_minutes:i===15?0:15,p:.13,pc:'EUR'})),events:[]};
const plan={valid:true,computed_utc:T,energy_valid:true,count:96,cost_eur:.65,minimum_c:11.8,slots:Array.from({length:96},(_,i)=>({utc:T+i*900,heat:i%10>=3&&i%10<=6,setpoint_c:16,indoor_c:12,eur:.007,kwh:.1}))};
const settings={region,pump_adapter:'mitsubishi_cn105_esphome',stored_in_device:true,tariff:{margin_c_per_kwh:0,transfer_day_c_per_kwh:0,transfer_night_c_per_kwh:0,electricity_tax_c_per_kwh:0,vat_percent:25.5,transfer_kind:'flat',day_start_hour:7,night_start_hour:22,components_include_vat:false},min_price_advantage_percent:10,adapters:[]};
const server=http.createServer((req,res)=>{
 if(req.method!=='GET'){mutations++;res.writeHead(405);res.end();return;}
 const url=new URL(req.url,'http://127.0.0.1');
 if(url.pathname==='/'){res.setHeader('Content-Type','text/html; charset=utf-8');res.end(html);return;}
 if(offline&&url.pathname.startsWith('/api/')){res.writeHead(503);res.end('{}');return;}
 let data={};
 switch(url.pathname){
 case '/api/status':data={...status,...(mode==='active'?{control_source:'MPC',mpc_fallback:'NONE',mpc_plan_computed_utc:T,mpc_state:'ACTIVE'}:mode==='rejected'?{mpc_fallback:'PLAN_INPUTS_CHANGED',mpc_plan_computed_utc:T}:{} )};break;
 case '/api/settings':data=settings;break;
 case '/api/prices':data={valid:true,area:'FI',currency:'EUR',slots};break;
 case '/api/timeline':data={valid:true,area:'FI',currency:'EUR',profile:'balanced',slots,segments:[],now:{action:'HOLD',source:'PRICE_CONTROL',reason:'HOLD_IN_DEADBAND'},target_c:12,comfort_min_c:11,comfort_max_c:13,indoor_fresh:true,heating_permitted:true};break;
 case '/api/weather':data=weather;break;
 case '/api/history':data={...history,range:url.searchParams.get('range')||'24h'};break;
 case '/api/plan':data=mode==='active'?plan:{valid:false,slots:[]};break;
 case '/api/air/history':data={hours:[]};break;
 case '/api/mpc':data={state:'CALIBRATING',fallback:'MODEL_LEARNING',source:'PRICE_CONTROL',calibration:{state:'LEARNING',samples:40,samples_needed:192,heating_slots:10,heating_needed:32,windows:3,windows_needed:12},meter:{},plan:{valid:false,slots:[]},trend:[]};break;
 case '/api/freshness':data={fallback:'NONE',sources:['prices','weather','sensor','pump'].map(id=>({id,status:'FRESH'}))};break;
 }
 res.setHeader('Content-Type','application/json');res.end(JSON.stringify(data));
});
const pause=ms=>new Promise(r=>setTimeout(r,ms));
// Bounded local smoke test; synthetic device data only, localhost HTTP.
const timer=setTimeout(()=>{console.error('Browser smoke test exceeded 90 seconds');process.exit(1);},90000);timer.unref();
(async()=>{
 await new Promise(r=>server.listen(0,'127.0.0.1',r));const port=server.address().port;
 const profile=fs.mkdtempSync(path.join(os.tmpdir(),'lite-browser-'));
 const chrome=spawn(browser,['--headless','--no-sandbox','--disable-dev-shm-usage','--disable-background-networking','--remote-debugging-port=0','--user-data-dir='+profile,'about:blank'],{stdio:['ignore','ignore','pipe']});
 let debug='';
 chrome.stderr.on('data',b=>{debug+=b.toString()});
 let ws;
 try{
  for(let i=0;i<100&&!debug.includes('DevTools listening on');i++)await pause(100);
  const endpoint=debug.match(/DevTools listening on (ws:\/\/127\.0\.0\.1:\d+\/devtools\/browser\/[^\s]+)/)?.[1];assert(endpoint,debug.slice(-700));
  const list=await (await fetch(endpoint.replace(/^ws:/,'http:').split('/devtools/')[0]+'/json/list')).json();
  ws=new WebSocket(list.find(x=>x.type==='page').webSocketDebuggerUrl);
  await new Promise((r,j)=>{ws.onopen=r;ws.onerror=j});
  let seq=0,pending=new Map(),exceptions=[];
  ws.onmessage=e=>{const m=JSON.parse(e.data);if(m.id&&pending.has(m.id)){const [r,j]=pending.get(m.id);pending.delete(m.id);m.error?j(Error(JSON.stringify(m.error))):r(m.result);}if(m.method==='Runtime.exceptionThrown')exceptions.push(m.params.exceptionDetails.exception?.description||m.params.exceptionDetails.text)};
  const call=(method,params={})=>new Promise((r,j)=>{const id=++seq;pending.set(id,[r,j]);ws.send(JSON.stringify({id,method,params}));});
  const evalJS=async(expression)=>{const r=await call('Runtime.evaluate',{expression,awaitPromise:true,returnByValue:true});if(r.exceptionDetails)throw Error(r.exceptionDetails.exception?.description||r.exceptionDetails.text);return r.result.value};
  await call('Runtime.enable');await call('Page.enable');
  await call('Page.addScriptToEvaluateOnNewDocument',{source:'Date.now=()=>'+now*1000+';'});
  await call('Emulation.setDeviceMetricsOverride',{width:1280,height:900,deviceScaleFactor:1,mobile:false});
  await call('Page.navigate',{url:'http://127.0.0.1:'+port+'/'});
  for(let i=0;i<100;i++){if(await evalJS("document.querySelector('#cl-temperature-chart svg')&&document.querySelector('#home-chart svg')&&document.querySelector('#cl-power-chart svg')&&document.getElementById('cl-kwh').textContent.includes('kWh')?true:false").catch(()=>false))break;await pause(120);}
  assert.equal(await evalJS("document.querySelectorAll('#view-home svg').length"),3,'home charts not loaded');
  assert.equal(await evalJS("document.getElementById('home-temp').textContent"),'11,8 °C');
  assert.match(await evalJS("document.getElementById('cl-next').textContent"),/ei vielä tiedetä/);
  assert.equal(await evalJS("document.getElementById('cl-heat-hours').textContent"),'—');
  assert.equal(await evalJS("document.getElementById('cl-pump').textContent"),'Pois pyydetty');
  assert.match(await evalJS("document.getElementById('cl-pump-detail').textContent"),/Sillalta luettu tilapyyntö/);
  assert.equal(await evalJS("document.getElementById('cl-kwh').textContent"),'3,20 kWh');
  assert.equal(await evalJS("document.getElementById('cl-cost').textContent"),'0,43 EUR');
  assert.equal(await evalJS("document.getElementById('technical-notice').hidden"),true);
  const svgReport=await evalJS("Array.from(document.querySelectorAll('#home-chart svg .cl-price-cheap')).length");assert(svgReport>0);
  if(out){fs.mkdirSync(out,{recursive:true});const shot=await call('Page.captureScreenshot',{format:'png'});fs.writeFileSync(path.join(out,'desktop.png'),Buffer.from(shot.data,'base64'));}
  await call('Emulation.setDeviceMetricsOverride',{width:390,height:844,deviceScaleFactor:1,mobile:true});
  await evalJS("showPrices();clRenderHistory();clRenderPlan()");
  const size=await evalJS("({vw:document.documentElement.clientWidth,sw:document.documentElement.scrollWidth,home:document.querySelector('#view-home').getBoundingClientRect().width})");assert(size.sw<=size.vw+1,JSON.stringify(size));
  if(out){const shot=await call('Page.captureScreenshot',{format:'png',captureBeyondViewport:true});fs.writeFileSync(path.join(out,'mobile.png'),Buffer.from(shot.data,'base64'));}
  const inspect=await evalJS("(()=>{let i=document.querySelector('#home-chart input[type=range]');i.value='4';i.dispatchEvent(new Event('input'));return i.getAttribute('aria-valuetext')})()");assert(inspect.includes('snt/kWh'));
  await evalJS("setDashboardLanguage('en')");
  assert.equal(await evalJS("document.querySelector('#view-home h1').textContent"),'Home energy');assert.equal(await evalJS("document.getElementById('cl-pump').textContent"),'Off requested');
  await evalJS("setDashboardLanguage('fi')");assert.equal(await evalJS("document.querySelector('#view-home h1').textContent"),'Kodin energia');
  await evalJS("setDashboardLanguage('en')");
  mode='active';await evalJS('reloadStatus().then(reloadPlan)');
  assert.equal(await evalJS("document.querySelectorAll('#cl-plan-chart svg rect').length"),96);
  assert.match(await evalJS("document.getElementById('cl-next').textContent"),/Next planned heating/);
  mode='rejected';await evalJS('reloadStatus()');assert.equal(await evalJS("document.querySelectorAll('#cl-plan-chart svg rect').length"),0);
  await evalJS("switchView('history')");for(let i=0;i<30;i++){if(await evalJS("!!document.querySelector('#hs-indoor-simple svg')"))break;await pause(100);}
  assert(await evalJS("!!document.querySelector('#hs-indoor-simple svg')&&!!document.querySelector('#hs-outdoor-simple svg')&&!document.querySelector('.cl-history-details').open"));
  await evalJS("switchView('details')");assert.equal(await evalJS("document.getElementById('technical-notice').hidden"),false);
  await evalJS("switchView('home');document.documentElement.dataset.theme='night'");
  assert(await evalJS("getComputedStyle(document.querySelector('#view-home')).color!==''"));
  offline=true;await evalJS('reloadStatus()');assert.equal(await evalJS("document.getElementById('cl-pump').textContent"),'—');assert.equal(await evalJS("document.querySelectorAll('#home-chart svg rect').length"),0);
  assert.equal(mutations,0);assert.deepEqual(exceptions,[]);
  console.log(JSON.stringify({browser:'Chromium headless',desktop:'1280x900',mobile:'390x844',mobileWidth:size,checks:['FI/EN','three data graphs','negative prices','inspector','learning vs active vs rejected plan','history details','dark theme','offline','no POST','no unhandled JS exceptions'],screenshots:out||null}));
 }finally{if(ws)ws.close();chrome.kill();server.close();await pause(200);fs.rmSync(profile,{recursive:true,force:true});}
})().catch(e=>{console.error(e);process.exitCode=1});
