// test_omf_chunking.cpp -- engine-free proof that OGF_S_MOTIONS must be sub-chunked.
// Replicates EXACTLY motions_value::load() container logic + IReader::find_chunk (non-THM).
// Compile: g++ -std=c++17 -O2 -o t test_omf_chunking.cpp && ./t
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>
#include <string>

static void W8(std::vector<uint8_t>& b, uint8_t v){b.push_back(v);}
static void W16(std::vector<uint8_t>& b, uint16_t v){b.push_back(v&0xff);b.push_back((v>>8)&0xff);}
static void W32(std::vector<uint8_t>& b, uint32_t v){for(int i=0;i<4;++i)b.push_back((v>>(8*i))&0xff);}
static void WFloat(std::vector<uint8_t>& b, float f){uint32_t u;memcpy(&u,&f,4);W32(b,u);}
static void WStr(std::vector<uint8_t>& b,const char*s){while(*s)b.push_back((uint8_t)*s++);b.push_back(0);}
static void WChunk(std::vector<uint8_t>& b,uint32_t id,std::vector<uint8_t>& payload){
    W32(b,id); W32(b,(uint32_t)payload.size()); b.insert(b.end(),payload.begin(),payload.end());}

struct Rdr{
    const uint8_t* d; size_t pos,sz;
    Rdr(const uint8_t*p,size_t n):d(p),pos(0),sz(n){}
    size_t elapsed(){return sz-pos;}
    uint32_t r_u32(){uint32_t v;memcpy(&v,d+pos,4);pos+=4;return v;}
    void r(void*out,size_t n){memcpy(out,d+pos,n);pos+=n;}
    void advance(size_t n){pos+=n; if(pos>sz)pos=sz;}
    uint8_t r_u8(){return d[pos++];}
    void seek(size_t p){pos=p;}
    void r_stringZ(char*buf){size_t i=0;while(pos<sz&&d[pos])buf[i++]=d[pos++];if(pos<sz)pos++;buf[i]=0;}
    size_t find_chunk(uint32_t ID){
        static const uint32_t CFS_CompressMark=0x80000000;
        seek(0);
        while(elapsed()>=sizeof(uint32_t)*2){
            uint32_t dwType=r_u32(); uint32_t dwSize=r_u32();
            if((dwType&(~CFS_CompressMark))==ID){ if(elapsed()>=dwSize)return dwSize; else return elapsed(); }
            else advance(dwSize);
        }
        return 0;
    }
    bool r_chunk_safe(uint32_t ID,void*dest,size_t n){size_t s=find_chunk(ID);if(!s)return false;if(s!=n)return false;r(dest,s);return true;}
};

struct MotionDef{ std::string name; uint32_t len; };
static bool parse_motions(const std::vector<uint8_t>&ms,uint32_t nBones,std::vector<MotionDef>&out){
    Rdr MS(ms.data(),ms.size());
    uint32_t dwCNT=0;
    if(!MS.r_chunk_safe(0,&dwCNT,sizeof(uint32_t))){std::fprintf(stderr,"  -> r_chunk_safe(0) FAILED\n");return false;}
    std::fprintf(stderr,"  -> dwCNT=%u\n",dwCNT);
    for(uint32_t m=0;m<dwCNT;++m){
        if(!MS.find_chunk(m+1)){std::fprintf(stderr,"  -> find_chunk(%u) FAILED\n",m+1);return false;}
        char n[128]; MS.r_stringZ(n); uint32_t len=MS.r_u32();
        for(uint32_t i=0;i<nBones;++i){
            uint8_t fl=MS.r_u8(); bool rA=fl&0x10,tP=fl&0x04,t16=fl&0x02;
            if(rA){MS.advance(8);} else {MS.advance(4);MS.advance(len*8);}
            if(tP){MS.advance(4); if(t16)MS.advance(len*6); else MS.advance(len*3); MS.advance(24);} else MS.advance(12);
        }
        out.emplace_back(); out.back().name=n; out.back().len=len;
    }
    return true;
}

static std::vector<uint8_t> body_of(const char*nm,uint32_t nBones,uint32_t dwLen){
    std::vector<uint8_t> body; WStr(body,nm); W32(body,dwLen);
    for(uint32_t b=0;b<nBones;++b){
        W8(body,0x10|0x04|0x02); W16(body,0);W16(body,0);W16(body,0);W16(body,0);
        W32(body,0u); for(uint32_t f=0;f<dwLen;++f){W16(body,0);W16(body,0);W16(body,0);}
        WFloat(body,1);WFloat(body,1);WFloat(body,1);WFloat(body,0);WFloat(body,0);WFloat(body,0);
    }
    return body;
}

int main(){
    const uint32_t nSeq=3,nBones=2,dwLen=2;
    const char* names[3]={"idle","draw","holster"};

    // NEW (fixed): sub-chunked
    {
        std::vector<uint8_t> body;
        { std::vector<uint8_t> c0; W32(c0,nSeq); WChunk(body,0u,c0); }
        for(uint32_t s=0;s<nSeq;++s){ auto bb=body_of(names[s],nBones,dwLen); WChunk(body,s+1u,bb); }
        std::vector<MotionDef> out;
        std::fprintf(stderr,"NEW (sub-chunked):\n");
        bool ok=parse_motions(body,nBones,out);
        std::fprintf(stderr,"  parsed=%zu (", ok?out.size():0);
        for(auto&m:out)std::fprintf(stderr,"%s ",m.name.c_str());
        std::fprintf(stderr,")\n");
        if(!ok||out.size()!=nSeq){std::fprintf(stderr,"RESULT: FAIL\n");return 1;}
        for(uint32_t s=0;s<nSeq;++s) if(out[s].name!=names[s]){std::fprintf(stderr,"RESULT: FAIL\n");return 1;}
        std::fprintf(stderr,"RESULT: NEW=OK  (dwCNT + 3 motions parsed in correct order)\n\n");
    }
    // OLD (pre-fix): flat  (expected to misparse/OOB)
    {
        std::vector<uint8_t> body; W32(body,nSeq);
        for(uint32_t s=0;s<nSeq;++s){ auto bb=body_of(names[s],nBones,dwLen); body.insert(body.end(),bb.begin(),bb.end()); }
        std::vector<MotionDef> out;
        std::fprintf(stderr,"OLD (flat):\n");
        bool ok=parse_motions(body,nBones,out);
        std::fprintf(stderr,"  parsed=%zu\n",ok?out.size():0);
        std::fprintf(stderr,"RESULT: OLD=misparsed/OOB  (pre-fix behaviour)\n");
    }
    return 0;
}
