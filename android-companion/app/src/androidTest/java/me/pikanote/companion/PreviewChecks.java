package me.pikanote.companion;
import android.app.Instrumentation;
import java.io.IOException;
import java.util.concurrent.*;
final class PreviewChecks {
 static void run(Instrumentation test)throws Exception {
  BleTransport b=new BleTransport(test.getTargetContext(),null);b.online=true;
  CountDownLatch locked=new CountDownLatch(1),release=new CountDownLatch(1);
  Thread other=new Thread(()->{b.rpcLock.lock();try{locked.countDown();release.await();}catch(InterruptedException e){Thread.currentThread().interrupt();}finally{b.rpcLock.unlock();}});other.start();
  try {if(!locked.await(2,TimeUnit.SECONDS))throw new AssertionError("RPC holder failed");long at=System.nanoTime();boolean rejected=false;try{b.previewRpc("/api/manager/settings","{\"action\":\"soundTest\"}");}catch(IOException busy){rejected=true;}if(!rejected||TimeUnit.NANOSECONDS.toMillis(System.nanoTime()-at)>200)throw new AssertionError("Preview waited behind RPC");if(b.previewUnavailable().isEmpty())throw new AssertionError("Busy channel reported ready");if(!b.pending.isEmpty())throw new AssertionError("Rejected preview was queued");}
  finally {release.countDown();other.join(2000);b.writer.shutdownNow();b.network.shutdownNow();}
 }
}
