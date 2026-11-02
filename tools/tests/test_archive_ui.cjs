const fs=require('fs'),vm=require('vm'),assert=require('assert');
const h=fs.readFileSync('tools/manager.html','utf8');
assert(h.includes('Are you sure to delete all archived data?'));
assert(h.includes('id="purge-archive-dialog"'));
const nodes={};let click;
const ctx=vm.createContext({tab:'archive',today:'2026-10-20',DEVICE_MODE:false,db:{revision:7,memories:[{id:'active',archived:false},{id:'forgotten',archived:true}],tasks:[{id:'open'},{id:'done',done:true},{id:'hidden',archived:true}],events:[{id:'future',date:'2026-10-21'},{id:'past',date:'2026-10-19'},{id:'hidden',archived:true},{id:'spanning',date:'2026-10-19',end:'2026-10-22'}]},pendingOperations:[],processingOperations:false,queueError:'',archiveQuery:'query',persist(){},render(){},toast(){},operationId:()=> 'purge-test',api:async(path,body)=>{ctx.sent=body},loadDeviceData:async()=>{},setPageLoading(){},document:{addEventListener(t,f){if(!click)click=f}},$:id=>nodes[id]??=({showModal(){this.open=true},close(){this.open=false}})});
vm.runInContext(h.slice(h.indexOf('function isArchived('),h.indexOf('function memoryCount(')),ctx);
vm.runInContext(h.slice(h.indexOf('function clearPreviewArchive('),h.indexOf("$('cancel-edit').onclick=")),ctx);
(async()=>{
 click({target:{closest:()=>true}});assert(nodes['purge-archive-dialog'].open);assert(!ctx.sent);
 nodes['cancel-purge-archive'].onclick();assert(!nodes['purge-archive-dialog'].open);assert(!ctx.sent);
 await nodes['confirm-purge-archive'].onclick();
 assert.deepStrictEqual(Array.from(ctx.db.memories,x=>x.id),['active']);assert.deepStrictEqual(Array.from(ctx.db.tasks,x=>x.id),['open']);assert.deepStrictEqual(Array.from(ctx.db.events,x=>x.id),['future','spanning']);
 ctx.DEVICE_MODE=true;ctx.pendingOperations.push({});await nodes['confirm-purge-archive'].onclick();assert(!ctx.sent);ctx.pendingOperations=[];
 await nodes['confirm-purge-archive'].onclick();assert(ctx.sent.confirmed&&ctx.sent.action==='purge'&&ctx.sent.kind==='archive'&&ctx.sent.revision===7);assert(!nodes['confirm-purge-archive'].disabled);
 console.log('Archive UI: confirmation/cancel, permanent purge selection, active spanning events, pending-save guard and explicit backend confirmation: PASS');
})().catch(e=>{console.error(e);process.exitCode=1});
