#include "pump/daikin_legacy.hpp"
#include <array>
#include <cmath>
#include <utility>

namespace enevina::daikin {
namespace {
using namespace std::literals;
std::string_view trim(std::string_view s) {
    constexpr auto space=" \t\r\n"sv;
    const auto first=s.find_first_not_of(space);
    if(first==s.npos)return {};
    return s.substr(first,s.find_last_not_of(space)-first+1);
}
bool field_name(std::string_view text) {
    if(text.empty() || text.size()>24)return false;
    for(const char c:text)
        if(!((c>='a' && c<='z') || (c>='0' && c<='9') || c=='_'))return false;
    return true;
}
struct Fields {
    std::array<std::pair<std::string_view,std::string_view>,80> fields{};
    size_t count=0;
    std::optional<std::string_view> get(std::string_view key) const {
        for(size_t i=0;i<count;++i)
            if(fields[i].first==key)return fields[i].second;
        return {};
    }
    std::string parameter(std::string_view key) const {
        return std::string(get(key).value_or(""sv));
    }
};
std::optional<Fields> fields(std::string_view input) {
    if(input.empty() || input.size()>max_body || input.find('\0')!=input.npos)return {};
    Fields out;
    input=trim(input);
    while(!input.empty()) {
        const auto comma=input.find(',');
        const auto segment=input.substr(0,comma);
        const auto equal=segment.find('=');
        if(equal==segment.npos || out.count>=out.fields.size())return {};
        const auto key=segment.substr(0,equal),value=segment.substr(equal+1);
        if(!field_name(key) || out.get(key))return {};
        out.fields[out.count++]={key,value};
        if(comma==input.npos)break;
        input.remove_prefix(comma+1);
    }
    if(out.get("ret")!="OK")return {};
    return out;
}

bool ascii_digit(char c) {return c>='0' && c<='9';}
std::optional<double> measure(std::optional<std::string_view> field,double low,double high) {
    if(!field || field->empty() || field->size()>10)return {};
    auto text=*field;
    const bool negative=text.front()=='-';
    if(negative)text.remove_prefix(1);
    if(text.empty() || !ascii_digit(text.front()))return {};
    double value=0,place=0;
    for(const char c:text) {
        if(c=='.' && place==0) {place=1;continue;}
        if(!ascii_digit(c))return {};
        const int n=c-'0';
        if(place==0)value=value*10+n;
        else {place*=.1;value+=n*place;}
    }
    if(negative)value=-value;
    if(value<low || value>high)return {};
    return value;
}
std::optional<std::string> mac(std::string_view text) {
    if(text.size()!=12)return {};
    std::string address(text);
    for(char& c:address) {
        if(c>='A' && c<='F')c=static_cast<char>(c+'a'-'A');
        if(!ascii_digit(c) && (c<'a' || c>'f'))return {};
    }
    if(address=="000000000000" || address=="ffffffffffff")return {};
    return address;
}
bool safe(std::string_view text) {
    if(text.empty() || text.size()>12)return false;
    for(const char c:text)
        if(!ascii_digit(c) && !(c>='A' && c<='Z') && !(c>='a' && c<='z') &&
           c!='-' && c!='.')return false;
    return true;
}
}

std::optional<Device> decode_identity(std::string_view reply) {
    const auto f=fields(reply);
    if(!f || f->get("type")!="aircon")return {};
    const auto field=f->get("mac");
    const auto address=field?mac(*field):std::nullopt;
    if(!address)return {};
    const auto ver=f->parameter("ver"),model=f->parameter("model");
    if(ver.size()>32 || model.size()>48)return {};
    return Device{*address,ver,model};
}
std::optional<Reading> decode_state(std::string_view reply) {
    const auto f=fields(reply);
    if(!f)return {};
    const auto power=f->get("pow"),mode=f->get("mode");
    if((power!="0" && power!="1") || !mode || mode->size()!=1 ||
       mode->front()<'0' || mode->front()>'7')return {};
    Reading state;
    state.power=power=="1";state.mode=static_cast<unsigned>(mode->front()-'0');
    state.target_c=measure(f->get("stemp"),8,40);
    state.target=f->parameter("stemp");
    state.humidity=f->parameter("shum");
    state.fan=f->parameter("f_rate");
    state.swing=f->parameter("f_dir");
    state.heat_humidity=f->parameter("dh4");
    state.heat_fan=f->parameter("dfr4");
    state.heat_swing=f->parameter("dfd4");
    return state;
}
std::optional<Temperatures> decode_sensors(std::string_view reply) {
    const auto f=fields(reply);
    if(!f)return {};
    return Temperatures{measure(f->get("htemp"),-15,55),
        measure(f->get("otemp"),-55,65),measure(f->get("cmpfreq"),0,200)};
}
std::optional<std::string> set_request(const Reading& current,const Patch& patch,
                                       const PumpHeatingLimits& limits) {
    if(!limits.heating || (!patch.power && !patch.heat_target_c && !patch.heat_mode) ||
       (patch.power==false && (!limits.off || patch.heat_target_c || patch.heat_mode)) ||
       (patch.heat_target_c && (!(patch.heat_mode || current.mode==4) ||
                                !limits.accepts(*patch.heat_target_c))))return {};
    const auto mode=patch.heat_mode?4u:current.mode;
    if(mode>7)return {};
    std::string target=current.target;
    if(patch.heat_target_c) {
        const double halves=*patch.heat_target_c*2;
        if(std::fabs(halves-std::round(halves))>0.001)return {};
        const int nearest=static_cast<int>(std::lround(halves));
        target=std::to_string(nearest/2);
        if(nearest%2)target+=".5";
    }
    const bool transition=patch.heat_mode && current.mode!=4;
    const auto& humidity=transition?current.heat_humidity:current.humidity;
    const auto& fan=transition && !current.fan.empty()?current.heat_fan:current.fan;
    const auto& swing=transition && !current.swing.empty()?current.heat_swing:current.swing;
    if(!safe(target) || !safe(humidity) ||
       (!current.fan.empty() && !safe(fan)) ||
       (!current.swing.empty() && !safe(swing)))return {};
    std::string result="/aircon/set_control_info?pow="+
        std::string(patch.power.value_or(current.power)?"1":"0")+
        "&mode="+std::to_string(mode)+"&stemp="+target+"&shum="+humidity;
    if(!fan.empty())result+="&f_rate="+fan;
    if(!swing.empty())result+="&f_dir="+swing;
    return result;
}
bool accepted(std::string_view reply) {return fields(reply).has_value();}
bool matches(const Reading& state,const Patch& patch) {
    return (patch.power || patch.heat_mode || patch.heat_target_c) &&
        (!patch.power || state.power==*patch.power) &&
        (!patch.heat_mode || state.mode==4) &&
        (!patch.heat_target_c ||
         (state.target_c && std::fabs(*state.target_c-*patch.heat_target_c)<0.001));
}
bool same_request(const Reading& a,const Reading& b) {
    return a.power==b.power && a.mode==b.mode && a.target_c==b.target_c &&
        a.humidity==b.humidity && a.fan==b.fan && a.swing==b.swing &&
        a.heat_humidity==b.heat_humidity && a.heat_fan==b.heat_fan &&
        a.heat_swing==b.heat_swing;
}
bool readback_matches(const Reading& before,const Reading& after,const Patch& patch) {
    return matches(after,patch) && after.power==patch.power.value_or(before.power) &&
        after.mode==(patch.heat_mode?4u:before.mode);
}
bool changes_mode(const Reading& before,const Patch& patch) {
    return before.power!=patch.power.value_or(before.power) ||
        (patch.heat_mode && before.mode!=4);
}
std::optional<double> heat_target(std::string_view value) {
    const auto target=measure(value,10,32);
    return target && std::fabs(*target*2-std::round(*target*2))<0.001?target:std::nullopt;
}
std::optional<PumpHeatingLimits> model_limits(std::string_view low,std::string_view high,
                                              std::string_view step) {
    const auto a=heat_target(low),b=heat_target(high);
    const double increment=step=="0.5"?.5:step=="1"?1.:0.;
    if(!a || !b || *a>=*b || increment==0)return {};
    const PumpHeatingLimits limits{*a,*b,increment,true,true};
    return limits.accepts(*a) && limits.accepts(*b)?std::optional{limits}:std::nullopt;
}
PumpRoomMode room_mode(const Reading& state) {
    if(!state.power)return PumpRoomMode::Off;
    if(state.mode==4)return state.target_c?PumpRoomMode::Heat:PumpRoomMode::Unknown;
    return PumpRoomMode::Other;
}
}
