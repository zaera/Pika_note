package me.pikanote.companion;
import android.app.*;import android.content.*;import org.json.*;import java.io.*;
final class V7WizardChecks {
 static void run(Instrumentation i)throws Exception{
  Context c=V7Checks.sandbox(i.getTargetContext(),"wizard");PhoneBackup.remove(c.getFilesDir());Store s=new Store(c);
  try{
   s.state.put("device","fixture").put("setupWizardStep",0);s.save();
   if(!SetupWizard.required(s)||SetupWizard.step(s)!=0)throw new Exception("Required API step missing");
   try{SetupWizard.advance(s,new JSONObject().put("step",0).put("skip",true));throw new Exception("Required key skipped");}catch(IOException expected){}
   SetupWizard.advance(s,new JSONObject().put("step",0).put("apiKey","sk-fixture-onboarding-key-123456"));
   Store resumed=new Store(c);if(SetupWizard.step(resumed)!=1||!DeviceLifecycle.paired(resumed))throw new Exception("Setup did not resume with existing pair");
   SetupWizard.advance(resumed,new JSONObject().put("step",1));
   if(!resumed.snapshot().getJSONObject("settings").optString("voice").equals("cedar"))throw new Exception("Male voice not default");
   for(int step=2;step<=5;step++)SetupWizard.advance(resumed,new JSONObject().put("step",step).put("skip",true));
   if(SetupWizard.required(resumed)||!resumed.state.optString("theme").equals("light")||resumed.state.optBoolean("appLock"))throw new Exception("Optional defaults failed");
   resumed.state.getJSONObject("settingsSecrets").remove("apiKey");if(SetupWizard.step(resumed)!=0)throw new Exception("Missing key bypassed required setup");
  }finally{PhoneBackup.remove(c.getFilesDir());}
 }
 static void ui(Instrumentation i)throws Exception{
  i.runOnMainSync(()->i.getTargetContext().stopService(new Intent(i.getTargetContext(),PikaService.class)));long stopAt=System.currentTimeMillis()+5000;while(PikaService.instance!=null&&System.currentTimeMillis()<stopAt)Thread.sleep(50);if(PikaService.instance!=null)throw new Exception("Real service must be stopped before isolated wizard UI");
  Context c=V7Checks.sandbox(i.getTargetContext(),"wizard-ui");PhoneBackup.remove(c.getFilesDir());Store s=new Store(c);s.state.put("device","fixture").put("appLock",false).put("setupWizardStep",0);s.save();
  java.lang.reflect.Field field=Store.class.getDeclaredField("singleton");field.setAccessible(true);field.set(null,s);MainActivity.instrumentPreview=true;MainActivity a=null;
  try{
   a=(MainActivity)i.startActivitySync(new Intent(i.getTargetContext(),MainActivity.class).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK));
   final MainActivity activity=a;final String html=new String(i.getTargetContext().getAssets().open("setup.html").readAllBytes(),java.nio.charset.StandardCharsets.UTF_8);
   i.runOnMainSync(()->activity.web.loadDataWithBaseURL("https://pika.local/",html,"text/html","UTF-8",null));
   UiRefreshChecks.await(i,a,"!!document.querySelector('#key')&&document.querySelector('#skip').hidden");
   UiRefreshChecks.eval(i,a,"document.querySelector('#key').value='sk-fixture-wizard-ui-key-123456';document.querySelector('#continue').click();true");
   UiRefreshChecks.await(i,a,"document.querySelector('#voice')?.value==='cedar'");
   i.runOnMainSync(()->activity.web.loadDataWithBaseURL("https://pika.local/",html.replace("<body>","<body data-wizard-test=second>"),"text/html","UTF-8",null));
   UiRefreshChecks.await(i,a,"document.body.dataset.wizardTest==='second'&&document.querySelector('#voice')?.value==='cedar'");
   UiRefreshChecks.eval(i,a,"document.querySelector('#continue').click();true");UiRefreshChecks.await(i,a,"!!document.querySelector('#calendar-id')");
   UiRefreshChecks.eval(i,a,"document.querySelector('#skip').click();true");UiRefreshChecks.await(i,a,"!!document.querySelector('#mail-file')");
   UiRefreshChecks.eval(i,a,"document.querySelector('#skip').click();true");UiRefreshChecks.await(i,a,"document.querySelector('#theme')?.value==='light'");
   UiRefreshChecks.eval(i,a,"document.querySelector('#theme').value='dark';document.querySelector('#continue').click();true");
   UiRefreshChecks.await(i,a,"!!document.querySelector('#pin')&&document.documentElement.dataset.theme==='dark'");
   UiRefreshChecks.eval(i,a,"document.querySelector('#skip').click();true");UiRefreshChecks.await(i,a,"typeof render==='function'");
   if(SetupWizard.required(s))throw new Exception("Wizard did not finish");
  }finally{if(a!=null){MainActivity activity=a;i.runOnMainSync(activity::finish);}MainActivity.instrumentPreview=false;field.set(null,null);PhoneBackup.remove(c.getFilesDir());}
 }

}
