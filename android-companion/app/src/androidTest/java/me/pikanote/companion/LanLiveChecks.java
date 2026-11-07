package me.pikanote.companion;
import android.app.*;import android.content.*;import org.json.*;import java.io.*;import java.util.*;
final class LanLiveChecks {
 static void await(java.util.function.BooleanSupplier condition,int seconds)throws Exception{long end=android.os.SystemClock.elapsedRealtime()+seconds*1000L;while(!condition.getAsBoolean()){if(android.os.SystemClock.elapsedRealtime()>end)throw new IOException("Live connection timed out");Thread.sleep(25);}}
 static JSONObject run(Instrumentation i)throws Exception{
  Context context=i.getTargetContext();context.startForegroundService(new Intent(context,PikaService.class));await(()->PikaService.instance!=null&&PikaService.instance.ble!=null,5);PikaService service=PikaService.instance;service.maintenance=true;
  try{await(()->service.ble.online,30);BleTransport ble=service.ble;ble.lan.offer();await(()->ble.lan.online(),25);
   Store store=Store.get(context);File sample=null;File[] files=store.mirror.listFiles((d,n)->n.matches("note_[0-9]+\\.wav"));if(files!=null)for(File file:files)if(sample==null||file.length()>sample.length())sample=file;
   if(sample==null)throw new IOException("No existing WAV for read-only benchmark");int count=(int)Math.min(sample.length(),131072);long at=android.os.SystemClock.elapsedRealtime();int offset=0;
   ble.rpc("/api/phone/lease","GET",null,new JSONObject().put("active",true));
   try(RandomAccessFile expected=new RandomAccessFile(sample,"r")){while(offset<count){BleTransport.Reply reply=ble.rpc("/api/phone/read-fast","GET",null,new JSONObject().put("name",sample.getName()).put("size",sample.length()).put("offset",offset));if(reply.status!=200||reply.bytes.length==0)throw new IOException("Read benchmark failed");byte[] local=new byte[reply.bytes.length];expected.readFully(local);if(!Arrays.equals(local,reply.bytes))throw new IOException("LAN data mismatch");offset+=reply.bytes.length;}}
   long download=android.os.SystemClock.elapsedRealtime()-at;StringBuilder payload=new StringBuilder();for(int n=0;n<32768;n++)payload.append((char)('a'+n%26));long hash=2166136261L;for(int n=0;n<payload.length();n++)hash=((hash^payload.charAt(n))*16777619L)&0xffffffffL;
   at=android.os.SystemClock.elapsedRealtime();for(int n=0;n<8;n++){BleTransport.Reply reply=ble.rpc("/api/phone/transfer-probe","POST",payload.toString(),null);if(reply.status!=200||reply.json().optLong("checksum")!=hash||reply.json().optInt("bytes")!=32768)throw new IOException("Upload benchmark mismatch");}long upload=android.os.SystemClock.elapsedRealtime()-at;
   ble.rpc("/api/phone/lease","GET",null,new JSONObject().put("active",false));
   return new JSONObject().put("downloadBytes",offset).put("downloadMs",download).put("uploadBytes",262144).put("uploadMs",upload).put("lan",ble.lan.online());
  }finally{service.maintenance=false;if(service.ble!=null&&service.ble.online)service.scheduleSync();}
 }
}
