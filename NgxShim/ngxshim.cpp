// nvngx.dll -- caller-validation shim for the DLSS 5 NR snippet.
//
// nvngx_dlssnr.dll checks the module its caller lives in and refuses anything
// whose path does not contain "nvngx.dll", returning 0xBAD00002
// (NVSDK_NGX_Result_FAIL_PlatformError) and logging
// "Error: Not called from NGX runtime - <path>".
//
// Each export here takes the real target function pointer plus that function's
// own arguments and calls it, so the return address the snippet walks back to
// belongs to THIS module.
//
// The forwarders must not be tail-call optimised: a `jmp` would leave the
// caller's return address on the stack and the check would fail from inside the
// shim, which looks exactly like the shim not working at all. The volatile sink
// plus the optimize pragma force a real call/ret pair.

#include <windows.h>

static volatile unsigned int g_sink;

typedef unsigned int NgxResult;

#pragma optimize("", off)

extern "C" {

__declspec(dllexport) NgxResult __cdecl shim_init(
    void* fn, unsigned long long appId, const wchar_t* appDataPath,
    void* device, unsigned int sdkVersion, const void* featureCommonInfo)
{
    typedef NgxResult(__cdecl * F)(unsigned long long, const wchar_t*, void*,
                                   unsigned int, const void*);
    NgxResult r = ((F)fn)(appId, appDataPath, device, sdkVersion, featureCommonInfo);
    g_sink = r;
    return r;
}

// Same arguments, the other documented ordering (FeatureCommonInfo before version).
__declspec(dllexport) NgxResult __cdecl shim_init_fci(
    void* fn, unsigned long long appId, const wchar_t* appDataPath,
    void* device, const void* featureCommonInfo, unsigned int sdkVersion)
{
    typedef NgxResult(__cdecl * F)(unsigned long long, const wchar_t*, void*,
                                   const void*, unsigned int);
    NgxResult r = ((F)fn)(appId, appDataPath, device, featureCommonInfo, sdkVersion);
    g_sink = r;
    return r;
}

__declspec(dllexport) NgxResult __cdecl shim_populate(void* fn, void* params)
{
    typedef NgxResult(__cdecl * F)(void*);
    NgxResult r = ((F)fn)(params);
    g_sink = r;
    return r;
}

__declspec(dllexport) NgxResult __cdecl shim_create(
    void* fn, void* ctx, unsigned int featureId, void* params, void** outHandle)
{
    typedef NgxResult(__cdecl * F)(void*, unsigned int, void*, void**);
    NgxResult r = ((F)fn)(ctx, featureId, params, outHandle);
    g_sink = r;
    return r;
}

__declspec(dllexport) NgxResult __cdecl shim_eval(
    void* fn, void* ctx, const void* handle, const void* params, void* callback)
{
    typedef NgxResult(__cdecl * F)(void*, const void*, const void*, void*);
    NgxResult r = ((F)fn)(ctx, handle, params, callback);
    g_sink = r;
    return r;
}

__declspec(dllexport) NgxResult __cdecl shim_release(void* fn, void* handle)
{
    typedef NgxResult(__cdecl * F)(void*);
    NgxResult r = ((F)fn)(handle);
    g_sink = r;
    return r;
}

__declspec(dllexport) NgxResult __cdecl shim_shutdown1(void* fn, void* device)
{
    typedef NgxResult(__cdecl * F)(void*);
    NgxResult r = ((F)fn)(device);
    g_sink = r;
    return r;
}

__declspec(dllexport) NgxResult __cdecl shim_getreq(
    void* fn, void* adapter, const void* featureDiscovery, void* outRequirement)
{
    typedef NgxResult(__cdecl * F)(void*, const void*, void*);
    NgxResult r = ((F)fn)(adapter, featureDiscovery, outRequirement);
    g_sink = r;
    return r;
}

} // extern "C"

#pragma optimize("", on)

BOOL APIENTRY DllMain(HMODULE, DWORD, LPVOID) { return TRUE; }
