// SPDX-License-Identifier: GPL-3.0-only
#include "sco/app.h"
#include "sco/host_menu.h"
#include "sco/host.h"
#include "sco/widgets.h"
#include "sco_content.h"
#include <cstdio>
static int passed=0, failed=0;
#define CHECK(c) do { if(c) ++passed; else { ++failed; std::printf("FAIL %d: %s\n",__LINE__,#c); } } while(0)
static sco::app::HostMenuContext seen{};
static unsigned loads=0, frames=0, stops=0;
enum class Mode { Normal, Refuse, LoadFault, FrameFault, StopFault, ExitFault };
static Mode mode = Mode::Normal;
static const void* watched = nullptr;
static int releases = 0;
static bool injectFault = false;
static bool faultRelease = false;
static void CountRelease(const void* owner) { if(owner==watched) { ++releases; if(faultRelease) throw 1; } }
static void FaultOnExit(const char*,const void*,void*) { throw 1; }
static uint32_t FaultGuard(void (*thunk)(void*),void* ctx) {
 if(injectFault) { injectFault=false; return 0xc0000005u; }
 thunk(ctx); return 0;
}
static sco_result Load(const sco::app::HostMenuContext& c,void*) {
 seen=c; ++loads;
 if(mode==Mode::LoadFault) throw 1;
 const void* found=nullptr;
 if(c.api->query_service(SCO_WIDGETS_NAME,SCO_WIDGETS_VERSION_1_0,&found)!=SCO_OK)return SCO_UNAVAILABLE;
 sco_widget_node n{};n.size=sizeof(n);n.kind=SCO_WIDGET_LABEL;n.id=1;n.label="Host settings";
 const auto result=static_cast<const sco_widgets_v1*>(found)->set_panel(c.self,"menu.main","Menu",&n,1,sizeof(n),nullptr,nullptr);
 if(mode==Mode::ExitFault && result==SCO_OK)
  return c.api->subscribe(c.self,"game.exit",FaultOnExit,nullptr);
 return mode==Mode::Refuse ? SCO_UNAVAILABLE : result;
}
static void Frame(const sco::app::HostMenuContext& c,void*,void* request) {
 CHECK(request==&frames);CHECK(sco::app::HostMenuActive(c));++frames;
 if(mode==Mode::FrameFault) throw 1;
}
static void Stop(const sco::app::HostMenuContext&,void*) { ++stops;if(mode==Mode::StopFault) throw 1; }
int main(){
 sco::app::Platform p;p.pluginsEnabled=false;p.hostMenu={nullptr,Load,Frame,Stop};
 CHECK(sco::host::NewPlugin("menu")==nullptr);
 sco::SetGameThread();
 static const uint32_t collision[2]={sizeof(collision),0};
 CHECK(sco::host::ProvideHostService(SCO_CONTENT_NAME,SCO_CONTENT_VERSION_1_0,collision)==sco::Result::Ok);
 CHECK(!sco::app::Start(p));CHECK(loads==0);CHECK(!sco::app::HostMenuFrame(&frames));
 CHECK(sco::app::Start(p));CHECK(loads==1);CHECK(seen.self && sco::app::HostMenuActive(seen));
 CHECK(seen.generation==1);
 CHECK(sco::app::IsHostMenuPanel("menu.main"));CHECK(!sco::app::IsHostMenuPanel("menu.status"));
 CHECK(sco::app::HostMenuFrame(&frames));CHECK(frames==1);
 auto old=seen;sco::app::Stop();CHECK(stops==1);CHECK(!sco::app::HostMenuActive(old));CHECK(sco::widgets::Panels().empty());
 CHECK(sco::app::Start(p));CHECK(seen.self!=old.self);CHECK(seen.generation!=old.generation);CHECK(!sco::app::HostMenuActive(old));
 sco::app::Stop();
 mode=Mode::Refuse;
 CHECK(sco::app::Start(p));CHECK(!sco::app::HostMenuActive(seen));
 CHECK(sco::widgets::Panels().empty());CHECK(stops==3);sco::app::Stop();
 mode=Mode::FrameFault;
 CHECK(sco::app::Start(p));auto faulted=seen;
 CHECK(!sco::app::HostMenuFrame(&frames));CHECK(!sco::app::HostMenuActive(faulted));
 CHECK(sco::widgets::Panels().empty());CHECK(stops==3);sco::app::Stop();
 mode=Mode::LoadFault;
 CHECK(sco::app::Start(p));CHECK(!sco::app::HostMenuActive(seen));CHECK(stops==3);sco::app::Stop();
 mode=Mode::StopFault;
 CHECK(sco::app::Start(p));auto stopped=seen;sco::app::Stop();
 CHECK(stops==4);CHECK(!sco::app::HostMenuActive(stopped));CHECK(sco::widgets::Panels().empty());
 mode=Mode::Normal;
 CHECK(sco::app::Start(p));CHECK(sco::app::HostMenuActive(seen));
 CHECK(!sco::app::HostMenuActive(old));CHECK(!sco::app::HostMenuActive(faulted));
 sco::app::Stop();
 mode=Mode::ExitFault;
 CHECK(sco::app::Start(p));watched=seen.self;
 CHECK(sco::AddReleaseHook(CountRelease)==sco::Result::Ok);
 sco::app::Stop(); // game.exit faults; cleanup must run without a later Tick
 CHECK(releases==1);CHECK(stops==5);CHECK(sco::widgets::Panels().empty());
 CHECK(sco::RemoveReleaseHook(CountRelease)==sco::Result::Ok);
 mode=Mode::Normal;
 CHECK(sco::app::Start(p));injectFault=true;sco::plugins::SetCallGuard(FaultGuard);
 CHECK(!sco::app::HostMenuFrame(&frames));CHECK(sco::widgets::Panels().empty());
 sco::plugins::SetCallGuard(nullptr);sco::app::Stop();CHECK(stops==5);
 CHECK(sco::app::Start(p));CHECK(sco::app::HostMenuActive(seen));sco::app::Stop();
 CHECK(sco::app::Start(p));auto aborted=seen;sco::app::HostMenuAbort();
 CHECK(!sco::app::HostMenuActive(aborted));CHECK(sco::widgets::Panels().empty());sco::app::Stop();
 CHECK(sco::app::Start(p));watched=seen.self;
 CHECK(sco::AddReleaseHook(CountRelease)==sco::Result::Ok);faultRelease=true;
 sco::app::Stop();faultRelease=false;CHECK(releases==2);
 CHECK(sco::RemoveReleaseHook(CountRelease)==sco::Result::Ok);
 const auto loadsBeforeBlockedRestart=loads;
 CHECK(sco::app::Start(p));CHECK(loads==loadsBeforeBlockedRestart);
 CHECK(!sco::app::HostMenuFrame(&frames));sco::app::Stop();
 std::printf("host_menu: %d passed, %d failed\n",passed,failed);return failed?1:0;
}
