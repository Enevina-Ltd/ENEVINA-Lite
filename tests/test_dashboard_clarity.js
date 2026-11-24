'use strict';
const assert=require('node:assert/strict'),vm=require('node:vm'),fs=require('node:fs'),path=require('node:path');
const header=fs.readFileSync(path.join(__dirname,'../firmware/cottage/include/cottage_premium_dashboard.hpp'),'utf8');
const code=header.split('<script id="clarity-script">')[1].split('</script>')[0];
const tl=header.split('<script id="timeline-script">')[1].split('</script>')[0];
const hs=header.split('<script id="history-script">')[1].split('</script>')[0];
assert(!/method\s*:\s*['"]POST|\/api\/(control|ota|settings|midea)/.test(code));
class Element {
  constructor(tag){this.tag=tag;this.children=[];this.attrs={};this.style={};this.dataset={};this.handlers={};this.textContent='';this.clientWidth=360;}
  append(...n){this.children.push(...n);}
  replaceChildren(...n){this.children=n;}
  setAttribute(k,v){this.attrs[k]=String(v);}
  addEventListener(k,fn){this.handlers[k]=fn;}
}
const elements=new Map(),get=id=>{if(!elements.has(id))elements.set(id,new Element('div'));return elements.get(id);};
const state={},ctx={document:{createElement:t=>new Element(t),createElementNS:(_n,t)=>new Element(t)},state,uiLanguage:'fi',uiLocale:()=>ctx.uiLanguage==='en'?'en-GB':'fi-FI',regionTimeZone:()=>'Europe/Helsinki',
  fmt:(v,d=1)=>Number.isFinite(v)?v.toFixed(d):'—',text:(id,v)=>get(id).textContent=v,$:get,priceFmt:v=>v.toFixed(2),priceDisplayMinorUnit:()=>'snt',console};
vm.createContext(ctx);
vm.runInContext(tl+'\n'+hs+'\n'+code+';this.A={clAxis,clSeries,clRuns,clBars,clLine,clPlanRows,clLivePlan,clNextHeat,clEnergy,clExplain,clCurrentTimeline,clPriceData,clRenderStatus,clRenderHistory,clRenderPlan,clReloadHomeHistory};',ctx);
const A=ctx.A,now=Math.floor(Date.now()/1000),T=Math.floor(now/900)*900;
for(const values of [[.1,.2],[-.1,.2],[-.2,-.1],[0,0],[.0001,.0002]]){
 const a=A.clAxis(values,true,.01);
 assert(a.lo<=0&&a.hi>=0&&a.hi>a.lo&&a.ticks.length>=2);
 assert(a.lo<=Math.min(...values)+1e-9&&a.hi>=Math.max(...values)-1e-9);
}
const rows=A.clSeries([{utc:T,spot:-.1,class:'cheap'},{utc:T+900,spot:.2,class:'expensive'},{utc:T+1800,spot:0,class:'neutral'},{utc:T+3600,spot:null},{utc:T+4500,spot:.05},{utc:T+4500,spot:.1}],900,'spot');
assert.deepEqual(Array.from(rows,r=>r.v),[-.1,.2,0,null,null]);
assert.equal(A.clRuns(rows,900).length,1);
const runGap=A.clRuns(A.clSeries([{utc:T,spot:.1},{utc:T+1800,spot:.2}],900,'spot'),900);
assert.equal(runGap.length,2);
const host=new Element('div');
A.clBars(host,rows,{unit:'snt/kWh',label:'Price',format:x=>String(x),inspect:p=>String(p.v),now:T,classes:true});
const svg=host.children[0],rects=svg.children.filter(e=>e.tag==='rect'&&e.attrs.class);
const zero=Number(svg.children.find(e=>e.tag==='line'&&e.attrs.class==='cl-zero').attrs.y1);
assert.equal(rects.length,3);
assert.equal(Number(rects[0].attrs.y),zero);assert(Number(rects[0].attrs.height)>0);
assert(Number(rects[1].attrs.y)<zero);assert.equal(Number(rects[2].attrs.height),0);
assert.equal(rects[0].attrs.class,'cl-price-cheap');assert.equal(rects[1].attrs.class,'cl-price-expensive');
assert(Math.abs(Number(rects[1].attrs.height)/Number(rects[0].attrs.height)-2)<1e-9);
assert(host.children.some(n=>n.className==='cl-inspect'));
const slider=host.children.find(n=>n.className==='cl-inspect').children[1];
slider.value='1';slider.handlers.input();assert.equal(host.dataset.clSelected,String(T+900));
A.clBars(host,rows,{unit:'snt/kWh',label:'Price',format:x=>String(x),inspect:p=>String(p.v),now:T,classes:true});
assert.equal(host.children.find(n=>n.className==='cl-inspect').children[1].value,'1');
const forecast=new Element('div');
A.clLine(forecast,A.clSeries([{utc:T,temperature_c:5},{utc:T+900,temperature_c:6},{utc:T+2700,temperature_c:4}],900,'temperature_c'),{step:900,label:'Forecast',forecast:true});
assert.equal(forecast.children[0].children.filter(e=>e.tag==='polyline').length,1);
assert(forecast.children[0].children.some(e=>e.attrs.class==='cl-line forecast'));
assert(forecast.children[0].children.some(e=>e.tag==='circle'));
const plan={valid:true,computed_utc:T,slots:[false,true,true,false].map((heat,i)=>({utc:T+i*900,heat}))};
const active={control_source:'MPC',mpc_fallback:'NONE',mpc_plan_computed_utc:T};
assert.equal(A.clLivePlan(plan,active,now).length,4);
assert.deepEqual(JSON.parse(JSON.stringify(A.clNextHeat(plan.slots))),{start:T+900,end:T+2700});
assert.equal(A.clLivePlan(plan,{...active,mpc_fallback:'PLAN_INPUTS_CHANGED'},now).length,0);
assert.equal(A.clLivePlan(plan,{...active,mpc_plan_computed_utc:T+1},now).length,0);
assert.equal(A.clLivePlan(plan,active,T+900).length,0);
assert.equal(A.clLivePlan({...plan,slots:[plan.slots[0],plan.slots[2]]},active,now).length,0);
assert.equal(A.clLivePlan(plan,null,now).length,0);
const meter={range:'24h',meter_scope:'heat_pump',kwh:0,pump_cost:0,pump_cost_currency:'EUR',power_coverage_minutes:180,pump_cost_minutes:180,slots_total:96};
assert.equal(A.clEnergy(meter).kwh,0);assert.equal(A.clEnergy(meter).cost,0);
assert.equal(A.clEnergy({...meter,meter_scope:'whole_house'}).cost,null);
assert.equal(A.clEnergy({...meter,meter_scope:'mixed'}).kwh,null);
assert.equal(A.clEnergy({...meter,pump_cost_currency:'mixed'}).cost,null);
assert.equal(A.clEnergy({...meter,power_coverage_minutes:0}).kwh,null);
assert.equal(A.clEnergy({...meter,range:'7d'}),null);
assert.match(A.clExplain({control_armed:true,control_gate:'AWAITING_READBACK'}),/tilalukua/);
assert.match(A.clExplain({control_armed:true,bootstrap_reason:'THERMOSTAT_NO_PRICES'}),/huonetermostaatin/);
ctx.uiLanguage='en';assert.match(A.clExplain({control_armed:true,control_gate:'DEFROST_HOLD'}),/defrost/);
const region={price_region_resolved:true,nordpool_area:'FI',currency:'EUR'};
state.site={region,pump_adapter:'mitsubishi_cn105_esphome'};
state.status={spot:.02,total_price:.08,price_area:'FI',price_currency:'EUR',pump_requested_mode:null,cn105_sse_climate_recent:true,cn105_sse_mode:'HEAT'};
state.timeline={valid:true,area:'FI',currency:'EUR',segments:[],slots:[{utc:T,total:.08,class:'cheap'},{utc:T+900,total:.1,class:'neutral'}]};
state.prices={valid:true,area:'FI',currency:'EUR',slots:[{utc:T,spot:.02,total:.08}]};state.priceMarketKey='FI|EUR';
assert.equal(A.clPriceData(now,24).classified,true);
state.timeline.currency='SEK';assert.equal(A.clPriceData(now,24).classified,false);assert.equal(A.clPriceData(now,24).rows.length,1);
state.prices.area='SE3';assert.equal(A.clPriceData(now,24).rows.length,0);
state.status.spot=null;state.timeline.currency='EUR';state.status.total_price=null;assert.equal(A.clPriceData(now,24).rows.length,0);
A.clRenderStatus();assert.equal(get('cl-pump').textContent,'—','a null new-API mode may not be replaced with stale SSE');
state.status=null;A.clRenderStatus();assert.equal(get('cl-pump').textContent,'—');
const ids=[...header.matchAll(/\bid="([^"]+)"/g)].map(m=>m[1]).filter(x=>!x.includes('$')&&!x.includes("'"));
for(const id of ['cl-temperature-chart','cl-plan-chart','cl-power-chart','cl-cost','cl-target','home-temp','view-details','technical-notice'])assert.equal(ids.filter(x=>x===id).length,1,id);
console.log('PASS clarity: true zero, negative and zero prices, gaps, keyboard selection, stale plans, region fencing, meter scopes, bilingual explanations, null pump state');
