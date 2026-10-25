#ifndef TITAN_FILE_FILTER_H
#define TITAN_FILE_FILTER_H
/* Fixed, pointer-free protocol shared with the separate private minifilter. */
#define FF_VERSION 1u
#define FF_MAGIC 0x46464C54u
#define FF_PORT_NAME L"\\TitanFileFilterPort"
#define FF_RULE_LIMIT 32u
#define FF_PATH_LIMIT 320u
#define FF_RING_CAPACITY 1024u
#define FF_READ_LIMIT 32u
#define FF_CONTEXT_LIMIT 128u
#define FF_AUDIT 0u
#define FF_DENY 1u
#define FF_OPEN 1u
#define FF_READ 2u
#define FF_WRITE 4u
#define FF_DELETE 8u
#define FF_RENAME_LINK 16u
#define FF_METADATA 32u
#define FF_SECURITY 64u
#define FF_EXECUTE_OPEN 128u
#define FF_ALL 255u
#define FF_FLAG_DENIED 1u
#define FF_FLAG_PATH_TRUNCATED 2u
#define FF_FLAG_TARGET_TRUNCATED 4u
#define FF_FLAG_TARGET_UNKNOWN 8u
#define FF_FLAG_PATH_INTENT 16u
#define FF_COMMAND_STATUS 1u
#define FF_COMMAND_STOP 2u
#define FF_COMMAND_READ 3u
#define FF_COMMAND_POLICY 4u
typedef unsigned char FF_U8;
typedef unsigned short FF_U16;
typedef unsigned int FF_U32;
typedef unsigned long long FF_U64;
typedef struct FF_RULE {
    FF_U32 Id,ProcessId,Operations,Action,PathLength,Reserved[3];
    FF_U16 Folder[FF_PATH_LIMIT];
} FF_RULE;
typedef struct FF_COMMAND { FF_U32 Magic,Version,Size,Operation,Count,Reserved; } FF_COMMAND;
typedef struct FF_POLICY { FF_COMMAND Header; FF_RULE Rules[FF_RULE_LIMIT]; } FF_POLICY;
typedef struct FF_EVENT {
    FF_U64 Sequence,Generation,StartQpc,DurationQpc,RequestedBytes,TransferredBytes;
    FF_U32 ProcessId,RuleId,Operations,Status,Flags;
    FF_U16 PathLength,TargetLength;
    FF_U16 Path[FF_PATH_LIMIT],Target[FF_PATH_LIMIT];
} FF_EVENT;
typedef struct FF_STATUS {
    FF_U32 Magic,Version,Size,Count,RuleCount,Inflight,Capacity,Reserved;
    FF_U64 Generation,Frequency,Observed,Denied,Dropped,NameFailures,Skipped,NextSequence;
} FF_STATUS;
typedef struct FF_REPLY { FF_STATUS Status; FF_EVENT Events[FF_READ_LIMIT]; } FF_REPLY;
#ifdef __cplusplus
#include <memory>
#include <string>
#include <vector>
namespace FileFilter {
static_assert(sizeof(FF_RULE)==672 && sizeof(FF_EVENT)==1352 && sizeof(FF_STATUS)==96,"Unexpected x64 ABI packing");
bool Validate(const FF_RULE&) noexcept;
bool Validate(const FF_POLICY&) noexcept;
bool Validate(const FF_REPLY&,std::size_t bytes) noexcept;
FF_RULE Rule(unsigned id,const std::wstring& existingFolder,unsigned operations=FF_ALL,
             unsigned action=FF_AUDIT,unsigned processId=0);
std::wstring Folder(const FF_RULE&);
std::wstring Path(const FF_EVENT&,bool destination=false);
std::wstring Operations(unsigned mask);
class Session {
    struct State;
    std::unique_ptr<State> state_;
public:
    Session();
    ~Session();
    Session(const Session&)=delete;
    Session& operator=(const Session&)=delete;
    void Start(const std::vector<FF_RULE>&);
    void SetRules(const std::vector<FF_RULE>&); // <=32; atomic replacement.
    std::vector<FF_EVENT> Read();               // Nonblocking; <=32 completions.
    FF_STATUS Status() const;
    void Stop() noexcept;                      // Closes owner port and clears policy.
};
}
#endif
#endif
