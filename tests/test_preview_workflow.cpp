// GPL-3.0-only. Real host, owned and world-control policy; synthetic entity backend.
#include "preview_workflow_consumer.h"
#include "../src/game/services/world_control.h"
#include "../src/game/services/owned.h"
#include <sco/host.h>
#include <sco/runtime.h>
#include <cstdio>
using sco::game::services::WorldControl;
using sco::game::services::WorldControlProvider;
static int passed=0, failed=0;
#define CHECK(x) do { if(x) ++passed; else { ++failed; std::printf("FAIL %d: %s\n",__LINE__,#x); } } while(0)
static sco_plugin* owner=nullptr;
static bool live=true, present=false, available=true;
static uint64_t next_id=100, active=0; static double position=0, camera=7, saved=0;
static bool on_thread(){return true;} static bool is_live(const sco_plugin* p){return live && p==owner;}
static bool is_owned(const sco_plugin* p,uint64_t id){return is_live(p) && present && id==active;}
static sco_result ray(uint64_t,const double*,const double*,double,const uint64_t* ids,uint32_t n,sc_world_hit* h){h->distance=(n==1 && ids[0]==active)?5:2; h->pos[0]=h->distance;return SCO_OK;}
static sco_result begin(){saved=camera;camera=20;return SCO_OK;} static sco_result step(const sc_world_camera_step* s){camera+=s->translation[0];return SCO_OK;} static sco_result restore(){camera=saved;return SCO_OK;}
static WorldControl* control;
static sco_result ray_t(sco_plugin* p,uint64_t z,const double* f,const double* d,double m,const uint64_t* ids,uint32_t n,sc_world_hit* h){return control->Ray(p,z,f,d,m,ids,n,h);}
static sco_result acquire_t(sco_plugin* p,uint64_t* l){return control->Acquire(p,l);} static sco_result step_t(sco_plugin* p,uint64_t l,const sc_world_camera_step* s){return control->Step(p,l,s);} static sco_result release_t(sco_plugin* p,uint64_t l){return control->Release(p,l);}
static sco_result spawn(sco_plugin* p,const char*,uint64_t,const double*,const double*,uint64_t* out){*out=0;if(!is_live(p))return SCO_BAD_ARG;if(!available)return SCO_UNAVAILABLE;present=true;active=next_id++;*out=active;return SCO_OK;}
static sco_result move(sco_plugin* p,uint64_t id,uint64_t,const double* pos,const double*){if(!is_live(p))return SCO_BAD_ARG;if(!is_owned(p,id))return SCO_NOT_FOUND;position=pos[0];return SCO_OK;}
static sco_result despawn(sco_plugin* p,uint64_t id){if(!is_live(p))return SCO_BAD_ARG;if(!is_owned(p,id))return SCO_NOT_FOUND;present=false;return SCO_OK;}
namespace sco::game::services {
bool EntitiesOwnedBy(const void* p,uint64_t id){return is_owned(static_cast<const sco_plugin*>(p),id);}
bool NpcOwnedBy(const void*,uint64_t){return false;}
}
static void cleanup(const void* p){if(p==owner){CHECK(control->ReleaseOwner(p)==SCO_OK);present=false;live=false;}}
int main(){
 sco::SetGameThread(); const auto* api=sco::host::BuildApi({"synthetic-preview"}); owner=sco::host::NewPlugin("preview_workflow");
 auto* foreign=sco::host::NewPlugin("preview_foreign"); CHECK(api && owner && foreign);
 WorldControlProvider provider{on_thread,is_live,is_owned,ray,begin,step,restore}; WorldControl c(provider,true); control=&c;
 CHECK(sco::AddReleaseHook(cleanup)==sco::Result::Ok);
 sc_entities_v1 e{};e.size=sizeof(e);e.spawn=spawn;e.set_transform=move;e.despawn=despawn;
 CHECK(sco::host::ProvideGameService(SC_ENTITIES_NAME,SC_ENTITIES_VERSION_1_1,&e)==sco::Result::Ok);
 CHECK(sco::game::services::StartOwned()==sco::Result::Ok); const void* raw=nullptr;
 CHECK(api->query_service(SC_OWNED_NAME,SC_OWNED_VERSION_1_0,&raw)==SCO_OK); const auto* o=static_cast<const sc_owned_v1*>(raw);
 CHECK(o!=nullptr); if (!o) return 1;
 sc_world_control_v1 w{};w.size=sizeof(w);w.raycast_filtered=ray_t;w.camera_acquire=acquire_t;w.camera_step=step_t;w.camera_release=release_t;
 CHECK(sco::host::ProvideGameService(SC_WORLD_CONTROL_NAME,SC_WORLD_CONTROL_VERSION_1_0,&w)==sco::Result::Ok);
 const void* eraw=nullptr; const void* wraw=nullptr;
 CHECK(api->query_service(SC_ENTITIES_NAME,SC_ENTITIES_VERSION_1_0,&eraw)==SCO_OK);
 CHECK(api->query_service(SC_WORLD_CONTROL_NAME,SC_WORLD_CONTROL_VERSION_1_0,&wraw)==SCO_OK);
 Preview p{owner,static_cast<const sc_entities_v1*>(eraw),o,static_cast<const sc_world_control_v1*>(wraw)}; sc_world_hit hit{};
 CHECK(p.begin(1)==SCO_OK && p.id && camera==20); uint64_t old=p.id, lease=p.lease;
 sc_world_camera_step delta{};delta.size=sizeof(delta);delta.translation[0]=2;delta.rotation_xyzw[3]=1;
 CHECK(w.camera_step(owner,lease,&delta)==SCO_OK && camera==22);
 CHECK(p.move(1,hit)==SCO_OK && hit.pos[0]==5 && position==5); auto copied=hit;
 CHECK(p.retain()==SCO_OK && sco::game::services::BuildRetainedBy(owner,p.id)); uint64_t ticket=0;
 CHECK(o->remove_build(foreign,p.id,&ticket)==SCO_NOT_FOUND && ticket==0);
 CHECK(p.remove(ticket)==SCO_OK && ticket!=0 && !present); hit.pos[0]=-17; CHECK(copied.pos[0]==5 && hit.pos[0]==-17);
 sc_removal_receipt receipt{}; receipt.size=sizeof(receipt);
 CHECK(o->receipt(owner,ticket,&receipt)==SCO_OK && receipt.state==SC_REMOVAL_REQUESTED && receipt.entity_id==old);
 CHECK(o->receipt(foreign,ticket,&receipt)==SCO_NOT_FOUND);
 const double origin[3]{}, direction[3]{1,0,0}; CHECK(w.raycast_filtered(owner,1,origin,direction,100,&old,1,&hit)==SCO_FAILED);
 CHECK(p.cancel()==SCO_OK && camera==7); CHECK(w.camera_release(owner,lease)==SCO_NOT_FOUND);
 CHECK(p.begin(1)==SCO_OK); CHECK(p.cancel()==SCO_OK && !present && camera==7);
 CHECK(p.begin(1)==SCO_OK); e.despawn=nullptr; CHECK(p.cancel()==SCO_UNAVAILABLE && camera==7 && present && p.lease==0); e.despawn=despawn; CHECK(p.cancel()==SCO_OK && !present);
 CHECK(p.begin(1)==SCO_OK && p.retain()==SCO_OK); auto unload_id=p.id; auto unload_lease=p.lease;
 CHECK(sco::Release(owner)==sco::Result::Ok);
 CHECK(camera==7 && !present && !sco::game::services::BuildRetainedBy(owner,unload_id));
 CHECK(p.move(1,hit)==SCO_BAD_ARG); CHECK(o->receipt(owner,ticket,&receipt)==SCO_BAD_ARG);
 CHECK(sco::Release(owner)==sco::Result::BadArg); CHECK(unload_lease!=0);
 owner=foreign; live=true;
 WorldControlProvider absent{on_thread,is_live,is_owned}; WorldControl unavailable(absent,true);control=&unavailable; Preview missing{owner,&e,o,&w};
 CHECK(missing.begin(1)==SCO_UNAVAILABLE && missing.lease==0 && !present);
 control=&c; available=false; CHECK(missing.begin(1)==SCO_UNAVAILABLE && camera==7 && missing.lease==0); available=true;
 auto truncated=w; truncated.size=offsetof(sc_world_control_v1,camera_release); Preview short_table{owner,&e,o,&truncated}; CHECK(short_table.begin(1)==SCO_UNAVAILABLE && !present);
 auto null_table=w; null_table.camera_release=nullptr; Preview null_method{owner,&e,o,&null_table}; CHECK(null_method.begin(1)==SCO_UNAVAILABLE && !present);
 sco::game::services::StopOwned(); sco::RemoveReleaseHook(cleanup);
 std::printf("preview_workflow: %d passed, %d failed (synthetic entity backend)\n",passed,failed); return failed?1:0;
}


