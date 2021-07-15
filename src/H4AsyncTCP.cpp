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

#include "IPAddress.h"

extern "C"{
  #include "lwip/opt.h"
  #include "lwip/tcp.h"
  #include "lwip/inet.h"
  #include "lwip/dns.h"
  #include "lwip/init.h"
}

PMB_HEAP_LIMITS H4AsyncTCP::safeHeapLimits;

H4_INT_MAP H4AsyncTCP::_errorNames={
#if H4AT_DEBUG
    {ERR_OK,"No error, everything OK"},
    {ERR_MEM,"Out of memory error"},
    {ERR_BUF,"Buffer error"},
    {ERR_TIMEOUT,"Timeout"},
    {ERR_RTE,"Routing problem"},
    {ERR_INPROGRESS,"Operation in progress"},
    {ERR_VAL,"Illegal value"},
    {ERR_WOULDBLOCK,"Operation would block"},
    {ERR_USE,"Address in use"},
    {ERR_ALREADY,"Already connecting"},
    {ERR_ISCONN,"Conn already established"},
    {ERR_CONN,"Not connected"},
    {ERR_IF,"Low-level netif error"},
    {ERR_ABRT,"Connection aborted"},
    {ERR_RST,"Connection reset"},
    {ERR_CLSD,"Connection closed"},
    {ERR_ARG,"Illegal argument"},
    {H4AT_ERR_OK,"OK"},
    {H4AT_ERR_ALREADY_CONNECTED,"Already Connected"},
    {H4AT_ERR_DNS_FAIL,"DNS Fail"},
    {H4AT_ERR_DNS_NF,"Remote Host not found"},
    {H4AT_ERR_UNKNOWN,"UNKNOWN"},
    {H4AT_HEAP_LIMITER_ON,"Heap Limiter ON"},
    {H4AT_HEAP_LIMITER_OFF,"Heap Limiter OFF"},
    {H4AT_HEAP_LIMITER_LOST,"Heap Limiter: packet discarded"},
    {H4AT_INPUT_TOO_BIG,"Input too big"},
#endif
};
 
void H4AsyncTCP::_ackTCP(uint16_t len){
    H4AT_PRINT2("ACK! INP %u Q=%d\n",len,_TXQ.size());
    if(_TXQ.size()){
        size_t amtToAck=_ackSize(len);
        while(amtToAck){
            _HAL_feedWatchdog(); // for massive acks
            _TXQ.front().len-=amtToAck;
            if(_TXQ.front().len > 0){
                H4AT_PRINT2("<---- AK! %u UNACKED LEN=%u\n",amtToAck,_TXQ.front().len);
                amtToAck=0;
            }
            else {
                amtToAck=(-1 * _TXQ.front().len);
                H4AT_PRINT2("<---- AK! %u OVERACKED H=%u\n",amtToAck,_HAL_freeHeap());
                _TXQ.front().ack();
                _TXQ.pop();
            }
        } 
        _releaseHeapLock();
    } else H4AT_PRINT4("TXQ empty!\n");
    H4AT_PRINT2("ACK! OUT %u Q=%d\n",len,_TXQ.size());
}

void H4AsyncTCP::_busted(size_t len) {
    _clearFragments();
    mbx::emptyPool();
    _cbError(H4AT_INPUT_TOO_BIG,len);
}

void H4AsyncTCP::_chopQ(H4AT_MSG_Q& q) {
    while(!q.empty()){
        mbx tmp=q.front();
        q.pop();
        tmp.ack();
    }
}

void H4AsyncTCP::_clearFragments() {
    for(auto &f:_fragments) f.clear();
    _fragments.clear();
    _fragments.shrink_to_fit();
}

void H4AsyncTCP::_connGuard(H4_FN_VOID f) {
    if(_pcb) f();
    else DISPATCH(this,Error,ERR_CONN,0);
}

void H4AsyncTCP::_cnxGuard(H4_FN_VOID f) {
    H4AT_PRINT4("_cnxGuard p=0x%08x\n",_pcb);
    if(_pcb){
        H4AT_PRINT4("_cnxGuard state=%d\n",_pcb->state);
        switch(_pcb->state){
            case 2:
            case 4:
                DISPATCH(this,Error,_pcb->state == 4 ? ERR_ISCONN:ERR_ALREADY,_pcb->state);
                return;
        }
    }
    else f();
}

void H4AsyncTCP::_connect() {
    if(_pcb=tcp_new()){
        H4AT_PRINT1("_connect p=0x%08x\n",_pcb);
        tcp_setprio(_pcb, TCP_PRIO_MIN);
        tcp_arg(_pcb, this);
        tcp_err(_pcb, &_tcp_error);
        size_t err = tcp_connect(_pcb, &_URL.addr, _URL.port,(tcp_connected_fn)&_tcp_connected);
        if(err) DISPATCH(this,Error,err,0);
    } else DISPATCH(this,Error,ERR_MEM,_HAL_freeHeap());
}

void H4AsyncTCP::_onData(struct pbuf *pb) {
    uint8_t* data=reinterpret_cast<uint8_t*>(pb->payload);
    size_t len=pb->len;
    H4AT_PRINT3("<---- RX %08X len=%d _stored=%d FH=%u flags=0x%02x\n",data,len,_stored,_HAL_maxHeapBlock(),pb->flags);
    if(pb->flags & PBUF_FLAG_PUSH){
        if(!_stored) _rxfn(data,len);
        else {
            uint8_t* bpp=mbx::getMemory(_stored+len);
            if(bpp){
                uint8_t* p=bpp;
                for(auto &f:_fragments){
                    memcpy(p,f.data,f.len);
                    H4AT_PRINT3("RECREATE %08X len=%d FH=%u\n",f.data,f.len,_HAL_maxHeapBlock());
                    p+=f.len;
                    f.clear();
                }
                _clearFragments();
                memcpy(p,data,len);
                H4AT_PRINT3("CALL USER %08X _stored=%d len=%d sum=%d FH=%u\n",bpp,_stored,len,_stored+len,_HAL_maxHeapBlock());
                _rxfn(bpp,_stored+len);
                mbx::clear(bpp);
                _stored=0;
                H4AT_PRINT3("BACK FROM USER FH=%u\n",_HAL_maxHeapBlock());
            } else _busted(_stored+len);
        }
    }
    else {
        _fragments.emplace_back(data,len,true);
        _stored+=len;
        H4AT_PRINT3("CR FRAG %08X len=%d _stored=%d\n",_fragments.back().data,_fragments.back().len,_stored);
    }
}

void  H4AsyncTCP::_parseURL(const std::string& url){
    if(url.find("http",0)) _parseURL(std::string("http://")+url);
    else {
        std::vector<std::string> vs=split(url,"//");
        _URL = {};
        _URL.secure=url.find("https",0)==std::string::npos ? false:true;
        H4AT_PRINT4("SECURE = %d  %s\n",_URL.secure,_URL.secure ? "TRUE":"FALSE");
        _URL.scheme=vs[0]+"//";
        H4AT_PRINT4("scheme %s\n", _URL.scheme.data());

        std::vector<std::string> vs2=split(vs[1],"?");
        _URL.query=vs2.size()>1 ? vs2[1]:"";
        H4AT_PRINT4("query %s\n", _URL.query.data());

        std::vector<std::string> vs3=split(vs2[0],"/");
        _URL.path=std::string("/")+((vs3.size()>1) ? join(std::vector<std::string>(++vs3.begin(),vs3.end()),"/")+"/":"");
        H4AT_PRINT4("path %s\n", _URL.path.data());

        std::vector<std::string> vs4=split(vs3[0],":");
        _URL.port=vs4.size()>1 ? atoi(vs4[1].data()):(_URL.secure ? 443:80);
        H4AT_PRINT4("port %d\n", _URL.port);

        _URL.host=vs4[0];
        H4AT_PRINT4("host %s\n",_URL.host.data());
    }
}

void H4AsyncTCP::_releaseHeapLock(){
    auto h=_HAL_freeHeap();
    if(_heapLock && h > safeHeapLimits.second){
        _heapLock=false;
        _causeError(H4AT_HEAP_LIMITER_OFF,_TXQ.size());
    }
    _runPXQ();
}

void H4AsyncTCP::_runPXQ(){
    if(_PXQ.size() && (!_pxqRunning)){
        _pxqRunning=h4.repeatWhile(
            [&]{ return _PXQ.size(); },1, // this is baffling! 
            [&]{ _TX(); },
            [&]{
                H4AT_PRINT4("PXQ ENDS\n");
                _pxqRunning=nullptr;
            }
        );
    }
}

void H4AsyncTCP::_tcp_error(void * arg, err_t err) { DISPATCH(arg,Error,err,0); }

err_t H4AsyncTCP::_tcp_connected(void* arg, void* tpcb, err_t err){
#if H4AT_DEBUG
    auto p=reinterpret_cast<H4AsyncTCP*>(arg);
    H4AT_PRINT4("_tcp_connected  a=0x%08x p=0x%08x e=%d IP=%s:%d\n",arg,tpcb,err,p->getRemoteAddressString().data(),p->getRemotePort());
#endif
    tcp_poll(reinterpret_cast<tcp_pcb*>(tpcb), &_tcp_poll,1); // units of 500mS
    tcp_sent(reinterpret_cast<tcp_pcb*>(tpcb), &_tcp_sent); // units of 500mS
    tcp_recv(reinterpret_cast<tcp_pcb*>(tpcb), &_tcp_recv); // units of 500mS
    DISPATCH_V(arg,Connect);
    return ERR_OK;
}

void H4AsyncTCP::_tcp_dns_found(const char * name, struct ip_addr * ipaddr, void * arg) {
    H4AT_PRINT4("_tcp_dns_found %s i=0x%08x p=0x%08x\n",name,ipaddr,arg);
    if(ipaddr){
        auto p=reinterpret_cast<H4AsyncTCP*>(arg);
        ip_addr_copy(p->_URL.addr, *ipaddr);
        h4.queueFunction([=]{ p->_connect(); });
    } else DISPATCH(arg,Error,H4AT_ERR_DNS_NF,0);
}

err_t H4AsyncTCP::_tcp_poll(void *arg, struct tcp_pcb *tpcb){
    DISPATCH_V(arg,Poll);
    return ERR_OK;
}

err_t H4AsyncTCP::_tcp_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *pb, err_t err){
    H4AT_PRINT4("_tcp_recv a=0x%08x p=0x%08x b=0x%08x err=%d\n",arg,tpcb,pb,err);
    if(pb){
        auto p=reinterpret_cast<H4AsyncTCP*>(arg);
        H4AT_PRINT4("PB 0x%08x  nxt=0x%08x  load=0x%08x tot=%d len=%d flags=0x%02x ref=%d\n",pb,pb->next,pb->payload,pb->tot_len,pb->len,pb->flags,pb->ref);
        h4.queueFunction([=]{ 
            p->_onData(pb);
            tcp_recved(tpcb, pb->len);
            pbuf_free(pb);
        });
    } //else Serial.printf("REMIND ME WHAT TF THIS MEANS????? (remote has closed)\n");
    return ERR_OK;
}

err_t H4AsyncTCP::_tcp_sent(void *arg, struct tcp_pcb *tpcb, uint16_t len){
//    H4AT_PRINT4("_tcp_sent a=0x%08x p=0x%08x len=%d\n",arg,tpcb,len);
    auto p=reinterpret_cast<H4AsyncTCP*>(arg);
    h4.queueFunction([=]{ p->_ackTCP(len); });
    return ERR_OK;
}

void H4AsyncTCP::_TX(){
    H4AT_PRINT2("TX 0x%08x len=%d TCP_SND_BUF=%d managed=%d\n",_PXQ.front().data,_PXQ.front().len,tcp_sndbuf(_pcb),_PXQ.front().managed);
    if(_PXQ.front().len>TCP_SND_BUF) {
        mbx m=_PXQ.front();
        _PXQ.pop();
        uint16_t nFrags=m.len/TCP_SND_BUF+((m.len%TCP_SND_BUF) ? 1:0); // so we can mark the final fragment
        int bytesLeft=m.len;
        do{
            size_t toSend=std::min(TCP_SND_BUF,bytesLeft);
            uint8_t* F=(--nFrags) ? (uint8_t*) nFrags:m.data;
            _PXQ.emplace(m.data+(m.len - bytesLeft),F,toSend,false);
            H4AT_PRINT2("CHUNK 0x%08x frag=0x%08x len=%d\n",m.data+(m.len - bytesLeft),F,toSend);
            bytesLeft-=toSend;
        } while(bytesLeft); /// set PSH flag on last fragment
    }
    else {
        if(_PXQ.front().len > tcp_sndbuf(_pcb)) while(!tcp_sndbuf(_pcb)) { yield(); }
        else {
            mbx m=_PXQ.front();
            H4AT_PRINT2("----> TX data=0x%08x len=%d TXQ=%d PXQ=%d H=%u\n",m.data,m.len,_TXQ.size(),_PXQ.size(),_HAL_freeHeap());
            uint8_t flags;
            if(m.managed) flags=TCP_WRITE_FLAG_COPY;
            if((uint32_t) m.frag > 5000 ) flags |= TCP_WRITE_FLAG_MORE; // sex this up: e.g. calculate maxium possible N packtes
            auto err=tcp_write(_pcb,m.data,m.len,flags);// arbitrary, sex it up - maxpaxket / sndbubuf
            if(!err){
                err=tcp_output(_pcb);
                if(!err){
                    _PXQ.pop();
                    _TXQ.push(m);
                } else DISPATCH(this,Error,22,22);
            } else DISPATCH(this,Error,33,33);
        }
    }
}
//
//
//
void H4AsyncTCP::close(bool abort){
    H4AT_PRINT4("close abort=%d\n",abort);
    _connGuard([=]{
//        h4.queueFunction([=]{
            err_t err;
            if(abort) tcp_abort(_pcb);
            else err = tcp_close(_pcb);
            _pcb=nullptr;
            if(err) DISPATCH(this,Error,err,abort);
            else {
                H4AT_PRINT4("DELETE ALL %d _PXQ MBXs\n",_PXQ.size());
                h4.cancel(_pxqRunning);
                _chopQ(_PXQ);

                H4AT_PRINT1("LET TXQ DRAIN %d\n",_TXQ.size());
                h4.repeatWhile(
                    [&]{ return _TXQ.size(); },10, // this is baffling! 
                    []{}, // feed watchdog?
                    [=]{
                        H4AT_PRINT4("TXQ DRAINED KILL CLIENT\n");
                        _clearFragments();
                        H4AT_PRINT4("FRAGMENTS CLEARED FH=%u\n",_HAL_maxHeapBlock());
                        mbx::emptyPool();
                        H4AT_PRINT4("close final 2 dispatch user od abort=%d\n",abort);
                        DISPATCH(this,Disconnect,abort);
                        H4AT_PRINT4("close final 3 hara kiri=%d\n",abort);
                        delete this; // HARA KIRI!
                    }
                );
            }
//        });
    });
}

void H4AsyncTCP::connect(const char* host,uint16_t port){
    _cnxGuard([=]{
        IPAddress ip;

        if(ip.fromString(host)) connect(ip,port);
        else {
            _URL.port=port;
            err_t err = dns_gethostbyname(host, &_URL.addr, (dns_found_callback)&_tcp_dns_found, this);
            if(err == ERR_OK) {
                H4AT_PRINT1("dns_gethostbyname err=%u\n",err);
                DISPATCH(this,Error,H4AT_ERR_DNS_FAIL,0);
                return;
            }
        }
    });
}

void H4AsyncTCP::connect(const char* url){
    _parseURL(url);
    connect(_URL.host.data(),_URL.port);
}

void H4AsyncTCP::connect(IPAddress ip,uint16_t port){
    _cnxGuard([=]{
        _URL.port=port;
        ip_addr_set_ip4_u32(&_URL.addr, ip);
        _connect();
    });
}

void H4AsyncTCP::connect(){ connect(_URL.host.data(),_URL.port); }

bool H4AsyncTCP::connected(){ return _pcb && _pcb->state== 4; }

std::string H4AsyncTCP::errorstring(int8_t e){
    #ifdef H4AT_DEBUG
        if(_errorNames.count(e)) return _errorNames[e];
        else return stringFromInt(e); 
    #else
        return stringFromInt(e); 
    #endif
}

std::string H4AsyncTCP::getRemoteAddressString(){
    IPAddress ip(getRemoteAddress());
    return std::string(ip.toString().c_str());
}

void H4AsyncTCP::safeHeap(size_t cutout,size_t cutin){
    H4AT_PRINT1("safeHeap: cutout=%u cutin=%u hysteresis=%d\n",cutout,cutin,cutin - cutout);
    if(cutout < cutin){
        safeHeapLimits.first=cutout;
        safeHeapLimits.second=cutin;
        H4AT_PRINT1("HEAP SAFETY: cutout=%u cutin=%u hysteresis=%u\n",cutout,cutin,cutin - cutout);
    } else H4AT_PRINT1("H4AT_HEAP_LIMITER_ERROR %d\n",cutout - cutin);
}

void H4AsyncTCP::setNoDelay(bool tf){
    _connGuard([=]{
        if(tf) tcp_nagle_disable(_pcb);
        else tcp_nagle_enable(_pcb);
    });
}

void H4AsyncTCP::txdata(const uint8_t* d,size_t len,bool copy){ _connGuard([=]{ txdata(mbx((uint8_t*) d,len,copy)); }); }

void H4AsyncTCP::txdata(mbx m){
    _HAL_feedWatchdog(); // fpr BIG Q's
    if(_heapLock){ // careful....!
        H4AT_PRINT1("HEAPLOCKED DISCARD %u BYTES\n",m.len);
        _causeError(H4AT_HEAP_LIMITER_LOST,m.len);
        m.ack();
        return;
    }
    else {
        auto h=_HAL_freeHeap();
        if(h > safeHeapLimits.first) {
            _PXQ.push(m);
            _runPXQ();
    }
        else {
            H4AT_PRINT1("HEAPLOCK FH=%u\n",h);
            _heapLock=true;
            _causeError(H4AT_HEAP_LIMITER_ON,h);
            txdata(m); // recurse to get rid of failed m
        }
    }
}

void H4AsyncTCP::TCPurl(const char* url,const uint8_t* fingerprint){
    _parseURL(url);
    H4AT_PRINT4("secure=%d\n",_URL.secure);
    /*
    if(_URL.secure){
    #if ASYNC_TCP_SSL_ENABLED
        #if H4AT_CHECK_FINGERPRINT
            if(fingerprint) memcpy(_fingerprint, fingerprint, SHA1_SIZE);
            else _cbError(H4AT_TLS_NO_FINGERPRINT,0);
        #endif
    #else
        _cbError(H4AT_TLS_NO_SSL,0);
    #endif
    } //else if(fingerprint) _cbError(H4AT_TLS_UNWANTED_FINGERPRINT,0);
    */
}
//
#if H4AT_DEBUG
void H4AsyncTCP::dumpQ(H4AT_MSG_Q& q) {
    H4AT_MSG_Q cq=q;
    H4AT_PRINT2("dumpQ: size=%d\n",q.size());
    while(!cq.empty()){
        mbx tmp=cq.front();
        Serial.printf("DUMPQ data=0x%08x len=%d\n",tmp.data,tmp.len);
        cq.pop();
    }
}

void H4AsyncTCP::dump(){ 
    Serial.printf("DUMP ALL %d POOL BLOX (1st 32 only)\n",mbx::pool.size());
    mbx::dump(32);

    auto nt=_TXQ.size();
    Serial.printf("DUMP ALL %d _TXQ MBXs\n",nt);
    dumpQ(_TXQ);
    auto np=_PXQ.size();
    Serial.printf("DUMP ALL %d _PXQ MBXs\n",np);
    dumpQ(_PXQ);

    Serial.printf("DUMP ALL %d FRAGMENTS\n",_fragments.size());
    for(auto & p:_fragments) Serial.printf("MBX 0x%08x len=%d\n",(void*) p.data,p.len);
    Serial.printf("\n");
}
#else
void H4AsyncTCP::dumpQ(H4AT_MSG_Q& q) {}
void H4AsyncTCP::dump(){}
#endif