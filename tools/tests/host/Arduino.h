
#pragma once
#include <string>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cctype>
#include <algorithm>
class String {
 std::string text;
 public:
 String(){} String(const char* value):text(value?value:""){} String(const std::string& v):text(v){}
 String(int v):text(std::to_string(v)){} String(unsigned long v):text(std::to_string(v)){}
 auto begin()const{return text.begin();} auto end()const{return text.end();}
 const char* c_str()const{return text.c_str();} size_t length()const{return text.size();}
 void replace(const String& a,const String& b){size_t p=0;while((p=text.find(a.c_str(),p))!=std::string::npos){text.replace(p,a.length(),b.c_str());p+=b.length();}}
 void toLowerCase(){for(char& c:text)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));}
 bool reserve(size_t n){text.reserve(n);return true;}
 bool concat(const char* s,size_t n){text.append(s,n);return true;}
 bool concat(const char* s){text.append(s);return true;} bool concat(char c){text+=c;return true;}
 void remove(size_t n){text.erase(n);} char operator[](size_t n)const{return text[n];}
 void setCharAt(size_t n,char c){text[n]=c;}
 String substring(size_t from,size_t to=std::string::npos)const{if(from>=text.size())return String();return text.substr(from,to==std::string::npos?to:to-from);}
 int indexOf(const char* c,int from=0)const{auto p=text.find(c,from);return p==std::string::npos?-1:(int)p;}
 int indexOf(char c,int from=0)const{auto p=text.find(c,from);return p==std::string::npos?-1:(int)p;}
 bool startsWith(const char* s)const{return text.rfind(s,0)==0;}
 bool endsWith(const char* s)const{return text.size()>=strlen(s)&&text.compare(text.size()-strlen(s),strlen(s),s)==0;}
 int toInt()const{return atoi(text.c_str());}
 void trim(){const auto a=text.find_first_not_of(" \r\n\t"),b=text.find_last_not_of(" \r\n\t");text=a==std::string::npos?"":text.substr(a,b-a+1);}
 String& operator+=(char c){text+=c;return *this;}
 String& operator+=(const String& v){text+=v.text;return *this;}
 friend String operator+(const String& a,const String& b){return a.text+b.text;}
 friend bool operator==(const String& a,const String& b){return a.text==b.text;}
 friend bool operator!=(const String& a,const String& b){return !(a==b);}
 friend bool operator<(const String& a,const String& b){return a.text<b.text;}
};
