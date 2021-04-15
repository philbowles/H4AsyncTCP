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
#include <AardvarkTCP.h>
#include <AardvarkUtils.h>

#define CSTR(x) x.c_str()

#if defined(ARDUINO_ARCH_ESP32)
void AardvarkTCP::_HAL_feedWatchdog(){}
uint32_t AardvarkTCP::_HAL_getFreeHeap(){ return ESP.getMaxAllocHeap(); }
#else
void AardvarkTCP::_HAL_feedWatchdog(){ ESP.wdtFeed(); }
uint32_t AardvarkTCP::_HAL_getFreeHeap(){ return ESP.getMaxFreeBlockSize(); }
#endif

AardvarkTCP::AardvarkTCP(): _URL(nullptr),AsyncClient(){
//    close(true);
//    if(_URL) delete _URL;

    setNoDelay(true);
    onConnect([this](void* obj, AsyncClient* c) { 
    #if ASYNC_TCP_SSL_ENABLED
        #if VARK_CHECK_FINGERPRINT
            if(_URL->secure) {
                SSL* clientSsl = getSSL();
                if (ssl_match_fingerprint(clientSsl, _fingerprint) != SSL_OK) {
                    _cbError(TLS_BAD_FINGERPRINT);
                    return;
                }
            }
        #else
            Serial.printf("WARNING! Fingerprint not checked!\n");
        #endif
    #endif
        _space=space();
        _maxpl=(_HAL_getFreeHeap() / 2 ) - VARK_HEAP_SAFETY;
        Serial.printf("Connected space=%u Max packet size %d\n",space(),getMaxPayloadSize());
        if( _cbConnect)  _cbConnect();
    });
    onDisconnect([this](void* obj, AsyncClient* c) { VARK_PRINT1("TCP onDisconnect\n"); _onDisconnect(VARK_TCP_DISCONNECTED); });
    onError([=](void* obj, AsyncClient* c,int error) { VARK_PRINT1("TCP onError %d\n",error); _cbError(VARK_TCP_UNHANDLED,error); });
    onAck([=](void* obj, AsyncClient* c,size_t len, uint32_t time){ _ackTCP(len,time); }); 
    onData([=](void* obj, AsyncClient* c, void* data, size_t len) { _onData(static_cast<uint8_t*>(data), len); });
    //onPoll([=](void* obj, AsyncClient* c) { });
} 

void AardvarkTCP::serverURL(const char* url,const uint8_t* fingerprint){
    _parseURL(url);
    Serial.printf("secure=%d\n",_URL->secure);
    if(_URL->secure){
    #if ASYNC_TCP_SSL_ENABLED
        if(fingerprint) memcpy(_fingerprint, fingerprint, SHA1_SIZE);
        else _cbError(VARK_TLS_NO_FINGERPRINT,0);
    #else
        _cbError(VARK_TLS_NO_SSL,0);
    #endif
    }
    else {
        if(fingerprint) _cbError(VARK_TLS_UNWANTED_FINGERPRINT,0);
    }
}

void AardvarkTCP::_onData(uint8_t* data, size_t len) {
    static uint32_t stored=0;
    VARK_PRINT4("<---- RX %08X len=%d PSH=%d stored=%d FH=%u\n",data,len,isRecvPush(),stored,_HAL_getFreeHeap());
    dumphex(data,len);
    if(getMaxPayloadSize() > stored+len){ // = sigma fragmets so far + current fragment 
        if(isRecvPush() || _URL->secure){
            uint8_t* bpp=static_cast<uint8_t*>(malloc(stored+len));
            uint8_t* p=bpp;
            for(auto &f:_fragments){
                memcpy(p,f.data,f.len);
                VARK_PRINT4("RECREATE %08X len=%d FH=%u\n",f.data,f.len,_HAL_getFreeHeap());
                p+=f.len;
                f.clear();
            }
            _fragments.clear();
            _fragments.shrink_to_fit();
            memcpy(p,data,len);
            VARK_PRINT4("RUN WITH %08X stored=%d len=%d sum=%d FH=%u\n",bpp,stored,len,stored+len,_HAL_getFreeHeap());
            _rxfn(bpp,stored+len);
            ::free(bpp);
            stored=0;
            VARK_PRINT4("BACK FROM USR FH=%u\n",_HAL_getFreeHeap());
        }
        else {
            _fragments.emplace_back(data,len,true);
            stored+=len;
            VARK_PRINT4("CR FRAG %08X len=%d stored=%d\n",_fragments.back().data,_fragments.back().len,stored);
        }
    }
    else {
        VARK_PRINT4("MPL=%d stored+len=%d FH=%u\n",getMaxPayloadSize(),stored+len,_HAL_getFreeHeap());
        _cbError(VARK_TOO_BIG,stored+len);
        serverDisconnect(true);
    }
}

void AardvarkTCP::_onDisconnect(int8_t r) {
    VARK_PRINT1("ON DISCONNECT FH=%u r=%d\n",_HAL_getFreeHeap(),r); 
    auto n=TXQ.size();
    Serial.printf("DELETE ALL %d TXQ MBXs\n",n);
    while(!TXQ.empty()){
        mbx tmp=std::move(TXQ.front());
        TXQ.pop();
        tmp.ack();
    }
    TXQ={};
    VARK_PRINT1("TXQ CLEARED FH=%u\n",_HAL_getFreeHeap()); 
    _fragments.clear();
    _fragments.shrink_to_fit();
    VARK_PRINT1("FRAGMENTS CLEARED FH=%u\n",_HAL_getFreeHeap()); 
    mbx::dump();
    VARK_PRINT1("SANITY CHECK: nMBX should=0 actual value=%d FH=%u\n",mbx::pool.size(),_HAL_getFreeHeap());
    for(auto const& p:mbx::pool) ::free(p);
    mbx::pool.clear();
    VARK_PRINT1("SANITY CHECK: nMBX should=0 actual value=%d FH=%u\n",mbx::pool.size(),_HAL_getFreeHeap());
    if(_cbDisconnect) _cbDisconnect(r);
}

void AardvarkTCP::serverConnect() {
    Serial.printf("serverConnect\n");
    if(!connected()){
        if(!_URL) return _cbError(VARK_NO_SERVER_DETAILS,0);
        VARK_PRINT1("CONNECTING to %s:%d secure=%d FH=%u\n",_URL->host,_URL->port,_URL->secure,_HAL_getFreeHeap());
        #if ASYNC_TCP_SSL_ENABLED
            connect(_URL->host, _URL->port, _URL->secure);
        #else
            if(_URL->secure) _cbError(VARK_TLS_NO_SSL,0);
            else connect(_URL->host, _URL->port);
        #endif
    }
}

void AardvarkTCP::serverDisconnect(bool force) {
    VARK_PRINT1("USER DCX\n");
    close(force);
    if(!connected()) _cbError(VARK_TCP_DISCONNECTED,0);
}

void AardvarkTCP::_ackTCP(size_t len, uint32_t time){
   VARK_PRINT4("ACK! nTXQ=%d ACK LENGTH=%d _secure=%d\n",TXQ.size(),len,_URL->secure);
    size_t amtToAck=len;
    while(amtToAck){
        if(!TXQ.empty()){
            mbx tmp=std::move(TXQ.front());
            tmp._dump(tmp.len);
            TXQ.pop();
            VARK_PRINT4("amt2ack=%d _secure=%d sub=%d leaving %d\n",amtToAck,_URL->secure,_ackSize(tmp.len),amtToAck-_ackSize(tmp.len));
            amtToAck-=_ackSize(tmp.len);
            tmp.ack();
        } else break;
    }
    _runTXQ();
}

void  AardvarkTCP::_parseURL(const std::string& url){
    if(url.find("http",0)) _parseURL(std::string("http://")+url);
    else {
        std::vector<std::string> vs=split(url,"//");
        _URL = new URL;
        _URL->secure=url.find("https",0)!=std::string::npos;
        _URL->scheme = new char[vs[0].size()+3];
        strcpy(_URL->scheme,(vs[0]+"//").c_str());
        Serial.printf("scheme %s\n", _URL->scheme);
        std::vector<std::string> vs2=split(vs[1],"?");

        std::string query=vs2.size()>1 ? urlencode(vs2[1]):"";
        _URL->query = new char[1+(query.size())];
        strcpy(_URL->query,CSTR(query));
        Serial.printf("query %s\n", _URL->query);

        std::vector<std::string> vs3=split(vs2[0],"/");
        std::string path("/");
        path+=vs3.size()>1 ? join(std::vector<std::string>(++vs3.begin(),vs3.end()),"/"):"";

        _URL->path = new char[1+(path.size())];
        strcpy(_URL->path,CSTR(path));
        Serial.printf("path %s\n", _URL->path);

        std::vector<std::string> vs4=split(vs3[0],":");

        _URL->port=vs4.size()>1 ? atoi(CSTR(vs4[1])):(_URL->secure ? 443:80);
        Serial.printf("port %d\n", _URL->port);

        _URL->host=new char[1+vs4[0].size()];
        strcpy(_URL->host,CSTR(vs4[0]));
        Serial.printf("host %s\n\n",_URL->host);
    }
}

void AardvarkTCP::_release(mbx m){
    VARK_PRINT4("_release 0x%08x len=%d _space=%d managed=%d\n",m.data,m.len,space(),m.managed);
    if(m.len>_space) {
        uint16_t nFrags=m.len/_space+((m.len%_space) ? 1:0); // so we can mark the final fragment
        size_t bytesLeft=m.len;
        do{
            size_t toSend=std::min(_space,bytesLeft);
            _HAL_feedWatchdog();
            ADFP F=(--nFrags) ? (ADFP) nFrags:m.data;
            TXQ.emplace(m.data+(m.len - bytesLeft),F,toSend,false); // very naughty, but works :)
            VARK_PRINT4("CHUNK 0x%08x frag=0x%08x len=%d\n",m.data+(m.len - bytesLeft),F,toSend);
            bytesLeft-=toSend;
        } while(bytesLeft);
        TXQ.pop(); // hara kiri - queue is now n smaller copies of yourself!
        _runTXQ();
    } 
    else {//if(canSend()){
        VARK_PRINT1("----> TX %d bytes (can send=%d)\n",m.len,canSend());
        add((const char*) m.data,m.len); // ESPAsyncTCP is WRONG on this, it should be a uint8_t*
        send();
    }
}

void  AardvarkTCP::_runTXQ(){ if(!TXQ.empty()) _release(std::move(TXQ.front())); }
//
//  PUBLIC
//
void AardvarkTCP::dump(){ 
    Serial.printf("DUMP ALL %d POOL BLOX (1st 32 only)\n",mbx::pool.size());
    mbx::dump(32);

    auto n=TXQ.size();
    Serial.printf("DUMP ALL %d TXQ MBXs\n",n);
    for(auto i=0;i<n;i++){
        auto tmp=std::move(TXQ.front());
        tmp._dump(tmp.len);
        TXQ.pop();
        TXQ.push(std::move(tmp));
    }

    Serial.printf("DUMP ALL %d FRAGMENTS\n",_fragments.size());
    for(auto & p:_fragments) Serial.printf("MBX 0x%08x len=%d\n",(void*) p.data,p.len);
    Serial.printf("\n");
}

void AardvarkTCP::txdata(const uint8_t* d,size_t len,bool copy){
    Serial.printf("TXQ len=%d 0x%08x %d\n",TXQ.size(),d,len);
    TXQ.emplace((uint8_t*) d,len,copy);
    dump();
    _runTXQ();
}