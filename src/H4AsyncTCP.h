/*
Creative Commons: Attribution-NonCommercial-ShareAlike 4.0 International (CC BY-NC-SA 4.0)
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
#include <H4Tools.h>
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
    H4AT_ERR_DNS_FAIL,
    H4AT_ERR_DNS_NF,
    H4AT_ERR_UNKNOWN,
    H4AT_HEAP_LIMITER_ON,
    H4AT_HEAP_LIMITER_OFF,
    H4AT_HEAP_LIMITER_LOST,
    H4AT_INPUT_TOO_BIG,
    H4AT_INPUT_PILEUP,
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
    #define H4AT_DUMP1(p,l) H4AT_dump<1>((p),l)
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

class mbx;
class H4AsyncWebServer;
class H4AT_HTTPHandler;
class H4AsyncClient;
class H4AS_HTTPRequest;

using H4AT_MEM_POOL     =std::unordered_set<uint8_t*>;
using H4AT_NVP_MAP      =std::unordered_map<std::string,std::string>;
using H4AT_FRAGMENTS    =std::queue<mbx>;
using H4AT_MSG_Q        =std::queue<mbx>;
using H4AS_RQ_HANDLER   =std::function<void(H4AT_HTTPHandler*)>;
using H4AT_FN_CB_ERROR  =std::function<void(int,int)>;
using H4AT_FN_RXDATA    =std::function<void(const uint8_t* data, size_t len)>;

void _raw_close(H4AsyncClient *rq,bool abort=false);
void _raw_error(void *arg, err_t err);

class mbx {
//                void            _create(uint8_t* p);
    public:
    static      H4AT_MEM_POOL   pool;
                bool            managed;
                int             len=0;
                uint8_t*        data=nullptr;
                uint8_t         flags=0;
        mbx(){}
        mbx(uint8_t* p,size_t s,bool copy=true,uint8_t f=0);
        // 
         ~mbx(){} // absolutely do not never nohow free the data pointer here! It must stay alive till it is ACKed

//                void            ack();
                void            clear();
        static  void            clear(uint8_t*);
        static  uint8_t*        getMemory(size_t size);
};

class H4AsyncClient {
        friend class mbx;
                void            _busted(size_t len);
                void            _cnxGuard(H4_FN_VOID f);
                void            _clearFragments();
                void            _parseURL(const std::string& url);
                void            _runPXQ();
                void            _TX();

    protected:
                H4AT_FN_RXDATA      _rxfn=[](const uint8_t* data, size_t len){};
    public:
                bool                _inIPQ=false;
    static      std::unordered_set<H4AsyncClient*> openConnections;
                struct  URL {
                    std::string     scheme;
                    std::string     host;
                    int             port;
                    std::string     path;
                    std::string     query;
                    std::string     fragment;
                    bool            secure=0;
                    ip_addr_t       addr;
                } _URL;

                H4_FN_VOID          _cbConnect;
                H4_FN_VOID          _cbDisconnect;
                H4AT_FN_CB_ERROR    _cbError;
                size_t              _cnxTimeout;
        static  H4_INT_MAP          _errorNames;
                H4AT_FRAGMENTS      _fragments;
                bool                _heapLock=false;
                uint32_t            _lastSeen=0;
                struct tcp_pcb      *pcb;
                H4AT_MSG_Q          _PXQ;
                H4_TIMER            _pxqRunning=nullptr;
                size_t              _stored=0;

        H4AsyncClient(tcp_pcb* p=0,size_t timeout=H4AT_CNX_TIMEOUT);
        virtual ~H4AsyncClient();
                void                close();
                void                connect(const char* host,uint16_t port);
                void                connect(IPAddress ip,uint16_t port);
                void                connect(const char* url);
                void                connect();
                bool                connected();
        static  std::string         errorstring(int e);
                uint32_t            localAddress();
                IPAddress           localIP();
                std::string         localIPstring();
                uint16_t            localPort();

                uint32_t            remoteAddress();
                IPAddress           remoteIP();
                std::string         remoteIPstring();
                uint16_t            remotePort();
                
                void                onConnect(H4_FN_VOID cb){ _cbConnect=cb; }
                void                onDisconnect(H4_FN_VOID cb){ _cbDisconnect=cb; }
                void                onError(H4AT_FN_CB_ERROR cb){ _cbError=cb; }
                void                onRX(H4AT_FN_RXDATA f){ _rxfn=f; }
                void                TCPurl(const char* url,const uint8_t* fingerprint=nullptr);
                void                txdata(mbx m);
                void                txdata(const uint8_t* d,size_t len,bool copy=true);

                void                dump();
                void                dumpQ(H4AT_MSG_Q& q);
// syscalls - just don't...
                void                _connect();
//                void                _onData(mbx m);
                void                _runIPQ();
        static  void                scavenge();
};

class H4AsyncServer {
    protected:
            struct tcp_pcb *        _raw_pcb;
            uint16_t                _port;

    public:
        H4AsyncServer(uint16_t port): _port(port){}

        virtual void        begin();
 
                void        newConnection(struct tcp_pcb *p,H4AsyncClient* c);
        virtual void        reset(){}
                void        startScavenger();
// don't call!  
        virtual void        _incoming(tcp_pcb* p)=0;
};