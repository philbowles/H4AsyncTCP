/*
 MIT License

Copyright (c) 2019 Phil Bowles <H48266@gmail.com>
   github     https://github.com/philbowles/H4
   blog       https://8266iot.blogspot.com
   groups     https://www.facebook.com/groups/esp8266questions/
              https://www.facebook.com/H4-Esp8266-Firmware-Support-2338535503093896/


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
#include<AardvarkUtils.h>
// non-member utils:
extern "C" {
    #include "user_interface.h"
}
void dumphex(const uint8_t* mem, size_t len) {
    if(mem && len){ // no-crash sanity
        auto W=16;
        uint8_t* src;
        memcpy(&src,&mem,sizeof(uint8_t*));
        Serial.printf("Address: 0x%08X len: 0x%X (%d)", (ptrdiff_t)src, len, len);
        for(uint32_t i = 0; i < len; i++) {
            if(i % W == 0) Serial.printf("\n[0x%08X] 0x%08X: ", (ptrdiff_t)src, i);
            Serial.printf("%02X ", *src);
            src++;
            //
            if(i % W == W-1 || src==mem+len){
                size_t ff=W-((src-mem) % W);
                for(int p=0;p<(ff % W);p++) Serial.print("   ");
                Serial.print("  "); // stretch this for nice alignment of final fragment
                for(uint8_t* j=src-(W-(ff%W));j<src;j++) Serial.printf("%c", isprint(*j) ? *j:'.');
                system_soft_wdt_feed();
            }
            system_soft_wdt_feed();
        }
        Serial.println();
    } else Serial.printf("ZERO PARAMETER!!! 0x%08x(%d)\n",mem,len);
}

string join(const vector<string>& vs,const char* delim) {
	string rv="";
	if(vs.size()){
		string sd(delim);
		for(auto const& v:vs) rv+=v+sd;
		for(int i=0;i<sd.size();i++) rv.pop_back();		
	}
	return rv;
}

string replaceAll(const string& s,const string& f,const string& r){
  string tmp=s;
  size_t pos = tmp.find(f);

  while( pos != string::npos){
    tmp.replace(pos, f.size(), r);
    pos =tmp.find(f, pos + r.size());
  }
return tmp;
}

vector<string> split(const string& s, const char* delimiter){
	vector<string> vt;
	string delim(delimiter);
	auto len=delim.size();
	auto start = 0U;
	auto end = s.find(delim);
	while (end != string::npos){
		vt.push_back(s.substr(start, end - start));
		start = end + len;
		end = s.find(delim, start);
	}
	string tec=s.substr(start, end);
	if(tec.size()) vt.push_back(tec);		
	return vt;
}

string stringFromBuff(const byte* data,size_t len){
   string rv(reinterpret_cast<const char*>(data), len); 
   return rv;
}

string stringFromInt(int i,const char* fmt){
	char buf[16];
	sprintf(buf,fmt,i);
	return string(buf);
}

bool stringIsAlpha(const string& s){ 
    return !(std::find_if(s.begin(), s.end(),[](char c) { return !std::isalpha(c); }) != s.end());
}

bool stringIsNumeric(const string& s){ 
    string abs=(s[0]=='-') ? string(++s.begin(),s.end()):s; // allow leading minus
    return all_of(abs.begin(), abs.end(), ::isdigit);
}

string lowercase(string s){
   string ucase(s);
   transform(ucase.begin(), ucase.end(),ucase.begin(), [](unsigned char c){ return std::tolower(c); } );
   return ucase;
}
string uppercase(string s){
   string ucase(s);
   transform(ucase.begin(), ucase.end(),ucase.begin(), [](unsigned char c){ return std::toupper(c); } );
   return ucase;
}

string rtrim(const string& s, const char d){
	string rv(s);
	while(rv.size() && (rv.back()==d)) rv.pop_back();
	return rv;	
}
string ltrim(const string& s, const char d){
	string rv(s);
	while(rv.size() && (rv.front()==d)) rv=string(++rv.begin(),rv.end());
	return rv;	
}
string trim(const string& s, const char d){ return(ltrim(rtrim(s,d),d)); }

string urlencode(const std::string &s){
    static const char lookup[]= "0123456789abcdef";
    std::string e;
    for(int i=0, ix=s.length(); i<ix; i++)
    {
        const char& c = s[i];
        if ( (48 <= c && c <= 57) ||//0-9
             (65 <= c && c <= 90) ||//abc...xyz
             (97 <= c && c <= 122) || //ABC...XYZ
             (c=='-' || c=='_' || c=='.' || c=='~') 
        ) e.push_back(c);
        else
        {
            e.push_back('%');
            e.push_back(lookup[ (c&0xF0)>>4 ]);
            e.push_back(lookup[ (c&0x0F) ]);
        }
    }
    return e;
}