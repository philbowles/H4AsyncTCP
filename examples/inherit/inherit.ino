#include<H4AsyncClient.h>
#include<Ticker.h>

#define URL "http://blackbox.local:80/index.html"
//#define URL "http://192.168.1.21:80/index.html"
//#define URL "https://robot.local:443/index.html"

class myprotocol: public H4AsyncClient{
    public:
        myprotocol(): H4AsyncClient(){
          onTCPconnect([]{ Serial.printf("User: Connected to %s\n",URL); });
          onTCPdisconnect([](int8_t reason){ Serial.printf("User: Disconnected from %s (reason %d)\n",URL,reason); });
       }

       void connect(const std::string& url,const uint8_t* fingerprint=nullptr){
          TCPurl(URL,fingerprint);
          TCPconnect();       
       }

      void disconnect(){ TCPdisconnect(); }

      void errorHandler(H4AT_cbError f){ onTCPerror(f); }
};


Ticker A1;
myprotocol mp;

void H4AsyncErrors(int e,int info){
  switch(e){
    case H4AT_TCP_DISCONNECTED:
        Serial.printf("ERROR: NOT CONNECTED info=%d\n",info);
        break;
    case H4AT_TCP_UNHANDLED:
        Serial.printf("ERROR: UNHANDLED TCP ERROR info=%d\n",info);
        break;
    case H4AT_TLS_BAD_FINGERPRINT:
        Serial.printf("ERROR: TLS_BAD_FINGERPRINT info=%d\n",info);
        break;
    case H4AT_TLS_NO_FINGERPRINT:
        Serial.printf("WARNING: NO FINGERPRINT, running insecure\n");
        break;
    case H4AT_TLS_NO_SSL:
        Serial.printf("ERROR: secure https:// requested, NO SSL COMPILED-IN: READ DOCS!\n");
        break;
    case H4AT_TLS_UNWANTED_FINGERPRINT:
        Serial.printf("WARNING: FINGERPRINT provided, insecure http:// given\n");
        break;
/*
    case H4AT_NO_SERVER_DETAILS: //  
        Serial.printf("ERROR:NO_SERVER_DETAILS info=%02x\n",info);
        break;
*/
    case H4AT_INPUT_TOO_BIG: //  
        Serial.printf("ERROR: RX msg(%d) that would 'break the bank'\n",info);
        break;
    default:
        Serial.printf("UNKNOWN ERROR: %u extra info %d\n",e,info);
        break;    
  }
}

void setup(){
    Serial.begin(115200);
    Serial.printf("H4Async Tester %s\n",H4Async_VERSION);

    WiFi.begin("XXXXXXXX","XXXXXXXX");
    while(WiFi.status()!=WL_CONNECTED){
      Serial.printf(".");
      delay(1000);
    }
    Serial.printf("WIFI CONNECTED IP=%s\n",WiFi.localIP().toString().c_str());

    mp.errorHandler(H4AsyncErrors);
    mp.connect(URL); // no fingerprint
    A1.once_ms(10000,[=](){ mp.disconnect(); });
    
}

void loop() {}