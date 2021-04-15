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
#include<AardvarkUtils.h>

VARK_MEM_POOL       mbx::pool;
//
//      x
//
void mbx::_create(ADFP p){
    if(managed){
        data=static_cast<uint8_t*>(malloc(len));
        memcpy(data,p,len);
        if(!pool.count(data)){
            pool.insert(data);
            Serial.printf("POOL NEW BLOCK 0x%08x len=%d  0x%08x\n",(void*) data,len,frag);
        } else Serial.printf("DUP POOL BLOK!!! 0x%08x\n",(void*) data);
    } else data=p;
    dumphex(data,len < 32 ? len:32);
}
/*
void  mbx::_move(mbx& other){
    data = other.data;
    other.data = nullptr;
    frag = other.frag;
    other.frag = nullptr;
    len=other.len;
    managed=other.managed;
}
*/
void mbx::_dump(size_t n){ 
    Serial.printf("BLOCK 0x%08x len=%d frag=0x%08x\n",(void*) data,len,frag);
    dumphex(data,len < n ? len:n);
}
//
// public
//
void mbx::ack(){
    Serial.printf("MBX ACK 0x%08x len=%d  0x%08x\n",(void*) data,len,frag);
    if(frag){
        if((int) frag < 1000) return; // some arbitrarily ridiculous max numberof _fragments SANITIZE as f of heap/mss
        data=frag; // reset data pointer to FIRST fragment, so whole block is freed
    }
    clear();
}

void mbx::clear(){
    if(pool.count(data)) {
        Serial.printf("MBX DEL BLOCK 0x%08x len=%d  0x%08x\n",(void*) data,len,frag);
        free(data);
        pool.erase(data);
    }
}

void mbx::dump(size_t n){ 
    Serial.printf("\nPool Blox\n");
    for(auto const& p:pool){
        dumphex(p,n); // Danger, Will Robinson!!!
        Serial.println();
    }
    Serial.println();
}