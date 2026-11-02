#pragma once
#include <WebServer.h>
#include <ArduinoJson.h>
#include <map>
class PhoneWebServer:public WebServer {
 bool bridge=false;String path,plain,output;HTTPMethod verb=HTTP_GET;std::map<String,String> args;
 std::map<String,THandlerFunction> routes;
 public:int bridgeStatus=200;
 PhoneWebServer(int port):WebServer(port){}
 using WebServer::on;using WebServer::send;using WebServer::send_P;using WebServer::arg;
 RequestHandler& on(const char* uri,HTTPMethod method,THandlerFunction fn){routes[String(int(method))+uri]=fn;return WebServer::on(uri,method,fn);}
 String arg(const String& name)const{if(!bridge)return WebServer::arg(name);if(name=="plain")return plain;auto found=args.find(name);return found==args.end()?String():found->second;}
 bool hasArg(const String& name)const{return bridge?(name=="plain"||args.count(name)):WebServer::hasArg(name);}
 HTTPMethod method()const{return bridge?verb:WebServer::method();}String uri()const{return bridge?path:WebServer::uri();}
 void send(int code,const char* type=nullptr,const String& body=String()){if(!bridge){WebServer::send(code,type,body);return;}bridgeStatus=code;output=body;}
 void send(int code,const char* type,const char* body){send(code,type,String(body));}
 void sendHeader(const String& name,const String& value,bool first=false){if(!bridge)WebServer::sendHeader(name,value,first);}
 void setContentLength(size_t size){if(!bridge)WebServer::setContentLength(size);}
 void sendContent(const String& body){if(bridge)output+=body;else WebServer::sendContent(body);}
 void sendContent(const char* body,size_t n){if(bridge)output.concat(body,n);else WebServer::sendContent(body,n);}
 bool dispatch(const JsonDocument& request,String& result){
  bridge=true;bridgeStatus=200;output="";args.clear();path=request["path"]|"";plain=request["body"]|"";verb=String(request["method"]|"GET")=="POST"?HTTP_POST:HTTP_GET;
  for(auto item:request["args"].as<JsonObjectConst>())args[item.key().c_str()]=item.value().as<String>();
  auto handler=routes.find(String(int(verb))+path);if(handler==routes.end()){bridgeStatus=404;output="{\"error\":\"Unsupported phone operation\"}";}else handler->second();
  result=std::move(output);bridge=false;plain="";return bridgeStatus<400;
 }
};
