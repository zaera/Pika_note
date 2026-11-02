package me.pikanote.companion;
import android.app.Instrumentation;
import android.content.*;
import org.json.*;
import java.io.*;
import java.nio.file.Files;
final class AudioAvailabilityChecks {
 static void run(Instrumentation test)throws Exception {
  Context box=new ContextWrapper(test.getTargetContext()){@Override public File getFilesDir(){File f=new File(super.getFilesDir(),"audio-ready-"+android.os.Process.myPid());f.mkdirs();return f;}};
  Store store=new Store(box);
  try {
   store.state.getJSONObject("snapshot").getJSONArray("memories").put(new JSONObject().put("id","m987").put("text","Audio pending").put("wav",true));store.save();
   String before=store.libraryVersion();
   JSONObject pending=store.api("/api/manager","GET",null).json().getJSONArray("memories").getJSONObject(0);
   if(pending.getBoolean("audioReady"))throw new AssertionError("Missing WAV marked ready");
   Files.write(store.safeFile("note_987.wav").toPath(),new byte[]{82,73,70,70});
   store.state.getJSONObject("files").put("note_987.wav","test-verified-sha");store.save();
   if(before.equals(store.libraryVersion()))throw new AssertionError("WAV arrival did not invalidate UI version");
   if(!store.api("/api/manager","GET",null).json().getJSONArray("memories").getJSONObject(0).getBoolean("audioReady"))throw new AssertionError("Downloaded WAV still unavailable");
  } finally {PhoneBackup.remove(box.getFilesDir());}
 }
}
