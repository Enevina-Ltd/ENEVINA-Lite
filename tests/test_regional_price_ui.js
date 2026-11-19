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
// Home timestamps must not remain hard-coded to Finland.
const fn=src.match(/function regionTimeZone\(\)\{[\s\S]*?\n\}/);
assert(fn,'regional timezone helper missing');
const ctx2={state:{site:{region:{country:'SE',timezone:''}}}};
vm.createContext(ctx2);vm.runInContext(fn[0]+';this.tz=regionTimeZone;',ctx2);
assert.equal(ctx2.tz(),'Europe/Stockholm');
ctx2.state.site.region.timezone='Europe/Oslo';
assert.equal(ctx2.tz(),'Europe/Oslo');
ctx2.state.site.region.timezone='Invalid/Zone';
assert.equal(ctx2.tz(),'Europe/Stockholm');
assert(src.includes("timeZone:regionTimeZone()"));
assert(src.includes("tariff_confirmed:'true'"),'normal contract save must confirm tariff');
console.log('test_regional_price_ui OK');
