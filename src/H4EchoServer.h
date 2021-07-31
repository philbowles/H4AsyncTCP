#include <H4AsyncTCP.h>

class H4EchoServer: public H4AsyncWebServer {
    public:
        H4EchoServer(uint16_t _port);
        
//        void begin() override;
//        void reset() override;
};