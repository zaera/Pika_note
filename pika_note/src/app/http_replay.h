#pragma once
#include <Arduino.h>
#include <vector>
// Replays completed transport steps while the state machine resumes on the UI thread.
class PikaHttpReplay {
 struct Reply {String key,body;int code;};std::vector<Reply> replies;size_t cursor=0;
 public:void begin(){cursor=0;}
 bool read(const String& key,String& body,int& code){
  if(cursor<replies.size()&&replies[cursor].key==key){body=replies[cursor].body;code=replies[cursor++].code;return true;}
  replies.resize(cursor);return false;
 }
 void append(const String& key,const String& body,int code){replies.push_back({key,body,code});}
 void clear(){replies.clear();cursor=0;}
 bool empty()const{return replies.empty();}
};
