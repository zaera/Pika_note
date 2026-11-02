package me.pikanote.companion;
import java.io.*;import java.nio.*;import java.util.*;
final class LiveSnapshotInbox {
 long token=-1;int expected=0;final ByteArrayOutputStream data=new ByteArrayOutputStream();
 synchronized byte[] accept(byte[] frame){if(frame.length<12)return null;ByteBuffer b=ByteBuffer.wrap(frame).order(ByteOrder.LITTLE_ENDIAN);long id=b.getInt()&0xffffffffL;int total=b.getInt(),offset=b.getInt();if(total<1||total>262144||offset<0||offset>total)return null;if(id!=token){if(offset!=0)return null;token=id;expected=total;data.reset();}if(total!=expected||offset!=data.size()||frame.length-12>total-offset)return null;data.write(frame,12,frame.length-12);if(data.size()!=expected)return null;byte[] complete=data.toByteArray();token=-1;expected=0;data.reset();return complete;}
 synchronized void reset(){token=-1;expected=0;data.reset();}
}
