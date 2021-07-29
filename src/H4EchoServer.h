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
/*
//
//   REQUEST
//
class H4L_request: public H4AsyncClient {
    public:            
        H4L_request(tcp_pcb* p);
        ~H4L_request(){}

                void        send(uint16_t code,const std::string& type,size_t length,const void* body);
                void        sendFile(const std::string& fn);
                void        sendFileParams(const std::string& fn,PMB_NVP_MAP& params);

        static  std::string mimeType(const std::string& fn);
};

//using H4AS_RQ_HANDLER   = std::function<void(H4L_request*)>;
//
//   andlaz
//
class H4L_handler {
    protected:
        std::string     _verb;
        std::string     _path;
        H4AS_RQ_HANDLER _f;

        virtual bool _handle(H4L_request* r){
            _f(r);
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
*/
class H4EchoServer: public H4AsyncServer {
    public:
        H4EchoServer(uint16_t _port);
        
//        void begin() override;
//        void reset() override;
};