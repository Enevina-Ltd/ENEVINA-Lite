#include "forecast/fmi.hpp"
#include "forecast/sources.hpp"
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>

namespace enevina {
namespace {
bool field(std::string_view record, std::string_view name,
    std::string_view& value) {
    const std::string open="<BsWfs:"+std::string(name)+">";
    const std::string close="</BsWfs:"+std::string(name)+">";
    const size_t start=record.find(open);
    if(start==std::string_view::npos) return false;
    const size_t from=start+open.size();
    const size_t end=record.find(close,from);
    if(end==std::string_view::npos || record.find(open,from)!=std::string_view::npos)
        return false;
    value=record.substr(from,end-from);
    return !value.empty();
}
}
bool parse_fmi_temperature(const char* xml, OutdoorPoint* output,
    size_t capacity, size_t& count) {
    count=0;
    if(!xml || !output || capacity==0) return false;
    const std::string_view document(xml);
    if(document.find("</wfs:FeatureCollection>")==std::string_view::npos ||
       document.find("<wfs:FeatureCollection")==std::string_view::npos)
        return false;
    constexpr std::string_view open="<BsWfs:BsWfsElement";
    constexpr std::string_view close="</BsWfs:BsWfsElement>";
    size_t cursor=0;
    while(true) {
        const size_t begin=document.find(open,cursor);
        if(begin==std::string_view::npos) break;
        const size_t body=document.find('>',begin+open.size());
        const size_t end=document.find(close,body);
        if(body==std::string_view::npos || end==std::string_view::npos ||
           count>=capacity) return false;
        const auto record=document.substr(body+1,end-body-1);
        std::string_view time,param,value;
        if(!field(record,"Time",time) ||
           !field(record,"ParameterName",param) ||
           !field(record,"ParameterValue",value) || param!="temperature" ||
           time.size()>32 || value.size()>31) return false;
        char time_text[33];
        std::memcpy(time_text,time.data(),time.size()); time_text[time.size()]='\0';
        char value_text[32];
        std::memcpy(value_text,value.data(),value.size()); value_text[value.size()]='\0';
        char* after=nullptr;
        const double temperature=std::strtod(value_text,&after);
        int64_t timestamp=0;
        if(!parse_utc_z(time_text,timestamp) || after==value_text || *after!='\0' ||
           !std::isfinite(temperature) || temperature < -60 || temperature > 60 ||
           timestamp%3600!=0 ||
           (count>0 && timestamp-output[count-1].timestamp_utc!=3600))
            return false;
        output[count++]={timestamp,temperature};
        cursor=end+close.size();
    }
    return count>=2;
}
bool interpolate_fmi_temperature(const OutdoorPoint* source, size_t count,
    int64_t first_utc, double* output_c, size_t slots) {
    if(!source || !output_c || count<2 || slots==0 || slots>192 ||
       first_utc%900!=0 || first_utc<source[0].timestamp_utc ||
       first_utc+static_cast<int64_t>(slots-1)*900>source[count-1].timestamp_utc)
        return false;
    for(size_t i=0;i<count;++i)
        if(!std::isfinite(source[i].temperature_c) ||
           source[i].temperature_c < -60 || source[i].temperature_c > 60 ||
           (i>0 && source[i].timestamp_utc-source[i-1].timestamp_utc!=3600))
            return false;
    size_t hour=0;
    for(size_t i=0;i<slots;++i) {
        const int64_t target=first_utc+static_cast<int64_t>(i)*900;
        while(hour+1<count-1 && target>source[hour+1].timestamp_utc)
            ++hour;
        const double fraction=double(target-source[hour].timestamp_utc)/3600.0;
        output_c[i]=source[hour].temperature_c+
            fraction*(source[hour+1].temperature_c-source[hour].temperature_c);
    }
    return true;
}
}
