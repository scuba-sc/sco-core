// SPDX-License-Identifier: GPL-3.0-only
#include "sco/host.h"
#include "sco/runtime.h"
#include "sco/widgets.h"
#include <cstdio>
#include <limits>

static int passed=0, failed=0, events=0;
static double last=0;
#define CHECK(c) do { if(c) ++passed; else { ++failed; std::printf("FAIL %d: %s\n",__LINE__,#c); } } while(0)
static void OnEvent(const char*,const sco_widget_event* e,void*) { ++events;last=e->value; }
struct Renderer : sco::widgets::Renderer {
    double value=0;
    void Render(const sco::widgets::Panel& panel,std::vector<sco::widgets::Event>& out) override {
        sco::widgets::Event event;event.node_id=panel.nodes[0].id;event.kind=SCO_WIDGET_SLIDER;event.value=value;
        out.push_back(event);
    }
};
int main() {
    sco::SetGameThread();const auto* api=sco::host::BuildApi({"numeric test"});
    auto* self=sco::host::NewPlugin("numeric");
    CHECK(api && self && sco::widgets::Start()==sco::Result::Ok);
    const void* found=nullptr;
    CHECK(api->query_service(SCO_WIDGETS_NAME,SCO_WIDGETS_VERSION_1_3,&found)==SCO_OK);
    const auto* widgets=static_cast<const sco_widgets_v1*>(found);
    uint32_t caps[SCO_WIDGETS_CAPS_1_2]={SCO_WIDGETS_CAPS_1_2};
    CHECK(widgets->capabilities(self,caps)==SCO_OK);
    CHECK((caps[SCO_WIDGETS_CAP_FEATURES]&SCO_WIDGETS_FEATURE_NUMERIC_PRESENTATION)!=0);
    sco_widget_node_1_3 node{};auto& base=node.base.base.base;
    base.size=sizeof(node);base.kind=SCO_WIDGET_SLIDER;base.id=1;base.label="Darkness";
    base.minimum=20;base.maximum=95;base.value=74;node.presentation=SCO_WIDGET_NUMERIC_INTEGER_PERCENT;
    auto set=[&] { return widgets->set_panel(self,"numeric.main","Menu",&base,1,sizeof(node),OnEvent,nullptr); };
    CHECK(set()==SCO_OK);
    Renderer renderer;
    for(int i=20;i<=95;++i) {renderer.value=i;CHECK(sco::widgets::RenderPanel("numeric.main",renderer)==sco::Result::Ok);CHECK(last==i);}
    CHECK(events==76);
    for(double invalid : {20.5,19.0,96.0,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}) {
        renderer.value=invalid;CHECK(sco::widgets::RenderPanel("numeric.main",renderer)==sco::Result::Ok);CHECK(events==76);
    }
    node.presentation=99;CHECK(set()==SCO_BAD_ARG);
    node.presentation=SCO_WIDGET_NUMERIC_INTEGER_PERCENT;node._reserved[1]=1;CHECK(set()==SCO_BAD_ARG);node._reserved[1]=0;
    base.value=74.5;CHECK(set()==SCO_BAD_ARG);base.value=74;
    base.minimum=-2147483649.0;CHECK(set()==SCO_BAD_ARG);base.minimum=20;
    base.kind=SCO_WIDGET_TOGGLE;base.value=1;base.minimum=0;base.maximum=0;CHECK(set()==SCO_BAD_ARG);
    // The failed replacements leave the previously published slider intact.
    renderer.value=21;CHECK(sco::widgets::RenderPanel("numeric.main",renderer)==sco::Result::Ok && events==77 && last==21);
    sco_widget_node old{};old.size=sizeof(old);old.kind=SCO_WIDGET_SLIDER;old.id=2;old.label="Legacy";
    old.minimum=0;old.maximum=1;old.value=0.5;
    CHECK(widgets->set_panel(self,"numeric.main","Legacy",&old,1,sizeof(old),OnEvent,nullptr)==SCO_OK);
    renderer.value=0.25;CHECK(sco::widgets::RenderPanel("numeric.main",renderer)==sco::Result::Ok && events==78 && last==0.25);
    CHECK(sco::Release(self)==sco::Result::Ok);sco::widgets::Stop();
    std::printf("widget numeric: %d passed, %d failed\n",passed,failed);return failed?1:0;
}
