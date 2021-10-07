/*
MIT License

Copyright (c) 2020 Phil Bowles with huge thanks to Adam Sharp http://threeorbs.co.uk
for testing, debugging, moral support and permanent good humour.

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
#include<H4AsyncTCP.h>

extern "C"{
  #include "lwip/tcp.h"
}

static err_t _raw_accept(void *arg, struct tcp_pcb *p, err_t err){
    h4.queueFunction([arg,p,err]{
        Serial.printf("RAW _raw_accept <-- arg=%p p=%p e=%d\n",arg,p,err);
        if(!err){
            tcp_setprio(p, TCP_PRIO_MIN);
            H4AT_PRINT1("RAW _raw_accept <-- arg=%p p=%p e=%d\n",arg,p,err);
            auto srv=reinterpret_cast<H4AsyncServer*>(arg);
            auto c=srv->_instantiateRequest(p);
            Serial.printf("NEW CONNECTION %p --> pcb=%p\n",c,p);
            if(c){
                c->_lastSeen=millis();
                c->onError([=](int e,int i){
                    if(e==ERR_MEM){
                        Serial.printf("OOM ERROR %d\n",i); // Retry-After: 120
                        return false;
                    } if(srv->_srvError) srv->_srvError(e,i);
                    return true;
                });
                H4AT_PRINT1("QF 1 %p\n",c);
                c->onRX([=](const uint8_t* data,size_t len){ srv->route(c,data,len); });
                H4AT_PRINT1("QF insert c --> in %p\n",c);
                H4AsyncClient::openConnections.insert(c);
                H4AT_PRINT1("QF insert c --> out %p\n",c);
            }  else Serial.printf("_instantiateRequest returns 0 !!!!!  %p\n",p);
        } else Serial.printf("RAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAW %d\n",err);
    });
    return ERR_OK;
}
//
//      H4AsyncServer
//
void H4AsyncServer::begin(){
//    h4.every(1000,[]{ heap_caps_check_integrity_all(true); });
    H4AT_PRINT1("SERVER %p listening on port %d\n",this,_port);
    auto _raw_pcb = tcp_new();
    if (_raw_pcb != NULL) {
        err_t err;
        tcp_arg(_raw_pcb,this);
        err = tcp_bind(_raw_pcb, IP_ADDR_ANY, _port);
        if (err == ERR_OK) {
            _raw_pcb = tcp_listen(_raw_pcb);
            tcp_accept(_raw_pcb, _raw_accept);
        } //else Serial.printf("RAW CANT BIND\n");
    } // else Serial.printf("RAW CANT GET NEW PCB\n");
}