'use strict';
const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
const src=fs.readFileSync('firmware/cottage/include/cottage_premium_dashboard.hpp','utf8');
const a=src.indexOf('function priceDisplayCurrency()'),b=src.indexOf('// Illustrative electrical-consumption',a);
assert(a>0&&b>a,'currency formatting function missing');
const state={status:{price_currency:'EUR'}};
const context={state,uiLanguage:'fi',fmt:(v)=>v.toFixed(2),number:v=>Number.isFinite(v)?v:null};
vm.createContext(context);
vm.runInContext(src.slice(a,b)+';this.price=priceFmt;this.currency=priceDisplayCurrency;this.minor=priceDisplayMinorUnit;',context);
for(const [currency,unit] of Object.entries({EUR:'snt',SEK:'öre',NOK:'øre',DKK:'øre',PLN:'gr'})){
  state.status.price_currency=currency;
  assert.equal(context.currency(),currency);
  assert.equal(context.minor(),unit);
  assert.equal(context.price(1.25),'125.00 '+unit+'/kWh');
}
state.status.price_currency='EUR';context.uiLanguage='en';
assert.equal(context.minor(),'c');
assert.equal(context.price(null),'—');
assert(src.includes("fmt(p.cost_eur,2)+' '+priceDisplayCurrency()"),'plan cost must use regional currency');
for(const id of ['price-spot-unit','price-spot-heading','price-total-heading','price-area-chip'])
 assert(src.includes('id="'+id+'"')&&src.includes("'"+id+"'"),'price label not dynamic: '+id);
console.log('test_regional_price_ui OK');
