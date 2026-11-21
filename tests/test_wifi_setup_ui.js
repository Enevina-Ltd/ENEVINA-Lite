'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

const source = fs.readFileSync(path.join(__dirname, '../firmware/cottage/src/cottage_wifi_page.cpp'), 'utf8');
const script = source.match(/<script>([\s\S]*?)<\/script>/)[1];
const dashboard = fs.readFileSync(path.join(__dirname,'../firmware/cottage/include/cottage_premium_dashboard.hpp'),'utf8');
const dashboardLanguage = dashboard.match(/let uiLanguage=\(\(\)=>\{[\s\S]*?\}\)\(\);/)[0];

function setupPage(lang) {
  const nodes = new Map();
  function element() {
    return {
      value:'', hidden:true, disabled:false, dataset:{},
      setAttribute(){}, append(child){this.children.push(child)},
      replaceChildren(...children){this.children=children},
      children:[],
    };
  }
  function get(id) {
    if (!nodes.has(id)) nodes.set(id, element());
    return nodes.get(id);
  }
  const responses = [];
  const requests = [];
  const pendingTimers = [];
  const context = vm.createContext({
    document:{
      getElementById:get, documentElement:{lang:'fi'},
      querySelectorAll:()=>[], createElement:element,
    },
    localStorage:{getItem:()=>lang,setItem(){}},
    URL, URLSearchParams,
    setTimeout:(callback)=>pendingTimers.push(callback),
    fetch:async (url, options={})=>{
      requests.push({url,options});
      if (url==='/api/wifi/scan') return {ok:true,json:async()=>({networks:[]})};
      assert.equal(responses[0]?.url,url);
      const {body, status=200}=responses.shift();
      return {ok:status<300,status,json:async()=>body};
    },
  });
  const next = (url,body,status)=>responses.push({url,body,status});
  return {get,context,next,requests,pendingTimers};
}

async function checkLanguage(lang) {
  const {get,context,next,requests,pendingTimers}=setupPage(lang);
  const status='/api/wifi/status';
  const retry='/api/wifi/retry-save';
  const connected={
    phase:'ONLINE',last_result:'CONNECTED',network:' Test Wi-Fi ',
    setup_ap_active:true,token:'settings-token',url_numeric:'http://192.168.1.23/',
  };
  // Reload the page after the connection succeeded but NVS did not.
  next(status,{...connected,storage:'FAILED'});
  await vm.runInContext(script,context);
  await new Promise(setImmediate);
  assert.equal(context.document.documentElement.lang,lang);
  assert.equal(get('done').hidden,true);
  assert.equal(get('retry-save').hidden,false);
  assert.match(get('msg').textContent,lang==='fi'?/tallennus epäonnistui/:/could not be saved/);
  next(retry,{accepted:true},202);
  next(status,{...connected,storage:'PENDING'});
  await get('retry-save').onclick();
  assert.equal(get('done').hidden,true);
  assert.equal(requests.find(r=>r.url===retry).options.body.get('token'),'settings-token');
  next(status,{...connected,storage:'SAVED'});
  await pendingTimers.shift()();
  assert.equal(get('done').hidden,false);
  assert.equal(get('donenet').textContent,' Test Wi-Fi ');
  const handoffUrl = new URL(get('urls').children[0].href);
  assert.equal(handoffUrl.href,'http://192.168.1.23/?ui_lang='+lang);
  const dashboardContext = vm.createContext({
    location:handoffUrl, URLSearchParams,
    localStorage:{setItem:(key,value)=>assert.equal(value,lang)},
    history:{replaceState:(_state,_title,url)=>assert.equal(url,'/')},
  });
  assert.equal(vm.runInContext(dashboardLanguage+'\nuiLanguage',dashboardContext),lang);
}

(async ()=>{
  await checkLanguage('fi');
  await checkLanguage('en');
  console.log('Wi-Fi setup UI: failed save, retry, confirmed save and language handoff passed (FI/EN)');
})().catch(error=>{console.error(error);process.exitCode=1});
