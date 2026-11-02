package me.pikanote.companion;
import android.content.*;import java.io.*;import java.util.concurrent.*;import org.json.*;
final class LiveActionChecks{
 static void run(Context parent)throws Exception{
  Context box=new ContextWrapper(parent){public File getFilesDir(){File p=new File(super.getFilesDir(),"live-action-check");p.mkdirs();return p;}};Store store=new Store(box);
  class Delayed extends BleTransport{
   final Semaphore entered=new Semaphore(0),release=new Semaphore(0);int calls=0;
   Delayed(){super(box,null);online=true;session=1;address="fixture";}
   @Override Reply rpc(String path,String method,String body,JSONObject args)throws Exception{if(!method.equals("POST"))throw new Exception("Unexpected extra RPC");int call=++calls;entered.release();if(!release.tryAcquire(4,TimeUnit.SECONDS))throw new Exception("Test gate timeout");if(call==4)throw new IOException("Simulated preview timeout");return Store.json(200,new JSONObject().put("saved",true));}
  }
  Delayed b=new Delayed();store.transport=b;ExecutorService workers=Executors.newFixedThreadPool(3);
  try{for(int n=1;n<=5;n++){
   Future<BleTransport.Reply> request=workers.submit(()->store.api("/api/manager/settings","POST","{\"action\":\"soundTest\",\"melody\":6,\"volume\":60}"));
   if(!b.entered.tryAcquire(1,TimeUnit.SECONDS))throw new Exception("Live request did not start");
   try{
    Future<String> read=workers.submit(()->{store.api("/api/manager","GET",null);return store.libraryVersion();});read.get(1,TimeUnit.SECONDS);
    Future<Boolean> duplicate=workers.submit(()->{try{store.api("/api/manager/settings","POST","{\"action\":\"soundTest\",\"melody\":6,\"volume\":60}");return false;}catch(IOException expected){return expected.getMessage().contains("already playing");}});if(!duplicate.get(1,TimeUnit.SECONDS))throw new Exception("Duplicate preview was not rejected promptly");
   }finally{b.release.release();}
   try{if(request.get(2,TimeUnit.SECONDS).status!=200||n==4)throw new Exception("Unexpected live result");}catch(ExecutionException e){if(n!=4||!(e.getCause() instanceof IOException))throw e;}
  }
  if(store.hasPending()||store.snapshot().getJSONArray("tasks").length()!=0)throw new Exception("Sound preview mutated phone records");
  store.state.getJSONArray("pending").put(new JSONObject().put("path","/api/manager").put("body",new JSONObject().put("kind","tasks")));
  b.release.release();if(store.api("/api/manager/settings","POST","{\"action\":\"soundTest\",\"melody\":11,\"volume\":60}").status!=200||store.state.getJSONArray("pending").length()!=1)throw new Exception("Queued edit blocked or was changed by sound preview");
  boolean guarded=false;try{store.api("/api/manager/settings","POST","{\"action\":\"askRenew\"}");}catch(IOException expected){guarded=expected.getMessage().contains("pending phone changes");}if(!guarded)throw new Exception("Mutating action bypassed pending changes guard");
  }finally{b.release.release(8);workers.shutdownNow();b.writer.shutdownNow();b.network.shutdownNow();PhoneBackup.remove(box.getFilesDir());}
 }
}
