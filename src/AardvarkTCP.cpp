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

size_t          AardvarkTCP::_maxpl;
VARK_FRAGMENTS  AardvarkTCP::_fragments;
VARK_MSG_Q      AardvarkTCP::_TXQ;

AardvarkTCP::AardvarkTCP(): AsyncClient(){
    setNoDelay(true);
    onConnect([this](void* obj, AsyncClient* c) { 
    #if ASYNC_TCP_SSL_ENABLED
        #if VARK_CHECK_FINGERPRINT
            if(_URL.secure) {
                SSL* clientSsl = getSSL();
                if (ssl_match_fingerprint(clientSsl, _fingerprint) != SSL_OK) {
                    _cbError(VARK_TLS_BAD_FINGERPRINT);
                    return;
                }
            }
        #endif
    #endif
        _maxpl=(_HAL_maxHeapBlock() / 2 ) - VARK_HEAP_SAFETY;
        VARK_PRINT1("Connected Max packet size %d TCP_MSS=%d SND_BUF=%d TCP_WND=%d\n",getMaxPayloadSize(),TCP_MSS,TCP_SND_BUF,TCP_WND);
        if( _cbConnect)  _cbConnect();
    });
    onDisconnect([this](void* obj, AsyncClient* c) { VARK_PRINT1("TCP onDisconnect\n"); _onDisconnect(VARK_TCP_DISCONNECTED); });
    onError([=](void* obj, AsyncClient* c,int error) { VARK_PRINT1("TCP onError %d\n",error); _cbError(VARK_TCP_UNHANDLED,error); });
    onAck([=](void* obj, AsyncClient* c,size_t len, uint32_t time){ _ackTCP(len,time); }); 
    onData([=](void* obj, AsyncClient* c, void* data, size_t len) { _onData(static_cast<uint8_t*>(data), len); });
    onPoll([=](void* obj, AsyncClient* c) { if(_cbPoll) _cbPoll(); });
} 

void AardvarkTCP::_ackTCP(size_t len, uint32_t time){
    VARK_PRINT4("ACK! nTXQ=%d ACK LENGTH=%d _secure=%d\n",_TXQ.size(),len,_URL.secure);
    size_t amtToAck=len;
    while(amtToAck){
        if(!_TXQ.empty()){
            mbx tmp=std::move(_TXQ.front());
            _TXQ.pop();
            VARK_PRINT4("amt2ack=%d _secure=%d sub=%d leaving %d\n",amtToAck,_URL.secure,_ackSize(tmp.len),amtToAck-_ackSize(tmp.len));
            amtToAck-=_ackSize(tmp.len);
            tmp.ack();
        } else break;
    }
    _runTXQ();
}

void AardvarkTCP::_busted(size_t len) {
    _clearFragments();
    mbx::emptyPool();
    _cbError(VARK_INPUT_TOO_BIG,len);
}

void AardvarkTCP::_clearFragments() {
    for(auto &f:_fragments) f.clear();
    _fragments.clear();
    _fragments.shrink_to_fit();
}

void AardvarkTCP::_onData(uint8_t* data, size_t len) {
    static uint32_t stored=0;
    VARK_PRINT1("<---- RX %08X len=%d PSH=%d stored=%d FH=%u\n",data,len,isRecvPush(),stored,_HAL_maxHeapBlock());
    VARK_DUMP4(data,len);
    if(isRecvPush() || _URL.secure){
        if(!stored && (len < TCP_MSS)) _rxfn(data,len);
        else {
            uint8_t* bpp=mbx::getMemory(stored+len);
            if(bpp){
                uint8_t* p=bpp;
                for(auto &f:_fragments){
                    memcpy(p,f.data,f.len);
                    VARK_PRINT4("RECREATE %08X len=%d FH=%u\n",f.data,f.len,_HAL_maxHeapBlock());
                    p+=f.len;
                    f.clear();
                }
                _clearFragments();
                memcpy(p,data,len);
                VARK_PRINT4("CALL USER %08X stored=%d len=%d sum=%d FH=%u\n",bpp,stored,len,stored+len,_HAL_maxHeapBlock());
                _rxfn(bpp,stored+len);
                mbx::clear(bpp);
                stored=0;
                VARK_PRINT4("BACK FROM USER FH=%u\n",_HAL_maxHeapBlock());
            } else _busted(stored+len);
        }
    }
    else {
        _fragments.emplace_back(data,len,true);
        stored+=len;
        VARK_PRINT4("CR FRAG %08X len=%d stored=%d\n",_fragments.back().data,_fragments.back().len,stored);
    }
}

void AardvarkTCP::_onDisconnect(int8_t r) {
    VARK_PRINT1("ON DISCONNECT FH=%u r=%d\n",_HAL_maxHeapBlock(),r); 

    VARK_PRINT4("DELETE ALL %d _TXQ MBXs\n",_TXQ.size());
    while(!_TXQ.empty()){
        mbx tmp=std::move(_TXQ.front());
        _TXQ.pop();
        tmp.ack();
    }

    _clearFragments();
    VARK_PRINT4("FRAGMENTS CLEARED FH=%u\n",_HAL_maxHeapBlock());

    VARK_PRINT4("SANITY CHECK: nMBX should=0 actual value=%d FH=%u\n",mbx::pool.size(),_HAL_maxHeapBlock());
    mbx::emptyPool();
    if(_cbDisconnect) _cbDisconnect(r);
}


void  AardvarkTCP::_parseURL(const std::string& url){
    if(url.find("http",0)) _parseURL(std::string("http://")+url);
    else {
        std::vector<std::string> vs=split(url,"//");
        _URL = {};
        _URL.secure=url.find("https",0)!=std::string::npos;

        _URL.scheme=vs[0]+"//";
        VARK_PRINT4("scheme %s\n", _URL.scheme.data());

        std::vector<std::string> vs2=split(vs[1],"?");
        _URL.query=vs2.size()>1 ? vs2[1]:"";
        VARK_PRINT4("query %s\n", _URL.query.data());

        std::vector<std::string> vs3=split(vs2[0],"/");
        _URL.path=string("/")+((vs3.size()>1) ? join(std::vector<std::string>(++vs3.begin(),vs3.end()),"/")+"/":"");
        VARK_PRINT4("path %s\n", _URL.path.data());

        std::vector<std::string> vs4=split(vs3[0],":");
        _URL.port=vs4.size()>1 ? atoi(vs4[1].data()):(_URL.secure ? 443:80);
        VARK_PRINT4("port %d\n", _URL.port);

        _URL.host=vs4[0];
        VARK_PRINT4("host %s\n",_URL.host.data());
    }
}

void AardvarkTCP::_release(mbx m){
    VARK_PRINT4("_release 0x%08x len=%d TCP_MSS=%d managed=%d\n",m.data,m.len,TCP_MSS,m.managed);
    if(m.len>TCP_MSS) {
        uint16_t nFrags=m.len/TCP_MSS+((m.len%TCP_MSS) ? 1:0); // so we can mark the final fragment
        int bytesLeft=m.len;
        do{
            size_t toSend=std::min(TCP_MSS,bytesLeft);
            _HAL_feedWatchdog();
            uint8_t* F=(--nFrags) ? (uint8_t*) nFrags:m.data;
            _TXQ.emplace(m.data+(m.len - bytesLeft),F,toSend,false); // very naughty, but works :)
            VARK_PRINT4("CHUNK 0x%08x frag=0x%08x len=%d\n",m.data+(m.len - bytesLeft),F,toSend);
            bytesLeft-=toSend;
        } while(bytesLeft);
        _TXQ.pop(); // hara kiri - queue is now n smaller copies of yourself!
        _runTXQ();
    } 
    else {//if(canSend()){
        VARK_PRINT1("----> TX %d bytes\n",m.len);
        add((const char*) m.data,m.len);
        send();
    }
}

void  AardvarkTCP::_runTXQ(){ if(!_TXQ.empty()) _release(std::move(_TXQ.front())); }
//
// protected
//
void AardvarkTCP::TCPconnect() {
    if(!connected()){
        //if(_URL!={}) return _cbError(VARK_NO_SERVER_DETAILS,0);
        VARK_PRINT1("CONNECTING to %s:%d secure=%d FH=%u\n",_URL.host.data(),_URL.port,_URL.secure,_HAL_maxHeapBlock());
        #if ASYNC_TCP_SSL_ENABLED
            connect(_URL.host.data(), _URL.port, _URL.secure);
        #else
            if(_URL.secure) _cbError(VARK_TLS_NO_SSL,0);
            else connect(_URL.host.data(), _URL.port);
        #endif
    }
}

void AardvarkTCP::TCPdisconnect(bool force) {
    VARK_PRINT1("USER DCX\n");
    close(force);
    if(!connected()) _cbError(VARK_TCP_DISCONNECTED,0);
}

void AardvarkTCP::TCPurl(const char* url,const uint8_t* fingerprint){
    _parseURL(url);
    VARK_PRINT4("secure=%d\n",_URL.secure);
    if(_URL.secure){
    #if ASYNC_TCP_SSL_ENABLED
        #if VARK_CHECK_FINGERPRINT
            if(fingerprint) memcpy(_fingerprint, fingerprint, SHA1_SIZE);
            else _cbError(VARK_TLS_NO_FINGERPRINT,0);
        #endif
    #else
        _cbError(VARK_TLS_NO_SSL,0);
    #endif
    } else if(fingerprint) _cbError(VARK_TLS_UNWANTED_FINGERPRINT,0);
}

void AardvarkTCP::txdata(const uint8_t* d,size_t len,bool copy){
    VARK_PRINT4("_TXQ D/L Q=%d 0x%08x len=%d copy=%d\n",_TXQ.size(),d,len,copy);
    txdata(mbx((uint8_t*) d,len,copy));
}

void AardvarkTCP::txdata(mbx m){
    VARK_PRINT4("_TXQ MBX Q=%d 0x%08x %d\n",_TXQ.size(),m.data,m.len);
    _TXQ.push(m);
    if(_TXQ.size()==1) _release(m);
}
//
//  PUBLIC
//
#if VARK_DEBUG
void AardvarkTCP::dump(){ 
    Serial.printf("DUMP ALL %d POOL BLOX (1st 32 only)\n",mbx::pool.size());
    mbx::dump(32);

    auto n=_TXQ.size();
    Serial.printf("DUMP ALL %d _TXQ MBXs\n",n);
    for(auto i=0;i<n;i++){
        auto tmp=std::move(_TXQ.front());
        tmp._dump(tmp.len);
        _TXQ.pop();
        _TXQ.push(std::move(tmp));
    }

    Serial.printf("DUMP ALL %d FRAGMENTS\n",_fragments.size());
    for(auto & p:_fragments) Serial.printf("MBX 0x%08x len=%d\n",(void*) p.data,p.len);
    Serial.printf("\n");
}
#endif