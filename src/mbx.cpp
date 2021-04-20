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
#include"mbx.h"
#include <pmbtools.h>

VARK_MEM_POOL       mbx::pool;
//
//      x
//
void mbx::_create(ADFP p){
    if(managed){
        data=getMemory(len);
        memcpy(data,p,len);
    } else data=p;
}
//
// public
//
void mbx::ack(){
    VARK_PRINT4("MBX ACK 0x%08x len=%d  frag=0x%08x\n",(void*) data,len,frag);
    if(frag){
        VARK_PRINT4("MBX FRAG=0x%08x\n",frag);
        if((int) frag < 1000) return; // some arbitrarily ridiculous max numberof _fragments SANITIZE as f of heap/mss
        data=frag; // reset data pointer to FIRST fragment, so whole block is freed
        VARK_PRINT4("MBX DELETE MASTER FRAG=0x%08x\n",frag);
    }
    clear();
}

void mbx::clear(ADFP p){
    if(pool.count(p)) {
        free(p);
        pool.erase(p);
        VARK_PRINT4("MBX DEL BLOCK 0x%08x FH=%u\n",p,_HAL_maxHeapBlock());
    }
}

void mbx::clear(){ clear(data); }

void mbx::emptyPool(){
    VARK_PRINT4("MBX EMPTY POOL len=%d FH=%u\n",pool.size(),_HAL_maxHeapBlock());
    for(auto const& p:pool) clear(p);
    pool.clear();
}

uint8_t* mbx::getMemory(size_t size){
    if(size > _HAL_maxPayloadSize()) return nullptr;
    uint8_t* mm=static_cast<uint8_t*>(malloc(size));
    if(mm){
        pool.insert(mm);
        VARK_PRINT4("MBX MANAGED MEMORY BLOCK ALLOCATED 0x%08x len=%d\n",mm,size);
    } //else VARK_PRINT1("********** MBX STATUS: FH=%u MXBLK=%u FM=%u\n",ESP.getFreeHeap(),ESP.getMaxFreeBlockSize(),ESP.getHeapFragmentation());
    return mm;
}

#if VARK_DEBUG
void mbx::_dump(size_t n){ 
    Serial.printf("BLOCK 0x%08x len=%d frag=0x%08x\n",(void*) data,len,frag);
    dumphex(data,len < n ? len:n);
}

void mbx::dump(size_t n){ 
    Serial.printf("\n%d Pool Blox\n",pool.size());
    for(auto const& p:pool){
        dumphex(p,n); // Danger, Will Robinson!!!
        Serial.println();
    }
    Serial.println();
}
#endif