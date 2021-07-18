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
        if (H4AT_DEBUG >= I) Serial.printf(std::string(std::string("H4AT:%d: H=%u M=%u ")+fmt).c_str(),I,_HAL_freeHeap(),_HAL_maxHeapBlock(),args...);
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
using H4AT_cbPoll       =std::function<err_t()>;
using H4AT_cbDisconnect =std::function<void(int8_t)>;
using H4AT_cbError      =std::function<void(int,int)>;
using H4AT_NVP_MAP      =std::map<std::string,std::string>;

/*

remove this shit
*/
class H4AsyncClient;

typedef std::function<void(void*, H4AsyncClient*)> AcConnectHandler;
typedef std::function<void(void*, H4AsyncClient*, size_t len, uint32_t time)> AcAckHandler;
typedef std::function<void(void*, H4AsyncClient*, err_t error)> AcErrorHandler;
typedef std::function<void(void*, H4AsyncClient*, void *data, size_t len)> AcDataHandler;
typedef std::function<void(void*, H4AsyncClient*, struct pbuf *pb)> AcPacketHandler;
typedef std::function<void(void*, H4AsyncClient*, uint32_t time)> AcTimeoutHandler;
typedef std::function<void(void*, size_t event)> AsNotifyHandler;

struct tcp_pcb;

//
//
//
class H4AsyncClient {
        friend class mbx;
        
#if ASYNC_TCP_SSL_ENABLED
            uint8_t             _fingerprint[SHA1_SIZE];
#endif
    public:
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
                void                _runPXQ();

                void                _TX();

        static  err_t               _tcp_connected(void* arg, void* tpcb, err_t err);

                tcp_pcb*            _pcb=nullptr;
                void                dump(); // null if no debug
                void                dumpQ(H4AT_MSG_Q& q);
#if H4AT_DEBUG
                size_t              _sigmaTX=0;
#endif

        static  err_t               _tcp_poll(void *arg, struct tcp_pcb *tpcb);
        static  void                _tcp_error(void * arg, err_t err);
        static  err_t               _tcp_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *pb, err_t err);
        static  err_t               _tcp_sent(void *arg, struct tcp_pcb *tpcb, uint16_t len);
        static  void                _tcp_dns_found(const char * name, struct ip_addr * ipaddr, void * arg);

        static  PMB_HEAP_LIMITS     safeHeapLimits;
                H4AT_MSG_Q          _PXQ; // pendingQ
                H4AT_MSG_Q          _TXQ; // to enable debug dump from higehr powers...
                URL                 _URL;
                void                _connect();
                void                _onData(struct tcp_pcb *tpcb,struct pbuf *pb);
                void                _parseURL(const std::string& url);
                void                _releaseHeapLock();
        //
                void                close();
                bool                connected();
//
        static  std::string         errorstring(int e);
//
                uint32_t            getRemoteAddress(){ return ip_addr_get_ip4_u32(&_URL.addr); }
                uint16_t            getRemotePort(){ return _URL.port; }
                uint32_t            getLocalAddress(){ return 666; }
                uint16_t            getLocalPort(){ return 42; };

                IPAddress           remoteIP(){ return IPAddress( getRemoteAddress()); }
                uint16_t            remotePort(){ return _URL.port; }

                IPAddress           localIP(){ return WiFi.localIP(); }
                uint16_t            localPort(){ return 42; };
//
                std::string         remoteIPString();
                bool                free();
                size_t              getRxTimeout(){}
                size_t              getAckTimeout(){}
                bool                getNoDelay(){}
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
//class H4L_handler;
class H4L_request;

#define DISPATCH(arg,f,...) ({ /* Serial.printf("SPATCH "#f"--\n"); */ dispatch(arg,&H4AsyncClient::_cb##f,__VA_ARGS__); } )
#define DISPATCH_V(arg,f) { /* Serial.printf("V "#f"--\n"); */ dispatch(arg,&H4AsyncClient::_cb##f); }

using H4AS_HEADER       = std::pair<std::string,std::string>;
using H4AS_HEADERS      = std::vector<H4AS_HEADER>;
using H4AS_NVP_MAP      = std::unordered_map<std::string,std::string>;
using H4AS_FN_HANDLER   = std::function<void(tcp_pcb*)>;
using H4AS_RQ_HANDLER   = std::function<void(H4L_request*)>;

//
//   andlaz
//
class H4L_handler {
    protected:
        std::string     _verb;
        std::string     _path;
        H4AS_RQ_HANDLER _f;

        virtual bool _handle(H4L_request* r){
            _f(r);
            return true;
        }
    public:
        H4L_handler(const char* verb,const char* path,H4AS_RQ_HANDLER f=nullptr): _verb(verb),_path(path),_f(f){}

        virtual bool handled(H4L_request* r,std::string verb,std::string path){
            if(!(verb==_verb && path==_path)) return false; //////////////////////////////// STARTSWITH!
            return _handle(r);
        };
};

class H4L_handlerFile: public H4L_handler {
    protected:
        virtual bool _handle(H4L_request* r) override;
    public:
        H4L_handlerFile(): H4L_handler("GET","*"){}

        virtual bool handled(H4L_request* r,std::string verb,std::string path) override;
        static bool serveFile(H4L_request* r,std::string fn);
};

class H4L_handler404: public H4L_handler {
    protected:
        virtual bool _handle(H4L_request* r) override;
    public:
        H4L_handler404(): H4L_handler("",""){}

        virtual bool handled(H4L_request* r,std::string verb,std::string path) override { return _handle(r); }
};

class H4AsyncServer {
    protected:
        H4AS_FN_HANDLER _client_cb=nullptr;
        uint16_t        _port;
        bool            _noDelay=false;
        tcp_pcb*        _pcb=nullptr;
    public:
        H4AsyncServer(uint16_t _port=80);
        ~H4AsyncServer();

                void        onClient(H4AS_FN_HANDLER cb){ _client_cb=cb; }
        virtual void        begin();
                void        end();
                void        setNoDelay(bool nodelay);
                bool        getNoDelay();
                uint8_t     status();

        static  err_t   _s_accept(void *arg, tcp_pcb* newpcb, err_t err);
    
    template<typename T>
    static void hookInClient(tcp_pcb* tpcb);
};

class H4Lightning: public H4AsyncServer {
    public:
        static  H4AS_NVP_MAP    _ext2ct;
        static  std::vector<H4L_handler*> handlers;

        H4Lightning(uint16_t _port);
        
        void begin() override;

        static void on(const char* verb,const char* path,H4AS_RQ_HANDLER f){ handlers.push_back(new H4L_handler{verb,path,f}); }
};
//
//   REQUEST
//
class H4L_request: public H4AsyncClient {
    public:            
        H4L_request(tcp_pcb* p);
        ~H4L_request(){}

        void send(uint16_t code,std::string type,size_t length,const void* body);
        void sendFile(std::string fn);
};