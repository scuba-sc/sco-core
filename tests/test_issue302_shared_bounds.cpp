#include "../src/game/sig_rows.h"
#include <cassert>
#include <cstring>
#include <limits>
#include <cstdio>
#include "issue302_test_support.h"
namespace sco { static uint8_t* source; uint8_t* Sig(const char*) { return source; } }
constexpr sco::game::rows::RipSpec spec{"source", 0, 3, 7};
int main(int argc,char**) {
 using namespace sco; using namespace sco::game::rows;
 issue302::GuardPages memory;
 const size_t page=memory.page;
 auto* p=memory.data;
 Image img{}; img.base=p; img.size=static_cast<uint32_t>(page); img.text={p,page};
 source=p+page-3;
 if(argc>1) { auto r=ResolveRip<spec>(img); assert(r.state==SigState::Failed); return 0; }
 uint8_t exact[]={0x48,0x8d,0x05,0,0,0,0}; uint8_t* hit=nullptr;
 assert(FindPattern({exact,7},"48 8D 05 ?? ?? ?? ??",&hit,1)==1);
 assert(FindRipLea({exact,7},0x48,0x8d,5,exact+7)==exact);
 source=p; std::memset(p,0,7);
 auto valid=ResolveRip<spec>(img); assert(valid.state==SigState::Ok && valid.at==p+7);
 img.base=nullptr; img.size=0; assert(ResolveRip<spec>(img).state==SigState::Failed);
 img.base=p; img.size=static_cast<uint32_t>(page);
 source=p+page-3; auto truncated=ResolveRip<spec>(img); assert(truncated.state==SigState::Failed); assert(std::strcmp(truncated.why,"RIP source instruction outside .text")==0);
 puts("issue302 shared/scanner PASS");
}
