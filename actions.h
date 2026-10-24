#pragma once
#include <string>
#include <vector>
#include <limits>
namespace ldcontrol {
struct QuickConfig {int function=0;bool sell=false;std::vector<unsigned> ids;};
enum class Intent { Automatic, Quick, Reset };
struct ControlStep {unsigned offset,value,delay;};
inline const std::vector<unsigned>& resetOffsets(){static const std::vector<unsigned> offsets{0,36,48,12,8,32,20,4,24};return offsets;}
inline bool validQuick(const QuickConfig&q){if(q.function==0)return !q.sell&&q.ids.empty();if((q.function!=1&&q.function!=14)||q.ids.size()>64||(q.function==14&&q.sell))return false;for(auto id:q.ids)if(!id||id>2147483647u)return false;return true;}
inline bool parseIds(const std::wstring&text,std::vector<unsigned>&out){
    if(text.size()>1024)return false;std::vector<unsigned> parsed;size_t i=0;
    auto spaces=[&]{while(i<text.size()&&(text[i]==L' '||text[i]==L'\t'))i++;};spaces();if(i==text.size()){out.clear();return true;}
    for(;;){spaces();unsigned value=0;size_t start=i;while(i<text.size()&&text[i]>=L'0'&&text[i]<=L'9'){unsigned digit=unsigned(text[i++]-L'0');if(value>(2147483647u-digit)/10)return false;value=value*10+digit;}
        if(i==start||!value||parsed.size()>=64)return false;parsed.push_back(value);spaces();if(i==text.size())break;if(text[i++]!=L',')return false;spaces();if(i==text.size())return false;}
    out=std::move(parsed);return true;
}
inline std::wstring idsText(const std::vector<unsigned>&ids){std::wstring text;for(auto id:ids){if(!text.empty())text+=L", ";text+=std::to_wstring(id);}return text;}
inline std::vector<ControlStep> quickStart(int function){return function==1?std::vector<ControlStep>{{36,1,100},{0,1,0}}:function==14?std::vector<ControlStep>{{0,11,0},{20,11,0}}:std::vector<ControlStep>{};}
inline bool needsQuickReset(const QuickConfig&q,int last,bool liveMatches){return last!=q.function||!liveMatches;}
}
