#include "backend.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <fstream>
#include <set>
#include <sstream>
#include <new>
#include <bcrypt.h>
#include <psapi.h>

namespace ldcontrol {
static constexpr wchar_t Console[]=L"D:\\LDPlayer\\LDPlayer9\\ldconsole.exe";
static constexpr wchar_t Adb[]=L"D:\\LDPlayer\\LDPlayer9\\adb.exe";
static constexpr char Digest[]="1eb769fb80291fae60215276575acecc5363c59e9433965014c6767ffefecd2a";
static constexpr unsigned char BuildId[]={0x57,0x5b,0x33,0x22,0x44,0xc8,0x36,0xfd,0x55,0x64,0x84,0xdb,0xb8,0x27,0xdd,0x79,0x91,0x10,0xe4,0x39};
static constexpr char Package[]="com.vng.playtogether";
struct Handle { HANDLE h; explicit Handle(HANDLE v=nullptr):h(v){} ~Handle(){if(h&&h!=INVALID_HANDLE_VALUE)CloseHandle(h);} Handle(const Handle&)=delete; };
struct CommandGuard { HANDLE process;~CommandGuard(){if(WaitForSingleObject(process,0)==WAIT_TIMEOUT){TerminateProcess(process,1);WaitForSingleObject(process,1000);}} };
static std::string trim(std::string s){auto first=s.find_first_not_of(" \r\n\t"),last=s.find_last_not_of(" \r\n\t");return first==std::string::npos?"":s.substr(first,last-first+1);}
static std::wstring wide(const std::string&s){if(s.empty())return {};UINT cp=CP_UTF8;int n=MultiByteToWideChar(cp,MB_ERR_INVALID_CHARS,s.data(),int(s.size()),nullptr,0);if(!n){cp=CP_ACP;n=MultiByteToWideChar(cp,0,s.data(),int(s.size()),nullptr,0);}std::wstring out(n,L'\0');if(n)MultiByteToWideChar(cp,0,s.data(),int(s.size()),out.data(),n);return out;}
static bool number(const std::string&s,unsigned long long&n,unsigned base=10){if(s.empty()||s.size()>(base==10?20u:16u))return false;n=0;for(char c:s){unsigned v=c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:99;if(v>=base||n>(~0ULL-v)/base)return false;n=n*base+v;}return true;}
static bool processStat(const std::string&text,DWORD pid,std::string&ticks,char&state){
    const auto open=text.find('('),close=text.find_last_of(')');unsigned long long parsed=0,start=0;
    if(open==std::string::npos||close==std::string::npos||close<=open||!number(trim(text.substr(0,open)),parsed)||parsed!=pid)return false;
    std::istringstream fields(text.substr(close+1));std::string field;
    for(unsigned i=0;i<20;i++){if(!(fields>>field))return false;if(!i){if(field.size()!=1||std::string("RSDZTWtXxIKP").find(field[0])==std::string::npos)return false;state=field[0];}}
    if(!number(field,start)||!start)return false;ticks=field;return true;
}
static bool validBoot(const std::string&boot){if(boot.size()!=36)return false;for(size_t i=0;i<boot.size();i++){if(i==8||i==13||i==18||i==23){if(boot[i]!='-')return false;}else if(!((boot[i]>='0'&&boot[i]<='9')||(boot[i]>='a'&&boot[i]<='f')))return false;}return true;}
enum class Health { Alive,Missing,Rebooted,Unknown };
static Health parseHealth(const std::string&output,const Row&r){
    if(!validBoot(r.bootId)||!r.game||r.startTicks.empty())return Health::Unknown;
    std::istringstream input(output);std::vector<std::string> lines;std::string line;
    while(std::getline(input,line)){line=trim(line);if(!line.empty())lines.push_back(line);if(lines.size()>5)return Health::Unknown;}
    if(lines.size()!=5||lines[0]!="LDMENU_HEALTH_V1"||lines[4]!="LDMENU_HEALTH_END"||lines[1].rfind("BOOT ",0)||lines[2].rfind("PIDS ",0)||lines[3].rfind("STAT ",0))return Health::Unknown;
    const auto boot=lines[1].substr(5);if(!validBoot(boot))return Health::Unknown;if(boot!=r.bootId)return Health::Rebooted;
    std::istringstream pids(lines[2].substr(5));std::string code,token;std::set<DWORD> seen;if(!(pids>>code)||(code!="0"&&code!="1"))return Health::Unknown;
    while(pids>>token){unsigned long long pid=0;if(!number(token,pid)||!pid||pid>MAXDWORD||!seen.insert(DWORD(pid)).second||seen.size()>4)return Health::Unknown;}
    if((code=="0"&&seen.empty())||(code=="1"&&!seen.empty()))return Health::Unknown;
    if(!seen.count(r.game))return Health::Missing;
    if(lines[3].rfind("STAT 0 ",0))return Health::Unknown;
    std::string ticks;char state=0;if(!processStat(lines[3].substr(7),r.game,ticks,state))return Health::Unknown;
    return ticks==r.startTicks&&state!='Z'&&state!='X'&&state!='x'?Health::Alive:Health::Missing;
}
static std::wstring healthScript(DWORD pid){
    return L"boot=$(cat /proc/sys/kernel/random/boot_id) || exit 1; pids=$(pidof com.vng.playtogether 2>/dev/null); pc=$?; st=$(cat /proc/"+std::to_wstring(pid)+L"/stat 2>/dev/null); sc=$?; printf 'LDMENU_HEALTH_V1\\nBOOT %s\\nPIDS %s %s\\nSTAT %s %s\\nLDMENU_HEALTH_END\\n' \"$boot\" \"$pc\" \"$pids\" \"$sc\" \"$st\"";
}
static bool sameControls(const Config&a,const Config&b){return a.function==b.function&&a.filter==b.filter&&a.sell==b.sell;}
static bool sameConfig(const Config&a,const Config&b){return sameControls(a,b)&&a.reconnect==b.reconnect;}
static bool parseList(const std::string&text,std::vector<Row>&out){
    std::istringstream input(text);std::string line;std::vector<Row> parsed;std::set<int> indices;
    while(std::getline(input,line)){line=trim(line);if(line.empty())continue;std::vector<std::string> f;std::istringstream fields(line);std::string cell;while(std::getline(fields,cell,','))f.push_back(cell);
        unsigned long long index=0,vm=0,ui=0;if(f.size()!=10||!number(f[0],index)||index>30000||f[1].empty()||f[1].size()>256||!indices.insert(int(index)).second||(f[4]!="0"&&f[4]!="1"))return false;
        Row r;r.index=int(index);r.name=wide(f[1]);r.running=f[4]=="1";
        if(r.running&&(!number(f[6],vm)||!vm||vm>MAXDWORD||!number(f[5],ui)||!ui||ui>MAXDWORD))return false;
        r.vm=DWORD(vm);r.ui=DWORD(ui);r.phase=r.running?Phase::Idle:Phase::Closed;r.detail=r.running?L"Sẵn sàng":L"LD đang tắt";parsed.push_back(r);
    }
    if(parsed.size()>512)return false;std::sort(parsed.begin(),parsed.end(),[](auto&a,auto&b){return a.index<b.index;});out=std::move(parsed);return true;
}
static bool online(const std::string&listing,int index){const std::string serial="emulator-"+std::to_string(5554+index*2);std::istringstream lines(listing);std::string line;unsigned matches=0;bool ready=false;while(std::getline(lines,line)){std::istringstream row(line);std::string s,status;if(row>>s>>status&&s==serial){matches++;ready=status=="device";}}return matches==1&&ready;}
enum class Link { Ready,Missing,Unknown };
static Link classify(bool marker,bool listed,const std::string&listing,int index){
    if(marker)return Link::Ready;if(!listed||listing.find("List of devices attached")==std::string::npos)return Link::Unknown;
    const std::string wanted="emulator-"+std::to_string(5554+index*2);std::istringstream lines(listing);std::string line,state;unsigned matches=0;
    while(std::getline(lines,line)){std::istringstream row(line);std::string serial,status;if(row>>serial>>status&&serial==wanted){matches++;state=status;}}
    if(matches==0)return Link::Missing;if(matches==1&&state=="offline")return Link::Missing;return Link::Unknown;
}
static bool launcher(const std::string&text,std::wstring&out){std::istringstream lines(text);std::string line,found;unsigned matches=0;while(std::getline(lines,line)){line=trim(line);if(line.rfind("com.vng.playtogether/",0))continue;if(line.size()>256||line.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_./$")!=std::string::npos)return false;matches++;found=line;}if(matches!=1)return false;out=wide(found);return true;}
static bool moduleBase(const std::string&text,unsigned long long&base){std::istringstream lines(text);std::string line;unsigned matches=0;while(std::getline(lines,line)){std::istringstream row(line);std::string range,perms,offset,dev,inode,path,extra;if(!(row>>range>>perms>>offset>>dev>>inode>>path)||path!="/data/local/tmp/libmod.so")continue;if(row>>extra){if(extra!="(deleted)"||row>>extra)return false;}auto dash=range.find('-');unsigned long long lo=0,hi=0,off=0;if(dash==std::string::npos||!number(range.substr(0,dash),lo,16)||!number(range.substr(dash+1),hi,16)||!number(offset,off,16)||hi<=lo)return false;if(off==0){if(!lo||lo%4096)return false;base=lo;matches++;}}return matches==1;}
static ContextState classifyMaps(const std::string&text,unsigned long long&base){
    std::istringstream input(text);std::string line;bool any=false,library=false;
    while(std::getline(input,line)){if(trim(line).empty())continue;std::istringstream fields(line);std::string range,perms,offset,dev,inode,path;unsigned long long lo=0,hi=0,off=0,ino=0,major=0,minor=0;
        if(!(fields>>range>>perms>>offset>>dev>>inode))return ContextState::Unknown;const auto dash=range.find('-'),colon=dev.find(':');
        if(dash==std::string::npos||colon==std::string::npos||!number(range.substr(0,dash),lo,16)||!number(range.substr(dash+1),hi,16)||hi<=lo||!number(offset,off,16)||!number(inode,ino)||!number(dev.substr(0,colon),major,16)||!number(dev.substr(colon+1),minor,16)||perms.size()!=4||std::string("r-").find(perms[0])==std::string::npos||std::string("w-").find(perms[1])==std::string::npos||std::string("x-").find(perms[2])==std::string::npos||std::string("ps").find(perms[3])==std::string::npos)return ContextState::Unknown;
        any=true;if(fields>>path&&path=="/data/local/tmp/libmod.so")library=true;
    }
    if(!any)return ContextState::Unknown;if(!library)return ContextState::Absent;
    return moduleBase(text,base)?ContextState::Verified:ContextState::Unknown;
}
static bool waitingPhase(Phase p){return p==Phase::WaitingAdb||p==Phase::WaitingContext||p==Phase::Cooldown;}
static bool reserveRestartAt(Row&r,ULONGLONG now,ULONGLONG&retryAt){
    std::array<ULONGLONG,5> recent{};size_t count=0;for(auto t:r.restartTimes)if(t&&now>=t&&now-t<RestartWindowMs)recent[count++]=t;
    r.restartTimes=recent;if(count==recent.size()){retryAt=recent[0]+RestartWindowMs;return false;}r.restartTimes[count]=now;retryAt=0;return true;
}
static bool bytes(const std::string&text,std::string&out,size_t count){std::string s;for(char c:text)if(c!=' '&&c!='\r'&&c!='\n'&&c!='\t')s+=c;if(s.size()!=count*2)return false;out.clear();for(size_t i=0;i<s.size();i+=2){unsigned long long v;if(!number(s.substr(i,2),v,16))return false;out+=char(v);}return true;}
static unsigned u32(const std::string&s,size_t at){unsigned n=0;for(int i=0;i<4;i++)n|=unsigned(static_cast<unsigned char>(s.at(at+i)))<<(8*i);return n;}
static bool sameControl(const std::string&v,const Config&c){return v.size()==48&&validConfig(c)&&u32(v,0)==(c.function?4u:0u)&&(!c.function||(u32(v,8)==(c.function==2?1u:2u)&&u32(v,36)==1))&&u32(v,32)==unsigned(c.filter)&&u32(v,12)==unsigned(c.sell);}
static bool knownNote(const std::string&b){return b.size()==36&&u32(b,0)==4&&u32(b,4)==20&&u32(b,8)==3&&b.compare(12,4,"GNU\0",4)==0&&std::equal(std::begin(BuildId),std::end(BuildId),reinterpret_cast<const unsigned char*>(b.data()+16));}
static std::wstring assetPath(const wchar_t*name=L"libmod-arm64.so"){wchar_t exe[32768];DWORD n=GetModuleFileNameW(nullptr,exe,32768);if(!n||n==32768)return {};std::wstring path(exe,n);return path.substr(0,path.find_last_of(L"\\/")+1)+L"LDMenu\\"+name;}
static bool assetHash(const std::wstring&path,const char*expected=Digest){Handle file(CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr));if(file.h==INVALID_HANDLE_VALUE)return false;BCRYPT_ALG_HANDLE alg=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;if(BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)return false;bool ok=BCryptCreateHash(alg,&hash,nullptr,0,nullptr,0,0)>=0;std::array<unsigned char,8192> b{};DWORD n=0;while(ok){if(!ReadFile(file.h,b.data(),DWORD(b.size()),&n,nullptr)){ok=false;break;}if(!n)break;ok=BCryptHashData(hash,b.data(),n,0)>=0;}std::array<unsigned char,32> digest{};if(ok)ok=BCryptFinishHash(hash,digest.data(),32,0)>=0;if(hash)BCryptDestroyHash(hash);BCryptCloseAlgorithmProvider(alg,0);char encoded[65];for(unsigned i=0;i<32;i++)std::snprintf(encoded+2*i,3,"%02x",digest[i]);return ok&&std::string(encoded)==expected;}
static bool commitReady(){PERFORMANCE_INFORMATION p{};p.cb=sizeof(p);return GetPerformanceInfo(&p,sizeof(p))&&p.CommitLimit>p.CommitTotal&&static_cast<unsigned long long>(p.CommitLimit-p.CommitTotal)*p.PageSize>=2ULL*1024*1024*1024;}
static std::wstring quoteArg(const std::wstring&s){std::wstring out=L"\"";unsigned slashes=0;for(auto c:s){if(c==L'\\'){slashes++;continue;}if(c==L'\"'){out.append(slashes*2+1,L'\\');out+=c;}else{out.append(slashes,L'\\');out+=c;}slashes=0;}out.append(slashes*2,L'\\');return out+L"\"";}
static std::wstring shellQuote(const std::wstring&s){std::wstring out=L"'";for(auto c:s)out+=c==L'\''?L"'\\''":std::wstring(1,c);return out+L"'";}
std::wstring phaseText(Phase p){switch(p){case Phase::Idle:return L"Sẵn sàng";case Phase::Queued:return L"Đang chờ";case Phase::Checking:return L"Kiểm tra";case Phase::Repairing:return L"Sửa kết nối";case Phase::Opening:return L"Mở game";case Phase::Loading:return L"Đang kết nối";case Phase::Connected:return L"Thành công";case Phase::Applying:return L"Áp dụng cấu hình";case Phase::WaitingAdb:return L"Chờ ADB";case Phase::WaitingContext:return L"Chờ xác minh";case Phase::Cooldown:return L"Tạm chờ";case Phase::Failed:return L"Thất bại";case Phase::Closed:return L"Đã tắt";}return {};}
Engine::Engine(){stop_=CreateEventW(nullptr,TRUE,FALSE,nullptr);wake_=CreateEventW(nullptr,FALSE,FALSE,nullptr);}
Engine::~Engine(){stop();if(stop_)CloseHandle(stop_);if(wake_)CloseHandle(wake_);}
void Engine::start(HWND w){window_=w;try{if(!stop_||!wake_){halt(3);return;}if(!thread_.joinable()){loadSettings(settings_,settingsError_,&state_.global);state_.error=settingsError_;thread_=std::thread([this]()noexcept{try{loop();}catch(const std::bad_alloc&){halt(1);}catch(...){halt(2);}});}}catch(const std::bad_alloc&){halt(1);}catch(...){halt(2);}}
void Engine::halt(unsigned reason)noexcept{unsigned expected=0;if(workerFailure_.compare_exchange_strong(expected,reason?reason:2)){if(stop_)SetEvent(stop_);if(wake_)SetEvent(wake_);if(window_)PostMessageW(window_,WorkerFailed,workerFailure_.load(),0);}}
void Engine::stop(){if(stop_)SetEvent(stop_);if(wake_)SetEvent(wake_);if(thread_.joinable())thread_.join();}
void Engine::notify(){if(window_)PostMessageW(window_,Changed,0,0);}
Snapshot Engine::snapshot(){std::lock_guard<std::mutex> lock(mutex_);return state_;}
void Engine::setBackground(bool){}
void Engine::refresh(){{std::lock_guard<std::mutex> lock(mutex_);refresh_=true;}SetEvent(wake_);}
void Engine::queueActionLocked(Row&r,Intent intent){
    r.intent=intent;r.actionPending=true;r.playQueued=false;r.automatic=false;r.wanted=true;r.revision++;r.generation++;r.retryAt=0;
    if(intent==Intent::Reset)r.quick={};
    if(active_&&active_->index==r.index)active_->cancelled=true;
    // Reset never starts/restarts a game. Quick attaches first only when needed.
    r.phase=intent==Intent::Reset||(r.game&&r.base)?Phase::Connected:Phase::Queued;
    r.applyAt=r.phase==Phase::Connected?GetTickCount64():0;r.detail=intent==Intent::Reset?L"Chờ Reset All":L"Chờ chạy chức năng rời";
}
bool Engine::runQuick(int index,const std::wstring&name,const QuickConfig&config,std::wstring&error){
    if(!validQuick(config)){error=L"Chức năng hoặc ID không hợp lệ";return false;}
    {std::lock_guard<std::mutex> lock(mutex_);auto r=std::find_if(state_.rows.begin(),state_.rows.end(),[&](auto&v){return v.index==index&&v.name==name&&v.running;});if(r==state_.rows.end()){error=L"LD không còn đang mở";return false;}
        if(r->actionPending||(active_&&active_->index==index)){error=L"LD đang xử lý; chờ thao tác hiện tại xong";return false;}
        r->quick=config;queueActionLocked(*r,config.function?Intent::Quick:Intent::Reset);
    }SetEvent(wake_);notify();return true;
}
bool Engine::configure(int index,const std::wstring&name,const Config&config,std::wstring&error,bool reset){
    if(!validConfig(config)){error=L"Cấu hình không hợp lệ";return false;}
    {std::lock_guard<std::mutex> lock(mutex_);if(!settingsError_.empty()){error=settingsError_;return false;}auto row=std::find_if(state_.rows.begin(),state_.rows.end(),[&](auto&r){return r.index==index&&r.name==name;});if(row==state_.rows.end()){error=L"LD không còn trong danh sách";return false;}
        auto entries=settings_;auto saved=std::find_if(entries.begin(),entries.end(),[&](auto&s){return s.index==index;});const bool changed=!sameConfig(row->config,config),controlsChanged=!sameControls(row->config,config);
        if(!reset&&saved!=entries.end()&&saved->name==name&&sameConfig(saved->config,config)&&!changed&&row->intent==Intent::Automatic)return true;
        if(saved==entries.end())entries.push_back({index,name,config});else *saved={index,name,config};
        if(!saveSettings(entries,error,state_.global))return false;settings_=std::move(entries);const bool previous=row->config.reconnect;row->config=config;
        if(reset){queueActionLocked(*row,Intent::Reset);}else{const bool switching=row->intent!=Intent::Automatic;row->intent=Intent::Automatic;row->actionPending=false;updateReconnectLocked(*row,previous);if(controlsChanged||switching){row->revision++;
        if(row->phase==Phase::Connected||row->phase==Phase::Applying){row->applyAt=std::max(row->applyAt,GetTickCount64()+500);row->phase=Phase::Connected;if(active_&&active_->index==index&&active_->features)active_->cancelled=true;}}
        }
    }SetEvent(wake_);notify();return true;
}
bool Engine::configureGlobal(const GlobalSettings&global,bool applyToRunning,std::wstring&error,bool reset){
    if(!validGlobal(global)){error=L"Cấu hình chung không hợp lệ";return false;}
    {std::lock_guard<std::mutex> lock(mutex_);if(!settingsError_.empty()){error=settingsError_;return false;}auto entries=settings_;
        if(applyToRunning)for(const auto&r:state_.rows)if(r.running){auto saved=std::find_if(entries.begin(),entries.end(),[&](auto&s){return s.index==r.index;});if(saved==entries.end())entries.push_back({r.index,r.name,global.defaults});else *saved={r.index,r.name,global.defaults};}
        if(!reset&&encodeSettings(entries,global)==encodeSettings(settings_,state_.global))return true;
        if(!saveSettings(entries,error,global))return false;settings_=std::move(entries);state_.global=global;
        for(auto&r:state_.rows){bool separate=std::any_of(settings_.begin(),settings_.end(),[&](auto&s){return s.index==r.index&&s.name==r.name;});if(!(applyToRunning&&r.running)&&separate)continue;if(!applyToRunning&&r.wanted)continue;const bool switching=r.intent!=Intent::Automatic,controlsChanged=!sameControls(r.config,global.defaults),previous=r.config.reconnect;r.config=global.defaults;if(reset&&applyToRunning&&r.running){queueActionLocked(r,Intent::Reset);continue;}r.intent=Intent::Automatic;r.actionPending=false;updateReconnectLocked(r,previous);if(!controlsChanged&&!switching)continue;r.revision++;
            if(applyToRunning&&r.running&&(r.phase==Phase::Connected||r.phase==Phase::Applying)){r.applyAt=std::max(r.applyAt,GetTickCount64()+500);r.phase=Phase::Connected;if(active_&&active_->index==r.index&&active_->features)active_->cancelled=true;}}
    }SetEvent(wake_);notify();return true;
}
static bool selectedForPlay(const Row&r,const GlobalSettings&g){return r.running&&std::any_of(g.playTargets.begin(),g.playTargets.end(),[&](auto&t){return t.index==r.index&&t.name==r.name;});}
bool Engine::play(std::wstring&error){
    bool found=false,queued=false;{std::lock_guard<std::mutex> lock(mutex_);if(!settingsError_.empty()){error=settingsError_;return false;}if(state_.playRunning){if(state_.playStopping){state_.playStopping=false;SetEvent(wake_);notify();}return true;}
        for(auto&r:state_.rows)if(selectedForPlay(r,state_.global)){found=true;if(r.wanted&&r.intent==Intent::Automatic)continue;queued=true;if(active_&&active_->index==r.index)active_->cancelled=true;if(std::none_of(settings_.begin(),settings_.end(),[&](auto&s){return s.index==r.index&&s.name==r.name;}))r.config=state_.global.defaults;r.intent=Intent::Automatic;r.actionPending=false;r.wanted=true;r.playQueued=true;r.automatic=false;r.generation++;r.game=0;r.base=0;r.applyAt=0;r.restarts=0;r.misses=0;r.phase=Phase::Queued;r.detail=L"Chờ Play kết nối";}
        if(queued){playGap_=state_.global.playDelaySeconds*1000ULL;nextPlay_=GetTickCount64();state_.playRunning=true;state_.playStopping=false;}}
    if(!found){error=L"Chọn LD đang mở trong Cấu Hình Chung rồi lưu";return false;}SetEvent(wake_);notify();return true;
}
void Engine::pausePlay(){{std::lock_guard<std::mutex> lock(mutex_);if(!state_.playRunning)return;state_.playStopping=true;}SetEvent(wake_);notify();}
bool Engine::advancePlayLocked(ULONGLONG now){
    if(!state_.playRunning||(active_&&active_->batch)||now<nextPlay_)return false;
    const bool pending=std::any_of(state_.rows.begin(),state_.rows.end(),[](auto&r){return r.running&&r.wanted&&r.playQueued&&r.phase==Phase::Queued;});
    if(!state_.playStopping&&pending)return false;
    if(state_.playStopping)for(auto&r:state_.rows)if(r.playQueued){r.playQueued=false;r.wanted=false;r.generation++;r.applyAt=0;r.phase=r.running?Phase::Idle:Phase::Closed;r.detail=L"Đã dừng Play";}
    state_.playRunning=false;state_.playStopping=false;nextPlay_=0;return true;
}
void Engine::toggle(int index){
    {std::lock_guard<std::mutex> lock(mutex_);for(auto&r:state_.rows)if(r.index==index){if(!r.running)return;r.wanted=!r.wanted;r.intent=Intent::Automatic;r.actionPending=false;r.playQueued=false;r.automatic=false;if(r.wanted&&std::none_of(settings_.begin(),settings_.end(),[&](auto&s){return s.index==r.index&&s.name==r.name;}))r.config=state_.global.defaults;r.generation++;r.game=0;r.base=0;r.applyAt=0;r.restarts=0;r.misses=0;r.phase=r.wanted?Phase::Queued:Phase::Idle;r.detail=r.wanted?L"Chờ kết nối":L"Đã ngắt điều khiển";if(active_&&active_->index==index)active_->cancelled=true;break;}}
    SetEvent(wake_);notify();
}
bool Engine::cancelled(const std::shared_ptr<Job>&j){return WaitForSingleObject(stop_,0)!=WAIT_TIMEOUT||(j&&j->cancelled.load());}
bool Engine::wait(DWORD ms,const std::shared_ptr<Job>&j){auto end=GetTickCount64()+ms;while(GetTickCount64()<end){if(cancelled(j))return false;WaitForSingleObject(wake_,DWORD(std::min<ULONGLONG>(200,end-GetTickCount64())));}return !cancelled(j);}
bool Engine::command(bool adb,const std::wstring&args,std::string&out,const std::shared_ptr<Job>&j,DWORD timeout){
    out.clear();if(cancelled(j))return false;SECURITY_ATTRIBUTES sa{sizeof(sa),nullptr,TRUE};HANDLE r,w;if(!CreatePipe(&r,&w,&sa,0))return false;Handle reader(r),writer(w);if(!SetHandleInformation(r,HANDLE_FLAG_INHERIT,0))return false;
    Handle input(CreateFileW(L"NUL",GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,&sa,OPEN_EXISTING,0,nullptr));if(input.h==INVALID_HANDLE_VALUE)return false;
    const wchar_t*exe=adb?Adb:Console;std::wstring cmd=L"\""+std::wstring(exe)+L"\" "+args;STARTUPINFOW si{};si.cb=sizeof(si);si.dwFlags=STARTF_USESTDHANDLES;si.hStdInput=input.h;si.hStdOutput=w;si.hStdError=w;PROCESS_INFORMATION pi{};
    if(!CreateProcessW(exe,cmd.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW,nullptr,nullptr,&si,&pi))return false;Handle process(pi.hProcess),thread(pi.hThread);CommandGuard cleanup{pi.hProcess};CloseHandle(writer.h);writer.h=nullptr;auto deadline=GetTickCount64()+timeout;bool done=false,ok=true;
    HANDLE pending[]={stop_,pi.hProcess};
    for(;;){if(cancelled(j)||GetTickCount64()>=deadline){ok=false;break;}DWORD avail=0;if(PeekNamedPipe(r,nullptr,0,nullptr,&avail,nullptr)&&avail){char buf[4096];DWORD got=0;if(!ReadFile(r,buf,std::min<DWORD>(avail,sizeof(buf)),&got,nullptr)||out.size()+got>1024*1024){ok=false;break;}out.append(buf,got);continue;}if(WaitForSingleObject(pi.hProcess,0)==WAIT_OBJECT_0){done=true;break;}WaitForMultipleObjects(2,pending,FALSE,20);}
    DWORD code=1;if(done)GetExitCodeProcess(pi.hProcess,&code);else{TerminateProcess(pi.hProcess,1);WaitForSingleObject(pi.hProcess,1000);}return ok&&done&&code==0;
}
bool Engine::shell(const std::shared_ptr<Job>&j,const std::wstring&text,std::string&out,DWORD timeout,bool privileged){
    if(j->bootId.empty()){if(j->useSu&&privileged)return false;return command(false,L"adb --index "+std::to_wstring(j->index)+L" --command "+quoteArg(L"shell "+text),out,j,timeout);}
    const std::wstring boot(j->bootId.begin(),j->bootId.end());const auto guarded=L"test $(cat /proc/sys/kernel/random/boot_id) = "+boot+L" && { "+text+L"; }";
    const auto remote=j->useSu&&privileged?L"su -c "+shellQuote(guarded):guarded;
    return command(true,L"-s emulator-"+std::to_wstring(5554+2*j->index)+L" shell "+quoteArg(remote),out,j,timeout);
}
bool Engine::fresh(const std::shared_ptr<Job>&j,Row&row){std::string text;std::vector<Row> rows;if(!command(false,L"list2",text,j)||!parseList(text,rows))return false;auto it=std::find_if(rows.begin(),rows.end(),[&](auto&r){return r.index==j->index&&r.name==j->name;});if(it==rows.end()||!it->running)return false;row=*it;return true;}
void Engine::report(const std::shared_ptr<Job>&j,Phase phase,const std::wstring&detail,DWORD game,unsigned long long base){
    {std::lock_guard<std::mutex> lock(mutex_);auto it=std::find_if(state_.rows.begin(),state_.rows.end(),[&](auto&r){return r.index==j->index&&r.generation==j->generation;});if(it==state_.rows.end()||cancelled(j))return;if((waitingPhase(phase)||phase==Phase::Repairing)&&!game){game=it->game;base=it->base;}if(!waitingPhase(phase))it->retryAt=0;if(phase==Phase::Failed){it->actionPending=false;it->applyAt=0;it->quickActive=0;}if(it->phase==phase&&it->detail==detail&&it->game==game&&it->base==base)return;it->phase=phase;it->detail=detail;it->game=game;it->base=base;if(phase==Phase::Connected)it->automatic=false;}
    notify();
}
void Engine::updateReconnectLocked(Row&r,bool previous){
    if(previous==r.config.reconnect||!r.automatic||!r.wanted)return;
    if(!r.config.reconnect||r.phase==Phase::Failed){
        r.generation++;r.applyAt=0;r.retryAt=0;r.playQueued=false;r.game=0;r.base=0;
        r.phase=r.config.reconnect?Phase::Queued:Phase::Failed;r.detail=r.config.reconnect?L"Chờ kết nối lại":L"Reconnect đang tắt";
        if(active_&&active_->index==r.index)active_->cancelled=true;
    }
}
void Engine::queueReconnect(const std::shared_ptr<Job>&j,const wchar_t*detail){
    bool enabled=false;{std::lock_guard<std::mutex> lock(mutex_);auto r=std::find_if(state_.rows.begin(),state_.rows.end(),[&](auto&v){return v.index==j->index&&v.generation==j->generation&&v.wanted;});if(r==state_.rows.end()||cancelled(j))return;enabled=r->intent==Intent::Automatic&&r->config.reconnect;if(enabled){r->automatic=true;r->playQueued=false;}else{r->quickActive=0;r->actionPending=false;}}
    report(j,enabled?Phase::Queued:Phase::Failed,enabled?detail:L"Game đã tắt; Reconnect đang tắt");
}
bool Engine::selectJobLocked(ULONGLONG now,ULONGLONG&due,std::shared_ptr<Job>&job,Row&selected){
    Row*best=nullptr;ULONGLONG bestAt=~0ULL;unsigned bestKind=3;
    if(state_.playRunning)due=std::min(due,nextPlay_);
    for(auto&r:state_.rows){
        if(!r.wanted||!r.running)continue;ULONGLONG ready=0;unsigned kind=3;
        if(r.phase==Phase::Queued&&(!r.automatic||r.config.reconnect)&&(!r.playQueued||(state_.playRunning&&!state_.playStopping))){ready=r.playQueued?nextPlay_:nextConnect_;kind=0;}
        else if(r.phase==Phase::Connected&&r.applyAt){ready=r.applyAt;kind=1;}
        else if(waitingPhase(r.phase)&&r.retryAt){ready=r.retryAt;kind=2;}
        if(kind==3)continue;due=std::min(due,ready);
        if(ready<=now&&(!best||ready<bestAt||(ready==bestAt&&kind<bestKind))){best=&r;bestAt=ready;bestKind=kind;}
    }
    if(!best)return false;selected=*best;job=std::make_shared<Job>();job->features=bestKind==1||(bestKind==2&&best->actionPending);job->recovery=!job->features&&bestKind==2&&best->phase==Phase::WaitingAdb;job->batch=bestKind==0&&best->playQueued;
    job->index=selected.index;job->name=selected.name;job->generation=selected.generation;job->bootId=selected.bootId;job->useSu=selected.useSu;active_=job;
    if(!job->features){nextConnect_=now+5000;best->playQueued=false;}return true;
}
void Engine::defer(const std::shared_ptr<Job>&j,Phase phase,const wchar_t*detail,ULONGLONG until){
    if(cancelled(j))return;{std::lock_guard<std::mutex> lock(mutex_);for(auto&r:state_.rows)if(r.index==j->index&&r.generation==j->generation&&r.wanted)r.retryAt=until?until:GetTickCount64()+HealthIntervalMs;}
    report(j,phase,detail);
}
bool Engine::reserveRestart(const std::shared_ptr<Job>&j,ULONGLONG&retryAt){
    const auto now=GetTickCount64();std::lock_guard<std::mutex> lock(mutex_);
    for(auto&r:state_.rows)if(r.index==j->index&&r.generation==j->generation&&r.wanted&&!cancelled(j)){
        return reserveRestartAt(r,now,retryAt);
    }
    retryAt=0;return false;
}
void Engine::scan(){
    {std::lock_guard<std::mutex> lock(mutex_);state_.scanning=true;refresh_=false;}notify();std::string text;std::vector<Row> rows;bool ok=command(false,L"list2",text)&&parseList(text,rows);
    {
        std::lock_guard<std::mutex> lock(mutex_);state_.scanning=false;
        if(!ok)state_.error=L"Không đọc được danh sách LDPlayer";
        else{
            state_.error=settingsError_;
            for(auto&r:rows){
                r.config=state_.global.defaults;
                auto saved=std::find_if(settings_.begin(),settings_.end(),[&](auto&s){return s.index==r.index&&s.name==r.name;});if(saved!=settings_.end())r.config=saved->config;
                auto old=std::find_if(state_.rows.begin(),state_.rows.end(),[&](auto&o){return o.index==r.index&&o.name==r.name;});
                if(old!=state_.rows.end()){
                    r.restartTimes=old->restartTimes;
                    if(r.running&&old->running&&r.vm==old->vm)r=*old;
                    else{r.generation=old->generation+1;if(active_&&active_->index==r.index)active_->cancelled=true;}
                }
            }
            state_.rows=std::move(rows);
        }
    }
    notify();
}
void Engine::monitor(){
    // A single ordinary-shell query per connected LD. ADB errors are not game exits.
    std::vector<Row> rows;{std::lock_guard<std::mutex> lock(mutex_);for(const auto&r:state_.rows)if(r.wanted&&r.phase==Phase::Connected&&r.intent==Intent::Automatic)rows.push_back(r);}
    for(const auto&r:rows){
        if(!r.wanted||r.phase!=Phase::Connected||cancelled({}))continue;
        auto j=std::make_shared<Job>();j->index=r.index;j->name=r.name;j->generation=r.generation;j->bootId=r.bootId;
        {std::lock_guard<std::mutex> lock(mutex_);auto row=std::find_if(state_.rows.begin(),state_.rows.end(),[&](auto&v){return v.index==r.index&&v.generation==r.generation&&v.wanted&&v.phase==Phase::Connected;});if(row==state_.rows.end())continue;active_=j;}
        std::string output;const auto health=command(true,L"-s emulator-"+std::to_wstring(5554+2*r.index)+L" shell "+quoteArg(healthScript(r.game)),output,j,HealthTimeoutMs)?parseHealth(output,r):Health::Unknown;
        bool lost=false,repair=false,valid=false;
        {std::lock_guard<std::mutex> lock(mutex_);for(auto&row:state_.rows)if(row.index==r.index&&row.generation==r.generation&&row.wanted&&row.phase==Phase::Connected&&!cancelled(j)){
            valid=true;if(health==Health::Unknown){row.misses=0;row.probeErrors=std::min(row.probeErrors+1,2u);repair=row.probeErrors>=2;}else{row.probeErrors=0;row.misses=health==Health::Alive?0:health==Health::Rebooted?2:std::min(row.misses+1,2u);lost=row.misses>=2;}
            if(lost){row.applyAt=0;row.misses=0;}
        }}
        if(valid&&repair&&!cancelled(j)){
            if(ensureAdb(j)){
                {std::lock_guard<std::mutex> lock(mutex_);for(auto&row:state_.rows)if(row.index==r.index&&row.generation==r.generation)row.probeErrors=0;}
                auto state=snapshot();auto row=std::find_if(state.rows.begin(),state.rows.end(),[&](auto&v){return v.index==r.index&&v.generation==r.generation;});
                if(row!=state.rows.end()&&row->phase==Phase::Repairing)report(j,Phase::Connected,L"ADB đã phục hồi; chờ kiểm tra game",r.game,r.base);
            }
        }
        if(valid&&lost&&!cancelled(j))queueReconnect(j,L"Game đã tắt hoặc đổi phiên; chờ kết nối lại");
        {std::lock_guard<std::mutex> lock(mutex_);if(active_==j)active_.reset();}
    }
}
bool Engine::ensureAdb(const std::shared_ptr<Job>&j){
    if(!j->bootId.empty()){
        std::string current;if(command(true,L"-s emulator-"+std::to_wstring(5554+2*j->index)+L" shell cat /proc/sys/kernel/random/boot_id",current,j)&&trim(current)==j->bootId)return true;
    }
    std::string text,listing;auto marker=[&]{
        j->bootId.clear();j->useSu=false;if(!shell(j,L"cat /proc/sys/kernel/random/boot_id",text))return false;const auto boot=trim(text);if(!validBoot(boot))return false;
        if(!command(true,L"-s emulator-"+std::to_wstring(5554+2*j->index)+L" shell cat /proc/sys/kernel/random/boot_id",text,j)||trim(text)!=boot)return false;j->bootId=boot;return true;
    };
    if(marker())return true;bool listed=command(true,L"devices",listing,j);Link link=classify(false,listed,listing,j->index);
    if(link==Link::Unknown){defer(j,Phase::WaitingAdb,L"Chờ ADB phản hồi; không reset khi chưa rõ lỗi");return false;}
    if(adbRepairs_>=2){defer(j,Phase::WaitingAdb,L"Chờ ADB hồi lại; đã dùng 2 lượt reset");return false;}
    Row row;if(!fresh(j,row)){report(j,Phase::Failed,L"LD đã tắt hoặc đổi danh tính");return false;}
    if(lastRepair_&&GetTickCount64()<lastRepair_+60000){defer(j,Phase::WaitingAdb,L"Chờ giới hạn phục hồi ADB",std::max(GetTickCount64()+HealthIntervalMs,lastRepair_+60000));return false;}
    report(j,Phase::Repairing,L"Mất ADB; phục hồi ADB server");adbRepairs_++;lastRepair_=GetTickCount64();
    if(!command(true,L"kill-server",text,j)||!command(true,L"start-server",text,j)){defer(j,Phase::WaitingAdb,L"Chờ ADB server hồi lại");return false;}
    auto end=GetTickCount64()+20000;while(GetTickCount64()<end&&!cancelled(j)){if(marker())return true;if(!wait(1000,j))break;}
    defer(j,Phase::WaitingAdb,L"ADB chưa khôi phục; tiếp tục chờ");return false;
}
void Engine::recoverAdb(const std::shared_ptr<Job>&j,const Row&r){
    std::string boot;const auto prefix=L"-s emulator-"+std::to_wstring(5554+2*j->index)+L" shell ";
    if(!command(true,prefix+L"cat /proc/sys/kernel/random/boot_id",boot,j,HealthTimeoutMs)||!validBoot(trim(boot))){
        if(adbRepairs_<2&&(!lastRepair_||GetTickCount64()>=lastRepair_+60000)){j->bootId.clear();if(!ensureAdb(j))return;}
        else{defer(j,Phase::WaitingAdb,L"Chờ ADB hồi lại");return;}
    }
    if(cancelled(j))return;Row now;if(!fresh(j,now)){defer(j,Phase::WaitingAdb,L"Chờ xác minh LD sau khi ADB hồi");return;}
    j->bootId.clear();if(!ensureAdb(j))return;
    {std::lock_guard<std::mutex> lock(mutex_);for(auto&row:state_.rows)if(row.index==j->index&&row.generation==j->generation){row.vm=now.vm;row.ui=now.ui;row.probeErrors=0;row.misses=0;}}
    if(!r.game){if(r.automatic)queueReconnect(j,L"ADB đã hồi; chờ kết nối lại");else report(j,Phase::Queued,L"ADB đã hồi; chờ kết nối");return;}
    std::string output;const auto health=command(true,prefix+quoteArg(healthScript(r.game)),output,j,HealthTimeoutMs)?parseHealth(output,r):Health::Unknown;
    if(health==Health::Alive){report(j,Phase::Connected,L"ADB đã hồi; game vẫn đang chạy",r.game,r.base);return;}
    if(health==Health::Missing||health==Health::Rebooted){queueReconnect(j,L"ADB đã hồi; chờ kết nối lại game");return;}
    defer(j,Phase::WaitingAdb,L"ADB đã phản hồi; chờ xác minh tiến trình game");
}
bool Engine::ensureRoot(const std::shared_ptr<Job>&j){
    j->useSu=false;std::string text;
    if(j->bootId.empty())return false;
    auto remember=[&]{std::lock_guard<std::mutex> lock(mutex_);for(auto&r:state_.rows)if(r.index==j->index&&r.generation==j->generation){r.bootId=j->bootId;r.useSu=false;}return true;};
    if(shell(j,L"id -u",text)&&trim(text)=="0")return remember();
    if(cancelled(j))return false;
    report(j,Phase::Checking,L"Nâng quyền ADB root");
    command(true,L"-s emulator-"+std::to_wstring(5554+2*j->index)+L" root",text,j);
    const auto end=GetTickCount64()+10000;
    while(GetTickCount64()<end&&!cancelled(j)){
        if(shell(j,L"id -u",text)&&trim(text)=="0")return remember();
        if(!wait(250,j))return false;
    }
    if(!cancelled(j))report(j,Phase::Failed,L"ADB chưa có quyền root; kiểm tra bật Root LDPlayer");
    return false;
}
bool Engine::context(const std::shared_ptr<Job>&j,DWORD&pid,unsigned long long&base){return inspectContext(j,pid,base)==ContextState::Verified;}
ContextState Engine::inspectContext(const std::shared_ptr<Job>&j,DWORD&pid,unsigned long long&base){
    std::string text;if(!shell(j,L"ps -A -o PID,PPID,NAME; result=$?; echo LDMENU_PS_$result",text))return ContextState::Unknown;
    text=trim(text);const std::string psEnd="LDMENU_PS_0";if(text.size()<psEnd.size()||text.compare(text.size()-psEnd.size(),psEnd.size(),psEnd))return ContextState::Unknown;text.resize(text.size()-psEnd.size());
    std::istringstream lines(text);std::string line;std::vector<DWORD> candidates;bool header=false;
    while(std::getline(lines,line)){std::istringstream row(line);std::string p,parent,name,extra;unsigned long long n=0;if(!(row>>p>>parent>>name))continue;if(p=="PID"&&parent=="PPID"&&name=="NAME"){header=true;continue;}if(name==Package){if(row>>extra||!number(p,n)||!n||n>MAXDWORD||candidates.size()>=4||std::find(candidates.begin(),candidates.end(),DWORD(n))!=candidates.end())return ContextState::Unknown;candidates.push_back(DWORD(n));}}
    if(!header)return ContextState::Unknown;if(candidates.empty())return ContextState::Absent;
    auto maps=[&](DWORD p,unsigned long long&b){std::string out;if(!shell(j,L"cat /proc/"+std::to_wstring(p)+L"/maps && echo LDMENU_MAPS_OK",out))return ContextState::Unknown;out=trim(out);const std::string end="LDMENU_MAPS_OK";if(out.size()<end.size()||out.compare(out.size()-end.size(),end.size(),end))return ContextState::Unknown;out.resize(out.size()-end.size());return classifyMaps(out,b);};
    unsigned matches=0;for(auto p:candidates){unsigned long long b=0;const auto state=maps(p,b);if(state==ContextState::Unknown)return state;if(state==ContextState::Verified){pid=p;base=b;matches++;}}
    if(!matches)return ContextState::Absent;if(matches!=1)return ContextState::Unknown;
    std::string first,ticks;char state=0;if(!shell(j,L"cat /proc/"+std::to_wstring(pid)+L"/stat",first)||!processStat(first,pid,ticks,state)||state=='Z'||state=='X'||state=='x')return ContextState::Unknown;
    std::string note;if(!shell(j,L"dd if=/proc/"+std::to_wstring(pid)+L"/mem bs=1 skip="+std::to_wstring(base+0x2d0)+L" count=36 2>/dev/null | xxd -p",text)||!bytes(text,note,36)||!knownNote(note))return ContextState::Unknown;
    unsigned long long again=0;std::string after,afterTicks;char afterState=0;
    if(maps(pid,again)!=ContextState::Verified||again!=base||!shell(j,L"cat /proc/"+std::to_wstring(pid)+L"/stat",after)||!processStat(after,pid,afterTicks,afterState)||ticks!=afterTicks||afterState=='Z'||afterState=='X'||afterState=='x')return ContextState::Unknown;
    return ContextState::Verified;
}
bool Engine::openGame(const std::shared_ptr<Job>&j){
    Row before;if(!fresh(j,before))return false;std::string text;
    if(!shell(j,L"id -u",text)||trim(text)!="0"){report(j,Phase::Failed,L"ADB chưa có quyền root");return false;}
    const auto loader=L"old=0; merged=0; p=/data/adb/modules/zygisk_il2cppdumper; if test -f $p/module.prop && test ! -e $p/disable && test ! -e $p/remove; then old=1; fi; p=/data/adb/modules/ldlogin; if test -f $p/ldmenu-loader.prop && test ! -e $p/disable && test ! -e $p/remove && grep -qx 'loader=ldnph-v1.2.0' $p/ldmenu-loader.prop && test $(sha256sum $p/zygisk/arm64-v8a.so | cut -d ' ' -f1) = a029b48dc4c354f8cf5a6fa2ab39c0db29f400305916ce5187bd8f061040554c && test $(sha256sum $p/zygisk/x86_64.so | cut -d ' ' -f1) = 3e348324a9eb26f0741fa57307c03aabd17f2021242456a43769c4763f50ed56; then merged=1; fi; if test $old = 1 && test $merged = 1; then echo LDNPH_DUPLICATE; elif test $old = 1 || test $merged = 1; then echo LDNPH_READY; fi";
    if(!shell(j,loader,text)||trim(text)!="LDNPH_READY"){report(j,Phase::Failed,trim(text)=="LDNPH_DUPLICATE"?L"Trùng loader; gỡ LDNPH cũ rồi restart LD":L"Thiếu loader LDLogin/LDNPH hoặc module đang tắt");return false;}
    std::wstring component;if(!shell(j,L"cmd package resolve-activity --brief com.vng.playtogether",text)||!launcher(text,component)){report(j,Phase::Failed,L"Chưa cài Play Together VNG");return false;}
    std::wstring asset=assetPath();if(!assetHash(asset)){report(j,Phase::Failed,L"Thiếu hoặc sai phiên bản thư viện");return false;}
    if(!shell(j,L"if test -e /data/local/tmp/libmod.so; then sha256sum /data/local/tmp/libmod.so; else echo ASSET_ABSENT; fi",text))return false;
    if(trim(text)!="ASSET_ABSENT"&&text.compare(0,64,Digest)!=0){report(j,Phase::Failed,L"Có thư viện khác; dừng để tránh ghi đè");return false;}
    if(trim(text)=="ASSET_ABSENT"&&!command(false,L"adb --index "+std::to_wstring(j->index)+L" --command "+quoteArg(L"push "+quoteArg(asset)+L" /data/local/tmp/libmod.so"),text,j))return false;
    if(!shell(j,L"sha256sum /data/local/tmp/libmod.so",text)||text.compare(0,64,Digest)!=0||!shell(j,L"chmod 0644 /data/local/tmp/libmod.so",text))return false;
    Row now;if(!fresh(j,now)||now.vm!=before.vm)return false;
    report(j,Phase::Opening,L"Mở lại game để nạp thư viện");if(!shell(j,L"am force-stop com.vng.playtogether",text)||!wait(1000,j))return false;
    if(!shell(j,L"am start -n "+component,text)||text.find("Error:")!=std::string::npos||text.find("Exception")!=std::string::npos)return false;
    return true;
}
bool Engine::restart(const std::shared_ptr<Job>&j,const Row&old){
    Row now;if(!fresh(j,now)||now.vm!=old.vm||!commitReady())return false;
    Handle process(OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,old.vm));if(!process.h||WaitForSingleObject(process.h,0)!=WAIT_TIMEOUT)return false;
    wchar_t path[32768];DWORD n=32768;if(!QueryFullProcessImageNameW(process.h,0,path,&n))return false;auto slash=wcsrchr(path,L'\\');if(!slash||_wcsicmp(slash+1,L"Ld9BoxHeadless.exe"))return false;
    std::string text;if(!command(false,L"quit --index "+std::to_wstring(j->index),text,j))return false;auto end=GetTickCount64()+30000;
    while(GetTickCount64()<end&&WaitForSingleObject(process.h,0)!=WAIT_OBJECT_0)if(!wait(500,j))return false;
    if(WaitForSingleObject(process.h,0)!=WAIT_OBJECT_0||cancelled(j))return false;
    // Launch detached from the terminal's job; otherwise closing a test runner can kill LD.
    BOOL inJob=FALSE;if(!IsProcessInJob(GetCurrentProcess(),nullptr,&inJob))return false;DWORD flags=CREATE_NO_WINDOW|CREATE_NEW_PROCESS_GROUP;if(inJob)flags|=CREATE_BREAKAWAY_FROM_JOB;
    std::wstring cmd=L"\""+std::wstring(Console)+L"\" launch --index "+std::to_wstring(j->index);STARTUPINFOW si{};si.cb=sizeof(si);PROCESS_INFORMATION pi{};if(!CreateProcessW(Console,cmd.data(),nullptr,nullptr,FALSE,flags,nullptr,nullptr,&si,&pi))return false;CloseHandle(pi.hThread);CloseHandle(pi.hProcess);
    end=GetTickCount64()+120000;while(GetTickCount64()<end){if(!wait(1500,j))return false;if(fresh(j,now)&&now.vm!=old.vm){{std::lock_guard<std::mutex> lock(mutex_);for(auto&r:state_.rows)if(r.index==j->index&&r.generation==j->generation){r.vm=now.vm;r.ui=now.ui;}}report(j,Phase::Repairing,L"LD đã mở lại; chờ 30 giây");return wait(30000,j);}}return false;
}
void Engine::connected(const std::shared_ptr<Job>&j,DWORD pid,unsigned long long base){
    Config config;Intent intent=Intent::Automatic;unsigned long long revision=0;{std::lock_guard<std::mutex> lock(mutex_);for(auto&r:state_.rows)if(r.index==j->index&&r.generation==j->generation){config=r.config;intent=r.intent;revision=r.revision;}}
    bool known=false,matched=intent==Intent::Automatic&&controlMatches(j,pid,base,config,known),automate=false;unsigned seconds=60;
    std::string stat,ticks;char processState=0;if(!shell(j,L"cat /proc/"+std::to_wstring(pid)+L"/stat",stat,HealthTimeoutMs,false)||!processStat(stat,pid,ticks,processState)||processState=='Z'||processState=='X'||processState=='x'){report(j,Phase::Failed,L"Chưa xác minh được danh tính tiến trình game");return;}
    {std::lock_guard<std::mutex> lock(mutex_);seconds=state_.global.settleSeconds;for(auto&r:state_.rows)if(r.index==j->index&&r.generation==j->generation){if(r.revision!=revision){matched=false;known=false;}automate=r.intent==Intent::Automatic?(known?!matched:bool(r.config.function||r.config.filter||r.config.sell)):r.actionPending;r.applyAt=automate?GetTickCount64()+((r.intent==Intent::Automatic||j->loaded)?seconds*1000ULL:0):0;r.misses=0;r.probeErrors=0;r.bootId=j->bootId;r.useSu=j->useSu;r.startTicks=ticks;}}
    report(j,Phase::Connected,matched?L"Đã nhận lại cấu hình đang chạy":automate?L"Đã kết nối; chờ "+std::to_wstring(seconds)+L" giây để áp dụng cấu hình":L"Đã kết nối",pid,base);
}
bool Engine::controlMatches(const std::shared_ptr<Job>&j,DWORD pid,unsigned long long base,const Config&config,bool&known){
    known=false;std::string output,values;DWORD currentPid=0;unsigned long long currentBase=0;
    if(!shell(j,L"dd if=/proc/"+std::to_wstring(pid)+L"/mem bs=1 skip="+std::to_wstring(base+0x117044)+L" count=48 2>/dev/null | xxd -p",output)||!bytes(output,values,48)||!context(j,currentPid,currentBase)||currentPid!=pid||currentBase!=base)return false;
    known=true;return sameControl(values,config);
}
static bool validWord(unsigned relative,unsigned value){return relative==0?(value==0||value==1||value==4||value==11):relative==8?value<=2:relative==12||relative==4||relative==24||relative==36?value<=1:relative==32?value<=6:relative==20?(value==0||value==11):relative==28?value<=2147483647u:relative==48?value==0:false;}
static std::string littleHex(unsigned value){char out[9];std::snprintf(out,sizeof(out),"%02x%02x%02x%02x",value&255,(value>>8)&255,(value>>16)&255,(value>>24)&255);return out;}
static unsigned long long u64(const std::string&s,size_t p){return u32(s,p)|(static_cast<unsigned long long>(u32(s,p+4))<<32);}
static bool controlSegment(const std::string&header){
    if(header.size()<64||u32(header,0)!=0x464c457f||static_cast<unsigned char>(header[4])!=2||static_cast<unsigned char>(header[5])!=1||(u32(header,16)&0xffff)!=3||(u32(header,16)>>16)!=183)return false;
    const auto offset=u64(header,32);const unsigned size=static_cast<unsigned char>(header[54])|(static_cast<unsigned char>(header[55])<<8),count=static_cast<unsigned char>(header[56])|(static_cast<unsigned char>(header[57])<<8);
    if(size!=56||!count||count>32||offset>header.size()||offset+static_cast<unsigned long long>(count)*size>header.size())return false;
    for(unsigned i=0;i<count;i++){const auto p=size_t(offset+i*size);if(u32(header,p)!=1||!(u32(header,p+4)&2))continue;const auto start=u64(header,p+16),length=u64(header,p+40);if(start<=0x117044&&length<=0x4000000&&start<=0x4000000&&start+length>=0x1187f9)return true;}return false;
}
static bool writableControl(const std::string&maps,unsigned long long base,unsigned relative){
    const auto address=base+0x117044+relative;std::istringstream lines(maps);std::string line;unsigned long long fileEnd=0;
    while(std::getline(lines,line)){std::istringstream fields(line);std::string range,perms,offset,dev,inode,path;if(!(fields>>range>>perms>>offset>>dev>>inode))continue;fields>>path;auto dash=range.find('-');unsigned long long lo=0,hi=0;if(dash==std::string::npos||!number(range.substr(0,dash),lo,16)||!number(range.substr(dash+1),hi,16)||hi<=lo)continue;
        bool writable=perms.size()>=2&&perms[0]=='r'&&perms[1]=='w',file=path=="/data/local/tmp/libmod.so";bool bss=path.empty()&&dev=="00:00"&&inode=="0"&&lo==fileEnd;
        if(writable&&(file||bss)&&address>=lo&&address+4<=hi)return true;fileEnd=file&&writable?hi:0;
    }return false;
}
bool Engine::word(const std::shared_ptr<Job>&j,const Row&row,unsigned relative,unsigned value){
    if(!validWord(relative,value)||cancelled(j)||j->startTicks.empty())return false;Row now;std::string output;
    if(!fresh(j,now)||now.vm!=row.vm)return false;
    const auto encoded=littleHex(value);const std::wstring data(encoded.begin(),encoded.end()),ticks(j->startTicks.begin(),j->startTicks.end());const auto memory=L"/proc/"+std::to_wstring(row.game)+L"/mem";const auto address=std::to_wstring(row.base+0x117044+relative);
    std::string noteHex="040000001400000003000000474e5500";char byte[3];for(auto v:BuildId){std::snprintf(byte,3,"%02x",v);noteHex+=byte;}const std::wstring expected(noteHex.begin(),noteHex.end());
    const auto start=L"test $(sed 's/.*) //' /proc/"+std::to_wstring(row.game)+L"/stat | cut -d ' ' -f20) = "+ticks;
    const auto note=L"test $(dd if="+memory+L" bs=1 skip="+std::to_wstring(row.base+0x2d0)+L" count=36 2>/dev/null | xxd -p | tr -d '[:space:]') = "+expected;
    const auto script=start+L" && "+note+L" && printf "+data+L" | xxd -r -p | dd of="+memory+L" seek="+address+L" bs=1 count=4 2>/dev/null && test $(dd if="+memory+L" bs=1 skip="+address+L" count=4 2>/dev/null | xxd -p | tr -d '[:space:]') = "+data+L" && "+start+L" && "+note+L" && echo LDMENU_WORD_OK";
    return shell(j,script,output)&&trim(output)=="LDMENU_WORD_OK";
}
bool Engine::readMemory(const std::shared_ptr<Job>&j,const Row&r,unsigned offset,size_t count,std::string&data){
    if(cancelled(j)||j->startTicks.empty()||!r.game||count>256)return false;
    const bool allowed=(offset==0x117044&&count==52)||(offset==0x118798&&count==24)||((offset==0x1187f8||offset==0x1187b4)&&count==1);
    if(!allowed)return false;const std::wstring ticks(j->startTicks.begin(),j->startTicks.end());std::string output;
    return shell(j,L"test $(sed 's/.*) //' /proc/"+std::to_wstring(r.game)+L"/stat | cut -d ' ' -f20) = "+ticks+L" && dd if=/proc/"+std::to_wstring(r.game)+L"/mem bs=1 skip="+std::to_wstring(r.base+offset)+L" count="+std::to_wstring(count)+L" 2>/dev/null | xxd -p",output)&&bytes(output,data,count);
}
bool Engine::resetAll(const std::shared_ptr<Job>&j,const Row&r){
    for(auto offset:resetOffsets())if(!word(j,r,offset,0)||!wait(100,j))return false;
    std::string data;if(!readMemory(j,r,0x117044,52,data))return false;
    for(auto offset:resetOffsets())if(u32(data,offset)!=0)return false;return true;
}
static bool vectorBounds(const std::string&data,unsigned long long&begin,size_t&count){
    if(data.size()!=24)return false;begin=u64(data,0);const auto end=u64(data,8),capacity=u64(data,16);
    if(begin%4||end<begin||capacity<end||capacity-begin>65536||end-begin>256||((end-begin)%4)||capacity>=0x800000000000ULL)return false;
    count=size_t((end-begin)/4);return !count||begin>=4096;
}
static bool readableRange(const std::string&maps,unsigned long long address,size_t size){
    if(!size||size>256||address>0x800000000000ULL-size)return false;std::istringstream lines(maps);std::string line;
    while(std::getline(lines,line)){std::istringstream fields(line);std::string range,perms;fields>>range>>perms;const auto dash=range.find('-');unsigned long long begin=0,end=0;if(dash!=std::string::npos&&perms.size()==4&&perms[0]=='r'&&number(range.substr(0,dash),begin,16)&&number(range.substr(dash+1),end,16)&&address>=begin&&address+size<=end)return true;}return false;
}
bool Engine::waitFilter(const std::shared_ptr<Job>&j,const Row&r,const std::vector<unsigned>&ids,bool zeroMailbox){
    const auto end=GetTickCount64()+6000;
    do{
        std::string meta,data,latch;unsigned long long begin=0;size_t count=0;
        if(!readMemory(j,r,0x118798,24,meta)||!vectorBounds(meta,begin,count))return false;
        bool matches=count==ids.size();
        if(matches&&count){std::string maps,output,after;const std::wstring ticks(j->startTicks.begin(),j->startTicks.end());
            if(!shell(j,L"cat /proc/"+std::to_wstring(r.game)+L"/maps",maps)||!readableRange(maps,begin,count*4))return false;
            if(!shell(j,L"test $(sed 's/.*) //' /proc/"+std::to_wstring(r.game)+L"/stat | cut -d ' ' -f20) = "+ticks+L" && dd if=/proc/"+std::to_wstring(r.game)+L"/mem bs=1 skip="+std::to_wstring(begin)+L" count="+std::to_wstring(count*4)+L" 2>/dev/null | xxd -p",output)||!bytes(output,data,count*4)||!readMemory(j,r,0x118798,24,after)||after!=meta)return false;
            for(size_t i=0;i<count;i++)if(u32(data,i*4)!=ids[i])matches=false;
        }
        if(matches){if(!zeroMailbox)return true;if(!readMemory(j,r,0x1187f8,1,latch))return false;if(latch[0]==0)return true;}
        if(!wait(250,j))return false;
    }while(GetTickCount64()<end&&!cancelled(j));return false;
}
bool Engine::applyIds(const std::shared_ptr<Job>&j,const Row&r,const std::vector<unsigned>&ids){
    // Native flags: +4 enables ID filtering, +28 is an ID mailbox. The game
    // consumes its nonzero pulse once; returning to zero releases that latch.
    // Clear the previous vector by its existing reset flag, never by writing
    // vector pointers or heap data from the host.
    if(!word(j,r,28,0)||!word(j,r,4,0)||!wait(250,j)||!waitFilter(j,r,{}))return false;
    if(!word(j,r,24,1)||!word(j,r,4,1)||!wait(250,j)||!waitFilter(j,r,{},true))return false;
    if(r.quick.function==1){bool cleared=false;const auto end=GetTickCount64()+6000;do{std::string flag;if(!readMemory(j,r,0x1187b4,1,flag))return false;if(flag[0]==0){cleared=true;break;}if(!wait(250,j))return false;}while(GetTickCount64()<end);if(!cleared)return false;}
    if(!word(j,r,24,0))return false;
    if(ids.empty())return word(j,r,4,0);
    std::vector<unsigned> accepted;
    for(auto id:ids){
        const bool special=r.quick.function==1&&id==5555;if(!special)accepted.push_back(id);
        if(!word(j,r,28,id)||!wait(100,j)||!waitFilter(j,r,accepted))return false;
        if(special){bool received=false;const auto end=GetTickCount64()+6000;do{std::string flag;if(!readMemory(j,r,0x1187b4,1,flag))return false;if(flag[0]==1){received=true;break;}if(!wait(250,j))return false;}while(GetTickCount64()<end);if(!received)return false;}
        if(!word(j,r,28,0)||!wait(200,j)||!waitFilter(j,r,accepted,true))return false;
    }
    return true;
}
void Engine::runtimeAction(const std::shared_ptr<Job>&j,Row row){
    auto finish=[&](bool ok,const wchar_t*detail){
        if(cancelled(j))return;{std::lock_guard<std::mutex> lock(mutex_);for(auto&r:state_.rows)if(r.index==j->index&&r.generation==j->generation){r.applyAt=0;r.actionPending=false;r.quickActive=ok&&row.intent==Intent::Quick?row.quick.function:0;}}
        report(j,ok?Phase::Connected:Phase::Failed,detail,row.game,row.base);
    };
    if(!ensureAdb(j)||!ensureRoot(j))return;
    DWORD pid=0;unsigned long long base=0;auto state=inspectContext(j,pid,base);
    if(row.intent==Intent::Reset&&state==ContextState::Absent){
        {std::lock_guard<std::mutex> lock(mutex_);for(auto&r:state_.rows)if(r.index==j->index&&r.generation==j->generation){r.actionPending=false;r.quickActive=0;r.applyAt=0;r.wanted=false;}}
        report(j,Phase::Idle,L"Đã lưu mặc định; không có chức năng đang chạy");return;
    }
    if(state!=ContextState::Verified||(row.game&&row.game!=pid)||(row.base&&row.base!=base)){finish(false,L"Phiên game đã đổi; chưa reset hoặc chạy chức năng");return;}
    row.game=pid;row.base=base;Row now;std::string elf,header,maps,stat;char ps=0;
    bool ok=fresh(j,now)&&now.vm==row.vm&&shell(j,L"dd if=/proc/"+std::to_wstring(pid)+L"/mem bs=1 skip="+std::to_wstring(base)+L" count=4096 2>/dev/null | xxd -p",elf)&&bytes(elf,header,4096)&&controlSegment(header)&&shell(j,L"cat /proc/"+std::to_wstring(pid)+L"/maps",maps);
    if(ok)for(auto off:resetOffsets())if(!writableControl(maps,base,off))ok=false;
    if(ok)ok=writableControl(maps,base,28)&&shell(j,L"cat /proc/"+std::to_wstring(pid)+L"/stat",stat)&&processStat(stat,pid,j->startTicks,ps)&&ps!='Z'&&ps!='X'&&ps!='x'&&(row.startTicks.empty()||row.startTicks==j->startTicks);
    if(!ok){finish(false,L"Chưa xác minh được vùng điều khiển; dừng lệnh");return;}
    report(j,Phase::Applying,row.intent==Intent::Reset?L"Reset All":L"Áp dụng chức năng rời",pid,base);
    std::string before;if(!readMemory(j,row,0x117044,52,before)){finish(false,L"Không đọc được trạng thái chức năng");return;}
    bool liveSame=row.quick.function==1?u32(before,0)==1&&u32(before,36)==1:u32(before,0)==11&&u32(before,20)==11;
    const bool reset=row.intent==Intent::Reset||needsQuickReset(row.quick,row.quickActive,liveSame);
    if(reset)ok=resetAll(j,row);
    if(ok&&row.intent==Intent::Quick){
        if(!validQuick(row.quick))ok=false;
        if(ok&&reset)for(const auto&s:quickStart(row.quick.function))if(!word(j,row,s.offset,s.value)||!wait(s.delay,j)){ok=false;break;}
        if(ok)ok=applyIds(j,row,row.quick.ids)&&word(j,row,12,row.quick.function==1&&row.quick.sell?1:0);
    }
    std::string after;DWORD afterPid=0;unsigned long long afterBase=0;
    if(ok)ok=readMemory(j,row,0x117044,52,after)&&context(j,afterPid,afterBase)&&afterPid==pid&&afterBase==base;
    if(ok&&row.intent==Intent::Quick)ok=(u32(after,0)==(row.quick.function==1?1u:11u))&&(row.quick.function==1?u32(after,36)==1:u32(after,20)==11)&&u32(after,4)==unsigned(!row.quick.ids.empty())&&u32(after,12)==unsigned(row.quick.function==1&&row.quick.sell);
    finish(ok,ok?(row.intent==Intent::Reset?L"Đã Reset All":row.quick.function==1?L"Câu Cá":L"Farm All"):L"Chưa xác nhận đủ thao tác; dừng, không tự lặp");
}
void Engine::apply(const std::shared_ptr<Job>&j,const Row&row){
    if(row.actionPending){runtimeAction(j,row);return;}
    if(!validConfig(row.config)||cancelled(j))return;if(!ensureAdb(j)||!ensureRoot(j))return;
    report(j,Phase::Applying,L"Áp dụng Bọ / Lọc / Bán",row.game,row.base);
    std::string elf,header,stat,maps;DWORD verifiedPid=0;unsigned long long verifiedBase=0;
    bool ok=context(j,verifiedPid,verifiedBase)&&verifiedPid==row.game&&verifiedBase==row.base&&shell(j,L"dd if=/proc/"+std::to_wstring(row.game)+L"/mem bs=1 skip="+std::to_wstring(row.base)+L" count=4096 2>/dev/null | xxd -p",elf)&&bytes(elf,header,4096)&&controlSegment(header)&&shell(j,L"cat /proc/"+std::to_wstring(row.game)+L"/maps",maps)&&writableControl(maps,row.base,0)&&writableControl(maps,row.base,36)&&shell(j,L"cat /proc/"+std::to_wstring(row.game)+L"/stat",stat);
    if(ok)for(auto off:resetOffsets())if(!writableControl(maps,row.base,off))ok=false;
    if(ok){std::string ticks;char state=0;ok=processStat(stat,row.game,ticks,state)&&ticks==row.startTicks&&state!='Z'&&state!='X'&&state!='x';if(ok)j->startTicks=ticks;}
    if(ok){std::string data,existing;ok=shell(j,L"dd if=/proc/"+std::to_wstring(row.game)+L"/mem bs=1 skip="+std::to_wstring(row.base+0x117044)+L" count=48 2>/dev/null | xxd -p",data)&&bytes(data,existing,48);if(ok&&sameControl(existing,row.config)){{std::lock_guard<std::mutex> lock(mutex_);for(auto&r:state_.rows)if(r.index==j->index&&r.generation==j->generation&&r.revision==row.revision)r.applyAt=0;}report(j,Phase::Connected,L"Đã nhận lại cấu hình đang chạy",row.game,row.base);return;}}
    if(ok)ok=resetAll(j,row)&&wait(500,j);
    if(ok&&row.config.function)ok=word(j,row,36,1)&&wait(100,j)&&word(j,row,8,row.config.function==2?1:2)&&wait(1000,j)&&word(j,row,0,4)&&wait(500,j);
    if(ok)ok=word(j,row,32,0)&&wait(500,j);
    if(ok&&row.config.filter)ok=word(j,row,32,unsigned(row.config.filter))&&wait(500,j);
    if(ok)ok=word(j,row,12,row.config.sell?1:0);
    if(cancelled(j))return;
    std::string output,values;DWORD pid=0;unsigned long long base=0;
    if(ok)ok=context(j,pid,base)&&pid==row.game&&base==row.base&&shell(j,L"dd if=/proc/"+std::to_wstring(pid)+L"/mem bs=1 skip="+std::to_wstring(base+0x117044)+L" count=48 2>/dev/null | xxd -p",output)&&bytes(output,values,48)&&u32(values,0)==(row.config.function?4u:0u)&&(!row.config.function||(u32(values,8)==(row.config.function==2?1u:2u)&&u32(values,36)==1))&&u32(values,32)==unsigned(row.config.filter)&&u32(values,12)==unsigned(row.config.sell);
    {std::lock_guard<std::mutex> lock(mutex_);for(auto&r:state_.rows)if(r.index==j->index&&r.generation==j->generation&&r.revision==row.revision){r.applyAt=0;r.quickActive=0;}}
    if(!ok){std::string processes;if(shell(j,L"ps -A -o PID,PPID,NAME; result=$?; echo LDMENU_PS_$result",processes)&&trim(processes).size()>=11&&trim(processes).compare(trim(processes).size()-11,11,"LDMENU_PS_0")==0){bool running=false;std::istringstream lines(processes);std::string line;while(std::getline(lines,line)){std::istringstream fields(line);std::string pid,parent,name;if(fields>>pid>>parent>>name&&name==Package)running=true;}if(!running){queueReconnect(j,L"Game đã tắt khi áp dụng; chờ kết nối lại");return;}}}
    report(j,ok?Phase::Connected:Phase::Failed,ok?L"Đã áp dụng cấu hình":L"Chưa xác nhận áp dụng cấu hình; dừng lệnh",row.game,row.base);
}
void Engine::connect(const std::shared_ptr<Job>&j){
    for(unsigned round=0,restarts=0;round<8&&!cancelled(j);round++){
        report(j,Phase::Checking,L"Kiểm tra LD và ADB");Row before;if(!fresh(j,before)){defer(j,Phase::WaitingContext,L"Chờ xác minh LD; chưa thực hiện kết nối");return;}
        {std::lock_guard<std::mutex> lock(mutex_);for(auto&r:state_.rows)if(r.index==j->index&&r.generation==j->generation){r.vm=before.vm;r.ui=before.ui;}}
        if(!ensureAdb(j))return;
        if(!ensureRoot(j))return;
        DWORD pid=0;unsigned long long base=0;
        const auto initial=inspectContext(j,pid,base);if(initial==ContextState::Verified){connected(j,pid,base);return;}
        if(initial==ContextState::Unknown){defer(j,Phase::WaitingContext,L"Chờ xác minh thư viện; chưa mở lại game");return;}
        if(!commitReady()){report(j,Phase::Failed,L"Commit trống dưới 2 GB; dừng kết nối");return;}
        if(!openGame(j)){
            if(cancelled(j))return;
            std::string marker,devices;bool echoOk=shell(j,L"echo LDCONTROL_ADB_READY",marker)&&trim(marker)=="LDCONTROL_ADB_READY";
            if(!echoOk){bool listed=command(true,L"devices",devices,j);if(classify(false,listed,devices,j->index)==Link::Missing&&ensureAdb(j)){continue;}}
            auto snap=snapshot();auto it=std::find_if(snap.rows.begin(),snap.rows.end(),[&](auto&r){return r.index==j->index;});if(it!=snap.rows.end()&&it->phase!=Phase::Failed&&!waitingPhase(it->phase))report(j,Phase::Failed,L"Không xác nhận được lệnh mở game");return;
        }
        j->loaded=true;
        report(j,Phase::Loading,L"Đang chờ game nạp thư viện");auto end=GetTickCount64()+ModuleLoadTimeoutMs;
        while(GetTickCount64()<end&&!cancelled(j)){if(!wait(2000,j))return;Row current;if(!fresh(j,current)||current.vm!=before.vm){report(j,Phase::Failed,L"LD đã đóng hoặc thay backend");return;}if(context(j,pid,base)){
                std::string text;if(shell(j,L"sha256sum /data/local/tmp/libmod.so",text)&&text.compare(0,64,Digest)==0)shell(j,L"rm -f /data/local/tmp/libmod.so",text);
                connected(j,pid,base);return;}}
        if(cancelled(j))return;
        // Only a fresh, complete absence proof can authorize a VM restart.
        const auto final=inspectContext(j,pid,base);if(final==ContextState::Verified){connected(j,pid,base);return;}
        if(final==ContextState::Unknown){defer(j,Phase::WaitingContext,L"Chưa xác minh được thư viện; không restart LD");return;}
        ULONGLONG retryAt=0;if(!reserveRestart(j,retryAt)){if(retryAt)defer(j,Phase::Cooldown,L"Đã đủ 5 lượt restart trong 30 phút; chờ giới hạn",retryAt);return;}
        restarts++;report(j,Phase::Repairing,L"Nạp thư viện thất bại; restart LD "+std::to_wstring(restarts)+L"/5");
        if(!restart(j,before)){report(j,Phase::Failed,L"Không xác minh được restart LD");return;}
        {std::lock_guard<std::mutex> lock(mutex_);for(auto&r:state_.rows)if(r.index==j->index)r.restarts=restarts;}
    }
    report(j,Phase::Failed,L"Đã hết lượt thử kết nối");
}
void Engine::loop(){
    ULONGLONG nextHealth=0;
    while(!cancelled({})){
        bool playChanged=false;{std::lock_guard<std::mutex> lock(mutex_);playChanged=advancePlayLocked(GetTickCount64());}if(playChanged)notify();
        bool refresh;{std::lock_guard<std::mutex> lock(mutex_);refresh=refresh_;}
        if(refresh)scan();
        bool connectedRows;{std::lock_guard<std::mutex> lock(mutex_);connectedRows=std::any_of(state_.rows.begin(),state_.rows.end(),[](auto&r){return r.wanted&&r.phase==Phase::Connected&&r.intent==Intent::Automatic;});}
        if(!connectedRows)nextHealth=0;else if(!nextHealth)nextHealth=GetTickCount64()+HealthIntervalMs;
        if(nextHealth&&GetTickCount64()>=nextHealth){monitor();nextHealth=GetTickCount64()+HealthIntervalMs;}
        std::shared_ptr<Job> job;Row selected;auto due=nextHealth?nextHealth:~0ULL;
        {std::lock_guard<std::mutex> lock(mutex_);selectJobLocked(GetTickCount64(),due,job,selected);}
        if(job){if(job->features)apply(job,selected);else if(job->recovery)recoverAdb(job,selected);else if(!cancelled(job))connect(job);{std::lock_guard<std::mutex> lock(mutex_);active_.reset();if(job->batch)nextPlay_=GetTickCount64()+playGap_;playChanged=advancePlayLocked(GetTickCount64());}if(playChanged||job->batch)notify();continue;}
        HANDLE events[]={stop_,wake_};const auto now=GetTickCount64();const DWORD sleep=due==~0ULL?INFINITE:DWORD(std::min<ULONGLONG>(due>now?due-now:0,MAXDWORD-1));WaitForMultipleObjects(2,events,FALSE,sleep);
    }
}
int tests(){int count=0,failed=0;auto check=[&](bool ok){count++;if(!ok){failed++;std::fprintf(stderr,"CHECK_FAILED %d\n",count);}};std::vector<Row> rows;
    check(parseList("0,LD,0,0,0,-1,-1,960,540,160\r\r\n2,LD-2,1,2,1,100,200,960,540,160\r\r\n",rows)&&rows.size()==2&&rows[1].index==2&&rows[1].vm==200);
    check(!parseList("2,LD-2,1,2,1,100,200,960,540,160\n2,Copy,1,2,1,100,200,960,540,160",rows));
    check(!parseList("2,LD-2,1,2,1,100,0,960,540,160",rows));check(!parseList("2,LD-2,1,2,1,100,4294967296,960,540,160",rows));check(!parseList("broken",rows));
    std::string devices="List of devices attached\nemulator-5558\tdevice\n";check(online(devices,2)&&!online(devices,1));check(!online(devices+"emulator-5558\tdevice\n",2));check(classify(true,false,"",2)==Link::Ready);check(classify(false,true,devices,1)==Link::Missing);check(classify(false,true,devices,2)==Link::Unknown);check(classify(false,false,"",2)==Link::Unknown);
    std::wstring component;check(launcher("com.vng.playtogether/.Main\r\r\n",component));check(!launcher("com.vng.playtogether/.Main;bad",component));check(!launcher("com.vng.playtogether/.Main\ncom.vng.playtogether/.Other",component));check(!launcher("com.haegin.playtogether/.Main",component));
    unsigned long long b=0;std::string maps="06fc0000-070cd000 r--p 00000000 08:12 12 /data/local/tmp/libmod.so (deleted)\n";check(moduleBase(maps,b)&&b==0x6fc0000);check(!moduleBase(maps+maps,b));check(!moduleBase("06fc0000-070cd000 r--p 00000000 08:12 12 /data/local/tmp/libmod.so.fake",b));std::string note(36,'\0');note[0]=4;note[4]=20;note[8]=3;note.replace(12,4,"GNU\0",4);std::copy(std::begin(BuildId),std::end(BuildId),note.begin()+16);check(knownNote(note));note[30]^=1;check(!knownNote(note));check(!knownNote("short"));
    check(classify(false,true,devices+"emulator-5558\tdevice\n",2)==Link::Unknown);
    check(classify(false,true,"List of devices attached\nemulator-5558\tunauthorized\n",2)==Link::Unknown);
    check(classify(false,true,"List of devices attached\nemulator-5558\toffline\n",2)==Link::Missing);
    check(quoteArg(L"push C:\\temp\\module.so /data/local/tmp/libmod.so")==L"\"push C:\\temp\\module.so /data/local/tmp/libmod.so\"");
    check(quoteArg(L"push \"C:\\my folder\\module.so\" /data/local/tmp/libmod.so")==L"\"push \\\"C:\\my folder\\module.so\\\" /data/local/tmp/libmod.so\"");
    check(ModuleLoadTimeoutMs==60000&&PostConnectDelayMs==60000);
    check(trim("  0\r\r\n")=="0");unsigned long long n;check(!number("18446744073709551616",n));
    Config cfg;cfg.function=13;cfg.filter=6;cfg.sell=true;cfg.reconnect=true;std::vector<SavedConfig> saved={{2,L"LD-2",cfg},{7,L"LD Tiếng Việt",{}}},decoded;
    check(decodeSettings(encodeSettings(saved),decoded)&&decoded.size()==2&&decoded[0].config.function==13&&decoded[0].config.filter==6&&decoded[0].config.reconnect&&decoded[0].config.sell&&decoded[1].name==L"LD Tiếng Việt");
    check(!decodeSettings("{broken",decoded));check(!decodeSettings("{\"schema\":2,\"devices\":[]}",decoded));
    check(!decodeSettings(encodeSettings({saved[0],saved[0]}),decoded));
    auto invalid=saved;invalid[0].index=-1;check(!decodeSettings(encodeSettings(invalid),decoded));invalid=saved;invalid[0].config.function=3;check(!decodeSettings(encodeSettings(invalid),decoded));invalid=saved;invalid[0].config.filter=7;check(!decodeSettings(encodeSettings(invalid),decoded));
    auto oversized=encodeSettings(saved);auto at=oversized.find("\"index\": 2");if(at!=std::string::npos)oversized.replace(at,10,"\"index\": 4294967298");check(!decodeSettings(oversized,decoded));
    check(validWord(4,1)&&validWord(8,0)&&!validWord(32,7)&&validWord(8,2)&&validWord(32,6)&&!validWord(20,1)&&!validWord(48,1)&&!validWord(28,2147483648u));
    const std::string writable="07000000-07100000 rw-p 00000000 08:12 12 /data/local/tmp/libmod.so (deleted)\n";
    check(writableControl(writable,0x06f00000,0));check(!writableControl(writable,0x06d00000,0));check(!writableControl("07000000-07100000 r--p 00000000 08:12 12 /data/local/tmp/libmod.so\n",0x06f00000,0));check(littleHex(0x1234abcd)=="cdab3412");
    check(writableControl("070d6000-070d7000 rw-p 00114000 08:12 12 /data/local/tmp/libmod.so (deleted)\n070d7000-070ed000 rw-p 00000000 00:00 0\n",0x06fc0000,0));
    check(!writableControl("070d7000-070ed000 rw-p 00000000 00:00 0\n",0x06fc0000,0));
    check(!writableControl("070d6000-070d7000 rw-p 00114000 08:12 12 /data/local/tmp/libmod.so\n070d7000-070ed000 rw-p 00000000 08:12 20 /other.so\n",0x06fc0000,0));
    std::string elf(120,'\0');auto set32=[&](size_t p,unsigned value){for(unsigned i=0;i<4;i++)elf[p+i]=char(value>>(i*8));};set32(0,0x464c457f);elf[4]=2;elf[5]=1;set32(16,(183u<<16)|3);set32(32,64);elf[54]=56;elf[56]=1;set32(64,1);set32(68,6);set32(80,0x116308);set32(104,93208);check(controlSegment(elf));set32(68,4);check(!controlSegment(elf));check(!controlSegment("short"));
    GlobalSettings common;common.defaults=cfg;common.settleSeconds=90;GlobalSettings commonRead;
    check(decodeSettings(encodeSettings(saved,common),decoded,&commonRead)&&commonRead.settleSeconds==90&&commonRead.defaults.function==13&&commonRead.defaults.filter==6&&commonRead.defaults.reconnect&&commonRead.defaults.sell&&decoded.size()==2);
    check(decodeSettings("{\"schema\":1,\"devices\":[]}",decoded,&commonRead)&&commonRead.settleSeconds==60&&commonRead.defaults.function==0&&!commonRead.defaults.reconnect);
    common.settleSeconds=600;check(decodeSettings(encodeSettings({},common),decoded,&commonRead)&&commonRead.settleSeconds==600);common.settleSeconds=601;check(!decodeSettings(encodeSettings({},common),decoded,&commonRead));
    common.settleSeconds=0;check(validGlobal(common)&&decodeSettings(encodeSettings({},common),decoded,&commonRead)&&commonRead.settleSeconds==0);common.defaults.function=7;check(!validGlobal(common)&&!decodeSettings(encodeSettings({},common),decoded,&commonRead));
    common={};common.playDelaySeconds=17;common.playTargets={{2,L"LD-2"},{9,L"LD Tiếng Việt"}};
    check(decodeSettings(encodeSettings(saved,common),decoded,&commonRead)&&commonRead.playDelaySeconds==17&&commonRead.playTargets.size()==2&&commonRead.playTargets[1].name==L"LD Tiếng Việt"&&decoded.size()==2);
    common.playDelaySeconds=3601;check(!validGlobal(common)&&!decodeSettings(encodeSettings({},common),decoded));common.playDelaySeconds=0;check(validGlobal(common));
    common.playTargets.push_back({2,L"Duplicate"});check(!validGlobal(common)&&!decodeSettings(encodeSettings({},common),decoded));common.playTargets={{-1,L"Bad"}};check(!validGlobal(common));common.playTargets={{3,L""}};check(!validGlobal(common));
    {
        Engine controller;Row open;open.index=2;open.name=L"LD-2";open.running=true;Row closed;closed.index=3;closed.name=L"LD-3";Row attached;attached.index=4;attached.name=L"LD-4";attached.running=true;attached.wanted=true;attached.phase=Phase::Connected;attached.game=123;
        controller.state_.rows={open,closed,attached};controller.state_.global.playDelaySeconds=17;controller.state_.global.playTargets={{2,L"LD-2"},{3,L"LD-3"},{4,L"LD-4"}};std::wstring error;
        check(controller.play(error)&&controller.state_.rows[0].wanted&&controller.state_.rows[0].playQueued&&controller.state_.rows[0].phase==Phase::Queued&&controller.playGap_==17000);
        check(!controller.state_.rows[1].wanted&&controller.state_.rows[2].wanted&&controller.state_.rows[2].phase==Phase::Connected&&controller.state_.rows[2].game==123);
        const auto generation=controller.state_.rows[0].generation;check(controller.play(error)&&controller.state_.rows[0].generation==generation&&controller.state_.rows[0].wanted);
        controller.state_.playRunning=false;controller.state_.global.playTargets={{2,L"Renamed"}};check(!controller.play(error));controller.state_.global.playTargets.clear();check(!controller.play(error));
    }
    {
        Engine controller;Row current;current.index=1;current.running=true;current.wanted=true;current.game=123;current.phase=Phase::Connected;Row pending;pending.index=2;pending.running=true;pending.wanted=true;pending.playQueued=true;pending.phase=Phase::Queued;controller.state_.rows={current,pending};controller.state_.playRunning=true;controller.nextPlay_=1000;controller.active_=std::make_shared<Job>();controller.active_->index=1;controller.active_->batch=true;
        controller.pausePlay();check(controller.state_.playStopping&&controller.state_.playRunning&&!controller.active_->cancelled);
        std::wstring resumeError;const auto pendingGeneration=controller.state_.rows[1].generation;check(controller.play(resumeError)&&!controller.state_.playStopping&&controller.state_.playRunning&&!controller.active_->cancelled&&controller.state_.rows[1].generation==pendingGeneration);controller.pausePlay();
        check(!controller.advancePlayLocked(2000)&&controller.state_.rows[1].wanted);controller.active_.reset();
        controller.pausePlay();check(controller.nextPlay_==1000);check(!controller.advancePlayLocked(999)&&controller.state_.playRunning);check(controller.advancePlayLocked(1000)&&!controller.state_.playRunning&&!controller.state_.playStopping);
        check(controller.state_.rows[0].wanted&&controller.state_.rows[0].game==123&&controller.state_.rows[0].phase==Phase::Connected);
        check(!controller.state_.rows[1].wanted&&!controller.state_.rows[1].playQueued&&controller.state_.rows[1].phase==Phase::Idle);
        controller.state_.playRunning=true;controller.nextPlay_=3000;check(!controller.advancePlayLocked(2999));check(controller.advancePlayLocked(3000)&&!controller.state_.playRunning);
    }
    {
        std::string values(48,'\0');Config c;check(sameControl(values,c));c.function=2;c.filter=1;check(!sameControl(values,c));auto write=[&](size_t offset,unsigned value){for(unsigned i=0;i<4;i++)values[offset+i]=char(value>>(8*i));};write(0,4);write(8,1);write(32,1);write(36,1);check(sameControl(values,c));write(8,2);check(!sameControl(values,c));c.function=13;check(sameControl(values,c));write(12,1);check(!sameControl(values,c));c.sell=true;check(sameControl(values,c));write(32,0);check(!sameControl(values,c));check(!sameControl("short",c));
    }
    {
        auto stat=[](DWORD pid,const char*ticks,char state='S'){std::string value=std::to_string(pid)+" (game (worker)) "+state;for(unsigned i=4;i<22;i++)value+=" 0";return value+" "+ticks+" 0";};
        std::string ticks;char state=0;check(processStat(stat(100,"12345"),100,ticks,state)&&ticks=="12345"&&state=='S');check(!processStat(stat(100,"12345"),101,ticks,state));check(!processStat("100 (bad) S 0",100,ticks,state));
        Row r;r.game=100;r.bootId="11111111-2222-4333-8444-555555555555";r.startTicks="12345";
        auto frame=[&](const std::string&boot,const std::string&pids,const std::string&st){return "LDMENU_HEALTH_V1\nBOOT "+boot+"\nPIDS "+pids+"\nSTAT "+st+"\nLDMENU_HEALTH_END\n";};
        check(parseHealth(frame(r.bootId,"0 100 200","0 "+stat(100,"12345")),r)==Health::Alive);
        check(parseHealth(frame(r.bootId,"1","1"),r)==Health::Missing);
        check(parseHealth(frame(r.bootId,"0 100 200","1"),r)==Health::Unknown);
        check(parseHealth(frame(r.bootId,"0 100 100","0 "+stat(100,"12345")),r)==Health::Unknown);
        check(parseHealth(frame(r.bootId,"0 100","0 "+stat(100,"99999")),r)==Health::Missing);
        check(parseHealth(frame(r.bootId,"0 100","0 "+stat(100,"12345",'Z')),r)==Health::Missing);
        check(parseHealth(frame("aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee","1","1"),r)==Health::Rebooted);
        check(parseHealth("error: device offline",r)==Health::Unknown);
        check(number("18446744073709551615",n)&&n==~0ULL);check(!number("18446744073709551616",n));
        unsigned long long base=0;const std::string loaded="01000000-01001000 r--p 00000000 08:12 12 /data/local/tmp/libmod.so (deleted)\n";
        check(classifyMaps(loaded,base)==ContextState::Verified&&base==0x1000000);
        check(classifyMaps(loaded+loaded,base)==ContextState::Unknown);check(classifyMaps("permission denied\n",base)==ContextState::Unknown);check(classifyMaps("",base)==ContextState::Unknown);
        check(classifyMaps("01000000-01001000 rw-p 00000000 00:00 18446744073709551615 [anon:heap]\n",base)==ContextState::Absent);
    }
    {
        Row r;ULONGLONG retry=0;for(unsigned i=1;i<=5;i++)check(reserveRestartAt(r,i*100,retry));
        check(!reserveRestartAt(r,600,retry)&&retry==RestartWindowMs+100);
        check(reserveRestartAt(r,RestartWindowMs+100,retry)&&r.restartTimes[0]==200);
        check(!reserveRestartAt(r,RestartWindowMs+101,retry)&&retry==RestartWindowMs+200);
        Engine c;Row row;row.index=2;row.name=L"LD-2";row.wanted=true;row.running=true;row.restartTimes=r.restartTimes;c.state_.rows.push_back(row);c.toggle(2);c.toggle(2);check(c.state_.rows[0].restartTimes==r.restartTimes);
    }
    {
        Engine c;Row first;first.index=1;first.name=L"LD-1";first.wanted=first.running=true;first.phase=Phase::Connected;first.applyAt=500;Row second=first;second.index=2;second.name=L"LD-2";second.applyAt=200;c.state_.rows={first,second};
        ULONGLONG due=~0ULL;std::shared_ptr<Job> job;Row picked;check(c.selectJobLocked(1000,due,job,picked)&&picked.index==2&&job->features);
        c.state_.rows[1].applyAt=1200;Row manual=first;manual.index=3;manual.phase=Phase::Queued;manual.applyAt=0;c.state_.rows.push_back(manual);c.nextConnect_=300;due=~0ULL;job.reset();check(c.selectJobLocked(1000,due,job,picked)&&picked.index==3&&!job->features);
        c.state_.rows[0].applyAt=2000;c.state_.rows[1].applyAt=2200;c.state_.rows[2].phase=Phase::WaitingAdb;c.state_.rows[2].retryAt=1500;due=~0ULL;job.reset();check(!c.selectJobLocked(1000,due,job,picked)&&due==1500);
        due=~0ULL;check(c.selectJobLocked(1600,due,job,picked)&&picked.index==3&&job->recovery);
        c.state_.rows.clear();due=~0ULL;job.reset();check(!c.selectJobLocked(10000,due,job,picked)&&due==~0ULL);
    }
    {
        Engine c;Row r;r.index=2;r.name=L"LD-2";r.running=r.wanted=true;r.generation=7;r.phase=Phase::Connected;r.config.reconnect=true;r.game=100;c.state_.rows.push_back(r);
        auto job=std::make_shared<Job>();job->index=2;job->name=r.name;job->generation=7;c.active_=job;c.queueReconnect(job,L"retry");check(c.state_.rows[0].automatic&&c.state_.rows[0].phase==Phase::Queued);
        c.state_.rows[0].config.reconnect=false;c.updateReconnectLocked(c.state_.rows[0],true);check(job->cancelled&&c.state_.rows[0].generation==8&&c.state_.rows[0].phase==Phase::Failed);
        c.report(job,Phase::Connected,L"stale",100,0x1000);check(c.state_.rows[0].phase==Phase::Failed);
        c.state_.rows[0].config.reconnect=true;c.updateReconnectLocked(c.state_.rows[0],false);check(c.state_.rows[0].phase==Phase::Queued&&c.state_.rows[0].generation==9);
        c.state_.rows[0].automatic=false;auto manual=std::make_shared<Job>();manual->index=2;manual->name=r.name;manual->generation=9;c.active_=manual;c.state_.rows[0].config.reconnect=false;c.updateReconnectLocked(c.state_.rows[0],true);check(!manual->cancelled&&c.state_.rows[0].phase==Phase::Queued&&c.state_.rows[0].generation==9);
        c.halt(1);c.halt(2);check(c.failure()==1&&c.cancelled({}));
    }
    {
        std::vector<unsigned> ids={99};check(parseIds(L"",ids)&&ids.empty());check(parseIds(L" 12, 345,2147483647 ",ids)&&ids==std::vector<unsigned>({12,345,2147483647}));
        check(!parseIds(L"0",ids)&&!parseIds(L"-1",ids)&&!parseIds(L"2147483648",ids));check(!parseIds(L"12,",ids)&&!parseIds(L",12",ids)&&!parseIds(L"1,,2",ids)&&!parseIds(L"1 2",ids)&&!parseIds(L"1; echo bad",ids));
        check(parseIds(L"5555, 5555",ids)&&ids.size()==2);check(parseIds(idsText(ids),ids)&&ids==std::vector<unsigned>({5555,5555}));
        std::wstring many=L"1";for(unsigned i=1;i<64;i++)many+=L",1";check(parseIds(many,ids)&&ids.size()==64);check(!parseIds(many+L",1",ids));
        QuickConfig quick;quick.function=1;quick.ids={1,2};quick.sell=true;check(validQuick(quick));quick.function=14;check(!validQuick(quick));quick.sell=false;check(validQuick(quick));quick.function=2;check(!validQuick(quick));quick.function=1;quick.ids={0};check(!validQuick(quick));
        quick={};check(quick.function==0&&validQuick(quick));quick.sell=true;check(!validQuick(quick));quick.sell=false;quick.ids={1};check(!validQuick(quick));quick={};quick.function=1;check(!needsQuickReset(quick,1,true)&&needsQuickReset(quick,1,false)&&needsQuickReset(quick,14,true)&&needsQuickReset(quick,0,true));
        check(resetOffsets()==std::vector<unsigned>({0,36,48,12,8,32,20,4,24}));for(auto offset:resetOffsets())check(validWord(offset,0));
        const auto fish=quickStart(1),farm=quickStart(14);check(fish.size()==2&&fish[0].offset==36&&fish[0].value==1&&fish[0].delay==100&&fish[1].offset==0&&fish[1].value==1);
        check(farm.size()==2&&farm[0].offset==0&&farm[0].value==11&&farm[1].offset==20&&farm[1].value==11&&quickStart(2).empty());
        std::string vector(24,'\0');auto store=[&](size_t p,unsigned long long value){for(unsigned k=0;k<8;k++)vector[p+k]=char(value>>(8*k));};unsigned long long begin=0;size_t size=0;
        check(vectorBounds(vector,begin,size)&&size==0);store(0,4096);store(8,4104);store(16,4112);check(vectorBounds(vector,begin,size)&&size==2);store(8,4092);check(!vectorBounds(vector,begin,size));store(8,4104);store(16,4100);check(!vectorBounds(vector,begin,size));
        check(readableRange("1000-2000 rw-p 0 00:00 0\n",4096,256)&&!readableRange("1000-2000 ---p 0 00:00 0\n",4096,4)&&!readableRange("1000-2000 rw-p 0 00:00 0\n",8190,4));
    }
    {
        Engine c;Row r;r.index=2;r.name=L"LD-2";r.running=true;r.wanted=true;r.game=100;r.base=0x1000000;r.phase=Phase::Connected;r.config={2,1,true,true};r.applyAt=50;r.playQueued=true;c.state_.rows.push_back(r);
        auto old=std::make_shared<Job>();old->index=2;old->generation=0;old->features=true;c.active_=old;
        c.queueActionLocked(c.state_.rows[0],Intent::Reset);const auto&reset=c.state_.rows[0];check(old->cancelled&&reset.actionPending&&reset.intent==Intent::Reset&&reset.phase==Phase::Connected&&!reset.playQueued&&reset.generation==1);
        check(reset.config.reconnect&&reset.config.function==2); // runtime action alone never mutates the saved profile
        c.active_.reset();std::shared_ptr<Job> action;Row picked;ULONGLONG due=~0ULL;check(c.selectJobLocked(GetTickCount64()+1,due,action,picked)&&action->features&&picked.intent==Intent::Reset);
        c.state_.rows[0].game=0;c.state_.rows[0].base=0;c.queueActionLocked(c.state_.rows[0],Intent::Reset);check(c.state_.rows[0].phase==Phase::Connected); // no connect/open job, even without a game
        c.state_.rows[0].actionPending=false;c.active_.reset();std::wstring error;QuickConfig q;q.function=1;q.ids={12};check(c.runQuick(2,L"LD-2",q,error)&&c.state_.rows[0].phase==Phase::Queued&&c.state_.rows[0].intent==Intent::Quick);
        check(!c.runQuick(2,L"LD-2",q,error)&&!c.runQuick(2,L"Renamed",q,error));
        auto current=std::make_shared<Job>();current->index=2;current->name=L"LD-2";current->generation=c.state_.rows[0].generation;c.state_.rows[0].config.reconnect=true;c.queueReconnect(current,L"lost");check(c.state_.rows[0].phase==Phase::Failed&&!c.state_.rows[0].automatic&&!c.state_.rows[0].actionPending);
        q={};c.active_.reset();check(c.runQuick(2,L"LD-2",q,error)&&c.state_.rows[0].intent==Intent::Reset&&c.state_.rows[0].phase==Phase::Connected&&c.state_.rows[0].config.reconnect);
    }
    std::printf("BACKEND_TESTS %d/%d\n",count-failed,count);return failed?1:0;
}
}
