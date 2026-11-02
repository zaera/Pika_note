package me.pikanote.companion;
import java.nio.*;import java.util.*;
final class LiveSnapshotChecks{
 static byte[] frame(int token,int total,int offset,byte[] bytes){return ByteBuffer.allocate(12+bytes.length).order(ByteOrder.LITTLE_ENDIAN).putInt(token).putInt(total).putInt(offset).put(bytes).array();}
 static void run(){byte[] source=new byte[20000];new Random(42).nextBytes(source);LiveSnapshotInbox in=new LiveSnapshotInbox();byte[] complete=null;for(int at=0;at<source.length;at+=488){byte[] f=frame(7,source.length,at,Arrays.copyOfRange(source,at,Math.min(source.length,at+488)));complete=in.accept(f);if(complete==null)in.accept(f);}if(!Arrays.equals(source,complete))throw new AssertionError("Live snapshot changed");if(in.accept(frame(8,300000,0,new byte[0]))!=null)throw new AssertionError("Oversize accepted");if(in.accept(frame(8,100,50,new byte[50]))!=null)throw new AssertionError("Out of order accepted");in.reset();if(in.accept(frame(7,100,50,new byte[50]))!=null)throw new AssertionError("Old tail accepted after reset");}
}
