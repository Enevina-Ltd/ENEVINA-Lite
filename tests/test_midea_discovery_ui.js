/* Run the actual discovery UI: a known non-AC appliance must never fill AC settings. */
'use strict';
const fs=require('fs'),path=require('path'),vm=require('vm'),assert=require('node:assert/strict');
const html=fs.readFileSync(path.join(__dirname,'../firmware/cottage/include/cottage_premium_dashboard.hpp'),'utf8');
const source=html.slice(html.indexOf('let mideaDiscoveryResults=[];'),html.indexOf("$('midea-save').addEventListener"));
function element(id){return {id,value:'',textContent:'',disabled:false,hidden:false,dataset:{},children:[],handlers:{},
  appendChild(v){this.children.push(v)},replaceChildren(...v){this.children=[...v]},
  addEventListener(k,v){this.handlers[k]=v}};}
const elements=new Map(),$=id=>{if(!elements.has(id))elements.set(id,element(id));return elements.get(id)};
let devices=[],calls=[],wizardOpen=false,scanCounter=0,pendingPolls=0,wrongScan=false;
const WZ={mideaChanged:false};
const context={document:{createElement:element},$,URLSearchParams,WZ,wzMideaStep:()=>wizardOpen,
  state:{status:{air:{config_token:'existing-test-token'}}},showMideaProtocolFields:()=>{},
  text:(id,value)=>{$(id).textContent=String(value)},
  setTimeout:fn=>{fn();},
  fetch:async(url,options)=>{
    const form=new URLSearchParams(options.body);calls.push({url,body:String(options.body)});
    if(!form.has('scan_id')){++scanCounter;pendingPolls=0;return {ok:true,status:202,json:async()=>({state:'pending',scan_id:scanCounter})};}
    assert.equal(form.get('scan_id'),String(scanCounter));
    if(++pendingPolls===1)return {ok:true,status:202,json:async()=>({state:'pending',scan_id:scanCounter})};
    return {ok:true,status:200,json:async()=>({state:'done',scan_id:wrongScan?scanCounter+1:scanCounter,devices})};
  }};
vm.createContext(context);vm.runInContext(source,context,{filename:'midea-discovery-dashboard.js'});
const run=s=>vm.runInContext(s,context);
const choose=device=>{context.candidate=device;run('selectDiscoveredMidea(candidate)')};
const ac={ip:'10.0.0.2',protocol:3,device_id:'123456',device_type_known:true,device_type:172,
  model_known:true,model:'NEW0OEM1',subtype_known:true,subtype:44204};
const commercial={...ac,ip:'10.0.0.3',device_id:'123457',device_type:204,model:'171PNL01'};
const hydronic={...ac,ip:'10.0.0.4',device_id:'123458',device_type:195,model:'00000001'};
const legacy={ip:'10.0.0.5',protocol:2,device_id:'123459'};
const reset=()=>{for(const name of ['ip','id','protocol','token','key'])$('midea-'+name).value='unchanged-'+name};
const unchanged=()=>{for(const name of ['ip','id','protocol','token','key'])assert.equal($('midea-'+name).value,'unchanged-'+name)};

(async()=>{
  reset();choose(commercial);unchanged();
  assert.match($('midea-feedback').textContent,/ei|not/i,'known CC explains that this AC adapter is unavailable');
  reset();choose(hydronic);unchanged();

  choose(ac);
  assert.equal($('midea-ip').value,'10.0.0.2');
  assert.equal($('midea-id').value,'123456');
  assert.equal($('midea-protocol').value,'3');
  assert.equal($('midea-token').value,'');assert.equal($('midea-key').value,'');
  assert.equal(calls.length,0,'selection never saves, authenticates or commands');

  reset();wizardOpen=true;choose(legacy);
  assert.equal(WZ.mideaChanged,true);
  assert.equal($('midea-ip').value,'10.0.0.5');assert.equal($('midea-protocol').value,'2');
  choose({...legacy,device_type_known:false,device_type:204,model_known:false,model:''});
  assert.equal($('midea-ip').value,'10.0.0.5','unknown metadata preserves existing discovery selection');

  reset();devices=[ac,commercial,legacy];
  await $('midea-discover').handlers.click();
  unchanged();
  const options=$('midea-discover-select').children;
  assert.equal(options.length,4);
  assert.equal(options[1].disabled,false,'unlisted generic AC model remains selectable');
  assert.equal(options[2].disabled,true,'known CC stays visible but is disabled');
  assert.equal(options[3].disabled,false,'minimal legacy discovery remains selectable');
  assert.match(options[1].textContent,/NEW0OEM1/);
  assert.match(options[2].textContent,/171PNL01/);
  assert.match(options[2].textContent,/0xCC/i);
  assert.equal(calls.length,3);assert(calls.every(c=>c.url==='/api/midea/discover'));
  assert.equal(new URLSearchParams(calls[1].body).get('scan_id'),'1');
  assert.equal(new URLSearchParams(calls[0].body).get('token'),'existing-test-token');

  $('midea-discover-select').value='1';$('midea-discover-select').handlers.change();unchanged();
  $('midea-discover-select').value='0';$('midea-discover-select').handlers.change();
  assert.equal($('midea-id').value,'123456');

  reset();devices=[hydronic];await $('midea-discover').handlers.click();unchanged();
  assert.equal($('midea-discover-select').hidden,false,'single unsupported device remains visible');
  assert.equal($('midea-discover-select').children[0].disabled,true);
  assert.match($('midea-feedback').textContent,/ei|not/i);

  reset();devices=[ac];await $('midea-discover').handlers.click();
  assert.equal($('midea-id').value,'123456','single supported AC retains autofill');
  assert.equal($('midea-discover-select').hidden,false,'known single-device metadata must remain visible');
  const single=$('midea-discover-select').children[0].textContent;
  assert.match(single,/NEW0OEM1/);assert.match(single,/0xAC/i);assert.match(single,/44204/);

  reset();devices=[legacy];await $('midea-discover').handlers.click();
  assert.equal($('midea-id').value,'123459','single unknown-metadata device retains autofill');
  assert.equal($('midea-discover-select').hidden,true,'minimal legacy discovery retains its existing display');
  reset();wrongScan=true;await $('midea-discover').handlers.click();unchanged();
  assert.match($('midea-feedback').textContent,/SCAN_ID/);
  assert.equal($('midea-discover').disabled,false);
  console.log('PASS Midea discovery: asynchronous polling, typed AC selection, unsupported visibility and legacy fallback');
})().catch(error=>{console.error(error);process.exit(1)});
