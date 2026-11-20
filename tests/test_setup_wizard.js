/* Setup wizard: pure logic in a VM + dictionary completeness. DOM rendering is
 * covered by the browser run documented in docs/SETUP_WIZARD.md. */
"use strict";
const fs=require('fs'),vm=require('vm'),assert=require('node:assert/strict'),path=require('path');
const header=fs.readFileSync(path.join(__dirname,'../firmware/cottage/include/cottage_premium_dashboard.hpp'),'utf8');
const m=/<script id="wizard-script">([\s\S]*?)<\/script>/.exec(header);
assert(m,'wizard script block missing');
const src=m[1];

// Every Finnish literal with non-ASCII letters must have an English entry.
const dict=JSON.parse(header.split('const UI_EN=Object.freeze(')[1].split(/\);\r?\nconst UI_EN_FRAGMENTS/)[0]);
const literals=[...src.matchAll(/WT\('([^']*)'\)/g)].map(x=>x[1]);
assert(literals.length>80,'expected the wizard literals to be found');
const allowedUntranslated=new Set(['Wi-Fi']);
const missing=literals.filter(l=>!allowedUntranslated.has(l)&&!(l in dict));
// Static wizard captions (no WT marker) must be translated as well.
const wizHtml=header.split('<div id="wizard"')[1].split('<div class="shell">')[0];
for(const c of [...wizHtml.matchAll(/>([^<>]+)</g)].map(x=>x[1].trim()).filter(Boolean))
  if(c!=='×')assert(c in dict,'static wizard caption not translated: '+c);
for(const a of [...wizHtml.matchAll(/aria-label="([^"]+)"/g)].map(x=>x[1]))
  assert(a in dict,'aria-label not translated: '+a);
assert.deepEqual(missing,[],'missing English translations');

const ctx={document:{},state:{},uiLanguage:'fi',siteErrors:{},maturityText:{}};
vm.createContext(ctx);
vm.runInContext(src+';this.api={wzBlankTariff,wzParseLatLon,wzTariffFields,wzSummary,wzComfortFields,WZ_PROFILES,wzNeedsAction,wzAgeText,wzSensorLine,wzSensorState,wzBindNeedsConfirm,wzShellyText,wzDevicesFields,wzSetShellyAddress,wzSignalText,wzStartStep,wzMinor,wzZoneLabel,wzNum,WZ_STEPS,wzSaveComfort,WZ};',ctx);
const A=ctx.api;

// coordinates: dot decimals only, ranges enforced, canonical 4 decimals
assert.equal(A.wzParseLatLon('61.4638, 24.0760'),'61.4638,24.0760');
assert.equal(A.wzParseLatLon('-33.8688;151.2093'),'-33.8688,151.2093');
for(const bad of ['61,4638, 24,0760','91,10','10,181','abc','','61.4','61.4,24.1,3',null,undefined,'1e2,3'])
  assert.equal(A.wzParseLatLon(bad),null,String(bad));

// tariff form
assert.equal(A.wzNum('0,49'),0.49);assert.equal(A.wzNum(' 3.45 '),3.45);
for(const bad of ['','abc','-1','1e3','0x10','1,2,3',null])assert.equal(A.wzNum(bad),null,String(bad));
let r=A.wzTariffFields({margin:'0,49',tday:'3,45',tnight:'2,05',tax:'2,83',vat:'25,5',incl:'true'});
assert(r.ok);
assert.deepEqual({...r.fields},{margin_c_per_kwh:'0.49',transfer_day_c_per_kwh:'3.45',transfer_night_c_per_kwh:'2.05',
  electricity_tax_c_per_kwh:'2.83',vat_percent:'25.5',components_include_vat:'true',tariff_confirmed:'true'});
assert(!A.wzTariffFields({margin:'',tday:'1',tnight:'1',tax:'1',vat:'25',incl:'false'}).ok);   // nothing guessed
assert(!A.wzTariffFields({margin:'1',tday:'1',tnight:'1',tax:'1',vat:'60',incl:'false'}).ok);
assert.equal(A.wzTariffFields({margin:'1',tday:'1',tnight:'1',tax:'1',vat:'0',incl:'x'}).fields.components_include_vat,'false');

// minor units follow the currency
assert.deepEqual(['EUR','SEK','NOK','DKK','PLN','XXX'].map(A.wzMinor),['snt','öre','øre','øre','gr','snt']);

// zone labels: 'Ruotsi – Tukholma (SE3)'
const se={name_fi:'Ruotsi',name_en:'Sweden',zones:[{id:'SE3',name_fi:'Tukholma',name_en:'Stockholm'},{id:'SE4'}]};
assert.equal(A.wzZoneLabel(se,se.zones[0],'fi'),'Ruotsi – Tukholma (SE3)');
assert.equal(A.wzZoneLabel(se,se.zones[0],'en'),'Sweden – Stockholm (SE3)');
const fi={name_fi:'Suomi',name_en:'Finland',zones:[{id:'FI'}]};
assert.equal(A.wzZoneLabel(fi,fi.zones[0],'fi'),'Suomi (FI)');

// resume point comes from the device, clamped
assert.equal(A.wzStartStep({region:{setup_step:3}}),3);
assert.equal(A.wzStartStep({region:{setup_step:9}}),A.WZ_STEPS-1);
assert.equal(A.wzStartStep({}),0);assert.equal(A.wzStartStep(null),0);

// summary: honest per-item state from live data
const good={site:{fmi_latlon:'61.4638,24.0760',pump_adapter:'mitsubishi_cn105_esphome',shelly_scope:'heat_pump',
    region:{city:'Kangasala',price_region_resolved:true,nordpool_area:'FI',currency:'EUR',tariff_confirmed:true}},
  status:{spot:0.05,price_error:0,cn105_source_verified:true,air:{sensor_fresh:true},shelly_w:300},
  weather:{valid:true},plan:{valid:true}};
let s=A.wzSummary(good);const by=Object.fromEntries(s.map(x=>[x.key,x.state]));
assert.deepEqual(by,{wifi:'ok',location:'ok',zone:'ok',tariff:'ok',pump:'ok',sensor:'ok',meter:'ok',profile:'ok',optimizer:'ok'});
assert.equal(A.wzNeedsAction(s).length,0);
// fresh device: nothing configured -> todo/warn, never a fake ok
s=A.wzSummary({site:{region:{}},status:{air:{}},weather:null,plan:null});
const by2=Object.fromEntries(s.map(x=>[x.key,x.state]));
assert.deepEqual(by2,{wifi:'ok',location:'todo',zone:'todo',tariff:'warn',pump:'todo',sensor:'warn',meter:'optional',profile:'ok',optimizer:'warn'});
assert.equal(A.wzNeedsAction(s).length,6);          // optional meter and wifi are not "action"
// zone chosen but the price fetch failed: warn with the real reason
const failing=JSON.parse(JSON.stringify(good));failing.status.spot=null;failing.status.price_error=104;
s=A.wzSummary(failing);const z=s.find(x=>x.key==='zone');
assert.equal(z.state,'warn');assert(z.detail.includes('ei ole vielä julkaissut'));
// a pump that is selected but silent is a warning, not ok
const quiet=JSON.parse(JSON.stringify(good));quiet.status.cn105_source_verified=false;
assert.equal(A.wzSummary(quiet).find(x=>x.key==='pump').state,'warn');
// Codex P1 (#56): one old snapshot is not a working connection
const pumpOf=st=>{const c=JSON.parse(JSON.stringify(good));c.status={...c.status,cn105_source_verified:false,...st};return A.wzSummary(c).find(x=>x.key==='pump');};
let pm=pumpOf({cn105_sse_climate_received:true,cn105_sse_climate_age_s:7});
assert.equal(pm.state,'warn');assert(pm.detail.includes('UART-lähdettä ei ole varmennettu'));      // observed is not verified
pm=pumpOf({cn105_sse_climate_received:true,cn105_sse_climate_age_s:900});
assert.equal(pm.state,'warn');assert(pm.detail.includes('ei tule'));                              // stale snapshot
pm=pumpOf({cn105_sse_climate_received:true,cn105_sse_climate_age_s:null});assert(pm.detail.includes('ei tule'));
pm=pumpOf({cn105_sse_climate_received:true,cn105_sse_climate_age_s:-5});assert(pm.detail.includes('ei tule'));
pm=pumpOf({cn105_source_verified:true});assert.equal(pm.state,'ok');
const midea=JSON.parse(JSON.stringify(good));midea.site.pump_adapter='midea_lan';
assert.equal(A.wzSummary(midea).find(x=>x.key==='pump').state,'warn');                            // never a fake ok for Midea
// a country change blanks the tariff draft (source guard on the handler below)
assert.deepEqual({...A.wzBlankTariff()},{margin:'',tday:'',tnight:'',tax:'',vat:'',incl:'false'});
const handler=header.split("sel.addEventListener('change'")[1].split('});')[0];
assert(handler.includes('wzBlankTariff()')&&handler.indexOf('wzBlankTariff()')<handler.indexOf('WZ.country=sel.value'));
// Skip on the devices step posts only the step (a mistyped meter address cannot block it)
assert(/else if\(s===6\)\{if\(skip\)await wzPost\(\{setup_step:'7'\}\);else await wzSaveDevices\(\);\}/.test(header));
assert(!header.includes("fmt(p.cost_eur,2)+' €'"),'plan cost must use the zone currency');
// stale/invalid weather is a warning
const noWx=JSON.parse(JSON.stringify(good));noWx.weather={valid:false};
assert.equal(A.wzSummary(noWx).find(x=>x.key==='location').state,'warn');
console.log('test_setup_wizard OK ('+literals.length+' literals checked)');
// manual coordinates without a city still count as a placed location
{const manual=JSON.parse(JSON.stringify(good));manual.site.region.city='';
 const loc=A.wzSummary(manual).find(x=>x.key==='location');
 assert.equal(loc.state,'ok');assert(loc.detail.includes('61.4638,24.0760'));}
console.log('manual-location summary ok');

// ---- comfort step: target, limits, profile ---------------------------------
{
  const ok=(v,mode)=>A.wzComfortFields({target:'21',min:'19',max:'23',profile:'saving',...v},mode);
  let r=ok({},3);
  assert(r.ok);assert.equal(r.target,21);
  assert.deepEqual({...r.fields},{optimization_profile:'saving',comfort_min_c:'19',comfort_max_c:'23'});
  // limits are optional and clear when empty
  r=ok({min:'',max:''},3);assert(r.ok);assert.equal(r.fields.comfort_min_c,'');assert.equal(r.fields.comfort_max_c,'');
  // Finnish decimal comma and half-degree steps
  r=ok({target:'21,5'},3);assert(r.ok);assert.equal(r.target,21.5);
  assert(!ok({target:'21.3'},3).ok);                       // 0.5 steps only
  assert(!ok({target:'15'},3).ok);                         // Asuttu is 16..26
  assert(ok({target:'15',min:'',max:''},2).ok);            // Ylläpito allows 5..24
  assert(!ok({target:'25'},2).ok);
  assert(!ok({target:'21'},0).ok);                         // no mode that has a target
  assert(ok({target:''},0).ok);                            // but limits/profile can still be saved
  // limits must stay in range, ordered, >= 1 C apart and outside target +-0.5
  for(const bad of [{min:'4'},{max:'27'},{min:'19.3'},{min:'abc'},{min:'23',max:'19'},{min:'20',max:'20.5'},
                    {min:'20.8'},{max:'21.2'},{profile:'eco'},{profile:''}])
    assert(!ok(bad,3).ok,JSON.stringify(bad));
  assert(ok({min:'20.5',max:'21.5'},3).ok);                // exactly 0.5 C either side is allowed
  assert.deepEqual([...A.WZ_PROFILES.map(p=>p.id)],['comfort','balanced','saving']);
}
// The three profile descriptions never promise a saving figure.
for(const p of A.WZ_PROFILES)assert(!/%|€|EUR/.test(p.text),p.id);
console.log('comfort step helpers ok');

// ---- devices step: sensors and meter ---------------------------------------
{
  assert.equal(A.wzAgeText(3),'3 s');assert.equal(A.wzAgeText(125),'2 min');assert.equal(A.wzAgeText(7300),'2 h');
  for(const bad of [NaN,-1,undefined,null,Infinity])assert.equal(A.wzAgeText(bad),'—');
  assert.equal(A.wzSensorLine({temperature_c:21.43,humidity_pct:45.4,battery_v:2.987,rssi_dbm:-62}),'21,4 °C · 45 % · 2,99 V · -62 dBm');
  assert.equal(A.wzSensorLine({}),'— · — · — · —');
  assert.equal(A.wzSensorState({live:true}),'live');
  assert.equal(A.wzSensorState({live:false}),'stale');assert.equal(A.wzSensorState({}),'stale');   // unknown is never "live"
  assert.equal(A.wzSignalText(-60),'Hyvä signaali');assert.equal(A.wzSignalText(-75),'Kohtalainen signaali');
  assert.equal(A.wzSignalText(-90),'Heikko signaali');assert.equal(A.wzSignalText(NaN),'Signaali tuntematon');
  // replacing a bound INDOOR sensor needs confirmation; first binding / outdoor / same MAC do not
  const some={bound:{indoor:'AA:AA:AA:AA:AA:01',outdoor:null}};
  assert(A.wzBindNeedsConfirm(some,'indoor','AA:AA:AA:AA:AA:02'));
  assert(!A.wzBindNeedsConfirm(some,'indoor','AA:AA:AA:AA:AA:01'));
  assert(!A.wzBindNeedsConfirm(some,'outdoor','AA:AA:AA:AA:AA:02'));
  assert(!A.wzBindNeedsConfirm({bound:{indoor:null}},'indoor','AA:AA:AA:AA:AA:02'));
  assert(!A.wzBindNeedsConfirm(null,'indoor','AA:AA:AA:AA:AA:02'));
  // Shelly rows explain why a device cannot be used; they never read as supported
  assert.equal(A.wzShellyText({support:'SUPPORTED',app:'Plus1PM',power_w:412.7}),'Plus1PM · 413 W');
  assert(A.wzShellyText({support:'SUPPORTED',app:'Plus1PM',power_w:null}).includes('teholukemaa'));
  assert(A.wzShellyText({support:'AUTH_REQUIRED',id:'shellyplus1pm-x'}).includes('salasana'));
  assert(A.wzShellyText({support:'GEN1_NOT_SUPPORTED',id:'SHSW-PM'}).includes('Gen1'));
  assert(A.wzShellyText({support:'whatever',id:'x'}).includes('Ei tunnistettu'));
  // meter fields: URL before scope, scope only with a URL, unchanged values are not resent
  const none={shelly_url:'',shelly_scope:'unknown'};
  assert.deepEqual({...A.wzDevicesFields({shelly:'',scope:'heat_pump'},none)},{setup_step:'7'});
  let f=A.wzDevicesFields({shelly:' http://192.168.1.30 ',scope:'heat_pump'},none);
  assert.deepEqual(Object.keys(f),['setup_step','shelly_url','shelly_scope']);
  assert.equal(f.shelly_url,'http://192.168.1.30');assert.equal(f.shelly_scope,'heat_pump');
  const have={shelly_url:'http://192.168.1.30',shelly_scope:'heat_pump'};
  assert.deepEqual({...A.wzDevicesFields({shelly:'http://192.168.1.30',scope:'heat_pump'},have)},{setup_step:'7'});
  f=A.wzDevicesFields({shelly:'http://192.168.1.30',scope:'whole_house'},have);
  assert.equal(f.shelly_scope,'whole_house');assert(!('shelly_url' in f));
  assert.equal(A.wzDevicesFields({shelly:'http://192.168.1.31',scope:'bogus'},have).shelly_scope,'unknown');   // bad scope -> unknown
  assert.deepEqual({...A.wzDevicesFields({shelly:'',scope:'heat_pump'},have)},{setup_step:'7',shelly_url:''}); // clearing the meter
  // A newly chosen meter must not inherit a prior pump-only confirmation.
  const draft=vm.runInContext('WZ',ctx);
  draft.shelly=have.shelly_url;draft.shellyScope='heat_pump';
  draft.shellyScopeFor=have.shelly_url;draft.shellyScopeInitialized=true;
  A.wzSetShellyAddress('http://192.168.1.31');
  assert.equal(draft.shellyScope,'unknown');
  assert.equal(A.wzDevicesFields({shelly:draft.shelly,scope:draft.shellyScope},have).shelly_scope,'unknown');
  draft.shellyScope='heat_pump'; // user explicitly confirms the replacement
  A.wzSetShellyAddress('http://192.168.1.31');
  assert.equal(draft.shellyScope,'heat_pump'); // same URL is not a new meter
  A.wzSetShellyAddress('http://192.168.1.32');
  assert.equal(draft.shellyScope,'unknown'); // manually typed replacement also resets
  // summary: a meter that is not confirmed as pump-only is a warning, not ok
  const meter=(scope,w)=>A.wzSummary({site:{region:{},shelly_scope:scope},status:{air:{},shelly_w:w},weather:null,plan:null}).find(x=>x.key==='meter');
  assert.equal(meter('heat_pump',300).state,'ok');
  assert.equal(meter('whole_house',300).state,'warn');assert.equal(meter('unknown',300).state,'warn');
  assert.equal(meter('unknown',null).state,'optional');
}
console.log('devices step helpers ok');

(async()=>{
  // P2 #57: failure of site NVS must never durably/actively change the target.
  // On air-write failure after site save, compensate old profile and band.
  const events=[];
  ctx.state.site={optimization:{profile:'balanced',comfort_min_c:10,comfort_max_c:14},
    region:{setup_step:5}};
  ctx.state.status={air:{mode:2,hold_target_c:12,occupied_target_c:21,drying_delta_c:3}};
  Object.assign(A.WZ.comfort,{target:'13',min:'9',max:'16',profile:'saving'});
  ctx.airPostLocal=async()=>{events.push('air');};
  ctx.reloadStatus=async()=>{events.push('reload');};
  ctx.wzPost=async()=>{throw Error('NVS');};
  await assert.rejects(A.wzSaveComfort(false),/NVS/);
  assert.deepEqual(events,[],'site-write failure must not change target');
  ctx.wzPost=async f=>{events.push({...f});};
  await A.wzSaveComfort(false);
  assert.equal(events[0].optimization_profile,'saving');
  assert.deepEqual(events.slice(1),['air','reload']);
  events.length=0;
  ctx.airPostLocal=async()=>{events.push('air');throw Error('AIR_NVS');};
  ctx.fetchJSON=async()=>{events.push('readback');return {air:ctx.state.status.air};};
  await assert.rejects(A.wzSaveComfort(false),/AIR_NVS/);
  assert.equal(events.length,4,'site write + mode error + readback + restore site');
  assert.equal(events[2],'readback');
  assert.equal(events[3].optimization_profile,'balanced');
  assert.equal(events[3].comfort_min_c,'10');
  assert.equal(events[3].comfort_max_c,'14');
  // Ambiguous HTTP response: committed target is visible on status readback.
  // Undo would pair a new target with the OLD limits (fresh Codex P2 #57).
  events.length=0;
  // Simulate stale shared state (a periodic status request superseded another).
  // Only the isolated fresh API result may prove a new durable target.
  ctx.fetchJSON=async()=>{events.push('readback');return {air:{...ctx.state.status.air,hold_target_c:13}};};
  ctx.reloadStatus=async()=>{events.push('reload');};
  await A.wzSaveComfort(false);
  assert.deepEqual(events.slice(1),['air','readback']);
  assert.equal(events.length,3,'do not compensate a confirmed successful target save');
  // If Wi-Fi is lost and readback is impossible, don't blindly undo either.
  events.length=0;
  ctx.state.status.air.hold_target_c=12;
  ctx.fetchJSON=async()=>{events.push('readback');throw Error('WiFi lost');};
  await assert.rejects(A.wzSaveComfort(false),/unverified/);
  assert.equal(events.length,3,'unknown air outcome cannot trigger rollback');
  // The API can return the old RAM target after a partial NVS commit.
  // It must NOT be mistaken for a durable rollback and must NOT trigger
  // compensating site writes that would corrupt the new limits.
  events.length=0;
  ctx.state.status={air:{mode:2,hold_target_c:12,occupied_target_c:21,drying_delta_c:3,
    config_storage_verified:false}};
  ctx.fetchJSON=async()=>{events.push('readback');return {air:ctx.state.status.air};};
  await assert.rejects(A.wzSaveComfort(false),/unverified/);
  assert.equal(events.length,3,'unverified NVS rollback must not undo site settings');
  console.log('wizard rollback-order and ambiguous-response tests OK');
})().catch(e=>{console.error(e);process.exitCode=1;});
