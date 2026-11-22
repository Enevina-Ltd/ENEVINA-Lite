#include "pump/daikin_dsiot.hpp"
#include <cJSON.h>
#include <initializer_list>
#include <memory>

namespace enevina::dsiot {
namespace {
using Json=std::unique_ptr<cJSON,decltype(&cJSON_Delete)>;
using JsonText=std::unique_ptr<char,decltype(&cJSON_free)>;

Json document(std::string_view body) {
    Json out(nullptr,&cJSON_Delete);
    if(body.empty() || body.size()>max_document || body.find('\0')!=body.npos)return out;
    // Bound recursion in the native parser even when a local adapter returns bad JSON.
    unsigned depth=0;
    bool quoted=false,escaped=false;
    for(char c:body) {
        if(quoted) {
            if(escaped)escaped=false;
            else if(c=='\\')escaped=true;
            else if(c=='"')quoted=false;
        } else if(c=='"')quoted=true;
        else if(c=='{' || c=='[') {if(++depth>16)return out;}
        else if(c=='}' || c==']') {if(depth==0)return out;--depth;}
    }
    if(quoted || depth!=0)return out;
    const char* end=nullptr;
    out.reset(cJSON_ParseWithLengthOpts(body.data(),body.size(),&end,false));
    if(out && body.substr(static_cast<size_t>(end-body.data())).find_first_not_of(" \r\t\n")!=body.npos)
        out.reset();
    return out;
}
const cJSON* field(const cJSON* object,std::string_view key) {
    if(!cJSON_IsObject(object))return nullptr;
    const cJSON* result=nullptr;
    for(auto* item=object->child;item;item=item->next) {
        if(!item->string || key!=item->string)continue;
        if(result)return nullptr;
        result=item;
    }
    return result;
}
std::string_view text(const cJSON* object,std::string_view key) {
    const auto* value=field(object,key);
    return cJSON_IsString(value) && value->valuestring?value->valuestring:"";
}
const cJSON* response(const cJSON* root,std::string_view path,int result_code) {
    const auto* list=field(root,"responses");
    if(!cJSON_IsArray(list))return nullptr;
    const cJSON* match=nullptr;
    for(auto* reply=list->child;reply;reply=reply->next) {
        if(text(reply,"fr")!=path)continue;
        if(match)return nullptr;
        match=reply;
    }
    const auto* code=field(match,"rsc");
    return cJSON_IsNumber(code) && code->valuedouble==result_code?match:nullptr;
}
const cJSON* node(const cJSON* reply,std::initializer_list<std::string_view> path) {
    auto* value=field(reply,"pc");
    for(const auto name:path) {
        const auto* entries=field(value,"pch");
        if(!cJSON_IsArray(entries))return nullptr;
        value=nullptr;
        for(auto* item=entries->child;item;item=item->next) {
            if(text(item,"pn")!=name)continue;
            if(value)return nullptr;
            value=item;
        }
    }
    return value;
}
int hex_digit(char c) {
    if(c>='0' && c<='9')return c-'0';
    if(c>='a' && c<='f')return c-'a'+10;
    if(c>='A' && c<='F')return c-'A'+10;
    return -1;
}
std::optional<uint8_t> octet(std::string_view text) {
    if(text.size()!=2)return {};
    const int a=hex_digit(text[0]),b=hex_digit(text[1]);
    if(a<0 || b<0)return {};
    return static_cast<uint8_t>((a<<4)|b);
}
std::optional<double> degrees_from(std::string_view text,double divisor,double minimum,double maximum) {
    const auto raw=octet(text);
    if(!raw)return {};
    const double value=(*raw<128?*raw:static_cast<int>(*raw)-256)/divisor;
    return value>=minimum && value<=maximum?std::optional<double>{value}:std::nullopt;
}
std::string mac_from(std::string_view reported) {
    if(reported.size()!=12)return {};
    constexpr char digits[]="0123456789abcdef";
    std::string mac;
    for(const char c:reported) {
        const int n=hex_digit(c);
        if(n<0)return {};
        mac.push_back(digits[n]);
    }
    if(mac=="000000000000" || mac=="ffffffffffff")return {};
    return mac;
}
std::string model_name(std::string_view value) {
    if(value.empty() || value.size()>96 || value.size()%2!=0)return {};
    std::string model;
    for(size_t i=0;i<value.size();i+=2) {
        const auto c=octet(value.substr(i,2));
        if(!c || *c<32 || *c>126)return {};
        model.push_back(static_cast<char>(*c));
    }
    const auto begin=model.find_first_not_of(' ');
    return begin==model.npos?std::string{}:model.substr(begin,model.find_last_not_of(' ')-begin+1);
}
Mode mode_from(std::string_view code) {
    if(code=="0000")return Mode::Fan;
    if(code=="0100")return Mode::Heat;
    if(code=="0200")return Mode::Cooling;
    if(code=="0300")return Mode::Auto;
    if(code=="0500")return Mode::Dry;
    return Mode::Unknown;
}

cJSON* named(cJSON* children,const char* name,const char* value=nullptr) {
    if(!children)return nullptr;
    auto* item=cJSON_CreateObject();
    if(!item)return nullptr;
    if(!cJSON_AddStringToObject(item,"pn",name) ||
       (value && !cJSON_AddStringToObject(item,"pv",value)) ||
       !cJSON_AddItemToArray(children,item)) {
        cJSON_Delete(item);
        return nullptr;
    }
    return value?item:cJSON_AddArrayToObject(item,"pch");
}
}

std::string status_request() {
    return R"({"requests":[{"op":2,"to":"/dsiot/edge.adp_i"},
        {"op":2,"to":"/dsiot/edge/adr_0100.dgc_status?filter=pv,pt,md"},
        {"op":2,"to":"/dsiot/edge/adr_0200.dgc_status?filter=pv,pt,md"}]})";
}
std::optional<Reading> decode_status(std::string_view body) {
    const auto root=document(body);
    const auto* adapter=response(root.get(),identity_path,2000);
    const auto* indoor=response(root.get(),indoor_path,2000);
    const auto* outdoor=response(root.get(),outdoor_path,2000);
    if(text(field(adapter,"pc"),"pn")!="adp_i" ||
       text(field(indoor,"pc"),"pn")!="dgc_status")return {};
    const auto mac=mac_from(text(node(adapter,{"mac"}),"pv"));
    const auto firmware=text(node(adapter,{"ver"}),"pv");
    const auto api=text(node(adapter,{"api_ver"}),"pv");
    const auto power=text(node(indoor,{"e_1002","e_A002","p_01"}),"pv");
    if(mac.empty() || firmware.size()>32 || api.size()>16 || (power!="00" && power!="01"))return {};
    Reading result;
    result.device={mac,std::string(firmware),std::string(api),
        model_name(text(node(indoor,{"e_1002","e_A001","p_01"}),"pv"))};
    result.power=power=="01";
    result.mode=mode_from(text(node(indoor,{"e_1002","e_3001","p_01"}),"pv"));
    const char* target=result.mode==Mode::Heat?"p_03":result.mode==Mode::Cooling?"p_02":
        result.mode==Mode::Auto?"p_1D":nullptr;
    if(target)
        result.target_c=degrees_from(text(node(indoor,{"e_1002","e_3001",target}),"pv"),2.,10,35);
    result.indoor_c=degrees_from(text(node(indoor,{"e_1002","e_A00B","p_01"}),"pv"),1.,-10,55);
    if(text(field(outdoor,"pc"),"pn")=="dgc_status")
        result.outdoor_c=degrees_from(text(node(outdoor,{"e_1003","e_A00D","p_01"}),"pv"),2.,-55,63);
    return result;
}
std::optional<std::string> write_request(const Reading& before,const Patch& patch,
                                         const PumpHeatingLimits& limits) {
    if(!limits.heating || (!patch.power && !patch.target_c && !patch.heat) ||
       (patch.power==false && (!limits.off || patch.target_c || patch.heat)) ||
       (patch.power==true && !patch.heat) || (patch.heat && (!patch.target_c || patch.power!=true)) ||
       (patch.target_c && (!patch.heat && before.mode!=Mode::Heat)))return {};
    char target[3]{};
    if(patch.target_c) {
        const double halves=*patch.target_c*2;
        if(!limits.accepts(*patch.target_c) || halves<20 || halves>70 ||
           std::fabs(halves-std::round(halves))>.001)return {};
        constexpr char digits[]="0123456789ABCDEF";
        const auto raw=static_cast<uint8_t>(std::lround(halves));
        target[0]=digits[raw>>4];target[1]=digits[raw&15];
    }

    Json root(cJSON_CreateObject(),&cJSON_Delete);
    if(!root)return {};
    auto* requests=cJSON_AddArrayToObject(root.get(),"requests");
    Json item(cJSON_CreateObject(),&cJSON_Delete);
    if(!item || !requests || !cJSON_AddNumberToObject(item.get(),"op",3) ||
       !cJSON_AddStringToObject(item.get(),"to",indoor_path.data()))return {};
    auto* pc=cJSON_AddObjectToObject(item.get(),"pc");
    if(!pc || !cJSON_AddStringToObject(pc,"pn","dgc_status"))return {};
    auto* control=named(cJSON_AddArrayToObject(pc,"pch"),"e_1002");
    if(!control)return {};
    if(patch.target_c) {
        auto* mode=named(control,"e_3001");
        if(!mode || (patch.heat && !named(mode,"p_01","0100")) || !named(mode,"p_03",target))return {};
    }
    if(patch.power && !named(named(control,"e_A002"),"p_01",*patch.power?"01":"00"))return {};
    if(!cJSON_AddItemToArray(requests,item.get()))return {};
    item.release();
    const JsonText encoded(cJSON_PrintUnformatted(root.get()),&cJSON_free);
    return encoded?std::optional<std::string>{encoded.get()}:std::nullopt;
}
bool write_accepted(std::string_view body) {
    const auto root=document(body);
    return response(root.get(),indoor_path,2004)!=nullptr;
}
bool readback_matches(const Reading& before,const Reading& after,const Patch& patch) {
    return (patch.power || patch.heat || patch.target_c) && before.device.mac==after.device.mac &&
        before.device.firmware==after.device.firmware && before.device.model==after.device.model &&
        after.power==patch.power.value_or(before.power) && after.mode==(patch.heat?Mode::Heat:before.mode) &&
        (!patch.target_c || (after.target_c && std::fabs(*after.target_c-*patch.target_c)<.001));
}
PumpRoomMode heating_mode(const Reading& reading) {
    if(!reading.power)return PumpRoomMode::Off;
    if(reading.mode==Mode::Unknown || (reading.mode==Mode::Heat && !reading.target_c))
        return PumpRoomMode::Unknown;
    return reading.mode==Mode::Heat?PumpRoomMode::Heat:PumpRoomMode::Other;
}
}
