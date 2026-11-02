const fs=require('fs'),vm=require('vm'),assert=require('assert');
const html=fs.readFileSync('tools/manager.html','utf8');
const validator=html.split('\n').find(line=>line.startsWith('function validMailFilter('));const validationContext=vm.createContext({});vm.runInContext(validator,validationContext);assert(validationContext.validMailFilter('@concept3d.com'));assert(validationContext.validMailFilter('person@example.com'));assert(!validationContext.validMailFilter('@domain..com'));assert(!validationContext.validMailFilter('person@example.com OR from:other'));
assert(html.includes('<h2>Gmail</h2>'));assert(html.includes("mail:'Mail'"));
const marker="document.addEventListener('submit',async e=>{if(e.target.id!=='mail-settings-form')";
const start=html.indexOf(marker),end=html.indexOf("\ndocument.addEventListener('click',async e=>{if(e.target.closest('#disconnect-mail'))",start);
assert(start>=0&&end>start);
async function test(device,credentials){let handler,persisted='',sent=null;
const controls=Object.fromEntries([0,1,2,3].map(i=>['mail-sender-'+i,{value:i<2?`sender${i+1}@example.com`:''}]));
controls['mail-unread-only']={checked:true};
controls['mail-account-file']={files:credentials?[{size:200,text:async()=>JSON.stringify(credentials)}]:[],value:'file'};
const context=vm.createContext({document:{addEventListener:(event,fn)=>{handler=fn}},DEVICE_MODE:device,db:{settings:{mailConfigured:true,mailAccount:'owner@example.com'}},$:id=>controls[id],Set,JSON,Object,validMailFilter:()=>true,toast:()=>{},render:()=>{},persist:()=>{persisted=JSON.stringify(context.db)},saveSettingsAction:async(action,record)=>{sent={action,record}}});
vm.runInContext(html.slice(start,end),context);await handler({target:{id:'mail-settings-form'},preventDefault:()=>{}});return{sent,persisted,db:context.db};}
(async()=>{const credentials={scope:'https://www.googleapis.com/auth/gmail.readonly',client_id:'test.apps.googleusercontent.com',client_secret:'dummy-secret',refresh_token:'dummy-refresh',email:'owner@example.com'};
let result=await test(false,credentials);assert(!result.persisted.includes('dummy-secret'));assert(!result.persisted.includes('dummy-refresh'));assert.strictEqual(result.db.settings.mailSenders.length,2);assert.strictEqual(result.db.settings.mailUnreadOnly,true);
result=await test(true,credentials);assert.strictEqual(result.sent.action,'mail');assert.strictEqual(result.sent.record.unreadOnly,true);assert.strictEqual(result.sent.record.credentials.refresh_token,'dummy-refresh');
result=await test(true,null);assert(!('credentials' in result.sent.record));assert.strictEqual(result.sent.record.senders.length,2);
console.log('Gmail form: optional credentials preserved, read-only import, preview does not persist secrets: PASS');})().catch(error=>{console.error(error);process.exit(1)});
