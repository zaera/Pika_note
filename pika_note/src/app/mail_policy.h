#pragma once
constexpr int kPikaMailLimit=4;

#include <string>
#include <vector>
#include "utf8_text.h"
inline std::vector<std::string> pikaMailSpeechChunks(const std::string& text,size_t maximum=500){
 std::vector<std::string> parts;if(maximum<4)return parts;size_t start=0;
 while(start<text.size()){
  size_t count=pikaUtf8Prefix(text.c_str()+start,maximum);
  if(start+count<text.size()){
   size_t split=std::string::npos;
   for(size_t i=count;i>count/3;--i){char c=text[start+i-1];if(c==' '||c=='\n'){split=i;break;}}
   if(split!=std::string::npos)count=split;
  }
  if(!count)break;parts.push_back(text.substr(start,count));start+=count;
 }return parts;
}

#include <cstdint>
inline bool pikaMailEligible(bool unreadOnly,bool unread,int64_t received,int64_t now){return !unreadOnly||(unread&&received>=now-7LL*24*60*60*1000);}

#include <cctype>
inline bool pikaMailDomainValid(const std::string& domain){
 if(domain.empty()||domain.size()>253||domain.find('.')==std::string::npos)return false;
 size_t start=0;while(start<domain.size()){size_t end=domain.find('.',start);if(end==std::string::npos)end=domain.size();size_t n=end-start;if(!n||n>63||domain[start]=='-'||domain[end-1]=='-')return false;for(size_t i=start;i<end;i++)if(!std::isalnum((unsigned char)domain[i])&&domain[i]!='-')return false;if(end==domain.size())return true;start=end+1;}return false;
}
inline bool pikaMailFilterValid(const std::string& filter){
 size_t at=filter.find('@');if(at==std::string::npos||filter.find('@',at+1)!=std::string::npos||!pikaMailDomainValid(filter.substr(at+1)))return false;
 for(size_t i=0;i<at;i++)if(!std::isalnum((unsigned char)filter[i])&&filter[i]!='.'&&filter[i]!='_'&&filter[i]!='-'&&filter[i]!='+')return false;return filter.size()<=254;
}
inline std::string pikaMailAddress(std::string from){size_t a=from.find('<'),b=from.find('>',a);if(a!=std::string::npos&&b!=std::string::npos)from=from.substr(a+1,b-a-1);while(!from.empty()&&std::isspace((unsigned char)from.front()))from.erase(0,1);while(!from.empty()&&std::isspace((unsigned char)from.back()))from.pop_back();for(char& c:from)c=std::tolower((unsigned char)c);return from;}
inline bool pikaMailSenderMatches(const std::string& from,const std::string& filter){if(filter.empty())return false;auto address=pikaMailAddress(from);return filter[0]=='@'?(address.size()>filter.size()&&address.compare(address.size()-filter.size(),filter.size(),filter)==0):address==filter;}
