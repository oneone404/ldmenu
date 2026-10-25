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
#include <tlhelp32.h>

namespace ldcontrol {
static constexpr wchar_t Console[]=L"D:\\LDPlayer\\LDPlayer9\\ldconsole.exe";
static constexpr wchar_t Adb[]=L"D:\\LDPlayer\\LDPlayer9\\adb.exe";
static constexpr char Digest[]="1eb769fb80291fae60215276575acecc5363c59e9433965014c6767ffefecd2a";
static constexpr unsigned char BuildId[]={0x57,0x5b,0x33,0x22,0x44,0xc8,0x36,0xfd,0x55,0x64,0x84,0xdb,0xb8,0x27,0xdd,0x79,0x91,0x10,0xe4,0x39};
static constexpr char Package[]="com.vng.playtogether";
struct Handle { HANDLE h; explicit Handle(HANDLE v=nullptr):h(v){} ~Handle(){if(h&&h!=INVALID_HANDLE_VALUE)CloseHandle(h);} Handle(const Handle&)=delete; };
struct CommandGuard { HANDLE process;~CommandGuard(){if(WaitForSingleObject(process,0)==WAIT_TIMEOUT){TerminateProcess(process,1);WaitForSingleObject(process,1000);}} };
static void killAdbProcesses(){
    Handle snap(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0));if(snap.h==INVALID_HANDLE_VALUE)return;
    PROCESSENTRY32W pe{};pe.dwSize=sizeof(pe);if(!Process32FirstW(snap.h,&pe))return;
    do{if(_wcsicmp(pe.szExeFile,L"adb.exe")==0){Handle proc(OpenProcess(PROCESS_TERMINATE|SYNCHRONIZE,FALSE,pe.th32ProcessID));if(proc.h){TerminateProcess(proc.h,1);WaitForSingleObject(proc.h,500);}}}while(Process32NextW(snap.h,&pe));
}
static void refreshRepairBudget(unsigned&repairs,ULONGLONG lastRepair,ULONGLONG now){if(lastRepair&&now>=lastRepair+300000ULL)repairs=0;}
static bool canRepairAdb(bool listed,const std::string&listing){return !listed||listing.find("List of devices attached")==std::string::npos;}
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
std::wstring phaseText(Phase p){switch(p){case Phase::Idle:return L"Sẵn sàng";case Phase::Queued:return L"Đang chờ";case Phase::Checking:return L"Kiểm tra";case Phase::Repairing:return L"Sửa kết nối";case Phase::Opening:return L"Mở game";case Phase::Loading:return L"Đang kết nối";case Phase::Connected:return L"Thành công";case Phase::Applying:return L"Áp dụng cấu hình";case Phase::WaitingAdb:return L"Chờ ADB";case Phase::WaitingContext:return L"Chờ xác minh";case Phase::Cooldown:return L"Tạm chờ";case Phase::Failed:return L"Thất bại";case Phase::Closed:return L"Đã tắt";}return {};}
Engine::Engine(){SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);stop_=CreateEventW(nullptr,TRUE,FALSE,nullptr);wake_=CreateEventW(nullptr,FALSE,FALSE,nullptr);}
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
    for(;;){if(cancelled(j)||GetTickCount64()>=deadline){ok=false;break;}const bool exited=WaitForSingleObject(pi.hProcess,0)==WAIT_OBJECT_0;DWORD avail=0;if(PeekNamedPipe(r,nullptr,0,nullptr,&avail,nullptr)&&avail){char buf[4096];DWORD got=0;if(!ReadFile(r,buf,std::min<DWORD>(avail,sizeof(buf)),&got,nullptr)||out.size()+got>1024*1024){ok=false;break;}out.append(buf,got);continue;}if(exited){done=true;break;}WaitForMultipleObjects(2,pending,FALSE,20);}
    DWORD code=1;if(done)GetExitCodeProcess(pi.hProcess,&code);else{TerminateProcess(pi.hProcess,1);WaitForSingleObject(pi.hProcess,1000);}return ok&&done&&code==0;
}
bool Engine::shell(const std::shared_ptr<Job>&j,const std::wstring&text,std::string&out,DWORD timeout){
    if(j->bootId.empty())return command(true,L"-s emulator-"+std::to_wstring(5554+2*j->index)+L" shell "+quoteArg(text),out,j,timeout);
    const std::wstring boot(j->bootId.begin(),j->bootId.end());const auto guarded=L"test $(cat /proc/sys/kernel/random/boot_id) = "+boot+L" && { "+text+L"; }";
    return command(true,L"-s emulator-"+std::to_wstring(5554+2*j->index)+L" shell "+quoteArg(guarded),out,j,timeout);
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
    job->index=selected.index;job->name=selected.name;job->generation=selected.generation;job->bootId=selected.bootId;job->rootReady=selected.rootReady;active_=job;
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
        auto j=std::make_shared<Job>();j->index=r.index;j->name=r.name;j->generation=r.generation;j->bootId=r.bootId;j->rootReady=r.rootReady;
        {std::lock_guard<std::mutex> lock(mutex_);auto row=std::find_if(state_.rows.begin(),state_.rows.end(),[&](auto&v){return v.index==r.index&&v.generation==r.generation&&v.wanted&&v.phase==Phase::Connected;});if(row==state_.rows.end())continue;active_=j;}
        std::string output;const auto health=command(true,L"-s emulator-"+std::to_wstring(5554+2*r.index)+L" shell "+quoteArg(healthScript(r.game)),output,j,HealthTimeoutMs)?parseHealth(output,r):Health::Unknown;
        bool lost=false,repair=false,valid=false;
        {std::lock_guard<std::mutex> lock(mutex_);for(auto&row:state_.rows)if(row.index==r.index&&row.generation==r.generation&&row.wanted&&row.phase==Phase::Connected&&!cancelled(j)){
            valid=true;if(health==Health::Unknown){row.misses=0;row.probeErrors=std::min(row.probeErrors+1,2u);repair=row.probeErrors>=2;}else{row.probeErrors=0;row.misses=health==Health::Alive?0:health==Health::Rebooted?2:std::min(row.misses+1,2u);lost=row.misses>=2;}
            if(lost){row.applyAt=0;row.misses=0;if(health==Health::Rebooted){row.bootId.clear();row.startTicks.clear();row.rootReady=false;}}
        }}
        if(valid&&repair&&!cancelled(j)){
            if(ensureAdb(j)){
                {std::lock_guard<std::mutex> lock(mutex_);for(auto&row:state_.rows)if(row.index==r.index&&row.generation==r.generation){row.bootId=j->bootId;row.rootReady=j->rootReady;row.probeErrors=0;}}
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
        std::string current;unsigned long long uid=0;
        if(shell(j,L"id -u",current)&&number(trim(current),uid)){j->rootReady=(uid==0);adbRepairs_=0;return true;}
    }
    std::string text,listing;auto marker=[&]{
        j->bootId.clear();j->rootReady=false;if(!shell(j,L"cat /proc/sys/kernel/random/boot_id",text,HealthTimeoutMs))return false;const auto boot=trim(text);if(!validBoot(boot))return false;
        j->bootId=boot;unsigned long long uid=0;if(!shell(j,L"id -u",text,HealthTimeoutMs)||!number(trim(text),uid)){j->bootId.clear();return false;}
        j->rootReady=(uid==0);adbRepairs_=0;return true;
    };
    if(marker())return true;
    if(cancelled(j))return false;
    bool listed=command(true,L"devices",listing,j,HealthTimeoutMs);Link link=classify(false,listed,listing,j->index);
    if(link==Link::Unknown&&!canRepairAdb(listed,listing)){
        command(true,L"-s emulator-"+std::to_wstring(5554+2*j->index)+L" reconnect",text,j,HealthTimeoutMs);
        if(wait(1000,j)&&marker())return true;
    }
    Row row;if(!fresh(j,row)){report(j,Phase::Failed,L"LD đã tắt hoặc đổi danh tính");return false;}
    const auto now=GetTickCount64();refreshRepairBudget(adbRepairs_,lastRepair_,now);
    if(adbRepairs_>=2){defer(j,Phase::WaitingAdb,L"Chờ giới hạn phục hồi ADB (5 phút)",std::max(now+HealthIntervalMs,lastRepair_+300000ULL));return false;}
    if(lastRepair_&&now<lastRepair_+60000ULL){defer(j,Phase::WaitingAdb,L"Chờ giới hạn phục hồi ADB",std::max(now+HealthIntervalMs,lastRepair_+60000ULL));return false;}
    report(j,Phase::Repairing,L"Mất ADB; khởi động lại ADB server");adbRepairs_++;lastRepair_=GetTickCount64();
    command(true,L"kill-server",text,j,HealthTimeoutMs);killAdbProcesses();
    if(!wait(300,j))return false;
    if(!command(true,L"start-server",text,j)){defer(j,Phase::WaitingAdb,L"Chờ ADB server hồi lại");return false;}
    auto end=GetTickCount64()+20000;while(GetTickCount64()<end&&!cancelled(j)){if(marker())return true;if(!wait(1000,j))break;}
    defer(j,Phase::WaitingAdb,L"ADB chưa khôi phục; tiếp tục chờ");return false;
}
void Engine::recoverAdb(const std::shared_ptr<Job>&j,const Row&r){
    std::string boot;const auto prefix=L"-s emulator-"+std::to_wstring(5554+2*j->index)+L" shell ";
    if(!command(true,prefix+L"cat /proc/sys/kernel/random/boot_id",boot,j,HealthTimeoutMs)||!validBoot(trim(boot))){
        refreshRepairBudget(adbRepairs_,lastRepair_,GetTickCount64());
        if(adbRepairs_<2&&(!lastRepair_||GetTickCount64()>=lastRepair_+60000ULL)){j->bootId.clear();if(!ensureAdb(j))return;}
        else{defer(j,Phase::WaitingAdb,L"Chờ ADB hồi lại");return;}
    }
    if(cancelled(j))return;Row now;if(!fresh(j,now)){defer(j,Phase::WaitingAdb,L"Chờ xác minh LD sau khi ADB hồi");return;}
    if(j->bootId.empty()&&!ensureAdb(j))return;
    {std::lock_guard<std::mutex> lock(mutex_);for(auto&row:state_.rows)if(row.index==j->index&&row.generation==j->generation){row.vm=now.vm;row.ui=now.ui;row.bootId=j->bootId;row.rootReady=j->rootReady;row.probeErrors=0;row.misses=0;}}
    if(!r.game){if(r.automatic)queueReconnect(j,L"ADB đã hồi; chờ kết nối lại");else report(j,Phase::Queued,L"ADB đã hồi; chờ kết nối");return;}
    std::string output;const auto health=command(true,prefix+quoteArg(healthScript(r.game)),output,j,HealthTimeoutMs)?parseHealth(output,r):Health::Unknown;
    if(health==Health::Alive){report(j,Phase::Connected,L"ADB đã hồi; game vẫn đang chạy",r.game,r.base);return;}
    if(health==Health::Missing||health==Health::Rebooted){queueReconnect(j,L"ADB đã hồi; chờ kết nối lại game");return;}
    defer(j,Phase::WaitingAdb,L"ADB đã phản hồi; chờ xác minh tiến trình game");
}
bool Engine::ensureRoot(const std::shared_ptr<Job>&j){
    if(j->bootId.empty())return false;
    auto remember=[&]{j->rootReady=true;std::lock_guard<std::mutex> lock(mutex_);for(auto&r:state_.rows)if(r.index==j->index&&r.generation==j->generation){r.bootId=j->bootId;r.rootReady=true;}return true;};
    if(j->rootReady)return remember();
    std::string text;
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
static bool parseContextFrame(const std::string&text,DWORD pid,std::string&ticksOut){
    std::istringstream vlines(text);std::vector<std::string> v;std::string line;
    while(std::getline(vlines,line)){line=trim(line);if(!line.empty())v.push_back(line);if(v.size()>5)return false;}
    if(v.size()!=5||v[0]!="LDMENU_CTX_V1"||v[4]!="LDMENU_CTX_END"||v[1].rfind("STAT1 ",0)||v[2].rfind("NOTE ",0)||v[3].rfind("STAT2 ",0))return false;
    std::string ticks,afterTicks,note;char state=0,afterState=0;
    if(!processStat(v[1].substr(6),pid,ticks,state)||state=='Z'||state=='X'||state=='x'||!bytes(v[2].substr(5),note,36)||!knownNote(note)||!processStat(v[3].substr(6),pid,afterTicks,afterState)||ticks!=afterTicks||afterState=='Z'||afterState=='X'||afterState=='x')return false;
    ticksOut=std::move(ticks);return true;
}
bool Engine::context(const std::shared_ptr<Job>&j,DWORD&pid,unsigned long long&base,std::string*mapsOut,std::string*ticksOut){return inspectContext(j,pid,base,mapsOut,ticksOut)==ContextState::Verified;}
ContextState Engine::inspectContext(const std::shared_ptr<Job>&j,DWORD&pid,unsigned long long&base,std::string*mapsOut,std::string*ticksOut){
    std::string text;if(!shell(j,L"ps -A -o PID,PPID,NAME; result=$?; echo LDMENU_PS_$result",text))return ContextState::Unknown;
    text=trim(text);const std::string psEnd="LDMENU_PS_0";if(text.size()<psEnd.size()||text.compare(text.size()-psEnd.size(),psEnd.size(),psEnd))return ContextState::Unknown;text.resize(text.size()-psEnd.size());
    std::istringstream lines(text);std::string line;std::vector<DWORD> candidates;bool header=false;
    while(std::getline(lines,line)){std::istringstream row(line);std::string p,parent,name,extra;unsigned long long n=0;if(!(row>>p>>parent>>name))continue;if(p=="PID"&&parent=="PPID"&&name=="NAME"){header=true;continue;}if(name==Package){if(row>>extra||!number(p,n)||!n||n>MAXDWORD||candidates.size()>=4||std::find(candidates.begin(),candidates.end(),DWORD(n))!=candidates.end())return ContextState::Unknown;candidates.push_back(DWORD(n));}}
    if(!header)return ContextState::Unknown;if(candidates.empty())return ContextState::Absent;
    std::string verifiedMaps;
    auto maps=[&](DWORD p,unsigned long long&b,std::string&raw){std::string out;if(!shell(j,L"cat /proc/"+std::to_wstring(p)+L"/maps && echo LDMENU_MAPS_OK",out))return ContextState::Unknown;out=trim(out);const std::string end="LDMENU_MAPS_OK";if(out.size()<end.size()||out.compare(out.size()-end.size(),end.size(),end))return ContextState::Unknown;out.resize(out.size()-end.size());raw=out;return classifyMaps(out,b);};
    unsigned matches=0;for(auto p:candidates){unsigned long long b=0;std::string raw;const auto state=maps(p,b,raw);if(state==ContextState::Unknown)return state;if(state==ContextState::Verified){pid=p;base=b;verifiedMaps=std::move(raw);matches++;}}
    if(!matches)return ContextState::Absent;if(matches!=1)return ContextState::Unknown;
    const auto verifyScript=L"st1=$(cat /proc/"+std::to_wstring(pid)+L"/stat 2>/dev/null) || exit 1; note=$(dd if=/proc/"+std::to_wstring(pid)+L"/mem bs=1 skip="+std::to_wstring(base+0x2d0)+L" count=36 2>/dev/null | xxd -p | tr -d '[:space:]') || exit 1; st2=$(cat /proc/"+std::to_wstring(pid)+L"/stat 2>/dev/null) || exit 1; printf 'LDMENU_CTX_V1\\nSTAT1 %s\\nNOTE %s\\nSTAT2 %s\\nLDMENU_CTX_END\\n' \"$st1\" \"$note\" \"$st2\"";
    std::string ticks;if(!shell(j,verifyScript,text)||!parseContextFrame(text,pid,ticks))return ContextState::Unknown;
    if(mapsOut)*mapsOut=std::move(verifiedMaps);if(ticksOut)*ticksOut=std::move(ticks);
    return ContextState::Verified;
}
bool Engine::openGame(const std::shared_ptr<Job>&j){
    Row before;if(!fresh(j,before))return false;std::string text;
    if(!j->rootReady&&(!shell(j,L"id -u",text)||trim(text)!="0")){report(j,Phase::Failed,L"ADB chưa có quyền root");return false;}
    const auto loader=L"old=0; merged=0; p=/data/adb/modules/zygisk_il2cppdumper; if test -f $p/module.prop && test ! -e $p/disable && test ! -e $p/remove; then old=1; fi; p=/data/adb/modules/ldlogin; if test -f $p/ldmenu-loader.prop && test ! -e $p/disable && test ! -e $p/remove && grep -qx 'loader=ldnph-v1.2.0' $p/ldmenu-loader.prop && test $(sha256sum $p/zygisk/arm64-v8a.so | cut -d ' ' -f1) = a029b48dc4c354f8cf5a6fa2ab39c0db29f400305916ce5187bd8f061040554c && test $(sha256sum $p/zygisk/x86_64.so | cut -d ' ' -f1) = 3e348324a9eb26f0741fa57307c03aabd17f2021242456a43769c4763f50ed56; then merged=1; fi; if test $old = 1 && test $merged = 1; then echo LDNPH_DUPLICATE; elif test $old = 1 || test $merged = 1; then echo LDNPH_READY; fi";
    if(!shell(j,loader,text)||trim(text)!="LDNPH_READY"){report(j,Phase::Failed,trim(text)=="LDNPH_DUPLICATE"?L"Trùng loader; gỡ LDNPH cũ rồi restart LD":L"Thiếu loader LDLogin/LDNPH hoặc module đang tắt");return false;}
    std::wstring component;if(!shell(j,L"cmd package resolve-activity --brief com.vng.playtogether",text)||!launcher(text,component)){report(j,Phase::Failed,L"Chưa cài Play Together VNG");return false;}
    std::wstring asset=assetPath();if(!assetHash(asset)){report(j,Phase::Failed,L"Thiếu hoặc sai phiên bản thư viện");return false;}
    if(!shell(j,L"if test -e /data/local/tmp/libmod.so; then sha256sum /data/local/tmp/libmod.so; else echo ASSET_ABSENT; fi",text))return false;
    if(trim(text)!="ASSET_ABSENT"&&text.compare(0,64,Digest)!=0){report(j,Phase::Failed,L"Có thư viện khác; dừng để tránh ghi đè");return false;}
    if(trim(text)=="ASSET_ABSENT"&&!command(true,L"-s emulator-"+std::to_wstring(5554+2*j->index)+L" push "+quoteArg(asset)+L" /data/local/tmp/libmod.so",text,j))return false;
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
    end=GetTickCount64()+120000;while(GetTickCount64()<end){if(!wait(1500,j))return false;if(fresh(j,now)&&now.vm!=old.vm){j->bootId.clear();j->rootReady=false;{std::lock_guard<std::mutex> lock(mutex_);for(auto&r:state_.rows)if(r.index==j->index&&r.generation==j->generation){r.vm=now.vm;r.ui=now.ui;r.bootId.clear();r.startTicks.clear();r.rootReady=false;}}report(j,Phase::Repairing,L"LD đã mở lại; chờ 30 giây");return wait(30000,j);}}return false;
}
static std::string noteHexString(){std::string h="040000001400000003000000474e5500";char b[3];for(auto v:BuildId){std::snprintf(b,3,"%02x",v);h+=b;}return h;}
void Engine::connected(const std::shared_ptr<Job>&j,DWORD pid,unsigned long long base,const std::string&verifiedTicks){
    std::string ticks=verifiedTicks;
    if(ticks.empty()){std::string stat;char processState=0;if(!shell(j,L"cat /proc/"+std::to_wstring(pid)+L"/stat",stat,HealthTimeoutMs)||!processStat(stat,pid,ticks,processState)||processState=='Z'||processState=='X'||processState=='x'){report(j,Phase::Failed,L"Chưa xác minh được danh tính tiến trình game");return;}}
    j->startTicks=ticks;
    Config config;Intent intent=Intent::Automatic;unsigned long long revision=0;{std::lock_guard<std::mutex> lock(mutex_);for(auto&r:state_.rows)if(r.index==j->index&&r.generation==j->generation){config=r.config;intent=r.intent;revision=r.revision;}}
    bool known=false,matched=intent==Intent::Automatic&&controlMatches(j,pid,base,config,known),automate=false;unsigned seconds=60;
    {std::lock_guard<std::mutex> lock(mutex_);seconds=state_.global.settleSeconds;for(auto&r:state_.rows)if(r.index==j->index&&r.generation==j->generation){if(r.revision!=revision){matched=false;known=false;}automate=r.intent==Intent::Automatic?(known?!matched:bool(r.config.function||r.config.filter||r.config.sell)):r.actionPending;r.applyAt=automate?GetTickCount64()+((r.intent==Intent::Automatic||j->loaded)?seconds*1000ULL:0):0;r.misses=0;r.probeErrors=0;r.bootId=j->bootId;r.rootReady=j->rootReady;r.startTicks=ticks;}}
    report(j,Phase::Connected,matched?L"Đã nhận lại cấu hình đang chạy":automate?L"Đã kết nối; chờ "+std::to_wstring(seconds)+L" giây để áp dụng cấu hình":L"Đã kết nối",pid,base);
}
bool Engine::controlMatches(const std::shared_ptr<Job>&j,DWORD pid,unsigned long long base,const Config&config,bool&known){
    known=false;if(j->startTicks.empty())return false;
    const auto noteHex=noteHexString();const std::wstring ticks(j->startTicks.begin(),j->startTicks.end()),expected(noteHex.begin(),noteHex.end()),mem=L"/proc/"+std::to_wstring(pid)+L"/mem";
    const auto start=L"test $(sed 's/.*) //' /proc/"+std::to_wstring(pid)+L"/stat | cut -d ' ' -f20) = "+ticks;
    const auto note=L"test $(dd if="+mem+L" bs=1 skip="+std::to_wstring(base+0x2d0)+L" count=36 2>/dev/null | xxd -p | tr -d '[:space:]') = "+expected;
    std::string output,values;
    if(!shell(j,start+L" && "+note+L" && dd if="+mem+L" bs=1 skip="+std::to_wstring(base+0x117044)+L" count=48 2>/dev/null | xxd -p && "+start+L" && "+note,output)||!bytes(output,values,48))return false;
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
bool Engine::word(const std::shared_ptr<Job>&j,const Row&row,unsigned relative,unsigned value,bool checkVm){
    if(!validWord(relative,value)||cancelled(j)||j->startTicks.empty())return false;std::string output;
    if(checkVm){Row now;if(!fresh(j,now)||now.vm!=row.vm)return false;}
    const auto encoded=littleHex(value),noteHex=noteHexString();const std::wstring data(encoded.begin(),encoded.end()),ticks(j->startTicks.begin(),j->startTicks.end()),expected(noteHex.begin(),noteHex.end());
    const auto memory=L"/proc/"+std::to_wstring(row.game)+L"/mem",address=std::to_wstring(row.base+0x117044+relative);
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
static bool verifyResetData(const std::string&output){
    std::string data;if(!bytes(output,data,52))return false;
    for(auto offset:resetOffsets())if(u32(data,offset)!=0)return false;
    return true;
}
bool Engine::resetAll(const std::shared_ptr<Job>&j,const Row&r){
    if(cancelled(j)||j->startTicks.empty()||!r.game)return false;
    const auto noteHex=noteHexString();const std::wstring ticks(j->startTicks.begin(),j->startTicks.end()),expected(noteHex.begin(),noteHex.end()),memory=L"/proc/"+std::to_wstring(r.game)+L"/mem";
    const auto start=L"test $(sed 's/.*) //' /proc/"+std::to_wstring(r.game)+L"/stat | cut -d ' ' -f20) = "+ticks;
    const auto note=L"test $(dd if="+memory+L" bs=1 skip="+std::to_wstring(r.base+0x2d0)+L" count=36 2>/dev/null | xxd -p | tr -d '[:space:]') = "+expected;
    std::wstring writes;
    for(auto offset:resetOffsets()){
        if(!validWord(offset,0))return false;
        writes+=L"printf 00000000 | xxd -r -p | dd of="+memory+L" seek="+std::to_wstring(r.base+0x117044+offset)+L" bs=1 count=4 2>/dev/null && (usleep 100000 2>/dev/null || sleep 0.1) && ";
    }
    const auto script=start+L" && "+note+L" && "+writes+start+L" && "+note+L" && dd if="+memory+L" bs=1 skip="+std::to_wstring(r.base+0x117044)+L" count=52 2>/dev/null | xxd -p";
    std::string output;return shell(j,script,output)&&verifyResetData(output);
}
static bool vectorBounds(const std::string&data,unsigned long long&begin,size_t&count){
    if(data.size()!=24)return false;begin=u64(data,0);const auto end=u64(data,8),capacity=u64(data,16);
    if(begin%4||end<begin||capacity<end||capacity-begin>65536||end-begin>256||((end-begin)%4)||capacity>=0x800000000000ULL)return false;
    count=size_t((end-begin)/4);return !count||begin>=4096;
}
static bool parseFilterMetaLatch(const std::string&out,std::string&meta,unsigned long long&begin,size_t&count,char&latch){
    std::string metaLatch;if(!bytes(out,metaLatch,25))return false;
    meta=metaLatch.substr(0,24);latch=metaLatch[24];
    return vectorBounds(meta,begin,count);
}
static bool verifyFilterBody(const std::string&out,const std::string&expectedMeta,const std::vector<unsigned>&ids,bool&matches){
    if(ids.empty()||ids.size()>64)return false;
    std::string bodyAfter;const size_t count=ids.size();
    if(!bytes(out,bodyAfter,count*4+24)||bodyAfter.substr(count*4)!=expectedMeta)return false;
    matches=true;for(size_t i=0;i<count;i++)if(u32(bodyAfter,i*4)!=ids[i])matches=false;
    return true;
}
static bool readableRange(const std::string&maps,unsigned long long address,size_t size){
    if(!size||size>256||address>0x800000000000ULL-size)return false;std::istringstream lines(maps);std::string line;
    while(std::getline(lines,line)){std::istringstream fields(line);std::string range,perms;fields>>range>>perms;const auto dash=range.find('-');unsigned long long begin=0,end=0;if(dash!=std::string::npos&&perms.size()==4&&perms[0]=='r'&&number(range.substr(0,dash),begin,16)&&number(range.substr(dash+1),end,16)&&address>=begin&&address+size<=end)return true;}return false;
}
bool Engine::waitFilter(const std::shared_ptr<Job>&j,const Row&r,const std::vector<unsigned>&ids,bool zeroMailbox){
    if(cancelled(j)||j->startTicks.empty()||!r.game)return false;
    const std::wstring ticks(j->startTicks.begin(),j->startTicks.end()),pid=std::to_wstring(r.game),mem=L"/proc/"+pid+L"/mem";
    const auto start=L"test $(sed 's/.*) //' /proc/"+pid+L"/stat | cut -d ' ' -f20) = "+ticks;
    std::string maps;const auto end=GetTickCount64()+6000;
    do{
        std::string out,meta;unsigned long long begin=0;size_t count=0;char latch=0;
        if(!shell(j,start+L" && { dd if="+mem+L" bs=1 skip="+std::to_wstring(r.base+0x118798)+L" count=24 2>/dev/null; dd if="+mem+L" bs=1 skip="+std::to_wstring(r.base+0x1187f8)+L" count=1 2>/dev/null; } | xxd -p",out)||!parseFilterMetaLatch(out,meta,begin,count,latch))return false;
        bool matches=count==ids.size()&&(!zeroMailbox||latch==0);
        if(matches&&count){
            if((maps.empty()||!readableRange(maps,begin,count*4))&&(!shell(j,L"cat /proc/"+pid+L"/maps",maps)||!readableRange(maps,begin,count*4)))return false;
            if(!shell(j,start+L" && { dd if="+mem+L" bs=1 skip="+std::to_wstring(begin)+L" count="+std::to_wstring(count*4)+L" 2>/dev/null; dd if="+mem+L" bs=1 skip="+std::to_wstring(r.base+0x118798)+L" count=24 2>/dev/null; } | xxd -p",out)||!verifyFilterBody(out,meta,ids,matches))return false;
        }
        if(matches)return true;
        if(!wait(250,j))return false;
    }while(GetTickCount64()<end&&!cancelled(j));return false;
}
bool Engine::applyIds(const std::shared_ptr<Job>&j,const Row&r,const std::vector<unsigned>&ids){
    // Native flags: +4 enables ID filtering, +28 is an ID mailbox. The game
    // consumes its nonzero pulse once; returning to zero releases that latch.
    // Clear the previous vector by its existing reset flag, never by writing
    // vector pointers or heap data from the host.
    if(!word(j,r,28,0,false)||!word(j,r,4,0,false)||!wait(250,j)||!waitFilter(j,r,{}))return false;
    if(!word(j,r,24,1,false)||!word(j,r,4,1,false)||!wait(250,j)||!waitFilter(j,r,{},true))return false;
    if(r.quick.function==1){bool cleared=false;const auto end=GetTickCount64()+6000;do{std::string flag;if(!readMemory(j,r,0x1187b4,1,flag))return false;if(flag[0]==0){cleared=true;break;}if(!wait(250,j))return false;}while(GetTickCount64()<end);if(!cleared)return false;}
    if(!word(j,r,24,0,false))return false;
    if(ids.empty())return word(j,r,4,0,false);
    std::vector<unsigned> accepted;
    for(auto id:ids){
        const bool special=r.quick.function==1&&id==5555;if(!special)accepted.push_back(id);
        if(!word(j,r,28,id,false)||!wait(100,j)||!waitFilter(j,r,accepted))return false;
        if(special){bool received=false;const auto end=GetTickCount64()+6000;do{std::string flag;if(!readMemory(j,r,0x1187b4,1,flag))return false;if(flag[0]==1){received=true;break;}if(!wait(250,j))return false;}while(GetTickCount64()<end);if(!received)return false;}
        if(!word(j,r,28,0,false)||!wait(200,j)||!waitFilter(j,r,accepted,true))return false;
    }
    return true;
}
void Engine::runtimeAction(const std::shared_ptr<Job>&j,Row row){
    auto finish=[&](bool ok,const wchar_t*detail){
        if(cancelled(j))return;{std::lock_guard<std::mutex> lock(mutex_);for(auto&r:state_.rows)if(r.index==j->index&&r.generation==j->generation){r.applyAt=0;r.actionPending=false;r.quickActive=ok&&row.intent==Intent::Quick?row.quick.function:0;}}
        report(j,ok?Phase::Connected:Phase::Failed,detail,row.game,row.base);
    };
    if(!ensureAdb(j)||!ensureRoot(j))return;
    DWORD pid=0;unsigned long long base=0;std::string maps,ticks;auto state=inspectContext(j,pid,base,&maps,&ticks);
    if(row.intent==Intent::Reset&&state==ContextState::Absent){
        {std::lock_guard<std::mutex> lock(mutex_);for(auto&r:state_.rows)if(r.index==j->index&&r.generation==j->generation){r.actionPending=false;r.quickActive=0;r.applyAt=0;r.wanted=false;}}
        report(j,Phase::Idle,L"Đã lưu mặc định; không có chức năng đang chạy");return;
    }
    if(state!=ContextState::Verified||(row.game&&row.game!=pid)||(row.base&&row.base!=base)){finish(false,L"Phiên game đã đổi; chưa reset hoặc chạy chức năng");return;}
    row.game=pid;row.base=base;j->startTicks=ticks;Row now;std::string elf,header;
    bool ok=(row.startTicks.empty()||row.startTicks==j->startTicks)&&fresh(j,now)&&now.vm==row.vm&&shell(j,L"dd if=/proc/"+std::to_wstring(pid)+L"/mem bs=1 skip="+std::to_wstring(base)+L" count=4096 2>/dev/null | xxd -p",elf)&&bytes(elf,header,4096)&&controlSegment(header);
    if(ok)for(auto off:resetOffsets())if(!writableControl(maps,base,off))ok=false;
    if(ok)ok=writableControl(maps,base,28);
    if(!ok){finish(false,L"Chưa xác minh được vùng điều khiển; dừng lệnh");return;}
    report(j,Phase::Applying,row.intent==Intent::Reset?L"Reset All":L"Áp dụng chức năng rời",pid,base);
    std::string before;if(!readMemory(j,row,0x117044,52,before)){finish(false,L"Không đọc được trạng thái chức năng");return;}
    bool liveSame=row.quick.function==1?u32(before,0)==1&&u32(before,36)==1:u32(before,0)==11&&u32(before,20)==11;
    const bool reset=row.intent==Intent::Reset||needsQuickReset(row.quick,row.quickActive,liveSame);
    if(reset)ok=resetAll(j,row);
    if(ok&&row.intent==Intent::Quick){
        if(!validQuick(row.quick))ok=false;
        if(ok&&reset)for(const auto&s:quickStart(row.quick.function))if(!word(j,row,s.offset,s.value,false)||!wait(s.delay,j)){ok=false;break;}
        if(ok)ok=applyIds(j,row,row.quick.ids)&&word(j,row,12,row.quick.function==1&&row.quick.sell?1:0,false);
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
    Row now;std::string elf,header,maps,ticks;DWORD verifiedPid=0;unsigned long long verifiedBase=0;
    bool ok=fresh(j,now)&&now.vm==row.vm&&context(j,verifiedPid,verifiedBase,&maps,&ticks)&&verifiedPid==row.game&&verifiedBase==row.base&&ticks==row.startTicks&&shell(j,L"dd if=/proc/"+std::to_wstring(row.game)+L"/mem bs=1 skip="+std::to_wstring(row.base)+L" count=4096 2>/dev/null | xxd -p",elf)&&bytes(elf,header,4096)&&controlSegment(header)&&writableControl(maps,row.base,0)&&writableControl(maps,row.base,36);
    if(ok)for(auto off:resetOffsets())if(!writableControl(maps,row.base,off))ok=false;
    if(ok)j->startTicks=ticks;
    if(ok){std::string data,existing;ok=shell(j,L"dd if=/proc/"+std::to_wstring(row.game)+L"/mem bs=1 skip="+std::to_wstring(row.base+0x117044)+L" count=48 2>/dev/null | xxd -p",data)&&bytes(data,existing,48);if(ok&&sameControl(existing,row.config)){{std::lock_guard<std::mutex> lock(mutex_);for(auto&r:state_.rows)if(r.index==j->index&&r.generation==j->generation&&r.revision==row.revision)r.applyAt=0;}report(j,Phase::Connected,L"Đã nhận lại cấu hình đang chạy",row.game,row.base);return;}}
    if(ok)ok=resetAll(j,row)&&wait(500,j);
    if(ok&&row.config.function)ok=word(j,row,36,1,false)&&wait(100,j)&&word(j,row,8,row.config.function==2?1:2,false)&&wait(1000,j)&&word(j,row,0,4,false)&&wait(500,j);
    if(ok)ok=word(j,row,32,0,false)&&wait(500,j);
    if(ok&&row.config.filter)ok=word(j,row,32,unsigned(row.config.filter),false)&&wait(500,j);
    if(ok)ok=word(j,row,12,row.config.sell?1:0,false);
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
        DWORD pid=0;unsigned long long base=0;std::string ticks;
        const auto initial=inspectContext(j,pid,base,nullptr,&ticks);if(initial==ContextState::Verified){connected(j,pid,base,ticks);return;}
        if(initial==ContextState::Unknown){defer(j,Phase::WaitingContext,L"Chờ xác minh thư viện; chưa mở lại game");return;}
        if(!commitReady()){report(j,Phase::Failed,L"Commit trống dưới 2 GB; dừng kết nối");return;}
        if(!openGame(j)){
            if(cancelled(j))return;
            std::string marker;bool echoOk=shell(j,L"echo LDCONTROL_ADB_READY",marker,HealthTimeoutMs)&&trim(marker)=="LDCONTROL_ADB_READY";
            if(!echoOk){j->bootId.clear();if(ensureAdb(j))continue;}
            auto snap=snapshot();auto it=std::find_if(snap.rows.begin(),snap.rows.end(),[&](auto&r){return r.index==j->index;});if(it!=snap.rows.end()&&it->phase!=Phase::Failed&&!waitingPhase(it->phase))report(j,Phase::Failed,L"Không xác nhận được lệnh mở game");return;
        }
        j->loaded=true;
        report(j,Phase::Loading,L"Đang chờ game nạp thư viện");auto end=GetTickCount64()+ModuleLoadTimeoutMs;
        while(GetTickCount64()<end&&!cancelled(j)){if(!wait(2000,j))return;Row current;if(!fresh(j,current)||current.vm!=before.vm){report(j,Phase::Failed,L"LD đã đóng hoặc thay backend");return;}if(context(j,pid,base,nullptr,&ticks)){
                std::string text;if(shell(j,L"sha256sum /data/local/tmp/libmod.so",text)&&text.compare(0,64,Digest)==0)shell(j,L"rm -f /data/local/tmp/libmod.so",text);
                connected(j,pid,base,ticks);return;}}
        if(cancelled(j))return;
        // Only a fresh, complete absence proof can authorize a VM restart.
        const auto final=inspectContext(j,pid,base,nullptr,&ticks);if(final==ContextState::Verified){connected(j,pid,base,ticks);return;}
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
}
