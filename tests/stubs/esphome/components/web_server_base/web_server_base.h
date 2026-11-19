#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <span>
static constexpr int HTTP_GET=0,HTTP_POST=1;
struct Response{void addHeader(const char *,const char *){}};
struct StringRef{std::string val;std::string str()const{return val;}bool operator==(const char*s)const{return val==s;}};
struct AsyncWebServerRequest{
 static constexpr size_t URL_BUF_SIZE=100;
 int method()const{return HTTP_GET;}
 StringRef url_to(std::span<char, URL_BUF_SIZE>)const{return {};}
 std::string arg(const char*)const{return {};}
 bool authenticate(const char*,const char*)const{return true;}
 void requestAuthentication()const{}
 Response* beginResponse(int,const char*,const std::string&){static Response r;return &r;}
 void send(Response*){}
 void send(int,const char*,const char*){}
};
class AsyncWebHandler{public:virtual ~AsyncWebHandler()=default;virtual bool canHandle(AsyncWebServerRequest*)const{return false;}virtual void handleRequest(AsyncWebServerRequest*){}virtual bool isRequestHandlerTrivial()const{return true;}};
namespace esphome{namespace web_server_base{struct WebServerBase{void add_handler(AsyncWebHandler*){}};extern WebServerBase*global_web_server_base;}}
