package me.pikanote.companion;
import java.io.IOException;
import java.time.*;
/** Normalize equivalent planner output; reject real times in all-day records. */
final class PlannedDates {
 static String currentMonthDate(String query,String day){
  String folded=query.toLowerCase(java.util.Locale.ROOT);if(!(folded.contains("этого месяца")||folded.contains("текущего месяца")||folded.contains("this month")||folded.contains("current month"))||day.length()<10)return "";
  java.util.regex.Matcher m=java.util.regex.Pattern.compile("[0-9]+").matcher(query);int chosen=0,count=0;while(m.find())try{int n=Integer.parseInt(m.group());if(n>=1&&n<=31){chosen=n;count++;}}catch(NumberFormatException ignored){}return count==1?day.substring(0,7)+String.format(java.util.Locale.ROOT,"-%02d",chosen):"";
 }
 static String canonical(String value,boolean allDay,boolean end){
  String v=value.trim();
  if(allDay&&v.length()>=16){String clock=v.substring(10);boolean midnight=clock.equals("T00:00")||clock.equals("T00:00:00");boolean last=end&&(clock.equals("T23:59")||clock.equals("T23:59:00")||clock.equals("T23:59:59"));if(midnight||last)return v.substring(0,10);}
  return v;
 }
 static void validate(String date,String end,boolean event,boolean allDay)throws IOException {
  try{
   if(event&&allDay){if(!date.matches("[0-9]{4}-[0-9]{2}-[0-9]{2}")||!end.matches("[0-9]{4}-[0-9]{2}-[0-9]{2}"))throw new IllegalArgumentException();LocalDate start=LocalDate.parse(date),last=LocalDate.parse(end);if(last.isBefore(start))throw new IllegalArgumentException();return;}
   LocalDateTime first=null,last=null;
   for(String value:new String[]{date,end}){if(value.isEmpty())continue;if(!value.matches("[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}"))throw new IllegalArgumentException();LocalDateTime local=LocalDateTime.parse(value);if(ZoneId.of("Europe/Kyiv").getRules().getValidOffsets(local).isEmpty())throw new IllegalArgumentException();}
   if(event){first=LocalDateTime.parse(date);last=LocalDateTime.parse(end);if(!last.isAfter(first))throw new IllegalArgumentException();}
  }catch(Exception bad){throw new IOException("Invalid planned date or event range",bad);}
 }
}
