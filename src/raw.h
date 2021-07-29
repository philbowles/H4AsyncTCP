#include<Arduino.h>
#include<H4AsyncTCP.h>

struct tcp_pcb;
class raw;
class H4L_handler;

using H4AS_RQ_HANDLER   = std::function<void(H4L_handler*)>;
using H4AT_FN_CB_DCX    = std::function<void(int)>;
using H4AT_FN_CB_ERROR  = std::function<void(int,int)>;

class H4L_request {
        friend class mbx;

    public:
            H4AT_FN_CB_DCX      _cbDisconnect;
            H4AT_FN_CB_ERROR    _cbError;

            raw*                _server=nullptr;
            H4AT_MEM_POOL       pool;
            H4AT_MSG_Q          _PXQ; // pendingQ
            H4AT_MSG_Q          _TXQ; // to enable debug dump from higehr powers...

            struct tcp_pcb *pcb;
            H4AT_FRAGMENTS      _fragments;
            uint32_t            _lastSeen=0;
            H4_TIMER            _pxqRunning=nullptr;
            size_t              _stored=0;
                
                void            _ackTCP(uint16_t len);
                void            _busted(size_t len);
                void            _clearFragments();
                void            _onData(mbx m);
                void            _runPXQ();
                void            _TX();

        H4L_request(tcp_pcb* p,raw* rp=nullptr);
        ~H4L_request();

                void        onDisconnect(H4AT_FN_CB_DCX cb){ _cbDisconnect=cb; }
                void        onError(H4AT_FN_CB_ERROR cb){ _cbError=cb; }
                void        txdata(mbx m);
                void        txdata(const uint8_t* d,size_t len,bool copy=true);

        void    process(uint8_t* data,size_t len);

        template <typename F,typename... Args>
        void dispatch(F f,Args... args) {
            H4_FN_VOID vf = std::bind([=](Args... args){ 
                if(this->*f) (this->*f)(args...);
                else Serial.printf(" 0x%08x OOOHHHHHHHHHHH FFFFFFFUCK\n",this);
            },std::forward<Args>(args)...);
            h4.queueFunction(vf);
        }
};

#define XDISPATCH_V(f,...) dispatch(&H4L_request::_cb##f,__VA_ARGS__);
#define XDISPATCH(f) dispatch(&H4L_request::_cb##f)

class H4L_handler {
    protected:
        std::string     _verb;
        std::string     _path;

        H4AS_RQ_HANDLER _f;
        PMB_NVP_MAP     _headers;
        H4L_request*    _r;

        virtual bool _handle(){
            if(_f) _f(this);
            else Serial.printf("AAAAAAAARGH NO HANDLER FUNCTION %s %s\n",_verb.data(),_path.data());
            return true;
        }

        static  std::string mimeType(const std::string& fn);
    public:
        PMB_NVP_MAP     _sniffHeader;
        H4L_handler(const std::string& verb,const std::string& path,H4AS_RQ_HANDLER f=nullptr): _verb(verb),_path(path),_f(f){}

        virtual bool handled(H4L_request* r,const std::string& verb,const std::string& path){
            // cleardown any previous from reqused connections
            _headers.clear();
            if(!(verb==_verb && path==_path)) return false; //////////////////////////////// STARTSWITH!
            _r=r;
            return _handle();
        };

        void        addHeader(const std::string& name,const std::string& value){ _headers[name]=value; }

        void        send(uint16_t code,const std::string& type,size_t length=0,const void* body=nullptr);
        void        sendFile(const std::string& fn);
        void        sendFileParams(const std::string& fn,PMB_NVP_MAP& params);
        void        sendstring(const std::string& type,const std::string& data){ send(200,type,data.size(),(const void*) data.data()); }
};

class H4L_handlerFile: public H4L_handler {
    protected:
        virtual bool _handle() override;
    public:
        H4L_handlerFile(): H4L_handler("GET","*"){}

        virtual bool handled(H4L_request* r,const std::string& verb,const std::string& path) override;
                bool serveFile(const std::string& fn);
};

class H4L_handler404: public H4L_handler {
    protected:
        virtual bool _handle() override;
    public:
        H4L_handler404(): H4L_handler("",""){}

        virtual bool handled(H4L_request* r,const std::string& verb,const std::string& path) override { return _handle(); }
};

class H4L_handlerSSE;

class H4L_SSEClient{
    public:
        H4L_handlerSSE* _handler;
        H4L_request*    _client;
        H4L_SSEClient(H4L_request* c,H4L_handlerSSE* h):_client(c),_handler(h){ H4AT_PRINT1("SSE CLIENT CTOR 0x%08x c=0x%08x h=0x%08x\n",this,c,h); }
        ~H4L_SSEClient(){
            H4AT_PRINT1("SSE CLIENT DTOR 0x%08x\n",this);
        }

    void send(const std::string& message, const std::string& event);
};

using H4AS_EVT_HANDLER   = std::function<void(H4L_SSEClient*)>;

#define H4AS_SSE_KA_ID 92
#define H4AS_SCAVENGE_ID 93
#define H4AS_SCAVENGE_FREQ 60000

class H4L_handlerSSE: public H4L_handler {
        std::unordered_set<H4L_SSEClient*> _clients;
        H4AS_EVT_HANDLER    _cbConnect;
        uint32_t            _timeout;

            void            onClient();
    public:
        uint32_t            _nextID=0;

        H4L_handlerSSE(const std::string& url,uint32_t timeout=H4AS_SCAVENGE_FREQ );
        ~H4L_handlerSSE();

    void onConnect(H4AS_EVT_HANDLER cb){ _cbConnect=cb; }
    void send(const std::string& message, const std::string& event="");
};

class raw {
                uint16_t                  _port;
    public:
        std::vector<H4L_handler*>         _handlers;

        raw(uint16_t port): _port(port){}

        void addHandler(H4L_handler* h){ _handlers.push_back(h); }
        void begin();
        void on(const std::string& verb,const std::string& path,H4AS_RQ_HANDLER f){ _handlers.push_back(new H4L_handler{verb,path,f}); }
        void startScavenger();
};