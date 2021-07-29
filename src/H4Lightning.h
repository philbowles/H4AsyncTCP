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

#include<H4AsyncTCP.h>
//
//   REQUEST
//
//class H4L_request: public H4AsyncClient {

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
            if(_f) _f(r);
            else Serial.printf("AAAAAAAARGH NO HANDLER FUNCTION %s %s\n",_verb.data(),_path.data());
            return true;
        }
    public:
        H4L_handler(const char* verb,const char* path,H4AS_RQ_HANDLER f=nullptr): _verb(verb),_path(path),_f(f){}

        virtual bool handled(H4L_request* r,const std::string& verb,const std::string& path){
            if(!(verb==_verb && path==_path)) return false; //////////////////////////////// STARTSWITH!
            return _handle(r);
        };
};

class H4L_handlerFile: public H4L_handler {
    protected:
        virtual bool _handle(H4L_request* r) override;
    public:
        H4L_handlerFile(): H4L_handler("GET","*"){}

        virtual bool handled(H4L_request* r,const std::string& verb,const std::string& path) override;
        static bool serveFile(H4L_request* r,const std::string& fn);
};

class H4L_handler404: public H4L_handler {
    protected:
        virtual bool _handle(H4L_request* r) override;
    public:
        H4L_handler404(): H4L_handler("",""){}

        virtual bool handled(H4L_request* r,const std::string& verb,const std::string& path) override { return _handle(r); }
};

class H4L_SSEClient{
        uint32_t        _lastID=0;
    public:
        H4L_request*    _client;
        H4L_SSEClient(H4L_request* c):_client(c){ H4AT_PRINT1("SSE CLIENT CTOR 0x%08x r=0x%08x\n",this,_client);
 }
        ~H4L_SSEClient(){
            H4AT_PRINT1("SSE CLIENT DTOR 0x%08x\n",this);
        }

    void send(const char *message, const char *event=NULL, uint32_t id=0, uint32_t reconnect=0);
};

using H4AS_EVT_HANDLER   = std::function<void(H4L_SSEClient*)>;

class H4L_handlerSSE: public H4L_handler {
        std::vector<H4L_SSEClient*> _clients;
        H4AS_EVT_HANDLER   _cbConnect;
        H4_TIMER           _keepAlive; 
    public:
        void onClient(H4L_request* r);

        H4L_handlerSSE(const std::string& url): H4L_handler("GET",url.data(),[=](H4L_request* r){ onClient(r); }){
            H4AT_PRINT1("SSE HANDLER CTOR 0x%08x\n",this);
        }
        ~H4L_handlerSSE(){
            H4AT_PRINT1("SSE HANDLER DTOR 0x%08x\n",this);
            for(auto &c:_clients){
                c->_client->close();
            }
//          h4.cancel(_keepAlive);
        }

    void onConnect(H4AS_EVT_HANDLER cb){ _cbConnect=cb; }

    void send(const char *message, const char *event=NULL, uint32_t id=0, uint32_t reconnect=0){ for(auto &c:_clients) c->send(message,event,id,reconnect); }
};
//
//
//
class H4Lightning: public H4AsyncServer {
    public:
        static  PMB_NVP_MAP    _ext2ct;
        static  std::vector<H4L_handler*> handlers;

        H4Lightning(uint16_t _port);
        
        void begin() override;
        void reset() override;
        static void on(const char* verb,const char* path,H4AS_RQ_HANDLER f){ handlers.push_back(new H4L_handler{verb,path,f}); }
        static void addHandler(H4L_handler* h){ handlers.push_back(h); }
};