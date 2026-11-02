package me.pikanote.companion;
import java.util.*;import java.util.zip.CRC32;import java.io.*;
final class UploadWindowChecks {
 static long crc(byte[] b){CRC32 c=new CRC32();c.update(b);return c.getValue();}
 static void require(boolean ok){if(!ok)throw new AssertionError("Upload integrity check failed");}
 static void run()throws Exception{
  byte[] source=new byte[139308];new Random(1234).nextBytes(source);ByteArrayOutputStream sink=new ByteArrayOutputStream();UploadWindow window=new UploadWindow(123);
  for(int start=0;start<source.length;start+=4096){int end=Math.min(source.length,start+4096);byte[] block=Arrays.copyOfRange(source,start,end);
   window.receive(999,start,block);require(window.seal(999,start,end,crc(block))==null);
   // Missing notification: seal rejects the entire window, never writes a prefix.
   window.receive(123,start+1,Arrays.copyOfRange(block,1,Math.min(2,block.length)));require(window.seal(123,start,end,crc(block))==null);
   // Corruption: checksum rejects it and allows a complete retry.
   byte[] bad=block.clone();bad[0]^=1;window.receive(123,start,bad);require(window.seal(123,start,end,crc(block))==null);
   for(int at=0;at<block.length;at+=492){byte[] part=Arrays.copyOfRange(block,at,Math.min(at+492,block.length));window.receive(123,start+at,part);window.receive(123,start+at,part);}
   byte[] accepted=window.seal(123,start,end,crc(block));require(Arrays.equals(accepted,block));require(window.seal(123,start,end,crc(block))==null);sink.write(accepted);window.committed(end);
   // Lost ACK/replayed block: acknowledged end stays fixed; do not duplicate TLS bytes.
   window.receive(123,start,block);require(window.seal(123,start,end,crc(block))==null);require(window.accepted()==end);
  }
  require(Arrays.equals(source,sink.toByteArray()));System.out.println("Upload windows: gaps, corruption, duplicates, stale token, lost ACK replay; 139308 bytes identical: PASS");
 }
 public static void main(String[] args)throws Exception{run();}
}
