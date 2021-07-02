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

#include<vark_config.h>

#include<Arduino.h>

#include<functional>
#include<string>
#include<vector>
#include<map>
#include<queue>

#include<pmbtools.h>

#ifdef ARDUINO_ARCH_ESP32
    #include<WiFi.h>
    #include<AsyncTCP.h> /// no tls yet
#else
    #include<async_config.h> // for the ssl
    #include<ESP8266WiFi.h>
    #include<ESPAsyncTCP.h>
    #if ASYNC_TCP_SSL_ENABLED
        #include<tcp_axtls.h>
        #define SHA1_SIZE 20
    #endif
#endif

extern void dumphex(const uint8_t*,size_t);

#if VARK_DEBUG
    template<int I, typename... Args>
    void VARK_PRINT(const char* fmt, Args... args) {
        if (VARK_DEBUG >= I) Serial.printf(std::string(std::string("AARD:%d: ")+fmt).c_str(),I,args...);
    }
    #define VARK_PRINT1(...) VARK_PRINT<1>(__VA_ARGS__)
    #define VARK_PRINT2(...) VARK_PRINT<2>(__VA_ARGS__)
    #define VARK_PRINT3(...) VARK_PRINT<3>(__VA_ARGS__)
    #define VARK_PRINT4(...) VARK_PRINT<4>(__VA_ARGS__)

    template<int I>
    void vark_dump(const uint8_t* p, size_t len) { if (VARK_DEBUG >= I) dumphex(p,len); }
    #define VARK_DUMP3(p,l) vark_dump<3>((p),l)
    #define VARK_DUMP4(p,l) vark_dump<4>((p),l)
#else
    #define VARK_PRINT1(...)
    #define VARK_PRINT2(...)
    #define VARK_PRINT3(...)
    #define VARK_PRINT4(...)

    #define VARK_DUMP3(...)
    #define VARK_DUMP4(...)
#endif

enum VARK_FAILURE : uint8_t {
    VARK_TCP_DISCONNECTED, // 0
    VARK_TCP_UNHANDLED, // 1
    VARK_TLS_BAD_FINGERPRINT, // 2
    VARK_TLS_NO_FINGERPRINT, // 3
    VARK_TLS_NO_SSL, // 4
    VARK_TLS_UNWANTED_FINGERPRINT, // 5
    VARK_INPUT_TOO_BIG, // 6
    VARK_HEAP_LIMITER_ON, // 7
    VARK_HEAP_LIMITER_OFF, // 8
    VARK_HEAP_LIMITER_LOST, // 9
    VARK_MAX_ERROR
};

#include<mbx.h>

using VARK_FN_RXDATA    =std::function<void(const uint8_t*,size_t)>;

class mbx;

using VARK_FRAGMENTS    =std::vector<mbx>;
using VARK_MSG_Q        =std::queue<mbx>;
using VARK_cbConnect    =std::function<void()>;
using VARK_cbPoll       =std::function<void()>;
using VARK_cbDisconnect =std::function<void(int8_t)>;
using VARK_cbError      =std::function<void(int,int)>;
using VARK_NVP_MAP      =std::map<std::string,std::string>;

class AardvarkTCP {
        friend class mbx;
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
                };

                VARK_cbConnect      _cbConnect=nullptr;
                VARK_cbDisconnect   _cbDisconnect=nullptr;
                VARK_cbError        _cbError=[](int e,int i){};
                VARK_cbPoll         _cbPoll=nullptr;
        static  VARK_FRAGMENTS      _fragments;
                bool                _heapLock=false;

                void                _acCommon(AsyncClient* ac);
                size_t              _ackSize(size_t len){ return  _URL.secure ? 69+((len>>4)<<4):len; } // that was SOME hack! v. proud
                void                _busted(size_t len);
                void                _clearFragments();
                void                _ackTCP(size_t len,uint32_t time);
                void                _onData(uint8_t* data, size_t len);
                void                _onDisconnect(int8_t r);
                void                _release(mbx m);
        inline  void                _runTXQ();

                VARK_FN_RXDATA      _rxfn=[](const uint8_t* data, size_t len){};
                AsyncClient*        _ac;
public:
#if VARK_DEBUG
                size_t              _sigmaTX=0;
#endif
        static  PMB_HEAP_LIMITS     safeHeapLimits;
        static  VARK_MSG_Q          _TXQ; // to enable debug dump from higehr powers...
                URL                 _URL;
                void                _causeError(int e,int i){ _cbError(e,i); }
                void                _parseURL(const std::string& url);
                void                _releaseLock();
        //
                void                close(){ _ac->close(); }
                bool                connected(){ return _ac->connected(); }
                bool                isRecvPush(){ return _ac->isRecvPush(); }
        static  size_t              maxPacket(){ return (_HAL_maxHeapBlock() - safeHeapLimits.first) / 2; }
                void                onTCPconnect(VARK_cbConnect callback){ _cbConnect=callback; }
                void                onTCPdisconnect(VARK_cbDisconnect callback){ _cbDisconnect=callback; }
                void                onTCPerror(VARK_cbError callback){ _cbError=callback; }
                void                onTCPpoll(VARK_cbPoll callback){ _cbPoll=callback; }
                void                rx(VARK_FN_RXDATA f){ _rxfn=f; }
        static  void                safeHeap(size_t cutout,size_t cutin);
                void                setNoDelay(bool tf){ _ac->setNoDelay(tf); }
                void                setRxTimeout(size_t t){ _ac->setRxTimeout(t); }
                void                setAckTimeout(size_t t){ _ac->setAckTimeout(t); }
                void                TCPconnect();
                void                TCPdisconnect(bool force = false);
                void                TCPurl(const char* url,const uint8_t* fingerprint=nullptr);
                void                txdata(mbx m);
                void                txdata(const uint8_t* d,size_t len,bool copy=true);

        AardvarkTCP(){ _acCommon(new AsyncClient()); } // use yer own
        AardvarkTCP(AsyncClient* acp){ _acCommon(acp); } // transform (nick) someone else's

#if VARK_DEBUG
        static  void                dump(); // null if no debug
#endif
};