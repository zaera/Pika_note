package me.pikanote.companion;
import android.app.*;import android.content.*;import org.json.*;
final class LiveSoundChecks{
 static void run(Instrumentation i)throws Exception{
 Context c=i.getTargetContext();MainActivity a=(MainActivity)i.startActivitySync(new Intent(c,MainActivity.class).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK));Store store=Store.get(c);long until=System.currentTimeMillis()+40000;while(System.currentTimeMillis()<until&&(store.transport==null||!store.transport.online||store.hasPending()))Thread.sleep(200);if(store.transport==null||!store.transport.online)throw new Exception("Pika not connected");if(store.hasPending())throw new Exception("Pending changes not drained");
 // Only the test Activity is unlocked; saved app PIN settings stay unchanged.
 a.unlocked=true;
 for(int n=1;n<=5;n++){long at=System.currentTimeMillis();BleTransport.Reply reply=a.appApi("/api/manager/settings","POST",new JSONObject().put("action","soundTest").put("melody",new int[]{11,12,7,-1,6}[n-1]).put("volume",60).toString());if(reply.status>=400||!reply.json().optBoolean("saved"))throw new Exception("Sound preview "+n+" rejected: "+reply.status);android.os.Bundle progress=new android.os.Bundle();progress.putString("preview",n+"/5 in "+(System.currentTimeMillis()-at)+" ms");i.sendStatus(0,progress);Thread.sleep(650);}
 }
}
