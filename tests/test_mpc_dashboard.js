"use strict";
// MPC card on the Optimization view: renders every state honestly, FI+EN,
// shows energy/cost ONLY when the backend marks them trusted, never claims
// a decision executed before the pump confirmed it, and only GETs /api/mpc.
const fs=require('fs'),vm=require('vm'),assert=require('node:assert/strict'),path=require('path');
const html=fs.readFileSync(path.join(__dirname,'../firmware/cottage/include/cottage_premium_dashboard.hpp'),'utf8');
const marker='<script data-part="mpc">';
assert.equal(html.split(marker).length,2,'exactly one MPC script block');
const source=html.split(marker)[1].split('</script>')[0];
// Read-only: no POST, no actuator or control endpoints from this card.
assert.ok(!/method\s*:\s*['"]POST/.test(source));
assert.ok(!/\/api\/(control|ota|settings|air|midea)/.test(source));
assert.ok(source.includes("fetch('/api/mpc'"));
const code=source.split('\n').filter(l=>!l.trim().startsWith('//')).join('\n')
  .replace(/no savings are claimed|säästöjä ei väitetä/g,'');
assert.ok(!/säästö|saving/i.test(code),'the card must not compute or claim savings');

function el(tag) {
  return {tag,children:[],attrs:{},style:{},className:'',id:'',_text:'',
    set textContent(v){this._text=String(v);this.children=[];},
    get textContent(){return this._text+this.children.map(c=>c.textContent||'').join('');},
    append(...n){for(const x of n)this.children.push(typeof x==='string'?{textContent:x}:x);},
    prepend(n){this.children.unshift(n);},
    replaceChildren(){this.children=[];this._text='';},
    setAttribute(k,v){this.attrs[k]=v;},addEventListener(){}};
}
function run(payload,language) {
  const grid=el('div'),card={value:null};
  const langs=[{addEventListener(){}},{addEventListener(){}}];
  const document={
    head:{appendChild(){}},
    createElement:el,createElementNS:(_n,t)=>el(t),createTextNode:t=>({textContent:t}),
    getElementById:id=>id==='mpc-card'?card.value:null,
    querySelector:s=>s==='#view-plan .grid'?grid:null,
    querySelectorAll:s=>s==='[data-lang]'?langs:[]
  };
  const orig=grid.prepend.bind(grid);
  grid.prepend=n=>{card.value=n;orig(n);};
  const fetched=[];
  const ctx={document,uiLanguage:language,uiLocale:()=>language==='en'?'en-GB':'fi-FI',
    uiTimeZone:()=>'Europe/Helsinki',Intl,Date,Math,Number,String,JSON,Error,setTimeout,
    setInterval(){},fetch:async(url,opt)=>{fetched.push([url,opt]);
      return {ok:true,json:async()=>payload};}};
  vm.createContext(ctx);
  vm.runInContext(source,ctx);
  return new Promise(r=>setTimeout(()=>r({card:card.value,fetched}),20));
}
const now=1768172400;
const learning={now_utc:now,state:'CALIBRATING',fallback:'MODEL_LEARNING',source:'PRICE_CONTROL',
  armed:false,decision_confirmed:false,
  calibration:{state:'LEARNING',block:'TOO_FEW_SAMPLES',samples:40,windows:0,windows_needed:12,
    validation_rms_c:null,validation_bias_c:null,planning_uncertainty_c:0.2,
    model:{plausible:false,cop_identified:false}},
  meter:{scope:'UNCONFIRMED',watts:640,used_for_model:false},
  plan:{valid:false,computed_utc:now,planning_fallback:'MODEL_LEARNING',slots:[]},
  trend:[{utc:now-900,measured_c:12.1,one_step_c:null,open_loop_c:null},
         {utc:now,measured_c:12.0,one_step_c:12.02,open_loop_c:null}]};
const slots=[];
for(let i=0;i<96;i++)slots.push({utc:now+i*900,heat:i%5===0,indoor_c:12+0.1*Math.sin(i/7),
  outdoor_c:-4,price:i>40&&i<44?-0.02:0.12,kwh:null,cost:null});
const active={...learning,state:'ACTIVE',fallback:'NONE',source:'MPC',armed:true,decision_confirmed:false,
  calibration:{...learning.calibration,state:'CALIBRATED',block:'NONE',samples:900,windows:24,
    validation_rms_c:0.05,validation_bias_c:-0.01,
    model:{plausible:true,tau_h:16.7,tau_std_h:0.7,lift_k:26.0,lift_std_k:1.0,lift_outdoor_c:-3,
      start_delay_min:2.9,afterheat_k:0.01,cop_identified:false,ua_lower_kw_per_c:null}},
  plan:{valid:true,computed_utc:now+20,first_utc:now,first_heat:true,horizon_slots:96,
    horizon_limit:'PRICES_END',lower_c:11.5,upper_c:13,energy_valid:false,energy_kwh:null,
    cost:null,currency:'EUR',slots}};
(async()=>{
  // Learning, Finnish: calibrating state, no model claims, no energy, scope warning.
  let r=await run(learning,'fi');
  let text=r.card.textContent;
  assert.equal(r.fetched[0][0],'/api/mpc');
  assert.equal(r.fetched[0][1].method,undefined);
  assert.ok(text.includes('Ennakoiva säätö (MPC)'));
  assert.ok(text.includes('Kalibroidaan: malli oppii vielä mittauksista'));
  assert.ok(text.includes('Hintaohjaus'));
  assert.ok(text.includes('40 / 192'));
  assert.ok(text.includes('Mallia ei vielä ole'));
  assert.ok(text.includes('voi sisältää muuta kulutusta'));
  assert.ok(!text.includes('kWh ·'));
  assert.ok(!text.includes('lähdetodennettu'));      // field absent -> no claim
  r=await run({...learning,pump_source_verified:false},'en');
  assert.ok(r.card.textContent.includes('A fresh pump reading is unavailable.'));
  r=await run({...learning,pump_source_verified:true},'en');
  assert.ok(!r.card.textContent.includes('A fresh pump reading is unavailable.'));
  for(const language of ['fi','en']) {
    r=await run({...learning,pump_source_verified:false,pump_connection_ready:true,gateway_request:true},language);
    assert.ok(r.card.textContent.includes(language==='en'?'Learns from gateway requests':'Oppii sillan lämmityspyynnöistä'));
    assert.ok(!r.card.textContent.includes('A fresh pump reading is unavailable.'));
  }
  for(const language of ['fi','en']){
    const detail={...learning,calibration:{...learning.calibration,
      samples_needed:192,heating_slots:7,off_slots:33,starts:2,stops:1,
      last_observation:'REQUEST_INVALID',block:'NEED_SWITCHES',
      last_saved_sample_utc:now-900,checkpoint_backend:'LITTLEFS'}};
    r=await run(detail,language);
    assert.ok(r.card.textContent.includes(language==='fi'?'Lämmitys 7/32':'Heating 7/32'));
    assert.ok(r.card.textContent.includes(language==='fi'?'Käynnistykset 2/4':'Starts 2/4'));
    assert.ok(r.card.textContent.includes(language==='fi'?'lämmityspyyntöä ei voitu todentaa':'heating request could not be verified'));
    assert.ok(r.card.textContent.includes('LITTLEFS'));
    const horizon={...active,plan:{...active.plan,horizon_limit:'LEARNED_OUTDOOR_RANGE'}};
    r=await run(horizon,language);
    assert.ok(r.card.textContent.includes(language==='fi'?'opitun ulkolämpötila-alueen raja':'learned outdoor-temperature range'));
  }
  for(const language of ['fi','en']){
    const defrost={...learning,state:'FALLBACK',fallback:'DEFROST_HOLD',source:'NONE',
      calibration:{...learning.calibration,last_observation:'DEFROST_OBSERVED',defrost_skipped_this_boot:3}};
    r=await run(defrost,language);
    assert.ok(r.card.textContent.includes(language==='fi'?'ohjaus odottaa ilmoitetun sulatuksen päättymistä':'waiting for reported defrost to finish'));
    assert.ok(r.card.textContent.includes(language==='fi'?'Sulatuksen/jälkivaiheen vuoksi ohitetut jaksot':'Defrost/recovery slots excluded since boot'));
    assert.ok(r.card.textContent.includes(language==='fi'?'ei normaalia oppimisnäytettä':'excluded from ordinary learning'));
    assert.ok(!r.card.textContent.includes('kWh ·'));
  }
  // Learning, English.
  r=await run(learning,'en');
  text=r.card.textContent;
  assert.ok(text.includes('Predictive control (MPC)'));
  assert.ok(text.includes('Calibrating: the model is still learning from measurements'));
  assert.ok(text.includes('may include other loads'));
  // Active and armed but NOT yet confirmed by the pump: never "executed".
  r=await run(active,'en');
  text=r.card.textContent;
  assert.ok(text.includes('Active'));
  assert.ok(text.includes('MPC plan'));
  assert.ok(text.includes('HEAT 16 °C'));
  assert.ok(text.includes('not yet confirmed by the pump'));
  assert.ok(!text.includes('confirmed by the pump')||text.includes('not yet confirmed'));
  assert.ok(text.includes('prices end'));
  assert.ok(text.includes('A reliable heat-pump consumption model is unavailable.'));
  assert.ok(text.includes('16.7') && text.includes('26.0'));
  assert.ok(text.includes('COP is not estimated'));
  const svg=r.card.children.find(c=>c.tag==='svg');
  assert.ok(svg,'chart rendered');
  // Negative price slots are drawn (red bars) rather than dropped.
  assert.ok(JSON.stringify(svg.children.map(c=>c.attrs.fill)).includes('var(--red)'));
  const residential={...active,pump_source_verified:false,pump_connection_ready:true,
    plan:{...active.plan,slots:[{...slots[0],setpoint_c:21.5},...slots.slice(1)]}};
  r=await run(residential,'en');
  assert.ok(r.card.textContent.includes('HEAT 21.5 °C'));
  assert.ok(!r.card.textContent.includes('A fresh pump reading is unavailable.'));
  r=await run(residential,'fi');
  assert.ok(r.card.textContent.includes('HEAT 21,5 °C'));
  // Shadow mode wording when not armed.
  r=await run({...active,armed:false},'fi');
  assert.ok(r.card.textContent.includes('varjotila: komentoja ei lähetetä'));
  // Confirmed readback.
  r=await run({...active,decision_confirmed:true},'fi');
  assert.ok(r.card.textContent.includes('pumppu vahvisti tilan'));
  // Energy only when the backend says it is trustworthy.
  const metered={...active,plan:{...active.plan,energy_valid:true,energy_kwh:12.34,cost:2.5}};
  r=await run(metered,'en');
  assert.ok(r.card.textContent.includes('12.34 kWh'));
  const shared={...metered,energy_basis:'ESTIMATED_INCREMENTAL',meter:{scope:'WHOLE_HOUSE',watts:1500},
    baseload:{ready:true,watts:169,incremental_w:1331,shared_meter_confirmed:true}};
  r=await run(shared,'fi');
  assert.ok(r.card.textContent.includes('Opittu OFF-pohjakuorma'));
  assert.ok(r.card.textContent.includes('169 W'));
  assert.ok(r.card.textContent.includes('Arvioitu lämmityksen lisäsähkö'));
  r=await run(shared,'en');
  assert.ok(r.card.textContent.includes('Learned OFF baseload'));
  assert.ok(r.card.textContent.includes('1,331 W'));
  assert.ok(r.card.textContent.includes('Estimated incremental heating electricity'));
  r=await run({...shared,baseload:{ready:false,watts:null}},'en');
  assert.ok(r.card.textContent.includes('Waiting for a stable OFF interval'));
  // Fallback with a reason (degraded model).
  r=await run({...learning,state:'FALLBACK',fallback:'MODEL_DEGRADED'},'en');
  assert.ok(r.card.textContent.includes('Fallback: the model no longer predicts accurately enough'));
  // Codex P2 (PR #63): a plan the controller rejected (stale indoor sensor)
  // is never displayed as in force; the controller's real reason is shown and
  // the old decisions, chart plan and "next decision" are gone.
  const stale={...active,state:'FALLBACK',fallback:'TEMPERATURE_STALE',source:'PRICE_CONTROL',
    plan:{...active.plan,valid:false,had_plan:true,plan_block:'TEMPERATURE_STALE',
          planning_fallback:'NONE',slots:[]}};
  r=await run(stale,'en');
  text=r.card.textContent;
  assert.ok(text.includes('Fallback: indoor temperature missing or stale'));
  assert.ok(text.includes('The earlier plan is not being executed: indoor temperature missing or stale.'));
  assert.ok(text.includes('There is no valid MPC plan for this quarter-hour.'));
  assert.ok(!text.includes('Next decision') && text.split('MPC plan').length===2);   // only inside the "no valid MPC plan" notice
  assert.ok(!text.includes('not yet confirmed') && !text.includes('confirmed by the pump'));
  assert.ok(text.includes('Price control'));
  r=await run(stale,'fi');
  assert.ok(r.card.textContent.includes('Aiempi suunnitelma ei ole käytössä: sisälämpötila puuttuu tai on vanha.'));
  // Even if a stale payload still carried slots, valid=false must hide them.
  r=await run({...stale,plan:{...stale.plan,slots}},'en');
  assert.ok(!r.card.textContent.includes('Next decision'));
  assert.ok(!r.card.children.find(c=>c.tag==='svg')||
    !JSON.stringify(r.card.children.find(c=>c.tag==='svg').children.map(c=>c.attrs.fill)).includes('var(--red)'));
  // No plan was ever made (learning): do not claim an earlier plan existed.
  r=await run(learning,'en');
  assert.ok(!r.card.textContent.includes('earlier plan'));
  // The owner's contradictory comfort limits are named, in both languages.
  const limits={...learning,state:'FALLBACK',fallback:'COMFORT_LIMITS_INVALID',
    plan:{...learning.plan,had_plan:false,plan_block:'COMFORT_LIMITS_INVALID'}};
  r=await run(limits,'en');
  assert.ok(r.card.textContent.includes('your temperature limits are contradictory or too close to the target'));
  r=await run(limits,'fi');
  assert.ok(r.card.textContent.includes('omat lämpötilarajat ovat ristiriitaiset tai liian lähellä tavoitetta'));
  // Plan displayed only while the controller accepts it.
  r=await run(active,'en');
  assert.ok(r.card.textContent.includes('Next decision'));
  assert.ok(!r.card.textContent.includes('earlier plan'));
  // Device error.
  r=await run({error:true},'fi');
  assert.ok(r.card.textContent.includes('MPC-tietoja ei saatu laitteelta.'));
  console.log('test_mpc_dashboard: all passed');
})().catch(e=>{console.error(e);process.exit(1);});
