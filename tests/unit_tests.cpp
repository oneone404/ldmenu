#include "../backend.cpp"

namespace ldcontrol {
static bool online(const std::string&listing,int index){const std::string serial="emulator-"+std::to_string(5554+index*2);std::istringstream lines(listing);std::string line;unsigned matches=0;bool ready=false;while(std::getline(lines,line)){std::istringstream row(line);std::string s,status;if(row>>s>>status&&s==serial){matches++;ready=status=="device";}}return matches==1&&ready;}

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
    {
        auto statLine=[](DWORD pid,const char*ticks,char state='S'){std::string v=std::to_string(pid)+" (com.vng.playtogether) "+state;for(unsigned i=4;i<22;i++)v+=" 0";return v+" "+ticks+" 0";};
        const auto validNoteHex=noteHexString();
        auto ctxFrame=[&](const std::string&s1,const std::string&nh,const std::string&s2){return "LDMENU_CTX_V1\nSTAT1 "+s1+"\nNOTE "+nh+"\nSTAT2 "+s2+"\nLDMENU_CTX_END\n";};
        std::string outTicks;
        check(parseContextFrame(ctxFrame(statLine(420,"777"),validNoteHex,statLine(420,"777")),420,outTicks)&&outTicks=="777");
        check(!parseContextFrame(ctxFrame(statLine(420,"777"),validNoteHex,statLine(420,"778")),420,outTicks));
        check(!parseContextFrame(ctxFrame(statLine(421,"777"),validNoteHex,statLine(420,"777")),420,outTicks));
        check(!parseContextFrame(ctxFrame(statLine(420,"777"),validNoteHex,statLine(421,"777")),420,outTicks));
        check(!parseContextFrame(ctxFrame(statLine(420,"777",'Z'),validNoteHex,statLine(420,"777",'Z')),420,outTicks));
        check(!parseContextFrame(ctxFrame(statLine(420,"777",'X'),validNoteHex,statLine(420,"777")),420,outTicks));
        check(!parseContextFrame(ctxFrame(statLine(420,"777"),validNoteHex,statLine(420,"777",'x')),420,outTicks));
        auto badNote=validNoteHex;badNote[40]=badNote[40]=='a'?'b':'a';
        check(!parseContextFrame(ctxFrame(statLine(420,"777"),badNote,statLine(420,"777")),420,outTicks));
        check(!parseContextFrame(ctxFrame(statLine(420,"777"),validNoteHex.substr(2),statLine(420,"777")),420,outTicks));
        check(!parseContextFrame(ctxFrame(statLine(420,"777"),"",statLine(420,"777")),420,outTicks));
        check(!parseContextFrame("LDMENU_CTX_V0\nSTAT1 "+statLine(420,"777")+"\nNOTE "+validNoteHex+"\nSTAT2 "+statLine(420,"777")+"\nLDMENU_CTX_END\n",420,outTicks));
        check(!parseContextFrame("LDMENU_CTX_V1\nSTAT1 "+statLine(420,"777")+"\nNOTE "+validNoteHex+"\nSTAT2 "+statLine(420,"777")+"\n",420,outTicks));
        check(!parseContextFrame(ctxFrame(statLine(420,"777"),validNoteHex,statLine(420,"777"))+"EXTRA\n",420,outTicks));

        auto toHex=[](const std::string&raw){std::string h;char buf[3];for(unsigned char c:raw){std::snprintf(buf,3,"%02x",c);h+=buf;}return h;};
        std::string rawReset(52,'\0');rawReset[16]=7;rawReset[28]=99;rawReset[40]=3;rawReset[44]=5;
        check(verifyResetData(toHex(rawReset)));
        for(auto off:resetOffsets()){auto dirty=rawReset;dirty[off]=1;check(!verifyResetData(toHex(dirty)));}
        check(!verifyResetData(toHex(rawReset.substr(1)))&&!verifyResetData(toHex(rawReset+"x"))&&!verifyResetData("zz"));

        std::string rawMetaLatch(25,'\0');auto store64=[&](std::string&s,size_t p,unsigned long long v){for(unsigned k=0;k<8;k++)s[p+k]=char(v>>(8*k));};
        std::string parsedMeta;unsigned long long begin=0;size_t cnt=0;char latch=1;
        check(parseFilterMetaLatch(toHex(rawMetaLatch),parsedMeta,begin,cnt,latch)&&cnt==0&&latch==0);
        store64(rawMetaLatch,0,8192);store64(rawMetaLatch,8,8200);store64(rawMetaLatch,16,8208);rawMetaLatch[24]=1;
        check(parseFilterMetaLatch(toHex(rawMetaLatch),parsedMeta,begin,cnt,latch)&&begin==8192&&cnt==2&&latch==1);
        check(!parseFilterMetaLatch(toHex(rawMetaLatch.substr(0,24)),parsedMeta,begin,cnt,latch));
        check(!parseFilterMetaLatch(toHex(rawMetaLatch+std::string(1,'\0')),parsedMeta,begin,cnt,latch));
        store64(rawMetaLatch,8,8188);check(!parseFilterMetaLatch(toHex(rawMetaLatch),parsedMeta,begin,cnt,latch));
        store64(rawMetaLatch,8,8200);

        std::string rawBody(8,'\0');auto store32=[&](std::string&s,size_t p,unsigned v){for(unsigned k=0;k<4;k++)s[p+k]=char(v>>(8*k));};
        store32(rawBody,0,12);store32(rawBody,4,345);
        bool matches=false;const std::string expectedMeta=rawMetaLatch.substr(0,24);
        check(verifyFilterBody(toHex(rawBody+expectedMeta),expectedMeta,{12,345},matches)&&matches);
        check(verifyFilterBody(toHex(rawBody+expectedMeta),expectedMeta,{12,346},matches)&&!matches);
        auto changedMeta=expectedMeta;changedMeta[8]^=4;
        check(!verifyFilterBody(toHex(rawBody+changedMeta),expectedMeta,{12,345},matches));
        check(!verifyFilterBody(toHex(rawBody),expectedMeta,{12,345},matches));
        check(!verifyFilterBody(toHex(rawBody+expectedMeta),expectedMeta,{},matches));

        Engine c;Row r;r.index=2;r.name=L"LD-2";r.running=r.wanted=true;r.bootId="11111111-2222-4333-8444-555555555555";r.rootReady=true;r.phase=Phase::Connected;r.applyAt=10;
        c.state_.rows={r};ULONGLONG due=~0ULL;std::shared_ptr<Job> job;Row picked;
        check(c.selectJobLocked(100,due,job,picked)&&job->bootId==r.bootId&&job->rootReady);
    }
    std::printf("BACKEND_TESTS %d/%d\n",count-failed,count);return failed?1:0;
}
}

int main(){return ldcontrol::tests();}
