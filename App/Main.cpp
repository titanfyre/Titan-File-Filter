#include "../SDK/FileFilter.h"
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <algorithm>
#include <array>
#include <deque>
#include <fstream>
#include <memory>
#include <sstream>
#include <cmath>
namespace {
constexpr COLORREF Blue=RGB(38,96,245),Ink=RGB(20,30,48),Muted=RGB(91,105,125),Paper=RGB(245,247,251);
HWND window{},eventsView{},rulesView{},processBox{},folderBox{},operationBox{},actionBox{},startButton{},stopButton{};
HFONT body{},heading{};HBRUSH background{},white{};bool active=false,preview=false;unsigned nextId=1;
FileFilter::Session session;std::vector<FF_RULE> rules;std::deque<FF_EVENT> events;
FF_STATUS status{},previous{};std::wstring note=L"Ready. Choose a folder, add an Audit rule, then Start. No policy is active.";
std::deque<double> rates;ULONGLONG lastUpdate=0;double operationsPerSecond=0;
void Text(HDC dc,int x,int y,const std::wstring& value,COLORREF color=Ink,HFONT font=nullptr){SelectObject(dc,font?font:body);SetTextColor(dc,color);SetBkMode(dc,TRANSPARENT);TextOutW(dc,x,y,value.c_str(),static_cast<int>(value.size()));}
void Box(HDC dc,RECT rect,COLORREF color,int radius=14){HBRUSH brush=CreateSolidBrush(color);auto old=SelectObject(dc,brush);auto pen=SelectObject(dc,GetStockObject(NULL_PEN));RoundRect(dc,rect.left,rect.top,rect.right,rect.bottom,radius,radius);SelectObject(dc,pen);SelectObject(dc,old);DeleteObject(brush);}
std::wstring Value(HWND control){int length=GetWindowTextLengthW(control);if(length<0||length>1024)throw std::invalid_argument("Input text is too long.");std::wstring value(static_cast<std::size_t>(length)+1,L'\0');GetWindowTextW(control,value.data(),length+1);value.resize(length);return value;}
unsigned Number(HWND control,unsigned maximum){auto s=Value(control);if(s.empty())return 0;unsigned long long n=0;for(auto digit:s){if(digit<L'0'||digit>L'9')throw std::invalid_argument("PID must contain only digits.");n=n*10+static_cast<unsigned>(digit-L'0');if(n>maximum)throw std::invalid_argument("PID is out of range.");}return static_cast<unsigned>(n);}
void Error(const std::exception& e){std::string text=e.what();note=std::wstring(text.begin(),text.end());InvalidateRect(window,nullptr,FALSE);}
void Cell(HWND list,int row,int column,const std::wstring& value){LVITEMW item{};item.mask=LVIF_TEXT;item.iItem=row;item.iSubItem=column;item.pszText=const_cast<wchar_t*>(value.c_str());if(!column)ListView_InsertItem(list,&item);else ListView_SetItem(list,&item);}
std::wstring Hex(unsigned value){wchar_t text[16]{};swprintf_s(text,L"0x%08X",value);return text;}
void Event(const FF_EVENT& e){
 if(events.size()==500){events.pop_front();ListView_DeleteItem(eventsView,0);}events.push_back(e);int row=ListView_GetItemCount(eventsView);
 Cell(eventsView,row,0,std::to_wstring(e.Sequence));Cell(eventsView,row,1,std::to_wstring(e.ProcessId));Cell(eventsView,row,2,FileFilter::Operations(e.Operations));
 Cell(eventsView,row,3,FileFilter::Path(e));Cell(eventsView,row,4,e.Flags&FF_FLAG_DENIED?L"DENIED":L"Observed");Cell(eventsView,row,5,Hex(e.Status));Cell(eventsView,row,6,std::to_wstring(e.RuleId));
}
void ListRules(){
 ListView_DeleteAllItems(rulesView);
 for(std::size_t i=0;i<rules.size();++i){const auto& r=rules[i];int row=static_cast<int>(i);
  Cell(rulesView,row,0,std::to_wstring(r.Id));Cell(rulesView,row,1,r.Action?L"Deny":L"Audit");Cell(rulesView,row,2,r.ProcessId?std::to_wstring(r.ProcessId):L"Any user process");
  Cell(rulesView,row,3,FileFilter::Operations(r.Operations));Cell(rulesView,row,4,FileFilter::Folder(r));
 }
}
HWND Control(const wchar_t* type,const wchar_t* text,int id,DWORD style=0){auto h=CreateWindowExW(0,type,text,WS_CHILD|WS_VISIBLE|WS_TABSTOP|style,0,0,10,10,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),nullptr,nullptr);SendMessageW(h,WM_SETFONT,reinterpret_cast<WPARAM>(body),TRUE);return h;}
HWND Button(const wchar_t* text,int id){return Control(L"BUTTON",text,id,BS_OWNERDRAW);}
void Combo(HWND box,const std::initializer_list<const wchar_t*>& values,int selected){for(auto text:values)SendMessageW(box,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text));SendMessageW(box,CB_SETCURSEL,selected,0);}
HWND List(const std::initializer_list<std::pair<const wchar_t*,int>>& columns){
 HWND list=Control(WC_LISTVIEWW,L"",0,LVS_REPORT|LVS_SINGLESEL|LVS_SHOWSELALWAYS);
 ListView_SetExtendedListViewStyle(list,LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER|LVS_EX_LABELTIP);unsigned i=0;
 for(const auto& col:columns){LVCOLUMNW column{};column.mask=LVCF_WIDTH|LVCF_TEXT;column.cx=col.second;column.pszText=const_cast<wchar_t*>(col.first);ListView_InsertColumn(list,static_cast<int>(i++),&column);}return list;
}
void Layout(){RECT r{};GetClientRect(window,&r);int width=r.right;
 MoveWindow(startButton,width-288,22,130,38,TRUE);MoveWindow(stopButton,width-148,22,124,38,TRUE);
 MoveWindow(GetDlgItem(window,8),width-174,264,150,36,TRUE);MoveWindow(GetDlgItem(window,3),width-174,308,150,34,TRUE);
 MoveWindow(folderBox,24,314,width-730,32,TRUE);MoveWindow(processBox,width-694,314,100,32,TRUE);MoveWindow(operationBox,width-582,314,190,240,TRUE);MoveWindow(actionBox,width-380,314,190,160,TRUE);
 MoveWindow(rulesView,24,360,width-214,104,TRUE);MoveWindow(GetDlgItem(window,4),width-174,360,150,34,TRUE);
 MoveWindow(GetDlgItem(window,5),width-174,406,150,34,TRUE);MoveWindow(eventsView,24,514,width-48,std::max(90,static_cast<int>(r.bottom)-580),TRUE);
 MoveWindow(GetDlgItem(window,6),width-336,474,150,30,TRUE);MoveWindow(GetDlgItem(window,7),width-174,474,150,30,TRUE);
}
void Stop(){session.Stop();active=false;EnableWindow(startButton,!preview);EnableWindow(stopButton,FALSE);note=L"Stopped. Scoped policy removed; normal Windows access restored.";}
std::string Utf8(const std::wstring& text){if(text.empty())return {};int size=WideCharToMultiByte(CP_UTF8,0,text.data(),static_cast<int>(text.size()),nullptr,0,nullptr,nullptr);std::string result(size,'\0');WideCharToMultiByte(CP_UTF8,0,text.data(),static_cast<int>(text.size()),result.data(),size,nullptr,nullptr);return result;}
std::string Csv(const std::wstring& text){auto value=Utf8(text);std::string result="\"";for(char c:value){result+=c;if(c=='\"')result+=c;}return result+"\"";}
void Export(){
 wchar_t path[MAX_PATH]=L"file-events.csv";OPENFILENAMEW dialog{};dialog.lStructSize=sizeof(dialog);dialog.hwndOwner=window;dialog.lpstrFilter=L"CSV file\0*.csv\0\0";dialog.lpstrFile=path;dialog.nMaxFile=MAX_PATH;dialog.lpstrDefExt=L"csv";dialog.Flags=OFN_OVERWRITEPROMPT|OFN_PATHMUSTEXIST;if(!GetSaveFileNameW(&dialog))return;
 std::ofstream file(path,std::ios::binary);if(!file)throw std::runtime_error("Cannot create the selected export file.");file<<"sequence,pid,operations,path,destination,decision,ntstatus,rule\n";
 for(const auto& e:events)file<<e.Sequence<<','<<e.ProcessId<<','<<Csv(FileFilter::Operations(e.Operations))<<','<<Csv(FileFilter::Path(e))<<','<<Csv(FileFilter::Path(e,true))<<','<<(e.Flags&FF_FLAG_DENIED?"DENY":"OBSERVED")<<','<<Csv(Hex(e.Status))<<','<<e.RuleId<<'\n';
 if(!file)throw std::runtime_error("CSV export failed while writing.");note=L"Displayed file metadata exported. No file contents were read or exported.";
}
void ChooseFolder(){IFileDialog* dialog=nullptr;if(FAILED(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog))))throw std::runtime_error("Windows folder picker is unavailable.");
 struct Close{IFileDialog* p;~Close(){p->Release();}} close{dialog};DWORD options=0;dialog->GetOptions(&options);dialog->SetOptions(options|FOS_PICKFOLDERS|FOS_FORCEFILESYSTEM);if(FAILED(dialog->Show(window)))return;IShellItem* item=nullptr;if(SUCCEEDED(dialog->GetResult(&item))){PWSTR path=nullptr;if(SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH,&path))){SetWindowTextW(folderBox,path);CoTaskMemFree(path);}item->Release();}}
void Paint(HDC dc){
 RECT r{};GetClientRect(window,&r);FillRect(dc,&r,background);Box(dc,{0,0,r.right,82},Ink,0);
 Text(dc,24,15,L"Titan File Filter",RGB(255,255,255),heading);Text(dc,25,49,preview?L"OFFLINE PREVIEW / synthetic data / no driver access":L"Scoped file-access policy / native C++ SDK / NTFS + ReFS target",RGB(192,207,230));
 int card=(r.right-72)/3;
 const std::array<std::wstring,3> labels{L"Observed operations",L"Denied operations",L"Dropped log records"};
 const std::array<std::wstring,3> values{std::to_wstring(status.Observed),std::to_wstring(status.Denied),std::to_wstring(status.Dropped)};
 for(int i=0;i<3;++i){int x=24+i*(card+12);Box(dc,{x,98,x+card,176},RGB(255,255,255));Text(dc,x+16,108,labels[i],Muted);Text(dc,x+16,133,values[i],Blue,heading);}
 Box(dc,{24,188,r.right-24,250},RGB(255,255,255));Text(dc,40,202,std::to_wstring(static_cast<unsigned long long>(operationsPerSecond))+L" ops/s   |   Unresolved names: "+std::to_wstring(status.NameFailures)+L"   Skipped: "+std::to_wstring(status.Skipped),Blue);
 if(rates.size()>1){const int left=r.right/2,right=r.right-42;double maximum=1;for(auto v:rates)maximum=std::max(maximum,v);HPEN pen=CreatePen(PS_SOLID,2,Blue);auto old=SelectObject(dc,pen);for(std::size_t i=0;i<rates.size();++i){int x=left+static_cast<int>(i)*(right-left)/63,y=238-static_cast<int>(rates[i]/maximum*38);if(i)LineTo(dc,x,y);else MoveToEx(dc,x,y,nullptr);}SelectObject(dc,old);DeleteObject(pen);}
 Text(dc,24,268,L"Folder access rules",Ink,heading);Text(dc,24,292,L"Existing local folder",Muted);Text(dc,r.right-694,292,L"PID (0 = any)",Muted);Text(dc,r.right-582,292,L"Operations",Muted);Text(dc,r.right-380,292,L"Action",Muted);
 Text(dc,24,476,L"File operation decisions",Ink,heading);
 Text(dc,24,r.bottom-54,note,Muted);Text(dc,24,r.bottom-29,L"First match wins. Audit respects Windows permissions. Close restores access. Paging/mapped/offloaded I/O is outside this preview.",Muted);
}
LRESULT CALLBACK Proc(HWND h,UINT message,WPARAM w,LPARAM l){
 switch(message){
 case WM_CREATE:{
  body=CreateFontW(16,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");heading=CreateFontW(24,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");background=CreateSolidBrush(Paper);white=CreateSolidBrush(RGB(255,255,255));
  window=h;startButton=Button(L"Start session",1);stopButton=Button(L"Stop / restore",2);EnableWindow(stopButton,FALSE);
  Button(L"Add rule",3);Button(L"Remove selected",4);Button(L"Clear all rules",5);Button(L"Driver setup",6);Button(L"Export CSV",7);
  processBox=Control(L"EDIT",L"0",10,WS_BORDER|ES_NUMBER);folderBox=Control(L"EDIT",L"",11,WS_BORDER|ES_AUTOHSCROLL);SendMessageW(processBox,EM_SETLIMITTEXT,12,0);SendMessageW(folderBox,EM_SETLIMITTEXT,1024,0);
  operationBox=Control(L"COMBOBOX",L"",13,CBS_DROPDOWNLIST);Combo(operationBox,{L"All supported",L"Read",L"Write / create",L"Delete",L"Rename / hardlink",L"Open",L"Metadata",L"Security",L"Execute-open"},0);
  actionBox=Control(L"COMBOBOX",L"",15,CBS_DROPDOWNLIST);Combo(actionBox,{L"Audit",L"Deny"},0);Button(L"Choose folder",8);
  rulesView=List({{L"ID",45},{L"Action",65},{L"Process",120},{L"Operations",200},{L"Native folder scope",420}});
  eventsView=List({{L"Sequence",75},{L"PID",65},{L"Operations",160},{L"Requested path",410},{L"Decision",90},{L"NTSTATUS",105},{L"Rule",50}});
  if(preview){FF_RULE sample{};sample.Id=1;sample.Action=FF_DENY;sample.Operations=FF_DELETE;std::wstring scope=L"\\Device\\HarddiskVolume3\\Example";sample.PathLength=static_cast<FF_U32>(scope.size());std::copy(scope.begin(),scope.end(),sample.Folder);rules.push_back(sample);sample.Id=2;sample.Action=FF_AUDIT;sample.Operations=FF_ALL;rules.push_back(sample);ListRules();EnableWindow(startButton,FALSE);for(int id:{3,4,5})EnableWindow(GetDlgItem(h,id),FALSE);status.Observed=128;status.Denied=12;note=L"Synthetic offline preview. Altitude allocation, Microsoft signing and kernel runtime validation are pending.";for(unsigned i=0;i<12;++i){FF_EVENT e{};e.Sequence=i+1;e.ProcessId=4242;e.Operations=i%3==0?FF_DELETE:FF_WRITE;e.RuleId=i%3==0?1u:2u;e.Flags=i%3==0?FF_FLAG_DENIED:0;e.Status=e.Flags?0xC0000022u:0;std::wstring path=L"\\Device\\HarddiskVolume3\\Example\\document-"+std::to_wstring(i)+L".txt";e.PathLength=static_cast<FF_U16>(path.size());std::copy(path.begin(),path.end(),e.Path);Event(e);}for(unsigned i=0;i<64;++i)rates.push_back(400+200*std::sin(i*.2));}
  SetTimer(h,1,250,nullptr);Layout();return 0;
 }
 case WM_SIZE:Layout();InvalidateRect(h,nullptr,FALSE);return 0;
 case WM_GETMINMAXINFO:reinterpret_cast<MINMAXINFO*>(l)->ptMinTrackSize={1120,740};return 0;
 case WM_COMMAND:if(HIWORD(w)==BN_CLICKED)try{
  switch(LOWORD(w)){
   case 1:if(rules.empty())throw std::runtime_error("Choose a folder and add an Audit or Deny rule first.");session.Start(rules);active=true;lastUpdate=GetTickCount64();previous={};events.clear();ListView_DeleteAllItems(eventsView);EnableWindow(startButton,FALSE);EnableWindow(stopButton,TRUE);note=L"Scoped session active. Decisions are evaluated in the kernel without waiting for this interface.";break;
   case 2:Stop();break;
   case 3:{
    if(rules.size()==FF_RULE_LIMIT)throw std::runtime_error("Maximum of 32 rules reached.");
    const unsigned masks[]={FF_ALL,FF_READ,FF_WRITE,FF_DELETE,FF_RENAME_LINK,FF_OPEN,FF_METADATA,FF_SECURITY,FF_EXECUTE_OPEN};
    auto selected=static_cast<unsigned>(SendMessageW(operationBox,CB_GETCURSEL,0,0));if(selected>=std::size(masks))throw std::runtime_error("Choose the operation mask.");
    auto candidate=rules;candidate.push_back(FileFilter::Rule(nextId,Value(folderBox),masks[selected],static_cast<unsigned>(SendMessageW(actionBox,CB_GETCURSEL,0,0)),Number(processBox,0xFFFFFFFFu)));
    if(active)session.SetRules(candidate);rules=std::move(candidate);++nextId;ListRules();note=active?L"Policy replaced atomically for subsequent supported file requests.":L"Rule prepared. Start session to enable this policy.";break;
   }
   case 4:{int selected=ListView_GetNextItem(rulesView,-1,LVNI_SELECTED);if(selected<0)throw std::runtime_error("Select the rule to remove.");auto candidate=rules;candidate.erase(candidate.begin()+selected);if(active)session.SetRules(candidate);rules=std::move(candidate);ListRules();break;}
   case 5:if(active)session.SetRules({});rules.clear();ListRules();note=L"Rules cleared. Windows file permissions remain in effect.";break;
   case 6:{wchar_t path[MAX_PATH]{};GetModuleFileNameW(nullptr,path,MAX_PATH);std::wstring setup=path;setup.resize(setup.find_last_of(L"\\/")+1);setup+=L"FileFilterSetup.exe";if(reinterpret_cast<INT_PTR>(ShellExecuteW(h,L"open",setup.c_str(),nullptr,nullptr,SW_SHOW))<=32)throw std::runtime_error("The matching installer is available after a genuine Microsoft altitude allocation, signing and validation.");break;}
   case 7:Export();break;
   case 8:ChooseFolder();break;
  }
  InvalidateRect(h,nullptr,FALSE);
 }catch(const std::exception& e){Error(e);}return 0;
 case WM_TIMER:if(active)try{for(unsigned batch=0;batch<8;++batch){auto items=session.Read();for(const auto& e:items)Event(e);if(items.size()<FF_READ_LIMIT)break;}status=session.Status();auto now=GetTickCount64();double seconds=std::max(.001,double(now-lastUpdate)/1000.);operationsPerSecond=status.Observed>=previous.Observed?double(status.Observed-previous.Observed)/seconds:0;previous=status;lastUpdate=now;if(rates.size()==64)rates.pop_front();rates.push_back(operationsPerSecond);InvalidateRect(h,nullptr,FALSE);}catch(const std::exception& e){Stop();Error(e);}return 0;
 case WM_DRAWITEM:{auto* item=reinterpret_cast<DRAWITEMSTRUCT*>(l);if(item->CtlType!=ODT_BUTTON)break;bool enabled=IsWindowEnabled(item->hwndItem)!=FALSE;Box(item->hDC,item->rcItem,enabled?Blue:RGB(219,225,236),10);wchar_t text[80]{};GetWindowTextW(item->hwndItem,text,80);SelectObject(item->hDC,body);SetTextColor(item->hDC,enabled?RGB(255,255,255):Muted);SetBkMode(item->hDC,TRANSPARENT);DrawTextW(item->hDC,text,-1,&item->rcItem,DT_CENTER|DT_VCENTER|DT_SINGLELINE);if(item->itemState&ODS_FOCUS){RECT focus=item->rcItem;InflateRect(&focus,-4,-4);DrawFocusRect(item->hDC,&focus);}return TRUE;}
 case WM_SETCURSOR:if(LOWORD(l)==HTCLIENT&&reinterpret_cast<HWND>(w)!=h){wchar_t type[40]{};GetClassNameW(reinterpret_cast<HWND>(w),type,40);if(!_wcsicmp(type,L"Button")||!_wcsicmp(type,L"ComboBox")){SetCursor(LoadCursorW(nullptr,IDC_HAND));return TRUE;}}break;
 case WM_CTLCOLOREDIT:case WM_CTLCOLORLISTBOX:SetTextColor(reinterpret_cast<HDC>(w),Ink);SetBkColor(reinterpret_cast<HDC>(w),RGB(255,255,255));return reinterpret_cast<LRESULT>(white);
 case WM_ERASEBKGND:return 1;
 case WM_PAINT:{PAINTSTRUCT paint{};HDC dc=BeginPaint(h,&paint);RECT r{};GetClientRect(h,&r);HDC buffer=CreateCompatibleDC(dc);HBITMAP bitmap=CreateCompatibleBitmap(dc,std::max(1L,r.right),std::max(1L,r.bottom));auto old=SelectObject(buffer,bitmap);Paint(buffer);BitBlt(dc,0,0,r.right,r.bottom,buffer,0,0,SRCCOPY);SelectObject(buffer,old);DeleteObject(bitmap);DeleteDC(buffer);EndPaint(h,&paint);return 0;}
 case WM_DESTROY:KillTimer(h,1);session.Stop();DeleteObject(body);DeleteObject(heading);DeleteObject(background);DeleteObject(white);PostQuitMessage(0);return 0;
 }
 return DefWindowProcW(h,message,w,l);
}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR command,int show){
 SetProcessDPIAware();CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);preview=std::wstring(command).find(L"--preview")!=std::wstring::npos;INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_LISTVIEW_CLASSES};InitCommonControlsEx(&controls);
 WNDCLASSW c{};c.lpfnWndProc=Proc;c.hInstance=instance;c.lpszClassName=L"TitanFileFilter";c.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassW(&c);
 window=CreateWindowW(c.lpszClassName,L"Titan File Filter",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,1200,820,nullptr,nullptr,instance,nullptr);if(!window)return 1;ShowWindow(window,show);
 MSG message{};while(GetMessageW(&message,nullptr,0,0)>0)if(!IsDialogMessageW(window,&message)){TranslateMessage(&message);DispatchMessageW(&message);}return 0;
}
