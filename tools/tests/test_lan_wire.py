"""Exercise the production Android AES-GCM record layer on the host JDK."""
from pathlib import Path
import subprocess, tempfile

root = Path(__file__).resolve().parents[2]
jdk = Path(r'C:\Program Files\Eclipse Adoptium\jdk-17.0.19.10-hotspot\bin')
source = r'''
package me.pikanote.companion;
import java.io.*;import java.util.*;
public class LanWireCheck {
 static byte[] key=new byte[32],challenge=new byte[32];
 static void check(boolean ok){if(!ok)throw new AssertionError();}
 public static void main(String[] args)throws Exception {
  for(int i=0;i<32;i++){key[i]=(byte)i;challenge[i]=(byte)(31-i);}
  ByteArrayOutputStream bytes=new ByteArrayOutputStream();
  LanWire phone=new LanWire(new ByteArrayInputStream(new byte[0]),bytes,key,challenge,true);
  byte[] payload=new byte[4006];new Random(42).nextBytes(payload);
  for(int i=0;i<2000;i++)phone.write(payload);
  byte[] all=bytes.toByteArray();check(all.length==2000*(4006+20));
  LanWire device=new LanWire(new ByteArrayInputStream(all),new ByteArrayOutputStream(),key,challenge,false);
  for(int i=0;i<2000;i++)check(Arrays.equals(device.read(),payload));
  check(!Arrays.equals(Arrays.copyOfRange(all,4,510),payload));
  check(!Arrays.equals(Arrays.copyOfRange(all,4,526),Arrays.copyOfRange(all,530,1052)));
  byte[] tampered=all.clone();tampered[24]^=1;boolean failed=false;
  try{new LanWire(new ByteArrayInputStream(tampered),new ByteArrayOutputStream(),key,challenge,false).read();}catch(Exception expected){failed=true;}check(failed);
  byte[] other=challenge.clone();other[0]^=1;failed=false;
  try{new LanWire(new ByteArrayInputStream(all),new ByteArrayOutputStream(),key,other,false).read();}catch(Exception expected){failed=true;}check(failed);
  failed=false;try{new LanWire(new ByteArrayInputStream(new byte[]{127,0,0,0}),new ByteArrayOutputStream(),key,challenge,false).read();}catch(IOException expected){failed=true;}check(failed);
  ByteArrayOutputStream reverse=new ByteArrayOutputStream();device=new LanWire(new ByteArrayInputStream(new byte[0]),reverse,key,challenge,false);device.write(LanWire.HELLO);
  check(Arrays.equals(new LanWire(new ByteArrayInputStream(reverse.toByteArray()),bytes,key,challenge,true).read(),LanWire.HELLO));
  System.out.println("LAN wire: bidirectional stream, 1 MB, unique nonces, tampering, wrong session, oversized record: PASS");
 }
}
'''
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    test = path / 'LanWireCheck.java'
    test.write_text(source, encoding='utf-8')
    subprocess.run([str(jdk/'javac.exe'), '-d', str(path), str(root/'android-companion/app/src/main/java/me/pikanote/companion/LanWire.java'), str(test)], check=True)
    subprocess.run([str(jdk/'java.exe'), '-cp', str(path), 'me.pikanote.companion.LanWireCheck'], check=True)
