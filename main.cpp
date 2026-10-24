// Native event-driven LDMenu UI: Win32, Direct2D and DirectWrite.
#include "backend.h"
#include <windowsx.h>
#include <d2d1.h>
#include <dwrite.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <new>

template<class T>static void release(T*&p){if(p){p->Release();p=nullptr;}}
static D2D1_COLOR_F rgb(unsigned c,float a=1){return D2D1::ColorF(float(c>>16&255)/255,float(c>>8&255)/255,float(c&255)/255,a);}
namespace UI {
constexpr unsigned Background=0x000000,Panel=0x0c0c0c,Border=0x242424,Text=0xffffff,Muted=0x909090,Accent=0xb026ff,Hover=0x1b0829,Green=0x52d6a0,Red=0xf17c91,Amber=0xe7bc75;
const wchar_t*Functions[]={L"OFF",L"Bọ Thành Tích",L"Bọ All Map"};
const int FunctionValues[]={0,2,13};
const wchar_t*Filters[]={L"OFF",L"Xanh Lá",L"Xanh Dương",L"Tím",L"Sự Kiện",L"Chỉ Thẻ",L"All Bọ"};
const wchar_t*QuickFunctions[]={L"OFF",L"Câu Cá",L"Farm All"};
const int QuickValues[]={0,1,14};
const unsigned Delays[]={0,30,60,90,120,180,300};
}
class Application {
    HWND window_=nullptr;ldcontrol::Engine engine_;ldcontrol::Snapshot state_;
    ID2D1Factory*factory_=nullptr;IDWriteFactory*writeFactory_=nullptr;ID2D1HwndRenderTarget*target_=nullptr;ID2D1SolidColorBrush*brush_=nullptr;ID2D1PathGeometry*playShape_=nullptr;
    IDWriteTextFormat*font_=nullptr,*small_=nullptr,*bold_=nullptr,*icon_=nullptr,*caption_=nullptr;
    float dpi_=96,width_=800,height_=450,rowScroll_=0;int hover_=-99,focus_=-1;bool tracking_=false;
    int settingsIndex_=-1,dropdown_=0,dropSelection_=0;std::wstring settingsName_,settingsError_;ldcontrol::Config draft_;
    bool quickMode_=false,deviceReset_=false,globalReset_=false;ldcontrol::QuickConfig quickDraft_;
    unsigned draftDelay_=60;bool applyToRunning_=false;
    int globalTab_=0;float planScroll_=0;std::vector<ldcontrol::PlayTarget> draftTargets_;
    HWND playDelayEdit_=nullptr,idsEdit_=nullptr;HFONT editFont_=nullptr;WNDPROC editProcedure_=nullptr;
    int scrollVisible_=0,dragScroll_=0;float dragY_=0,dragStart_=0;
    unsigned ramUsed_=0,ramTotal_=0;
    static constexpr float Top=44,RowHeight=30;
    static LRESULT CALLBACK procedure(HWND w,UINT m,WPARAM p,LPARAM l){auto*a=reinterpret_cast<Application*>(GetWindowLongPtrW(w,GWLP_USERDATA));try{if(m==WM_NCCREATE){a=static_cast<Application*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);a->window_=w;SetWindowLongPtrW(w,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(a));}return a?a->message(m,p,l):DefWindowProcW(w,m,p,l);}catch(const std::bad_alloc&){if(a)a->engine_.halt(1);}catch(...){if(a)a->engine_.halt(2);}if(m==WM_PAINT)ValidateRect(w,nullptr);return m==WM_NCCREATE?FALSE:0;}
    static LRESULT CALLBACK editProcedure(HWND w,UINT m,WPARAM p,LPARAM l){auto*a=reinterpret_cast<Application*>(GetWindowLongPtrW(w,GWLP_USERDATA));if(a&&m==WM_KEYDOWN&&(p==VK_ESCAPE||p==VK_RETURN)){PostMessageW(a->window_,m,p,l);return 0;}return a?CallWindowProcW(a->editProcedure_,w,m,p,l):DefWindowProcW(w,m,p,l);}
    void invalidate(){if(!IsIconic(window_))InvalidateRect(window_,nullptr,FALSE);}
    void discard(){release(brush_);release(target_);}
    void updateRam(bool repaint=false){
        MEMORYSTATUSEX m{};m.dwLength=sizeof(m);if(!GlobalMemoryStatusEx(&m)||!m.ullTotalPhys)return;
        const unsigned total=unsigned((m.ullTotalPhys+(1ULL<<29))>>30),used=unsigned(((m.ullTotalPhys-m.ullAvailPhys)+(1ULL<<29))>>30);
        if(used!=ramUsed_||total!=ramTotal_){ramUsed_=used;ramTotal_=total;if(repaint)invalidate();}
    }
    HRESULT resources(){if(target_)return S_OK;RECT c;GetClientRect(window_,&c);auto props=D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT);props.dpiX=dpi_;props.dpiY=dpi_;auto hr=factory_->CreateHwndRenderTarget(props,D2D1::HwndRenderTargetProperties(window_,D2D1::SizeU(c.right,c.bottom)),&target_);if(SUCCEEDED(hr))hr=target_->CreateSolidColorBrush(rgb(UI::Text),&brush_);if(FAILED(hr))discard();return hr;}
    void color(unsigned c,float a=1){brush_->SetColor(rgb(c,a));}
    void fill(float l,float t,float r,float b,unsigned c,float radius=0,float alpha=1){color(c,alpha);if(radius)target_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(l,t,r,b),radius,radius),brush_);else target_->FillRectangle(D2D1::RectF(l,t,r,b),brush_);}
    void stroke(float l,float t,float r,float b,unsigned c,float radius=0){color(c);if(radius)target_->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(l,t,r,b),radius,radius),brush_);else target_->DrawRectangle(D2D1::RectF(l,t,r,b),brush_);}
    void text(const std::wstring&s,float x,float y,float w,float h,unsigned c,IDWriteTextFormat*f=nullptr,DWRITE_TEXT_ALIGNMENT align=DWRITE_TEXT_ALIGNMENT_LEADING){f=f?f:font_;f->SetTextAlignment(align);color(c);target_->DrawTextW(s.c_str(),UINT32(s.size()),f,D2D1::RectF(x,y,x+w,y+h),brush_,D2D1_DRAW_TEXT_OPTIONS_CLIP);}
    void glyph(const wchar_t*s,float x,float y,float w,float h,unsigned c,bool caption=false){text(s,x,y,w,h,c,caption?caption_:icon_,DWRITE_TEXT_ALIGNMENT_CENTER);}
    void iconButton(int id,const wchar_t*s,float x,float y,float size=30){if(hover_==id)fill(x,y,x+size,y+size,UI::Hover,5);glyph(s,x,y,size,size,UI::Accent);}
    void playButton(float x,float y){const bool running=state_.playRunning&&!state_.playStopping;fill(x+1,y+1,x+29,y+29,hover_==-16||running?UI::Hover:UI::Panel,9);if(running){fill(x+10,y+8,x+13.5f,y+22,UI::Accent,1.7f);fill(x+18,y+8,x+21.5f,y+22,UI::Accent,1.7f);}else if(playShape_){D2D1_MATRIX_3X2_F original;target_->GetTransform(&original);target_->SetTransform(D2D1::Matrix3x2F::Scale(.85f,.85f,D2D1::Point2F(15,15))*D2D1::Matrix3x2F::Translation(x,y));color(UI::Accent);target_->FillGeometry(playShape_,brush_);target_->SetTransform(original);}}
    void toggleDraw(float x,float y,bool on){fill(x,y,x+36,y+18,on?UI::Accent:0x303030,9);color(UI::Text);target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x+(on?27:9),y+9),6,6),brush_);}
    float listBottom()const{return std::max(Top+RowHeight,height_-12-(state_.error.empty()?0:26));}
    float listContent()const{return float(state_.rows.size())*RowHeight;}
    float paneX()const{return (width_-440)/2;}
    bool settingsOpen()const{return settingsIndex_!=-1;}
    bool globalSettings()const{return settingsIndex_==-2;}
    bool planTab()const{return globalSettings()&&globalTab_==0;}
    float paneHeight()const{return globalSettings()?396.f:quickMode_?340.f:300.f;}
    float paneY()const{return std::max(38.f,(height_-paneHeight())/2);}
    float controlsY()const{return globalSettings()?280.f:204.f;}
    float fieldY(int field)const{return globalSettings()?(field==1?108.f:field==2?166.f:224.f):(field==1?76.f:140.f);}
    int delaySelection()const{for(int i=0;i<7;i++)if(UI::Delays[i]==draftDelay_)return i;return 2;}
    int dropdownCount()const{return dropdown_==1?3:7;}
    float dropdownY()const{float y=paneY()+fieldY(dropdown_)+34;return std::min(y,height_-10-dropdownCount()*28);}
    void layoutEditor(){if(!playDelayEdit_)return;const float scale=dpi_/96;
        if(planTab())SetWindowPos(playDelayEdit_,HWND_TOP,int((paneX()+238)*scale),int((paneY()+122)*scale),int(174*scale),int(23*scale),SWP_NOACTIVATE|SWP_SHOWWINDOW);else{if(GetFocus()==playDelayEdit_)SetFocus(window_);ShowWindow(playDelayEdit_,SW_HIDE);}
        if(idsEdit_){if(settingsOpen()&&quickMode_&&quickDraft_.function&&!dropdown_)SetWindowPos(idsEdit_,HWND_TOP,int((paneX()+32)*scale),int((paneY()+144)*scale),int(376*scale),int(24*scale),SWP_NOACTIVATE|SWP_SHOWWINDOW);else{if(GetFocus()==idsEdit_)SetFocus(window_);ShowWindow(idsEdit_,SW_HIDE);}}
    }
    bool readPlayDelay(unsigned&out){wchar_t value[16]{};GetWindowTextW(playDelayEdit_,value,16);if(!*value)return false;out=0;for(auto*p=value;*p;p++){if(*p<L'0'||*p>L'9'||out>3600)return false;out=out*10+unsigned(*p-L'0');}return out<=3600;}
    bool targetSelected(const ldcontrol::Row&r)const{return std::any_of(draftTargets_.begin(),draftTargets_.end(),[&](auto&t){return t.index==r.index&&t.name==r.name;});}
    void setTarget(const ldcontrol::Row&r,bool selected){draftTargets_.erase(std::remove_if(draftTargets_.begin(),draftTargets_.end(),[&](auto&t){return t.index==r.index;}),draftTargets_.end());if(selected)draftTargets_.push_back({r.index,r.name});}
    void checkDraw(float x,float y,bool on){fill(x,y,x+14,y+14,on?UI::Accent:UI::Background,3);if(on)glyph(L"\uE73E",x,y,14,14,UI::Text,true);else stroke(x+.5f,y+.5f,x+13.5f,y+13.5f,UI::Border,3);}
    void clamp(){rowScroll_=std::clamp(rowScroll_,0.f,std::max(0.f,listContent()-(listBottom()-Top)));planScroll_=std::clamp(planScroll_,0.f,std::max(0.f,float(state_.rows.size())*20-208));}
    void showScroll(int id){scrollVisible_=id;SetTimer(window_,1,1100,nullptr);invalidate();}
    void scrollbar(int id,float top,float bottom,float content,float offset){if(content<=bottom-top||(scrollVisible_!=id&&dragScroll_!=id))return;float visible=bottom-top,thumb=std::max(22.f,visible*visible/content);float y=top+(visible-thumb)*offset/(content-visible);fill(width_-12,y,width_-8,y+thumb,UI::Accent,2);}
    void updateSnapshot(){const int selected=focus_>=0&&focus_<int(state_.rows.size())?state_.rows[focus_].index:-1;state_=engine_.snapshot();state_.rows.erase(std::remove_if(state_.rows.begin(),state_.rows.end(),[](auto&r){return !r.running;}),state_.rows.end());focus_=-1;for(size_t i=0;i<state_.rows.size();i++)if(state_.rows[i].index==selected)focus_=int(i);hover_=-99;updateRam(false);clamp();invalidate();}
    int functionSelection()const{for(int i=0;i<3;i++)if(UI::FunctionValues[i]==draft_.function)return i;return 0;}
    int hit(float x,float y){
        if(x<0||y<0||x>=width_||y>=height_)return -99;
        if(y<32){if(x>width_-32)return -2;if(x>width_-64)return -3;if(x>width_-96)return -4;if(x>=width_-132&&x<width_-100)return -5;if(x>=width_-168&&x<width_-136)return -13;if(x>=width_-204&&x<width_-172)return -16;return -99;}
        if(settingsOpen()){float l=paneX(),t=paneY();
            if(dropdown_){float dy=dropdownY(),count=float(dropdownCount());if(x>=l+20&&x<=l+420&&y>=dy&&y<dy+count*28)return 10000+int((y-dy)/28);return -20;}
            if(x<l||x>l+440||y<t||y>t+paneHeight())return -19;
            if(y>=t+10&&y<t+40){if(x>=l+396&&x<l+426)return -6;if(!quickMode_){if(x>=l+360&&x<l+390)return -8;if(x>=l+324&&x<l+354)return -7;}}
            if(quickMode_){if(x>=l+20&&x<=l+420&&y>=t+76&&y<t+108)return -9;if(quickDraft_.function==1&&x>=l+20&&x<=l+420&&y>=t+204&&y<t+236)return -12;if(x>=l+20&&x<=l+420&&y>=t+256&&y<t+288)return -50;return -99;}
            if(globalSettings()&&y>=t+48&&y<t+78){if(x>=l+20&&x<l+220)return -40;if(x>=l+220&&x<=l+420)return -41;}
            if(planTab()){if(x>=l+20&&x<=l+210){if(y>=t+116&&y<t+140)return -18;if(y>=t+142&&y<t+350){int row=int((y-t-142+planScroll_)/20);if(row>=0&&row<int(state_.rows.size()))return 20000+row;}}return -99;}
            if(x>=l+20&&x<=l+420){if(y>=t+fieldY(1)&&y<t+fieldY(1)+32)return -9;if(y>=t+fieldY(2)&&y<t+fieldY(2)+32)return -10;}
            if(globalSettings()&&x>=l+20&&x<=l+420){if(y>=t+fieldY(3)&&y<t+fieldY(3)+32)return -14;if(y>=t+322&&y<t+350)return -15;}
            if(y>=t+controlsY()&&y<t+controlsY()+32){if(x>=l+20&&x<l+225)return -11;if(x>=l+245&&x<l+420)return -12;}
            return -99;
        }
        if(x>=width_-18&&y>=Top&&y<listBottom()&&listContent()>listBottom()-Top)return -30;
        if(y>=Top&&y<listBottom()){int row=int((y-Top+rowScroll_)/RowHeight);if(row>=0&&row<int(state_.rows.size())){if(x>=width_-56&&x<width_-20)return 1000+row;if(x>=width_-90&&x<width_-56)return 3000+row;if(x>=width_-148&&x<width_-96)return row;}}
        return -99;
    }
    void openSettings(int row){if(row<0||row>=int(state_.rows.size()))return;auto&r=state_.rows[row];settingsIndex_=r.index;settingsName_=r.name;draft_=r.config;quickMode_=false;deviceReset_=false;settingsError_.clear();dropdown_=0;layoutEditor();invalidate();}
    void openQuick(int row){if(row<0||row>=int(state_.rows.size()))return;auto&r=state_.rows[row];settingsIndex_=r.index;settingsName_=r.name;quickMode_=true;quickDraft_=r.quick;SetWindowTextW(idsEdit_,ldcontrol::idsText(quickDraft_.ids).c_str());settingsError_.clear();dropdown_=0;layoutEditor();invalidate();}
    void openGlobalSettings(){settingsIndex_=-2;quickMode_=false;globalReset_=false;globalTab_=0;planScroll_=0;settingsName_=L"Cấu Hình";draft_=state_.global.defaults;draftDelay_=state_.global.settleSeconds;draftTargets_=state_.global.playTargets;SetWindowTextW(playDelayEdit_,std::to_wstring(state_.global.playDelaySeconds).c_str());applyToRunning_=false;settingsError_.clear();dropdown_=0;layoutEditor();invalidate();}
    void action(int id){
        if(id==-2){PostMessageW(window_,WM_CLOSE,0,0);return;}if(id==-3){ShowWindow(window_,IsZoomed(window_)?SW_RESTORE:SW_MAXIMIZE);return;}if(id==-4){ShowWindow(window_,SW_MINIMIZE);return;}if(engine_.failure())return;if(id==-5){engine_.refresh();return;}
        if(id==-13){openGlobalSettings();return;}
        if(id==-16){if(state_.playRunning&&!state_.playStopping)engine_.pausePlay();else{std::wstring error;if(!engine_.play(error)){openGlobalSettings();settingsError_=error;}}const auto current=engine_.snapshot();state_.playRunning=current.playRunning;state_.playStopping=current.playStopping;invalidate();return;}
        if(settingsOpen()){
            if(id>=10000&&id<10007){int selection=id-10000;if(dropdown_==1&&selection<3){if(quickMode_){quickDraft_.function=UI::QuickValues[selection];if(quickDraft_.function!=1)quickDraft_.sell=false;if(!quickDraft_.function){quickDraft_.ids.clear();SetWindowTextW(idsEdit_,L"");}}else draft_.function=UI::FunctionValues[selection];}if(dropdown_==2&&selection<7)draft_.filter=selection;if(dropdown_==3&&selection<7)draftDelay_=UI::Delays[selection];dropdown_=0;}
            else if(id==-20)dropdown_=0;else if(id==-6||id==-19){settingsIndex_=-1;dropdown_=0;}else if(id==-7&&!quickMode_){if(planTab()){draftTargets_.clear();SetWindowTextW(playDelayEdit_,L"5");}else{draft_={};if(globalSettings()){draftDelay_=60;applyToRunning_=true;globalReset_=true;}else deviceReset_=true;}settingsError_.clear();}
            else if(id==-8&&!quickMode_){settingsError_.clear();bool saved=false;if(globalSettings()){ldcontrol::GlobalSettings common;common.defaults=draft_;common.settleSeconds=draftDelay_;common.playTargets=draftTargets_;if(readPlayDelay(common.playDelaySeconds))saved=engine_.configureGlobal(common,applyToRunning_,settingsError_,globalReset_);else settingsError_=L"Delay phải từ 0 đến 3600 giây";}else saved=engine_.configure(settingsIndex_,settingsName_,draft_,settingsError_,deviceReset_);if(saved)settingsIndex_=-1;}
            else if(id==-50&&quickMode_){wchar_t value[1025]{};if(quickDraft_.function)GetWindowTextW(idsEdit_,value,1025);settingsError_.clear();if(!ldcontrol::parseIds(value,quickDraft_.ids))settingsError_=L"ID: số 1–2147483647, cách bằng dấu phẩy; tối đa 64";else if(engine_.runQuick(settingsIndex_,settingsName_,quickDraft_,settingsError_))settingsIndex_=-1;}
            else if(id==-9||id==-10||id==-14){dropdown_=id==-9?1:id==-10?2:3;dropSelection_=dropdown_==1?(quickMode_?(quickDraft_.function==14?2:quickDraft_.function==1?1:0):functionSelection()):dropdown_==2?draft_.filter:delaySelection();}
            else if(id==-11)draft_.reconnect=!draft_.reconnect;else if(id==-12){if(quickMode_)quickDraft_.sell=!quickDraft_.sell;else draft_.sell=!draft_.sell;}
            else if(id==-15)applyToRunning_=!applyToRunning_;
            else if(id==-40||id==-41){globalTab_=id==-40?0:1;dropdown_=0;}
            else if(planTab()&&id>=20000&&id<20512){int row=id-20000;if(row<int(state_.rows.size())){auto r=state_.rows[row];setTarget(r,!targetSelected(r));}}
            else if(planTab()&&id==-18){bool all=!state_.rows.empty()&&std::all_of(state_.rows.begin(),state_.rows.end(),[&](auto&r){return targetSelected(r);});for(const auto&r:state_.rows)setTarget(r,!all);}
            layoutEditor();invalidate();return;
        }
        if(id>=3000&&id<3512){openQuick(id-3000);return;}if(id>=1000&&id<1512){openSettings(id-1000);return;}if(id>=0&&id<int(state_.rows.size())){focus_=id;engine_.toggle(state_.rows[id].index);}
    }
    void drawDropdown(){if(!dropdown_)return;float l=paneX(),y=dropdownY();int count=dropdownCount();fill(l+20,y,l+420,y+count*28,UI::Panel,5);stroke(l+20,y,l+420,y+count*28,UI::Border,5);for(int i=0;i<count;i++){if(i==dropSelection_||hover_==10000+i)fill(l+22,y+i*28+1,l+418,y+(i+1)*28-1,UI::Accent,3);text(dropdown_==1?std::wstring(quickMode_?UI::QuickFunctions[i]:UI::Functions[i]):dropdown_==2?std::wstring(UI::Filters[i]):std::to_wstring(UI::Delays[i])+L" giây",l+32,y+i*28,370,28,UI::Text);}}
    void settingsDraw(){
        float l=paneX(),t=paneY();fill(0,32,width_,height_,0x000000,0,.78f);fill(l,t,l+440,t+paneHeight(),UI::Panel,9);stroke(l,t,l+440,t+paneHeight(),UI::Border,9);
        text(settingsName_,l+20,t+10,290,30,UI::Text,bold_);
        if(!quickMode_){if(!globalSettings()||!planTab()){if(hover_==-7)fill(l+324,t+10,l+354,t+40,0x301018,5);glyph(L"\uE72C",l+324,t+10,30,30,UI::Red);}else iconButton(-7,L"\uE72C",l+324,t+10);iconButton(-8,L"\uE74E",l+360,t+10);}iconButton(-6,L"\uE8BB",l+396,t+10);
        if(quickMode_){
            text(L"Chức Năng",l+20,t+50,400,24,UI::Muted,small_);fill(l+20,t+76,l+420,t+108,UI::Background,5);stroke(l+20,t+76,l+420,t+108,UI::Border,5);text(UI::QuickFunctions[quickDraft_.function==14?2:quickDraft_.function==1?1:0],l+32,t+76,350,32,UI::Text);glyph(L"\uE70D",l+388,t+76,24,32,UI::Accent);
            if(quickDraft_.function){text(quickDraft_.function==1?L"ID Cá":L"ID",l+20,t+114,400,24,UI::Muted,small_);fill(l+20,t+140,l+420,t+172,UI::Background,5);stroke(l+20,t+140,l+420,t+172,UI::Border,5);}
            if(quickDraft_.function==1){text(L"BÁN NHANH",l+20,t+204,320,32,UI::Text,bold_);toggleDraw(l+384,t+211,quickDraft_.sell);}
            fill(l+20,t+256,l+420,t+288,hover_==-50?0xc454ff:UI::Accent,5);glyph(L"\uE768",l+205,t+256,30,32,UI::Text);
            if(!settingsError_.empty())text(settingsError_,l+20,t+298,400,30,UI::Red,small_);drawDropdown();return;
        }
        if(globalSettings())for(int tab=0;tab<2;tab++){float x=l+20+tab*200;bool selected=globalTab_==tab;if(selected||hover_==-40-tab)fill(x,t+48,x+200,t+78,UI::Hover,4);text(tab?L"Cấu Hình LD":L"Cấu Hình Chung",x,t+48,200,30,selected?UI::Accent:UI::Muted,bold_,DWRITE_TEXT_ALIGNMENT_CENTER);if(selected)fill(x+20,t+76,x+180,t+78,UI::Accent);}
        if(planTab()){
            text(L"Áp Dụng Cho:",l+20,t+86,190,26,UI::Text);text(L"Delay Giữa Các LD (s):",l+230,t+86,190,26,UI::Text);fill(l+230,t+118,l+420,t+150,UI::Background,4);stroke(l+230,t+118,l+420,t+150,UI::Border,4);
            bool all=!state_.rows.empty()&&std::all_of(state_.rows.begin(),state_.rows.end(),[&](auto&r){return targetSelected(r);});checkDraw(l+26,t+121,all);text(L"All",l+52,t+116,150,24,UI::Text,bold_);
            target_->PushAxisAlignedClip(D2D1::RectF(l+20,t+142,l+210,t+350),D2D1_ANTIALIAS_MODE_ALIASED);
            for(size_t i=0;i<state_.rows.size();i++){float y=t+142+float(i)*20-planScroll_;if(y+20<=t+142||y>=t+350)continue;if(hover_==20000+int(i))fill(l+20,y,l+210,y+20,UI::Hover,3);checkDraw(l+26,y+3,targetSelected(state_.rows[i]));text(state_.rows[i].name,l+52,y,150,20,UI::Text);}
            if(state_.rows.empty())text(L"Chưa có LD đang mở",l+26,t+148,180,30,UI::Muted,small_);target_->PopAxisAlignedClip();
            float content=float(state_.rows.size())*20;if(content>208&&scrollVisible_==3){float thumb=std::max(22.f,208*208/content),y=t+142+(208-thumb)*planScroll_/(content-208);fill(l+206,y,l+210,y+thumb,UI::Accent,2);}
            if(!settingsError_.empty())text(settingsError_,l+20,t+354,400,28,UI::Red,small_);return;
        }
        for(int field=1;field<=2;field++){float y=t+fieldY(field);text(field==1?L"Chức Năng":L"Lọc Bọ",l+20,y-26,400,24,UI::Muted,small_);fill(l+20,y,l+420,y+32,UI::Background,5);stroke(l+20,y,l+420,y+32,UI::Border,5);text(field==1?UI::Functions[functionSelection()]:UI::Filters[draft_.filter],l+32,y,350,32,UI::Text);glyph(L"\uE70D",l+388,y,24,32,UI::Accent);}
        if(globalSettings()){float y=t+fieldY(3);text(L"Chờ Sau Kết Nối",l+20,y-26,400,24,UI::Muted,small_);fill(l+20,y,l+420,y+32,UI::Background,5);stroke(l+20,y,l+420,y+32,UI::Border,5);text(std::to_wstring(draftDelay_)+L" giây",l+32,y,350,32,UI::Text);glyph(L"\uE70D",l+388,y,24,32,UI::Accent);text(L"Áp Dụng Cho LD Đang Mở",l+20,t+322,350,28,UI::Text);toggleDraw(l+384,t+327,applyToRunning_);}
        const float cy=t+controlsY();text(L"RECONNECT",l+20,cy,140,32,UI::Text,bold_);toggleDraw(l+169,cy+7,draft_.reconnect);text(L"BÁN",l+245,cy,100,32,UI::Text,bold_);toggleDraw(l+384,cy+7,draft_.sell);
        if(!settingsError_.empty())text(settingsError_,l+20,t+paneHeight()-42,400,26,UI::Red,small_);
        drawDropdown();
    }
    void draw(){
        if(engine_.failure()||IsIconic(window_)||FAILED(resources()))return;
        if(!ramTotal_)updateRam(false);
        target_->BeginDraw();target_->Clear(rgb(UI::Background));
        text(L"LDMenu",18,0,66,32,UI::Text,bold_);
        if(ramTotal_)text(std::to_wstring(ramUsed_)+L"/"+std::to_wstring(ramTotal_)+L" GB",84,0,110,32,UI::Muted,small_);
        fill(0,31,width_,32,UI::Border);
        for(int i=0;i<3;i++){int id=-4+i;float x=width_-96+i*32;if(hover_==id)fill(x,0,x+32,31,UI::Accent);glyph(i==0?L"\uE921":i==1?(IsZoomed(window_)?L"\uE923":L"\uE922"):L"\uE8BB",x,0,32,31,UI::Text,true);}
        iconButton(-5,L"\uE72C",width_-132,1,30);
        iconButton(-13,L"\uE713",width_-168,1,30);
        playButton(width_-204,1);
        target_->PushAxisAlignedClip(D2D1::RectF(20,Top,width_-18,listBottom()),D2D1_ANTIALIAS_MODE_ALIASED);
        for(size_t i=0;i<state_.rows.size();i++){auto&r=state_.rows[i];float y=Top+float(i)*RowHeight-rowScroll_;if(y+RowHeight<=Top||y>=listBottom())continue;
            fill(20,y,width_-20,y+RowHeight-3,UI::Panel,5);text(r.name,34,y,180,RowHeight-3,UI::Text,bold_);
            unsigned c=r.phase==ldcontrol::Phase::Connected?UI::Green:r.phase==ldcontrol::Phase::Failed?UI::Red:r.wanted?UI::Amber:UI::Text;
            text(ldcontrol::phaseText(r.phase),230,y,130,RowHeight-3,c,small_);if(r.phase!=ldcontrol::Phase::Idle)text(r.detail,367,y,width_-543,RowHeight-3,UI::Muted,small_);
            toggleDraw(width_-142,y+5,r.wanted);iconButton(3000+int(i),L"\uE945",width_-88,y,28);iconButton(1000+int(i),L"\uE713",width_-54,y,28);
        }
        if(state_.rows.empty())text(state_.scanning?L"Đang đọc danh sách LDPlayer...":L"Chưa có LDPlayer đang mở",34,Top+12,width_-100,36,UI::Muted);
        target_->PopAxisAlignedClip();scrollbar(1,Top,listBottom(),listContent(),rowScroll_);
        if(!state_.error.empty())text(state_.error,24,height_-36,width_-48,24,UI::Red,small_);
        if(settingsOpen())settingsDraw();
        auto hr=target_->EndDraw();if(hr==D2DERR_RECREATE_TARGET)discard();
    }
    void startDrag(float y){const float visible=listBottom()-Top,content=listContent();if(content<=visible)return;dragScroll_=1;dragY_=y;float thumb=std::max(22.f,visible*visible/content),thumbTop=Top+(visible-thumb)*rowScroll_/(content-visible);if(y<thumbTop||y>thumbTop+thumb)rowScroll_=std::clamp((y-Top-thumb/2)/(visible-thumb),0.f,1.f)*(content-visible);dragStart_=rowScroll_;SetCapture(window_);showScroll(1);}
    LRESULT message(UINT m,WPARAM p,LPARAM l){switch(m){
        case WM_NCCALCSIZE:if(p)return 0;break;
        case WM_NCHITTEST:{POINT pt{GET_X_LPARAM(l),GET_Y_LPARAM(l)};ScreenToClient(window_,&pt);RECT r;GetClientRect(window_,&r);int b=IsZoomed(window_)?0:6;bool left=pt.x<b,right=pt.x>=r.right-b,top=pt.y<b,bottom=pt.y>=r.bottom-b;if(top&&left)return HTTOPLEFT;if(top&&right)return HTTOPRIGHT;if(bottom&&left)return HTBOTTOMLEFT;if(bottom&&right)return HTBOTTOMRIGHT;if(left)return HTLEFT;if(right)return HTRIGHT;if(top)return HTTOP;if(bottom)return HTBOTTOM;float x=pt.x*96.f/dpi_,y=pt.y*96.f/dpi_;return y<32&&x<width_-208?HTCAPTION:HTCLIENT;}
        case WM_GETMINMAXINFO:{auto*info=reinterpret_cast<MINMAXINFO*>(l);info->ptMinTrackSize={LONG(800*dpi_/96),LONG(450*dpi_/96)};MONITORINFO mi{};mi.cbSize=sizeof(mi);if(GetMonitorInfoW(MonitorFromWindow(window_,MONITOR_DEFAULTTONEAREST),&mi)){LONG w=mi.rcWork.right-mi.rcWork.left,h=mi.rcWork.bottom-mi.rcWork.top;if(w*9>h*16)w=h*16/9;else h=w*9/16;info->ptMaxSize={w,h};info->ptMaxPosition={(mi.rcWork.left-mi.rcMonitor.left)+(mi.rcWork.right-mi.rcWork.left-w)/2,(mi.rcWork.top-mi.rcMonitor.top)+(mi.rcWork.bottom-mi.rcWork.top-h)/2};}return 0;}
        case WM_SIZING:{auto*r=reinterpret_cast<RECT*>(l);LONG w=r->right-r->left,h=r->bottom-r->top;if(p==WMSZ_TOP||p==WMSZ_BOTTOM){w=LONG(std::lround(h*16.0/9));r->right=r->left+w;}else{h=LONG(std::lround(w*9.0/16));if(p==WMSZ_TOPLEFT||p==WMSZ_TOPRIGHT)r->top=r->bottom-h;else r->bottom=r->top+h;}return TRUE;}
        case WM_SIZE:width_=LOWORD(l)*96.f/dpi_;height_=HIWORD(l)*96.f/dpi_;if(p==SIZE_MINIMIZED){discard();if(playDelayEdit_)ShowWindow(playDelayEdit_,SW_HIDE);if(idsEdit_)ShowWindow(idsEdit_,SW_HIDE);}else{if(target_)target_->Resize(D2D1::SizeU(LOWORD(l),HIWORD(l)));layoutEditor();}clamp();engine_.setBackground(p==SIZE_MINIMIZED);invalidate();return 0;
        case WM_DPICHANGED:{dpi_=float(HIWORD(p));if(target_)target_->SetDpi(dpi_,dpi_);auto*r=reinterpret_cast<RECT*>(l);SetWindowPos(window_,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOACTIVATE|SWP_NOZORDER);invalidate();return 0;}
        case WM_ERASEBKGND:return 1;
        case WM_CTLCOLOREDIT:if(reinterpret_cast<HWND>(l)==playDelayEdit_||reinterpret_cast<HWND>(l)==idsEdit_){SetTextColor(reinterpret_cast<HDC>(p),RGB(255,255,255));SetBkColor(reinterpret_cast<HDC>(p),RGB(0,0,0));return reinterpret_cast<LRESULT>(GetStockObject(BLACK_BRUSH));}break;
        case WM_NCPAINT:return 0;
        case WM_NCACTIVATE:return DefWindowProcW(window_,m,p,-1);
        case WM_ACTIVATE:invalidate();return DefWindowProcW(window_,m,p,l);
        case WM_PAINT:{PAINTSTRUCT ps;BeginPaint(window_,&ps);draw();EndPaint(window_,&ps);return 0;}
        case ldcontrol::Changed:if(!engine_.failure())updateSnapshot();return 0;
        case ldcontrol::WorkerFailed:state_.playRunning=false;state_.playStopping=false;MessageBoxW(window_,p==1?L"LDMenu đã dừng điều khiển vì không cấp phát được bộ nhớ. Giải phóng RAM/commit rồi mở lại LDMenu.":L"LDMenu đã dừng điều khiển do lỗi nội bộ. Các game được giữ nguyên; hãy mở lại LDMenu.",L"LDMenu",MB_OK|MB_ICONERROR);return 0;
        case WM_MOUSEMOVE:{float x=GET_X_LPARAM(l)*96.f/dpi_,y=GET_Y_LPARAM(l)*96.f/dpi_;if(dragScroll_){const float visible=listBottom()-Top,content=listContent();if(content<=visible){dragScroll_=0;ReleaseCapture();return 0;}float thumb=std::max(22.f,visible*visible/content);if(visible>thumb)rowScroll_=dragStart_+(y-dragY_)*(content-visible)/(visible-thumb);clamp();showScroll(1);return 0;}int id=hit(x,y);if(id!=hover_){hover_=id;invalidate();}if(id==-30)showScroll(1);if(!tracking_){TRACKMOUSEEVENT t{sizeof(t),TME_LEAVE,window_,0};TrackMouseEvent(&t);tracking_=true;}return 0;}
        case WM_MOUSELEAVE:hover_=-99;tracking_=false;invalidate();return 0;
        case WM_LBUTTONDOWN:{float x=GET_X_LPARAM(l)*96.f/dpi_,y=GET_Y_LPARAM(l)*96.f/dpi_;if(hit(x,y)==-30)startDrag(y);return 0;}
        case WM_LBUTTONUP:if(dragScroll_){dragScroll_=0;ReleaseCapture();invalidate();return 0;}SetFocus(window_);action(hit(GET_X_LPARAM(l)*96.f/dpi_,GET_Y_LPARAM(l)*96.f/dpi_));invalidate();return 0;
        case WM_CAPTURECHANGED:dragScroll_=0;return 0;
        case WM_MOUSEWHEEL:{if(planTab()){planScroll_-=GET_WHEEL_DELTA_WPARAM(p)*40.f/WHEEL_DELTA;clamp();showScroll(3);return 0;}if(settingsOpen())return 0;rowScroll_-=GET_WHEEL_DELTA_WPARAM(p)*45.f/WHEEL_DELTA;clamp();showScroll(1);return 0;}
        case WM_TIMER:if(p==1&&!dragScroll_&&hover_!=-30){KillTimer(window_,1);scrollVisible_=0;invalidate();}else if(p==2&&!IsIconic(window_)){updateRam(true);}return 0;
        case WM_KEYDOWN:
            if(p==VK_ESCAPE&&settingsOpen()){if(dropdown_)dropdown_=0;else settingsIndex_=-1;layoutEditor();invalidate();return 0;}
            if(settingsOpen()){if(dropdown_&&(p==VK_DOWN||p==VK_UP)){int n=dropdownCount();dropSelection_=(dropSelection_+(p==VK_UP?-1:1)+n)%n;invalidate();return 0;}if(p==VK_RETURN){action(dropdown_?10000+dropSelection_:quickMode_?-50:-8);return 0;}return 0;}
            if(p==VK_F5){engine_.refresh();return 0;}if(p==VK_TAB||p==VK_DOWN||p==VK_UP){if(!state_.rows.empty()){int n=int(state_.rows.size());focus_=(focus_+(p==VK_UP?-1:1)+n)%n;rowScroll_=std::clamp(float(focus_)*RowHeight-(listBottom()-Top)/2,0.f,std::max(0.f,listContent()-(listBottom()-Top)));showScroll(1);}return 0;}if(p==VK_SPACE||p==VK_RETURN){action(focus_);return 0;}break;
        case WM_CLOSE:KillTimer(window_,2);engine_.stop();DestroyWindow(window_);return 0;
        case WM_DESTROY:PostQuitMessage(0);return 0;
    }return DefWindowProcW(window_,m,p,l);}
public:
    ~Application(){engine_.stop();discard();release(playShape_);if(editFont_)DeleteObject(editFont_);release(caption_);release(icon_);release(bold_);release(small_);release(font_);release(writeFactory_);release(factory_);}
    bool initialize(HINSTANCE instance){
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);if(FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,&factory_))||FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),reinterpret_cast<IUnknown**>(&writeFactory_))))return false;
        auto font=[&](float size,DWRITE_FONT_WEIGHT weight,IDWriteTextFormat**out,const wchar_t*name=L"Segoe UI"){auto hr=writeFactory_->CreateTextFormat(name,nullptr,weight,DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,size,L"vi-VN",out);if(SUCCEEDED(hr)){(*out)->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);(*out)->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);}return SUCCEEDED(hr);};
        if(!font(13,DWRITE_FONT_WEIGHT_NORMAL,&font_)||!font(11,DWRITE_FONT_WEIGHT_NORMAL,&small_)||!font(13,DWRITE_FONT_WEIGHT_SEMI_BOLD,&bold_)||!font(14,DWRITE_FONT_WEIGHT_NORMAL,&icon_,L"Segoe MDL2 Assets")||!font(10,DWRITE_FONT_WEIGHT_NORMAL,&caption_,L"Segoe MDL2 Assets"))return false;
        if(FAILED(factory_->CreatePathGeometry(&playShape_)))return false;ID2D1GeometrySink*sink=nullptr;if(FAILED(playShape_->Open(&sink)))return false;sink->BeginFigure(D2D1::Point2F(8.5f,7.2f),D2D1_FIGURE_BEGIN_FILLED);sink->AddBezier(D2D1::BezierSegment(D2D1::Point2F(8.5f,5.6f),D2D1::Point2F(9.7f,5),D2D1::Point2F(11.1f,5.8f)));sink->AddLine(D2D1::Point2F(22,12.6f));sink->AddBezier(D2D1::BezierSegment(D2D1::Point2F(23.4f,13.4f),D2D1::Point2F(23.4f,15),D2D1::Point2F(22,15.8f)));sink->AddLine(D2D1::Point2F(11.1f,22.6f));sink->AddBezier(D2D1::BezierSegment(D2D1::Point2F(9.7f,23.4f),D2D1::Point2F(8.5f,22.8f),D2D1::Point2F(8.5f,21.2f)));sink->AddLine(D2D1::Point2F(8.5f,7.2f));sink->EndFigure(D2D1_FIGURE_END_CLOSED);const auto shapeResult=sink->Close();sink->Release();if(FAILED(shapeResult))return false;
        WNDCLASSEXW cls{};cls.cbSize=sizeof(cls);cls.lpfnWndProc=procedure;cls.hInstance=instance;cls.hCursor=LoadCursor(nullptr,IDC_ARROW);cls.hIcon=LoadIconW(instance,MAKEINTRESOURCEW(1));cls.hIconSm=static_cast<HICON>(LoadImageW(instance,MAKEINTRESOURCEW(1),IMAGE_ICON,GetSystemMetrics(SM_CXSMICON),GetSystemMetrics(SM_CYSMICON),LR_SHARED));cls.lpszClassName=L"LDMenu.Direct2D";if(!RegisterClassExW(&cls))return false;
        dpi_=float(GetDpiForSystem());RECT work;SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0);int w=std::min(int(800*dpi_/96),int(work.right-work.left-40)),h=int(std::lround(w*9.0/16));if(h>work.bottom-work.top-50){h=int(work.bottom-work.top-50);w=int(std::lround(h*16.0/9));}
        window_=CreateWindowExW(0,cls.lpszClassName,L"LDMenu",WS_POPUP|WS_THICKFRAME|WS_MINIMIZEBOX|WS_MAXIMIZEBOX|WS_SYSMENU|WS_CLIPCHILDREN,work.left+(work.right-work.left-w)/2,work.top+(work.bottom-work.top-h)/2,w,h,nullptr,nullptr,instance,this);if(!window_)return false;
        playDelayEdit_=CreateWindowExW(0,L"EDIT",L"5",WS_CHILD|WS_TABSTOP|ES_NUMBER|ES_AUTOHSCROLL|ES_RIGHT,0,0,100,24,window_,reinterpret_cast<HMENU>(420),instance,nullptr);if(!playDelayEdit_)return false;SetWindowLongPtrW(playDelayEdit_,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(this));editProcedure_=reinterpret_cast<WNDPROC>(SetWindowLongPtrW(playDelayEdit_,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(editProcedure)));editFont_=CreateFontW(-int(13*dpi_/96),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");SendMessageW(playDelayEdit_,WM_SETFONT,reinterpret_cast<WPARAM>(editFont_),TRUE);SendMessageW(playDelayEdit_,EM_SETLIMITTEXT,4,0);
        idsEdit_=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|WS_TABSTOP|ES_AUTOHSCROLL,0,0,100,24,window_,reinterpret_cast<HMENU>(421),instance,nullptr);if(!idsEdit_)return false;SetWindowLongPtrW(idsEdit_,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(this));SetWindowLongPtrW(idsEdit_,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(editProcedure));SendMessageW(idsEdit_,WM_SETFONT,reinterpret_cast<WPARAM>(editFont_),TRUE);SendMessageW(idsEdit_,EM_SETLIMITTEXT,1024,0);SendMessageW(idsEdit_,0x1501,TRUE,reinterpret_cast<LPARAM>(L"Để trống: không lọc ID"));
        int dark=1;DwmSetWindowAttribute(window_,20,&dark,sizeof(dark));int corners=2;DwmSetWindowAttribute(window_,33,&corners,sizeof(corners));DWORD border=0xfffffffe;DwmSetWindowAttribute(window_,34,&border,sizeof(border));DWMNCRENDERINGPOLICY nc=DWMNCRP_DISABLED;DwmSetWindowAttribute(window_,DWMWA_NCRENDERING_POLICY,&nc,sizeof(nc));
        updateRam(false);SetTimer(window_,2,3000,nullptr);
        engine_.start(window_);ShowWindow(window_,SW_SHOW);UpdateWindow(window_);return true;
    }
    int run(){MSG m{};while(GetMessageW(&m,nullptr,0,0)>0){TranslateMessage(&m);DispatchMessageW(&m);}return int(m.wParam);}
};
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int){
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
    HANDLE singleton=CreateMutexW(nullptr,TRUE,L"Local\\NPHReconnectConsole-20261023");if(!singleton||GetLastError()==ERROR_ALREADY_EXISTS){if(singleton)CloseHandle(singleton);MessageBoxW(nullptr,L"Hãy thoát Config hoặc LDMenu đang chạy trước.",L"LDMenu",MB_OK|MB_ICONINFORMATION);return 3;}
    int code=0;{Application app;if(app.initialize(instance))code=app.run();else{MessageBoxW(nullptr,L"Không khởi tạo được Direct2D.",L"LDMenu",MB_OK|MB_ICONERROR);code=1;}}ReleaseMutex(singleton);CloseHandle(singleton);return code;
}
