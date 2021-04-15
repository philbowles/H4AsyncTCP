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

#include"vark_config.h"

#include<Arduino.h>
#include<ESP8266WiFi.h>

#include<functional>
#include<string>
#include<map>
#include<queue>
#include<vector>

#ifdef ARDUINO_ARCH_ESP32
#include <AsyncTCP.h> /// no tls yet
#elif defined(ARDUINO_ARCH_ESP8266)
#include <ESPAsyncTCP.h>
    #if ASYNC_TCP_SSL_ENABLED
        #include <tcp_axtls.h>
        #define SHA1_SIZE 20
    #endif
#elif defined(ARDUINO_ARCH_STM32)
#include <STM32AsyncTCP.h>
#else
#error Platform not supported
#endif

#if VARK_DEBUG
    template<int I, typename... Args>
    void VARK_print(const char* fmt, Args... args) {
        if (VARK_DEBUG >= I) Serial.printf(std::string(std::string("D:%d: ")+fmt).c_str(),I,args...);
    }
  #define VARK_PRINT1(...) VARK_print<1>(__VA_ARGS__)
  #define VARK_PRINT2(...) VARK_print<2>(__VA_ARGS__)
  #define VARK_PRINT3(...) VARK_print<3>(__VA_ARGS__)
  #define VARK_PRINT4(...) VARK_print<4>(__VA_ARGS__)
#else
  #define VARK_PRINT1(...)
  #define VARK_PRINT2(...)
  #define VARK_PRINT3(...)
  #define VARK_PRINT4(...)
#endif

enum VARK_FAILURE : uint8_t {
    VARK_TCP_DISCONNECTED,
    VARK_TCP_UNHANDLED,
    VARK_TLS_BAD_FINGERPRINT,
    VARK_TLS_NO_FINGERPRINT,
    VARK_TLS_NO_SSL,
    VARK_TLS_UNWANTED_FINGERPRINT,
    VARK_NO_SERVER_DETAILS,
    VARK_TOO_BIG,
    VARK_MEM_LEAK,
    VARK_MAX_ERROR
};

#define DEFAULT_RX_TIMEOUT 1                    // Seconds for timeout
/*
err_enum_t {
  ERR_OK = 0, ERR_MEM = -1, ERR_BUF = -2, ERR_TIMEOUT = -3,
  ERR_RTE = -4, ERR_INPROGRESS = -5, ERR_VAL = -6, ERR_WOULDBLOCK = -7,
  ERR_USE = -8, ERR_ALREADY = -9, ERR_ISCONN = -10, ERR_CONN = -11,
  ERR_IF = -12, ERR_ABRT = -13, ERR_RST = -14, ERR_CLSD = -15,
  ERR_ARG = -16
}

#define HTTPCODE_CONNECTION_REFUSED  (-1)
#define HTTPCODE_SEND_HEADER_FAILED  (-2)
#define HTTPCODE_SEND_PAYLOAD_FAILED (-3)
#define HTTPCODE_NOT_CONNECTED       (-4)
#define HTTPCODE_CONNECTION_LOST     (-5)
#define HTTPCODE_NO_STREAM           (-6)
#define HTTPCODE_NO_HTTP_SERVER      (-7)
#define HTTPCODE_TOO_LESS_RAM        (-8)
#define HTTPCODE_ENCODING            (-9)
#define HTTPCODE_STREAM_WRITE        (-10)
#define HTTPCODE_TIMEOUT             (-11)
*/
using VARK_DELAYED_FREE = uint8_t*;
using ADFP              = VARK_DELAYED_FREE; // SOOO much less typing - ARMA "delayed free" pointer

#include<mbx.h>

using VARK_FN_RXDATA    = std::function<void(const uint8_t*,size_t)>;
using VARK_FN_RXSTRING  = std::function<void(const std::string&)>;

class mbx;

using VARK_FRAGMENTS    = std::vector<mbx>;
using VARK_MSG_Q        = std::queue<mbx>;

using VARK_cbConnect    =std::function<void()>;
using VARK_cbDisconnect =std::function<void(int8_t)>;
using VARK_cbError      =std::function<void(int,int)>;

using VARK_NVP_MAP      =std::map<std::string,std::string>;

class AardvarkTCP: public AsyncClient {
#if ASYNC_TCP_SSL_ENABLED
            uint8_t             _fingerprint[SHA1_SIZE];
#endif
    struct  URL {
        char*   scheme;
        char*   host;
        int     port;
        char*   path;
        char*   query;
        char*   fragment;
        bool    secure;
        URL():
            scheme(nullptr),
            host(nullptr),
            port(80),
            path(nullptr),
            query(nullptr),
            fragment(nullptr)
            {};
        ~URL(){
            delete[] scheme;
            delete[] host;
            delete[] path;
            delete[] query;
            delete[] fragment;
        }
    };
            VARK_MSG_Q          TXQ;
            VARK_cbConnect      _cbConnect=nullptr;
            VARK_cbDisconnect   _cbDisconnect=nullptr;
            VARK_FRAGMENTS      _fragments;
            size_t              _space;
            size_t              _maxpl;

            void                _onData(uint8_t* data, size_t len);
            void                _onDisconnect(int8_t r);

            size_t              _ackSize(size_t len){ return  _URL->secure ? 69+((len>>4)<<4):len; } // that was SOME hack! v. proud
            void                _ackTCP(size_t len,uint32_t time);
            void                _runTXQ();
            void                _release(mbx m);

    protected:
            VARK_FN_RXDATA      _rxfn=[](const uint8_t* data, size_t len){};
            VARK_cbError        _cbError=[](int e,int i){};
            URL*                _URL;

            void                _HAL_feedWatchdog();
            uint32_t            _HAL_getFreeHeap();
            void                _parseURL(const std::string& url);
    public:
        AardvarkTCP();
//        ~AardvarkTCP();       singleton, lifetime of app: never called

            void                dump(); // null if no debug
//
            size_t inline       getMaxPayloadSize(){ return _maxpl; }
            void                onServerConnect(VARK_cbConnect callback){ _cbConnect=callback; }
            void                onServerDisconnect(VARK_cbDisconnect callback){ _cbDisconnect=callback; }
            void                onServerError(VARK_cbError callback){ _cbError=callback; }
//
//                              rx   *M U S T*   free the data pointer passed in
//                                               otherwise you will get error VARK_MEM_LEAK
//
            void                rx(VARK_FN_RXDATA f){ _rxfn=f; }
            void                rxstring(VARK_FN_RXSTRING f){ 
                _rxfn=[=](const uint8_t* data, size_t len){
                    std::string x((const char*) data,len);
                    ::free((void*) data);
                    f(x);
                 }; 
            }

            void                serverConnect();
            void                serverDisconnect(bool force = false);
            void                serverURL(const char* url,const uint8_t* fingerprint=nullptr);
            void                txdata(const uint8_t* d,size_t len,bool copy=true);
            void                txstring(const std::string& s,bool copy=true){ txdata((const uint8_t*) s.c_str(),s.size(),copy); }
//
//              DO NOT CALL ANY FUNCTION STARTING WITH UNDERSCORE!!! _
//
};