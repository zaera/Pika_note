from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[2]
jdk=Path(r'C:\Program Files\Eclipse Adoptium\jdk-17.0.19.10-hotspot\bin')
code=r'''package me.pikanote.companion;
public class SsidCheck{static void check(boolean b){if(!b)throw new AssertionError();}public static void main(String[] a){
 check(WifiSsidPolicy.matches("zaera2","\"zaera2\""));check(WifiSsidPolicy.matches("zaera2","zaera2"));
 check(!WifiSsidPolicy.matches("zaera2","\"zaera5\""));check(!WifiSsidPolicy.matches("zaera2","Zaera2"));
 check(!WifiSsidPolicy.matches("zaera2","<unknown ssid>"));check(!WifiSsidPolicy.matches("zaera2",null));check(!WifiSsidPolicy.matches("",""));
 check(WifiSsidPolicy.matches(" ssid ","\" ssid \""));check(!WifiSsidPolicy.matches("ssid","\" ssid \""));
 check(WifiSsidPolicy.matches("\"ssid\"","\"\"ssid\"\""));WifiSsidPolicy.Observation observed=new WifiSsidPolicy.Observation();Object first=new Object(),second=new Object();
 check(observed.resolve(first,"<unknown ssid>").isEmpty());check(observed.resolve(first,"\"zaera2\"").equals("\"zaera2\""));
 check(observed.resolve(first,"<unknown ssid>").equals("\"zaera2\""));check(observed.resolve(second,"<unknown ssid>").isEmpty());
 observed.resolve(first,"zaera2");observed.clear(second);check(observed.resolve(first,null).equals("zaera2"));observed.clear(first);check(observed.resolve(first,null).isEmpty());
 observed.resolve(first,"zaera2");observed.clear(null);check(observed.resolve(first,null).isEmpty());check(observed.resolve(null,"zaera2").isEmpty());
 System.out.println("Exact SSID policy: match, quotes, mismatch, case, unknown, null and significant whitespace: PASS");}}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);src=p/'SsidCheck.java';src.write_text(code,encoding='utf8')
 subprocess.run([str(jdk/'javac.exe'),'-d',str(p),str(root/'android-companion/app/src/main/java/me/pikanote/companion/WifiSsidPolicy.java'),str(src)],check=True)
 subprocess.run([str(jdk/'java.exe'),'-cp',str(p),'me.pikanote.companion.SsidCheck'],check=True)
fw=(root/'pika_note/src/app/phone_lan.cpp').read_text(encoding='utf8')
assert 'String(input["phoneSsid"]|"")!=next.ssid' in fw
assert 'WiFi.SSID()==current.ssid' in fw
print('Firmware independently checks offered phone SSID and associated SSID: PASS')
