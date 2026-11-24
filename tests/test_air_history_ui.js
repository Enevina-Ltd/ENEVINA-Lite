'use strict';
const fs=require('node:fs'),path=require('node:path'),vm=require('node:vm'),assert=require('node:assert/strict');
const src=fs.readFileSync(path.join(__dirname,'../firmware/cottage/include/cottage_premium_dashboard.hpp'),'utf8');
const reload='async function reloadHistory(){'+src.split('async function reloadHistory(){')[1].split('async function requestControlPermission(')[0];
const post='async function airPostLocal(path,fields) {'+src.split('async function airPostLocal(path,fields) {')[1].split("$('ruuvi-bind').addEventListener")[0];
const pending=[],state={status:{air:{config_token:'test'}}};let renders=0;
let status=200,body={saved:true};
const context={state,URLSearchParams,translateFi:s=>s,showHistory:()=>{renders++;},
 fetchJSON:()=>new Promise((resolve,reject)=>pending.push({resolve,reject})),
 fetch:async()=>({ok:status===200,status,json:async()=>body})};
vm.createContext(context);vm.runInContext(reload+'\n'+post,context);
(async()=>{
 const first=context.reloadHistory(),second=context.reloadHistory();
 pending[1].resolve({hours:[{temp:21}]});await second;
 pending[0].resolve({hours:[{temp:11}]});await first;
 assert.equal(state.history.hours[0].temp,21);assert.equal(renders,1);
 const failed=context.reloadHistory();pending[2].reject(Error('HTTP 503'));await failed;
 assert.equal(state.history,null);assert.equal(state.airHistoryReadFailed,true);
 const recovery=context.reloadHistory();pending[3].resolve({hours:[]});await recovery;
 assert.equal(state.airHistoryReadFailed,false);assert.equal(state.history.hours.length,0);
 status=503;body={error:'HISTORY_BUSY'};
 const rejected=context.airPostLocal('/api/air/bind',{which:'indoor',mac:'none'});
 const result=assert.rejects(rejected,/Historian tallennus on kesken/);
 await new Promise(setImmediate);
 pending[4].resolve({hours:[{temp:12}]});await result;
 assert.equal(state.history.hours[0].temp,12);
 status=200;body={saved:true};
 const bound=context.airPostLocal('/api/air/bind',{which:'indoor',mac:'none'});
 await new Promise(setImmediate);pending[5].resolve({hours:[]});await bound;
 assert.equal(state.history.hours.length,0);
 console.log('PASS Ruuvi history read failures, out-of-order responses, busy rebind and post-bind refresh');
})().catch(e=>{console.error(e);process.exitCode=1;});
