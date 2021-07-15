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
#else
    #include<WiFi.h>
#endif

#include "h4async_config.h"
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
        if (H4AT_DEBUG >= I) Serial.printf(std::string(std::string("H4AT:%d: ")+fmt).c_str(),I,args...);
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

using H4AT_MEM_POOL         = std::unordered_set<uint8_t*>;

class mbx {
                void            _create(uint8_t* p);
    public:
        static  H4AT_MEM_POOL   pool;

                bool            managed=false;
                int             len=0;
                uint8_t*        data=nullptr;
                uint8_t*        frag=nullptr;
        mbx(){}
        mbx(uint8_t* p,size_t s,bool copy=true): frag(nullptr),len(s),managed(copy){ _create(p); }
        mbx(uint8_t* p,uint8_t* f,size_t s,bool copy=true): frag(f),len(s),managed(copy){ _create(p); }
        // 
        virtual ~mbx(){} // absolutely do not never nohow free the data pointer here! It must stay alive till it is ACKed
        virtual void            ack();
                void            clear();
        static  void            clear(uint8_t*);
        static  void            emptyPool();
        static  uint8_t*        getMemory(size_t size);
#if H4AT_DEBUG 
        static  void            dump(size_t n=16);
                void            _dump(size_t n=10000);
#endif
};

using H4AT_FN_RXDATA    =std::function<void(const uint8_t*,size_t)>;

using H4AT_FRAGMENTS    =std::vector<mbx>;
using H4AT_MSG_Q        =std::queue<mbx>;
using H4AT_cbConnect    =std::function<void()>;
using H4AT_cbPoll       =std::function<void()>;
using H4AT_cbDisconnect =std::function<void(int8_t)>;
using H4AT_cbError      =std::function<void(int,int)>;
using H4AT_NVP_MAP      =std::map<std::string,std::string>;

struct tcp_pcb;

class H4AsyncTCP {
        friend class mbx;
                tcp_pcb*            _pcb=nullptr;

        static void _tcp_dns_found(const char * name, struct ip_addr * ipaddr, void * arg);
        static err_t _tcp_connected(void* arg, void* tpcb, err_t err);
        static void _tcp_error(void * arg, err_t err);
        static err_t _tcp_poll(void *arg, struct tcp_pcb *tpcb);
        static err_t _tcp_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *pb, err_t err);
        static err_t _tcp_sent(void *arg, struct tcp_pcb *tpcb, uint16_t len);
       /*
        */

#if ASYNC_TCP_SSL_ENABLED
            uint8_t             _fingerprint[SHA1_SIZE];
#endif
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
                H4AT_cbPoll         _cbPoll=nullptr;
        static  H4_INT_MAP          _errorNames;
                H4AT_FRAGMENTS      _fragments;
                bool                _heapLock=false;
                H4AT_MSG_Q          _PXQ; // pendingQ
                H4_TIMER            _pxqRunning=nullptr;
                H4AT_FN_RXDATA      _rxfn=[](const uint8_t* data, size_t len){};
                size_t              _stored=0;

                size_t              _ackSize(size_t len){ return  _URL.secure ? 69+((len>>4)<<4):len; } // that was SOME hack! v. proud
                void                _busted(size_t len);
                void                _chopQ(H4AT_MSG_Q& q);
                void                _clearFragments();
                void                _cnxGuard(H4_FN_VOID f);
                void                _connGuard(H4_FN_VOID f);
                void                _ackTCP(uint16_t len);
//                void                _onDisconnect(int8_t r);
                void                _runPXQ();
                void                _TX();
public:
                void                dump(); // null if no debug
                void                dumpQ(H4AT_MSG_Q& q);
#if H4AT_DEBUG
                size_t              _sigmaTX=0;
#endif
        static  PMB_HEAP_LIMITS     safeHeapLimits;
                H4AT_MSG_Q          _TXQ; // to enable debug dump from higehr powers...
                URL                 _URL;
                void                _causeError(int e,int i){ _cbError(e,i); }
                void                _connect();
                void                _onData(struct pbuf *pb);
                void                _parseURL(const std::string& url);
                void                _releaseHeapLock();
        //
                void                close(bool abort=false);
                bool                connected();
//
        static  std::string         errorstring(int8_t e);
//
                std::string         getRemoteAddressString();
                uint32_t            getRemoteAddress(){ return ip_addr_get_ip4_u32(&_URL.addr); }
                uint16_t            getRemotePort(){ return _URL.port; }
                size_t              getRxTimeout(){}
                size_t              getAckTimeout(){}
                bool                getNoDelay(){}
//                
                bool                isRecvPush(){  }
        static  size_t              maxPacket(){ return (_HAL_maxHeapBlock() - PMB_HEAP_SAFETY) / 2; }
                void                onTCPconnect(H4AT_cbConnect callback){ _cbConnect=callback; }
                void                onTCPdisconnect(H4AT_cbDisconnect callback){ _cbDisconnect=callback; }
                void                onTCPerror(H4AT_cbError callback){ _cbError=callback; }
                void                rx(H4AT_FN_RXDATA f){ _rxfn=f; }
        static  void                safeHeap(size_t cutout,size_t cutin);
                void                setRxTimeout(size_t t){ setRxTimeout(t); }
                void                setAckTimeout(size_t t){ setAckTimeout(t); }
                void                TCPurl(const char* url,const uint8_t* fingerprint=nullptr);
                void                txdata(mbx m);
                void                txdata(const uint8_t* d,size_t len,bool copy=true);
/*
// AsyncClient compatibility
                void                write(const char* d,size_t len,uint8_t flags=0){ write(d,len,flags); }
                void                onData(AcDataHandler cb, void* arg = 0){ _rxfn=cb; }
                void                onConnect(AcConnectHandler cb, void* arg = 0){ _cbConnect=cb; }
*/
    public:
                void                connect(const char* host,uint16_t port);
                void                connect(IPAddress ip,uint16_t port);
                void                connect(const char* url);
                void                connect();
                void                onConnect(H4AT_cbConnect callback){ _cbConnect=callback; }
                void                onDisconnect(H4AT_cbDisconnect callback){ _cbDisconnect=callback; }
                void                onError(H4AT_cbError callback){ _cbError=callback; }
                void                onPoll(H4AT_cbPoll callback){ _cbPoll=callback; }
                void                setNoDelay(bool tf);

        H4AsyncTCP(){
                safeHeapLimits=heapLimits();
        }; // use yer own
//
        template <typename F,typename... Args>
        static  void dispatch(void* p, F f,Args... args) {
            H4AsyncTCP *hp = reinterpret_cast<H4AsyncTCP*>(p);
            H4_FN_VOID vf = std::bind([f, hp](Args... args){ if(hp->*f) (hp->*f)(args...); },std::forward<Args>(args)...);
            h4.queueFunction(vf);
        }
};

#define DISPATCH(arg,f,...) (dispatch(arg,&H4AsyncTCP::_cb##f,__VA_ARGS__))
#define DISPATCH_V(arg,f) dispatch(arg,&H4AsyncTCP::_cb##f)