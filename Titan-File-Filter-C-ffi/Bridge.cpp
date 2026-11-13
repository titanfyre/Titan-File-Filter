#include "SDK/FileFilter.h"
#include "WireTypes.h"
#include <map>
struct ProductSession{

std::unique_ptr<FileFilter::Session> session;
FileFilter::Session& Get(){if(!session)session=std::make_unique<FileFilter::Session>();return *session;}
std::vector<FF_RULE> Rules(const Json& a){auto array=a.at("rules");Require(array.is_array() && array.size()<=FF_RULE_LIMIT,"Too many rules");std::vector<FF_RULE> rules;for(const auto& r:array)rules.push_back(FileFilter::Rule(Number(r,"id"),Wide(r.at("folder").get<std::string>()),Number(r,"operations",FF_ALL,FF_ALL),Number(r,"action",FF_AUDIT,FF_DENY),Number(r,"pid")));return rules;}

Json Invoke(const std::string& op,const Json& a){

if(op=="check"){auto rule=FromWire<FF_RULE>(a.at("rule"));return {{"valid",FileFilter::Validate(rule)},{"value",EncodeWire(rule)}};}
if(op=="make_rule")return EncodeWire(FileFilter::Rule(Number(a,"id"),Wide(a.at("folder").get<std::string>()),Number(a,"operations",FF_ALL,FF_ALL),Number(a,"action",FF_AUDIT,FF_DENY),Number(a,"pid")));
if(op=="start"){Get().Start(Rules(a));return Json::object();}
if(op=="set_rules"){Get().SetRules(Rules(a));return Json::object();}
if(op=="stop"){if(session)session->Stop();return Json::object();}
if(op=="status")return EncodeWire(Get().Status());
if(op=="read")return VectorWire(Get().Read());
if(op=="event_path"){auto e=FromWire<FF_EVENT>(a.at("event"));Require(e.PathLength<FF_PATH_LIMIT && e.TargetLength<FF_PATH_LIMIT,"Path exceeds capacity");return {{"path",Utf8(FileFilter::Path(e,a.value("destination",false)))}};}
if(op=="operation_names")return {{"operations",Utf8(FileFilter::Operations(Number(a,"mask",0,FF_ALL)))}};

throw SdkError(50,"Unknown operation");}
};
static Runtime<ProductSession> runtime;
extern "C" __declspec(dllexport) unsigned titan_file_filter_json_open(std::uint64_t* id)noexcept{return runtime.Open(id);}
extern "C" __declspec(dllexport) unsigned titan_file_filter_json_close(std::uint64_t id)noexcept{return runtime.Close(id);}
extern "C" __declspec(dllexport) char* titan_file_filter_json_call(std::uint64_t id,const char* request,std::uint32_t length)noexcept{return runtime.Call(id,request,length);}
extern "C" __declspec(dllexport) void titan_file_filter_json_free(char* response)noexcept{delete[] response;}
