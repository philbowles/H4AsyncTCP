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

static err_t _raw_accept(void *arg, struct tcp_pcb *newpcb, err_t err){
    Serial.printf("RAW _raw_accept <-- arg=%p p=%p e=%d\n",arg,newpcb,err);
    if(!err){
        tcp_setprio(newpcb, TCP_PRIO_MIN);
        H4AT_PRINT1("RAW _raw_accept <-- arg=%p p=%p e=%d\n",arg,newpcb,err);
        reinterpret_cast<H4AsyncServer*>(arg)->_newConnection(newpcb);
        return ERR_OK;
    } else {
        Serial.printf("RAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAW %d\n",err);
        return err;
    }
}
//
//      H4AsyncServer
//
void H4AsyncServer::_newConnection(struct tcp_pcb *p){
    h4.queueFunction([=]{
        Serial.printf("NEW CONNECTION --> pcb=%p\n",p);
        auto c=_instantiateRequest(p);
        if(c){
            c->_lastSeen=millis();
            Serial.printf("QF insert c --> in %p\n",c);
            H4AsyncClient::openConnections.insert(c);
            H4AT_PRINT1("QF insert c --> out %p\n",c);
            c->onError([=](int e,int i){
                if(e==ERR_MEM){
                    Serial.printf("OOM ERROR %d\n",i);
                    
                } if(_srvError) _srvError(e,i);
            });
            H4AT_PRINT1("QF 1 %p\n",c);
            c->onRX([=](const uint8_t* data,size_t len){ route(c,data,len); });
            H4AT_PRINT1("QF 2 %p\n",c);
            H4AsyncClient::_scavenge();
            H4AT_PRINT1("QF 3 %p\n",c);
        } //else Serial.printf("WHAT THE ACTUAL FUCK??? ZERO client ptr!");
    });
}
//
void H4AsyncServer::begin(){
    H4AT_PRINT1("SERVER %p begin\n",this);
    static auto _raw_pcb = tcp_new();
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