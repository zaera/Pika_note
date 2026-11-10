package me.pikanote.companion;
/** Exact, case-sensitive SSID gate. Unknown identity always keeps BLE. */
final class WifiSsidPolicy {
 static String identity(String value){if(value==null)return "";if(value.length()>=2&&value.charAt(0)=='"'&&value.charAt(value.length()-1)=='"')value=value.substring(1,value.length()-1);return value.equals("<unknown ssid>")?"":value;}
 static boolean matches(String configured,String phone){return configured!=null&&!configured.isEmpty()&&configured.equals(identity(phone));}
 // A redacted background read may reuse only the same still-live Network.
 static final class Observation {
  private Object network;private String ssid="";
  synchronized String resolve(Object current,String raw){if(current==null){clear(null);return "";}if(!identity(raw).isEmpty()){network=current;ssid=raw;return raw;}if(current.equals(network))return ssid;clear(null);return "";}
  synchronized void clear(Object lost){if(lost==null||lost.equals(network)){network=null;ssid="";}}
 }
}
