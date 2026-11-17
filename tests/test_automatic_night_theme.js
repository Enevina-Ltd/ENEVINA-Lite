/* Real dashboard clock-based automatic theme tested without browser/network. */
'use strict';
const fs=require('fs'),vm=require('vm'),assert=require('node:assert/strict');
const ui=fs.readFileSync(require('path').join(__dirname,'../firmware/cottage/include/cottage_premium_dashboard.hpp'),'utf8');
assert.equal((ui.match(/<script>/g)||[]).length,2);
const source=ui.split('<script>')[1].split('</script>')[0];
assert(source.includes('Europe/Helsinki'));
assert(source.includes('setInterval(applyAutomaticNightTheme,60000)'));
assert(source.includes("!document.hidden"));
assert(!source.includes('fetch('));
let now=Date.parse('2026-11-17T19:05:00Z');
const doc={documentElement:{dataset:{}},hidden:false,handlers:{},
  querySelector(selector){assert.equal(selector,'meta[name="theme-color"]');
    return {setAttribute(name,value){assert.equal(name,'content');doc.themeColor=value}};},
  addEventListener(event,handler){doc.handlers[event]=handler;}};
let timer,focus,systemDark=false;
class FixedDate extends Date {
  constructor(...a){super(...(a.length?a:[now]));}
  static now(){return now;}
}
const context={document:doc,window:{addEventListener(event,callback){
    if(event==='focus')focus=callback;},
    matchMedia(){return {matches:systemDark};}},
  Date:FixedDate,Intl,Number,Error,setInterval(fn,ms){assert.equal(ms,60000);timer=fn;}};
vm.runInNewContext(source,context,{filename:'automatic-night.js'});
assert.equal(doc.documentElement.dataset.theme,'night','21:05 in Helsinki must be dark');
assert.equal(doc.themeColor,'#0c1a17');
now=Date.parse('2026-11-17T09:00:00Z');
timer();
assert.equal(doc.documentElement.dataset.theme,'day','11:00 in Helsinki must be light');
assert.equal(doc.themeColor,'#f5f5f0');
now=Date.parse('2026-06-15T18:55:00Z');
focus();
assert.equal(doc.documentElement.dataset.theme,'night','DST: 21:55 Helsinki dark');
now=Date.parse('2026-06-15T04:00:00Z');
doc.hidden=false;doc.handlers.visibilitychange();
assert.equal(doc.documentElement.dataset.theme,'day','DST: 07:00 Helsinki light');
now=Date.parse('2026-01-15T04:59:00Z');
timer();
assert.equal(doc.documentElement.dataset.theme,'night','Winter: 06:59 Helsinki night');
now=Date.parse('2026-01-15T05:00:00Z');
timer();
assert.equal(doc.documentElement.dataset.theme,'day','Winter: 07:00 Helsinki day');
now=Date.parse('2000-01-01T00:00:00Z');systemDark=true;
timer();
assert.equal(doc.documentElement.dataset.theme,'night','Bad clock: use device dark preference');
console.log('PASS automatic Finnish day/night transitions, DST, wake, invalid-clock fallback');
