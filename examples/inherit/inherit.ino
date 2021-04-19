#include<AardvarkTCP.h>
#include<Ticker.h>

#define LIBRARY "AardvarkTCP "Aardvark_VERSION
//#define URL "http://robot.local:80/index.html"
#define URL "https://robot.local:443/index.html"

class myprotocol: public AardvarkTCP{
    public:
        myprotocol(): AardvarkTCP(){}
};

Ticker T1;
myprotocol mp;

void AardvarkErrors(int e,int info){
  switch(e){
    case VARK_TCP_DISCONNECTED:
        Serial.printf("ERROR: NOT CONNECTED info=%d\n",info);
        break;
    case VARK_TCP_UNHANDLED:
        Serial.printf("ERROR: UNHANDLED TCP ERROR info=%d\n",info);
        break;
    case VARK_TLS_BAD_FINGERPRINT:
        Serial.printf("ERROR: TLS_BAD_FINGERPRINT info=%d\n",info);
        break;
    case VARK_TLS_NO_FINGERPRINT:
        Serial.printf("WARNING: NO FINGERPRINT, running insecure\n");
        break;
    case VARK_TLS_UNWANTED_FINGERPRINT:
        Serial.printf("WARNING: FINGERPRINT provided, insecure http:// given\n");
        break;
    case VARK_TLS_NO_SSL:
        Serial.printf("ERROR: secure https:// requested, NO SSL COMPILED-IN: READ DOCS!\n");
        break;
    case VARK_NO_SERVER_DETAILS: //  
    //  99.99% unlikely to ever happen, make sure you call setServer before trying to connect!!!
        Serial.printf("ERROR:NO_SERVER_DETAILS info=%02x\n",info);
        break;
    default:
        Serial.printf("UNKNOWN ERROR: %u extra info %d\n",e,info);
        break;    
  }
}

void setup(){

    Serial.begin(115200);
    Serial.printf("Clean Compile Tester %s\n",LIBRARY);

    mp.onTCPconnect([]{ 
      Serial.printf("User: Connected to %s max payload %d\n",URL,mp.getMaxPayloadSize());
      T1.once_ms(10000,[=](){ mp.TCPdisconnect(); });
    });
    mp.onTCPdisconnect([](int8_t reason){ Serial.printf("User: Disconnected from %s (reason %d)\n",URL,reason); });
    mp.onTCPerror(AardvarkErrors);
    mp.TCPurl(URL);
    mp.TCPconnect();
}

void loop() {}