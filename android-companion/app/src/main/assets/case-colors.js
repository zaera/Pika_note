/* Phone-only continuous case palette. The source SVGs stay unchanged. */
(()=>{
 let taps=0,first=0,last=0,dialog,canvas,context,field='body',hue=0,saturation=0,value=1;
 let colors={body:'#e9e9e9',buttons:'#ef573f'};
 const resetTaps=()=>{taps=first=last=0};
 function rgb(h,s,v){const c=v*s,x=c*(1-Math.abs(h/60%2-1)),m=v-c;const a=h<60?[c,x,0]:h<120?[x,c,0]:h<180?[0,c,x]:h<240?[0,x,c]:h<300?[x,0,c]:[c,0,x];return a.map(n=>Math.round((n+m)*255))}
 function hex(){return '#'+rgb(hue,saturation,value).map(n=>n.toString(16).padStart(2,'0')).join('')}
 function readHex(text){const [r,g,b]=text.slice(1).match(/../g).map(n=>parseInt(n,16)/255),max=Math.max(r,g,b),min=Math.min(r,g,b),d=max-min;hue=!d?0:max===r?60*((g-b)/d%6):max===g?60*((b-r)/d+2):60*((r-g)/d+4);if(hue<0)hue+=360;saturation=max?d/max:0;value=max}
 function paint(){
  context.fillStyle=`hsl(${hue} 100% 50%)`;context.fillRect(0,0,320,200);
  const white=context.createLinearGradient(0,0,320,0);white.addColorStop(0,'#fff');white.addColorStop(1,'#fff0');context.fillStyle=white;context.fillRect(0,0,320,200);
  const black=context.createLinearGradient(0,0,0,200);black.addColorStop(0,'#0000');black.addColorStop(1,'#000');context.fillStyle=black;context.fillRect(0,0,320,200);
  context.beginPath();context.arc(saturation*320,(1-value)*200,6,0,Math.PI*2);context.strokeStyle=value>.6?'#222':'#fff';context.lineWidth=2;context.stroke();
  dialog.querySelector('#case-hue').value=String(hue);dialog.querySelector('#case-hex').value=colors[field];
  for(const key of ['body','buttons']){const b=dialog.querySelector(`[data-case-field="${key}"]`);b.style.setProperty('--swatch',colors[key]);b.setAttribute('aria-pressed',String(field===key))}
 }
 function select(key){field=key;readHex(colors[key]);paint()}
 function update(){colors[field]=hex();paint()}
 function redraw(){document.querySelectorAll('.pika-render img').forEach(img=>img.src=img.src.split('?')[0]+'?palette='+Date.now())}
 async function save(body){const r=await fetch('/api/phone/case-colors',{method:'POST',body:JSON.stringify(body)});if(!r.ok)throw Error('Colors not saved');redraw();dialog.close()}
 function ensure(){
  if(dialog)return;
  dialog=document.createElement('dialog');dialog.id='case-colors-dialog';dialog.className='dialog';
  dialog.innerHTML='<form class="dialog-scroll"><h2>Your Pika.</h2><p class="helper">Case colors · this app only</p><div class="case-fields"><button class="btn" type="button" data-case-field="body">Body</button><button class="btn" type="button" data-case-field="buttons">Side buttons</button></div><canvas id="case-palette" width="320" height="200" aria-label="Choose saturation and brightness"></canvas><label for="case-hue">Hue</label><input id="case-hue" type="range" min="0" max="359" step="1"><div class="field"><label for="case-hex">HEX color</label><input id="case-hex" type="text" maxlength="7" pattern="#[a-fA-F0-9]{6}" autocomplete="off" spellcheck="false" placeholder="#ef573f"><small class="helper">Choose on the palette or enter any #RRGGBB color.</small></div><div class="actions"><button class="btn" type="button" data-case-default>Default</button><button class="btn" type="button" data-case-cancel>Cancel</button><button class="moment" type="submit">Save colors</button></div></form>';
  document.body.append(dialog);canvas=dialog.querySelector('canvas');context=canvas.getContext('2d');
  const point=e=>{const r=canvas.getBoundingClientRect();saturation=Math.max(0,Math.min(1,(e.clientX-r.left)/r.width));value=1-Math.max(0,Math.min(1,(e.clientY-r.top)/r.height));update()};
  canvas.onpointerdown=e=>{e.preventDefault();canvas.setPointerCapture(e.pointerId);point(e)};
  canvas.onpointermove=e=>{if(canvas.hasPointerCapture(e.pointerId))point(e)};
  canvas.onpointerup=e=>{if(canvas.hasPointerCapture(e.pointerId))canvas.releasePointerCapture(e.pointerId)};
  dialog.querySelector('#case-hue').oninput=e=>{hue=Number(e.target.value);update()};
  dialog.querySelector('#case-hex').oninput=e=>{const text=e.target.value.trim();if(/^#[a-fA-F0-9]{6}$/.test(text)){colors[field]=text.toLowerCase();readHex(text);paint()}};
  dialog.querySelectorAll('[data-case-field]').forEach(b=>b.onclick=()=>select(b.dataset.caseField));
  dialog.querySelector('[data-case-cancel]').onclick=()=>dialog.close();
  dialog.querySelector('[data-case-default]').onclick=()=>save({reset:true}).catch(e=>toast(e.message));
  dialog.querySelector('form').onsubmit=e=>{e.preventDefault();if(!/^#[a-fA-F0-9]{6}$/.test(dialog.querySelector('#case-hex').value))return;save(colors).catch(error=>toast(error.message))};
 }
 document.addEventListener('visibilitychange',resetTaps);window.addEventListener('blur',resetTaps);
 document.addEventListener('click',async e=>{
  if(!e.target.closest('.pika-render')||e.target.closest('[data-tab]')||document.querySelector('dialog[open]')){resetTaps();return}
  const now=performance.now();if(!taps||now-last>550||now-first>1800){taps=0;first=now}last=now;
  if(++taps<5)return;resetTaps();
  try{const r=await fetch('/api/phone/case-colors');if(!r.ok)throw Error('Cannot load case colors');colors=await r.json();ensure();select('body');dialog.showModal()}catch(error){toast(error.message)}
 });
 // Rebuilding Today or navigating away also ends an unfinished tap sequence.
 document.addEventListener('DOMContentLoaded',()=>{const c=document.getElementById('content');if(c)new MutationObserver(resetTaps).observe(c,{childList:true})});
})();
