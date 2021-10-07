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
#pragma once
#include <h4async_config.h>

#include<Arduino.h>

#define LWIP_INTERNAL
extern "C"{
  #include "lwip/err.h"
}

#ifdef ARDUINO_ARCH_ESP8266
    #include<ESP8266WiFi.h>
#else
    #include<WiFi.h>
#endif

#include <H4Tools.h>
#include <H4.h>

#include<functional>
#include<string>
#include<vector>
#include<map>
#include<queue>
#include<unordered_set>

enum {
    H4AT_ERR_DNS_FAIL,
    H4AT_ERR_DNS_NF,
    H4AT_ERR_UNKNOWN,
    H4AT_HEAP_LIMITER_ON,
    H4AT_HEAP_LIMITER_OFF,
    H4AT_HEAP_LIMITER_LOST,
    H4AT_INPUT_TOO_BIG,
    H4AT_CLOSING,
    H4AT_OUTPUT_TOO_BIG
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

class H4AsyncWebServer;
class H4AT_HTTPHandler;
class H4AsyncClient;
class H4AS_HTTPRequest;

using H4AT_NVP_MAP      =std::unordered_map<std::string,std::string>;
using H4AS_RQ_HANDLER   =std::function<void(H4AT_HTTPHandler*)>;
using H4AT_FN_ERROR     =std::function<bool(int,int)>;
using H4AT_FN_RXDATA    =std::function<void(const uint8_t* data, size_t len)>;

class H4AsyncClient {
                void                _parseURL(const std::string& url);
    protected:
                H4AT_FN_RXDATA      _rxfn=[](const uint8_t* data,size_t len){ Serial.printf("RXFN SAFETY\n"); dumphex(data,len); };
    public:
        static  std::unordered_set<H4AsyncClient*> openConnections;
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

                uint8_t*            _bpp=nullptr;
                H4_FN_VOID          _cbConnect;
                H4_FN_VOID          _cbDisconnect;
                H4AT_FN_ERROR       _cbError=[](int e,int i){ return true; }; // return false to avoid auto-shutdown
                bool                _closing=false;
//                size_t              _cnxTimeout;
        static  H4_INT_MAP          _errorNames;
//                size_t              _heapLO;
//                size_t              _heapHI;
                uint32_t            _lastSeen=0;
                bool                _nagle=false;
                struct tcp_pcb      *pcb;
                size_t              _stored=0;

        H4AsyncClient(tcp_pcb* p=0);
        virtual ~H4AsyncClient(){ H4AT_PRINT2("H4AsyncClient DTOR %p pcb=%p _bpp=%p\n",this,pcb,_bpp); }
                void                close(){ _shutdown(); }
                void                connect(const std::string& host,uint16_t port);
                void                connect(IPAddress ip,uint16_t port);
                void                connect(const std::string& url);
                bool                connected();
                //
                //void                dump();
                //
        static  std::string         errorstring(int e);
//                void                keepAlive(){ _lastSeen=millis(); }
                uint32_t            localAddress();
                IPAddress           localIP();
                std::string         localIPstring();
                uint16_t            localPort();
                size_t              maxPacket(){ return ( _HAL_maxHeapBlock() * (100-H4T_HEAP_CUTIN_PC)) / 100; }
//                size_t              maxPacket(){ return 3285; }
                void                nagle(bool b=true);
                void                onConnect(H4_FN_VOID cb){ _cbConnect=cb; }
                void                onDisconnect(H4_FN_VOID cb){ _cbDisconnect=cb; }
                void                onError(H4AT_FN_ERROR cb){ _cbError=cb; }
                void                onRX(H4AT_FN_RXDATA f){ _rxfn=f; }
                uint32_t            remoteAddress();
                IPAddress           remoteIP();
                std::string         remoteIPstring();
                uint16_t            remotePort();
                void                TX(const uint8_t* d,size_t len,bool copy=true);

// syscalls - just don't...
                uint8_t*            _addFragment(const uint8_t* data,u16_t len);
                void                _clearDanglingInput();
                void                _connect();
                void                _handleFragment(const uint8_t* data,u16_t len,u8_t flags);
                void                _notify(int e,int i=0);
        static  void                _scavenge();
                void                _shutdown();
};

class H4AsyncServer {
    protected:
            uint16_t                _port;
    public:
            H4AT_FN_ERROR        _srvError;
        H4AsyncServer(uint16_t port): _port(port){}
        virtual ~H4AsyncServer(){}

        virtual void        begin();
                void        onError(H4AT_FN_ERROR f){ _srvError=f; }
        virtual void        reset(){}
        virtual void        route(void* c,const uint8_t* data,size_t len)=0;

        virtual H4AsyncClient* _instantiateRequest(struct tcp_pcb *p)=0;
};