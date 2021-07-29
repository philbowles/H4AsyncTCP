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
#include "H4AsyncTCP.h"
#include "raw.h"

//
void mbx::_create(H4L_request*  c,uint8_t* p){
//    Serial.printf("mbx::_create 0x%08x p=0x%08x m=%d\n",c,p,managed);
    _c=c;
    if(managed){
        data=getMemory(c,len);
        if(data) memcpy(data,p,len);
        else H4AT_PRINT4("MBX 0x%08x len=%d MALLOC FAIL\n",(void*) data,len);
    } else data=p;
}
//
// public
//
void mbx::ack(){
    H4AT_PRINT4("MBX ACK 0x%08x len=%d  frag=0x%08x\n",(void*) data,len,frag);
    if(frag){
        H4AT_PRINT4("MBX FRAG=0x%08x\n",frag);
        if((int) frag < 1000) return; // some arbitrarily ridiculous max numberof _fragments SANITIZE as f of heap/mss
        data=frag; // reset data pointer to FIRST fragment, so whole block is freed
        H4AT_PRINT4("MBX DELETE MASTER FRAG=0x%08x\n",frag);
    }
    clear();
}

void mbx::clear(H4L_request* c, uint8_t* p){
    if(c->pool.count(p)) {
        H4AT_PRINT4("FOR 0x%08x MBX DEL BLOCK 0x%08x\n",c,p);
        free(p);
        c->pool.erase(p);
    }
}

void mbx::clear(){ clear(_c,data); }

uint8_t* mbx::getMemory(H4L_request* c,size_t size){
    uint8_t* mm=static_cast<uint8_t*>(malloc(size));
    if(mm){
        c->pool.insert(mm);
        H4AT_PRINT4("MBX MANAGED MEMORY BLOCK ALLOCATED 0x%08x len=%d\n",mm,size);
    } else H4AT_PRINT1("********** MBX FAIL STATUS: FH=%u MXBLK=%u ASKED:%u\n",_HAL_freeHeap(),_HAL_maxHeapBlock(),size);
    return mm;
}