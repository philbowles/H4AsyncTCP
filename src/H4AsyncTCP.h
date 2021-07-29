/*
MIT License

Copyright (c) 2021 Phil Bowles

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
#pragma once

#define LWIP_INTERNAL

#ifdef ARDUINO_ARCH_ESP8266
    #include<ESP8266WiFi.h>
    #include<LittleFS.h>
    #define HAL_FS LittleFS
#else
    #include<WiFi.h>
    #include<FS.h>
    #include<SPIFFS.h>
    #define HAL_FS SPIFFS
#endif

#include <h4async_config.h>
#include <pmbtools.h>
#include <H4.h>

#include<functional>
#include<string>
#include<vector>
#include<map>
#include<queue>
#include<unordered_set>

extern "C" {
    #include "lwip/init.h"
    #include "lwip/err.h"
    #include "lwip/pbuf.h"
};

enum {
    H4AT_ERR_OK,
    H4AT_ERR_ALREADY_CONNECTED,
    H4AT_ERR_DNS_FAIL,
    H4AT_ERR_DNS_NF,
    H4AT_ERR_UNKNOWN,
    H4AT_HEAP_LIMITER_ON,
    H4AT_HEAP_LIMITER_OFF,
    H4AT_HEAP_LIMITER_LOST,
    H4AT_INPUT_TOO_BIG,
};

#if H4AT_DEBUG
    template<int I, typename... Args>
    void H4AT_PRINT(const char* fmt, Args... args) {
        #ifdef ARDUINO_ARCH_ESP32
        if (H4AT_DEBUG >= I) Serial.printf(std::string(std::string("H4AT:%d: H=%u M=%u S=%u ")+fmt).c_str(),I,_HAL_freeHeap(),_HAL_maxHeapBlock(),uxTaskGetStackHighWaterMark(NULL),args...);
        #else
        if (H4AT_DEBUG >= I) Serial.printf(std::string(std::string("H4AT:%d: H=%u M=%u ")+fmt).c_str(),I,_HAL_freeHeap(),_HAL_maxHeapBlock(),args...);
        #endif
    }
    #define H4AT_PRINT1(...) H4AT_PRINT<1>(__VA_ARGS__)
    #define H4AT_PRINT2(...) H4AT_PRINT<2>(__VA_ARGS__)
    #define H4AT_PRINT3(...) H4AT_PRINT<3>(__VA_ARGS__)
    #define H4AT_PRINT4(...) H4AT_PRINT<4>(__VA_ARGS__)

    template<int I>
    void H4AT_dump(const uint8_t* p, size_t len) { if (H4AT_DEBUG >= I) dumphex(p,len); }
    #define H4AT_DUMP2(p,l) H4AT_dump<2>((p),l)
    #define H4AT_DUMP3(p,l) H4AT_dump<3>((p),l)
    #define H4AT_DUMP4(p,l) H4AT_dump<4>((p),l)
#else
    #define H4AT_PRINT1(...)
    #define H4AT_PRINT2(...)
    #define H4AT_PRINT3(...)
    #define H4AT_PRINT4(...)

    #define H4AT_DUMP2(...)
    #define H4AT_DUMP3(...)
    #define H4AT_DUMP4(...)
#endif

class H4AsyncClient;
class H4L_request;

class mbx {
                H4L_request*  _c;
                void            _create(H4L_request*  c,uint8_t* p);
    public:
                bool            managed=false;
                int             len=0;
                uint8_t*        data=nullptr;
                uint8_t*        frag=nullptr;
                uint8_t         flags=0;
        mbx(){}
        mbx(H4L_request*  c, uint8_t* p,size_t s,bool copy=true,uint8_t j=0): frag(nullptr),len(s),managed(copy),flags(j){ _create(c,p); }
        mbx(H4L_request*  c, uint8_t* p,uint8_t* f,size_t s,bool copy=true,uint8_t j=0): frag(f),len(s),managed(copy),flags(j){ _create(c,p); }
        // 
         ~mbx(){} // absolutely do not never nohow free the data pointer here! It must stay alive till it is ACKed
          void                 ack();
          void                 clear();
        static  void           clear(H4L_request* c, uint8_t*);
        static uint8_t*        getMemory(H4L_request* c,size_t size);
};
using H4AT_MEM_POOL     = std::unordered_set<uint8_t*>;

using H4AT_FN_RXDATA    =std::function<void(const uint8_t*,size_t)>;

using H4AT_FRAGMENTS    =std::vector<mbx>;
using H4AT_MSG_Q        =std::queue<mbx>;
using H4AT_cbConnect    =std::function<void()>;
using H4AT_cbPoll       =std::function<err_t()>;
using H4AT_cbDisconnect =std::function<void(int8_t)>;
using H4AT_cbError      =std::function<void(int,int)>;
using H4AT_NVP_MAP      =std::map<std::string,std::string>;

struct tcp_pcb;

using H4AT_CNXLIST      = std::unordered_set<H4AsyncClient*>;

class H4AsyncClient {
        friend class mbx;
            H4AT_MEM_POOL       pool;
        
#if ASYNC_TCP_SSL_ENABLED
            uint8_t             _fingerprint[SHA1_SIZE];
        
#endif
    public:
        static  H4AT_CNXLIST    _openConnections;
                struct  URL {
                    std::string     scheme;
                    std::string     host;
                    int             port;
                    std::string     path;
                    std::string     query;
                    std::string     fragment;
                    bool            secure=0;
                    ip_addr_t       addr;
                };
                H4AT_cbConnect      _cbConnect=nullptr;
                H4AT_cbDisconnect   _cbDisconnect=nullptr;
                H4AT_cbError        _cbError=[](int e,int i){};
                H4AT_cbPoll         _cbPoll=[]{ return ERR_OK; };
                bool                _closeConnection=true;
        static  H4_INT_MAP          _errorNames;
                H4AT_FRAGMENTS      _fragments;
                bool                _heapLock=false;
                uint32_t            _lastSeen=0;
                H4_TIMER            _pxqRunning=nullptr;
                H4AT_FN_RXDATA      _rxfn=[](const uint8_t* data, size_t len){};
                size_t              _stored=0;

                size_t              _ackSize(size_t len){ return  _URL.secure ? 69+((len>>4)<<4):len; } // that was SOME hack! v. proud
                void                _busted(size_t len);
                void                _chopQ(H4AT_MSG_Q& q);
                void                _clearFragments();
                void                _cnxGuard(H4_FN_VOID f);
                void                _connGuard(H4_FN_VOID f);
                err_t               _ackTCP(uint16_t len);
                void                _runPXQ();
                void                _TX();

                tcp_pcb*            _pcb=nullptr;
                void                dumpQ(H4AT_MSG_Q& q);
/*
#if H4AT_DEBUG

void mbxdumpPool(size_t n){ 
    Serial.printf("\n%d Pool Blox\n",pool.size());
    for(auto const& p:pool){
        dumphex(p,n); // Danger, Will Robinson!!!
        Serial.println();
    }
    Serial.println();
}
void mbxemptyPool(){
    H4AT_PRINT2("MBX EMPTY POOL len=%d FH=%u MXBLK=%u\n",pool.size(),_HAL_freeHeap(),_HAL_maxHeapBlock());
    for(auto const& p:pool) mbx::clear(this,p);
    pool.clear();
}
#endif
*/

        static  err_t               _tcp_poll(void *arg, struct tcp_pcb *tpcb);
        static  void                _tcp_error(void * arg, err_t err);
        static  err_t               _tcp_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *pb, err_t err);
        static  err_t               _tcp_sent(void *arg, struct tcp_pcb *tpcb, uint16_t len);
        static  void                _tcp_dns_found(const char * name, struct ip_addr * ipaddr, void * arg);
        static  err_t               _tcp_connected(void* arg, void* tpcb, err_t err);

        static  PMB_HEAP_LIMITS     safeHeapLimits;
                H4AT_MSG_Q          _PXQ; // pendingQ
                H4AT_MSG_Q          _TXQ; // to enable debug dump from higehr powers...
                URL                 _URL;
                void                _connect();
//                err_t               _onData(struct tcp_pcb *tpcb,struct pbuf *pb);
                void                _onData(mbx m);
                void                _parseURL(const std::string& url);
                void                _releaseHeapLock();
        //
                void                close();
                bool                connected();
//
        static  std::string         errorstring(int e);
//
                uint32_t            remoteAddress();
                IPAddress           remoteIP();
                std::string         remoteIPstring();
                uint16_t            remotePort();
                
                uint32_t            localAddress();
                IPAddress           localIP();
                std::string         localIPstring();
                uint16_t            localPort();
//                
        static  size_t              maxPacket(){ return (_HAL_maxHeapBlock() - PMB_HEAP_SAFETY) / 2; }
                void                onTCPconnect(H4AT_cbConnect callback){ _cbConnect=callback; }
                void                onTCPdisconnect(H4AT_cbDisconnect callback){ _cbDisconnect=callback; }
                void                onTCPerror(H4AT_cbError callback){ _cbError=callback; }
                void                rx(H4AT_FN_RXDATA f){ _rxfn=f; }
        static  void                safeHeap(size_t cutout,size_t cutin);
                void                setRxTimeout(size_t t){ H4AT_PRINT1("Some plank called setRxTimeout(%d)\n",t); }
                void                setAckTimeout(size_t t){ H4AT_PRINT1("Some plank called setAckTimeout(%d)\n",t); }
                void                TCPurl(const char* url,const uint8_t* fingerprint=nullptr);
                void                txdata(mbx m);
                void                txdata(const uint8_t* d,size_t len,bool copy=true);
    public:
                void                connect(const char* host,uint16_t port);
                void                connect(IPAddress ip,uint16_t port);
                void                connect(const char* url);
                void                connect();
                void                dump();
                void                onConnect(H4AT_cbConnect callback){ _cbConnect=callback; }
                void                onDisconnect(H4AT_cbDisconnect callback){ _cbDisconnect=callback; }
                void                onError(H4AT_cbError callback){ _cbError=callback; }
                void                onPoll(H4AT_cbPoll callback){ _cbPoll=callback; }
                void                setNoDelay(bool tf);

        H4AsyncClient(tcp_pcb* p=0);
        ~H4AsyncClient();
//
        template <typename F,typename... Args>
        static  void dispatch(void* p, F f,Args... args) {
            H4AsyncClient *hp = reinterpret_cast<H4AsyncClient*>(p);
            H4_FN_VOID vf = std::bind([f, hp](Args... args){ 
                if(hp->*f) (hp->*f)(args...);
//                else Serial.printf(" 0x%08x OOOHHHHHHHHHHH FFFFFFFUCK\n",hp);
            },std::forward<Args>(args)...);
            h4.queueFunction(vf);
        }
};
class H4L_request;

#define DISPATCH(arg,f,...) ({ /* Serial.printf("SPATCH "#f"--\n"); */ dispatch(arg,&H4AsyncClient::_cb##f,__VA_ARGS__); } )
#define DISPATCH_V(arg,f) { /* Serial.printf("V "#f"--\n"); */ dispatch(arg,&H4AsyncClient::_cb##f); }

using H4AS_HEADER       = std::pair<std::string,std::string>;
using H4AS_HEADERS      = std::vector<H4AS_HEADER>;
using H4AS_FN_CLIENT    = std::function<void(tcp_pcb*)>;

class H4AsyncServer {
    protected:
                H4AS_FN_CLIENT  _client_cb=nullptr;
                uint16_t        _port;
                bool            _noDelay=false;
                tcp_pcb*        _pcb=nullptr;
    public:
        static  H4AT_MSG_Q      _IPQ;

        H4AsyncServer(uint16_t _port=80);
        ~H4AsyncServer();

        virtual void        begin();
                void        dump();
        virtual void        end();
                bool        getNoDelay();
                void        onClient(H4AS_FN_CLIENT cb);
        virtual void        reset(){}
                void        setNoDelay(bool nodelay);
                uint8_t     status();

        static  err_t   _s_accept(void *arg, tcp_pcb* newpcb, err_t err);
};