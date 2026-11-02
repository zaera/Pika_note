from pathlib import Path
import subprocess,tempfile,sys
root=Path(__file__).resolve().parents[2]
source=r'''
#include "mail_policy.h"
#include "mail_text.h"
#include <cassert>
#include <iostream>
int main(){
 assert(kPikaMailLimit==4);
 assert(pikaMailFilterValid("@concept3d.com"));assert(pikaMailFilterValid("support@example.com"));
 assert(!pikaMailFilterValid("@example..com"));assert(!pikaMailFilterValid("@-example.com"));assert(!pikaMailFilterValid("@example.com OR from:other"));
 assert(pikaMailSenderMatches("Concept3D <info@concept3d.com>","@concept3d.com"));
 assert(!pikaMailSenderMatches("info@evilconcept3d.com","@concept3d.com"));assert(!pikaMailSenderMatches("info@concept3d.com.evil.org","@concept3d.com"));
 assert(pikaMailSenderMatches("Support <SUPPORT@EXAMPLE.COM>","support@example.com"));
 assert(pikaMailHeader("=?UTF-8?B?0J/RgNC40LLQtdGC?=")=="Привет");
 assert(pikaMailHeader("=?UTF-8?Q?Hello_world?= <a@example.com>")=="Hello world <a@example.com>");
 assert(pikaMailHeader("=?UTF-8?Q?Hello?=  =?UTF-8?Q?_world?=")=="Hello world");
 assert(pikaMailHeader("=?windows-1251?Q?=CF=F0=E8=E2=E5=F2?=")=="Привет");
 assert(pikaMailHeader("=?UTF-8?Q?bad=ZZ?=")=="=?UTF-8?Q?bad=ZZ?=");
 assert(pikaMailHeader("ordinary subject")=="ordinary subject");
 assert(pikaMailSnippet("One &amp; two &#39; &#x41; &unknown;")=="One & two ' A &unknown;");
 assert(pikaMailSnippet("&#xD800; &#99999999;")=="&#xD800; &#99999999;");
 const int64_t now=1800000000000LL;
 assert(pikaMailEligible(true,true,now-6LL*86400000,now));
 assert(!pikaMailEligible(true,true,now-8LL*86400000,now));
 assert(!pikaMailEligible(true,false,now-1000,now));
 assert(pikaMailEligible(false,false,now-100LL*86400000,now));
 std::string longText;for(int i=0;i<100;i++)longText+="Письмо: проверка полного превью без обрыва. ";
 auto chunks=pikaMailSpeechChunks(longText);assert(chunks.size()>1);std::string joined;
 for(const auto& chunk:chunks){assert(chunk.size()<=500);assert(!chunk.empty());assert(((unsigned char)chunk[0]&0xc0)!=0x80);joined+=chunk;}assert(joined==longText);
 assert(pikaMailSpeechChunks("short")[0]=="short");assert(pikaMailSpeechChunks("").empty());
 assert(pikaMailBase64Body("0J_RgNC40LLQtdGC")=="Привет");
 assert(pikaMailLatestText("Latest text\nOn Monday somebody wrote:\nOld text").find("Old")==std::string::npos);
 assert(pikaMailSpeechClean("Open https://example.com/secret?x=y please").find("https")==std::string::npos);
 assert(pikaMailSpeechClean("Open https://example.com/secret?x=y please").find("ссылка")!=std::string::npos);
 assert(pikaMailPlainHtml("<p>Hello</p><script>bad()</script>").find("bad")==std::string::npos);
 assert(pikaMailPlainHtml("<a href=\"https://example.com\">Open</a>").find("ссылка")!=std::string::npos);
 assert(pikaMailCharset(std::string("\xcf\xf0\xe8\xe2\xe5\xf2"),"windows-1251")=="Привет");
 std::cout<<"Mail address/domain filters, week cutoff, safe text and speech chunks: PASS\n";
}
'''
with tempfile.TemporaryDirectory() as directory:
 p=Path(directory);(p/'test.cpp').write_text(source,encoding='utf-8');subprocess.run((sys.argv[1:] or ['c++'])+[str(p/'test.cpp'),'-std=c++17','-I',str(root/'pika_note/src/app'),'-o',str(p/'test.exe')],check=True);subprocess.run([str(p/'test.exe')],check=True)
