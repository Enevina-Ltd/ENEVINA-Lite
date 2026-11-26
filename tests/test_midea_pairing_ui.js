'use strict';
// Execute actual dashboard import and connect handlers with no network.
const fs=require('fs'),path=require('path'),vm=require('vm'),assert=require('node:assert/strict');
const src=fs.readFileSync(path.join(__dirname,'../firmware/cottage/include/cottage_premium_dashboard.hpp'),'utf8');
const code=src.slice(src.indexOf('function mideaBackupCredentials('),src.indexOf('let greeStatus='));
const elements=new Map(),$=id=>{
 if(!elements.has(id))elements.set(id,{value:'',disabled:false,dataset:{},handlers:{},addEventListener(k,v){this.handlers[k]=v;}});
 return elements.get(id);
};
let requests=[],jobState={pairing_id:7,pairing_pending:false,pairing_succeeded:true},reloads=0;
const WZ={mideaChanged:true};
const context={$,WZ,URLSearchParams,wzMideaStep:()=>true,WT:s=>s,state:{site:{pump_adapter:'midea_lan'},status:{air:{config_token:'test'}}},
 text:(id,v)=>{$(id).textContent=v;},setTimeout:fn=>fn(),reloadMidea:async()=>{reloads++;},
 fetch:async(url,opt)=>{requests.push({url,opt});return {ok:true,json:async()=>({queued:true,pairing_id:7})};},
 fetchJSON:async()=>jobState};
vm.createContext(context);vm.runInContext(code,context);
const backup={format:'midea-smart-ac-credentials',version:1,device:{id:'123456',host:'10.0.0.9',port:6444,device_type:'AC',token:'a'.repeat(128),key:'b'.repeat(64)}};
function parse(value,id){context.raw=JSON.stringify(value);context.id=id;return vm.runInContext('mideaBackupCredentials(raw,id)',context);}
(async()=>{
 assert.equal(parse(backup,'123456').token,'a'.repeat(128));
 assert.throws(()=>parse(backup,'123457'),/backup_device/);
 assert.throws(()=>parse({...backup,version:2},'123456'),/backup_invalid/);
 assert.throws(()=>parse({...backup,device:{...backup.device,device_type:'CC'}},'123456'),/backup_invalid/);
 assert.throws(()=>parse({...backup,device:{...backup.device,key:'x'}},'123456'),/backup_invalid/);
 $('midea-ip').value='10.0.0.2';$('midea-id').value='123456';$('midea-protocol').value='3';
 $('midea-backup').files=[{size:300,text:async()=>JSON.stringify(backup)}];
 await $('midea-backup').handlers.change();
 assert.equal($('midea-ip').value,'10.0.0.2','backup address is never trusted');
 assert.equal($('midea-token').value,'a'.repeat(128));
 assert.equal(requests.length,0,'import itself must never transmit credentials');
 jobState={pairing_id:7,pairing_pending:false,pairing_succeeded:false,pairing_code:'authentication_failed'};
 await $('midea-save').handlers.click();
 assert.equal(WZ.mideaChanged,true);
 assert.equal(reloads,0,'failed read must not be presented as paired');
 jobState={pairing_id:7,pairing_pending:false,pairing_succeeded:true};
 await $('midea-save').handlers.click();
 assert.equal(WZ.mideaChanged,false);assert.equal(reloads,1);
 assert.equal($('midea-key').value,'');assert.equal($('midea-token').value,'');
 assert.equal(requests[0].opt.redirect,'error');
 // V2: no credential fields required, no fallback to V3.
 $('midea-protocol').value='2';await $('midea-save').handlers.click();
 const fields=requests.at(-1).opt.body;
 assert.equal(fields.get('protocol'),'2');assert.equal(fields.get('v3_token'),'');
 console.log('PASS Midea pairing UI: strict backup identity, V2 no credentials, verified result, secret clearing');
})().catch(e=>{console.error(e);process.exit(1);});
