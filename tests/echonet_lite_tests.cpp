#include "pump/echonet_lite.hpp"
#include "pump/adapter_catalog.hpp"
#include <cassert>
#include <initializer_list>
#include <iostream>
#include <vector>
using namespace enevina::echonet;
const Object ac{1,0x30,1};
Packet response(const Packet& request,Service service,std::initializer_list<std::vector<Byte>> props) {
    auto q=parse(request);assert(q);
    Packet p{};
    const Byte head[]{0x10,0x81,Byte(q->tid>>8),Byte(q->tid),q->destination[0],q->destination[1],q->destination[2],
       q->source[0],q->source[1],q->source[2],Byte(service),Byte(props.size())};
    for(Byte b:head)p.bytes[p.size++]=b;
    for(const auto& prop:props)for(Byte b:prop)p.bytes[p.size++]=b;
    return p;
}
int main(){
    const auto* entry=enevina::find_adapter("echonet_lite_lan");
    assert(entry && entry->maturity==enevina::AdapterMaturity::FirmwareControlExperimental);
    assert(enevina::adapter_selectable_in_lite_firmware(entry->id));
    assert(!enevina::adapter_allowed_cn105_physical_writes(entry->id));
    auto q=discover(0x1234);assert(q && q->size==14);
    const std::array<Byte,14> expected{0x10,0x81,0x12,0x34,5,0xff,1,0x0e,0xf0,1,0x62,1,0xd6,0};
    for(size_t i=0;i<expected.size();++i)assert(q->bytes[i]==expected[i]);
    auto d=response(*q,Service::GetRes,{{0xd6,10,3,1,0x30,1,2,0x90,1,1,0x30,2}});
    auto devices=discovery_result(*parse(*q),*parse(d));
    assert(devices && devices->count==2 && devices->objects[1][2]==2);
    for(size_t n=0;n<d.size;++n)assert(!parse(d.bytes.data(),n));
    auto bad=d;bad.bytes[3]^=1;assert(!discovery_result(*parse(*q),*parse(bad)));
    bad=d;bad.bytes[7]^=1;assert(!discovery_result(*parse(*q),*parse(bad)));
    bad=d;bad.bytes[10]=0x52;assert(!parse(bad));
    bad=d;bad.bytes[14]=4;assert(!discovery_result(*parse(*q),*parse(bad)));
    bad=d;bad.bytes[23]=1;assert(!discovery_result(*parse(*q),*parse(bad)));
    bad=d;++bad.size;assert(!parse(bad));
    auto cq=read_capabilities(2,ac);assert(cq);
    auto cr=response(*cq,Service::GetRes,{{0x9e,4,3,0x80,0xb0,0xb3},
        {0x9f,8,7,0x80,0xb0,0xb3,0xbb,0xbe,0xba,0x88}});
    auto cap=capabilities(*parse(*cq),*parse(cr));assert(cap);
    assert(cap->writable.has(0xb3) && !cap->writable.has(0xbb) && cap->readable.has(0xbb));
    Byte bits[17]{};bits[0]=16;for(int i=1;i<=16;++i)bits[i]=0x08;
    auto bitmap=property_map({0x9f,bits,17});assert(bitmap && bitmap->has(0xb0) && bitmap->has(0xbf) && !bitmap->has(0x80));
    bits[0]=17;assert(!property_map({0x9f,bits,17}));bits[0]=16;
    Byte dup[]{2,0x80,0x80};assert(!property_map({0x9f,dup,3}));
    auto sq=read_state(3,ac,*cap);assert(sq);
    auto sr=response(*sq,Service::GetRes,{{0x80,1,0x30},{0xb0,1,0x43},{0xb3,1,21},
        {0xbb,1,19},{0xbe,1,0xf6},{0xba,1,55},{0x88,1,0x42}});
    auto s=state(*parse(*sq),*parse(sr));assert(s && s->power && s->mode==Mode::Heat &&
        s->setpoint_c==21 && s->indoor_c==19 && s->outdoor_c==-10 && s->humidity_pct==55 && s->fault==false);
    bad=sr;bad.bytes[14]=0x32;assert(!state(*parse(*sq),*parse(bad)));
    bad=sr;bad.bytes[23]=0x80;auto unknown=state(*parse(*sq),*parse(bad));assert(unknown && !unknown->indoor_c);
    bad=sr;bad.bytes[26]=0x7e;unknown=state(*parse(*sq),*parse(bad));assert(unknown && !unknown->outdoor_c);
    bad=sr;bad.bytes[15]=0x80;assert(!parse(bad)); // duplicate EPC
    bad=sr;bad.bytes[12]=0x81;assert(!state(*parse(*sq),*parse(bad)));
    ModelLimits lim{16,30,true,true,true,false,false,false};
    Desired desired{true,Mode::Cool,24};
    auto cmd=command(4,ac,*cap,lim,desired);assert(cmd);
    assert(cmd->bytes[10]==0x61 && cmd->bytes[11]==3 && cmd->bytes[12]==0xb0 && cmd->bytes[14]==0x42 &&
           cmd->bytes[15]==0xb3 && cmd->bytes[17]==24 && cmd->bytes[18]==0x80 && cmd->bytes[20]==0x30);
    auto ack=response(*cmd,Service::SetRes,{{0xb0,0},{0xb3,0},{0x80,0}});
    assert(matches(*parse(*cmd),*parse(ack)) && !confirmed(desired,*s));
    auto applied=*s;applied.mode=Mode::Cool;applied.setpoint_c=24;assert(confirmed(desired,applied));
    applied.power=false;assert(!confirmed(desired,applied));
    bad=ack;bad.bytes[10]=0x51;assert(!parse(bad));
    assert(!command(5,ac,*cap,lim,Desired{true,Mode::Dry,24}));
    assert(!command(5,ac,*cap,lim,Desired{true,Mode::Heat,31}));
    assert(!command(5,ac,*cap,ModelLimits{},desired));
    auto restricted=*cap;restricted.writable.codes[0xb3-0x80]=false;
    assert(!command(5,ac,restricted,lim,desired));
    restricted=*cap;restricted.readable.codes[0xb3-0x80]=false;
    assert(!command(5,ac,restricted,lim,desired));
    assert(!command(5,node_profile,*cap,lim,desired));
    assert(!command(5,ac,*cap,lim,Desired{}));
    assert(!confirmed(Desired{},applied));
    const auto off=command(6,ac,*cap,lim,Desired{false,{},{}});assert(off && off->bytes[14]==0x31);
    const Byte repeated[]{0x80,0x80};assert(!get(7,ac,repeated,2));
    assert(!get(7,{1,0x30,0},repeated,1));
    std::cout<<"ECHONET codec discovery, capability maps, state, command, readback and malformed-packet checks passed\n";
}
