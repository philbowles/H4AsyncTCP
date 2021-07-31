#include<Arduino.h>
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

struct tcp_pcb;

class H4AsyncWebServer;
class H4AT_HTTPHandler;
class H4AsyncClient;

class mbx {
                H4AsyncClient*  _c;
                void            _create(H4AsyncClient*  c,uint8_t* p);
    public:
                bool            managed=false;
                int             len=0;
                uint8_t*        data=nullptr;
                uint8_t*        frag=nullptr;
                uint8_t         flags=0;
        mbx(){}
        mbx(H4AsyncClient*  c, uint8_t* p,size_t s,bool copy=true,uint8_t j=0): frag(nullptr),len(s),managed(copy),flags(j){ _create(c,p); }
        mbx(H4AsyncClient*  c, uint8_t* p,uint8_t* f,size_t s,bool copy=true,uint8_t j=0): frag(f),len(s),managed(copy),flags(j){ _create(c,p); }
        // 
         ~mbx(){} // absolutely do not never nohow free the data pointer here! It must stay alive till it is ACKed
                void            ack();
                void            clear();
        static  void            clear(H4AsyncClient* c, uint8_t*);
        static  uint8_t*        getMemory(H4AsyncClient* c,size_t size);
};

using H4AT_MEM_POOL     =std::unordered_set<uint8_t*>;
using H4AT_FRAGMENTS    =std::vector<mbx>;
using H4AT_MSG_Q        =std::queue<mbx>;
using H4AT_NVP_MAP      =std::unordered_map<std::string,std::string>;
using H4AS_RQ_HANDLER   =std::function<void(H4AT_HTTPHandler*)>;
using H4AT_FN_CB_DCX    =std::function<void(int)>;
using H4AT_FN_CB_ERROR  =std::function<void(int,int)>;

class H4AsyncClient {
        friend class mbx;
                void                _busted(size_t len);
                void                _clearFragments();
                void                _runPXQ();
                void                _TX();
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

                H4_FN_VOID          _cbConnect;
                H4AT_FN_CB_DCX      _cbDisconnect;
                H4AT_FN_CB_ERROR    _cbError;
        static  H4_INT_MAP          _errorNames;
                H4AT_FRAGMENTS      _fragments;
                bool                _heapLock=false;
                uint32_t            _lastSeen=0;
                struct tcp_pcb      *pcb;
                H4AT_MEM_POOL       pool;
                H4AT_MSG_Q          _PXQ; // pendingQ
                H4_TIMER            _pxqRunning=nullptr;
// process                H4AT_FN_RXDATA      _rxfn=[](const uint8_t* data, size_t len){};
                H4AsyncWebServer*   _server=nullptr;
                size_t              _stored=0;

        H4AsyncClient(tcp_pcb* p=0,H4AsyncWebServer* rp=nullptr);
        ~H4AsyncClient();
                void                close();
                void                connect(const char* host,uint16_t port);
                void                connect(IPAddress ip,uint16_t port);
                void                connect(const char* url);
                void                connect();
        static  std::string         errorstring(int e);
                uint32_t            localAddress();
                IPAddress           localIP();
                std::string         localIPstring();
                uint16_t            localPort();

                uint32_t            remoteAddress();
                IPAddress           remoteIP();
                std::string         remoteIPstring();
                uint16_t            remotePort();
                
                void                onConnect(H4_FN_VOID callback){ _cbConnect=callback; }
                void                onDisconnect(H4AT_FN_CB_DCX cb){ _cbDisconnect=cb; }
                void                onError(H4AT_FN_CB_ERROR cb){ _cbError=cb; }
                void                txdata(mbx m);
                void                txdata(const uint8_t* d,size_t len,bool copy=true);

                void                dump();
                void                dumpQ(H4AT_MSG_Q& q);

                void                process(uint8_t* data,size_t len);
// syscalls - just don't...
                 void                _onData(mbx m);

       template <typename F,typename... Args>
        void dispatch(F f,Args... args) {
            H4_FN_VOID vf = std::bind([=](Args... args){ 
                if(this->*f) (this->*f)(args...);
                else Serial.printf(" 0x%08x OOOHHHHHHHHHHH FFFFFFFUCK\n",this);
            },std::forward<Args>(args)...);
            h4.queueFunction(vf);
        }
};

#define XDISPATCH_V(f,...) dispatch(&H4AsyncClient::_cb##f,__VA_ARGS__);
#define XDISPATCH(f) dispatch(&H4AsyncClient::_cb##f)

class H4AT_HTTPHandler {
    protected:
        std::string     _verb;
        std::string     _path;

        H4AS_RQ_HANDLER _f;
        H4AT_NVP_MAP     _headers;
        H4AsyncClient*    _r;

        virtual bool _handle(){
            if(_f) _f(this);
            else Serial.printf("AAAAAAAARGH NO HANDLER FUNCTION %s %s\n",_verb.data(),_path.data());
            return true;
        }

        static  std::string     mimeType(const std::string& fn);
        static  H4_INT_MAP      _responseCodes;

    public:
        static  H4AT_NVP_MAP    mimeTypes;
                H4AT_NVP_MAP    _sniffHeader;

        H4AT_HTTPHandler(const std::string& verb,const std::string& path,H4AS_RQ_HANDLER f=nullptr): _verb(verb),_path(path),_f(f){}

        virtual bool handled(H4AsyncClient* r,const std::string& verb,const std::string& path){
            // cleardown any previous from reqused connections
            _headers.clear();
            if(!(verb==_verb && path==_path)) return false; //////////////////////////////// STARTSWITH!
            _r=r;
            return _handle();
        };

                void        addHeader(const std::string& name,const std::string& value){ _headers[name]=value; }
        virtual void        send(uint16_t code,const std::string& type,size_t length=0,const void* body=nullptr);
        virtual void        sendFile(const std::string& fn);
        virtual void        sendFileParams(const std::string& fn,H4AT_NVP_MAP& params);
        virtual void        sendstring(const std::string& type,const std::string& data){ send(200,type,data.size(),(const void*) data.data()); }
};

class H4AT_HTTPHandlerFile: public H4AT_HTTPHandler {
    protected:
        virtual bool _handle() override;
    public:
        H4AT_HTTPHandlerFile(): H4AT_HTTPHandler("GET","*"){}

        virtual bool handled(H4AsyncClient* r,const std::string& verb,const std::string& path) override;
                bool serveFile(const std::string& fn);
};

class H4AT_HTTPHandler404: public H4AT_HTTPHandler {
    protected:
        virtual bool _handle() override;
    public:
        H4AT_HTTPHandler404(): H4AT_HTTPHandler("",""){}

        virtual bool handled(H4AsyncClient* r,const std::string& verb,const std::string& path) override { return _handle(); }
};

class H4AT_HTTPHandlerSSE;

class H4AT_SSEClient{
    public:
            H4AsyncClient*    _client;        
            H4AT_HTTPHandlerSSE* _handler;
            size_t          lastID;

        H4AT_SSEClient(H4AsyncClient* c,H4AT_HTTPHandlerSSE* h):_client(c),_handler(h){}
        ~H4AT_SSEClient(){}
                uint32_t            lastId(){ return lastID; }
                void                send(const std::string& message, const std::string& event,uint32_t id=0);
                void                sendEvent(const std::string& event);
};

using H4AS_EVT_HANDLER   = std::function<void(H4AT_SSEClient*)>;

class H4AT_HTTPHandlerSSE: public H4AT_HTTPHandler {
            std::unordered_set<H4AT_SSEClient*>  _clients;
            H4AS_EVT_HANDLER                    _cbConnect;
            uint32_t                            _timeout;
            std::map<size_t,std::string>        _backlog;
            size_t                              _bs;
    protected:
        virtual bool                _handle() override;
        static  std::string inline  _sseSubMsg(const std::string& type, const std::string& content){ return type+":"+content+"\n"; }
    public:
            size_t                              _nextID=0;

        H4AT_HTTPHandlerSSE(const std::string& url,size_t backlog=0,uint32_t timeout=H4AS_SCAVENGE_FREQ);
        ~H4AT_HTTPHandlerSSE();

                size_t              count(){ return _clients.size(); }
        virtual void                saveBacklog(const std::string& msg);
                void                onConnect(H4AS_EVT_HANDLER cb){ _cbConnect=cb; }
        virtual void                resendBacklog(H4AT_SSEClient*, size_t lastid);
                void                send(const std::string& message, const std::string& event="");
        static  std::string         sse(const std::string& message=":", const std::string& event="", size_t id=0);
};


using H4AS_HANDLER_LIST = std::vector<H4AT_HTTPHandler*>;


class H4AsyncServer {
    protected:
            uint16_t                _port;
    public:
            H4AS_HANDLER_LIST       _handlers;

        H4AsyncServer(uint16_t port): _port(port){}

                void        begin();
        virtual void        startScavenger(){}
};

class H4AsyncWebServer: public H4AsyncServer {
            size_t                  _cacheAge;
    public:
            H4AS_HANDLER_LIST       _handlers;

        H4AsyncWebServer(uint16_t port,size_t cacheAge=86400): _cacheAge(cacheAge),H4AsyncServer(port){}

            void addHandler(H4AT_HTTPHandler* h){ _handlers.push_back(h); }
            void begin();
            void on(const std::string& verb,const std::string& path,H4AS_RQ_HANDLER f){ _handlers.push_back(new H4AT_HTTPHandler{verb,path,f}); }
            void startScavenger() override;
};
/*
template<typename T,typename R>
static err_t _h4tcp_accept(void *arg, struct tcp_pcb *newpcb, err_t err);
*/