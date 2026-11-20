/* History view helpers: pure logic in a VM + dictionary completeness.
 * SVG rendering is covered by the browser run documented in docs/HISTORY.md. */
"use strict";
const fs=require('fs'),vm=require('vm'),assert=require('node:assert/strict'),path=require('path');
const header=fs.readFileSync(path.join(__dirname,'../firmware/cottage/include/cottage_premium_dashboard.hpp'),'utf8');
const m=/<script id="history-script">([\s\S]*?)<\/script>/.exec(header);
assert(m,'history script block missing');
const src=m[1];

const dict=JSON.parse(header.split('const UI_EN=Object.freeze(')[1].split(/\);\r?\nconst UI_EN_FRAGMENTS/)[0]);
const literals=[...src.matchAll(/HT\('([^']*)'\)/g)].map(x=>x[1]);
assert(literals.length>15);
assert.deepEqual(literals.filter(l=>!(l in dict)&&l!=='Wi-Fi'),[],'missing English translations');
// static view text
const view=header.split('id="view-history"')[1].split('<section class="view" id="view-settings">')[0];
for(const c of [...view.matchAll(/>([^<>]+)</g)].map(x=>x[1].trim()).filter(Boolean))
  assert(c in dict,'static history caption not translated: '+c);
for(const a of [...view.matchAll(/aria-label="([^"]+)"/g)].map(x=>x[1]))assert(a in dict,'aria-label: '+a);
// strings built in the main script's showHistoryView
const mainSrc=header.split('async function reloadHistoryView')[1].split('async function reloadPlan')[0];
for(const lit of [...mainSrc.matchAll(/'([^'\n]*[äöÄÖ][^'\n]*)'/g)].map(x=>x[1]))
  assert(lit in dict||[...Object.keys(dict)].some(k=>k.length>=8&&lit.includes(k)),'history literal not translated: '+lit);
// the view never invents data
assert(!/interpol/i.test(src.replace(/\/\/.*$/gm,'')),'history helpers must not interpolate');

const ctx={document:{},state:{}};
vm.createContext(ctx);
vm.runInContext(src+';this.api={hsValidate,hsSegments,hsExtent,hsDuration,hsEventText,hsEventClass,hsCoverageText,hsLowCoverage,hsCsv,hsEnergyText,hsAxisTicks,hsAxisDaily,HS_RANGES};',ctx);
const A=ctx.api;

const step=3600,T0=1794999600;
const pt=(i,o={})=>({t:T0+i*step,n:4,a:2,f:0,os:1,r:0,in:21,rh:45,out:-3,sp:22.5,w:400,scope:'heat_pump',power_coverage_minutes:15,p:0.1,pc:'EUR',...o});
const good={valid:true,step_s:step,currency:'EUR',meter_scope:'unknown',persistent:true,slots_total:8,slots_present:6,
  kwh:3.2,kwh_slots:6,points:[pt(0),pt(1),pt(2),pt(3),pt(4),pt(5),pt(6),pt(7)],events:[]};
assert(A.hsValidate(good));
for(const bad of [null,{},{...good,valid:false},{...good,points:[pt(0)]},{...good,step_s:0},{...good,step_s:60},
    {...good,points:[pt(0),{...pt(1),t:T0+2*step}]},{...good,points:[pt(0),{...pt(1),t:'x'}]},{...good,events:null}])
  assert.equal(A.hsValidate(bad),null,JSON.stringify(bad).slice(0,70));

// gaps: lines break at missing values and at forecast/measured changes; never bridged
const pts=[pt(0),pt(1),pt(2,{in:null}),pt(3,{in:null}),pt(4),pt(5,{in:22}),pt(6,{in:null}),pt(7)];
const segs=A.hsSegments(pts,'in');
assert.deepEqual([...segs.map(s=>s.pts.length)],[2,2,1]);
assert.equal(segs[1].pts[0].t,T0+4*step);                       // starts after the hole, not at the hole's edge
const out=[pt(0,{out:-3,f:1}),pt(1,{out:-3,f:1}),pt(2,{out:-2,f:0}),pt(3,{out:-2,f:0}),pt(4,{out:null,f:0})];
const split=A.hsSegments(out,'out','f');
assert.deepEqual([...split.map(s=>[s.tag,s.pts.length])],[[1,2],[0,2]]);   // forecast and measured are different lines
assert.deepEqual([...A.hsSegments([pt(0,{in:null}),pt(1,{in:null})],'in')],[]);
assert.deepEqual([...A.hsSegments([],'in')],[]);

// extent: only present values, never zero-height, null when nothing exists
const ex=A.hsExtent(pts,['in']);
assert(ex.lo<21&&ex.hi>22);
assert.equal(A.hsExtent([pt(0,{in:null,out:null})],['in','out']),null);
const flat=A.hsExtent([pt(0),pt(1)],['in']);assert(flat.hi-flat.lo>0.9);

// durations and events
assert.equal(A.hsDuration(45),'45 s');assert.equal(A.hsDuration(300),'5 min');
assert.equal(A.hsDuration(3*3600),'3 h');assert.equal(A.hsDuration(4.5*3600),'4,5 h');assert.equal(A.hsDuration(3*86400),'3 vrk');
for(const bad of [NaN,-1,undefined])assert.equal(A.hsDuration(bad),'—');
const dur=A.hsDuration;
assert.equal(A.hsEventText({k:2,s:1,v:0},dur),'Wi-Fi katkesi');
assert.equal(A.hsEventText({k:3,s:2,v:300},dur),'Lämpöpumpun tiedonsiirto palasi, katko kesti 5 min');
assert.equal(A.hsEventText({k:2,s:3,v:0},dur),'Huonelämpötila-anturi katkesi');
assert(A.hsEventText({k:1,s:0,v:3},dur).endsWith('3)'));
assert(A.hsEventText({k:4,s:0,v:2},dur).endsWith(': 2'));
assert(A.hsEventText({k:6,s:0,v:1},dur).includes('epäonnistui'));
assert.equal(A.hsEventText({k:99,s:0,v:0},dur),'Tuntematon tapahtuma');
assert.equal(A.hsEventText({k:2,s:77,v:0},dur),'? katkesi');         // unknown source is not hidden
assert.deepEqual(['2','6','3','5','1','4'].map(k=>A.hsEventClass({k:Number(k)})),['bad','bad','ok','ok','info','info']);

// coverage is honest: a half-empty window is flagged
assert(A.hsCoverageText(good).includes('75 %')&&A.hsCoverageText(good).includes('6 / 8'));
assert.equal(A.hsLowCoverage(good),true);                          // 75 % < 80 %
assert.equal(A.hsLowCoverage({slots_total:10,slots_present:9}),false);
assert.equal(A.hsLowCoverage({slots_total:0,slots_present:0}),false);
assert.equal(A.hsCoverageText({}),'—');

// energy: unknown when nothing was metered, labelled by what the meter measures
const kwh=v=>v.toFixed(1).replace('.',',');
assert.equal(A.hsEnergyText({kwh:null},kwh),'Ei kulutuslukemia tältä ajalta.');
assert(A.hsEnergyText({kwh:3.2,kwh_slots:6,meter_scope:'heat_pump'},kwh).includes('vain lämpöpumppua'));
assert(A.hsEnergyText({kwh:3.2,kwh_slots:6,meter_scope:'whole_house'},kwh).includes('koko talon'));
assert(A.hsEnergyText({kwh:3.2,kwh_slots:6,meter_scope:'unknown'},kwh).includes('ei ole varmistettu'));
assert(A.hsEnergyText({kwh:0,kwh_slots:6,meter_scope:'unknown'},kwh).startsWith('0,0 kWh'));
// Codex P1 (#60): a target change inside the range is never summed or relabelled
const mixed=A.hsEnergyText({kwh:null,kwh_slots:6,meter_scope:'mixed',current_meter_scope:'heat_pump'},kwh);
assert(mixed.includes('vaihtunut')&&!mixed.includes('kWh')&&!mixed.includes('vain lämpöpumppua'),mixed);
// the label follows the range's scope, not the owner's present statement
assert(A.hsEnergyText({kwh:3.2,kwh_slots:6,meter_scope:'whole_house',current_meter_scope:'heat_pump'},kwh).includes('koko talon'));   // a real zero is a zero

// CSV: exactly the shown data, empty cells for missing, forecast flag only when an outdoor value exists
const csv=A.hsCsv({...good,points:[pt(0),pt(1,{in:null,out:null,w:null,sp:null,p:null,a:1,r:1,f:1})]});
const lines=csv.trim().split('\n');
assert.equal(lines.length,3);
const cols=lines[0].split(',');
assert(lines[0].startsWith('utc_start,records,indoor_c'));
for(const name of ['price_currency','price_per_kwh','power_scope','power_coverage_min','outdoor_source'])
  assert(cols.includes(name),name+' must survive CSV export');
const at=(line,name)=>line.split(',')[cols.indexOf(name)];
assert.equal(at(lines[1],'price_currency'),'EUR');
assert.equal(at(lines[1],'power_scope'),'heat_pump');
assert.equal(at(lines[1],'power_coverage_min'),'15');
assert.equal(at(lines[1],'outdoor_source'),'measured');
assert.equal(at(lines[1],'price_per_kwh'),'0.1');
assert.equal(at(lines[2],'price_per_kwh'),'');
assert.equal(at(lines[2],'power_coverage_min'),'');
const csvMixed=A.hsCsv({...good,currency:'mixed',points:[
  pt(0,{pc:'SEK',p:1}),pt(1,{pc:'EUR',p:0.1})]});
assert(csvMixed.includes(',1,SEK,')&&csvMixed.includes(',0.1,EUR,'),
  'CSV must keep SEK and EUR on their own rows even when site now uses EUR');
assert(!csv.includes('NaN')&&!csv.includes('null')&&!csv.includes('undefined'));

// axis ticks: local 6-hour boundaries (or midnights on long ranges)
const hour=u=>Math.floor(((u%86400)/3600+2)%24);
const t6=A.hsAxisTicks(T0,T0+48*3600,3600,hour);
assert(t6.length>=7&&t6.length<=9);for(const t of t6)assert.equal(hour(t)%6,0);
assert.equal(A.hsAxisDaily(T0,T0+48*3600,3600),false);assert.equal(A.hsAxisDaily(T0,T0+7*86400,3600),true);
const t30=A.hsAxisTicks(T0,T0+30*86400,10800,hour);
assert(t30.length>=29&&t30.length<=31);
// 3 h buckets starting off midnight (UTC+2): the tick is the first bucket of the new local day
for(const t of t30)assert(hour(t)<=hour(t-10800)&&hour(t)<3);
const t7=A.hsAxisTicks(T0,T0+7*86400,3600,hour);assert(t7.length>=6&&t7.length<=7);for(const t of t7)assert.equal(hour(t),0);
assert.deepEqual([...A.HS_RANGES.map(r=>r.id)],['24h','7d','30d']);
console.log('test_history_view OK ('+literals.length+' literals checked)');
