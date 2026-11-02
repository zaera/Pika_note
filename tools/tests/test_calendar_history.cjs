const fs=require('fs'),vm=require('vm'),assert=require('assert');
const html=fs.readFileSync('tools/manager.html','utf8');
const body=html.slice(html.indexOf('function isArchived('),html.indexOf('function memoryCount('));
const ctx=vm.createContext({tab:'calendar',today:'2026-10-21',db:{events:[{id:'past',date:'2026-10-19T12:00'},{id:'future',date:'2026-10-25T12:00'},{id:'hidden',date:'2026-10-25T13:00',archived:true}],tasks:[{id:'done',done:true},{id:'open',done:false}]}});
vm.runInContext(body,ctx);
assert.deepStrictEqual(Array.from(ctx.visibleItems('events'),v=>v.id),['past','future']);
assert.deepStrictEqual(Array.from(ctx.visibleItems('tasks'),v=>v.id),['open']);
ctx.tab='archive';assert.deepStrictEqual(Array.from(ctx.visibleItems('events'),v=>v.id),['past','hidden']);
assert.deepStrictEqual(Array.from(ctx.visibleItems('tasks'),v=>v.id),['done']);
ctx.tab='events';assert.deepStrictEqual(Array.from(ctx.visibleItems('events'),v=>v.id),['future']);
console.log('Calendar keeps past events; Archive preserves past events and done tasks; Events page shows upcoming: PASS');

ctx.db.memories=[{id:'old-active',date:'2026-10-01',archived:false},{id:'today-active',date:'2026-10-21',archived:false},{id:'forgotten',date:'2026-10-21',archived:true}];
ctx.tab='memories';assert.deepStrictEqual(Array.from(ctx.visibleItems('memories',false),v=>v.id),['old-active','today-active']);
ctx.tab='archive';assert.deepStrictEqual(Array.from(ctx.visibleItems('memories'),v=>v.id),['forgotten']);
console.log('Memories includes active notes from every day and excludes forgotten notes: PASS');
