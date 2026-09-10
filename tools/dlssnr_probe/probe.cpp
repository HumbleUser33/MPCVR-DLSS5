// Standalone probe for the DLSS 5 Neural Rendering snippet (NGX feature 18).
//
// Answers, in isolation from MPC Video Renderer:
//   1. does the caller-validation check fire, and does the nvngx.dll shim clear it?
//   2. is our hand-rolled NVSDK_NGX_Parameter vtable laid out correctly?
//   3. does Init_Ext succeed on this GPU, and with which argument order?
//   4. does CreateFeature(18) succeed -- i.e. does the GPU architecture gate pass?
//   5. does EvaluateFeature actually write to the output texture?
//
// Everything is printed, including the snippet's own log lines, which is where
// "Unsupported GPU architecture 0x%x, minimum required 0x%x" would appear.

#include <windows.h>
#include <d3d11_1.h>
#include <d3d12.h>
#include <dxgi1_2.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <map>
#include "../../external/minhook/include/MinHook.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

struct ID3D12Resource; // never dereferenced here, only needs to name a type

// ---------------------------------------------------------------- NGX ABI

typedef uint32_t NVSDK_NGX_Result;

static const NVSDK_NGX_Result NGX_Success            = 0x1;
static const NVSDK_NGX_Result NGX_Fail               = 0xBAD00000;
static const NVSDK_NGX_Result NGX_FAIL_PlatformError = 0xBAD00002;

#define NGX_SUCCEED(r) (((r) & 0xFFF00000) != 0xBAD00000)
#define NGX_FAILED(r)  (((r) & 0xFFF00000) == 0xBAD00000)

static const uint32_t NGX_FEATURE_DLSSNR = 18;

struct NVSDK_NGX_Handle { unsigned int Id; };

struct NVSDK_NGX_PathListInfo {
    const wchar_t* const* Path;
    unsigned int Length;
};

enum NVSDK_NGX_Logging_Level : uint32_t {
    NGX_LOG_OFF = 0, NGX_LOG_ON = 1, NGX_LOG_VERBOSE = 2
};

typedef void(__cdecl* NVSDK_NGX_AppLogCallback)(const char* message,
                                                NVSDK_NGX_Logging_Level level,
                                                uint32_t sourceComponent);

struct NVSDK_NGX_LoggingInfo {
    NVSDK_NGX_AppLogCallback LoggingCallback;
    NVSDK_NGX_Logging_Level  MinimumLoggingLevel;
    bool                     DisableOtherLoggingSinks;
};

struct NVSDK_NGX_FeatureCommonInfo_Internal;

struct NVSDK_NGX_FeatureCommonInfo {
    NVSDK_NGX_PathListInfo                PathListInfo;
    NVSDK_NGX_FeatureCommonInfo_Internal* InternalData;
    NVSDK_NGX_LoggingInfo                 LoggingInfo;
};

// --- feature discovery: reports the minimum HW arch / OS without needing Init
enum NVSDK_NGX_Application_Identifier_Type : uint32_t {
    NGX_AppId_Application = 0, NGX_AppId_Project = 1
};
struct NVSDK_NGX_ProjectIdDescription {
    const char* ProjectId; uint32_t EngineType; const char* EngineVersion;
};
struct NVSDK_NGX_Application_Identifier {
    NVSDK_NGX_Application_Identifier_Type IdentifierType;
    union { NVSDK_NGX_ProjectIdDescription ProjectDesc; unsigned long long ApplicationId; } v;
};
struct NVSDK_NGX_FeatureDiscoveryInfo {
    uint32_t                            SDKVersion;
    uint32_t                            FeatureID;
    NVSDK_NGX_Application_Identifier    Identifier;
    const wchar_t*                      ApplicationDataPath;
    const NVSDK_NGX_FeatureCommonInfo*  FeatureInfo;
};
struct NVSDK_NGX_FeatureRequirement {
    uint32_t FeatureSupported;      // bitfield, 0 == supported
    unsigned int MinHWArchitecture; // NV_GPU_ARCHITECTURE_ID
    char MinOSVersion[255];
};

// Exactly the SDK's declaration order. No virtual destructor -- one would take
// slot 0 under MSVC and shift every entry.
class NVSDK_NGX_Parameter
{
public:
    virtual void Set(const char* n, unsigned long long v) = 0;   //  0
    virtual void Set(const char* n, float v) = 0;                //  1
    virtual void Set(const char* n, double v) = 0;               //  2
    virtual void Set(const char* n, unsigned int v) = 0;         //  3
    virtual void Set(const char* n, int v) = 0;                  //  4
    virtual void Set(const char* n, ID3D11Resource* v) = 0;      //  5
    virtual void Set(const char* n, ID3D12Resource* v) = 0;      //  6
    virtual void Set(const char* n, void* v) = 0;                //  7
    virtual NVSDK_NGX_Result Get(const char* n, unsigned long long* o) const = 0; //  8
    virtual NVSDK_NGX_Result Get(const char* n, float* o) const = 0;              //  9
    virtual NVSDK_NGX_Result Get(const char* n, double* o) const = 0;             // 10
    virtual NVSDK_NGX_Result Get(const char* n, unsigned int* o) const = 0;       // 11
    virtual NVSDK_NGX_Result Get(const char* n, int* o) const = 0;                // 12
    virtual NVSDK_NGX_Result Get(const char* n, ID3D11Resource** o) const = 0;    // 13
    virtual NVSDK_NGX_Result Get(const char* n, ID3D12Resource** o) const = 0;    // 14
    virtual NVSDK_NGX_Result Get(const char* n, void** o) const = 0;              // 15
    virtual void Reset() = 0;                                                     // 16
};

// ------------------------------------------------------------- reporting

static const char* NgxResultStr(NVSDK_NGX_Result r)
{
    switch (r) {
    case 0x1:        return "Success";
    case 0xBAD00000: return "Fail (generic)";
    case 0xBAD00001: return "FAIL_FeatureNotSupported";
    case 0xBAD00002: return "FAIL_PlatformError";
    case 0xBAD00003: return "FAIL_FeatureAlreadyExists";
    case 0xBAD00004: return "FAIL_FeatureNotFound";
    case 0xBAD00005: return "FAIL_InvalidParameter";
    case 0xBAD00006: return "FAIL_ScratchBufferTooSmall";
    case 0xBAD00007: return "FAIL_NotInitialized";
    case 0xBAD00008: return "FAIL_UnsupportedInputFormat";
    case 0xBAD00009: return "FAIL_RWFlagMissing (needs UAV)";
    case 0xBAD0000A: return "FAIL_MissingInput";
    case 0xBAD0000B: return "FAIL_UnableToInitializeFeature";
    case 0xBAD0000C: return "FAIL_OutOfDate";
    case 0xBAD0000D: return "FAIL_OutOfGPUMemory";
    case 0xBAD0000E: return "FAIL_UnsupportedFormat";
    case 0xBAD0000F: return "FAIL_UnableToWriteToAppDataPath";
    case 0xBAD00010: return "FAIL_UnsupportedParameter";
    case 0xBAD00011: return "FAIL_Denied";
    case 0xBAD00012: return "FAIL_NotImplemented";
    }
    return "<unknown>";
}

static void Step(const char* s) { printf("\n=== %s ===\n", s); }
static void Report(const char* what, NVSDK_NGX_Result r)
{
    printf("  %-40s 0x%08X  %s\n", what, r, NgxResultStr(r));
}

// ------------------------------------------- our NVSDK_NGX_Parameter impl
// Type-tolerant on purpose: a wrong vtable slot then yields a wrong *value*,
// never a bad write, so the failure is diagnosable instead of a crash.

class CParamStore final : public NVSDK_NGX_Parameter
{
public:
    enum class Kind : uint8_t { None, U64, F32, F64, U32, I32, Res11, Res12, Ptr };
    struct Slot { Kind kind = Kind::None; uint64_t u64 = 0; double f64 = 0; void* ptr = nullptr; };

    std::map<std::string, Slot> m;
    mutable std::vector<std::pair<std::string, int>> trace;
    bool tracing = false;

private:
    Slot& put(const char* n, int slot) {
        std::string key = n ? n : "<null>";
        if (tracing) trace.emplace_back(key, slot);
        return m[key];
    }
    const Slot* get(const char* n) const {
        auto it = m.find(n ? n : "<null>");
        return it == m.end() ? nullptr : &it->second;
    }
public:
    void Set(const char* n, unsigned long long v) override { auto& s = put(n,0); s.kind=Kind::U64; s.u64=v; s.f64=(double)v; }
    void Set(const char* n, float v)              override { auto& s = put(n,1); s.kind=Kind::F32; s.f64=v; s.u64=(uint64_t)v; }
    void Set(const char* n, double v)             override { auto& s = put(n,2); s.kind=Kind::F64; s.f64=v; s.u64=(uint64_t)v; }
    void Set(const char* n, unsigned int v)       override { auto& s = put(n,3); s.kind=Kind::U32; s.u64=v; s.f64=(double)v; }
    void Set(const char* n, int v)                override { auto& s = put(n,4); s.kind=Kind::I32; s.u64=(uint64_t)(int64_t)v; s.f64=(double)v; }
    void Set(const char* n, ID3D11Resource* v)    override { auto& s = put(n,5); s.kind=Kind::Res11; s.ptr=v; }
    void Set(const char* n, ID3D12Resource* v)    override { auto& s = put(n,6); s.kind=Kind::Res12; s.ptr=v; }
    void Set(const char* n, void* v)              override { auto& s = put(n,7); s.kind=Kind::Ptr;  s.ptr=v; }

    NVSDK_NGX_Result Get(const char* n, unsigned long long* o) const override
        { if(!o) return 0xBAD00005; auto v=get(n); if(!v) return 0xBAD00004; *o=v->u64; return NGX_Success; }
    NVSDK_NGX_Result Get(const char* n, float* o) const override
        { if(!o) return 0xBAD00005; auto v=get(n); if(!v) return 0xBAD00004; *o=(float)v->f64; return NGX_Success; }
    NVSDK_NGX_Result Get(const char* n, double* o) const override
        { if(!o) return 0xBAD00005; auto v=get(n); if(!v) return 0xBAD00004; *o=v->f64; return NGX_Success; }
    NVSDK_NGX_Result Get(const char* n, unsigned int* o) const override
        { if(!o) return 0xBAD00005; auto v=get(n); if(!v) return 0xBAD00004; *o=(unsigned)v->u64; return NGX_Success; }
    NVSDK_NGX_Result Get(const char* n, int* o) const override
        { if(!o) return 0xBAD00005; auto v=get(n); if(!v) return 0xBAD00004; *o=(int)v->u64; return NGX_Success; }
    NVSDK_NGX_Result Get(const char* n, ID3D11Resource** o) const override
        { if(!o) return 0xBAD00005; auto v=get(n); if(!v) return 0xBAD00004; *o=(ID3D11Resource*)v->ptr; return NGX_Success; }
    NVSDK_NGX_Result Get(const char* n, ID3D12Resource** o) const override
        { if(!o) return 0xBAD00005; auto v=get(n); if(!v) return 0xBAD00004; *o=(ID3D12Resource*)v->ptr; return NGX_Success; }
    NVSDK_NGX_Result Get(const char* n, void** o) const override
        { if(!o) return 0xBAD00005; auto v=get(n); if(!v) return 0xBAD00004; *o=v->ptr; return NGX_Success; }

    void Reset() override { m.clear(); }

    void Dump(const char* title) const {
        printf("  %s: %zu entries\n", title, m.size());
        int n = 0;
        for (auto& kv : m) {
            if (n++ >= 40) { printf("    ... (%zu more)\n", m.size() - 40); break; }
            const char* k = "?";
            switch (kv.second.kind) {
            case Kind::U64: k="u64"; break;  case Kind::F32: k="f32"; break;
            case Kind::F64: k="f64"; break;  case Kind::U32: k="u32"; break;
            case Kind::I32: k="i32"; break;  case Kind::Res11: k="d3d11"; break;
            case Kind::Res12: k="d3d12"; break; case Kind::Ptr: k="ptr"; break;
            default: break;
            }
            printf("    %-46s %-6s u64=%llu f=%.4f p=%p\n", kv.first.c_str(), k,
                   (unsigned long long)kv.second.u64, kv.second.f64, kv.second.ptr);
        }
    }
};

// ------------------------------------------------------ snippet entry points

typedef NVSDK_NGX_Result(__cdecl* PFN_InitVerFirst)(unsigned long long, const wchar_t*,
    ID3D11Device*, unsigned int, const void*);
typedef NVSDK_NGX_Result(__cdecl* PFN_Populate)(NVSDK_NGX_Parameter*);
typedef NVSDK_NGX_Result(__cdecl* PFN_Create)(ID3D11DeviceContext*, uint32_t,
    NVSDK_NGX_Parameter*, NVSDK_NGX_Handle**);
typedef NVSDK_NGX_Result(__cdecl* PFN_Eval)(ID3D11DeviceContext*, const NVSDK_NGX_Handle*,
    const NVSDK_NGX_Parameter*, void*);
typedef NVSDK_NGX_Result(__cdecl* PFN_Release)(NVSDK_NGX_Handle*);
typedef NVSDK_NGX_Result(__cdecl* PFN_Shutdown1)(ID3D11Device*);
typedef unsigned int(__cdecl* PFN_GetU32)(void);

// shim trampolines
typedef NVSDK_NGX_Result(__cdecl* PFN_ShimInit)(void*, unsigned long long, const wchar_t*,
    void*, unsigned int, const void*);
typedef NVSDK_NGX_Result(__cdecl* PFN_ShimInitFci)(void*, unsigned long long, const wchar_t*,
    void*, const void*, unsigned int);
typedef NVSDK_NGX_Result(__cdecl* PFN_ShimPopulate)(void*, void*);
typedef NVSDK_NGX_Result(__cdecl* PFN_ShimCreate)(void*, void*, unsigned int, void*, void**);
typedef NVSDK_NGX_Result(__cdecl* PFN_ShimEval)(void*, void*, const void*, const void*, void*);
typedef NVSDK_NGX_Result(__cdecl* PFN_ShimRelease)(void*, void*);
typedef NVSDK_NGX_Result(__cdecl* PFN_ShimShutdown1)(void*, void*);

// ------------------------------------------------------------------- NVAPI
// The snippet decides whether the GPU is supported by asking NVAPI, not by
// looking at anything we pass it (log strings: "SetGPUArch:: NvAPI_GPU_GetArchInfo
// failed with error: %d", "m_gpuArch = 0x%x"). These are the public dispatch
// ids used to reach those entry points.

typedef void*    (__cdecl* PFN_nvapi_QueryInterface)(unsigned int id);
typedef int      (__cdecl* PFN_NvAPI_Initialize)(void);
typedef int      (__cdecl* PFN_NvAPI_EnumPhysicalGPUs)(void** handles, int* count);

struct NV_GPU_ARCH_INFO {
    unsigned int version;
    unsigned int architecture;    // NV_GPU_ARCHITECTURE_ID
    unsigned int implementation;
    unsigned int revision;
};
typedef int (__cdecl* PFN_NvAPI_GPU_GetArchInfo)(void* hGpu, NV_GPU_ARCH_INFO* info);

static const unsigned int NVAPI_ID_Initialize       = 0x0150E828;
static const unsigned int NVAPI_ID_EnumPhysicalGPUs = 0xE5AC921F;
static const unsigned int NVAPI_ID_GPU_GetArchInfo  = 0xD8265D24;

// MAKE_NVAPI_VERSION(NV_GPU_ARCH_INFO_V2, 2) == sizeof | (ver << 16)
static const unsigned int NV_GPU_ARCH_INFO_VER_2 = sizeof(NV_GPU_ARCH_INFO) | (2u << 16);
static const unsigned int NV_GPU_ARCH_INFO_VER_1 = sizeof(NV_GPU_ARCH_INFO) | (1u << 16);

static const char* ArchName(unsigned int a)
{
    switch (a) {
    case 0x110: return "Maxwell GM000";
    case 0x120: return "Maxwell GM200";
    case 0x130: return "Pascal GP100";
    case 0x140: return "Volta GV100";
    case 0x150: return "Volta GV110";
    case 0x160: return "Turing TU100 (RTX 20)";
    case 0x170: return "Ampere GA100 (RTX 30)";
    case 0x180: return "Ampere GA10x?";
    case 0x190: return "Ada AD100 (RTX 40)";
    case 0x1A0: return "Blackwell GB100?";
    case 0x1B0: return "Blackwell GB200?";
    }
    return "unknown";
}

// ---------------------------------------------------- architecture override
// The snippet resolves NvAPI_GPU_GetArchInfo through nvapi_QueryInterface and
// caches the pointer. Hooking the resolved function itself therefore catches
// the call whenever it happens. The override is armed only around our own NGX
// calls, so nothing else in the process sees a different GPU.

static PFN_NvAPI_GPU_GetArchInfo s_pRealArchInfo = nullptr;
static unsigned int s_SpoofArch = 0;   // 0 = pass through untouched
static int          s_SpoofHits = 0;

static int __cdecl ArchInfoDetour(void* hGpu, NV_GPU_ARCH_INFO* info)
{
    const int r = s_pRealArchInfo(hGpu, info);
    if (r == 0 && info && s_SpoofArch) {
        info->architecture = s_SpoofArch;
        s_SpoofHits++;
    }
    return r;
}

// Rather than guess which NVAPI entry point the snippet consults, log every id
// it resolves. nvapi_QueryInterface is the single door all of them come through.
static PFN_nvapi_QueryInterface s_pRealQI = nullptr;
struct QIRecord { unsigned int id; bool resolved; const char* phase; };
static std::vector<QIRecord> s_QIIds;
static bool s_QILogging = false;
static const char* s_QIPhase = "startup";

static const char* NvApiIdName(unsigned int id)
{
    switch (id) {
    case 0x0150E828: return "NvAPI_Initialize";
    case 0xD22BDD7E: return "NvAPI_Unload";
    case 0xE5AC921F: return "NvAPI_EnumPhysicalGPUs";
    case 0xD8265D24: return "NvAPI_GPU_GetArchInfo";
    case 0xCEEE8E9F: return "NvAPI_GPU_GetFullName";
    case 0x01053FA5: return "NvAPI_GetInterfaceVersionString";
    case 0xF951A4D1: return "NvAPI_SYS_GetDriverAndBranchVersion";
    case 0xADD604D1: return "NvAPI_GetLogicalGPUFromPhysicalGPU?";
    case 0x842B066E: return "NvAPI_GPU_GetLogicalGpuInfo?";
    case 0x2DDFB66E: return "NvAPI_GPU_GetPCIIdentifiers?";
    case 0x5F608315: return "NvAPI_GPU_GetSystemType?";
    case 0x7F9B368: return "NvAPI_GPU_GetBusId?";
    }
    return "";
}

static void* __cdecl QueryInterfaceDetour(unsigned int id)
{
    void* p = s_pRealQI(id);
    if (s_QILogging) {
        // A null return means this driver does not provide that entry point --
        // which is exactly what "the driver is too old" would look like.
        s_QIIds.push_back({ id, p != nullptr, s_QIPhase });
    }
    return p;
}

// ------------------------------------------------------------------ globals

static std::vector<std::string> g_log;

static void __cdecl NgxLog(const char* msg, NVSDK_NGX_Logging_Level lvl, uint32_t comp)
{
    if (!msg) return;
    std::string s(msg);
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
    if (s.empty()) return;
    g_log.push_back(s);
    printf("    [ngx L%u C%u] %s\n", (unsigned)lvl, comp, s.c_str());
}

// ------------------------------------------------------------------ helpers

static std::wstring DirOf(const std::wstring& p)
{
    size_t s = p.find_last_of(L"\\/");
    return s == std::wstring::npos ? L"." : p.substr(0, s);
}

static std::wstring ExeDir()
{
    wchar_t buf[MAX_PATH]{};
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return DirOf(buf);
}

static bool Exists(const std::wstring& p)
{
    return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES;
}

static std::wstring FindSnippet(const wchar_t* override_)
{
    if (override_ && *override_) return override_;
    std::wstring d = ExeDir();
    const wchar_t* rel[] = { L"\\nvngx_dlssnr.dll", L"\\..\\nvngx_dlssnr.dll",
                             L"\\..\\..\\nvngx_dlssnr.dll", L"\\..\\..\\..\\nvngx_dlssnr.dll" };
    for (auto r : rel) { std::wstring c = d + r; if (Exists(c)) return c; }
    return L"nvngx_dlssnr.dll";
}

// The driver's NGX core. The snippet expects to be driven by it: it registers
// callbacks the core is supposed to poll, and owns the real parameter objects.
static HMODULE LoadCore()
{
    if (HMODULE h = LoadLibraryW(L"_nvngx.dll")) return h;
    WIN32_FIND_DATAW fd{};
    const std::wstring root = L"C:\\Windows\\System32\\DriverStore\\FileRepository\\";
    HANDLE hf = FindFirstFileW((root + L"nv_disp*").c_str(), &fd);
    if (hf == INVALID_HANDLE_VALUE) return nullptr;
    HMODULE res = nullptr;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        std::wstring cand = root + fd.cFileName + L"\\_nvngx.dll";
        if (!Exists(cand)) continue;
        wprintf(L"  core: %s\n", cand.c_str());
        res = LoadLibraryExW(cand.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        if (res) break;
    } while (FindNextFileW(hf, &fd));
    FindClose(hf);
    return res;
}

static ID3D11Texture2D* MakeTex(ID3D11Device* dev, UINT w, UINT h)
{
    D3D11_TEXTURE2D_DESC d{};
    d.Width = w; d.Height = h; d.MipLevels = 1; d.ArraySize = 1;
    d.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    d.SampleDesc = { 1, 0 };
    d.Usage = D3D11_USAGE_DEFAULT;
    d.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET |
                  D3D11_BIND_UNORDERED_ACCESS;
    ID3D11Texture2D* t = nullptr;
    HRESULT hr = dev->CreateTexture2D(&d, nullptr, &t);
    if (FAILED(hr)) printf("  CreateTexture2D failed 0x%08X\n", (unsigned)hr);
    return t;
}

// ------------------------------------------------- full D3D12 feature run
// Init succeeding only proves the backend is alive. The architecture check
// that carries the "Unsupported GPU architecture" message lives in
// NGXCG2R::Init, which runs at CreateFeature time -- so this is the test that
// says whether the network really runs on this hardware.

static ID3D12Resource* MakeTex12(ID3D12Device* dev, UINT w, UINT h, D3D12_RESOURCE_STATES state)
{
    D3D12_HEAP_PROPERTIES hp{}; hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width = w; rd.Height = h; rd.DepthOrArraySize = 1; rd.MipLevels = 1;
    rd.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    rd.SampleDesc = { 1, 0 };
    rd.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    ID3D12Resource* res = nullptr;
    HRESULT hr = dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, state,
                                              nullptr, __uuidof(ID3D12Resource), (void**)&res);
    if (FAILED(hr)) printf("  CreateCommittedResource failed 0x%08X\n", (unsigned)hr);
    return res;
}

template <typename TProc>
static void RunD3D12Feature(ID3D12Device* dev, TProc S,
                            PFN_ShimCreate shCreate, PFN_ShimEval shEval,
                            PFN_ShimRelease shRelease, bool useShim, UINT w, UINT h)
{
    Step("D3D12 CreateFeature + Evaluate");

    void* pCreate12  = S("NVSDK_NGX_D3D12_CreateFeature");
    void* pEval12    = S("NVSDK_NGX_D3D12_EvaluateFeature");
    void* pRelease12 = S("NVSDK_NGX_D3D12_ReleaseFeature");
    void* pPop12     = S("NVSDK_NGX_D3D12_PopulateParameters_Impl");
    if (!pCreate12 || !pEval12) { printf("  missing D3D12 exports\n"); return; }

    ID3D12CommandQueue* queue = nullptr;
    ID3D12CommandAllocator* alloc = nullptr;
    ID3D12GraphicsCommandList* list = nullptr;
    ID3D12Fence* fence = nullptr;
    D3D12_COMMAND_QUEUE_DESC qd{}; qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    if (FAILED(dev->CreateCommandQueue(&qd, __uuidof(ID3D12CommandQueue), (void**)&queue)) ||
        FAILED(dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator), (void**)&alloc)) ||
        FAILED(dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc, nullptr, __uuidof(ID3D12GraphicsCommandList), (void**)&list)) ||
        FAILED(dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), (void**)&fence))) {
        printf("  could not build the D3D12 command objects\n");
        return;
    }
    UINT64 fenceVal = 0;
    HANDLE ev = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    auto Submit = [&](const char* what) {
        list->Close();
        ID3D12CommandList* lists[] = { list };
        queue->ExecuteCommandLists(1, lists);
        queue->Signal(fence, ++fenceVal);
        if (fence->GetCompletedValue() < fenceVal) {
            fence->SetEventOnCompletion(fenceVal, ev);
            WaitForSingleObject(ev, 10000);
        }
        printf("  %s submitted and completed\n", what);
        alloc->Reset();
        list->Reset(alloc, nullptr);
    };

    ID3D12Resource* in12  = MakeTex12(dev, w, h, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    ID3D12Resource* out12 = MakeTex12(dev, w, h, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    if (!in12 || !out12) { printf("  could not create the textures\n"); return; }
    printf("  textures %ux%u RGBA16F created\n", w, h);

    CParamStore p12;
    (void)pPop12;   // the D3D11 run already proved the vtable; not needed here

    p12.Set("DLSSNR.Width",  (unsigned int)w);
    p12.Set("DLSSNR.Height", (unsigned int)h);
    p12.Set("DLSSNR.Enabled", (int)1);
    p12.Set("DLSSNR.Style", (int)0);
    p12.Set("DLSSNR.Hint.Render.Preset", (int)0);
    p12.Set("DLSSNR.ScalingRatio", 1.0f);
    p12.Set("CreationNodeMask", (unsigned int)1);
    p12.Set("VisibilityNodeMask", (unsigned int)1);

    NVSDK_NGX_Handle* feat = nullptr;
    typedef NVSDK_NGX_Result(__cdecl* PFN_Create12)(ID3D12GraphicsCommandList*, uint32_t,
        NVSDK_NGX_Parameter*, NVSDK_NGX_Handle**);
    NVSDK_NGX_Result rc = (useShim && shCreate)
        ? shCreate(pCreate12, list, NGX_FEATURE_DLSSNR, &p12, (void**)&feat)
        : ((PFN_Create12)pCreate12)(list, NGX_FEATURE_DLSSNR, &p12, &feat);
    Report("D3D12 CreateFeature(18)", rc);
    Submit("CreateFeature work");

    if (NGX_SUCCEED(rc) && feat) {
        p12.Set("DLSSNR.Color",      (ID3D12Resource*)in12);
        p12.Set("DLSSNR.Output",     (ID3D12Resource*)out12);
        p12.Set("DLSSNR.Backbuffer", (ID3D12Resource*)out12);
        p12.Set("DLSSNR.Reset", (int)1);
        p12.Set("DLSSNR.UICorrection", (int)0);
        p12.Set("DLSSNR.DepthInverted", (int)1);
        p12.Set("DLSSNR.UseAutoMask", (int)1);
        p12.Set("DLSSNR.MVecScaleX", 1.0f);
        p12.Set("DLSSNR.MVecScaleY", 1.0f);
        p12.Set("DLSSNR.Intensity", 1.0f);
        p12.Set("DLSSNR.LocalToneStrength", 1.0f);
        p12.Set("DLSSNR.LocalStructureStrength", 1.0f);
        p12.Set("DLSSNR.SkinStructureStrength", 1.0f);
        static const char* const pre[] = { "DLSSNR.ColorSubrect", "DLSSNR.OutputSubrect", "DLSSNR.BackbufferSubrect" };
        for (const char* q : pre) {
            char n[64];
            sprintf_s(n, "%sBaseX", q);  p12.Set(n, (unsigned int)0);
            sprintf_s(n, "%sBaseY", q);  p12.Set(n, (unsigned int)0);
            sprintf_s(n, "%sWidth", q);  p12.Set(n, (unsigned int)w);
            sprintf_s(n, "%sHeight", q); p12.Set(n, (unsigned int)h);
        }

        typedef NVSDK_NGX_Result(__cdecl* PFN_Eval12)(ID3D12GraphicsCommandList*,
            const NVSDK_NGX_Handle*, const NVSDK_NGX_Parameter*, void*);
        for (int frame = 1; frame <= 2; frame++) {
            NVSDK_NGX_Result re = (useShim && shEval)
                ? shEval(pEval12, list, feat, &p12, nullptr)
                : ((PFN_Eval12)pEval12)(list, feat, &p12, nullptr);
            char lbl[48]; sprintf_s(lbl, "D3D12 Evaluate frame %d", frame);
            Report(lbl, re);
            Submit("Evaluate work");
            p12.Set("DLSSNR.Reset", (int)0);
            if (NGX_FAILED(re)) break;
        }

        if (pRelease12) {
            typedef NVSDK_NGX_Result(__cdecl* PFN_Rel12)(NVSDK_NGX_Handle*);
            NVSDK_NGX_Result rr = (useShim && shRelease)
                ? shRelease(pRelease12, feat)
                : ((PFN_Rel12)pRelease12)(feat);
            Report("D3D12 ReleaseFeature", rr);
        }
    }

    if (in12) in12->Release();
    if (out12) out12->Release();
    if (ev) CloseHandle(ev);
    if (fence) fence->Release();
    if (list) list->Release();
    if (alloc) alloc->Release();
    if (queue) queue->Release();
}

// ------------------------------------------------- D3D11 <-> D3D12 sharing
// MPC Video Renderer stays a D3D11 application; only the snippet needs D3D12.
// So a texture has to be visible to both devices. Four ways to arrange that --
// this reports which ones this driver actually accepts, instead of guessing.

static void TestSharing(ID3D11Device* dev11, ID3D12Device* dev12, UINT w, UINT h)
{
    Step("D3D11 <-> D3D12 texture sharing");

    ID3D11Device1* dev11_1 = nullptr;
    dev11->QueryInterface(__uuidof(ID3D11Device1), (void**)&dev11_1);
    printf("  ID3D11Device1: %s\n", dev11_1 ? "yes" : "NO");

    // --- direction 1: created on D3D11, opened on D3D12 --------------------
    auto try11to12 = [&](const char* label, UINT misc) {
        D3D11_TEXTURE2D_DESC d{};
        d.Width = w; d.Height = h; d.MipLevels = 1; d.ArraySize = 1;
        d.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        d.SampleDesc = { 1, 0 };
        d.Usage = D3D11_USAGE_DEFAULT;
        d.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET |
                      D3D11_BIND_UNORDERED_ACCESS;
        d.MiscFlags = misc;

        ID3D11Texture2D* t11 = nullptr;
        HRESULT hr = dev11->CreateTexture2D(&d, nullptr, &t11);
        if (FAILED(hr)) { printf("  %-34s CreateTexture2D 0x%08X\n", label, (unsigned)hr); return; }

        IDXGIResource1* res1 = nullptr;
        hr = t11->QueryInterface(__uuidof(IDXGIResource1), (void**)&res1);
        if (FAILED(hr)) { printf("  %-34s no IDXGIResource1 0x%08X\n", label, (unsigned)hr); t11->Release(); return; }

        HANDLE sh = nullptr;
        hr = res1->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, nullptr, &sh);
        if (FAILED(hr) || !sh) { printf("  %-34s CreateSharedHandle 0x%08X\n", label, (unsigned)hr); res1->Release(); t11->Release(); return; }

        ID3D12Resource* r12 = nullptr;
        hr = dev12->OpenSharedHandle(sh, __uuidof(ID3D12Resource), (void**)&r12);
        printf("  %-34s %s (open 0x%08X)\n", label,
               SUCCEEDED(hr) ? "WORKS" : "fails at OpenSharedHandle", (unsigned)hr);
        if (r12) {
            D3D12_RESOURCE_DESC rd = r12->GetDesc();
            printf("       -> D3D12 sees flags 0x%X%s\n", rd.Flags,
                   (rd.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) ? " (UAV ok)" : " (NO UAV -- NGX needs it)");
            r12->Release();
        }
        CloseHandle(sh); res1->Release(); t11->Release();
    };

    try11to12("11->12  SHARED|NTHANDLE",
              D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE);
    try11to12("11->12  KEYEDMUTEX|NTHANDLE",
              D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX | D3D11_RESOURCE_MISC_SHARED_NTHANDLE);
    try11to12("11->12  SHARED only",
              D3D11_RESOURCE_MISC_SHARED);

    // --- direction 2: created on D3D12, opened on D3D11 --------------------
    auto try12to11 = [&](const char* label, D3D12_RESOURCE_FLAGS flags) {
        D3D12_HEAP_PROPERTIES hp{}; hp.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC rd{};
        rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        rd.Width = w; rd.Height = h; rd.DepthOrArraySize = 1; rd.MipLevels = 1;
        rd.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        rd.SampleDesc = { 1, 0 };
        rd.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        rd.Flags = flags;

        ID3D12Resource* r12 = nullptr;
        HRESULT hr = dev12->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_SHARED, &rd,
                        D3D12_RESOURCE_STATE_COMMON, nullptr, __uuidof(ID3D12Resource), (void**)&r12);
        if (FAILED(hr)) { printf("  %-34s CreateCommittedResource 0x%08X\n", label, (unsigned)hr); return; }

        HANDLE sh = nullptr;
        hr = dev12->CreateSharedHandle(r12, nullptr, GENERIC_ALL, nullptr, &sh);
        if (FAILED(hr) || !sh) { printf("  %-34s CreateSharedHandle 0x%08X\n", label, (unsigned)hr); r12->Release(); return; }

        ID3D11Texture2D* t11 = nullptr;
        hr = dev11_1 ? dev11_1->OpenSharedResource1(sh, __uuidof(ID3D11Texture2D), (void**)&t11) : E_NOINTERFACE;
        printf("  %-34s %s (open 0x%08X)\n", label,
               SUCCEEDED(hr) ? "WORKS" : "fails at OpenSharedResource1", (unsigned)hr);
        if (t11) {
            D3D11_TEXTURE2D_DESC d{};
            t11->GetDesc(&d);
            printf("       -> D3D11 sees bind 0x%X%s%s\n", d.BindFlags,
                   (d.BindFlags & D3D11_BIND_SHADER_RESOURCE) ? " (SRV ok)" : " (NO SRV)",
                   (d.BindFlags & D3D11_BIND_RENDER_TARGET) ? " (RTV ok)" : " (no RTV)");
            t11->Release();
        }
        CloseHandle(sh); r12->Release();
    };

    try12to11("12->11  UAV",
              D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    try12to11("12->11  UAV|SIMULTANEOUS_ACCESS",
              (D3D12_RESOURCE_FLAGS)(D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS |
                                     D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS));
    try12to11("12->11  UAV|RT|SIMULTANEOUS",
              (D3D12_RESOURCE_FLAGS)(D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS |
                                     D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET |
                                     D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS));

    printf("\n  What matters: a line that says WORKS, whose D3D12 side keeps UAV\n"
           "  (NGX writes its output through one) and whose D3D11 side keeps SRV\n"
           "  and RTV (the renderer blits into it and samples out of it).\n");

    if (dev11_1) dev11_1->Release();
}

// ---------------------------------------------------------------------- main

int wmain(int argc, wchar_t** argv)
{
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("DLSS 5 Neural Rendering probe -- NGX feature 18, D3D11 backend\n");
    printf("==============================================================\n");

    const wchar_t* dllOverride = nullptr;
    UINT W = 1920, H = 1080;
    bool noShim = false;
    unsigned int spoofArg = 0;   // explicit architecture id (hex)
    bool spoofAuto = false;      // take it from GetFeatureRequirements
    for (int i = 1; i < argc; i++) {
        if (!wcscmp(argv[i], L"--dll") && i + 1 < argc)  dllOverride = argv[++i];
        else if (!wcscmp(argv[i], L"--w") && i + 1 < argc) W = _wtoi(argv[++i]);
        else if (!wcscmp(argv[i], L"--h") && i + 1 < argc) H = _wtoi(argv[++i]);
        else if (!wcscmp(argv[i], L"--no-shim")) noShim = true;
        else if (!wcscmp(argv[i], L"--spoof") && i + 1 < argc) spoofArg = wcstoul(argv[++i], nullptr, 16);
        else if (!wcscmp(argv[i], L"--spoof-auto")) spoofAuto = true;
    }

    // ---- make the snippet talk --------------------------------------------
    // Its LoggingInfo never reaches it: the snippet ABI's 5th Init argument is
    // a Parameter*, not a FeatureCommonInfo, so the callback we pass is simply
    // never seen. These env knobs are the only channel left, and they must be
    // set before any NGX library is loaded.
    Step("NGX logging");
    wchar_t logDir[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, logDir);
    wcscat_s(logDir, L"dlssnr_log\\");
    CreateDirectoryW(logDir, nullptr);
    SetEnvironmentVariableW(L"__NGX_ENABLE_OVERRIDE_LOG_PATH", L"1");
    SetEnvironmentVariableW(L"__NGX_LOG_PATH_OVERRIDE", logDir);
    SetEnvironmentVariableW(L"__NGX_LOG_LEVEL", L"2");
    wprintf(L"  log directory: %s\n", logDir);
    printf("  __NGX_ENABLE_OVERRIDE_LOG_PATH=1  __NGX_LOG_LEVEL=2\n");

    // ---- D3D11 device, shaped like CDX11VideoProcessor::SetDevice ---------
    Step("D3D11 device");
    D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0,
                                   D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0 };
    ID3D11Device* dev0 = nullptr; ID3D11DeviceContext* ctx0 = nullptr;
    D3D_FEATURE_LEVEL fl{};
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
                                   levels, 4, D3D11_SDK_VERSION, &dev0, &fl, &ctx0);
    if (FAILED(hr)) { printf("  D3D11CreateDevice failed 0x%08X\n", (unsigned)hr); return 1; }

    ID3D11Device1* dev = nullptr; ID3D11DeviceContext1* ctx = nullptr;
    dev0->QueryInterface(__uuidof(ID3D11Device1), (void**)&dev);
    if (dev) dev->GetImmediateContext1(&ctx);
    if (!dev || !ctx) { printf("  no ID3D11Device1/Context1\n"); return 1; }
    printf("  feature level 0x%X\n", fl);

    { // mirror the renderer: it sets this for d3d11 subtitles
        ID3D10Multithread* mt = nullptr;
        if (SUCCEEDED(ctx->QueryInterface(__uuidof(ID3D10Multithread), (void**)&mt)) && mt) {
            mt->SetMultithreadProtected(TRUE); mt->Release();
        }
    }

    IDXGIDevice* dxgiDev = nullptr; IDXGIAdapter* ad = nullptr;
    if (SUCCEEDED(dev->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgiDev)) &&
        SUCCEEDED(dxgiDev->GetAdapter(&ad))) {
        DXGI_ADAPTER_DESC d{}; ad->GetDesc(&d);
        wprintf(L"  adapter: %s\n", d.Description);
        printf("  vendor 0x%04X device 0x%04X vram %llu MB\n", d.VendorId, d.DeviceId,
               (unsigned long long)(d.DedicatedVideoMemory >> 20));
        ad->Release();
    }
    if (dxgiDev) dxgiDev->Release();

    D3D11_FEATURE_DATA_FORMAT_SUPPORT2 fs2{ DXGI_FORMAT_R16G16B16A16_FLOAT, 0 };
    dev->CheckFeatureSupport(D3D11_FEATURE_FORMAT_SUPPORT2, &fs2, sizeof(fs2));
    printf("  RGBA16F UAV typed store: %s\n",
           (fs2.OutFormatSupport2 & D3D11_FORMAT_SUPPORT2_UAV_TYPED_STORE) ? "yes" : "NO");

    // ---- what does NVAPI say this GPU is? ---------------------------------
    // Confirms the dispatch id and the struct layout before anything relies on
    // them. Expected: 0x160 Turing, 0x170 Ampere, 0x190 Ada, 0x1B0 Blackwell.
    Step("NVAPI architecture");
    {
        HMODULE hNvApi = LoadLibraryW(L"nvapi64.dll");
        printf("  nvapi64.dll: %s\n", hNvApi ? "loaded" : "NOT FOUND");
        if (hNvApi) {
            auto QI = (PFN_nvapi_QueryInterface)GetProcAddress(hNvApi, "nvapi_QueryInterface");
            printf("  nvapi_QueryInterface: %p\n", (void*)QI);
            if (QI) {
                auto pInit = (PFN_NvAPI_Initialize)      QI(NVAPI_ID_Initialize);
                auto pEnum = (PFN_NvAPI_EnumPhysicalGPUs)QI(NVAPI_ID_EnumPhysicalGPUs);
                auto pArch = (PFN_NvAPI_GPU_GetArchInfo) QI(NVAPI_ID_GPU_GetArchInfo);
                printf("  Initialize=%p EnumPhysicalGPUs=%p GetArchInfo=%p\n",
                       (void*)pInit, (void*)pEnum, (void*)pArch);
                if (pInit && pEnum && pArch) {
                    printf("  NvAPI_Initialize -> %d\n", pInit());
                    void* gpus[64] = {};
                    int count = 0;
                    const int re = pEnum(gpus, &count);
                    printf("  EnumPhysicalGPUs -> %d, count %d\n", re, count);
                    for (int i = 0; i < count; i++) {
                        NV_GPU_ARCH_INFO ai = {};
                        ai.version = NV_GPU_ARCH_INFO_VER_2;
                        int ra = pArch(gpus[i], &ai);
                        if (ra != 0) {
                            ai = {};
                            ai.version = NV_GPU_ARCH_INFO_VER_1;
                            ra = pArch(gpus[i], &ai);
                        }
                        printf("  GPU %d: status %d  arch 0x%X (%s)  impl 0x%X  rev 0x%X\n",
                               i, ra, ai.architecture, ArchName(ai.architecture),
                               ai.implementation, ai.revision);
                    }
                }
            }
        }
    }

    // ---- architecture override --------------------------------------------
    // Installed before the snippet is loaded so it is in place whenever the
    // snippet resolves and calls NvAPI_GPU_GetArchInfo.
    if (spoofArg || spoofAuto) {
        Step("Architecture override");
        HMODULE hNv = LoadLibraryW(L"nvapi64.dll");
        auto QI = hNv ? (PFN_nvapi_QueryInterface)GetProcAddress(hNv, "nvapi_QueryInterface") : nullptr;
        auto pArch = QI ? (PFN_NvAPI_GPU_GetArchInfo)QI(NVAPI_ID_GPU_GetArchInfo) : nullptr;
        const MH_STATUS mh = MH_Initialize();
        if (!pArch) {
            printf("  could not resolve NvAPI_GPU_GetArchInfo\n");
        } else if (mh != MH_OK && mh != MH_ERROR_ALREADY_INITIALIZED) {
            printf("  MH_Initialize failed (%d)\n", (int)mh);
        } else if (MH_CreateHook(pArch, &ArchInfoDetour, (void**)&s_pRealArchInfo) != MH_OK) {
            printf("  MH_CreateHook failed\n");
        } else if (MH_EnableHook(pArch) != MH_OK) {
            printf("  MH_EnableHook failed\n");
        } else {
            printf("  hook installed on NvAPI_GPU_GetArchInfo (%p)\n", (void*)pArch);
        }

        // Log which NVAPI entry points the snippet actually resolves.
        if (QI && MH_CreateHook(QI, &QueryInterfaceDetour, (void**)&s_pRealQI) == MH_OK
               && MH_EnableHook(QI) == MH_OK) {
            printf("  hook installed on nvapi_QueryInterface (%p)\n", (void*)QI);
        } else {
            printf("  could not hook nvapi_QueryInterface\n");
        }

        // Arm NOW, before the snippet is loaded. The previous run armed only
        // after GetFeatureRequirements and got a single hit, which means the
        // snippet had already read and cached the architecture by then.
        s_SpoofArch = spoofArg ? spoofArg : 0x1B0;
        printf("  reporting architecture 0x%X (%s) from here on\n",
               s_SpoofArch, ArchName(s_SpoofArch));
    }

    // ---- load snippet + shim ---------------------------------------------
    s_QILogging = true;   // from here on, record what the snippet asks NVAPI for
    Step("Libraries");
    std::wstring snippetPath = FindSnippet(dllOverride);
    wprintf(L"  snippet: %s\n", snippetPath.c_str());
    if (!Exists(snippetPath)) { printf("  NOT FOUND\n"); return 1; }

    HMODULE hSnip = LoadLibraryExW(snippetPath.c_str(), nullptr,
                                   LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!hSnip) { printf("  LoadLibrary failed, err=%lu\n", GetLastError()); return 1; }
    printf("  snippet loaded at %p\n", (void*)hSnip);

    std::wstring shimPath = ExeDir() + L"\\nvngx.dll";
    HMODULE hShim = nullptr;
    if (!noShim) {
        wprintf(L"  shim:    %s\n", shimPath.c_str());
        hShim = Exists(shimPath) ? LoadLibraryExW(shimPath.c_str(), nullptr,
                                                  LOAD_WITH_ALTERED_SEARCH_PATH) : nullptr;
        printf("  shim %s\n", hShim ? "loaded" : "NOT LOADED");
    }

    auto S = [&](const char* n) { return (void*)GetProcAddress(hSnip, n); };

    if (auto f = (PFN_GetU32)S("NVSDK_NGX_GetSnippetVersion")) printf("  snippet version 0x%08X\n", f());
    if (auto f = (PFN_GetU32)S("NVSDK_NGX_GetAPIVersion"))     printf("  API version     0x%08X\n", f());
    if (auto f = (PFN_GetU32)S("NVSDK_NGX_GetGPUArchitecture"))printf("  GPU arch        0x%08X\n", f());

    void* pInitExt  = S("NVSDK_NGX_D3D11_Init_Ext");
    void* pPopulate = S("NVSDK_NGX_D3D11_PopulateParameters_Impl");
    void* pCreate   = S("NVSDK_NGX_D3D11_CreateFeature");
    void* pEval     = S("NVSDK_NGX_D3D11_EvaluateFeature");
    void* pRelease  = S("NVSDK_NGX_D3D11_ReleaseFeature");
    void* pShut1    = S("NVSDK_NGX_D3D11_Shutdown1");
    if (!pInitExt || !pCreate || !pEval || !pPopulate) {
        printf("  missing required exports\n"); return 1;
    }

    PFN_ShimInit     shInit     = hShim ? (PFN_ShimInit)    GetProcAddress(hShim, "shim_init") : nullptr;
    PFN_ShimInitFci  shInitFci  = hShim ? (PFN_ShimInitFci) GetProcAddress(hShim, "shim_init_fci") : nullptr;
    PFN_ShimPopulate shPopulate = hShim ? (PFN_ShimPopulate)GetProcAddress(hShim, "shim_populate") : nullptr;
    PFN_ShimCreate   shCreate   = hShim ? (PFN_ShimCreate)  GetProcAddress(hShim, "shim_create") : nullptr;
    PFN_ShimEval     shEval     = hShim ? (PFN_ShimEval)    GetProcAddress(hShim, "shim_eval") : nullptr;
    PFN_ShimRelease  shRelease  = hShim ? (PFN_ShimRelease) GetProcAddress(hShim, "shim_release") : nullptr;
    PFN_ShimShutdown1 shShut1   = hShim ? (PFN_ShimShutdown1)GetProcAddress(hShim, "shim_shutdown1") : nullptr;
    if (hShim && !shInit) printf("  WARNING: shim loaded but exports missing\n");

    // ---- app data path ----------------------------------------------------
    wchar_t dataPath[MAX_PATH]{};
    GetTempPathW(MAX_PATH, dataPath);
    wcscat_s(dataPath, L"dlssnr_probe\\");
    CreateDirectoryW(dataPath, nullptr);
    wprintf(L"  app data path: %s\n", dataPath);

    std::wstring snipDir = DirOf(snippetPath);
    const wchar_t* pathList[1] = { snipDir.c_str() };
    NVSDK_NGX_FeatureCommonInfo fci{};
    fci.PathListInfo.Path = pathList;
    fci.PathListInfo.Length = 1;
    fci.InternalData = nullptr;
    fci.LoggingInfo.LoggingCallback = &NgxLog;
    fci.LoggingInfo.MinimumLoggingLevel = NGX_LOG_VERBOSE;
    fci.LoggingInfo.DisableOtherLoggingSinks = false;

    const unsigned long long APP_ID = 141959980ull;
    // Use the version the snippet itself declares. It reports 0x13, and the
    // driver core rejects anything newer with FAIL_OutOfDate.
    unsigned int sdkVer = 0x13;
    if (auto f = (PFN_GetU32)S("NVSDK_NGX_GetAPIVersion")) {
        unsigned v = f();
        if (v >= 0x10 && v <= 0x20) sdkVer = v;
    }
    printf("  using SDK version 0x%02X\n", sdkVer);

    // ---- 1. is the caller check active? -----------------------------------
    Step("Caller validation (PopulateParameters_Impl)");
    CParamStore store;
    store.tracing = true;

    NVSDK_NGX_Result rDirect = ((PFN_Populate)pPopulate)(&store);
    Report("direct call", rDirect);
    bool useShim = false;
    if (rDirect == NGX_FAIL_PlatformError) {
        printf("  -> caller validation is ACTIVE (this is expected)\n");
        if (shPopulate) {
            NVSDK_NGX_Result rShim = shPopulate(pPopulate, &store);
            Report("via nvngx.dll shim", rShim);
            useShim = NGX_SUCCEED(rShim);
            printf("  -> shim %s\n", useShim ? "CLEARS the check" : "does NOT clear the check");
        } else {
            printf("  -> no shim available; cannot proceed past the check\n");
        }
    } else if (NGX_SUCCEED(rDirect)) {
        printf("  -> caller validation is NOT active on this build\n");
    }
    store.tracing = false;
    store.Dump("parameter store after PopulateParameters_Impl");
    if (!store.trace.empty()) {
        printf("  vtable slots exercised by the snippet:\n");
        std::map<int,int> slotCount;
        for (auto& t : store.trace) slotCount[t.second]++;
        for (auto& sc : slotCount) printf("    slot %2d used %d time(s)\n", sc.first, sc.second);
    }

    // helpers that route through the shim when needed
    auto CallInit = [&](const void* fciArg, unsigned ver) -> NVSDK_NGX_Result {
        if (useShim && shInit) return shInit(pInitExt, APP_ID, dataPath, dev, ver, fciArg);
        return ((PFN_InitVerFirst)pInitExt)(APP_ID, dataPath, dev, ver, fciArg);
    };
    auto CallCreate = [&](uint32_t id, NVSDK_NGX_Parameter* p, NVSDK_NGX_Handle** out) {
        if (useShim && shCreate) return shCreate(pCreate, ctx, id, p, (void**)out);
        return ((PFN_Create)pCreate)(ctx, id, p, out);
    };
    auto CallEval = [&](NVSDK_NGX_Handle* h, NVSDK_NGX_Parameter* p) {
        if (useShim && shEval) return shEval(pEval, ctx, h, p, nullptr);
        return ((PFN_Eval)pEval)(ctx, h, p, nullptr);
    };

    // ---- 1b. what does the snippet say it requires? -----------------------
    unsigned int reqMinArch = 0;
    Step("GetFeatureRequirements");
    {
        void* pReq = S("NVSDK_NGX_D3D11_GetFeatureRequirements");
        typedef NVSDK_NGX_Result(__cdecl* PFN_Req)(IDXGIAdapter*,
            const NVSDK_NGX_FeatureDiscoveryInfo*, NVSDK_NGX_FeatureRequirement*);
        typedef NVSDK_NGX_Result(__cdecl* PFN_ShimReq)(void*, void*, const void*, void*);
        PFN_ShimReq shReq = hShim ? (PFN_ShimReq)GetProcAddress(hShim, "shim_getreq") : nullptr;

        IDXGIDevice* dd = nullptr; IDXGIAdapter* aa = nullptr;
        dev->QueryInterface(__uuidof(IDXGIDevice), (void**)&dd);
        if (dd) dd->GetAdapter(&aa);

        if (pReq && aa) {
            NVSDK_NGX_FeatureDiscoveryInfo fdi{};
            fdi.SDKVersion = sdkVer;
            fdi.FeatureID  = NGX_FEATURE_DLSSNR;
            fdi.Identifier.IdentifierType = NGX_AppId_Application;
            fdi.Identifier.v.ApplicationId = APP_ID;
            fdi.ApplicationDataPath = dataPath;
            fdi.FeatureInfo = &fci;

            NVSDK_NGX_FeatureRequirement req{};
            NVSDK_NGX_Result rq = (useShim && shReq)
                ? shReq(pReq, aa, &fdi, &req)
                : ((PFN_Req)pReq)(aa, &fdi, &req);
            Report("GetFeatureRequirements", rq);
            if (NGX_SUCCEED(rq)) {
                printf("    FeatureSupported  : 0x%X %s\n", req.FeatureSupported,
                       req.FeatureSupported == 0 ? "(SUPPORTED)" : "");
                if (req.FeatureSupported & 1)  printf("      - CheckNotPresent\n");
                if (req.FeatureSupported & 2)  printf("      - DriverVersionUnsupported\n");
                if (req.FeatureSupported & 4)  printf("      - AdapterUnsupported  <-- GPU too old\n");
                if (req.FeatureSupported & 8)  printf("      - OSVersionBelowMinimum\n");
                if (req.FeatureSupported & 16) printf("      - NotImplemented\n");
                printf("    MinHWArchitecture : 0x%X\n", req.MinHWArchitecture);
                reqMinArch = req.MinHWArchitecture;
                req.MinOSVersion[254] = 0;
                printf("    MinOSVersion      : %s\n", req.MinOSVersion);
            }
        } else {
            printf("  export or adapter unavailable\n");
        }
        if (aa) aa->Release();
        if (dd) dd->Release();
    }

    // Refine to what the snippet itself asked for, if that differs from the
    // provisional value armed before the snippet was loaded.
    if ((spoofArg || spoofAuto) && !spoofArg && reqMinArch && reqMinArch != s_SpoofArch) {
        printf("\n  refining reported architecture to 0x%X (%s)\n",
               reqMinArch, ArchName(reqMinArch));
        s_SpoofArch = reqMinArch;
    }
    printf("\n  NvAPI_GPU_GetArchInfo calls intercepted so far: %d\n", s_SpoofHits);

    // ---- 1c. bring up the driver's NGX core first -------------------------
    // The reference implementation inits the core session AND the snippet
    // session. The snippet registers callbacks the core is meant to poll, so
    // skipping the core can leave it half-configured.
    Step("NGX core (_nvngx.dll)");
    NVSDK_NGX_Parameter* coreParams = nullptr;
    HMODULE hCore = LoadCore();
    printf("  core %s\n", hCore ? "loaded" : "NOT FOUND");
    if (hCore) {
        auto cInitExt = (void*)GetProcAddress(hCore, "NVSDK_NGX_D3D11_Init_Ext");
        auto cInit    = (void*)GetProcAddress(hCore, "NVSDK_NGX_D3D11_Init");
        auto cAlloc   = (NVSDK_NGX_Result(__cdecl*)(NVSDK_NGX_Parameter**))
                        GetProcAddress(hCore, "NVSDK_NGX_D3D11_AllocateParameters");
        auto cGetCap  = (NVSDK_NGX_Result(__cdecl*)(NVSDK_NGX_Parameter**))
                        GetProcAddress(hCore, "NVSDK_NGX_D3D11_GetCapabilityParameters");
        printf("  core exports: Init_Ext=%p Init=%p Alloc=%p GetCap=%p\n",
               cInitExt, cInit, (void*)cAlloc, (void*)cGetCap);

        // App-side ABI: FeatureCommonInfo 4th, version 5th.
        typedef NVSDK_NGX_Result(__cdecl* PFN_CoreInitFci)(unsigned long long,
            const wchar_t*, ID3D11Device*, const void*, unsigned int);
        NVSDK_NGX_Result rCore = 0xBAD00000;
        if (cInit) {
            for (unsigned v : { sdkVer, 0x13u, 0x14u, 0x12u, 0x11u }) {
                printf("  calling core Init with version 0x%02X ...\n", v);
                rCore = ((PFN_CoreInitFci)cInit)(APP_ID, dataPath, dev, &fci, v);
                char lbl[64]; sprintf_s(lbl, "core Init ver 0x%02X", v);
                Report(lbl, rCore);
                if (NGX_SUCCEED(rCore)) { sdkVer = v; break; }
                if (rCore != 0xBAD0000C) break;   // only retry the OutOfDate case
            }
        }
        printf("  core init %s\n", NGX_SUCCEED(rCore) ? "SUCCEEDED" : "failed");
        if (cAlloc) {
            NVSDK_NGX_Result ra = cAlloc(&coreParams);
            Report("core AllocateParameters", ra);
            if (NGX_FAILED(ra)) coreParams = nullptr;
        }
        if (!coreParams && cGetCap) {
            NVSDK_NGX_Result rg = cGetCap(&coreParams);
            Report("core GetCapabilityParameters", rg);
            if (NGX_FAILED(rg)) coreParams = nullptr;
        }
        printf("  core parameter object: %s\n", coreParams ? "obtained" : "none");
    }

    // ---- 2. init ----------------------------------------------------------
    s_QIPhase = "D3D11 init";
    Step("Init_Ext (snippet)");
    NVSDK_NGX_Result r = CallInit(&fci, sdkVer);
    Report("Init_Ext(id,path,dev,ver,&fci)", r);
    if (NGX_FAILED(r)) {
        NVSDK_NGX_Result r2 = CallInit(nullptr, sdkVer);
        Report("Init_Ext(id,path,dev,ver,nullptr)", r2);
        if (NGX_SUCCEED(r2)) r = r2;
    }
    if (NGX_FAILED(r) && useShim && shInitFci) {
        NVSDK_NGX_Result r3 = shInitFci(pInitExt, APP_ID, dataPath, dev, &fci, sdkVer);
        Report("Init_Ext(id,path,dev,&fci,ver)", r3);
        if (NGX_SUCCEED(r3)) r = r3;
    }
    // The snippet ABI declares the 5th argument as const NVSDK_NGX_Parameter*,
    // not a FeatureCommonInfo. The real NGX core passes a populated parameter
    // block there; nullptr may simply read as "no capabilities".
    if (NGX_FAILED(r) && coreParams) {
        if (useShim && shPopulate) shPopulate(pPopulate, coreParams);
        NVSDK_NGX_Result r4 = CallInit(coreParams, sdkVer);
        Report("Init_Ext(...,ver,coreParams)", r4);
        if (NGX_SUCCEED(r4)) r = r4;
    }
    if (NGX_FAILED(r)) {
        NVSDK_NGX_Result r5 = CallInit(&store, sdkVer);
        Report("Init_Ext(...,ver,ownParams)", r5);
        if (NGX_SUCCEED(r5)) r = r5;
    }

    // Plain Init (4 args in the snippet ABI) -- never tried until now.
    if (NGX_FAILED(r)) {
        void* pInitPlain = S("NVSDK_NGX_D3D11_Init");
        if (pInitPlain) {
            typedef NVSDK_NGX_Result(__cdecl* PFN_Plain)(unsigned long long,
                const wchar_t*, ID3D11Device*, unsigned int);
            typedef NVSDK_NGX_Result(__cdecl* PFN_ShimPlain)(void*, unsigned long long,
                const wchar_t*, void*, unsigned int);
            NVSDK_NGX_Result r6 = (useShim && shInit)
                ? ((PFN_ShimPlain)shInit)(pInitPlain, APP_ID, dataPath, dev, sdkVer)
                : ((PFN_Plain)pInitPlain)(APP_ID, dataPath, dev, sdkVer);
            Report("Init(id,path,dev,ver)", r6);
            if (NGX_SUCCEED(r6)) r = r6;
        }
    }

    for (unsigned v : { 0x14u, 0x13u, 0x16u }) {
        if (NGX_SUCCEED(r)) break;
        NVSDK_NGX_Result rv = CallInit(&fci, v);
        char lbl[64]; sprintf_s(lbl, "Init_Ext with version 0x%02X", v);
        Report(lbl, rv);
        if (NGX_SUCCEED(rv)) { r = rv; sdkVer = v; }
    }
    printf("  arch override applied %d time(s) by end of init\n", s_SpoofHits);

    // ---- 2b. is it the D3D11 path specifically that is inert? -------------
    // The reference player and the ReShade bridge both go through D3D12 only.
    // If D3D12 init succeeds on the same adapter where D3D11 refuses, the
    // answer is the backend, not the GPU and not our calling convention.
    if (NGX_FAILED(r)) {
        s_QIPhase = "D3D12 init";
        Step("D3D12 cross-check");
        void* pInit12 = S("NVSDK_NGX_D3D12_Init_Ext");
        printf("  NVSDK_NGX_D3D12_Init_Ext: %p\n", pInit12);
        if (pInit12) {
            IDXGIFactory1* fac = nullptr;
            IDXGIAdapter1* a1 = nullptr;
            ID3D12Device* dev12 = nullptr;
            if (SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&fac))) {
                for (UINT i = 0; fac->EnumAdapters1(i, &a1) != DXGI_ERROR_NOT_FOUND; i++) {
                    DXGI_ADAPTER_DESC1 ad1{};
                    a1->GetDesc1(&ad1);
                    if (ad1.VendorId == 0x10DE &&
                        SUCCEEDED(D3D12CreateDevice(a1, D3D_FEATURE_LEVEL_11_0,
                                                    __uuidof(ID3D12Device), (void**)&dev12))) {
                        wprintf(L"  D3D12 device on %s\n", ad1.Description);
                        break;
                    }
                    a1->Release(); a1 = nullptr;
                }
            }
            if (dev12) {
                typedef NVSDK_NGX_Result(__cdecl* PFN_Init12)(unsigned long long,
                    const wchar_t*, ID3D12Device*, unsigned int, const void*);
                typedef NVSDK_NGX_Result(__cdecl* PFN_ShimInit12)(void*, unsigned long long,
                    const wchar_t*, void*, unsigned int, const void*);
                bool ok12 = false;
                for (unsigned v : { sdkVer, 0x15u, 0x14u }) {
                    NVSDK_NGX_Result r12 = (useShim && shInit)
                        ? ((PFN_ShimInit12)shInit)(pInit12, APP_ID, dataPath, dev12, v, nullptr)
                        : ((PFN_Init12)pInit12)(APP_ID, dataPath, dev12, v, nullptr);
                    char lbl[64]; sprintf_s(lbl, "D3D12 Init_Ext ver 0x%02X", v);
                    Report(lbl, r12);
                    if (NGX_SUCCEED(r12)) {
                        printf("  ==> D3D12 WORKS where D3D11 does not.\n");
                        ok12 = true;
                        break;
                    }
                }
                TestSharing(dev, dev12, W, H);
                if (ok12) RunD3D12Feature(dev12, S, shCreate, shEval, shRelease, useShim, W, H);
                dev12->Release();
            } else {
                printf("  could not create a D3D12 device\n");
            }
            if (a1) a1->Release();
            if (fac) fac->Release();
        }
    }

    const bool initOk = NGX_SUCCEED(r);
    printf("  => init %s\n", initOk ? "SUCCEEDED" : "FAILED");

    // ---- 3. create the feature -------------------------------------------
    Step("CreateFeature (id 18)");
    store.Reset();
    if (useShim && shPopulate) shPopulate(pPopulate, &store);
    else ((PFN_Populate)pPopulate)(&store);

    store.Set("DLSSNR.Width",  (unsigned int)W);
    store.Set("DLSSNR.Height", (unsigned int)H);
    store.Set("DLSSNR.Enabled", (int)1);
    store.Set("DLSSNR.Style", (int)0);
    store.Set("DLSSNR.Hint.Render.Preset", (int)0);
    store.Set("DLSSNR.ScalingRatio", 1.0f);
    store.Set("CreationNodeMask", (unsigned int)1);
    store.Set("VisibilityNodeMask", (unsigned int)1);
    printf("  requesting %ux%u\n", W, H);

    NVSDK_NGX_Handle* feature = nullptr;
    NVSDK_NGX_Result rc = CallCreate(NGX_FEATURE_DLSSNR, &store, &feature);
    Report("CreateFeature (our param store)", rc);

    // Retry with the core's own parameter object -- it may carry capability
    // state the snippet consults that our bare store does not have.
    if (NGX_FAILED(rc) && coreParams) {
        coreParams->Set("DLSSNR.Width",  (unsigned int)W);
        coreParams->Set("DLSSNR.Height", (unsigned int)H);
        coreParams->Set("DLSSNR.Enabled", (int)1);
        coreParams->Set("DLSSNR.Style", (int)0);
        coreParams->Set("DLSSNR.Hint.Render.Preset", (int)0);
        coreParams->Set("DLSSNR.ScalingRatio", 1.0f);
        coreParams->Set("CreationNodeMask", (unsigned int)1);
        coreParams->Set("VisibilityNodeMask", (unsigned int)1);
        NVSDK_NGX_Result rc2 = CallCreate(NGX_FEATURE_DLSSNR, coreParams, &feature);
        Report("CreateFeature (core params)", rc2);
        if (NGX_SUCCEED(rc2)) rc = rc2;
    }

    // ---- 4. evaluate ------------------------------------------------------
    if (NGX_SUCCEED(rc) && feature) {
        Step("EvaluateFeature");
        ID3D11Texture2D* texIn = MakeTex(dev, W, H);
        ID3D11Texture2D* texOut = MakeTex(dev, W, H);
        if (texIn && texOut) {
            // paint something non-trivial into the input via a RTV clear
            ID3D11RenderTargetView* rtv = nullptr;
            if (SUCCEEDED(dev->CreateRenderTargetView(texIn, nullptr, &rtv))) {
                const float c[4] = { 0.35f, 0.55f, 0.20f, 1.0f };
                ctx->ClearRenderTargetView(rtv, c);
                rtv->Release();
            }
            ID3D11RenderTargetView* rtv2 = nullptr;
            if (SUCCEEDED(dev->CreateRenderTargetView(texOut, nullptr, &rtv2))) {
                const float z[4] = { 0, 0, 0, 0 };
                ctx->ClearRenderTargetView(rtv2, z);
                rtv2->Release();
            }
            ctx->OMSetRenderTargets(0, nullptr, nullptr);
            ID3D11ShaderResourceView* nullSRV[4] = {};
            ctx->PSSetShaderResources(0, 4, nullSRV);

            store.Set("DLSSNR.Color",      (ID3D11Resource*)texIn);
            store.Set("DLSSNR.Output",     (ID3D11Resource*)texOut);
            store.Set("DLSSNR.Backbuffer", (ID3D11Resource*)texOut);
            store.Set("DLSSNR.Reset", (int)1);
            store.Set("DLSSNR.UICorrection", (int)0);
            store.Set("DLSSNR.DepthInverted", (int)1);
            store.Set("DLSSNR.UseAutoMask", (int)1);
            store.Set("DLSSNR.MVecScaleX", 1.0f);
            store.Set("DLSSNR.MVecScaleY", 1.0f);
            store.Set("DLSSNR.Intensity", 1.0f);
            store.Set("DLSSNR.LocalToneStrength", 1.0f);
            store.Set("DLSSNR.LocalStructureStrength", 1.0f);
            store.Set("DLSSNR.SkinStructureStrength", 1.0f);
            store.Set("DLSSNR.ColorSubrectBaseX",  (unsigned int)0);
            store.Set("DLSSNR.ColorSubrectBaseY",  (unsigned int)0);
            store.Set("DLSSNR.ColorSubrectWidth",  (unsigned int)W);
            store.Set("DLSSNR.ColorSubrectHeight", (unsigned int)H);
            store.Set("DLSSNR.OutputSubrectBaseX", (unsigned int)0);
            store.Set("DLSSNR.OutputSubrectBaseY", (unsigned int)0);
            store.Set("DLSSNR.OutputSubrectWidth", (unsigned int)W);
            store.Set("DLSSNR.OutputSubrectHeight",(unsigned int)H);
            store.Set("DLSSNR.BackbufferSubrectBaseX", (unsigned int)0);
            store.Set("DLSSNR.BackbufferSubrectBaseY", (unsigned int)0);
            store.Set("DLSSNR.BackbufferSubrectWidth", (unsigned int)W);
            store.Set("DLSSNR.BackbufferSubrectHeight",(unsigned int)H);

            Report("Evaluate frame 1", CallEval(feature, &store));
            store.Set("DLSSNR.Reset", (int)0);
            Report("Evaluate frame 2", CallEval(feature, &store));
            ctx->Flush();

            // did anything actually get written?
            D3D11_TEXTURE2D_DESC sd{};
            texOut->GetDesc(&sd);
            sd.Usage = D3D11_USAGE_STAGING; sd.BindFlags = 0;
            sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ; sd.MiscFlags = 0;
            ID3D11Texture2D* stage = nullptr;
            if (SUCCEEDED(dev->CreateTexture2D(&sd, nullptr, &stage))) {
                ctx->CopyResource(stage, texOut);
                D3D11_MAPPED_SUBRESOURCE mr{};
                if (SUCCEEDED(ctx->Map(stage, 0, D3D11_MAP_READ, 0, &mr))) {
                    const uint16_t* px = (const uint16_t*)mr.pData;
                    bool nonZero = false;
                    for (int i = 0; i < 64 && !nonZero; i++) if (px[i]) nonZero = true;
                    printf("  output first pixels: %04X %04X %04X %04X  (%s)\n",
                           px[0], px[1], px[2], px[3],
                           nonZero ? "non-zero -- the network wrote something"
                                   : "ALL ZERO -- evaluate was skipped");
                    ctx->Unmap(stage, 0);
                }
                stage->Release();
            }
        }
        if (texIn) texIn->Release();
        if (texOut) texOut->Release();
    }

    // ---- teardown ---------------------------------------------------------
    Step("Teardown");
    if (feature && pRelease) {
        NVSDK_NGX_Result rr = (useShim && shRelease) ? shRelease(pRelease, feature)
                                                     : ((PFN_Release)pRelease)(feature);
        Report("ReleaseFeature", rr);
    }
    if (initOk && pShut1) {
        NVSDK_NGX_Result rs = (useShim && shShut1) ? shShut1(pShut1, dev)
                                                   : ((PFN_Shutdown1)pShut1)(dev);
        Report("Shutdown1", rs);
    }
    ctx->Release(); dev->Release(); ctx0->Release(); dev0->Release();

    // ---- summary ----------------------------------------------------------
    Step("Summary");
    printf("  caller check active : %s\n", rDirect == NGX_FAIL_PlatformError ? "yes" : "no");
    printf("  shim required/works : %s\n", useShim ? "yes / yes" : "n/a");
    if (s_SpoofArch) printf("  arch override       : 0x%X, applied %d time(s)\n", s_SpoofArch, s_SpoofHits);
    else             printf("  arch override       : off\n");

    if (!s_QIIds.empty()) {
        printf("\n  NVAPI entry points requested, by phase:\n");
        for (const char* ph : { "startup", "D3D11 init", "D3D12 init" }) {
            int n = 0, denied = 0;
            for (const auto& q : s_QIIds) if (!strcmp(q.phase, ph)) n++;
            printf("    [%s] %d request(s)%s\n", ph, n, n ? ":" : "");
            for (const auto& q : s_QIIds) {
                if (strcmp(q.phase, ph)) continue;
                if (!q.resolved) denied++;
                printf("      0x%08X  %-8s %s\n", q.id,
                       q.resolved ? "ok" : "REFUSED", NvApiIdName(q.id));
            }
            if (n && denied) {
                printf("      -> %d entry point(s) the driver does not provide\n", denied);
            }
        }
        printf("\n  A REFUSED line means this driver lacks that function -- that is what\n"
               "  \"driver too old\" looks like. No requests at all during D3D11 init means\n"
               "  the snippet refused before ever asking the driver anything.\n");
    }
    printf("  init                : %s\n", initOk ? "ok" : "FAILED");
    printf("  CreateFeature(18)   : 0x%08X %s\n", rc, NgxResultStr(rc));
    if (!g_log.empty()) {
        printf("\n  snippet log lines (%zu):\n", g_log.size());
        for (auto& l : g_log) printf("    %s\n", l.c_str());
    } else {
        printf("\n  snippet produced no log lines through the callback.\n");
    }

    // Anything the snippet wrote for itself, in either directory.
    for (const std::wstring& dir : { std::wstring(logDir), std::wstring(dataPath) }) {
        WIN32_FIND_DATAW fd{};
        HANDLE hf = FindFirstFileW((dir + L"*").c_str(), &fd);
        if (hf == INVALID_HANDLE_VALUE) continue;
        bool any = false;
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            any = true;
            wprintf(L"\n  --- %s%s (%lu bytes) ---\n", dir.c_str(), fd.cFileName, fd.nFileSizeLow);
            FILE* f = nullptr;
            if (_wfopen_s(&f, (dir + fd.cFileName).c_str(), L"rb") == 0 && f) {
                char line[4096];
                int n = 0;
                while (fgets(line, sizeof(line), f) && n++ < 400) fputs(line, stdout);
                if (n >= 400) printf("  ... (truncated)\n");
                fclose(f);
            }
        } while (FindNextFileW(hf, &fd));
        FindClose(hf);
        if (!any) wprintf(L"\n  (nothing written in %s)\n", dir.c_str());
    }

    printf("\ndone.\n");
    return NGX_SUCCEED(rc) ? 0 : 2;
}
