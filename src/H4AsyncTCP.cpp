extern "C" {
    #include "lwip/opt.h"
    #include "lwip/debug.h"
    #include "lwip/stats.h"
    #include "lwip/tcp.h"
}

#include <H4AsyncTCP.h>

static std::unordered_set<H4AsyncClient*> H4ATopenConnections;

H4_INT_MAP H4AT_HTTPHandler::_responseCodes{
    {100,"Continue"},
    {101,"Switching Protocols"},
    {200,"OK"},
    {304,"Not Modified"},
    {400,"Bad Request"},
    {401,"Unauthorized"},
    {403,"Forbidden"},
    {404,"Not Found"},
    {408,"Request Time-out"},
    {415,"Unsupported Media Type"},
    {500,"Internal Server Error"},
    {503,"Service Unavailable"}
};

H4AT_NVP_MAP H4AT_HTTPHandler::mimeTypes={
  {"html","text/html"},
  {"htm","text/html"},
  {"css","text/css"},
  {"json","application/json"},
  {"js","application/javascript"},
  {"png","image/png"},
  {"jpg","image/jpeg"},
  {"ico","image/x-icon"},
  {"xml","text/xml"},
};

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

static struct tcp_pcb *_raw_pcb;

static void _ocGuard(H4AsyncClient *rq,H4_FN_VOID f){
    if(H4ATopenConnections.count(rq)) f();
    else H4AT_PRINT1("INVALID ACTION ON 0x%08x - ALREADY GONE!!!\n",rq);
}
/*
template<typename T,typename R>
static err_t _h4tcp_accept(void *arg, struct tcp_pcb *newpcb, err_t err){
//    H4AT_PRINT1("_h4tcp_accept a=0x%08x p=0x%08x e=%d\n",arg,newpcb,err);
    auto srv=reinterpret_cast<T*>(arg);
    tcp_setprio(newpcb, TCP_PRIO_MIN);
    H4ATopenConnections.insert(new R(newpcb,srv));
    srv->startScavenger();
    return ERR_OK;
}
*/
static err_t _raw_accept(void *arg, struct tcp_pcb *newpcb, err_t err){
//    H4AT_PRINT1("RAW _raw_accept a=0x%08x p=0x%08x e=%d\n",arg,newpcb,err);
    auto srv=reinterpret_cast<H4AsyncWebServer*>(arg);
    tcp_setprio(newpcb, TCP_PRIO_MIN);
    H4ATopenConnections.insert(new H4AsyncClient(newpcb,srv));
    srv->startScavenger();
    return ERR_OK;
}

static void _raw_close(H4AsyncClient *rq){
//    H4AT_PRINT1("RAW _raw_close IN RQ=0x%08x\n",rq);
    _ocGuard(rq,[=]{
        struct tcp_pcb *tpcb=rq->pcb;
//        H4AT_PRINT1("RAW _raw_close IN a=0x%08x t=0x%08x pool=%d PXQ=%d TXQ=%d\n",rq,tpcb,rq->pool.size(),rq->_PXQ.size(),rq->_TXQ.size());
        tcp_arg(tpcb, NULL);
//        tcp_sent(tpcb, NULL);
        tcp_recv(tpcb, NULL);
        tcp_err(tpcb, NULL);
        tcp_close(tpcb);
        //
        H4ATopenConnections.erase(rq);
//        H4AT_PRINT2("RQ 0x%08x out of open list\n",rq);
        delete rq;
//        H4AT_PRINT2("OC 0x%08x SHE'S GONE!\n",rq);
    });
}

static void _raw_error(void *arg, err_t err){
  H4AT_PRINT1("CONNECTION 0x%08x *************ERROR********* err=%d\n",arg,err);
  auto rq=reinterpret_cast<H4AsyncClient*>(arg);
  _ocGuard(rq,[=]{
    _raw_close(rq);
    rq->XDISPATCH_V(Error,err,0);
  });
}

static err_t _raw_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err){
//    H4AT_PRINT1("CONNECTION 0x%08x raw_recv PCB=0x%08x PBUF=0x%08x e=%d\n",arg,tpcb,p,err);
    auto rq=reinterpret_cast<H4AsyncClient*>(arg);
    _ocGuard(rq,[&]{
        if (p == NULL) {
            H4AT_PRINT1("CONNECTION 0x%08x remote host closed connection PCB=0x%08x b=0x%08x e=%d\n",arg,tpcb,p,err);
            h4.queueFunction([=]{ _raw_close(rq); });
            err = ERR_OK;
        }
        else {
            if(err != ERR_OK) {
                ///* cleanup, for unknown reason
                H4AT_PRINT1("cleanup, for unknown reason a=0x%08x p=0x%08x b=0x%08x e=%d\n",arg,tpcb,p,err);
                if (p != NULL) pbuf_free(p);
//                return err;
            }
            else {
//                H4AT_PRINT1("CONNECTION 0x%08x PCB=0x%08x b=0x%08x tot=%d len=%d flags=0x%02x e=%d\n",arg,tpcb,p,p->tot_len,p->len,p->flags,err);
                h4.queueFunction([=]{
                    rq->_lastSeen=millis();
                    rq->_onData(mbx(rq,(uint8_t*) p->payload,p->tot_len,true,p->flags));
                    tcp_recved(tpcb, p->tot_len);
                    pbuf_free(p);
                });
                err = ERR_OK;
            }
        }
    });
    return err;
}
/*
static err_t _raw_sent(void *arg, struct tcp_pcb *tpcb, u16_t len){
//    H4AT_PRINT1("CONNECTION 0x%08x raw_sent PCB=0x%08x len=%d\n",arg,tpcb,len);
    auto rq = reinterpret_cast<H4AsyncClient*>(arg);
    _ocGuard(rq,[=]{
        h4.queueFunction([=]{ 
            rq->_lastSeen=millis();
            rq->_ackTCP(len);
        });
    });
    return ERR_OK;
}
*/
static err_t _tcp_connected(void* arg, void* tpcb, err_t err){
    auto p=reinterpret_cast<tcp_pcb*>(tpcb);
#if H4AT_DEBUG
    IPAddress ip(ip_addr_get_ip4_u32(&p->local_ip));
    H4AT_PRINT2("_tcp_connected p=0x%08x e=%d IP=%s:%d\n",tpcb,err,ip.toString().c_str(),p->local_port);
#endif
//    tcp_poll(p, &_tcp_poll,1); // units of 500mS
//    tcp_sent(p, &_tcp_sent);
    tcp_recv(p, &_raw_recv); 
    XDISPATCH_V(arg,Connect);
    return ERR_OK;
}

static void _tcp_dns_found(const char * name, struct ip_addr * ipaddr, void * arg) {
    H4AT_PRINT2("_tcp_dns_found %s i=0x%08x p=0x%08x\n",name,ipaddr,arg);
    if(ipaddr){
        auto p=reinterpret_cast<H4AsyncClient*>(arg);
        ip_addr_copy(p->_URL.addr, *ipaddr);
        h4.queueFunction([=]{ p->_connect(); });
    } else DISPATCH(arg,Error,H4AT_ERR_DNS_NF,0);
}


void H4AsyncWebServer::begin(){
//    H4AT_PRINT1("BEGIN 0x%08x\n",this);
    _handlers.push_back(new H4AT_HTTPHandlerFile());
    _handlers.push_back(new H4AT_HTTPHandler404());

    _raw_pcb = tcp_new();
    if (_raw_pcb != NULL) {
        err_t err;
        tcp_arg(_raw_pcb,this);
        err = tcp_bind(_raw_pcb, IP_ADDR_ANY, _port);
        if (err == ERR_OK) {
//            _raw_pcb = tcp_listen_with_backlog(_raw_pcb,1);
            _raw_pcb = tcp_listen(_raw_pcb);
            tcp_accept(_raw_pcb, _raw_accept);
        } 
        //else Serial.printf("RAW CANT BIND\n");
    } 
//    else Serial.printf("RAW CANT GET NEW PCB\n");
}

void H4AsyncWebServer::startScavenger(){
    h4.repeatWhile(
        [=]{ return H4ATopenConnections.size(); },
        H4AS_SCAVENGE_FREQ,
        [=]{
            H4AT_PRINT1("SCAVENGE CONNECTIONS!\n");
            std::vector<H4AsyncClient*> tbd;
            for(auto &oc:H4ATopenConnections){
                H4AT_PRINT1("T=%u OC 0x%08x ls=%u age(s)=%u SCAV=%u\n",millis(),oc,oc->_lastSeen,(millis() - oc->_lastSeen) / 1000,H4AS_SCAVENGE_FREQ);
                if((millis() - oc->_lastSeen) > H4AS_SCAVENGE_FREQ) tbd.push_back(oc);
            }
            for(auto &oc:tbd) _raw_close(oc);
            H4AT_PRINT1("\n");
        },
        [=]{ H4AT_PRINT1("ALL CONNECTIONS CLOSED!\n"); h4.cancel(); H4AT_PRINT1("Scavenging stopped\n"); },
        H4AS_SCAVENGE_ID,
        true
    );
}
//
//
//
H4AsyncClient::H4AsyncClient(struct tcp_pcb *newpcb,H4AsyncWebServer* srv): _server(srv),pcb(newpcb){
//    H4AT_PRINT1("NEW CONNECTION REQUEST 0x%08x PCB=0x%08x\n",this,newpcb);
    tcp_arg(newpcb, this);
    tcp_recv(newpcb, _raw_recv);
    tcp_err(newpcb, _raw_error);
//    tcp_sent(newpcb, _raw_sent);
    tcp_nagle_enable(newpcb); // FIX!!!!!!!!!
}

H4AsyncClient::~H4AsyncClient(){
//    H4AT_PRINT1("END CONNECTION REQUEST 0x%08x pool=%d PXQ=%d TXQ=%d\n",this,pool.size(),_PXQ.size(),_TXQ.size());
    if(_cbDisconnect) _cbDisconnect(13); // sort out reason code
}
/*
void H4AsyncClient::_ackTCP(uint16_t len){

    H4AT_PRINT2("<---- AK! %u\n",len);
    //size_t amtToAck=_ackSize(len); fixup later
    //
    #if H4AT_DEBUG
    auto cq=_TXQ;
    while(!cq.empty()){
        Serial.printf("    ~~~~~~~~ %d\n",cq.front().len);
        cq.pop();
    }
    #endif
    //
    size_t amtToAck=len; 
    while(amtToAck){
        _TXQ.front().len-=amtToAck;
        if(_TXQ.front().len > 0){
            H4AT_PRINT2("<---- AK! %u UNACKED LEN=%u\n",amtToAck,_TXQ.front().len);
            amtToAck=0;
        }
        else {
            amtToAck=(-1 * _TXQ.front().len);
            H4AT_PRINT2("<---- AK! %u OVERACKED\n",amtToAck);
            _TXQ.front().ack();
            _TXQ.pop();
        }
        _HAL_feedWatchdog(); // for massive acks
    } 
//    _releaseHeapLock();

}
*/

void H4AsyncClient::_busted(size_t len) {
    _clearFragments();
//    mbxemptyPool();
    //_cbError(H4AT_INPUT_TOO_BIG,len);
}

void H4AsyncClient::_clearFragments() {
    for(auto &f:_fragments) f.clear();
    _fragments.clear();
    _fragments.shrink_to_fit();
}

void H4AsyncClient::_onData(mbx m) {
    H4AT_PRINT2("<---- RX d=0x%08x len=%d _stored=%d flags=0x%02x\n",m.data,m.len,_stored,m.flags);
//    H4AT_DUMP2(m.data,m.len);
    if(m.flags & PBUF_FLAG_PUSH){
        if(!_stored) h4.queueFunction([=]{ process(m.data,m.len); },[=]{ mbx::clear(this,m.data); });
        else {
            uint8_t* bpp=mbx::getMemory(this,_stored+m.len);
            if(bpp){
                uint8_t* p=bpp;
                for(auto &f:_fragments){
                    memcpy(p,f.data,f.len);
                    H4AT_PRINT3("RECREATE %08x len=%d\n",f.data,f.len);
                    p+=f.len;
                    f.clear();
                    _HAL_feedWatchdog();
                }
                _clearFragments();
                memcpy(p,m.data,m.len);
                H4AT_PRINT3("CALL USER %08x _stored=%d len=%d sum=%d\n",bpp,_stored,m.len,_stored+m.len);
                h4.queueFunction([=]{ process(bpp,_stored+m.len); },[=]{ 
                    mbx::clear(this,bpp);
                    _stored=0;
                    H4AT_PRINT3("BACK FROM USER\n");
                });
            } else _busted(_stored+m.len);
        }
    }
    else {
        _fragments.emplace_back(this,m.data,m.len,true);
        _stored+=m.len;
        H4AT_PRINT3("CR FRAG %08x len=%d _stored=%d\n",_fragments.back().data,_fragments.back().len,_stored);
    }
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

void H4AsyncClient::_TX(){
    H4AT_PRINT2("TX 0x%08x len=%d PXQ=%d A=%d managed=%d\n",_PXQ.front().data,_PXQ.front().len,_PXQ.size(),tcp_sndbuf(pcb),_PXQ.front().managed);
    uint8_t flags=_PXQ.front().managed ? TCP_WRITE_FLAG_COPY:0;

    uint8_t* base=_PXQ.front().data;
    while(_PXQ.front().len){
        int available=tcp_sndbuf(pcb);
        if(available){
            auto chunk=std::min(_PXQ.front().len,available);
            H4AT_PRINT2("remaining=%d available=%d chunk=%d\n",_PXQ.front().len,available,chunk);
            if(_PXQ.front().len) flags |= TCP_WRITE_FLAG_MORE;
            err_t err=tcp_write(pcb,_PXQ.front().data,chunk,flags);// arbitrary, sex it up - maxpaxket / sndbubuf
            if(!err){
                _lastSeen=millis();
                _PXQ.front().data+=chunk;
                _PXQ.front().len-=chunk;
            }
            else {
                mbx::clear(this,base);
                _PXQ.pop();
                XDISPATCH_V(Error,err,22);
                break;
            }
        }
        else {
            _HAL_feedWatchdog(); 
            yield();
        }
    }
    mbx::clear(this,base);
    _PXQ.pop();
}
//
//
//
void H4AsyncClient::close(){ _raw_close(this); }

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

void H4AsyncClient::dumpQ(H4AT_MSG_Q& q) {
#if H4AT_DEBUG
    H4AT_MSG_Q cq=q;
    H4AT_PRINT2("dumpQ: size=%d\n",q.size());
    while(!cq.empty()){
        mbx tmp=cq.front();
        Serial.printf("DUMPQ data=0x%08x len=%d\n",tmp.data,tmp.len);
        cq.pop();
    }
#endif
}

void H4AsyncClient::dump(){ 
#if H4AT_DEBUG
    H4AT_PRINT1("LOCAL: RAW=0x%08x IPA=%s, IPS=%s port=%d\n",localAddress(),localIP().toString().c_str(),localIPstring().data(),localPort());
    H4AT_PRINT1("REMOTE: RAW=0x%08x IPA=%s, IPS=%s port=%d\n",remoteAddress(),remoteIP().toString().c_str(),remoteIPstring().data(),remotePort());
    H4AT_PRINT1("Last Seen=%u Age(s)=%u\n",_lastSeen,(millis()-_lastSeen)/1000);

    mbxdumpPool(32);
    dumpQ(_PXQ);
    for(auto & p:_fragments) Serial.printf("MBX 0x%08x len=%d\n",(void*) p.data,p.len);
#endif
}

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

void H4AsyncClient::process(uint8_t* data,size_t len){
//    H4AT_DUMP4(data,len);
    std::vector<std::string> rqst=split(std::string((const char*)data,len),"\r\n");
    std::vector<std::string> vparts=split(rqst[0]," ");

    H4AT_NVP_MAP _rqHeaders;
    for(auto &r:std::vector<std::string>(++rqst.begin(),--rqst.end())){
        std::vector<std::string> rparts=split(r,":");
        _rqHeaders[uppercase(rparts[0])]=trim(rparts[1]);
    }
//    for(auto &r:_rqHeaders) Serial.printf("RQ %s=%s\n",r.first.data(),r.second.data());

    //H4AT_PRINT1("CONNECTION 0x%08x PCB=0x%08x REQUEST %s %s checking %d _handlers\n",this,pcb,vparts[0].data(),vparts[1].data(),_server->_handlers.size());
    for(auto h:_server->_handlers){
        for(auto &s:h->_sniffHeader) if(_rqHeaders.count(uppercase(s.first))) h->_sniffHeader[s.first]=_rqHeaders[uppercase(s.first)];
        if(h->handled(this,vparts[0],vparts[1])) break;
    }
}

uint32_t H4AsyncClient::remoteAddress(){ return ip_addr_get_ip4_u32(&pcb->remote_ip); }
IPAddress H4AsyncClient::remoteIP(){ return IPAddress( remoteAddress()); }
std::string H4AsyncClient::remoteIPstring(){ return std::string(remoteIP().toString().c_str()); }
uint16_t H4AsyncClient::remotePort(){ return pcb->remote_port;  }

void H4AsyncClient::txdata(const uint8_t* d,size_t len,bool copy){ 
//    H4AT_PRINT4("H4AsyncClient::txdata 0x%08x l=%d cop=%d\n",d,len,copy);
//    _connGuard([=]{ 
        H4AT_PRINT3("H4AsyncClient::txdata 0x%08x l=%d\n",d,len);
        txdata(mbx(this,(uint8_t*) d,len,copy));
//    }); 
}

void H4AsyncClient::txdata(mbx m){
    _PXQ.push(m);
    _runPXQ();
/*
//    _HAL_feedWatchdog(); // fpr BIG Q's
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
   */
}

void H4AT_HTTPHandler::send(uint16_t code,const std::string& type,size_t length,const void* body){
    H4AT_PRINT2("CONNECTION 0x%08x send(%d,%s,%d,0x%08x)\n",this,code,type.data(),length,body);

    std::string status=std::string("HTTP/1.1 ")+stringFromInt(code,"%3d ").append(_responseCodes[code])+"\r\n";
    _headers["Content-Type"]=type;
    if(length) _headers["Content-Length"]=stringFromInt(length);
    for(auto const& h:_headers) status+=h.first+": "+h.second+"\r\n";
    status+="\r\n";
    //
    auto h=status.size();
    auto total=h+length;
    uint8_t* buff=(uint8_t*) malloc(total);
    if(buff){
        memcpy(buff,status.data(),h);
        if(length) memcpy(buff+h,body,length);
        _r->txdata(buff,total);
        //H4AT_DUMP2(buff,total);
        free(buff);
    } else _raw_error(_r,ERR_MEM);
}

void H4AT_HTTPHandler::sendFile(const std::string& fn){ }//H4AT_HTTPHandlerFile::serveFile(this,fn); }
        
void H4AT_HTTPHandler::sendFileParams(const std::string& fn,H4AT_NVP_MAP& params){
    std::string fdata=stringFromFile(fn);
    std::string rdata=replaceParams(fdata,params);
    send(200,mimeType(fn),rdata.size(),rdata.data());
}

std::string H4AT_HTTPHandler::mimeType(const std::string& fn){
    std::string e = fn.substr(fn.rfind('.')+1);
    return mimeTypes.count(e) ? mimeTypes[e]:"text/plain";
}
//
//
//
bool H4AT_HTTPHandlerFile::_handle(){ return serveFile(_path); }

bool H4AT_HTTPHandlerFile::handled(H4AsyncClient* r,const std::string& verb,const std::string& path) {
//    H4AT_PRINT2("H4AT_HTTPHandlerFile::handled Vi=%s Vc=%s path=%s\n",verb.data(),_verb.data(),path.data());
    if(verb!=_verb) return false;
    _path=path;
    _r=r; // has to be a better way!
    return _handle();
}

bool H4AT_HTTPHandlerFile::serveFile(const std::string& fn){
    bool rv=false;
    readFileRaw(fn,[&](const char* data,size_t n){
        if(n){
            send(200,mimeType(fn),n,(const uint8_t*) data);
            rv=true;
        } else Serial.printf("HF::SF WTF????????????\n");
    });
    return rv;
}

bool H4AT_HTTPHandler404::_handle() { send(404,"text/plain",0,nullptr); return true; }
//
// SSE
//
H4AT_HTTPHandlerSSE::H4AT_HTTPHandlerSSE(const std::string& url, size_t backlog,uint32_t timeout):
    _timeout(timeout),
    _bs(backlog),
    H4AT_HTTPHandler("GET",url) {
        Serial.printf("SSE HANDLER CTOR 0x%08x backlog=%d timeout=%d\n",this,_bs,_timeout);
        _sniffHeader["last-event-id"]="";
}

H4AT_HTTPHandlerSSE::~H4AT_HTTPHandlerSSE(){
    H4AT_PRINT3("SSE HANDLER DTOR 0x%08x\n",this);
    for(auto &c:_clients) c->_client->_lastSeen=0;
    h4.cancelSingleton(H4AS_SSE_KA_ID);
}

bool H4AT_HTTPHandlerSSE::_handle(){
    auto _c=new H4AT_SSEClient(_r,this);
    _clients.insert(_c);
    _c->lastID=atoi(_sniffHeader["last-event-id"].data());
    Serial.printf("SSE HANDLER saving rqst 0x%08x TO=%d KA RATE=%d _c=0x%08x lid=%d\n",_r,_timeout,(_timeout * 1) / 2,_c,_c->lastID);
    _c->_client->onDisconnect([=](int x){
        _clients.erase(_c);
        H4AT_PRINT2("SSE CLIENT 0x%08x DCX - LEAVES %d clients\n",_c,_clients.size());
        if(!_clients.size()) {
            h4.cancelSingleton(H4AS_SSE_KA_ID);
            _cbConnect(nullptr); // notify all gone
        }
    });

    addHeader("Cache-Control","no-cache");
    H4AT_HTTPHandler::send(200,"text/event-stream",0,nullptr); // explicitly send zero!

    h4.queueFunction([=]{ 
        H4AT_PRINT2("SSE CLIENT 0x%08x set timeout %d\n",_c,_timeout);
        std::string retry("retry: ");
        retry.append(stringFromInt(_timeout)).append("\n\n");
        _c->_client->txdata((const uint8_t *) retry.data(),retry.size());
        _cbConnect(_c);
    });

    h4.every((_timeout * 1) / 2,[=]{ H4AT_PRINT2("H4AT_HTTPHandlerSSE 0x%08x send KA\n",this); send(":"); },nullptr,H4AS_SSE_KA_ID,true); // name it

    H4AT_PRINT2("SSE HANDLER OUT! 0x%08x\n",_r);
    return true;
}

void H4AT_HTTPHandlerSSE::saveBacklog(const std::string& m){
    _backlog[_nextID]=m.c_str();
    if(_backlog.size() > _bs) _backlog.erase(_nextID - _bs);
//    Serial.printf("SSE backlog saved = %d\n",_backlog.size());
}

void H4AT_HTTPHandlerSSE::send(const std::string& message, const std::string& event){
    std::string m=sse(message,event,++_nextID);
    for(auto &c:_clients) c->send(message,event);
    if(_bs) saveBacklog(m.c_str());
}
//
// H4AT_SSEClient
//
void H4AT_SSEClient::send(const std::string& message,  const std::string& event,uint32_t id){
    std::string m=H4AT_HTTPHandlerSSE::sse(message,event,id ? id:_handler->_nextID); // tidy that awful crap
    _client->txdata((const uint8_t *) m.c_str(),m.length());
}

void H4AT_SSEClient::sendEvent(const std::string& event){ _client->txdata((const uint8_t *) event.c_str(),event.length()); }

std::string H4AT_HTTPHandlerSSE::sse(const std::string& message, const std::string& event, size_t id){
    std::string rv;
    if(message[0]!=':'){
        if(id) rv+=_sseSubMsg("id",stringFromInt(id));
        if(event.size()) rv+=_sseSubMsg("event",event);
        std::vector<std::string> data=split(replaceAll(message,"\r",""),"\n");
        for(auto &d:data) rv+=_sseSubMsg("data",d);
    } else rv=message+","+event+","+stringFromInt(id)+"\n";
    rv+="\n";
    return rv;
}

void H4AT_HTTPHandlerSSE::resendBacklog(H4AT_SSEClient* c, size_t lastid){
    for(auto b:_backlog){
        if(b.first > lastid){
            Serial.printf("RESEND BACKLOG %d to %0x08x\n",b.first,c);
            c->sendEvent(b.second);
        }
    }
}