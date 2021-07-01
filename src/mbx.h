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
#pragma once
#include<AardvarkTCP.h>
#include<unordered_set>

using VARK_MEM_POOL         = std::unordered_set<uint8_t*>;

class mbx {
                void            _create(uint8_t* p);
    public:
        static  VARK_MEM_POOL   pool;

                bool            managed=false;
                size_t          len=0;
                uint8_t*        data=nullptr;
                uint8_t*        frag=nullptr;

        mbx(){}
        mbx(uint8_t* p,size_t s,bool copy=true): frag(nullptr),len(s),managed(copy){ _create(p); }
        mbx(uint8_t* p,uint8_t* f,size_t s,bool copy=true): frag(f),len(s),managed(copy){ _create(p); }
        // 
        virtual ~mbx(){} // absolutely do not never nohow free the data pointer here! It must stay alive till it is ACKed
        virtual void            ack();
                void            clear();
        static  void            clear(uint8_t*);
        static  void            emptyPool();
        static  uint8_t*        getMemory(size_t size);
#if VARK_DEBUG 
        static  void            dump(size_t n=64);
                void            _dump(size_t n=10000);
#endif
};