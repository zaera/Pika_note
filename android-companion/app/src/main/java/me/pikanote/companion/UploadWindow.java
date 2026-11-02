package me.pikanote.companion;
import java.util.Arrays;
import java.util.zip.CRC32;
/** Bounded, idempotent receive window. No bytes reach TLS before a checked seal. */
final class UploadWindow {
 final long token;final byte[] bytes=new byte[4096];int accepted=0,buffered=0;boolean committing=false;
 UploadWindow(long token){this.token=token;}
 synchronized void receive(long value,int offset,byte[] data){
  if(value!=token||committing||offset<accepted||offset!=accepted+buffered||data.length>bytes.length-buffered)return;
  System.arraycopy(data,0,bytes,buffered,data.length);buffered+=data.length;
 }
 synchronized byte[] seal(long value,int start,int end,long crc){
  if(value!=token||committing||start!=accepted||end<start||end-start>bytes.length)return null;
  CRC32 check=new CRC32();check.update(bytes,0,buffered);
  if(buffered!=end-start||check.getValue()!=crc){buffered=0;return null;}
  committing=true;return Arrays.copyOf(bytes,buffered);
 }
 synchronized void committed(int end){if(!committing||end!=accepted+buffered)throw new IllegalStateException("Upload offset changed");accepted=end;buffered=0;committing=false;}
 synchronized String diagnostic(){return "accepted="+accepted+" buffered="+buffered+" committing="+committing;}
 synchronized int accepted(){return accepted;}
 synchronized boolean committing(){return committing;}
}
