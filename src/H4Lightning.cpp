/*
MIT License

Copyright (c) 2020 Phil Bowles with huge thanks to Adam Sharp http://threeorbs.co.uk
for testing, debugging, moral support and permanent good humour.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/
#include<H4AsyncTCP.h>
extern "C"{
  #include "lwip/tcp.h"
}

std::vector<H4L_handler*> H4Lightning::handlers;

H4AS_NVP_MAP H4Lightning::_ext2ct={
  {"html","text/html"},
  {"htm","text/html"},
  {"css","text/css"},
//  {"json","application/json"},
  {"js","application/javascript"},
  {"png","image/png"},
//  {"gif","image/gif"},
  {"jpg","image/jpeg"},
  {"ico","image/x-icon"},
//  {"svg","image/svg+xml"},
//  {"eot","font/eot"},
//  {"woff","font/woff"},
//  {"woff2","font/woff2"},
//  {"ttf","font/ttf"},
  {"xml","text/xml"},
//  {"pdf","application/pdf"},
//  {"zip","application/zip"},
//  {"gz","application/x-gzip"}
};

const char* _responseCodeToString(int code) {
    switch (code) {
        case 100: return "Continue";
        case 101: return "Switching Protocols";
        case 200: return "OK";
//        case 201: return "Created";
//        case 202: return "Accepted";
//        case 203: return "Non-Authoritative Information";
//        case 204: return "No Content";
//        case 205: return "Reset Content";
//        case 206: return "Partial Content";
//        case 300: return "Multiple Choices";
//        case 301: return "Moved Permanently";
//        case 302: return "Found";
//        case 303: return "See Other";
        case 304: return "Not Modified";
//        case 305: return "Use Proxy";
//        case 307: return "Temporary Redirect";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
//        case 402: return "Payment Required";
        case 403: return "Forbidden";
        case 404: return "Not Found";
//        case 405: return "Method Not Allowed";
//        case 406: return "Not Acceptable";
//        case 407: return "Proxy Authentication Required";
        case 408: return "Request Time-out";
//        case 409: return "Conflict";
//        case 410: return "Gone";
//        case 411: return "Length Required";
//        case 412: return "Precondition Failed";
//        case 413: return "Request Entity Too Large";
//        case 414: return "Request-URI Too Large";
        case 415: return "Unsupported Media Type";
//        case 416: return "Requested range not satisfiable";
//        case 417: return "Expectation Failed";
        case 500: return "Internal Server Error";
//        case 501: return "Not Implemented";
//        case 502: return "Bad Gateway";
        case 503: return "Service Unavailable";
//        case 504: return "Gateway Time-out";
//        case 505: return "HTTP Version not supported";
        default:  return "??? ";
    }
}
//
//
//
H4Lightning::H4Lightning(uint16_t _port): H4AsyncServer(_port){
    HAL_FS.begin();
    _client_cb=[=](tcp_pcb* tpcb){ new H4L_request(tpcb); };
}

H4L_request::H4L_request(tcp_pcb* p): H4AsyncClient(p){
    H4AT_PRINT3("H4L_request CTOR 0x%08x p=%08xx\n",this,p);
    onConnect([=]{
        IPAddress ip(ip_addr_get_ip4_u32(&_pcb->local_ip));
        H4AT_PRINT2("0x%08x CLIENT incoming =0x%08x from %s:%d\n",this,(void*)this,ip.toString().c_str(),_pcb->local_port);
    });

    onDisconnect([=](int r){ Serial.printf("0x%08x ON DCX %d\n",this,r); });

    onPoll([&]{
        if(_TXQ.size()==0 && _PXQ.size()==0) {
//            if(_closeConnection){
                H4AT_PRINT3("0x%08x HAD ENOUGH OF THIS LIFE \n",this);
                close();
                tcp_abort(_pcb);
                return ERR_ABRT;
//            } else H4AT_PRINT3("0x%08x Ah! ah! ah! ah! stayin' alive\n",this);
        }
    });

    onError([=](int e,int i){ 
        switch(e){
            case H4AT_HEAP_LIMITER_ON:
                Serial.printf("Heap lock ON!\n");
                break;
            case H4AT_HEAP_LIMITER_OFF:
                Serial.printf("Heap lock OFF!\n");
                break;
            case H4AT_HEAP_LIMITER_LOST:
                Serial.printf("DATA LOSS %d BYTES: STOP SENDING!!!\n",i);
                break;
            default:
                Serial.printf("CEST MOI? ERROR %d info=%d\n",e,i);
        }
    });

    rx([=](const uint8_t* data, size_t len){
        std::vector<std::string> rqst=split(std::string((const char*) data,len),"\r\n");
        std::vector<std::string> vparts=split(rqst[0]," ");
        for(auto r:rqst) {
            if(r.find("Connection:")!=std::string::npos){
                _closeConnection=r.find("keep-alive")==std::string::npos;
//                Serial.printf("0x%08x CNXION %s therefore _close=%d\n",this,r.data(),_closeConnection);
            }
        }
        H4AT_PRINT1("%08x **************************** REQUEST %s %s checking %d handlers\n",this,vparts[0].data(),vparts[1].data(),H4Lightning::handlers.size());
        auto n=0;
        for(auto h:H4Lightning::handlers) if(h->handled(this,vparts[0],vparts[1])) break;
    });
    // set to sideload
    tcp_setprio(_pcb, TCP_PRIO_MIN);
    tcp_arg(_pcb, this);
    
    H4AsyncClient::_tcp_connected(this,_pcb,ERR_OK);
}

void H4Lightning::begin(){
    handlers.push_back(new H4L_handlerFile());
    handlers.push_back(new H4L_handler404());
    H4AsyncServer::begin();
}

void H4L_request::send(uint16_t code,std::string type,size_t length,const void* body){
    H4AT_PRINT3("send(%d,%s,%d,0x%08x)\n",code,type.data(),length,body);
    std::string status=std::string("HTTP/1.1 ")+stringFromInt(code,"%3d ").append(_responseCodeToString(code))+"\r\n";
    status+=std::string("Content-Type: ")+type+"\r\n";
    status+=std::string("Content-Length: ")+stringFromInt(length)+"\r\n\r\n";
    auto h=status.size();
    auto total=h+length;
    uint8_t* buff=(uint8_t*) malloc(total);
    memcpy(buff,status.data(),h);
    if(length) memcpy(buff+h,body,length);
    txdata(buff,total);
    ::free(buff);
}

void H4L_request::sendFile(std::string fn){ H4L_handlerFile::serveFile(this,fn); }
//
//
//
bool H4L_handlerFile::handled(H4L_request* r,std::string verb,std::string path) {
    if(verb!=_verb) return false;
    _path=path;
    return _handle(r);
}

bool H4L_handlerFile::serveFile(H4L_request* r, std::string fn){
    bool rv=false;
    readFileRaw(fn,[&](const char* e,const char* data,size_t n){
        if(n){
            std::string ct=H4Lightning::_ext2ct.count(e) ? H4Lightning::_ext2ct[e]:"text/plain";
            r->send(200,H4Lightning::_ext2ct[e],n,(const uint8_t*) data);
            rv=true;
        }
    });
    return rv;
}

bool H4L_handlerFile::_handle(H4L_request* r){ return serveFile(r,_path); }

bool H4L_handler404::_handle(H4L_request* r) { r->send(404,"text/plain",5,"oops!"); return true; }