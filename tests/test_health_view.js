/* Freshness / offline / OTA-summary helpers: pure logic in a VM + dictionary completeness.
 * Rendering is covered by the browser run documented in docs/OTA_AND_OFFLINE.md. */
"use strict";
const fs=require('fs'),vm=require('vm'),assert=require('node:assert/strict'),path=require('path');
const header=fs.readFileSync(path.join(__dirname,'../firmware/cottage/include/cottage_premium_dashboard.hpp'),'utf8');
const m=/<script id="health-script">([\s\S]*?)<\/script>/.exec(header);
assert(m,'health script block missing');
const src=m[1];

const dict=JSON.parse(header.split('const UI_EN=Object.freeze(')[1].split(');\nconst UI_EN_FRAGMENTS')[0]);
const literals=[...src.matchAll(/FT\('((?:[^'\\]|\\.)*)'\)/g)].map(x=>x[1].replace(/\\"/g,'"'));
assert(literals.length>50);
assert.deepEqual(literals.filter(l=>!(l in dict)),[],'missing English translations');
for(const id of ['fresh-card','firmware-update-card']){
  const card=header.split('id="'+id+'"')[1].split('</article>')[0];
  for(const c of [...card.matchAll(/>([^<>]+)</g)].map(x=>x[1].trim()).filter(x=>/[äöÄÖ]/.test(x)))
    assert(c in dict,'static caption not translated ('+id+'): '+c);
}
const mainSrc=header.split('async function reloadFreshness')[1].split('async function reloadPlan')[0];
for(const lit of [...mainSrc.matchAll(/'([^'\n]*[äöÄÖ][^'\n]*)'/g)].map(x=>x[1]))
  assert(lit in dict||Object.keys(dict).some(k=>k.length>=8&&lit.includes(k)),'literal not translated: '+lit);
// the offline card must never promise more than the controller does
const fresh=header.split('id="fresh-card"')[1].split('</article>')[0];
assert(fresh.includes('ei käytetä uutena tietona')&&fresh.includes('pumpun oma säädin ja turvatoiminnot toimivat aina'));

const ctx={document:{},state:{}};
vm.createContext(ctx);
vm.runInContext(src+';this.api={frValidate,frSource,frSourceText,frOverall,otaSummary,otaLastUpdateText};',ctx);
const A=ctx.api;

const fmt={time:u=>'T'+u,age:s=>s+'s',error:c=>'E'+c,kib:b=>Math.round(b/1024)+' KiB'};
const src_=(id,state,o={})=>({id,state,last_ok_utc:0,valid_until_utc:0,age_s:-1,error:0,...o});
const report=(o={})=>({clock_ok:true,wifi_ok:true,offline:false,fallback:'price_control',control_armed:false,last_contact_utc:1000,next_expiry_utc:5000,
  sources:[src_('prices','ok',{valid_until_utc:5000,last_ok_utc:900}),src_('weather','ok',{valid_until_utc:6000,last_ok_utc:800}),
           src_('sensor','ok',{age_s:20}),src_('pump','ok',{age_s:5})],...o});

// validation
assert(A.frValidate(report()));
for(const bad of [null,{},{fallback:'x'},{fallback:'x',sources:[]},{...report(),sources:report().sources.slice(0,3)},
    {...report(),sources:report().sources.slice().reverse()},{...report(),fallback:5}])
  assert.equal(A.frValidate(bad),null);

// every source state is explained, with its own times, never as fresh when it is not
const p=(state,o)=>A.frSourceText(src_('prices',state,{last_ok_utc:100,valid_until_utc:200,error:104,...o}),fmt);
assert.equal(p('ok').dot,'ok');assert(p('ok').detail.includes('T200'));
assert.equal(p('short').dot,'warn');assert(p('short').detail.includes('termostaattiin'));
assert.equal(p('stale').dot,'bad');assert(p('stale').detail.includes('T100')&&p('stale').detail.includes('36 h'));
assert.equal(p('expired').dot,'bad');assert(p('expired').detail.includes('uutena tietona'));
assert.equal(p('error').dot,'bad');assert(p('error').detail.includes('E104')&&p('error').detail.includes('T100'));   // reason AND last good time
assert.equal(p('missing').dot,'bad');assert.equal(p('not_configured').dot,'off');
assert.equal(p('error',{last_ok_utc:0}).detail.includes('Viimeisin onnistunut'),false);                          // no invented last-good time
const w=(state)=>A.frSourceText(src_('weather',state,{last_ok_utc:100,valid_until_utc:200}),fmt);
assert.equal(w('ok').dot,'ok');assert.equal(w('expired').dot,'warn');assert(w('expired').detail.includes('ei käytetä'));
assert.equal(w('not_configured').dot,'off');
const sen=(state,age)=>A.frSourceText(src_('sensor',state,{age_s:age}),fmt);
assert(sen('ok',20).detail.includes('20s'));assert.equal(sen('stale',900).dot,'bad');
assert(sen('stale',900).detail.includes('900s')&&sen('stale',900).detail.includes('Lämmitysehdotusta ei tehdä'));
assert(!sen('stale',-1).detail.includes('viimeisin'));                                                         // unknown age is not shown
const pu=(state,age)=>A.frSourceText(src_('pump',state,{age_s:age}),fmt);
assert.equal(pu('stale',600).dot,'warn');assert.equal(pu('ok',5).dot,'ok');assert.equal(pu('not_configured',-1).dot,'off');

// overall statement mirrors the controller's fallback
let o=A.frOverall(report({control_armed:true}),fmt);
assert.equal(o.kind,'ok');assert.equal(o.headline,'Ohjaustiedot ovat tuoreita');
let disabled=A.frOverall(report(),fmt);
assert.equal(disabled.kind,'warn');assert(disabled.headline.includes('Vain lämmitysehdotus'));
assert(disabled.detail.includes('omilla asetuksillaan')&&disabled.detail.includes('ei lähetä komentoja'));
let linkDown=A.frOverall(report({control_armed:true,sources:report().sources.map(x=>x.id==='pump'?{...x,state:'stale'}:x)}),fmt);
assert(linkDown.detail.includes('yhteys ei ole todennetusti kunnossa'));
o=A.frOverall(report({offline:true,wifi_ok:false}),fmt);
assert.equal(o.kind,'warn');assert(o.detail.includes('T5000')&&o.detail.includes('Wi-Fi-pumppuyhteyttä')); // cached data is not working pump control
o=A.frOverall(report({offline:true,wifi_ok:false,next_expiry_utc:0}),fmt);
assert(o.detail.includes('Ei Wi-Fi-yhteyttä'));
o=A.frOverall(report({fallback:'thermostat',control_armed:true}),fmt);
assert.equal(o.kind,'warn');assert(o.detail.includes('±0,5'));
assert(!o.detail.includes('huone pidetään'));
assert(A.frOverall(report({fallback:'thermostat'}),fmt).headline.includes('Vain lämmitysehdotus'));
o=A.frOverall(report({fallback:'blocked',offline:true,wifi_ok:false}),fmt);
assert.equal(o.kind,'bad');assert(o.detail.includes('omalla säätimellään')&&o.detail.includes('ei lähetä komentoja'));
o=A.frOverall(report({clock_ok:false,fallback:'thermostat'}),fmt);
assert(o.detail.includes('Kellonaikaa ei ole synkronoitu'));

// OTA summary: one honest sentence per real device state
const ota=(o={})=>({ota_signed_pull_enabled:true,ota_signed_status:0,ota_signed_good_version:20261101,ota_signed_candidate:20261101,
  ota_last_check_utc:1000,ota_dashboard_pending:false,ota_downloaded_bytes:0,...o});
assert.equal(A.otaSummary(ota({ota_signed_pull_enabled:false}),fmt).kind,'bad');
assert(A.otaSummary(ota({ota_signed_pull_enabled:false}),fmt).detail.includes('ilman allekirjoitettua'));
assert.equal(A.otaSummary(ota({ota_dashboard_pending:true}),fmt).kind,'busy');
let r=A.otaSummary(ota({ota_signed_status:5,ota_downloaded_bytes:512*1024}),fmt);
assert.equal(r.kind,'busy');assert(r.detail.includes('512 KiB')&&r.detail.includes('edellinen toimiva versio säilyy'));
assert.equal(A.otaSummary(ota({ota_signed_status:6}),fmt).kind,'busy');
assert.equal(A.otaSummary(ota({ota_signed_status:2}),fmt).kind,'warn');
assert(A.otaSummary(ota({ota_signed_status:3}),fmt).detail.includes('Mitään ei ole asennettu'));
r=A.otaSummary(ota({ota_signed_status:4}),fmt);
assert.equal(r.kind,'bad');assert(r.detail.includes('ei asenneta'));
assert.equal(A.otaSummary(ota({ota_last_check_utc:0}),fmt).headline,'Ei vielä tarkistettu');
r=A.otaSummary(ota(),fmt);
assert.equal(r.kind,'ok');assert(r.detail.includes('20261101'));
r=A.otaSummary(ota({ota_signed_candidate:20261119}),fmt);                       // seen but not installable: never "available"
assert.equal(r.kind,'info');assert(r.detail.includes('20261119')&&r.headline.includes('ei ole asennettavissa'));
assert.equal(A.otaSummary(ota({ota_signed_good_version:0,ota_signed_candidate:0}),fmt).detail.includes('ei vielä ole kirjattu'),true);
for(const s of [0,2,3,4,5,6]){const rr=A.otaSummary(ota({ota_signed_status:s}),fmt);assert(rr.headline&&rr.detail,'status '+s);}
// last update
assert.equal(A.otaLastUpdateText({ota_last_update_utc:1234,ota_last_update_version:20261119},fmt),'T1234 · julkaisu 20261119');
assert.equal(A.otaLastUpdateText({ota_last_update_utc:1234,ota_last_update_version:0},fmt),'T1234');
for(const bad of [{},{ota_last_update_utc:null},{ota_last_update_utc:0},{ota_last_update_utc:'x'}])
  assert(A.otaLastUpdateText(bad,fmt).startsWith('Ei tiedossa'));
console.log('test_health_view OK ('+literals.length+' literals checked)');
