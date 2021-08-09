/* Licence: 
Creative Commons
Attribution-NonCommercial-ShareAlike 4.0 International (CC BY-NC-SA 4.0)
https://creativecommons.org/licenses/by-nc-sa/4.0/legalcode

You are free to:

Share — copy and redistribute the material in any medium or format
Adapt — remix, transform, and build upon the material

The licensor cannot revoke these freedoms as long as you follow the license terms. Under the following terms:

Attribution — You must give appropriate credit, provide a link to the license, and indicate if changes were made. 
You may do so in any reasonable manner, but not in any way that suggests the licensor endorses you or your use.

NonCommercial — You may not use the material for commercial purposes.

ShareAlike — If you remix, transform, or build upon the material, you must distribute your contributions 
under the same license as the original.

No additional restrictions — You may not apply legal terms or technological measures that legally restrict others 
from doing anything the license permits.

Notices:
You do not have to comply with the license for elements of the material in the public domain or where your use is 
permitted by an applicable exception or limitation. To discuss an exception, contact the author:

philbowles2012@gmail.com

No warranties are given. The license may not give you all of the permissions necessary for your intended use. 
For example, other rights such as publicity, privacy, or moral rights may limit how you use the material.
*/
#include <H4AsyncTCP.h>

#include "IPAddress.h"

extern "C"{
    #include "lwip/opt.h"
    #include "lwip/tcp.h"
    #include "lwip/inet.h"
    #include "lwip/dns.h"
}

H4AT_MEM_POOL               mbx::pool;

std::unordered_set<H4AsyncClient*> H4AsyncClient::openConnections;

H4_INT_MAP H4AsyncClient::_errorNames={
#if H4AT_DEBUG
    {ERR_OK,"No error, everything OK"},
    {ERR_MEM,"Out of memory error"}, // -1
    {ERR_BUF,"Buffer error"},
    {ERR_TIMEOUT,"Timeout"},
    {ERR_RTE,"Routing problem"},
    {ERR_INPROGRESS,"Operation in progress"}, // -5
    {ERR_VAL,"Illegal value"},
    {ERR_WOULDBLOCK,"Operation would block"},
    {ERR_USE,"Address in use"},
    {ERR_ALREADY,"Already connecting"},
    {ERR_ISCONN,"Conn already established"}, // -10
    {ERR_CONN,"Not connected"},
    {ERR_IF,"Low-level netif error"},
    {ERR_ABRT,"Connection aborted"},
    {ERR_RST,"Connection reset"},
    {ERR_CLSD,"Connection closed"},
    {ERR_ARG,"Illegal argument"},
    {H4AT_ERR_DNS_FAIL,"DNS Fail"},
    {H4AT_ERR_DNS_NF,"Remote Host not found"},
    {H4AT_ERR_UNKNOWN,"UNKNOWN"},
    {H4AT_HEAP_LIMITER_ON,"Heap Limiter ON"},
    {H4AT_HEAP_LIMITER_OFF,"Heap Limiter OFF"},
    {H4AT_HEAP_LIMITER_LOST,"Heap Limiter: packet discarded"},
    {H4AT_INPUT_TOO_BIG,"Input too big"},
    {H4AT_INPUT_PILEUP,"Input too fast for bandwidth"},
#endif
};

void _ocGuard(H4AsyncClient *rq,H4_FN_VOID f){
    if(H4AsyncClient::openConnections.count(rq)) f();
    else H4AT_PRINT1("INVALID ACTION ON %p - ALREADY GONE!!!\n",rq);
}
/*
enum tcp_state {
  CLOSED      = 0,
  LISTEN      = 1,
  SYN_SENT    = 2,
  SYN_RCVD    = 3,
  ESTABLISHED = 4,
  FIN_WAIT_1  = 5,
  FIN_WAIT_2  = 6,
  CLOSE_WAIT  = 7,
  CLOSING     = 8,
  LAST_ACK    = 9,
  TIME_WAIT   = 10
};
static const char * const tcp_state_str[] = {
  "CLOSED",
  "LISTEN",
  "SYN_SENT",
  "SYN_RCVD",
  "ESTABLISHED",
  "FIN_WAIT_1",
  "FIN_WAIT_2",
  "CLOSE_WAIT",
  "CLOSING",
  "LAST_ACK",
  "TIME_WAIT"
};
*/
void _raw_close(H4AsyncClient *rq,bool abort){
    H4AT_PRINT1("RAW CLOSE ENTRY RQ=%p abort=%d PXQ=%d IPQ=%d\n",rq,abort,rq->_PXQ.size(),rq->_fragments.size());
    _ocGuard(rq,[=]{
        H4AT_PRINT1("RAW _raw_close RQ=%p 2\n",rq);
        struct tcp_pcb *tpcb=rq->pcb;
        H4AT_PRINT1("RAW _raw_close RQ=%p PCB=%p 3\n",rq,tpcb);
        if(tpcb){
            if(rq->_inIPQ){
                Serial.printf("FRAGMENT PROBLEMS!!!!!!!!!!!!!!!!!!!!! %d\n",rq->_fragments.size());
//                while(!rq->_fragments.empty()){
//                    rq->_fragments.front().clear();
//                    rq->_fragments.pop();
//                }
//                rq->_inIPQ=false;
            }
            if(rq->_PXQ.size()){
                Serial.printf("PXQ PROBLEMS!!!!!!!!!!!!!!!!!!!!! %d\n",rq->_PXQ.size());
                while(!rq->_PXQ.empty()){
                    rq->_PXQ.front().clear();
                    rq->_PXQ.pop();
                }
                Serial.printf("PXQ cleared %d\n",rq->_PXQ.size());
            }
            H4AT_PRINT1("RAW _raw_close RQ=%p PCB=%p STATE=%d 4\n",rq,tpcb,tpcb->state);
            if(abort) tcp_abort(tpcb);
            tcp_arg(tpcb, NULL); // do we need this / these ?
            tcp_recv(tpcb, NULL);
            tcp_err(tpcb, NULL);
            H4AT_PRINT1("RAW _raw_close RQ=%p PCB=%p STATE=%d 5\n",rq,tpcb,tpcb->state);
            err_t err=tcp_close(tpcb);
            H4AT_PRINT1("RAW _raw_close RQ=%p PCB=%p STATE=%d 6\n",rq,tpcb,tpcb->state);
            if(err) Serial.printf("WTF 67???????????????????????????? %d\n",err);
            rq->pcb=NULL; // == eff = reset;
            H4AT_PRINT1("RAW _raw_close RQ=%p PCB=%p STATE=%d 7\n",rq,tpcb,tpcb->state);
            if(rq->_cbDisconnect) rq->_cbDisconnect();
            H4AT_PRINT1("RAW _raw_close RQ=%p PCB=%p STATE=%d 8\n",rq,tpcb,tpcb->state);
        } else Serial.printf("WTF???? tpcb=0!\n");
    });
}

err_t _raw_poll(void *arg, struct tcp_pcb *tpcb){
//    Serial.printf("T=%u POLL!\n",millis());
    auto c=reinterpret_cast<H4AsyncClient*>(arg);
    static size_t tix=0;
    tix++;
    if(!(tix%c->_cnxTimeout)){
        tix=0;
//        Serial.printf("T=%u BOSH it!\n",millis());
        tcp_abort(tpcb);
        c->pcb=NULL;
        return ERR_ABRT;
    }
    return ERR_OK;
}

void _raw_error(void *arg, err_t err){
    H4AT_PRINT2("CONNECTION %p *ERROR* err=%d %s\n",arg,err,H4AsyncClient::errorstring(err).data());
    auto rq=reinterpret_cast<H4AsyncClient*>(arg);
    if(err) { if(rq->_cbError) rq->_cbError(err,0); }
    else Serial.printf("NO NEED TO PANIC!!!\n");
}

err_t _raw_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err){
    if(err) Serial.printf("WATTTTTAAAAFFFFFFFFFF???? %d\n",err);
    auto rq=reinterpret_cast<H4AsyncClient*>(arg);
    H4AT_PRINT1("CONNECTION %p raw_recv PCB=%p PBUF=%p PL=%p L=%d\n",arg,tpcb,p,p ? p->payload:0,p ? p->tot_len:666);
    _ocGuard(rq,[&]{
        if (p == NULL) {
            H4AT_PRINT2("CONNECTION %p remote host closed connection PCB=%p b=%p e=%d\n",arg,tpcb,p,err);
            _raw_error(rq,ERR_CLSD);
            _raw_close(rq);
            return ERR_ABRT;
        }
        else {
            if(rq->_inIPQ){
                Serial.printf("************************ I can feel my lifetime piling up...\n");
                _raw_error(rq,H4AT_INPUT_PILEUP);
                _raw_close(rq);
                return ERR_ABRT;
            }
            else {
                rq->_lastSeen=millis();
                H4AT_PRINT1("GRAB DATA %p %d from pcb\n",p->payload,p->tot_len);
//                HAL_disableInterrupts();
                rq->_fragments.push(mbx((uint8_t*) p->payload,p->tot_len,true,p->flags));
                rq->_stored+=p->tot_len;
//                HAL_enableInterrupts();
                if(p->flags & PBUF_FLAG_PUSH) h4.queueFunction([=]{ rq->_runIPQ(); });
                tcp_recved(tpcb, p->tot_len);
                pbuf_free(p);                    
                return ERR_OK;
            }
        }
    });
    return ERR_ABRT;
}

err_t _tcp_connected(void* arg, void* tpcb, err_t err){
    auto rq=reinterpret_cast<H4AsyncClient*>(arg);
    tcp_poll(reinterpret_cast<struct tcp_pcb*>(tpcb),NULL,0);
    H4AT_PRINT1("C=%p cnx timer %d cancelled\n",rq,H4AT_CNX_TIMEOUT); //rq->_cnxTimeout);
    auto p=reinterpret_cast<tcp_pcb*>(tpcb);
#if H4AT_DEBUG
    IPAddress ip(ip_addr_get_ip4_u32(&p->remote_ip));
    H4AT_PRINT2("C=%p _tcp_connected p=%p e=%d IP=%s:%d\n",rq,tpcb,err,ip.toString().c_str(),p->remote_port);
#endif
    tcp_recv(p, &_raw_recv);
    H4AsyncClient::openConnections.insert(rq);
    if(rq->_cbConnect) rq->_cbConnect();
    return ERR_OK;
}

void _tcp_dns_found(const char * name, struct ip_addr * ipaddr, void * arg) {
    H4AT_PRINT2("_tcp_dns_found %s i=%p p=%p\n",name,ipaddr,arg);
    if(ipaddr){
        auto p=reinterpret_cast<H4AsyncClient*>(arg);
        ip_addr_copy(p->_URL.addr, *ipaddr);
        p->_connect();
    }
    else {
        auto rq=reinterpret_cast<H4AsyncClient*>(arg);
        if(rq->_cbError) rq->_cbError(H4AT_ERR_DNS_NF,0);
    }
}
//
//
//
H4AsyncClient::H4AsyncClient(struct tcp_pcb *newpcb,size_t timeout): pcb(newpcb),_cnxTimeout(timeout){
    H4AT_PRINT1("H4AC CTOR %p PCB=%p\n",this,newpcb);
    if(pcb){
        tcp_arg(pcb, this);
        tcp_recv(pcb, _raw_recv);
        tcp_err(pcb, _raw_error);
        tcp_nagle_enable(pcb); // FIX!!!!!!!!!
    }
}

H4AsyncClient::~H4AsyncClient(){
    H4AT_PRINT1("H4AsyncClient DTOR %p pool=%d PXQ=%d\n",this,mbx::pool.size(),_PXQ.size());
}

void H4AsyncClient::_busted(size_t len) {
    _clearFragments();
//    mbxemptyPool();
//_cbError(H4AT_INPUT_TOO_BIG,len);
}

void H4AsyncClient::_clearFragments() {
    H4AT_PRINT1("_clearFragments %p N=%d pool=%d\n",this,_fragments.size(),mbx::pool.size());
    while(!_fragments.empty()){
        _fragments.front().clear();
        _fragments.pop();
    }
}

void H4AsyncClient::_cnxGuard(H4_FN_VOID f) { // tidy this
    H4AT_PRINT1("_cnxGuard p=%p\n",pcb);
    if(pcb){
        H4AT_PRINT1("_cnxGuard state=%d\n",pcb->state);
        switch(pcb->state){
            case 2:
            case 4:
                if(_cbError) _cbError(pcb->state == 4 ? ERR_ISCONN:ERR_ALREADY,pcb->state);
                return;
        }
    }
    else f();
}

void H4AsyncClient::_connect() {
    H4AT_PRINT1("_connect\n");
    if(!pcb) pcb=tcp_new();
    H4AT_PRINT2("_connect p=%p\n",pcb);
    tcp_arg(pcb, this);
    tcp_err(pcb, &_raw_error);
    size_t err = tcp_connect(pcb, &_URL.addr, _URL.port,(tcp_connected_fn)&_tcp_connected);
    if(err) { if(_cbError) _cbError(err,0); }
    tcp_poll(pcb,&_raw_poll,2);
}

void H4AsyncClient::_runIPQ() {
    if(pcb){
        if(_fragments.size()==1){
            H4AT_PRINT1("Non fragged optimisation!\n");
            _rxfn(_fragments.front().data,_fragments.front().len);
            _fragments.front().clear();
            _fragments.pop();
        }
        else {
            _inIPQ=true;
            uint8_t* bpp=mbx::getMemory(_stored);
            size_t  pktsz=_stored;
            if(bpp){
                uint8_t* p=bpp;
                while(!_fragments.empty()){
                    H4AT_PRINT1("RECREATE %p len=%d\n",_fragments.front().data,_fragments.front().len);
                    memcpy(p,_fragments.front().data,_fragments.front().len);
                    p+=_fragments.front().len;
                    _fragments.front().clear();
                    _fragments.pop();
                    _HAL_feedWatchdog();
                }
                H4AT_PRINT1("CALL USER %p _stored=%d\n",bpp,pktsz);
                _rxfn(bpp,pktsz);
                H4AT_PRINT1("BACK FROM USER BPP=%p l=%d\n",bpp,pktsz);
                mbx::clear(bpp);
            } else _busted(_stored);
            _inIPQ=false;
        }
        _stored=0;
    } else Serial.printf("EEEEEEEEEEEEEEEEKKKKKKKKKKKKKK IPQ with no PCB!!!\n");
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

void H4AsyncClient::_runPXQ(){
    H4AT_PRINT2("_runPXQ\n");
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

void H4AsyncClient::_TX(){
    H4AT_PRINT2("TX %p len=%d PXQ=%d managed=%d PCB=%p\n",_PXQ.front().data,_PXQ.front().len,_PXQ.size(),_PXQ.front().managed,pcb);
    uint8_t flags=_PXQ.front().managed ? TCP_WRITE_FLAG_COPY:0;
    uint8_t* base=_PXQ.front().data;
    while(_PXQ.front().len){
        if(pcb){
            int available=tcp_sndbuf(pcb);
            if(available){
                auto chunk=std::min(_PXQ.front().len,available);
                H4AT_PRINT2("remaining=%d available=%d chunk=%d\n",_PXQ.front().len,available,chunk);
                if(_PXQ.front().len) flags |= TCP_WRITE_FLAG_MORE;
                err_t err=tcp_write(pcb,_PXQ.front().data,chunk,flags);
                H4AT_PRINT2("remaining err=%d\n",err);
                if(!err){
                    _lastSeen=millis();
                    _PXQ.front().data+=chunk;
                    _PXQ.front().len-=chunk;
                }
                else {
                    if(_cbError) _cbError(err,22);
                    break;
                }
            } 
            else {
//                H4AT_PRINT2("relax and swing...%d",tcp_sndbuf(pcb));
                _HAL_feedWatchdog(); 
                yield();
            }
        } 
        else {
            Serial.printf("WAAAAAAAATTTTTTTTTTT   FFFFFFFFFFF NO PCB!!!!! PXQ=%d\n",_PXQ.size());
//            while(!_PXQ.empty()){
//                _PXQ.front().clear();
//                _PXQ.pop();
//            }
            break;
        }
    }
    mbx::clear(base);
    if(!_PXQ.empty()) _PXQ.pop(); // edge case where close has alreayd cleared Q
}
//
//
//
void H4AsyncClient::close(){ 
    _lastSeen=0;
    scavenge();
}

void H4AsyncClient::connect(const char* host,uint16_t port){
    H4AT_PRINT1("connect h=%s, port=%d\n",host,port);
    _cnxGuard([=]{
        IPAddress ip;
        if(ip.fromString(host)) connect(ip,port);
        else {
//            Serial.printf("look me up!:%s => %d\n",host,ip.fromString(host));
            _URL.port=port;
            err_t err = dns_gethostbyname(host, &_URL.addr, (dns_found_callback)&_tcp_dns_found, this);
//            Serial.printf("look me up!:%s => %d err=%d\n",host,ip.fromString(host),err);
            if(err != ERR_OK) {
                if(_cbError) _cbError(H4AT_ERR_DNS_FAIL,0);
                return;
            }
        }
    });
}

void H4AsyncClient::connect(const char* url){
    H4AT_PRINT1("connect url=%s\n",url);
    _parseURL(url);
    connect(_URL.host.data(),_URL.port);
}

void H4AsyncClient::connect(IPAddress ip,uint16_t port){
    H4AT_PRINT1("connect ip=%s, port=%d\n",ip.toString().c_str(),_URL.port);
    _cnxGuard([=]{
        _URL.port=port;
        ip_addr_set_ip4_u32(&_URL.addr, ip);
        _connect();
    });
}

void H4AsyncClient::connect(){ 
    H4AT_PRINT1("connect uh=%s, uport=%d\n",_URL.host.data(),_URL.port);
    connect(_URL.host.data(),_URL.port);
}

bool H4AsyncClient::connected(){ return pcb && pcb->state== 4; }

void H4AsyncClient::dumpQ(H4AT_MSG_Q& q) {
#if H4AT_DEBUG
    H4AT_MSG_Q cq=q;
    H4AT_PRINT2("dumpQ: size=%d\n",q.size());
    while(!cq.empty()){
        mbx tmp=cq.front();
        Serial.printf("DUMPQ data=%p len=%d\n",tmp.data,tmp.len);
        cq.pop();
    }
#endif
}

void H4AsyncClient::dump(){ 
#if H4AT_DEBUG
    H4AT_PRINT1("LOCAL: RAW=%p IPA=%s, IPS=%s port=%d\n",localAddress(),localIP().toString().c_str(),localIPstring().data(),localPort());
    H4AT_PRINT1("REMOTE: RAW=%p IPA=%s, IPS=%s port=%d\n",remoteAddress(),remoteIP().toString().c_str(),remoteIPstring().data(),remotePort());
    H4AT_PRINT1("Last Seen=%u Age(s)=%u\n",_lastSeen,(millis()-_lastSeen)/1000);

//    mbxdumpPool(32);
    dumpQ(_PXQ);
//    for(auto & p:_fragments) Serial.printf("MBX %p len=%d\n",(void*) p.data,p.len);
#endif
}

std::string H4AsyncClient::errorstring(int e){
    #ifdef H4AT_DEBUG
        H4AT_PRINT1("errorstring(%d)\n",e);
        if(_errorNames.count(e)) return _errorNames[e];
        else return stringFromInt(e); 
    #else
        return stringFromInt(e); 
    #endif
}

uint32_t H4AsyncClient::localAddress(){ return ip_addr_get_ip4_u32(&pcb->local_ip); }
IPAddress H4AsyncClient::localIP(){ return IPAddress( localAddress()); }
std::string H4AsyncClient::localIPstring(){ return std::string(localIP().toString().c_str()); }
uint16_t H4AsyncClient::localPort(){ return pcb->local_port; };

uint32_t H4AsyncClient::remoteAddress(){ return ip_addr_get_ip4_u32(&pcb->remote_ip); }
IPAddress H4AsyncClient::remoteIP(){ return IPAddress( remoteAddress()); }
std::string H4AsyncClient::remoteIPstring(){ return std::string(remoteIP().toString().c_str()); }
uint16_t H4AsyncClient::remotePort(){ return pcb->remote_port;  }

void H4AsyncClient::TCPurl(const char* url,const uint8_t* fingerprint){
    H4AT_PRINT1("TCPurl %s secure=%d\n",url,_URL.secure);
    _parseURL(url);
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

void H4AsyncClient::txdata(const uint8_t* d,size_t len,bool copy){ 
    H4AT_PRINT1("H4AsyncClient::txdata %p l=%d copy=%d\n",d,len,copy);
    _ocGuard(this,[=]{ txdata(mbx((uint8_t*) d,len,copy)); }); 
}

void H4AsyncClient::txdata(mbx m){
    _PXQ.push(m);
    _runPXQ();
/*
//    _HAL_feedWatchdog(); // fpr BIG Q's
    if(_heapLock){ // careful....!
        H4AT_PRINT1("HEAPLOCKED DISCARD %u BYTES\n",m.len);
        DISPATCH(this,Error,H4AT_HEAP_LIMITER_LOST,m.len);
        m.clear();
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
   */
}

void H4AsyncClient::scavenge(){
    h4.repeatWhile(
        []{ return openConnections.size(); },
        H4AS_SCAVENGE_FREQ,
        []{
            H4AT_PRINT1("SCAVENGE CONNECTIONS!\n");
            std::vector<H4AsyncClient*> tbd;
            for(auto &oc:openConnections){
                H4AT_PRINT1("T=%u OC %p ls=%u age(s)=%u SCAV=%u\n",millis(),oc,oc->_lastSeen,(millis() - oc->_lastSeen) / 1000,H4AS_SCAVENGE_FREQ);
                if((millis() - oc->_lastSeen) > H4AS_SCAVENGE_FREQ) tbd.push_back(oc);
            }
            for(auto &rq:tbd) {
                _raw_close(rq); // why not abort?
                openConnections.erase(rq);
                delete rq;
            }
            H4AT_PRINT1("\n");
        },
        []{
            H4AT_PRINT1("ALL CONNECTIONS CLOSED!\n");
            h4.cancel();
            H4AT_PRINT1("Scavenging stopped\n"); 
        },
        H4AT_SCAVENGER_ID,
        true
    );
}