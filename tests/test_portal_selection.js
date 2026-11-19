const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
const html=fs.readFileSync('components/selectro_izar/portal.html','utf8');
const js=html.split('<script>')[1].split('</script>')[0];
class FakeElement{
 constructor(id){this.id=id;this._value='';this._innerHTML='';this.focus=()=>{};this.getBoundingClientRect=()=>({top:0});this.textContent='';this.listeners={};}
 get value(){return this._value;} set value(v){this._value=String(v);}
 get innerHTML(){return this._innerHTML;}
 set innerHTML(v){this._innerHTML=v;if(this.id==='slot')this._value='0';}
 addEventListener(name,cb){this.listeners[name]=cb;}
}
const elements=new Map();
const el=id=>{if(!elements.has(id))elements.set(id,new FakeElement(id));return elements.get(id);};
const slots=Array.from({length:10},()=>({id:'',name:'',total_m3:null,flow_l_h:null,leak_alarm:null,alarm_details:'UNASSIGNED'}));
const state={found:[{id:'219A769B',rssi:-46,age_s:3,valid:true,total_m3:3222.12,flow_l_h:24,leak_alarm:true,printed:'T26WA900123',status:'LEAKAGE'}],slots};
const ctx={document:{getElementById:el},fetch:async()=>({ok:true,json:async()=>state}),setInterval:()=>{},window:{scrollTo:()=>{}},scrollY:0,URLSearchParams,confirm:()=>true,alert:()=>{}};
vm.createContext(ctx);vm.runInContext(js,ctx);
(async()=>{
 await vm.runInContext('refresh()',ctx);
 el('slot').value='4';el('slot').listeners.change();
 await vm.runInContext('refresh()',ctx);
 assert.equal(el('slot').value,'4','refresh must preserve selected slot');
 vm.runInContext("choose('219A769B')",ctx);
 assert.equal(el('slot').value,'4','choose must preserve manually selected slot');
 slots[3].id='ABCDEF12'; slots[3].alarm_details='BACK_FLOW SENSOR_FRAUD';
 await vm.runInContext('refresh()',ctx);
 assert.equal(el('slot').value,'4','options update must preserve slot');
 assert.match(el('found').innerHTML,/Leak Alarm/);
 assert.match(el('found').innerHTML,/24\.0 L\/h/);
 assert.match(el('slots').innerHTML,/SENSOR_FRAUD/);
 console.log('PORTAL SELECTION AND FLAGS TESTS PASSED');
})().catch(e=>{console.error(e);process.exit(1)});
