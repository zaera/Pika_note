package me.pikanote.companion;
import android.app.Instrumentation;import android.content.Intent;import org.json.*;
final class BrandLiveChecks{
 static void check(boolean value,String reason)throws Exception{if(!value)throw new Exception(reason);}
 static void run(Instrumentation i)throws Exception{
 i.startActivitySync(new Intent(i.getTargetContext(),MainActivity.class).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK));PikaService service=null;long until=System.currentTimeMillis()+45000;
 while(System.currentTimeMillis()<until){service=PikaService.instance;if(service!=null&&service.ble.online&&!service.syncing&&!service.ble.deviceBusy)break;Thread.sleep(200);}
 check(service!=null&&service.ble.online&&!service.syncing,"Device not ready");service.maintenance=true;
 try{JSONObject settings=service.ble.rpc("/api/phone/settings-export","GET",null,null).json();JSONObject spaces=settings.getJSONObject("namespaces");java.util.Iterator<String> keys=spaces.keys();while(keys.hasNext())check(!keys.next().contains(BrandCompatibility.previous()),"Legacy settings namespace");check(spaces.has("pikaExtras")&&spaces.has("pika"),"Settings namespaces missing");
 JSONObject manager=service.ble.rpc("/api/manager","GET",null,null).json();check(manager.getJSONObject("settings").optString("firmwareVersion").contains("v8.0"),"Firmware version");check(manager.getJSONObject("settings").getJSONArray("vibrationPatterns").length()==6,"Vibration lost");
 JSONObject files=service.ble.rpc("/api/phone/files","GET",null,null).json();check(files.getJSONArray("files").length()>30,"Device library missing");
 android.os.Bundle output=new android.os.Bundle();output.putString("device","Settings namespaces: "+spaces.length()+"; files: "+files.getJSONArray("files").length());i.sendStatus(0,output);
 }finally{service.maintenance=false;}
 }
}