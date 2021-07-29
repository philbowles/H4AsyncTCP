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
#include<H4EchoServer.h>
extern "C"{
  #include "lwip/tcp.h"
}
//
//
//
H4EchoServer::H4EchoServer(uint16_t _port): H4AsyncServer(_port){
    _client_cb=[=](tcp_pcb* tpcb){
        auto c=new H4AsyncClient(tpcb);
        c->onConnect([=]{
            IPAddress ip(ip_addr_get_ip4_u32(&c->_pcb->remote_ip));
            H4AT_PRINT2("0x%08x CLIENT incoming=0x%08x from %s:%d\n",this,(void*)c,ip.toString().c_str(),c->_pcb->remote_port);
        });

        c->onPoll([&]{
            /*
            Serial.printf("ECHO BEACH T=%d P=%d C=%d\n",c->_TXQ.size(),c->_PXQ.size(),c->_closeConnection);
            if(c->_TXQ.size()==0 && c->_PXQ.size()==0) {
                if(c->_closeConnection){
                    H4AT_PRINT3("0x%08x HAD ENOUGH OF THIS LIFE \n",c);
                    c->close();
                    tcp_abort(c->_pcb);
                    return ERR_ABRT;
                } else H4AT_PRINT3("0x%08x Ah! ah! ah! ah! stayin' alive\n",c);
            }
            */
            return ERR_OK;
        });

        c->rx([=](const uint8_t* data, size_t len){ c->txdata(data,len); });
        // set to sideload
        tcp_setprio(c->_pcb, TCP_PRIO_MIN);
        tcp_arg(c->_pcb, c);
        
        H4AsyncClient::_tcp_connected(c,c->_pcb,ERR_OK);
    //    return c;
    };
}