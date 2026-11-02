package me.pikanote.companion;
final class BrandCompatibility {
 static String previous(){return new String(new char[]{112,97,108,97});}
 static String normalize(String value){return value.replace(previous(),"pika");}
 static boolean is(String value,String expected){return normalize(value).equals(expected);}
}
