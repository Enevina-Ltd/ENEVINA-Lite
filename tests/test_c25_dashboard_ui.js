/* Node VM regression for the actual firmware dashboard JS, no browser/ESP required. */
"use strict";
const fs=require('fs'),vm=require('vm'),assert=require('node:assert/strict');
const html=fs.readFileSync(require('path').join(__dirname,'../firmware/cottage/include/cottage_premium_dashboard.hpp'),'utf8');
let source=html.split('<script>')[1].split('</script>')[0];
assert(source.includes('function showStatus'));
source=source.split('\ninitialRefresh();')[0]; // avoid starting network activity on test bootstrap

function el(id){
  return {id, value:'', textContent:'', className:'', disabled:false, dataset:{},
    cells:[],children:[],handlers:{},style:{},
    classList:{toggle(){},add(){},remove(){}},
    setAttribute(){},removeAttribute(){},replaceChildren(){this.children=[]},
    append(...e){this.children.push(...e)},appendChild(e){this.children.push(e)},
    addEventListener(event,fn){this.handlers[event]=fn},
    insertRow(){const row=el('row');this.children.push(row);return row},
    insertCell(){const cell=el('cell');this.cells.push(cell);return cell}
  };
}
const elements=new Map();
const $=id=>{if(!elements.has(id))elements.set(id,el(id));return elements.get(id)};
const modes=[0,1,2].map(i=>Object.assign(el('mode'+i),{dataset:{mode:String(i)}}));
const views=['home','prices','plan','settings'].map(x=>Object.assign(el('view-'+x),{id:'view-'+x}));
const nav=['home','prices','plan','settings'].map(x=>Object.assign(el('nav-'+x),{dataset:{view:x}}));
const document={activeElement:null,getElementById:$,querySelectorAll(s){
  if(s==='[data-mode]')return modes;
  if(s==='[data-view]')return nav;
  if(s==='.view')return views;
  return [];
},createElement:el};
let offline=false,configured=null,pauseSave=false,resolveSave=null,pauseStatus=false,settleStatus=null;
const status={air:{sensor_fresh:true,mode:0,temperature_c:21,age_s:2,rh_pct:53,
    dewpoint_c:11.0,outside_temperature_c:2.5,hold_target_c:12,drying_delta_c:4,
    config_token:'test'},
    spot:0.10,total_price:0.18,cn105:true,shelly_w:300,
    wifi_rssi_dbm:-62,free_internal_heap:100000,free_psram:7000000,
    firmware_version:'test',ota_upload_enabled:false,market_last_fetch_utc:1794900000};
const context={document,location:{hash:''},history:{replaceState(){}},
  Date,URLSearchParams,console,setInterval(){},
  fetch:async(path,opt)=>{
    if(offline)throw Error('offline');
    if(path==='/api/status'){
      if(pauseStatus){
        // Snapshot as though the endpoint replied with its earlier values.
        const old=JSON.parse(JSON.stringify(status));
        return new Promise((resolve,reject)=>{settleStatus={
          ok:()=>resolve({ok:true,json:async()=>old}),
          fail:()=>reject(Error('stale status error'))
        };});
      }
      return {ok:true,json:async()=>status};
    }
    if(path==='/api/air/config'){
      configured=String(opt.body);
      if(pauseSave)return new Promise(resolve=>{resolveSave=()=>resolve({ok:true})});
      return{ok:true};
    }
    return {ok:true,json:async()=>({})};
  }
};
vm.createContext(context);
vm.runInContext(source,context,{filename:'dashboard.js'});
const run=x=>vm.runInContext(x,context);
(async()=>{
  run('state.status=('+JSON.stringify(status)+');showStatus()');
  assert.equal($('home-dew').textContent,'11,0 °C');
  assert.equal($('device-cn105').textContent,'Tuore');
  assert.equal($('setting-hold').value,12);
  modes[2].handlers.click();
  $('setting-hold').value='18';
  $('setting-hold').handlers.input();
  run('showStatus()');
  assert.equal(run('state.mode'),2);
  assert.equal($('setting-hold').value,'18');
  assert.equal($('setting-delta').value,4);
  await $('save-mode').handlers.click();
  assert.match(configured,/mode=2/);
  assert.match(configured,/hold=18/);
  assert.equal(run('state.settingsDirty'),false);
  // Codex P2: submitting 18 C must not wipe edits to 20 C made while
  // the POST response is pending. Nor may refresh unlock a second POST.
  pauseSave=true;
  modes[2].handlers.click();
  $('setting-hold').value='18';
  $('setting-hold').handlers.input();
  const inFlight=$('save-mode').handlers.click();
  assert.equal(run('state.settingsSaving'),true);
  assert.equal($('save-mode').disabled,true);
  const submitted=configured;
  await $('save-mode').handlers.click();
  assert.equal(configured,submitted,'second POST must be blocked');
  $('setting-hold').value='20';
  $('setting-hold').handlers.input();
  modes[1].handlers.click();
  assert.equal(run('state.settingsDirty'),true);
  assert.equal(typeof resolveSave,'function');
  resolveSave();
  await inFlight;
  assert.match(configured,/hold=18/);
  assert.match(configured,/mode=2/);
  assert.equal($('setting-hold').value,'20','newer unsaved draft retained');
  assert.equal(run('state.mode'),1);
  assert.equal(run('state.settingsDirty'),true);
  assert.equal(run('state.settingsSaving'),false);
  pauseSave=false;
  await $('save-mode').handlers.click();
  assert.match(configured,/hold=20/);
  assert.match(configured,/mode=1/);
  assert.equal(run('state.settingsDirty'),false);
  // Codex P2: a periodic status request that began BEFORE the settings POST
  // may complete after the post-save refresh. It must not restore old 0/12C.
  pauseStatus=true;
  const staleSuccess=run('reloadStatus()');
  assert.equal(typeof settleStatus.ok,'function');
  pauseStatus=false;
  modes[2].handlers.click();
  $('setting-hold').value='18';
  $('setting-hold').handlers.input();
  status.air.mode=2;status.air.hold_target_c=18;
  await $('save-mode').handlers.click();
  assert.equal(run('state.mode'),2);
  assert.equal(String($('setting-hold').value),'18');
  assert.equal(run('state.settingsDirty'),false);
  settleStatus.ok();
  await staleSuccess;
  assert.equal(run('state.mode'),2,'older success must not restore mode 0');
  assert.equal(String($('setting-hold').value),'18','older success must not restore 12C');
  assert.equal(run('state.settingsDirty'),false);
  // Stale failures also must not erase a newer successful refresh.
  pauseStatus=true;
  const staleFailure=run('reloadStatus()');
  const rejectOld=settleStatus.fail;
  pauseStatus=false;
  await run('reloadStatus()');
  rejectOld();
  await staleFailure;
  assert.equal($('device-cn105').textContent,'Tuore','stale failure must not hide fresh CN105');
  assert.equal(String($('setting-hold').value),'18');
  assert.equal(run('state.mode'),2);
  offline=true;
  await run('reloadStatus()');
  for(const id of ['home-dew','home-outside','device-cn105','device-shelly','device-wifi','device-ram','home-rh','home-watts'])
    assert.notEqual($(id).textContent,'Tuore','stale '+id);
  assert.equal($('home-dew').textContent,'—');
  assert.equal($('home-outside').textContent,'—');
  assert.equal($('device-cn105').textContent,'Ei yhteyttä');
  assert.equal($('save-mode').disabled,true);
  offline=false;
  await run('reloadStatus()');
  assert.equal($('device-cn105').textContent,'Tuore');
  assert.equal($('home-dew').textContent,'11,0 °C');
  console.log('PASS dashboard mid-POST edits, out-of-order status success/failure, stale clearance and recovery');
})().catch(e=>{console.error(e);process.exitCode=1});
