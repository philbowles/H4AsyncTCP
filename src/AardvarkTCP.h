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
    #include <ESPAsyncTCP.h>
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
    VARK_TCP_DISCONNECTED,
    VARK_TCP_UNHANDLED,
    VARK_TLS_BAD_FINGERPRINT,
    VARK_TLS_NO_FINGERPRINT,
    VARK_TLS_NO_SSL,
    VARK_TLS_UNWANTED_FINGERPRINT,
    VARK_NO_SERVER_DETAILS,
    VARK_INPUT_TOO_BIG,
    VARK_MAX_ERROR
};
/* LwIP
err_enum_t {
  ERR_OK = 0, ERR_MEM = -1, ERR_BUF = -2, ERR_TIMEOUT = -3,
  ERR_RTE = -4, ERR_INPROGRESS = -5, ERR_VAL = -6, ERR_WOULDBLOCK = -7,
  ERR_USE = -8, ERR_ALREADY = -9, ERR_ISCONN = -10, ERR_CONN = -11,
  ERR_IF = -12, ERR_ABRT = -13, ERR_RST = -14, ERR_CLSD = -15,
  ERR_ARG = -16
}
*/
#include<mbx.h>

using VARK_FN_RXDATA    = std::function<void(const uint8_t*,size_t)>;

class mbx;

using VARK_FRAGMENTS    = std::vector<mbx>;
using VARK_MSG_Q        = std::queue<mbx>;

using VARK_cbConnect    =std::function<void()>;
using VARK_cbPoll       =std::function<void()>;
using VARK_cbDisconnect =std::function<void(int8_t)>;
using VARK_cbError      =std::function<void(int,int)>;

using VARK_NVP_MAP      =std::map<std::string,std::string>;

class AardvarkTCP: public AsyncClient {
        friend class mbx;
#if ASYNC_TCP_SSL_ENABLED
            uint8_t             _fingerprint[SHA1_SIZE];
#endif
    struct  URL {
        std::string   scheme;
        std::string   host;
        int           port;
        std::string   path;
        std::string   query;
        std::string   fragment;
        bool          secure;
        URL(){};
    };
        static  VARK_MSG_Q          _TXQ; // to enable debug dump from higehr powers...
                VARK_cbConnect      _cbConnect=nullptr;
                VARK_cbDisconnect   _cbDisconnect=nullptr;
                VARK_cbError        _cbError=[](int e,int i){};
                VARK_cbPoll         _cbPoll=nullptr;
        static  VARK_FRAGMENTS      _fragments;
        static  size_t              _maxpl;

                size_t              _ackSize(size_t len){ return  _URL->secure ? 69+((len>>4)<<4):len; } // that was SOME hack! v. proud
                void                _busted(size_t len);
                void                _clearFragments();
                void                _ackTCP(size_t len,uint32_t time);
                void                _onData(uint8_t* data, size_t len);
                void                _onDisconnect(int8_t r);
                void                _release(mbx m);
        inline  void                _runTXQ();

                VARK_FN_RXDATA      _rxfn=[](const uint8_t* data, size_t len){};
    protected:
                URL*                _URL;
                void                _causeError(int e,int i){ _cbError(e,i); }
                void                _parseURL(const std::string& url);
        //
        static  size_t inline       getMaxPayloadSize(){ return _maxpl; }
                void                onTCPconnect(VARK_cbConnect callback){ _cbConnect=callback; }
                void                onTCPdisconnect(VARK_cbDisconnect callback){ _cbDisconnect=callback; }
                void                onTCPerror(VARK_cbError callback){ _cbError=callback; }
                void                onTCPpoll(VARK_cbPoll callback){ _cbPoll=callback; }
                void                rx(VARK_FN_RXDATA f){ _rxfn=f; }
                void                TCPconnect();
                void                TCPdisconnect(bool force = false);
                void                TCPurl(const char* url,const uint8_t* fingerprint=nullptr);
                void                txdata(mbx m);
                void                txdata(const uint8_t* d,size_t len,bool copy=true);

    public:
        AardvarkTCP();

#if VARK_DEBUG
        static  void                dump(); // null if no debug
#endif
//
//              DO NOT CALL ANY FUNCTION STARTING WITH UNDERSCORE!!! _
//
};