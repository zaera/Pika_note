#pragma once
#include <string>
#include <cstdint>
#include <cctype>
inline void pikaMailUtf8(std::string& out,uint32_t c){if(c<128)out+=char(c);else if(c<2048){out+=char(0xc0|(c>>6));out+=char(0x80|(c&63));}else if(c<65536){out+=char(0xe0|(c>>12));out+=char(0x80|((c>>6)&63));out+=char(0x80|(c&63));}else{out+=char(0xf0|(c>>18));out+=char(0x80|((c>>12)&63));out+=char(0x80|((c>>6)&63));out+=char(0x80|(c&63));}}
inline int pikaMailHex(char c){return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;}
inline std::string pikaMailHeader(const std::string& input){
 std::string out;size_t pos=0;bool previousEncoded=false;
 while(pos<input.size()){
  size_t start=input.find("=?",pos);if(start==std::string::npos){out+=input.substr(pos);break;}
  std::string before=input.substr(pos,start-pos);size_t q=input.find('?',start+2),end=q==std::string::npos?q:input.find("?=",q+3);
  if(q==std::string::npos||q+2>=input.size()||input[q+2]!='?'||end==std::string::npos){out+=input.substr(pos);break;}
  std::string charset=input.substr(start+2,q-start-2);for(char& c:charset)c=std::tolower((unsigned char)c);
  if(charset!="utf-8"&&charset!="us-ascii"&&charset!="iso-8859-1"&&charset!="windows-1251"){out+=input.substr(pos,end+2-pos);pos=end+2;previousEncoded=false;continue;}
  char encoding=std::tolower((unsigned char)input[q+1]);std::string raw,payload=input.substr(q+3,end-q-3);bool valid=true;
  if(encoding=='b'){unsigned acc=0;int bits=0;for(char c:payload){if(c=='=')break;const std::string alphabet="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";size_t n=alphabet.find(c);if(n==std::string::npos){valid=false;break;}acc=(acc<<6)|n;bits+=6;if(bits>=8){bits-=8;raw+=char((acc>>bits)&255);}}}
  else if(encoding=='q'){for(size_t i=0;i<payload.size();i++){char c=payload[i];if(c=='_')raw+=' ';else if(c=='='){if(i+2>=payload.size()||pikaMailHex(payload[i+1])<0||pikaMailHex(payload[i+2])<0){valid=false;break;}raw+=char(pikaMailHex(payload[i+1])*16+pikaMailHex(payload[i+2]));i+=2;}else raw+=c;}}
  else valid=false;
  if(!valid){out+=input.substr(pos,end+2-pos);pos=end+2;previousEncoded=false;continue;}
  bool whitespace=true;for(char c:before)whitespace&=std::isspace((unsigned char)c)!=0;if(!(previousEncoded&&whitespace))out+=before;
  if(charset=="iso-8859-1"||charset=="windows-1251"){for(unsigned char c:raw){uint32_t code=c;if(charset=="windows-1251"){if(c>=192)code=0x410+c-192;else if(c==168)code=0x401;else if(c==184)code=0x451;else if(c>=128)code='?';}pikaMailUtf8(out,code);}}else out+=raw;
  pos=end+2;previousEncoded=true;
 }for(char& c:out)if((unsigned char)c<32)c=' ';return out;
}
inline std::string pikaMailSnippet(const std::string& input){
 std::string out;for(size_t i=0;i<input.size();i++){if(input[i]!='&'){out+=input[i];continue;}size_t end=input.find(';',i);if(end==std::string::npos||end-i>12){out+='&';continue;}std::string key=input.substr(i+1,end-i-1);uint32_t code=0;if(key=="amp")code='&';else if(key=="lt")code='<';else if(key=="gt")code='>';else if(key=="quot")code='"';else if(key=="apos")code='\'';else if(key=="nbsp")code=' ';else if(key.size()>1&&key[0]=='#'){bool hex=key[1]=='x'||key[1]=='X';size_t at=hex?2:1;bool valid=at<key.size();for(;at<key.size();at++){int digit=hex?pikaMailHex(key[at]):key[at]>='0'&&key[at]<='9'?key[at]-'0':-1;if(digit<0){valid=false;break;}code=code*(hex?16:10)+digit;if(code>0x10ffff){valid=false;break;}}if(!valid)code=0;}
 if(code&&code<=0x10ffff&&!(code>=0xd800&&code<=0xdfff)){pikaMailUtf8(out,code);i=end;}else out+='&';
 }return out;
}
inline std::string pikaMailBase64Body(const char* input,size_t maximum=12000){std::string out;unsigned bits=0,value=0;const std::string alphabet="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";for(const char* p=input;*p&&out.size()<maximum;p++){char c=*p;if(c=='=')break;if(c=='-')c='+';if(c=='_')c='/';size_t n=alphabet.find(c);if(n==std::string::npos)continue;value=(value<<6)|n;bits+=6;if(bits>=8){bits-=8;out+=char((value>>bits)&255);}}return out;}
inline std::string pikaMailPlainHtml(const std::string& html){
 std::string out;bool tag=false;std::string token,skip;
 for(size_t i=0;i<html.size();i++){
  char c=html[i];if(c=='<'){tag=true;token.clear();continue;}
  if(tag){if(c=='>'){tag=false;std::string name=token.substr(0,token.find_first_of(" \t\r\n"));for(char& ch:name)ch=std::tolower((unsigned char)ch);
    if(name=="script"||name=="style"||name=="blockquote")skip=name;
    std::string lowerToken=token;for(char& ch:lowerToken)ch=std::tolower((unsigned char)ch);
    if(skip.empty()&&name=="a"&&lowerToken.find("href")!=std::string::npos)out+=" ссылка ";
    else if(!skip.empty()&&name=="/"+skip)skip.clear();
    if(skip.empty()&&(name=="br"||name=="br/"||name=="/p"||name=="/div"||name=="/li"))out+='\n';
   }else if(token.size()<200)token+=c;continue;}
  if(skip.empty())out+=c;
 }return pikaMailSnippet(out);
}
inline std::string pikaMailLatestText(const std::string& body){
 std::string out;size_t start=0;
 while(start<body.size()){size_t end=body.find('\n',start);if(end==std::string::npos)end=body.size();std::string line=body.substr(start,end-start);size_t at=line.find_first_not_of(" \t\r");std::string clean=at==std::string::npos?"":line.substr(at);std::string folded=clean;for(char& c:folded)c=std::tolower((unsigned char)c);
  if(folded.find("-----original message-----")!=std::string::npos||folded.find("-----forwarded message-----")!=std::string::npos||(folded.rfind("on ",0)==0&&folded.find("wrote:")!=std::string::npos)||folded.rfind("from:",0)==0||clean.rfind("От:",0)==0||clean.find("написал:")!=std::string::npos||clean.find("писал(а):")!=std::string::npos)break;
  if(clean.empty()||clean[0]!='>'){out+=line;out+='\n';}start=end+1;
 }return out;
}
inline std::string pikaMailSpeechClean(const std::string& raw){
 std::string input=pikaMailSnippet(raw),out;
 for(size_t i=0;i<input.size();){
  bool url=input.compare(i,7,"http://")==0||input.compare(i,8,"https://")==0||input.compare(i,4,"www.")==0;
  if(url){out+=" ссылка ";while(i<input.size()&&!std::isspace((unsigned char)input[i])&&input[i]!='<'&&input[i]!='>'&&input[i]!='"'&&input[i]!=')')i++;continue;}
  char c=input[i++];if(c=='<'||c=='>'||c=='['||c==']'||c=='('||c==')'||c=='*'||c=='_'||c=='#'||c=='|'||c=='/'||c=='\\'||c=='='||c=='&'){out+=' ';continue;}
  out+=c;
 }return out;
}
inline std::string pikaMailCharset(std::string raw,std::string charset){for(char& c:charset)c=std::tolower((unsigned char)c);if(charset!="windows-1251"&&charset!="iso-8859-1")return raw;std::string out;for(unsigned char c:raw){uint32_t code=c;if(charset=="windows-1251"){if(c>=192)code=0x410+c-192;else if(c==168)code=0x401;else if(c==184)code=0x451;else if(c>=128)code='?';}pikaMailUtf8(out,code);}return out;}
