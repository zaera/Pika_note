package me.pikanote.companion;

import java.io.*;
import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;
import java.security.*;
import javax.crypto.*;
import javax.crypto.spec.*;

// A fresh server challenge derives a different AES key on every TCP connection.
// The bootstrap key travels only over the authenticated owner's encrypted BLE link.
final class LanWire {
 static final byte[] HELLO="PIKA-LAN-1".getBytes(StandardCharsets.US_ASCII);
 final DataInputStream in;final DataOutputStream out;final byte[] key;
 final int txDirection,rxDirection;long tx=0,rx=0;
 LanWire(InputStream input,OutputStream output,byte[] secret,byte[] challenge,boolean phone)throws Exception{
  in=new DataInputStream(input);out=new DataOutputStream(output);
  Mac mac=Mac.getInstance("HmacSHA256");mac.init(new SecretKeySpec(secret,"HmacSHA256"));key=mac.doFinal(challenge);
  txDirection=phone?0x50484f4e:0x50494b41;rxDirection=phone?0x50494b41:0x50484f4e;
 }
 byte[] crypt(int mode,int direction,long sequence,byte[] bytes)throws Exception{
  Cipher cipher=Cipher.getInstance("AES/GCM/NoPadding");byte[] nonce=ByteBuffer.allocate(12).putInt(direction).putLong(sequence).array();
  cipher.init(mode,new SecretKeySpec(key,"AES"),new GCMParameterSpec(128,nonce));return cipher.doFinal(bytes);
 }
 synchronized void write(byte[] frame)throws Exception{
  if(frame.length>4096||tx==Long.MAX_VALUE)throw new IOException("Invalid LAN frame");
  byte[] sealed=crypt(Cipher.ENCRYPT_MODE,txDirection,tx++,frame);byte[] record=ByteBuffer.allocate(4+sealed.length).putInt(sealed.length).put(sealed).array();out.write(record);out.flush();
 }
 byte[] read()throws Exception{
  int size=in.readInt();if(size<16||size>4112||rx==Long.MAX_VALUE)throw new IOException("Invalid LAN record");byte[] sealed=new byte[size];in.readFully(sealed);
  return crypt(Cipher.DECRYPT_MODE,rxDirection,rx++,sealed);
 }
}
