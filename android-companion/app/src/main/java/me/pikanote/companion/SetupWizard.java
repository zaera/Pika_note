package me.pikanote.companion;
import org.json.*;import java.io.*;
/** Durable post-pair setup. The required key step is enforced outside the WebView. */
final class SetupWizard {
 static int step(Store s){synchronized(s){if(!hasKey(s))return 0;return Math.max(0,Math.min(6,s.state.optInt("setupWizardStep",6)));}}
 static boolean hasKey(Store s){JSONObject keys=s.state.optJSONObject("settingsSecrets");return keys!=null&&keys.optString("apiKey").trim().length()>=20;}
 static boolean required(Store s){return DeviceLifecycle.paired(s)&&step(s)<6;}
 static JSONObject status(Store s)throws Exception{synchronized(s){JSONObject settings=s.snapshot().getJSONObject("settings");return new JSONObject().put("step",step(s)).put("keyConfigured",hasKey(s)).put("voice",settings.optString("voice","cedar")).put("theme",s.state.optString("theme","light"));}}
 static JSONObject advance(Store s,JSONObject r)throws Exception{
  if(!DeviceLifecycle.paired(s))throw new IOException("Connect your Pika first");
  int current=step(s);if(current>=6)return status(s);
  if(r.optInt("step",-1)!=current)throw new IOException("Setup changed; reopen this step");
  if(r.optBoolean("back")){if(current>0){synchronized(s){s.state.put("setupWizardStep",current-1);s.save();}}return status(s);}
  boolean skip=r.optBoolean("skip");JSONObject settings=null;
  switch(current){
   case 0:{if(skip)throw new IOException("Add your OpenAI API key to continue");String key=r.optString("apiKey").trim();if(key.isEmpty()&&hasKey(s))break;if(!key.startsWith("sk-")||key.length()<20||key.length()>512||key.matches(".*\\s+.*"))throw new IOException("Paste the complete OpenAI API key");settings=new JSONObject().put("action","ai").put("apiKey",key).put("voice","cedar");break;}
   case 1:{String voice=r.optString("voice","cedar");if(!voice.equals("cedar")&&!voice.equals("coral"))throw new IOException("Choose a voice");settings=new JSONObject().put("action","ai").put("voice",voice);break;}
   case 2:if(!skip){String id=r.optString("calendarId").trim();JSONObject json=new JSONObject(r.optString("serviceJson"));if(id.isEmpty()||id.equals("primary")||!json.optString("type").equals("service_account")||json.optString("client_email").isEmpty()||json.optString("private_key").isEmpty())throw new IOException("Choose a service account JSON and the shared Calendar ID");settings=new JSONObject().put("action","google").put("calendarId",id).put("serviceJson",json.toString()).put("enabled",true);}break;
   case 3:if(!skip){JSONObject json=r.getJSONObject("credentials");if(!json.optString("scope").equals("https://www.googleapis.com/auth/gmail.readonly")||json.optString("client_id").isEmpty()||json.optString("client_secret").isEmpty()||json.optString("refresh_token").isEmpty()||json.optString("email").isEmpty())throw new IOException("Choose gmail-account.json from the Gmail connection helper");settings=new JSONObject().put("action","mail").put("credentials",json).put("senders",new JSONArray()).put("unreadOnly",true);}break;
   case 4:{String theme=skip?"light":r.optString("theme","light");if(!theme.equals("light")&&!theme.equals("dark"))throw new IOException("Choose light or dark");synchronized(s){s.state.put("theme",theme);s.save();}break;}
   case 5:if(!skip){String pin=r.optString("pin");if(!pin.matches("[0-9]{4}")||!pin.equals(r.optString("repeatPin")))throw new IOException("Enter the same four digits twice");synchronized(s){s.state.put("appPin",pin).put("appLock",true);s.save();}}break;
  }
  if(settings!=null){BleTransport.Reply result=s.api("/api/manager/settings","POST",settings.toString());if(result.status>=400)throw new IOException(result.json().optString("error","Could not save settings"));}
  synchronized(s){if(!hasKey(s))throw new IOException("Add your OpenAI API key to continue");s.state.put("setupWizardStep",current+1);s.save();}
  return status(s);
 }
}
