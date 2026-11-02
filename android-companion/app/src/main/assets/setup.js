document.documentElement.dataset.theme=PikaNative.theme();
const pending=new Map();let sequence=0,state={step:0},busy=false;
window.__nativeDone=(id,status,base64)=>{const p=pending.get(id);if(!p)return;pending.delete(id);try{const text=new TextDecoder().decode(Uint8Array.from(atob(base64),c=>c.charCodeAt(0))),result=JSON.parse(text);if(status>=400)p.reject(Error(result.error||'Could not save settings'));else p.resolve(result)}catch(error){p.reject(error)}};
function api(body){return new Promise((resolve,reject)=>{const id=String(++sequence);pending.set(id,{resolve,reject});PikaNative.request(id,'/api/phone/setup',body?'POST':'GET',body?JSON.stringify(body):'')})}
const $=id=>document.getElementById(id);
function paint(){
 if(state.step>=6){location.replace('/');return}
 const names=['OpenAI API key','Your voice','Google Calendar','Gmail','Appearance','App PIN'];
 $('step-label').textContent=`${state.step+1} / 6 · ${names[state.step]}`;
 document.querySelector('.progress').innerHTML=names.map((_,i)=>`<span class="${i<=state.step?'active':''}"></span>`).join('');
 $('skip').hidden=state.step<2;$('back').hidden=state.step===0;$('continue').textContent=state.step===5?'Finish':'Continue';$('error').textContent='';
 const pages=[
 `<h1>A little intelligence.</h1><p>Add your OpenAI API key to enable Ask and voice transcription. This step is required.</p><ol><li>Open <a href="https://platform.openai.com/api-keys">OpenAI API keys</a> and sign in.</li><li>Create a new secret key and copy it here. Check your API billing balance if requests are rejected.</li></ol><label for="key">OpenAI API key</label><input id="key" type="password" autocomplete="new-password" spellcheck="false" placeholder="${state.keyConfigured?'Key saved · leave empty to keep it':'sk-…'}"><p>Your key is saved in encrypted app storage. <a href="https://developers.openai.com/api/docs/quickstart">Official OpenAI guide</a></p>`,
 `<h1>A familiar voice.</h1><p>Choose the voice for answers on Pika. You can change it later in Config.</p><label for="voice">Voice</label><select id="voice"><option value="cedar">Male · Cedar</option><option value="coral">Female · Coral</option></select>`,
 `<h1>Your calendar.</h1><p>Optional. Connect a calendar for events.</p><ol><li>In <a href="https://console.cloud.google.com/">Google Cloud</a>, enable Calendar API and create a service account with a JSON key.</li><li>Share your Google calendar with the JSON’s <code>client_email</code>, allowing changes to events.</li><li>Copy Calendar ID from Google Calendar → Settings → Integrate calendar, then choose the JSON below.</li></ol><label for="calendar-id">Calendar ID</label><input id="calendar-id" placeholder="…@group.calendar.google.com" autocomplete="off"><label for="calendar-file">Service account JSON key</label><input id="calendar-file" type="file" accept="application/json,.json"><p>You can also connect this later in Config.</p>`,
 `<h1>A little mail.</h1><p>Optional. Read recent unread messages. Pika cannot delete or change your mail.</p><ol><li>Enable Gmail API in <a href="https://console.cloud.google.com/">Google Cloud</a>. Configure OAuth consent, add your account as a test user and create a Desktop OAuth client.</li><li>On your computer, save that client JSON in <code>config/gmail-oauth-client.json</code> and run <code>python tools/gmail_connect.py</code> from the Pika repository.</li><li>Sign in, allow read-only mail access, and choose the generated <code>gmail-account.json</code> here.</li></ol><label for="mail-file">Gmail account JSON</label><input id="mail-file" type="file" accept="application/json,.json"><p>Sender filters are available later in Config.</p>`,
 `<h1>Make it yours.</h1><p>Choose your app’s appearance. Light is the default.</p><label for="theme">Theme</label><select id="theme"><option value="light">Light</option><option value="dark">Dark</option></select>`,
 `<h1>Just for you.</h1><p>Optional. Protect this app with a four-digit PIN. It does not change Bluetooth pairing.</p><label for="pin">4-digit PIN</label><input id="pin" type="password" inputmode="numeric" pattern="[0-9]{4}" maxlength="4" autocomplete="new-password"><label for="pin-repeat">Repeat PIN</label><input id="pin-repeat" type="password" inputmode="numeric" pattern="[0-9]{4}" maxlength="4" autocomplete="new-password">`
 ];
 $('slide').innerHTML=pages[state.step];if($('voice'))$('voice').value=state.voice||'cedar';if($('theme'))$('theme').value=state.theme||'light';
}
async function move(skip=false,back=false){
 if(busy)return;busy=true;$('error').textContent='';document.querySelectorAll('button').forEach(b=>b.disabled=true);
 try{
  const body={step:state.step,skip,back};
  if(!skip&&!back){switch(state.step){
   case 0:body.apiKey=$('key').value.trim();break;
   case 1:body.voice=$('voice').value;break;
   case 2:{body.calendarId=$('calendar-id').value.trim();const file=$('calendar-file').files[0];if(!file||file.size>12000)throw Error('Choose the service account JSON key');body.serviceJson=await file.text();JSON.parse(body.serviceJson);break}
   case 3:{const file=$('mail-file').files[0];if(!file||file.size>6000)throw Error('Choose gmail-account.json');body.credentials=JSON.parse(await file.text());break}
   case 4:body.theme=$('theme').value;break;
   case 5:body.pin=$('pin').value;body.repeatPin=$('pin-repeat').value;break;
  }}
  state=await api(body);document.documentElement.dataset.theme=state.theme;paint();window.scrollTo(0,0);
 }catch(error){$('error').textContent=error.message}finally{busy=false;document.querySelectorAll('button').forEach(b=>b.disabled=false)}
}
$('setup-form').addEventListener('submit',e=>{e.preventDefault();move()});$('skip').onclick=()=>move(true);$('back').onclick=()=>move(false,true);window.pikaHandleBack=()=>{if(state.step>0)move(false,true)};
window.pikaApplyTheme=theme=>document.documentElement.dataset.theme=theme;
api().then(result=>{state=result;paint()}).catch(error=>$('error').textContent=error.message);
