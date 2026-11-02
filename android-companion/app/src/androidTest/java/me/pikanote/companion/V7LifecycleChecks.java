package me.pikanote.companion;
import android.app.*;import android.content.*;import java.io.*;import java.nio.file.*;import java.nio.charset.StandardCharsets;import java.util.*;import org.json.*;
/** Explicitly invoked real-device lifecycle probes; never part of the APK. */
final class V7LifecycleChecks {
 static PikaService ready(Instrumentation i)throws Exception{
  i.startActivitySync(new Intent(i.getTargetContext(),MainActivity.class).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK));
  long end=System.currentTimeMillis()+90000;
  while(System.currentTimeMillis()<end){PikaService s=PikaService.instance;if(s!=null&&s.ble.online&&!s.syncing&&!s.ble.deviceBusy&&!s.voiceBusy)return s;Thread.sleep(200);}
  PikaService s=PikaService.instance;throw new IOException("Pika not idle: service="+(s!=null)+(s==null?"":" online="+s.ble.online+" sync="+s.syncing+" busy="+s.ble.deviceBusy+" voice="+s.voiceBusy+" status="+s.status+" syncStatus="+Store.get(i.getTargetContext()).syncMessage));
 }
 static void forget(Instrumentation i)throws Exception{
  Context c=i.getTargetContext();ready(i);Store before=Store.get(c);
  if(before.hasPending())throw new IOException("Pending changes; do not forget yet");
  DeviceLifecycle.forget(c);
  Store after=Store.get(c);
  if(DeviceLifecycle.paired(after)||!after.state.optString("ownerKey").isEmpty()||after.state.has("settingsSecrets"))throw new IOException("Phone lifecycle cleanup incomplete");
  for(String kind:new String[]{"memories","tasks","events"})if(after.snapshot().getJSONArray(kind).length()!=0)throw new IOException("Old records remain after Forget");
  MainActivity a=MainActivity.active;if(a==null)throw new IOException("Onboarding activity unavailable");
  UiRefreshChecks.await(i,a,"!!document.querySelector('#slide')&&document.body.textContent.includes('Hello, Pika.')");
 }
 static void restore(Instrumentation i)throws Exception{
  Context c=i.getTargetContext();Store s=Store.get(c);
  if(!DeviceLifecycle.paired(s)||s.hasPending())throw new IOException("Pair clean Pika before restore");
  File backup=new File(c.getCacheDir(),"lifecycle-restore.pika-backup"),passwordFile=new File(c.getCacheDir(),"lifecycle-password");
  char[] password=new String(Files.readAllBytes(passwordFile.toPath()),StandardCharsets.UTF_8).trim().toCharArray();
  String owner=s.state.optString("ownerKey"),address=s.state.optString("device");
  try(InputStream in=new FileInputStream(backup)){
   PortableBackup.restore(s,in,password);
   if(!owner.equals(s.state.optString("ownerKey"))||!address.equals(s.state.optString("device")))throw new IOException("Restore replaced target ownership");
   if(!s.restoredPending()||s.state.optJSONObject("restoreDeviceSecrets")==null)throw new IOException("Restore missing records/settings intent");
   s.authorizeRestore();
  }finally{Arrays.fill(password,'\0');passwordFile.delete();backup.delete();}
  JSONObject expectedSecrets=new JSONObject(s.state.getJSONObject("restoreDeviceSecrets").toString());
  int[] counts=new int[3];String[] kinds={"memories","tasks","events"};for(int n=0;n<3;n++)counts[n]=s.snapshot().getJSONArray(kinds[n]).length();
  i.startActivitySync(new Intent(c,MainActivity.class).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK));
  long end=System.currentTimeMillis()+600000,next=0;PikaService service=null;
  while(System.currentTimeMillis()<end){
   service=PikaService.instance;
   if(service!=null&&service.ble.online&&!service.syncing&&!s.restoredPending())break;
   if(System.currentTimeMillis()>=next){android.os.Bundle status=new android.os.Bundle();status.putString("restore",s.syncMessage);i.sendStatus(0,status);next=System.currentTimeMillis()+10000;}
   Thread.sleep(250);
  }
  if(service==null||!service.ble.online||s.restoredPending()||service.syncing)throw new IOException("Restore did not finish: "+s.syncMessage);
  BleTransport.Reply reply=service.ble.rpc("/api/phone/settings-export","GET",null,null);
  if(reply.status!=200||!reply.json().toString().equals(expectedSecrets.toString()))throw new IOException("Restored device settings differ from backup");
  synchronized(s){JSONObject remote=s.state.optJSONObject("remoteSnapshot");if(remote==null)throw new IOException("No confirmed remote library");for(int n=0;n<3;n++)if(remote.getJSONArray(kinds[n]).length()!=counts[n])throw new IOException("Restored device record count differs: "+kinds[n]);}
 }
 static boolean same(Object a,Object b)throws Exception{
  if(a instanceof JSONObject&&b instanceof JSONObject){JSONObject x=(JSONObject)a,y=(JSONObject)b;if(x.length()!=y.length())return false;Iterator<String> keys=x.keys();while(keys.hasNext()){String k=keys.next();if(!y.has(k)||!same(x.get(k),y.get(k)))return false;}return true;}
  if(a instanceof JSONArray&&b instanceof JSONArray){JSONArray x=(JSONArray)a,y=(JSONArray)b;if(x.length()!=y.length())return false;for(int n=0;n<x.length();n++)if(!same(x.get(n),y.get(n)))return false;return true;}
  return String.valueOf(a).equals(String.valueOf(b));
 }
 static void verify(Instrumentation i)throws Exception{
  Context c=i.getTargetContext();JSONObject expected=new JSONObject(new String(Files.readAllBytes(new File(c.getCacheDir(),"lifecycle-expected.json").toPath()),StandardCharsets.UTF_8));
  i.startActivitySync(new Intent(c,MainActivity.class).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK));
  Store s=Store.get(c);long end=System.currentTimeMillis()+600000,next=0;PikaService service=null;
  while(System.currentTimeMillis()<end){service=PikaService.instance;if(service!=null&&service.ble.online&&!service.syncing&&!s.restoredPending())break;if(System.currentTimeMillis()>=next){android.os.Bundle status=new android.os.Bundle();status.putString("restore",s.syncMessage);i.sendStatus(0,status);next=System.currentTimeMillis()+10000;}Thread.sleep(250);}
  if(service==null||!service.ble.online||service.syncing||s.restoredPending())throw new IOException("Restore not finished: "+s.syncMessage);
  BleTransport.Reply reply=service.ble.rpc("/api/phone/settings-export","GET",null,null);
  if(reply.status!=200||!same(reply.json(),expected.getJSONObject("deviceSecrets")))throw new IOException("Device settings differ from backup");
  JSONObject remote=s.state.optJSONObject("remoteSnapshot");if(remote==null)throw new IOException("No remote snapshot");
  for(String kind:new String[]{"memories","tasks","events"})if(remote.getJSONArray(kind).length()!=expected.getJSONObject("counts").getInt(kind))throw new IOException("Record count differs: "+kind);
  for(Iterator<String> names=expected.getJSONObject("files").keys();names.hasNext();){String name=names.next();if(name.equals("manager.json")&&expected.has("manager")){JSONObject actual=new JSONObject(new String(Files.readAllBytes(s.safeFile(name).toPath()),StandardCharsets.UTF_8)),original=new JSONObject(expected.getJSONObject("manager").toString());actual.remove("revision");original.remove("revision");if(!same(actual,original))throw new IOException("Restored manager differs beyond revision");}else if(!s.safeFile(name).isFile()||!PhoneVoice.hash(s.safeFile(name)).equals(expected.getJSONObject("files").getString(name)))throw new IOException("Restored phone file mismatch: "+name);}
  if(expected.has("askTurns")&&!same(PhoneAsk.ordered(s),expected.getJSONArray("askTurns")))throw new IOException("Restored Ask dialogue differs from backup");
  if(s.hasPending())throw new IOException("Pending changes remain");
 }

}
