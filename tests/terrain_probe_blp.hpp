#pragma once
// Generated BLP2 bytes, passed through the production parser. No game files.
static std::vector<uint8_t> generatedBLP2(int variant){
    std::vector<uint8_t> bytes(1172,0);
    auto u32=[&](size_t at,uint32_t value){for(int i=0;i<4;++i)bytes[at+i]=uint8_t(value>>(8*i));};
    bytes[0]='B';bytes[1]='L';bytes[2]='P';bytes[3]='2';u32(4,1);
    bytes[8]=2;bytes[9]=variant==0?0:8;bytes[10]=variant==0?0:(variant==1?1:7);bytes[11]=1;
    u32(12,512);u32(16,512);
    for(uint32_t size=512,level=0;;size=std::max(1u,size/2),++level){
        const uint32_t blockBytes=variant==0?8:16;
        const uint32_t count=((size+3)/4)*((size+3)/4);
        u32(20+level*4,uint32_t(bytes.size()));u32(84+level*4,count*blockBytes);
        std::array<uint8_t,16> block{};
        const int color=variant==0?0:8;
        if(variant==1)std::fill_n(block.begin(),8,255); // DXT3 opaque alpha
        if(variant==2)block[0]=255; // DXT5 opaque alpha index 0
        block[color]=0;block[color+1]=0xf8; // red RGB565, all selectors zero
        block[color+2]=0xe0;block[color+3]=7;
        for(uint32_t i=0;i<count;++i)bytes.insert(bytes.end(),block.begin(),block.begin()+blockBytes);
        if(size==1)break;
    }
    return bytes;
}
