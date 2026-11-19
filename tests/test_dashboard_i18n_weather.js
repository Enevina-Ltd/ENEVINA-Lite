"use strict";
// The actual flash-backed dashboard script, exercised without live pump writes.
const fs=require('fs'),vm=require('vm'),assert=require('node:assert/strict'),path=require('path');
const header=fs.readFileSync(path.join(__dirname,'../firmware/cottage/include/cottage_premium_dashboard.hpp'),'utf8');
const script=header.split('<script>').at(-1).split('</script>')[0].split('\ninitialRefresh();')[0];
const elementCache=new Map();
function element(id) {
  return {id,value:'',textContent:'',className:'',dataset:{},disabled:false,
    attributes:{},children:[],cells:[],handlers:{},style:{},
    classList:{toggle(){},add(){},remove(){}},
    setAttribute(key,value){this.attributes[key]=value;},
    getAttribute(key){return this.attributes[key]??null;},
    removeAttribute(){},replaceChildren(){this.children=[];},
    append(...nodes){this.children.push(...nodes);},
    addEventListener(type,handler){this.handlers[type]=handler;},
    insertRow(){const x=element('row');this.children.push(x);return x;},
    insertCell(){const x=element('cell');this.cells.push(x);return x;}
  };
}
function get(id){if(!elementCache.has(id))elementCache.set(id,element(id));return elementCache.get(id);}
const nav=['home','prices','weather','plan','settings'].map(name=>Object.assign(element('nav-'+name),{dataset:{view:name}}));
const views=['home','prices','weather','plan','settings'].map(name=>element('view-'+name));
const modes=[0,1,2,3].map(n=>Object.assign(element('mode-'+n),{dataset:{mode:String(n)}}));
const languages=['fi','en'].map(code=>Object.assign(element('lang-'+code),{dataset:{lang:code}}));
const originalNodes=['Sääennuste','Ylläpitolämpö','Asetukset','Kastepiste'].map(nodeValue=>({
  nodeValue,parentElement:{closest(){return null;}}
}));
const placeholder=element('placeholder');placeholder.setAttribute('placeholder','Syötä vuosikulutus');
const document={
  body:{},documentElement:{lang:'fi'},title:'Enevina Lite | Energianhallinta',activeElement:null,
  getElementById:get,
  querySelectorAll(selector){
    if(selector==='[data-mode]')return modes;
    if(selector==='[data-view]')return nav;
    if(selector==='.view')return views;
    if(selector==='[data-lang]')return languages;
    if(selector==='[aria-label],[placeholder],[title]')return [placeholder];
    return [];
  },
  createTreeWalker(){let i=0;return {nextNode(){return originalNodes[i++]||null;}};},
  createElement:element,createElementNS:(_ns,name)=>element(name)
};
const store=new Map();
const context={document,NodeFilter:{SHOW_TEXT:4},window:{addEventListener(){}},
  localStorage:{getItem:key=>store.get(key)||null,setItem:(key,value)=>store.set(key,value)},
  URLSearchParams,Date,console,location:{hash:''},history:{replaceState(){}},setInterval(){},
  fetch:async()=>({ok:true,json:async()=>({valid:false,slots:[]})})};
vm.createContext(context);
vm.runInContext(script,context,{filename:'dashboard_i18n_weather.js'});
const run=x=>vm.runInContext(x,context);
assert.equal(document.documentElement.lang,'fi');
assert.equal(originalNodes[0].nodeValue,'Sääennuste');
run("setDashboardLanguage('en')");
assert.equal(document.documentElement.lang,'en');
assert.equal(store.get('enevina_lite_language'),'en');
assert.equal(originalNodes[0].nodeValue,'Weather forecast');
assert.equal(originalNodes[1].nodeValue,'Maintenance');
assert.equal(originalNodes[2].nodeValue,'Settings');
assert.equal(placeholder.getAttribute('placeholder'),'Enter annual consumption');
assert.equal(document.title,'Enevina Lite | Energy Management');
assert.equal(run('translateFi("Automaattiohjaus on kytketty. Käskyjä lähetetään Ylläpitolämpö- tai Asuttu-tilassa, kun mittaus- ja yhteysportit ovat kunnossa.")'),
 'Automatic control is enabled. Commands are sent in Maintenance or Occupied mode when sensor and connection checks pass.');
assert.equal(run('translateFi("RuuviTag ei ole tuore tai anturia ei ole sidottu")'),'RuuviTag is stale or no sensor is bound');
assert.equal(run('translateFi("5,00 snt/kWh")'),'5,00 c/kWh');
// Weather preview: four future hourly points from real FMI-style API data.
const first=Math.ceil(Date.now()/3600000)*3600;
const weather={valid:true,fetched_utc:first-1800,slots:[1,2,3,4].map((v,i)=>({
  utc:first+i*3600,temperature_c:[-4,-2,1,0][i]
}))};
run('state.weather='+JSON.stringify(weather)+';showWeather()');
assert.equal(get('weather-next').textContent,'-4.0 °C');
assert.equal(get('weather-min').textContent,'-4.0 °C');
assert.equal(get('weather-max').textContent,'1.0 °C');
assert.equal(get('weather-rows').children.length,2);
assert.ok(get('weather-chart').children.length>=2);
assert.match(get('home-weather-summary').textContent,/4 ennustetuntia/);
// A valid endpoint flag with no usable slots must not manufacture data.
run('state.weather={valid:true,slots:[{utc:0,temperature_c:NaN}]};showWeather()');
assert.equal(get('weather-next').textContent,'—');
assert.equal(get('weather-rows').children.length,1);
run("setDashboardLanguage('fi')");
assert.equal(document.documentElement.lang,'fi');
assert.equal(originalNodes[0].nodeValue,'Sääennuste');
assert.equal(originalNodes[1].nodeValue,'Ylläpitolämpö');
assert.equal(placeholder.getAttribute('placeholder'),'Syötä vuosikulutus');
assert.equal(document.title,'Enevina Lite | Energianhallinta');
assert.equal(store.get('enevina_lite_language'),'fi');
console.log('PASS bilingual FI/EN switch, persisted choice, original text, weather chart and missing-data handling');
