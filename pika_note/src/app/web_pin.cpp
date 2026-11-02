#include <mbedtls/md.h>
#include "factory_reset.h"
#include "web_pin.h"
#include "settings.h"
#include "../../local_config.generated.h"
#include <ArduinoJson.h>
#include <Preferences.h>
#include <esp_random.h>
#include <mbedtls/sha256.h>
namespace {
Preferences prefs;String pinHash;int pinLength=4;unsigned failures=0;uint32_t blockedUntil=0;
struct Session{String token;uint32_t touched=0;} sessions[4];unsigned cursor=0;
String digest(const String& value){uint8_t bytes[32];mbedtls_sha256((const unsigned char*)value.c_str(),value.length(),bytes,0);char hex[65];for(int i=0;i<32;i++)sprintf(hex+i*2,"%02x",bytes[i]);return String(hex);}
bool equal(const String& a,const String& b){if(a.length()!=b.length())return false;uint8_t delta=0;for(size_t i=0;i<a.length();i++)delta|=a[i]^b[i];return delta==0;}
String requestToken(WebServer& server){String cookie=server.header("Cookie");int start=cookie.indexOf("pika_session=");if(start<0)return "";if(start>0&&cookie[start-1]!=' '&&cookie[start-1]!=';')return "";start+=13;int end=cookie.indexOf(';',start);return end<0?cookie.substring(start):cookie.substring(start,end);}
const char* lockPage=R"PIN(<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Unlock Pika</title><style>*{box-sizing:border-box}body{margin:0;background:#f6f4ef;color:#302e29;font-family:Arial,sans-serif;min-height:100dvh;display:grid;place-items:center}.lock{width:min(340px,90vw);padding:32px 16px;text-align:center}.logo{width:64px;height:64px;border-radius:50%;background:#e9e4da;display:grid;place-items:center;margin:0 auto 24px}.logo:after{content:'';width:34px;height:34px;border-radius:50%;background:#ef573f}h1{font-size:34px;letter-spacing:-1px;margin:0 0 9px}.hint{font-size:13px;color:#8a8073;min-height:20px}.dots{display:flex;justify-content:center;gap:13px;margin:28px 0 36px}.dot{width:11px;height:11px;border:1.5px solid #c9bdae;border-radius:50%}.dot.on{background:#ef573f;border-color:#ef573f}.keys{display:grid;grid-template-columns:repeat(3,1fr);gap:14px}button{border:0;border-radius:50%;width:70px;height:70px;justify-self:center;background:#e9e4da;color:#302e29;font:29px Arial;cursor:pointer;touch-action:manipulation}button{-webkit-tap-highlight-color:transparent;outline:none;user-select:none;-webkit-user-select:none}button:focus:not(:focus-visible){outline:none;box-shadow:none}button:active{background:#ef573f;color:white}button:focus-visible{outline:none;box-shadow:0 0 0 2px #ef573f}.erase{font-size:19px;background:transparent}.blank{visibility:hidden}.foot{margin-top:30px;font-size:11px;color:#968a7b}</style><main class="lock"><div class="logo" aria-hidden="true"></div><h1>Welcome back.</h1><p class="hint" id="status" role="status">Enter your Pika PIN</p><div class="dots" id="dots" aria-label="PIN digits"></div><div class="keys"><button data-n="1">1</button><button data-n="2">2</button><button data-n="3">3</button><button data-n="4">4</button><button data-n="5">5</button><button data-n="6">6</button><button data-n="7">7</button><button data-n="8">8</button><button data-n="9">9</button><span class="blank"></span><button data-n="0">0</button><button class="erase" id="erase" aria-label="Delete last digit">⌫</button></div><p class="foot">Your notes. Your space.</p></main><script>let pin='',busy=false;const size=PIN_LENGTH;const dots=document.querySelector('#dots'),status=document.querySelector('#status');function paint(){dots.replaceChildren(...Array.from({length:size},(_,i)=>{const dot=document.createElement('span');dot.className='dot'+(i<pin.length?' on':'');return dot}));dots.setAttribute('aria-label',pin.length+' of '+size+' digits entered')}async function digit(n){if(busy||pin.length>=size)return;pin+=n;paint();if(pin.length!==size)return;busy=true;status.textContent='Unlocking…';try{const result=await fetch('/auth/unlock',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams({pin})});if(result.ok){location.replace('/');return}status.textContent=result.status===429?'Too many attempts. Try again in 30 seconds.':'Incorrect PIN. Try again.'}catch{status.textContent='Pika is unavailable. Try again.'}pin='';busy=false;paint()}document.addEventListener('click',e=>{const button=e.target.closest('button');if(button?.dataset.n)digit(button.dataset.n);if(button?.id==='erase'&&!busy){pin=pin.slice(0,-1);paint()}});document.addEventListener('keydown',e=>{if(/^[0-9]$/.test(e.key)){e.preventDefault();digit(e.key)}else if(e.key==='Backspace'&&!busy){e.preventDefault();pin=pin.slice(0,-1);paint()}});paint();</script></html>)PIN";
}
void pikaLocalConfigBegin(){
 prefs.begin("pikaPin",false);pinHash=prefs.getString("hash","");pinLength=prefs.getInt("length",4);if(pinLength!=4){pinHash="";pinLength=4;}
 if(pikaFactoryConfigSuppressed())return;
 JsonDocument cfg;if(deserializeJson(cfg,PIKA_LOCAL_CONFIG_JSON))return;
 String pin=cfg["pin"]|"";
 if(pin.length()==4){bool valid=true;for(char c:pin)if(c<'0'||c>'9')valid=false;if(valid){String hash=digest(pin);if(prefs.getString("pinCfg","")!=hash){prefs.putString("hash",hash);prefs.putInt("length",pin.length());prefs.putString("pinCfg",hash);pinHash=hash;pinLength=pin.length();}}}
 String api=cfg["openai_api_key"]|"",key=cfg["google_service_json"]|"",calendar=cfg["google_calendar_id"]|"";
 if(api.length()){String fingerprint=digest(api);if(prefs.getString("apiCfg","")!=fingerprint&&pikaSaveSettings("","",api,pikaSpeechVoice()))prefs.putString("apiCfg",fingerprint);}
 if(key.length()&&calendar.length()){String fingerprint=digest(calendar+key);if(prefs.getString("googleCfg","")!=fingerprint&&pikaSaveGoogleSettings(calendar,key,true))prefs.putString("googleCfg",fingerprint);}
}
bool pikaWebSessionValid(WebServer& server){
 const String token=requestToken(server);
 for(auto& session:sessions)if(session.token.length()&&equal(token,session.token)&&millis()-session.touched<30UL*60*1000){session.touched=millis();return true;}
 return false;
}
bool pikaWebAccess(WebServer& server){
 const String path=server.uri();server.sendHeader("Cache-Control","no-store");
 if(path=="/portal.css")return true;
 if(path=="/auth/unlock"&&server.method()==HTTP_POST){
   if(blockedUntil&&static_cast<int32_t>(blockedUntil-millis())>0){server.send(429,"application/json","{\"error\":\"Wait 30 seconds\"}");return false;}
   if(!pinHash.length()||!equal(digest(server.arg("pin")),pinHash)){if(++failures>=5){blockedUntil=millis()+30000;failures=0;}server.send(401,"application/json","{\"error\":\"Incorrect PIN\"}");return false;}
   failures=0;blockedUntil=0;uint8_t bytes[32];esp_fill_random(bytes,sizeof(bytes));char token[65];for(int i=0;i<32;i++)sprintf(token+i*2,"%02x",bytes[i]);sessions[cursor]={String(token),millis()};cursor=(cursor+1)%4;
   server.sendHeader("Set-Cookie",String("pika_session=")+token+"; HttpOnly; SameSite=Strict; Path=/");server.send(200,"application/json","{\"unlocked\":true}");return false;
 }
 String token=requestToken(server);bool unlocked=false;for(auto& session:sessions){if(session.token.length()&&equal(token,session.token)&&millis()-session.touched<30UL*60*1000){session.touched=millis();unlocked=true;}}
 if(path=="/auth/logout"){if(server.method()!=HTTP_POST){server.send(405);return false;}for(auto& session:sessions)if(equal(token,session.token))session.token="";server.sendHeader("Set-Cookie","pika_session=; HttpOnly; SameSite=Strict; Path=/; Max-Age=0");server.send(200,"application/json","{}");return false;}
 if(unlocked)return true;
 if(path.startsWith("/api/")||server.method()!=HTTP_GET){server.send(401,"application/json","{\"error\":\"Unlock Pika first\"}");return false;}
 if(!pinHash.length()){server.send(503,"text/html; charset=utf-8","<meta name='viewport' content='width=device-width'><p>Set a 4-digit PIN in config/local.json, then build and flash Pika.</p>");return false;}
 String html(lockPage);html.replace("PIN_LENGTH",String(pinLength));server.send(200,"text/html; charset=utf-8",html);return false;
}

bool pikaWebChangePin(const String& current,const String& next,String& error){
 if(blockedUntil&&static_cast<int32_t>(blockedUntil-millis())>0){error="Wait 30 seconds before retrying";return false;}
 if(next.length()!=4){error="New PIN must contain four digits";return false;}for(char c:next)if(c<'0'||c>'9'){error="New PIN must contain four digits";return false;}
 if(!equal(digest(current),pinHash)){if(++failures>=5){blockedUntil=millis()+30000;failures=0;}error="Incorrect current PIN";return false;}
 const String hash=digest(next);if(prefs.putString("hash",hash)!=64){error="Could not save PIN";return false;}pinHash=hash;pinLength=4;failures=0;blockedUntil=0;for(auto& session:sessions)session.token="";return true;
}

bool pikaWebPinProof(const String& nonce,const String& proof){if(nonce.length()!=64||proof.length()!=64||pinHash.length()!=64)return false;unsigned char out[32];char hex[65];if(mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),(const unsigned char*)pinHash.c_str(),pinHash.length(),(const unsigned char*)nonce.c_str(),nonce.length(),out))return false;for(int i=0;i<32;i++)sprintf(hex+2*i,"%02x",out[i]);hex[64]=0;return equal(hex,proof);}
