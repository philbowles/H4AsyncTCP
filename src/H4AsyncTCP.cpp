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

PMB_HEAP_LIMITS H4AsyncClient::safeHeapLimits;

H4_INT_MAP H4AsyncClient::_errorNames={
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
H4AsyncClient::H4AsyncClient(tcp_pcb* p){
    _pcb=p;
    H4AT_PRINT3("H4AsyncClient CTOR 0x%08x 0x%08x\n",this,_pcb);
    safeHeapLimits=heapLimits();
};

H4AsyncClient::~H4AsyncClient(){
    H4AT_PRINT3("H4AsyncClient DTOR 0x%08x\n",this);
}

void H4AsyncClient::_ackTCP(uint16_t len){
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
}

void H4AsyncClient::_busted(size_t len) {
    _clearFragments();
    mbx::emptyPool();
    _cbError(H4AT_INPUT_TOO_BIG,len);
}

void H4AsyncClient::_chopQ(H4AT_MSG_Q& q) {
    while(!q.empty()){
        mbx tmp=q.front();
        q.pop();
        tmp.ack();
    }
}

void H4AsyncClient::_clearFragments() {
    for(auto &f:_fragments) f.clear();
    _fragments.clear();
    _fragments.shrink_to_fit();
}

void H4AsyncClient::_connGuard(H4_FN_VOID f) {
    if(_pcb) f();
    else DISPATCH(this,Error,ERR_CONN,0);
}

void H4AsyncClient::_cnxGuard(H4_FN_VOID f) {
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

void H4AsyncClient::_connect() {
    if(!_pcb) _pcb=tcp_new();
    H4AT_PRINT2("_connect p=0x%08x\n",_pcb);
    tcp_setprio(_pcb, TCP_PRIO_MIN);
    tcp_arg(_pcb, this);
    tcp_err(_pcb, &_tcp_error);
    size_t err = tcp_connect(_pcb, &_URL.addr, _URL.port,(tcp_connected_fn)&_tcp_connected);
    if(err) DISPATCH(this,Error,err,0);
}

void H4AsyncClient::_onData(struct tcp_pcb *tpcb,struct pbuf *pb) {
    if(!pb){
        Serial.printf("DODGY PBUF!==========================================\n");
        return;
    }
    uint8_t* data=reinterpret_cast<uint8_t*>(pb->payload);
    size_t len=pb->len;
    H4AT_PRINT3("<---- RX[0x%08X] pb=0x%08x d=0x%08X len=%d _stored=%d FH=%u flags=0x%02x\n",this,tpcb,data,len,_stored,_HAL_maxHeapBlock(),pb->flags);
//    H4AT_DUMP4(data,len);
    if(pb->flags & PBUF_FLAG_PUSH){
        if(!_stored) {
            if(_rxfn) _rxfn(data,len);
            else Serial.printf("WAAAAAAAAAAAAAt VVVVVVVVVVVVVVVVVVVVV FUX?\n");
        }
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
    tcp_recved(tpcb, pb->len);
    pbuf_free(pb); // watch this!!
}

void  H4AsyncClient::_parseURL(const std::string& url){
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

void H4AsyncClient::_releaseHeapLock(){
    auto h=_HAL_freeHeap();
    if(_heapLock && h > safeHeapLimits.second){
        _heapLock=false;
        DISPATCH(this,Error,H4AT_HEAP_LIMITER_OFF,_TXQ.size());
    }
    _runPXQ(); // really?
}

void H4AsyncClient::_runPXQ(){
    if(_PXQ.size() && (!_pxqRunning)){
        _pxqRunning=h4.repeatWhile(
            [&]{ return _PXQ.size(); },1, // this is baffling! 
            [=]{ _TX(); },
            [&]{
                H4AT_PRINT4("PXQ DRAINED\n");
                _pxqRunning=nullptr;
            }
        );
    }
}

void H4AsyncClient::_tcp_error(void * arg, err_t err) { DISPATCH(arg,Error,err,0); }

err_t H4AsyncClient::_tcp_connected(void* arg, void* tpcb, err_t err){
#if H4AT_DEBUG
    auto p=reinterpret_cast<tcp_pcb*>(tpcb);
    IPAddress ip(ip_addr_get_ip4_u32(&p->local_ip));
    H4AT_PRINT3("_tcp_connected  a=0x%08x p=0x%08x e=%d IP=%s:%d\n",arg,tpcb,err,ip.toString().c_str(),p->local_port);
#endif
    tcp_poll(p, &_tcp_poll,1); // units of 500mS
    tcp_sent(p, &_tcp_sent);
    tcp_recv(p, &_tcp_recv); 
    DISPATCH_V(arg,Connect);
    return ERR_OK;
}

void H4AsyncClient::_tcp_dns_found(const char * name, struct ip_addr * ipaddr, void * arg) {
    H4AT_PRINT3("_tcp_dns_found %s i=0x%08x p=0x%08x\n",name,ipaddr,arg);
    if(ipaddr){
        auto p=reinterpret_cast<H4AsyncClient*>(arg);
        ip_addr_copy(p->_URL.addr, *ipaddr);
        h4.queueFunction([=]{ p->_connect(); });
    } else DISPATCH(arg,Error,H4AT_ERR_DNS_NF,0);
}

err_t H4AsyncClient::_tcp_poll(void *arg, struct tcp_pcb *tpcb){
    auto p=reinterpret_cast<H4AsyncClient*>(arg);
    if(p->_cbPoll) return p->_cbPoll();
    else return ERR_OK;
}

err_t H4AsyncClient::_tcp_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *pb, err_t err){
    auto p=reinterpret_cast<H4AsyncClient*>(arg);
    if(pb){
        H4AT_PRINT3("_tcp_recv a=0x%08x p=0x%08x b=0x%08x err=%d\n",p,tpcb,pb,err);
        h4.queueFunction([=]{ p->_onData(tpcb,pb); }); // change to dispatch?
        return ERR_OK;
    } 
    else {
        H4AT_PRINT3("remote has closed connexion\n");
        p->_closeConnection=true;
        return _tcp_poll(arg,tpcb);
    }
}

err_t H4AsyncClient::_tcp_sent(void *arg, struct tcp_pcb *tpcb, uint16_t len){
    H4AT_PRINT2("_tcp_sent a=0x%08x p=0x%08x len=%d\n",arg,tpcb,len);
    auto p=reinterpret_cast<H4AsyncClient*>(arg);
    h4.queueFunction([=]{ p->_ackTCP(len); }); // change to onACK + dispatch
    return ERR_OK;
}

void H4AsyncClient::_TX(){
    H4AT_PRINT3("TX 0x%08x len=%d TCP_SND_BUF=%d managed=%d\n",_PXQ.front().data,_PXQ.front().len,tcp_sndbuf(_pcb),_PXQ.front().managed);
    if(_PXQ.front().len>TCP_SND_BUF) {
        mbx m=_PXQ.front();
        _PXQ.pop();
        uint16_t nFrags=m.len/TCP_SND_BUF+((m.len%TCP_SND_BUF) ? 1:0); // so we can mark the final fragment
        int bytesLeft=m.len;
        do{
            size_t toSend=std::min(TCP_SND_BUF,bytesLeft);
            uint8_t* F=(--nFrags) ? (uint8_t*) nFrags:m.data;
            _PXQ.emplace(m.data+(m.len - bytesLeft),F,toSend,false);
            H4AT_PRINT3("CHUNK 0x%08x frag=0x%08x len=%d\n",m.data+(m.len - bytesLeft),F,toSend);
            bytesLeft-=toSend;
        } while(bytesLeft); /// set PSH flag on last fragment
    }
    else {
        if(_PXQ.front().len > tcp_sndbuf(_pcb)) while(!tcp_sndbuf(_pcb)) { yield(); }
        else {
            mbx m=_PXQ.front();
            H4AT_PRINT3("----> TX data=0x%08x len=%d TXQ=%d PXQ=%d H=%u\n",m.data,m.len,_TXQ.size(),_PXQ.size(),_HAL_freeHeap());
            uint8_t flags;
            if(m.managed) flags=TCP_WRITE_FLAG_COPY;
            if((uint32_t) m.frag > 5000 ) flags |= TCP_WRITE_FLAG_MORE; // sex this up: e.g. calculate maxium possible N packtes
            auto err=tcp_write(_pcb,m.data,m.len,flags);// arbitrary, sex it up - maxpaxket / sndbubuf
            if(!err){
                err=tcp_output(_pcb);
                if(!err){
                    _PXQ.pop();
                    _TXQ.push(m);
                } else DISPATCH(this,Error,err,22);
            } else DISPATCH(this,Error,err,33);
        }
    }
}
//
//
//
void H4AsyncClient::close(){
    H4AT_PRINT4("close\n");
    _connGuard([=]{
        H4AT_PRINT3("DELETE ALL %d _PXQ MBXs\n",_PXQ.size());
        tcp_arg(_pcb, NULL);
        h4.cancel(_pxqRunning);
        _chopQ(_PXQ);
        H4AT_PRINT3("LET TXQ DRAIN %d\n",_TXQ.size());
        h4.repeatWhile(
            [&]{ return _TXQ.size(); },10, // this is baffling! 
            []{}, // feed watchdog?
            [=]{
                H4AT_PRINT3("TXQ DRAINED KILL CLIENT\n");
                _clearFragments();
                mbx::emptyPool();
                h4.queueFunction(
                    [=]{ if(_cbDisconnect) _cbDisconnect(0); },
                    [=]{ delete this; }
                );
            }
        );
    });
}

void H4AsyncClient::connect(const char* host,uint16_t port){
    _cnxGuard([=]{
        IPAddress ip;

        if(ip.fromString(host)) connect(ip,port);
        else {
            _URL.port=port;
            err_t err = dns_gethostbyname(host, &_URL.addr, (dns_found_callback)&_tcp_dns_found, this);
            if(err == ERR_OK) {
                DISPATCH(this,Error,H4AT_ERR_DNS_FAIL,0);
                return;
            }
        }
    });
}

void H4AsyncClient::connect(const char* url){
    _parseURL(url);
    connect(_URL.host.data(),_URL.port);
}

void H4AsyncClient::connect(IPAddress ip,uint16_t port){
    _cnxGuard([=]{
        _URL.port=port;
        ip_addr_set_ip4_u32(&_URL.addr, ip);
        _connect();
    });
}

void H4AsyncClient::connect(){ connect(_URL.host.data(),_URL.port); }

bool H4AsyncClient::connected(){ return _pcb && _pcb->state== 4; }

std::string H4AsyncClient::errorstring(int e){
    #ifdef H4AT_DEBUG
        if(_errorNames.count(e)) return _errorNames[e];
        else return stringFromInt(e); 
    #else
        return stringFromInt(e); 
    #endif
}

bool H4AsyncClient::free(){
  if(!_pcb)
    return true;
  if(_pcb->state == 0 || _pcb->state > 4)
    return true;
  return false;
}

std::string H4AsyncClient::remoteIPString(){
    return std::string(remoteIP().toString().c_str());
}

void H4AsyncClient::safeHeap(size_t cutout,size_t cutin){
    H4AT_PRINT1("safeHeap: cutout=%u cutin=%u hysteresis=%d\n",cutout,cutin,cutin - cutout);
    if(cutout < cutin){
        safeHeapLimits.first=cutout;
        safeHeapLimits.second=cutin;
        H4AT_PRINT1("HEAP SAFETY: cutout=%u cutin=%u hysteresis=%u\n",cutout,cutin,cutin - cutout);
    } else H4AT_PRINT1("H4AT_HEAP_LIMITER_ERROR %d\n",cutout - cutin);
}

void H4AsyncClient::setNoDelay(bool tf){
    _connGuard([=]{
        if(tf) tcp_nagle_disable(_pcb);
        else tcp_nagle_enable(_pcb);
    });
}

void H4AsyncClient::txdata(const uint8_t* d,size_t len,bool copy){ 
    _connGuard([=]{ 
        H4AT_PRINT3("H4AsyncClient::txdata 0x%08x l=%d\n",d,len);
        txdata(mbx((uint8_t*) d,len,copy));
    }); 
}

void H4AsyncClient::txdata(mbx m){
    _HAL_feedWatchdog(); // fpr BIG Q's
    if(_heapLock){ // careful....!
        H4AT_PRINT1("HEAPLOCKED DISCARD %u BYTES\n",m.len);
        DISPATCH(this,Error,H4AT_HEAP_LIMITER_LOST,m.len);
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
            DISPATCH(this,Error,H4AT_HEAP_LIMITER_ON,h);
            txdata(m); // recurse to get rid of failed m
        }
    }
}

void H4AsyncClient::TCPurl(const char* url,const uint8_t* fingerprint){
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
void H4AsyncClient::dumpQ(H4AT_MSG_Q& q) {
    H4AT_MSG_Q cq=q;
    H4AT_PRINT2("dumpQ: size=%d\n",q.size());
    while(!cq.empty()){
        mbx tmp=cq.front();
        Serial.printf("DUMPQ data=0x%08x len=%d\n",tmp.data,tmp.len);
        cq.pop();
    }
}

void H4AsyncClient::dump(){ 
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
void H4AsyncClient::dumpQ(H4AT_MSG_Q& q) {}
void H4AsyncClient::dump(){}
#endif
/*

/*
H4AsyncServer::H4AsyncServer(IPAddress addr, uint16_t port)
: _port(port)
, _addr(addr)
, _noDelay(false)
, _pcb(0)
, _connect_cb(0)
, _connect_cb_arg(0)
{}
*/
H4AsyncServer::H4AsyncServer(uint16_t port): _port(port){ H4AT_PRINT1("SERVER=0x%08x\n",this); }

H4AsyncServer::~H4AsyncServer(){
    end();
}

void H4AsyncServer::begin(){
    if(_pcb) return;

    int8_t err;
    _pcb = tcp_new();
    if (!_pcb) return;

    err = tcp_bind(_pcb, IP_ADDR_ANY, _port);
    if (err != ERR_OK) {
        tcp_close(_pcb);
        return;
    }

    //static uint8_t backlog = 5; // #define this
    _pcb = tcp_listen_with_backlog(_pcb, 3);
    H4AT_PRINT1("SERVER tcp_listen_with_backlog moi=0x%08x p=0x%08x err=%d TDLB=%d TDLB=%d\n",this,_pcb,err,TCP_DEFAULT_LISTEN_BACKLOG,TCP_LISTEN_BACKLOG);
    if (!_pcb) return;

    tcp_setprio(_pcb, TCP_PRIO_MIN);
    tcp_arg(_pcb, (void*) this);
    tcp_accept(_pcb, &_s_accept);
}

void H4AsyncServer::end(){
    if(_pcb){
        tcp_arg(_pcb, NULL);
        tcp_accept(_pcb, NULL);
        if(tcp_close(_pcb) != ERR_OK) tcp_abort(_pcb);
        _pcb = NULL;
    }
}

void H4AsyncServer::setNoDelay(bool nodelay){ _noDelay = nodelay; }

bool H4AsyncServer::getNoDelay(){ return _noDelay; }

uint8_t H4AsyncServer::status(){
    if (!_pcb) return 0;
    return _pcb->state;
}

err_t H4AsyncServer::_s_accept(void * arg, tcp_pcb * tpcb, err_t err){
    H4AT_PRINT4("_s_accept INCOMING!!! arg=0x%08x p=0x%08x\n",arg,tpcb);
    auto p=reinterpret_cast<H4AsyncServer*>(arg);
    p->_client_cb(tpcb);
    return ERR_OK;
}