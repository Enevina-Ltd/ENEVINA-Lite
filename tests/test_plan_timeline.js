/* Plan timeline helpers: pure logic in a VM + dictionary completeness.
 * SVG rendering is covered by the browser run documented in docs/PLAN_TIMELINE.md. */
"use strict";
const fs=require('fs'),vm=require('vm'),assert=require('node:assert/strict'),path=require('path');
const header=fs.readFileSync(path.join(__dirname,'../firmware/cottage/include/cottage_premium_dashboard.hpp'),'utf8');
const m=/<script id="timeline-script">([\s\S]*?)<\/script>/.exec(header);
assert(m,'timeline script block missing');
const src=m[1];

const dict=JSON.parse(header.split('const UI_EN=Object.freeze(')[1].split(');\nconst UI_EN_FRAGMENTS')[0]);
const literals=[...src.matchAll(/TT\('([^']*)'\)/g)].map(x=>x[1]);
assert(literals.length>25);
assert.deepEqual(literals.filter(l=>!(l in dict)),[],'missing English translations');
// static card text must be translated too
const card=header.split('id="timeline-card"')[1].split('</article>')[0];
for(const c of [...card.matchAll(/>([^<>]+)</g)].map(x=>x[1].trim()).filter(Boolean))
  assert(c in dict,'static timeline caption not translated: '+c);
// the page must never promise a prediction it cannot make
assert(!/ennustettu (sisä|huone)lämpötila|kustannusarvio:|säästö/i.test(card.replace('ei näytetä','')),'timeline card makes a forbidden claim');
assert(card.includes('ei käytä sitä päätöksessään'),'outdoor-not-used note missing');

const ctx={document:{},state:{}};
vm.createContext(ctx);
vm.runInContext(src+';this.api={tlValidate,tlLayout,tlSlotIndex,tlOutdoorAt,tlControlText,tlNowText,tlSegmentText,tlTicks,tlDeg};',ctx);
const A=ctx.api;

const slot=(i,total,cls='neutral')=>({utc:1794999600+i*900,total,class:cls,median:0.2});
const good={valid:true,slots:[slot(0,0.1,'cheap'),slot(1,0.3,'expensive'),slot(2,0.2)],segments:[]};
assert(A.tlValidate(good));
for(const bad of [null,{},{valid:false,slots:good.slots,segments:[]},{valid:true,slots:[slot(0,0.1)],segments:[]},
    {valid:true,slots:[slot(0,0.1),{...slot(1,0.2),utc:1794999600+1800}],segments:[]},   // gap
    {valid:true,slots:[slot(0,0.1),{...slot(1,NaN)}],segments:[]},
    {valid:true,slots:[slot(0,0.1),{...slot(1,0.2),class:5}],segments:[]},
    {valid:true,slots:good.slots}])
  assert.equal(A.tlValidate(bad),null,JSON.stringify(bad).slice(0,60));

// layout: bars within the plot, baseline at zero, negative prices handled
const L=A.tlLayout(good.slots,720,270,{left:46,right:42,top:16,bottom:46});
assert.equal(L.plotW,632);assert.equal(L.plotH,208);
assert(Math.abs(L.x(L.t0)-46)<1e-9&&Math.abs(L.x(L.t1)-678)<1e-9);
assert(L.y(0)>L.y(0.3));assert(Math.abs(L.y(0)-(16+208))<1e-9);
const neg=A.tlLayout([slot(0,-0.05),slot(1,0.2)],720,270,{left:46,right:42,top:16,bottom:46});
assert(neg.lo===-0.05&&neg.y(0)<neg.y(-0.05));
const flat=A.tlLayout([slot(0,0.2),slot(1,0.2)],720,270,{left:46,right:42,top:16,bottom:46});
assert(Number.isFinite(flat.y(0.2)));                        // flat prices do not divide by zero
// hit testing
assert.equal(A.tlSlotIndex(L,good.slots,46),0);assert.equal(A.tlSlotIndex(L,good.slots,677),2);
assert.equal(A.tlSlotIndex(L,good.slots,10),-1);assert.equal(A.tlSlotIndex(L,good.slots,700),-1);

// outdoor forecast: interpolate, never bridge gaps or extrapolate far
const wx={valid:true,slots:[{utc:1794999600,temperature_c:0},{utc:1795003200,temperature_c:2},{utc:1795014000,temperature_c:-4}]};
assert.equal(A.tlOutdoorAt(wx,1794999600+1800),1);
assert.equal(A.tlOutdoorAt(wx,1795003200),2);
assert.equal(A.tlOutdoorAt(wx,1795003200+3600),null);        // 3 h hole between points: no bridge
assert.equal(A.tlOutdoorAt(wx,1794999600-3600),0);            // within 90 min before the first point
assert.equal(A.tlOutdoorAt(wx,1794999600-7200),null);
assert.equal(A.tlOutdoorAt(wx,1795014000+7200),null);
assert.equal(A.tlOutdoorAt(null,1794999600),null);
assert.equal(A.tlOutdoorAt({valid:false,slots:wx.slots},1794999600),null);   // invalid forecast is not used

// controller texts reflect the profile headroom
assert(A.tlControlText('cheap',{preheat_c:1.0,coast_c:1.0}).includes('+1,0 °C'));
assert(A.tlControlText('cheap',{preheat_c:0,coast_c:0}).includes('vain, jos huone on tavoitetta kylmempi'));
assert(A.tlControlText('expensive',{preheat_c:0.7,coast_c:0.7}).includes('0,7 °C'));
assert(A.tlControlText('expensive',{preheat_c:0,coast_c:0}).includes('ei lämmitetä'));
assert(A.tlControlText('neutral',{}).includes('±0,5'));
assert(A.tlControlText('unknown',{}).includes('ei ole riittävästi'));

// "now" line uses real reasons, setpoint only when heating
assert.equal(A.tlNowText({action:'HEAT_SETPOINT',setpoint_c:22.5,reason:'LOW_COST_WINDOW'}),'Lämmitys päällä 22,5 °C — sähkö on nyt edullista');
assert.equal(A.tlNowText({action:'OFF',setpoint_c:null,reason:'HIGH_COST_WINDOW'}),'Lämmitys pois — sähkö on nyt kallista');
assert.equal(A.tlNowText({action:'WAT',reason:'???'}),'Ei päätöstä — syy ei tiedossa');
assert.equal(A.tlNowText(null),'Ei päätöstä');

// segment sentence carries the real numbers
const seg={class:'cheap',start:1794999600,end:1795007100,avg_total:0.1,avg_median:0.2,slots:7};
const txt=A.tlSegmentText(seg,{preheat_c:1,coast_c:1},v=>(v*100).toFixed(1)+' c','x–y'.split('–').length?((a,b)=>a+'..'+b):null);
assert(txt.startsWith('Edullinen jakso '));assert(txt.includes('10.0 c')&&txt.includes('50 %')&&txt.includes('alempi'));
const dear=A.tlSegmentText({...seg,class:'expensive',avg_total:0.3,avg_median:0.2},{preheat_c:0,coast_c:0},v=>String(v),(a,b)=>a+'..'+b);
assert(dear.startsWith('Kallis jakso ')&&dear.includes('50 %')&&dear.includes('korkeampi'));
assert(A.tlSegmentText({...seg,avg_median:0},{},v=>String(v),(a,b)=>a+'..'+b).includes('keskihinta'));   // zero median: no division

// time axis: 6-hour local ticks at hour boundaries only
const hour=utc=>Math.floor(((utc%86400)/3600+2)%24);          // UTC+2 for the test
const Lt=A.tlLayout(Array.from({length:96},(_,i)=>slot(i,0.2)),720,270,{left:46,right:42,top:16,bottom:46});
const ticks=A.tlTicks(Lt,null,hour);
assert(ticks.length>=3&&ticks.length<=5);
for(const t of ticks){assert.equal(hour(t)%6,0);assert.equal(t%3600,0);}
assert.equal(A.tlDeg(21.04),'21,0');assert.equal(A.tlDeg(-3.26),'-3,3');
for(const k of ['Profiili','Tavoite','Rajat','Säästö','Tasapainoinen','Mukavuus','Rajoja ei ole asetettu (tavoite ±0,5 °C)','Tavoitelämpötilaa ei ole tässä tilassa'])assert(k in dict,'chip label not translated: '+k);
console.log('test_plan_timeline OK ('+literals.length+' literals checked)');
