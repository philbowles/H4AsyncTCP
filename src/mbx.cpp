/*
Creative Commons: Attribution-NonCommercial-ShareAlike 4.0 International (CC BY-NC-SA 4.0)
https://creativecommons.org/licenses/by-nc-sa/4.0/legalcode

You are free to:

Share — copy and redistribute the material in any medium or format
Adapt — remix, transform, and build upon the material

The licensor cannot revoke these freedoms as long as you follow the license terms. Under the following terms:

Attribution — You must give appropriate credit, provide a link to the license, and indicate if changes were made. 
You may do so in any reasonable manner, but not in any way that suggests the licensor endorses you or your use.

NonCommercial — You may not use the material for commercial purposes.

ShareAlike — If you remix, transform, or build upon the material, you must distribute your contributions 
under the same license as the original.

No additional restrictions — You may not apply legal terms or technological measures that legally restrict others 
from doing anything the license permits.

Notices:
You do not have to comply with the license for elements of the material in the public domain or where your use is 
permitted by an applicable exception or limitation. To discuss an exception, contact the author:

philbowles2012@gmail.com

No warranties are given. The license may not give you all of the permissions necessary for your intended use. 
For example, other rights such as publicity, privacy, or moral rights may limit how you use the material.
*/
#include "H4AsyncTCP.h"

mbx::mbx(uint8_t* p,size_t s,bool copy,uint8_t f): len(s),managed(copy),flags(f){
    if(managed){
        data=getMemory(len);
        if(data) {
            memcpy(data,p,len);
            H4AT_PRINT4("MBX %p len=%d COPIED FROM %p POOL=%d\n",(void*) data,len,p,pool.size());
        }
        else H4AT_PRINT4("MBX %p len=%d MALLOC FAIL\n",(void*) data,len);
    } 
    else {
        H4AT_PRINT4("MBX %p len=%d UNMANAGED POOL=%d\n",p,len,pool.size());
        data=p;
    }
}
//
// public
//
void mbx::clear(uint8_t* p){
    if(pool.count(p)) {
        H4AT_PRINT4("MBX DEL BLOCK %p\n",p);
        free(p);
        pool.erase(p);
        H4AT_PRINT4("MBX DEL %p POOL NOW %d\n",p,pool.size());
    } else H4AT_PRINT4("INSANITY? %p NOT IN POOL!\n",p);
}

void mbx::clear(){ clear(data); }

uint8_t* mbx::getMemory(size_t size){
    uint8_t* mm=static_cast<uint8_t*>(malloc(size));
    if(mm){
        pool.insert(mm);
        H4AT_PRINT4("MBX GM %p len=%d POOL=%d\n",mm,size,pool.size());
    } else H4AT_PRINT4("********** MBX FAIL STATUS: FH=%u MXBLK=%u ASKED:%u\n",_HAL_freeHeap(),_HAL_maxHeapBlock(),size);
    return mm;
}