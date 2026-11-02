package me.pikanote.companion;
import android.app.Instrumentation;import android.content.*;import org.json.*;import java.io.*;import java.nio.file.Files;import java.util.*;import java.util.zip.*;import javax.crypto.*;
final class BrandChecks{
 static void check(boolean value,String reason)throws Exception{if(!value)throw new Exception(reason);}
 static void run(Instrumentation i)throws Exception{
 Context real=i.getTargetContext();JSONObject state=new JSONObject(new String(Vault.read(new File(real.getFilesDir(),"phone-store.aes")),"UTF-8"));
 check(state.getJSONObject("snapshot").getJSONArray("memories").length()==25,"Memory migration count");check(state.getJSONObject("snapshot").getJSONArray("tasks").length()==33,"Task migration count");check(state.optString("ownerKey").length()==64,"Owner missing");check(state.has("settingsSecrets"),"Credentials missing");
 File root=new File(real.getFilesDir(),"brand-test");root.mkdirs();Context box=new ContextWrapper(real){public File getFilesDir(){return root;}};Store target=new Store(box);target.state.put("ownerKey","keep-owner");target.save();
 JSONObject manifest=new JSONObject().put("format",BrandCompatibility.previous()+"-phone").put("version",1).put("state",new JSONObject(target.state.toString()));byte[] content="0\n".getBytes("UTF-8");String hash=PhoneVoice.hash(write(new File(root,"fixture"),content));manifest.put("files",new JSONArray().put(new JSONObject().put("name","index.csv").put("size",content.length).put("sha256",hash)));
 ByteArrayOutputStream clear=new ByteArrayOutputStream();try(ZipOutputStream zip=new ZipOutputStream(clear)){zip.putNextEntry(new ZipEntry(BrandCompatibility.previous()+"-phone.json"));zip.write(manifest.toString().getBytes("UTF-8"));zip.closeEntry();zip.putNextEntry(new ZipEntry("notes/index.csv"));zip.write(content);zip.closeEntry();}
 byte[] magic={80,65,76,65,55,0},salt=new byte[16],nonce=new byte[12];char[] password="brand-test-password".toCharArray();ByteArrayOutputStream encrypted=new ByteArrayOutputStream();encrypted.write(magic);encrypted.write(salt);encrypted.write(nonce);encrypted.write(PortableBackup.cipher(Cipher.ENCRYPT_MODE,password,salt,nonce,magic).doFinal(clear.toByteArray()));
 PortableBackup.restore(target,new ByteArrayInputStream(encrypted.toByteArray()),password.clone());check(target.state.optString("ownerKey").equals("keep-owner"),"Backup changed ownership");check(target.safeFile("index.csv").isFile(),"Legacy backup missing file");PhoneBackup.remove(root);
 Files.write(new File(real.getFilesDir(),"recovery-export.json").toPath(),Vault.read(new File(real.getFilesDir(),"phone-store.aes")));
 }
 static File write(File f,byte[] b)throws Exception{Files.write(f.toPath(),b);return f;}
}
