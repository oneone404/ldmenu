#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <atomic>
#include <array>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "settings.h"
#include "actions.h"

namespace ldcontrol {
constexpr UINT Changed=WM_APP+21;
constexpr UINT WorkerFailed=WM_APP+22;
constexpr DWORD ModuleLoadTimeoutMs=60000;
constexpr DWORD PostConnectDelayMs=60000;
constexpr DWORD HealthIntervalMs=30000;
constexpr DWORD HealthTimeoutMs=3000;
constexpr ULONGLONG RestartWindowMs=30ULL*60*1000;
enum class Phase { Idle, Queued, Checking, Repairing, Opening, Loading, Connected, Applying, WaitingAdb, WaitingContext, Cooldown, Failed, Closed };
enum class ContextState { Verified, Absent, Unknown };
struct Row {
    int index=0; std::wstring name,detail; DWORD vm=0,ui=0,game=0;
    bool running=false,wanted=false,playQueued=false,automatic=false; Phase phase=Phase::Idle;
    unsigned restarts=0,misses=0,probeErrors=0; unsigned long long generation=0,base=0;
    Config config;unsigned long long applyAt=0,revision=0;
    std::string bootId,startTicks;bool rootReady=false;
    unsigned long long retryAt=0;
    std::array<ULONGLONG,5> restartTimes{};
    Intent intent=Intent::Automatic;QuickConfig quick;bool actionPending=false;int quickActive=0;
};
struct Snapshot { std::vector<Row> rows; std::wstring error; bool scanning=false,playRunning=false,playStopping=false; GlobalSettings global; };
struct Job { int index; std::wstring name; std::string bootId,startTicks; unsigned long long generation; bool features=false,batch=false,rootReady=false,recovery=false,loaded=false; std::atomic<bool> cancelled{false}; };
class Engine {
public:
    Engine(); ~Engine();
    void start(HWND window); void stop(); void refresh(); void toggle(int index);
    Snapshot snapshot(); void setBackground(bool background);
    bool configure(int index,const std::wstring& name,const Config& config,std::wstring& error,bool reset=false);
    bool configureGlobal(const GlobalSettings& global,bool applyToRunning,std::wstring& error,bool reset=false);
    bool runQuick(int index,const std::wstring& name,const QuickConfig& config,std::wstring& error);
    bool play(std::wstring& error);
    void pausePlay();
    unsigned failure()const noexcept{return workerFailure_.load();}
    void halt(unsigned reason)noexcept;
private:
    friend int tests();
    HWND window_=nullptr; HANDLE stop_=nullptr,wake_=nullptr;
    std::thread thread_; std::mutex mutex_; Snapshot state_;
    std::shared_ptr<Job> active_; bool refresh_=true;
    std::atomic<unsigned> workerFailure_{0};
    std::vector<SavedConfig> settings_;
    std::wstring settingsError_;
    unsigned adbRepairs_=0; ULONGLONG lastRepair_=0,nextConnect_=0;
    ULONGLONG nextPlay_=0,playGap_=5000;
    bool advancePlayLocked(ULONGLONG now);
    bool selectJobLocked(ULONGLONG now,ULONGLONG& due,std::shared_ptr<Job>& job,Row& selected);
    void updateReconnectLocked(Row& row,bool previous);
    void queueReconnect(const std::shared_ptr<Job>& job,const wchar_t* detail);
    void loop(); void scan(); void monitor(); void connect(const std::shared_ptr<Job>& job);
    void recoverAdb(const std::shared_ptr<Job>& job,const Row& row);
    void defer(const std::shared_ptr<Job>& job,Phase phase,const wchar_t* detail,ULONGLONG until=0);
    bool reserveRestart(const std::shared_ptr<Job>& job,ULONGLONG& retryAt);
    void report(const std::shared_ptr<Job>& job,Phase phase,const std::wstring& detail,DWORD game=0,unsigned long long base=0);
    bool cancelled(const std::shared_ptr<Job>& job); bool wait(DWORD ms,const std::shared_ptr<Job>& job={});
    bool command(bool adb,const std::wstring& args,std::string& output,const std::shared_ptr<Job>& job={},DWORD timeout=10000);
    bool shell(const std::shared_ptr<Job>& job,const std::wstring& text,std::string& output,DWORD timeout=10000);
    bool fresh(const std::shared_ptr<Job>& job,Row& row);
    bool ensureAdb(const std::shared_ptr<Job>& job);
    bool ensureRoot(const std::shared_ptr<Job>& job);
    bool context(const std::shared_ptr<Job>& job,DWORD& pid,unsigned long long& base,std::string* mapsOut=nullptr,std::string* ticksOut=nullptr);
    ContextState inspectContext(const std::shared_ptr<Job>& job,DWORD& pid,unsigned long long& base,std::string* mapsOut=nullptr,std::string* ticksOut=nullptr);
    bool openGame(const std::shared_ptr<Job>& job);
    bool restart(const std::shared_ptr<Job>& job,const Row& old);
    void connected(const std::shared_ptr<Job>& job,DWORD pid,unsigned long long base,const std::string& verifiedTicks={});
    bool controlMatches(const std::shared_ptr<Job>& job,DWORD pid,unsigned long long base,const Config& config,bool& known);
    void apply(const std::shared_ptr<Job>& job,const Row& row);
    void runtimeAction(const std::shared_ptr<Job>& job,Row row);
    void queueActionLocked(Row& row,Intent intent);
    bool resetAll(const std::shared_ptr<Job>& job,const Row& row);
    bool readMemory(const std::shared_ptr<Job>& job,const Row& row,unsigned offset,size_t count,std::string& data);
    bool waitFilter(const std::shared_ptr<Job>& job,const Row& row,const std::vector<unsigned>& ids,bool zeroMailbox=false);
    bool applyIds(const std::shared_ptr<Job>& job,const Row& row,const std::vector<unsigned>& ids);
    bool word(const std::shared_ptr<Job>& job,const Row& row,unsigned relative,unsigned value,bool checkVm=true);
    void notify();
};
int tests();
std::wstring phaseText(Phase phase);
}
