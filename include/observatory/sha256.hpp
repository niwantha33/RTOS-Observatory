// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace observatory::hash_detail {
class Sha256 {
    std::array<std::uint32_t,8> state_{
        0x6a09e667u,0xbb67ae85u,0x3c6ef372u,0xa54ff53au,
        0x510e527fu,0x9b05688cu,0x1f83d9abu,0x5be0cd19u};
    std::array<std::uint8_t,64> buffer_{};
    std::uint64_t bytes_{};
    std::size_t fill_{};
    static constexpr std::array<std::uint32_t,64> k_{
        0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,
        0x923f82a4u,0xab1c5ed5u,0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,
        0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,0xe49b69c1u,0xefbe4786u,
        0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
        0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,
        0x06ca6351u,0x14292967u,0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,
        0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,0xa2bfe8a1u,0xa81a664bu,
        0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
        0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,
        0x5b9cca4fu,0x682e6ff3u,0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,
        0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u};
    static std::uint32_t rotate(std::uint32_t x,unsigned n) {
        return (x>>n)|(x<<(32u-n));
    }
    void transform(const std::uint8_t* b) {
        std::array<std::uint32_t,64> w{};
        for(std::size_t i=0;i<16;++i) {
            const auto j=4*i;
            w[i]=(static_cast<std::uint32_t>(b[j])<<24)|
                 (static_cast<std::uint32_t>(b[j+1])<<16)|
                 (static_cast<std::uint32_t>(b[j+2])<<8)|
                  static_cast<std::uint32_t>(b[j+3]);
        }
        for(std::size_t i=16;i<64;++i) {
            const auto a=w[i-15],z=w[i-2];
            const auto x=rotate(a,7)^rotate(a,18)^(a>>3);
            const auto y=rotate(z,17)^rotate(z,19)^(z>>10);
            w[i]=w[i-16]+x+w[i-7]+y;
        }
        auto a=state_[0],b0=state_[1],c=state_[2],d=state_[3];
        auto e=state_[4],f=state_[5],g=state_[6],h=state_[7];
        for(std::size_t i=0;i<64;++i) {
            const auto s1=rotate(e,6)^rotate(e,11)^rotate(e,25);
            const auto ch=(e&f)^((~e)&g);
            const auto t1=h+s1+ch+k_[i]+w[i];
            const auto s0=rotate(a,2)^rotate(a,13)^rotate(a,22);
            const auto maj=(a&b0)^(a&c)^(b0&c);
            const auto t2=s0+maj;
            h=g;g=f;f=e;e=d+t1;d=c;c=b0;b0=a;a=t1+t2;
        }
        const std::array<std::uint32_t,8> delta{a,b0,c,d,e,f,g,h};
        for(std::size_t i=0;i<8;++i)state_[i]+=delta[i];
    }
public:
    void update(const std::uint8_t* data,std::size_t n) {
        for(std::size_t i=0;i<n;++i) {
            buffer_[fill_++]=data[i];
            if(fill_==64) {
                transform(buffer_.data());
                fill_=0;
            }
        }
        bytes_+=static_cast<std::uint64_t>(n);
    }
    void update(std::string_view s) {
        update(reinterpret_cast<const std::uint8_t*>(s.data()),s.size());
    }
    std::string digest() {
        const auto bits=bytes_*8u;
        std::uint8_t marker=0x80;
        update(&marker,1);
        const std::uint8_t zero=0;
        while(fill_!=56)update(&zero,1);
        std::array<std::uint8_t,8> count{};
        for(int i=0;i<8;++i)count[static_cast<std::size_t>(i)]=
            static_cast<std::uint8_t>(bits>>(56-8*i));
        update(count.data(),count.size());
        constexpr char hex[]="0123456789abcdef";
        std::string result;
        result.reserve(64);
        for(const auto w:state_)
            for(int shift=28;shift>=0;shift-=4)
                result+=hex[(w>>shift)&0xfu];
        return result;
    }
};
inline std::string sha256(std::string_view bytes) {
    Sha256 hasher;
    hasher.update(bytes);
    return hasher.digest();
}
} // namespace observatory::hash_detail
