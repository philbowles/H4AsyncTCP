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
    #include "lwip/tcp.h"
    #include "lwip/dns.h"
}

#ifdef ARDUINO_ARCH_ESP32
    #include "lwip/priv/tcpip_priv.h"
#endif

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
    {H4AT_INPUT_TOO_BIG,"Input exceeds safe heap"},
    {H4AT_OUTPUT_TOO_BIG,"Output exceeds safe heap"}
#endif
};
/*
void _ocGuard(H4AsyncClient *rq,H4_FN_VOID f){
    if(H4AsyncClient::openConnections.count(rq)) f();
    else H4AT_PRINT1("INVALID ACTION ON %p - ALREADY GONE!!!\n",rq);
}
*/
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
typedef struct {
    struct tcp_pcb* pcb;
    const uint8_t* data;
    size_t size;
    uint8_t apiflags;
} tcp_api_call_t;

void H4AsyncClient::_shutdown(){
    H4AT_PRINT1("_shutdown %p\n",this);
    if(pcb){
        H4AT_PRINT1("RAW 1 cleanup queues PCB=%p STATE=%d\n",pcb,pcb->state);
        _clearDanglingInput();
        H4AT_PRINT1("RAW 2 clean STATE=%d\n",pcb->state);
        tcp_arg(pcb, NULL);
        tcp_recv(pcb, NULL);
        tcp_err(pcb, NULL);
        err_t err=ERR_OK;
        H4AT_PRINT1("RAW 3 CLOSE STATE=%d\n",pcb->state);
        err=tcp_close(pcb);
        H4AT_PRINT1("RAW 4 err=%d STATE=%d\n",err,pcb->state);
        if(_cbDisconnect) _cbDisconnect();
        H4AT_PRINT1("RAW 4a err=%d STATE=%d\n",err,pcb->state);
        pcb=NULL; // == eff = reset;
        auto c=this;
        h4.queueFunction([c]{
            H4AT_PRINT1("RAW 5 offload %p\n",c);
            openConnections.erase(c);
            H4AT_PRINT1("RAW 6 OC cleared\n");
            delete c;
        });
    } //else Serial.printf("WTF???? %p pcb=0!\n",this);
}

err_t _raw_poll(void *arg, struct tcp_pcb *tpcb){ // purely for startup timeout shortening
    auto c=reinterpret_cast<H4AsyncClient*>(arg);
    H4AT_PRINT2("_raw_poll %p %p\n",arg,c);
    static size_t tix=0;
    tix++;
    if(!(tix%c->_cnxTimeout)){
        tix=0;
        tcp_abort(tpcb);
        c->pcb=NULL;
        return ERR_ABRT;
    }
    return ERR_OK;
}

void _raw_error(void *arg, err_t err){
    H4AT_PRINT2("CONNECTION %p *ERROR* err=%d %s\n",arg,err,H4AsyncClient::errorstring(err).data());
    auto rq=reinterpret_cast<H4AsyncClient*>(arg);
    if(err) { rq->_notify(err,0); }
    //else Serial.printf("NO NEED TO PANIC!!!\n");
}

err_t _raw_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err){
    if(err) Serial.printf("WATTTTTAAAAFFFFFFFFFF???? %d\n",err);
    auto rq=reinterpret_cast<H4AsyncClient*>(arg);
    if(rq->_closing){
        Serial.printf("_raw_recv during close %p pcb=%p!!!\n",rq,p);
        return ERR_ABRT;
    }
    //H4AT_PRINT1("CONNECTION %p raw_recv PCB=%p PBUF=%p PL=%p L=%d\n",arg,tpcb,p,p ? p->payload:0,p ? p->tot_len:0);
    if (p == NULL) {
        rq->_closing=true;
        H4AT_PRINT1("CONNECTION %p remote host closed connection PCB=%p b=%p e=%d\n",rq,tpcb,p,err);
        _raw_error(rq,ERR_CLSD);
        rq->_shutdown();
        return ERR_ABRT;
    }
    else {
    Serial.printf("************* _raw_recv %p %p\n",tpcb,p);
    dumphex((const uint8_t*) p->payload,64);
        rq->_lastSeen=millis();
        auto cp=p;
        auto ctpcb=tpcb;
        H4AT_PRINT1("************* GRAB DATA %p %d 0x%02x bpp=%p\n",cp->payload,cp->tot_len,cp->flags,rq->_bpp);
            h4.queueFunction(
            [rq,cp]{
                rq->_handleFragment((const uint8_t*) cp->payload,cp->tot_len,cp->flags);
            },
            [rq,ctpcb,cp]{
                tcp_recved(ctpcb, cp->tot_len);
                H4AT_PRINT1("************* FREE DATA %p %d 0x%02x bpp=%p\n",cp->payload,cp->tot_len,cp->flags,rq->_bpp);
                pbuf_free(cp);
            });
        return ERR_OK;
    }
}

err_t _tcp_connected(void* arg, void* tpcb, err_t err){
    auto rq=reinterpret_cast<H4AsyncClient*>(arg);
    auto p=reinterpret_cast<tcp_pcb*>(tpcb);
    tcp_poll(p,NULL,0);
    H4AT_PRINT2("C=%p cnx timer %d cancelled\n",rq,H4AT_CNX_TIMEOUT); //rq->_cnxTimeout);

#if H4AT_DEBUG
    IPAddress ip(ip_addr_get_ip4_u32(&p->remote_ip));
    H4AT_PRINT2("C=%p _tcp_connected p=%p e=%d IP=%s:%d\n",rq,tpcb,err,ip.toString().c_str(),p->remote_port);
#endif
    h4.queueFunction([rq]{
        H4AsyncClient::openConnections.insert(rq);
        if(rq->_cbConnect) rq->_cbConnect();
    });
    tcp_recv(p, &_raw_recv);
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

#ifdef ARDUINO_ARCH_ESP32
static err_t _tcp_write_api(struct tcpip_api_call_data *api_call_params){
#else
static err_t _tcp_write_api(void *api_call_params){
#endif
    tcp_api_call_t * params = (tcp_api_call_t *)api_call_params;
    auto err = tcp_write(params->pcb, params->data, params->size, params->apiflags);
    if(err) Serial.printf("ERR %d after write H=%u sb=%d Q=%d\n",err,_HAL_freeHeap(),tcp_sndbuf(params->pcb),tcp_sndqueuelen(params->pcb));
    return err;
}

static err_t _tcp_write(struct tcp_pcb* p,const uint8_t* data, size_t size, uint8_t apiflags) {
    tcp_api_call_t params{p,data,size,apiflags};
    #ifdef ARDUINO_ARCH_ESP32
        return tcpip_api_call(_tcp_write_api, (struct tcpip_api_call_data*)&params);
    #else
        return _tcp_write_api((void*) &params);
    #endif
}
//
//
//
H4AsyncClient::H4AsyncClient(struct tcp_pcb *newpcb,size_t timeout): pcb(newpcb),_cnxTimeout(timeout){
    _heapLO=(_HAL_freeHeap() * H4T_HEAP_CUTOUT_PC) / 100;
    _heapHI=(_HAL_freeHeap() * H4T_HEAP_CUTIN_PC) / 100;
    H4AT_PRINT1("H4AC CTOR %p PCB=%p LO=%u HI=%u\n",this,newpcb,_heapLO,_heapHI);
    if(pcb){
        tcp_arg(pcb, this);
        tcp_recv(pcb, _raw_recv);
        tcp_err(pcb, _raw_error);
//        tcp_nagle_enable(pcb); // FIX!!!!!!!!!
    }
}

void H4AsyncClient::_busted(size_t len) {
    _clearDanglingInput();
    _notify(H4AT_INPUT_TOO_BIG,len);
}

void H4AsyncClient::_clearDanglingInput() {
  H4AT_PRINT1("_clearDanglingInput <-- %u\n",_HAL_freeHeap());
    if(_bpp){
        H4AT_PRINT1("_clearDanglingInput p=%p _s=%d\n",_bpp,_stored);
        free(_bpp);
        _bpp=nullptr;
        _stored=0;
    }
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

uint8_t* H4AsyncClient::_addFragment(const uint8_t* data,u16_t len){
    if(_stored + len > maxPacket()){
        _clearDanglingInput();
        _notify(H4AT_INPUT_TOO_BIG,_stored + len);
        return nullptr;
    }
    else {
        uint8_t* p=static_cast<uint8_t*>(realloc(_bpp,_stored+len));
        H4AT_PRINT1("AF realloc %p freed, new=%p\n",_bpp,p);
        free(_bpp);
        if(p){
            _bpp=p;
            memcpy(_bpp+_stored,data,len);
            _stored+=len;
            return p;
        }
        // shouldn't ever happen!
        else {
            Serial.printf("not enough realloc mem\n");
            _clearDanglingInput();
            return nullptr;
        }
    }
}

void H4AsyncClient::_handleFragment(const uint8_t* data,u16_t len,u8_t flags) {
    H4AT_PRINT1("%p _handleFragment %p %d f=0x%02x bpp=%p _s=%d\n",this,data,len,flags,_bpp,_stored);
    if(!_closing){
        if(flags & PBUF_FLAG_PUSH){
            if(!_stored) _rxfn(data,len);
            else {
                if(!_addFragment(data,len)) _notify(ERR_MEM,len);
                else {
                    _rxfn(_bpp,_stored);
                    _clearDanglingInput();
                }
            }
        } else if(!_addFragment(data,len)) _notify(ERR_MEM,len);
    } else Serial.printf("HF while closing!!!\n");
}

bool H4AsyncClient::_heapGuard(H4_FN_VOID f){
    static size_t limit=_heapLO;
    auto h=_HAL_freeHeap();
    //H4AT_PRINT1("Heapguard h=%u LO=%u HI=%u LIM=%u\n",h,_heapLO,_heapHI,limit);
    if(h > limit){
        if(limit == _heapHI){
            limit = _heapLO;
            _notify(H4AT_HEAP_LIMITER_OFF,h);
        }
        f();
        return true;
    }
    else {
        if(limit == _heapLO){
            limit = _heapHI;
            _notify(H4AT_HEAP_LIMITER_ON,h);
        }
    }
    return false;
}

void H4AsyncClient::_scavenge(){
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
                rq->_closing=true;
                rq->_shutdown();
            }
        },
        []{
            h4.cancel();
            H4AT_PRINT1("Scavenging stopped\n"); 
        },
        H4AT_SCAVENGER_ID,
        true
    );
}
//
//      PUBLICS
//
void H4AsyncClient::connect(const std::string& host,uint16_t port){
    H4AT_PRINT2("connect h=%s, port=%d\n",host.data(),port);
//    _cnxGuard([=]{
        IPAddress ip;
        if(ip.fromString(host.data())) connect(ip,port);
        else {
            _URL.port=port;
            err_t err = dns_gethostbyname(host.data(), &_URL.addr, (dns_found_callback)&_tcp_dns_found, this);
            if(err != ERR_OK) {
                _notify(H4AT_ERR_DNS_FAIL,0);
                return;
            }
        }
//    });
}

void H4AsyncClient::connect(const std::string& url){
    _parseURL(url);
   connect(_URL.host.data(),_URL.port);
}

void H4AsyncClient::connect(IPAddress ip,uint16_t port){
    H4AT_PRINT2("connect ip=%s, port=%d\n",ip.toString().c_str(),_URL.port);
//    _cnxGuard([=]{
        _URL.port=port;
        ip_addr_set_ip4_u32(&_URL.addr, ip);
        _connect();
//    });
}

bool H4AsyncClient::connected(){ return pcb && pcb->state== 4; }

std::string H4AsyncClient::errorstring(int e){
    #ifdef H4AT_DEBUG
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

void H4AsyncClient::nagle(bool enable){
    if(pcb){
        if(enable) tcp_nagle_enable(pcb);
        else tcp_nagle_disable(pcb);
    } else Serial.printf("NAGLE PCB NULL\n");
}

uint32_t H4AsyncClient::remoteAddress(){ return ip_addr_get_ip4_u32(&pcb->remote_ip); }
IPAddress H4AsyncClient::remoteIP(){ return IPAddress( remoteAddress()); }
std::string H4AsyncClient::remoteIPstring(){ return std::string(remoteIP().toString().c_str()); }
uint16_t H4AsyncClient::remotePort(){ return pcb->remote_port;  }

void H4AsyncClient::TX(const uint8_t* data,size_t len,bool copy){ 
    H4AT_PRINT2("TX %p len=%d copy=%d max=%d\n",data,len,copy, maxPacket());
    if(!_closing){    
//        if(len > maxPacket()) {
//            Serial.printf("maxPacket HMB=%d CPC=%d res=%d\n",_HAL_maxHeapBlock(),H4T_HEAP_CUTIN_PC, ( _HAL_maxHeapBlock() * (100-H4T_HEAP_CUTIN_PC)) / 100);
//            _notify(H4AT_OUTPUT_TOO_BIG,len);
//        }
//        else {
            uint8_t flags=copy ? TCP_WRITE_FLAG_COPY:0;
            size_t  sent=0;
            size_t  left=len;
            _lastSeen=millis();
            while(left){
                size_t available=tcp_sndbuf(pcb);
                if(available && (tcp_sndqueuelen(pcb) < TCP_SND_QUEUELEN )){
                    auto chunk=std::min(left,available);
                    if(left - chunk) flags |= TCP_WRITE_FLAG_MORE;
                    H4AT_PRINT3("TX CHUNK %p len=%d left=%u f=0x%02x\n",data+sent,chunk,left,flags);
                    auto err=_tcp_write(pcb,data+sent,chunk,flags);
                    if(err){
                        _notify(err,33);
                        break;
                    } 
                    else {
                        sent+=chunk;
                        left-=chunk;
                    }
                } 
                else {
                    _HAL_feedWatchdog();
                    yield();
                }
            }
//        }
    } else H4AT_PRINT1("%p _TX called during close!\n",this);
}