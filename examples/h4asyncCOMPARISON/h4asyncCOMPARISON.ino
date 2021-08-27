/*
 Yes, I know: this code is a filthy hack! It doesn't cleanup the connection, uses statics for
 cheap-n-dirty record counting, is full of unused / redundant sh*t and commits a dozen other sins...
 I chopped it from another example when I was tired in 5 minutes flat to perform a one-time task
 ...so live with it!
 */
#include<H4.h>
H4 h4(115200);

H4_TIMER  dataLoss;
H4_TIMER  stagger;

#define USE_ASYNC_CLIENT 1

#define USE_NAGLE true
#define DEFAULT_TIMEOUT  10000
#define STAGGER 0
#define BURST_SIZE 20

#ifdef ARDUINO_ARCH_ESP32
  #include<WiFi.h>
#else
  #include<ESP8266WiFi.h>
#endif
 
#if USE_ASYNC_CLIENT 
  #ifdef ARDUINO_ARCH_ESP32
    #define LIB_NAME "AsyncTCP"
    #include<AsyncTCP.h>
  #else
    #define LIB_NAME "ESPAsyncTCP"
    #include<ESPAsyncTCP.h>
  #endif
#else
  #include<H4AsyncTCP.h>
  #define LIB_NAME "H4AsyncTCP"
#endif

#define BUFSIZE_PACKET  TCP_SND_BUF
#define GTBUF_PACKET (TCP_SND_BUF * 3) / 2
#define SPEED_PACKET TCP_SND_BUF / BURST_SIZE

#if USE_ASYNC_CLIENT 
  char bigbuf[GTBUF_PACKET]; // WRONG!!!
#else
  uint8_t bigbuf[GTBUF_PACKET];
#endif

struct test{
  size_t      N;
  size_t      size;
  size_t      Tstart;
  size_t      dataTX;
  size_t      dataRX;
#if USE_ASYNC_CLIENT
  AsyncClient* a;
#else
  H4AsyncClient* a;
#endif   
};

using VT = std::vector<test>;
VT tests={
  {1,BUFSIZE_PACKET}, 
  {2,BUFSIZE_PACKET}, 
  {1,GTBUF_PACKET},
  {BURST_SIZE,SPEED_PACKET}  
};

size_t I=0;


void handleData(size_t i,void* data,size_t len){
    VT::value_type* T=&tests[i];
    size_t N=T->N;
    size_t size=T->size;
    T->dataRX+=len;
    // Serial.printf("HD%d T=%u RCV %d/%d bytes [%d] from 0x%08x\n",i+1,millis(),len,T->dataRX,T->dataTX,data);
    if(T->dataRX==T->dataTX){
      h4.cancel(dataLoss);
      auto Ttotal=millis()-T->Tstart;
      Serial.printf("T=%u TEST %d: PASS: %d bytes received. T=%u BPSavg=%d\n",millis(),i+1,T->dataRX,Ttotal,(T->dataRX/Ttotal));
      h4.cancel(stagger);
#if USE_ASYNC_CLIENT
      T->a->close(); /// ...and kill?
#else
      T->a->close(); 
#endif  
    }
}

void sendPackets(size_t i){
    VT::value_type* T=&tests[i];
    size_t N=T->N;
    size_t size=T->size;

    Serial.printf("T=%u TEST %d: Send %d packet(s) @ %d (=%d) STAGGER=%d\n",millis(),i+1,N,size,N*size,STAGGER);
    T->dataRX=0;
    T->dataTX=N*size;
      dataLoss=h4.once(DEFAULT_TIMEOUT,[=]{
      Serial.printf("TEST %d: FAIL: DATA LOSS - RCVD %u\n",i+1,T->dataRX);
      dataLoss=h4.cancel(stagger);
      T->a->close(); /// ...and kill?  
    });

    T->Tstart=millis();
    stagger=h4.nTimes(N,STAGGER,
      [=]{
#if USE_ASYNC_CLIENT
        T->a->write(bigbuf,size);
#else
        T->a->TX(bigbuf,size,false); // data is static - no copy
#endif
      }
    );
}

void runTest(size_t i){
#if USE_ASYNC_CLIENT
    AsyncClient* a=new AsyncClient();
    tests[i].a=a;

    a->onData([=](void*, AsyncClient*, void* data,size_t len){ 
      handleData(i,(void*) data,len);  
    });
    
    a->onConnect([=](void*, AsyncClient*){
      a->setNoDelay(!USE_NAGLE);
      sendPackets(i);
    });
    a->connect("192.168.1.20",8080);
#else
    H4AsyncClient* a=new H4AsyncClient();
    tests[i].a=a;
    a->onRX([=](const uint8_t* data, size_t len){ handleData(i,(void*) data,len); });
    a->onConnect([=]{ 
        a->nagle(USE_NAGLE);
        sendPackets(i); 
    });
    a->connect("192.168.1.20:8080");
#endif
}

#define TEST_LAUNCH DEFAULT_TIMEOUT + 1000
void runTests(){
    I=0;   
    h4Chunker<VT>(tests,[=](VT::iterator i){ runTest(I++); },TEST_LAUNCH,TEST_LAUNCH);
}

void h4setup(){
  WiFi.mode(WIFI_STA);
  WiFi.begin("XXXXXXXX", "XXXXXXXX");
  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("Connection Failed! Rebooting...");
    Serial.print(".");
    delay(5000);
  }
  Serial.printf("\nIP: %s\n",WiFi.localIP().toString().c_str());
  Serial.printf("LIB %s: NAGLE=%s MSS=%d SND_BUF=%d WND=%d\n",LIB_NAME,USE_NAGLE ? "true":"false",TCP_MSS,TCP_SND_BUF,TCP_WND);
  auto i=1;
  for(auto const& t:tests) Serial.printf("  Test %d: %d pkts %d\n",i++,t.N,t.size);
  Serial.printf("Cry 'Havoc!' and let slip the packets of War!\n\n");
  runTests();
}
