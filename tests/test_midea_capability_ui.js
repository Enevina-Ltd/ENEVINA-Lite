/* Exercise the actual Midea dashboard script against different device profiles. */
'use strict';
const fs=require('fs'),path=require('path'),vm=require('vm'),assert=require('node:assert/strict');
const html=fs.readFileSync(path.join(__dirname,'../firmware/cottage/include/cottage_premium_dashboard.hpp'),'utf8');
const source=html.slice(html.indexOf('function showMideaProtocolFields()'),html.indexOf('let mideaDiscoveryResults=[];'));
function element(id){return {id,value:'',textContent:'',disabled:false,hidden:false,dataset:{},children:[],handlers:{},
  setAttribute(k,v){this[k]=String(v)},removeAttribute(k){delete this[k]},
  appendChild(v){this.children.push(v)},append(...v){this.children.push(...v)},
  replaceChildren(...v){this.children=[...v]},addEventListener(k,v){this.handlers[k]=v},
  get options(){return this.children}};}
const elements=new Map(),$=id=>{if(!elements.has(id))elements.set(id,element(id));return elements.get(id)};
let offline=false,submitted=null,holdNextRefresh=false,resolveHeldRefresh=null;
const basic={selected:true,configured:true,fresh:true,protocol:3,power:true,mode:4,target_c:22,fan:40,swing:0,
  pilot_approved:true,pilot_min_c:16,pilot_max_c:30,control_busy:false,control_pending:false,
  read_code:'ok',control_code:'not_requested',state_known:['power','mode','target_c','fan','swing','eco','sleep','freeze_protection'],
  eco:false,sleep:false,freeze_protection:false,
  profile:{family:'legacy',protocol_version:0,subtype_known:true,subtype:1,model_known:false,model:''},
  capabilities:{source:'reported',reported:true,modes:[2,4,5],fans:[40,60,80,102],swings:[0,12],temperature_step:0.5,
    temp_limits:{'2':{known:true,min_c:17,max_c:30},'4':{known:true,min_c:16,max_c:30}},
    controls:['power','mode','target_c','fan','swing','eco','sleep','freeze_protection']},
  control_policy:{general_available:true,property_available:false,general_block:'',requires_freeze_choice:false}};
let current=JSON.parse(JSON.stringify(basic));
const context={document:{createElement:element},$,console,URLSearchParams,Date,
  state:{status:{air:{config_token:'test'}},site:{pump_adapter:'midea_lan'}},
  text:(id,value)=>{$(id).textContent=value==null?'—':String(value)},fmt:v=>String(v),time:v=>String(v),
  uiLocale:()=> 'en-GB',regionTimeZone:()=> 'UTC',translateFi:v=>v,
  confirm:()=>true,
  fetchJSON:async()=>{
    if(offline)throw Error('offline');
    const snapshot=JSON.parse(JSON.stringify(current));
    if(holdNextRefresh){holdNextRefresh=false;return new Promise(resolve=>{resolveHeldRefresh=()=>resolve(snapshot)})}
    return snapshot;
  },
  fetch:async(url,options)=>{submitted={url,body:String(options.body)};return {ok:true,json:async()=>({queued:true})}}};
vm.createContext(context);vm.runInContext(source,context,{filename:'midea-dashboard.js'});
const run=s=>vm.runInContext(s,context);
const values=id=>$(id).children.map(x=>x.value);
const refresh=()=>run('reloadMidea()');
(async()=>{
  // A model that does not report auto/dry/horizontal swing must not offer them.
  await refresh();
  assert.deepEqual(values('midea-command-mode'),['','2','4','5'],'modes must come from this device');
  assert.deepEqual(values('midea-command-fan'),['','40','60','80','102']);
  assert.deepEqual(values('midea-command-swing'),['','0','12']);
  assert.equal(String($('midea-command-temperature').min),'16');
  assert.equal(String($('midea-command-temperature').max),'30');
  assert.equal(String($('midea-command-temperature').step),'0.5');
  assert.equal($('midea-field-eco').hidden,false);
  assert.equal($('midea-field-display').hidden,true);
  assert.equal($('midea-command-send').disabled,true,'an empty draft is not a command');

  // Refresh must preserve a valid unsaved draft and clear a newly unsupported one.
  $('midea-command-mode').value='2';$('midea-command-temperature').value='24.5';
  $('midea-command-fan').value='60';
  await refresh();
  assert.equal($('midea-command-mode').value,'2');
  assert.equal($('midea-command-temperature').value,'24.5');
  assert.equal($('midea-command-fan').value,'60');
  assert.equal(String($('midea-command-temperature').min),'17','draft mode selects its own range');
  assert.equal($('midea-command-send').disabled,false);
  current.capabilities.modes=[4,5];current.capabilities.fans=[40,80,102];
  current.capabilities.temperature_step=1;
  await refresh();
  assert.equal($('midea-command-mode').value,'');
  assert.equal($('midea-command-fan').value,'');
  assert.equal($('midea-command-temperature').value,'','unsupported half-degree draft is cleared');
  current.pilot_min_c=16.5;current.pilot_max_c=29.5;
  await refresh();
  assert.equal(String($('midea-command-temperature').min),'17','approved ranges must align to actual device steps');
  assert.equal(String($('midea-command-temperature').max),'29');

  // Changing mode can require a target in the destination mode's narrower
  // range. Show that target in the draft and submit both fields together.
  current=JSON.parse(JSON.stringify(basic));current.target_c=30;
  current.capabilities.temp_limits['2']={known:true,min_c:18,max_c:26};
  await refresh();
  $('midea-command-temperature').value='';$('midea-command-mode').value='2';
  $('midea-command-mode').handlers.change();
  assert.equal($('midea-command-temperature').value,'26','mode changes propose a valid destination target');
  assert.equal($('midea-command-send').disabled,false);
  $('midea-command-temperature').value='';$('midea-command-temperature').handlers.input();
  assert.equal($('midea-command-send').disabled,true,'clearing a required target cannot send an invalid mode-only command');
  $('midea-command-temperature').value='26';$('midea-command-temperature').handlers.input();
  await $('midea-command-send').handlers.click();
  assert.equal(new URLSearchParams(submitted.body).get('mode'),'2');
  assert.equal(new URLSearchParams(submitted.body).get('target_c'),'26');
  current.target_c=22.5;current.capabilities.temperature_step=1;
  await refresh();
  $('midea-command-temperature').value='';$('midea-command-mode').value='2';
  $('midea-command-mode').handlers.change();
  assert.equal($('midea-command-temperature').value,'23','mode changes also align an inherited half-degree target');
  $('midea-command-temperature').value='24';
  $('midea-command-mode').handlers.change();
  assert.equal($('midea-command-temperature').value,'24','a valid explicit target survives a mode change');
  $('midea-command-mode').value='';$('midea-command-temperature').value='';

  // Dry exits normally choose Auto, but a Low/High-only device requires an
  // explicit supported fan. Preserve the user's choice and never send 102.
  current=JSON.parse(JSON.stringify(basic));current.mode=3;current.fan=40;
  current.capabilities.modes=[2,3,4,5];current.capabilities.fans=[40,80,100];
  current.capabilities.temp_limits['3']={known:true,min_c:16,max_c:30};
  await refresh();
  assert(values('midea-command-mode').includes('4'));
  $('midea-command-mode').value='4';$('midea-command-mode').handlers.change();
  assert.equal($('midea-command-fan').value,'40','Dry exit proposes the supported current fan');
  assert.equal($('midea-command-send').disabled,false);
  $('midea-command-fan').value='';$('midea-command-fan').handlers.change();
  assert.equal($('midea-command-send').disabled,true,'a required explicit fan cannot be removed');
  $('midea-command-fan').value='80';$('midea-command-fan').handlers.change();
  await $('midea-command-send').handlers.click();
  const dryBody=new URLSearchParams(submitted.body);
  assert.equal(dryBody.get('mode'),'4');assert.equal(dryBody.get('fan'),'80');
  assert.notEqual(dryBody.get('fan'),'102');
  current.fan=102;await refresh();
  $('midea-command-mode').value='2';$('midea-command-mode').handlers.change();
  assert.equal($('midea-command-fan').value,'40','an unsupported current fan uses the first supported choice');
  $('midea-command-fan').value='80';$('midea-command-fan').handlers.change();
  $('midea-command-mode').value='4';$('midea-command-mode').handlers.change();
  assert.equal($('midea-command-fan').value,'80','valid explicit fan drafts survive mode changes');
  $('midea-command-mode').value='';$('midea-command-fan').value='';

  // Selecting an exclusive preset clears only another enabled draft; it does
  // not fabricate observed state or send unrequested explicit false values.
  current=JSON.parse(JSON.stringify(basic));current.capabilities.controls.push('turbo');
  await refresh();
  $('midea-command-eco').value='true';$('midea-command-eco').handlers.change();
  $('midea-command-sleep').value='false';$('midea-command-sleep').handlers.change();
  $('midea-command-turbo').value='true';$('midea-command-turbo').handlers.change();
  assert.equal($('midea-command-eco').value,'','enabling Turbo clears a conflicting Eco draft');
  assert.equal($('midea-command-sleep').value,'false','an explicit false draft is retained');
  $('midea-command-freeze_protection').value='true';$('midea-command-freeze_protection').handlers.change();
  assert.equal($('midea-command-turbo').value,'');
  await $('midea-command-send').handlers.click();
  const presetBody=new URLSearchParams(submitted.body);
  assert.equal(presetBody.get('freeze_protection'),'true');
  assert.equal(presetBody.get('sleep'),'false');
  assert.equal(presetBody.has('eco'),false);assert.equal(presetBody.has('turbo'),false);
  $('midea-command-eco').value='true';$('midea-command-turbo').value='true';
  assert.equal(run('updateMideaControlAvailability()'),false,'contradictory drafts are rejected even without a change event');
  $('midea-command-eco').value='';$('midea-command-turbo').value='';

  // An unsupported frost feature can safely permit basic control, while its
  // inferred off state remains absent from the live-value contract.
  current=JSON.parse(JSON.stringify(basic));
  current.capabilities.controls=current.capabilities.controls.filter(x=>x!=='freeze_protection');
  current.state_known=current.state_known.filter(x=>x!=='freeze_protection');current.freeze_protection=null;
  await refresh();
  assert.equal($('midea-current-freeze_protection').textContent,'—');
  assert.equal($('midea-field-freeze_protection').hidden,true);
  $('midea-command-temperature').value='23';$('midea-command-temperature').handlers.input();
  assert.equal($('midea-command-send').disabled,false,'inference does not become an unnecessary control block');
  $('midea-command-temperature').value='';

  // Unknown optional values stay unknown even when a capability advertises support.
  current=JSON.parse(JSON.stringify(basic));
  current.state_known=current.state_known.filter(x=>x!=='eco');current.eco=null;
  await refresh();
  assert.equal($('midea-field-eco').hidden,false);
  assert.equal($('midea-current-eco').textContent,'—');
  assert.equal($('midea-command-eco').value,'','advertisement must not create a draft');

  // An unknown preserved frost bit blocks a general draft, but not a selective B0 write.
  current.capabilities.controls.push('display');current.state_known.push('display');current.display=false;
  current.state_known=current.state_known.filter(x=>x!=='freeze_protection');
  current.freeze_protection=null;
  current.control_policy={general_available:false,property_available:true,general_block:'missing_frost_state',requires_freeze_choice:true};
  $('midea-command-temperature').value='23';
  await refresh();
  assert.equal($('midea-command-send').disabled,true);
  assert.match($('midea-control-availability').textContent,/jäätymis|frost/i);
  current.capabilities.controls=current.capabilities.controls.filter(x=>x!=='freeze_protection');
  current.control_policy.requires_freeze_choice=false;
  await refresh();
  assert.equal($('midea-field-freeze_protection').hidden,true);
  assert.match($('midea-control-availability').textContent,/jäätymis|frost/i,'an unavailable frost choice must still explain the missing state');
  current.capabilities.controls.push('freeze_protection');current.control_policy.requires_freeze_choice=true;
  await refresh();
  $('midea-command-freeze_protection').value='false';
  $('midea-command-freeze_protection').handlers.change();
  assert.equal($('midea-command-send').disabled,false,'an explicit frost choice can make the general draft complete');
  $('midea-command-temperature').value='';$('midea-command-freeze_protection').value='';
  $('midea-command-display').value='true';
  $('midea-command-display').handlers.change();
  assert.equal($('midea-command-send').disabled,false,'selective properties do not rewrite the unknown frost bit');

  // Mixed command families must be explained and never partially submitted.
  $('midea-command-temperature').value='23';
  $('midea-command-temperature').handlers.input();
  assert.equal($('midea-command-send').disabled,true);
  submitted=null;
  await $('midea-command-send').handlers.click();
  assert.equal(submitted,null,'mixed families cannot be sent as a partial command');
  $('midea-command-temperature').value='';
  $('midea-command-temperature').handlers.input();
  await $('midea-command-send').handlers.click();
  assert.equal(submitted.url,'/api/midea/control');
  assert.equal(new URLSearchParams(submitted.body).get('display'),'true');
  assert.equal(new URLSearchParams(submitted.body).has('target_c'),false);

  // A failed/stale refresh revokes command readiness immediately, without losing drafts.
  current.state_known.push('display');current.display=true;
  $('midea-command-display').value='false';
  await refresh();assert.equal($('midea-command-send').disabled,false);
  assert.equal($('midea-current-display').textContent,'Päällä');
  offline=true;await refresh();
  assert.equal($('midea-command-send').disabled,true);
  assert.equal($('midea-current-display').textContent,'—','offline optional values are not presented as current state');
  assert.equal($('midea-command-display').value,'false');
  offline=false;current.fresh=false;await refresh();
  assert.equal($('midea-command-send').disabled,true);
  current=JSON.parse(JSON.stringify(basic));current.pilot_approved=false;await refresh();
  $('midea-command-temperature').value='23';$('midea-command-temperature').handlers.input();
  assert.equal($('midea-command-send').disabled,true,'capabilities never approve the existing pilot gate');
  // A slow old response cannot restore another profile or its revoked approval.
  current=JSON.parse(JSON.stringify(basic));holdNextRefresh=true;
  const older=refresh();
  current.capabilities.modes=[2];current.pilot_approved=false;
  await refresh();resolveHeldRefresh();await older;
  assert.deepEqual(values('midea-command-mode'),['','2']);
  assert.equal($('midea-command-send').disabled,true,'an old request cannot restore pilot readiness');
  console.log('PASS Midea device-specific controls, drafts, frost policy, selective writes and stale-state gating');
})().catch(e=>{console.error(e);process.exitCode=1});
