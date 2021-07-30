extern "C" {
    #include "lwip/opt.h"
    #include "lwip/debug.h"
    #include "lwip/stats.h"
    #include "lwip/tcp.h"
}

#include "H4AsyncTCP.h"
#include "raw.h"

static std::unordered_set<H4L_request*> H4ATopenConnections;

static String generateEventMessage(const char *message, const char *event, uint32_t id){
  String ev = "";

  if(id){
    ev += "id: ";
    ev += String(id);
    ev += "\r\n";
  }

  if(event != ""){
    ev += "event: ";
    ev += String(event);
    ev += "\r\n";
  }

  if(message != NULL){
    size_t messageLen = strlen(message);
    char * lineStart = (char *)message;
    char * lineEnd;
    do {
      char * nextN = strchr(lineStart, '\n');
      char * nextR = strchr(lineStart, '\r');
      if(nextN == NULL && nextR == NULL){
        size_t llen = ((char *)message + messageLen) - lineStart;
        char * ldata = (char *)malloc(llen+1);
        if(ldata != NULL){
          memcpy(ldata, lineStart, llen);
          ldata[llen] = 0;
          ev += "data: ";
          ev += ldata;
          ev += "\r\n\r\n";
          free(ldata);
        }
        lineStart = (char *)message + messageLen;
      } else {
        char * nextLine = NULL;
        if(nextN != NULL && nextR != NULL){
          if(nextR < nextN){
            lineEnd = nextR;
            if(nextN == (nextR + 1))
              nextLine = nextN + 1;
            else
              nextLine = nextR + 1;
          } else {
            lineEnd = nextN;
            if(nextR == (nextN + 1))
              nextLine = nextR + 1;
            else
              nextLine = nextN + 1;
          }
        } else if(nextN != NULL){
          lineEnd = nextN;
          nextLine = nextN + 1;
        } else {
          lineEnd = nextR;
          nextLine = nextR + 1;
        }

        size_t llen = lineEnd - lineStart;
        char * ldata = (char *)malloc(llen+1);
        if(ldata != NULL){
          memcpy(ldata, lineStart, llen);
          ldata[llen] = 0;
          ev += "data: ";
          ev += ldata;
          ev += "\r\n";
          free(ldata);
        }
        lineStart = nextLine;
        if(lineStart == ((char *)message + messageLen))
          ev += "\r\n";
      }
    } while(lineStart < ((char *)message + messageLen));
  }

  return ev;
}

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

PMB_NVP_MAP _ext2ct={
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

static struct tcp_pcb *_raw_pcb;

static void _ocGuard(H4L_request *rq,H4_FN_VOID f){
    if(H4ATopenConnections.count(rq)) f();
    else H4AT_PRINT1("INVALID ACTION ON 0x%08x - ALREADY GONE!!!\n",rq);
}

static err_t _raw_accept(void *arg, struct tcp_pcb *newpcb, err_t err){
//    H4AT_PRINT1("RAW _raw_accept a=0x%08x p=0x%08x e=%d\n",arg,newpcb,err);
    auto srv=reinterpret_cast<raw*>(arg);
    tcp_setprio(newpcb, TCP_PRIO_MIN);
    H4ATopenConnections.insert(new H4L_request(newpcb,srv));
    srv->startScavenger();
    return ERR_OK;
}

static void _raw_close(H4L_request *rq){
//    H4AT_PRINT1("RAW _raw_close IN RQ=0x%08x\n",rq);
    _ocGuard(rq,[=]{
        struct tcp_pcb *tpcb=rq->pcb;
//        H4AT_PRINT1("RAW _raw_close IN a=0x%08x t=0x%08x pool=%d PXQ=%d TXQ=%d\n",rq,tpcb,rq->pool.size(),rq->_PXQ.size(),rq->_TXQ.size());
        tcp_arg(tpcb, NULL);
        tcp_sent(tpcb, NULL);
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
  auto rq=reinterpret_cast<H4L_request*>(arg);
  _ocGuard(rq,[=]{
    _raw_close(rq);
    rq->XDISPATCH_V(Error,err,0);
  });
}

static err_t _raw_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err){
//    H4AT_PRINT1("CONNECTION 0x%08x raw_recv PCB=0x%08x PBUF=0x%08x e=%d\n",arg,tpcb,p,err);
    auto rq=reinterpret_cast<H4L_request*>(arg);
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

static err_t _raw_sent(void *arg, struct tcp_pcb *tpcb, u16_t len){
//    H4AT_PRINT1("CONNECTION 0x%08x raw_sent PCB=0x%08x len=%d\n",arg,tpcb,len);
    auto rq = reinterpret_cast<H4L_request*>(arg);
    _ocGuard(rq,[=]{
        h4.queueFunction([=]{ 
            rq->_lastSeen=millis();
            rq->_ackTCP(len);
        });
    });
    return ERR_OK;
}

void raw::begin(){
//    H4AT_PRINT1("BEGIN 0x%08x\n",this);
    _handlers.push_back(new H4L_handlerFile());
    _handlers.push_back(new H4L_handler404());

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

void raw::startScavenger(){
    h4.repeatWhile(
        [=]{ return H4ATopenConnections.size(); },
        H4AS_SCAVENGE_FREQ,
        [=]{
            H4AT_PRINT1("SCAVENGE CONNECTIONS!\n");
            std::vector<H4L_request*> tbd;
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

H4L_request::H4L_request(struct tcp_pcb *newpcb,raw* srv): _server(srv),pcb(newpcb){
//    H4AT_PRINT1("NEW CONNECTION REQUEST 0x%08x PCB=0x%08x\n",this,newpcb);
    tcp_arg(newpcb, this);
    tcp_recv(newpcb, _raw_recv);
    tcp_err(newpcb, _raw_error);
    tcp_sent(newpcb, _raw_sent);
    tcp_nagle_enable(newpcb); // FIX!!!!!!!!!
}

H4L_request::~H4L_request(){
//    H4AT_PRINT1("END CONNECTION REQUEST 0x%08x pool=%d PXQ=%d TXQ=%d\n",this,pool.size(),_PXQ.size(),_TXQ.size());
    if(_cbDisconnect) _cbDisconnect(13); // sort out reason code
}

void H4L_request::_ackTCP(uint16_t len){

    H4AT_PRINT2("<---- AK! %u\n",len);
    //size_t amtToAck=_ackSize(len); fixup later
    /*
    #if H4AT_DEBUG
    auto cq=_TXQ;
    while(!cq.empty()){
        Serial.printf("    ~~~~~~~~ %d\n",cq.front().len);
        cq.pop();
    }
    #endif
    */
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

void H4L_request::_busted(size_t len) {
    _clearFragments();
//    mbxemptyPool();
    //_cbError(H4AT_INPUT_TOO_BIG,len);
}

void H4L_request::_clearFragments() {
    for(auto &f:_fragments) f.clear();
    _fragments.clear();
    _fragments.shrink_to_fit();
}

void H4L_request::_onData(mbx m) {
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

void H4L_request::_runPXQ(){
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

void H4L_request::_TX(){
    H4AT_PRINT4("TX 0x%08x len=%d TCP_SND_BUF=%d managed=%d\n",_PXQ.front().data,_PXQ.front().len,tcp_sndbuf(pcb),_PXQ.front().managed);
    if(_PXQ.front().len>TCP_SND_BUF) {
        mbx m=_PXQ.front();
        _PXQ.pop();
        uint16_t nFrags=m.len/TCP_SND_BUF+((m.len%TCP_SND_BUF) ? 1:0); // so we can mark the final fragment
        int bytesLeft=m.len;
        do{
            size_t toSend=std::min(TCP_SND_BUF,bytesLeft);
            uint8_t* F=(--nFrags) ? (uint8_t*) nFrags:m.data;
            _PXQ.emplace(this,m.data+(m.len - bytesLeft),F,toSend,false);
            H4AT_PRINT3("CHUNK 0x%08x frag=0x%08x len=%d\n",m.data+(m.len - bytesLeft),F,toSend);
            bytesLeft-=toSend;
            _HAL_feedWatchdog();
        } while(bytesLeft); /// set PSH flag on last fragment
    }
    else {
        if(_PXQ.front().len > tcp_sndbuf(pcb)) while(!tcp_sndbuf(pcb)) { 
            _HAL_feedWatchdog(); 
            yield();
        }
        else {
            mbx m=_PXQ.front();
            uint8_t flags=0;;
            flags=TCP_WRITE_FLAG_COPY;
            if((uint32_t) m.frag > 5000 ) flags = TCP_WRITE_FLAG_MORE; // sex this up: e.g. calculate maxium possible N packtes
            H4AT_PRINT2("0x%08x ----> TX data=0x%08x len=%d TXQ=%d PXQ=%d flags=0x%02x frag=0x%08x\n",this,m.data,m.len,_TXQ.size(),_PXQ.size(),flags,m.frag);
            err_t err=tcp_write(pcb,m.data,m.len,flags);// arbitrary, sex it up - maxpaxket / sndbubuf
            if(!err){
                err=tcp_output(pcb);
                if(!err){
                    _PXQ.pop();
                    _TXQ.push(m);
                } else XDISPATCH_V(Error,err,22);
            } else XDISPATCH_V(Error,err,33);
        }
    }
}
//
//
//
void H4L_request::process(uint8_t* data,size_t len){
//    H4AT_DUMP4(data,len);
    std::vector<std::string> rqst=split(std::string((const char*)data,len),"\r\n");
    std::vector<std::string> vparts=split(rqst[0]," ");

    PMB_NVP_MAP _rqHeaders;
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

void H4L_request::txdata(const uint8_t* d,size_t len,bool copy){ 
//    H4AT_PRINT4("H4AsyncClient::txdata 0x%08x l=%d cop=%d\n",d,len,copy);
//    _connGuard([=]{ 
        H4AT_PRINT3("H4AsyncClient::txdata 0x%08x l=%d\n",d,len);
        txdata(mbx(this,(uint8_t*) d,len,copy));
//    }); 
}

void H4L_request::txdata(mbx m){
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

void H4L_handler::send(uint16_t code,const std::string& type,size_t length,const void* body){
    H4AT_PRINT2("CONNECTION 0x%08x send(%d,%s,%d,0x%08x)\n",this,code,type.data(),length,body);

    std::string status=std::string("HTTP/1.1 ")+stringFromInt(code,"%3d ").append(_responseCodeToString(code))+"\r\n";
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

void H4L_handler::sendFile(const std::string& fn){ }//H4L_handlerFile::serveFile(this,fn); }
        
void H4L_handler::sendFileParams(const std::string& fn,PMB_NVP_MAP& params){
    std::string fdata=stringFromFile(fn);
    std::string rdata=replaceParams(fdata,params);
    send(200,mimeType(fn),rdata.size(),rdata.data());
}

std::string H4L_handler::mimeType(const std::string& fn){
    std::string e = fn.substr(fn.rfind('.')+1);
    return _ext2ct.count(e) ? _ext2ct[e]:"text/plain";
}
//
//
//
bool H4L_handlerFile::_handle(){ return serveFile(_path); }

bool H4L_handlerFile::handled(H4L_request* r,const std::string& verb,const std::string& path) {
//    H4AT_PRINT2("H4L_handlerFile::handled Vi=%s Vc=%s path=%s\n",verb.data(),_verb.data(),path.data());
    if(verb!=_verb) return false;
    _path=path;
    _r=r; // has to be a better way!
    return _handle();
}

bool H4L_handlerFile::serveFile(const std::string& fn){
    bool rv=false;
    readFileRaw(fn,[&](const char* data,size_t n){
        if(n){
            send(200,mimeType(fn),n,(const uint8_t*) data);
            rv=true;
        } else Serial.printf("HF::SF WTF????????????\n");
    });
    return rv;
}

bool H4L_handler404::_handle() { send(404,"text/plain",0,nullptr); return true; }
//
// SSE
//
H4L_handlerSSE::H4L_handlerSSE(const std::string& url,uint32_t timeout):
    _timeout(timeout),
    H4L_handler("GET",url) {
//        H4AT_PRINT1("SSE HANDLER CTOR 0x%08x\n",this);
        _sniffHeader["last-event-id"]="";
}

H4L_handlerSSE::~H4L_handlerSSE(){
//    H4AT_PRINT1("SSE HANDLER DTOR 0x%08x\n",this);
    for(auto &c:_clients) c->_client->_lastSeen=0;
    h4.cancelSingleton(H4AS_SSE_KA_ID);
}

bool H4L_handlerSSE::_handle(){
    H4AT_PRINT2("SSE HANDLER saving rqst 0x%08x TO=%d KA RATE=%d\n",_r,_timeout,(_timeout * 3) / 4);

    auto _c=new H4L_SSEClient(_r,this);
    _clients.insert(_c);

    _c->_client->onDisconnect([=](int x){
        _clients.erase(_c);
        H4AT_PRINT2("SSE CLIENT 0x%08x DCX - LEAVES %d clients\n",_c,_clients.size());
        if(!_clients.size()) h4.cancelSingleton(H4AS_SSE_KA_ID);
    });

    addHeader("Cache-Control","no-cache");
    H4L_handler::send(200,"text/event-stream",0,nullptr); // explicitly send zero!

    h4.queueFunction([=]{ 
        H4AT_PRINT2("SSE CLIENT 0x%08x set timeout %d\n",_c,_timeout);
        std::string retry("retry: ");
        retry.append(stringFromInt(_timeout)).append("\n\n");
        _c->_client->txdata((const uint8_t *) retry.data(),retry.size());
        _cbConnect(_c);
    });

    h4.every((_timeout * 1) / 2,[=]{ H4AT_PRINT2("H4L_handlerSSE 0x%08x send KA\n",this); send(":"); },nullptr,H4AS_SSE_KA_ID,true); // name it

    H4AT_PRINT2("SSE HANDLER OUT! 0x%08x\n",_r);
    return true;
}

void H4L_handlerSSE::send(const std::string& message, const std::string& event){
    for(auto &c:_clients) c->send(message,event);
    _nextID++;
}
//
// H4L_SSEClient
//
void H4L_SSEClient::send(const std::string& message,  const std::string& event){
    String m;
    if(message[0]==':') m=":\n\n"; // keep alive
    else m=generateEventMessage(message.data(),event.data(),_handler->_nextID); // tidy that awful crap
    _client->txdata((const uint8_t *) m.c_str(),m.length());
}