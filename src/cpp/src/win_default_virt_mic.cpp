#include "win_default_virt_mic.hpp"

#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <initguid.h>
#include <mmdeviceapi.h>
#include <mmreg.h>
#include <objbase.h>
#include <functiondiscoverykeys_devpkey.h>
#include <propidl.h>
#include <propsys.h>
#include <propvarutil.h>
#include <cwchar>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

// Undocumented Policy Config (Windows Vista+). See DanStevens/AudioEndPointController PolicyConfig.h.
struct LVDeviceShareMode {
    int dummy;
};

MIDL_INTERFACE("f8679f50-850a-41cf-9c72-430f290290c8")
ILVPolicyConfigWin7 : public IUnknown {
public:
    virtual HRESULT STDMETHODCALLTYPE GetMixFormat(PCWSTR, WAVEFORMATEX**) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDeviceFormat(PCWSTR, INT, WAVEFORMATEX**) = 0;
    virtual HRESULT STDMETHODCALLTYPE ResetDeviceFormat(PCWSTR) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDeviceFormat(PCWSTR, WAVEFORMATEX*, WAVEFORMATEX*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetProcessingPeriod(PCWSTR, INT, PINT64, PINT64) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetProcessingPeriod(PCWSTR, PINT64) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetShareMode(PCWSTR, LVDeviceShareMode*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetShareMode(PCWSTR, LVDeviceShareMode*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetPropertyValue(PCWSTR, const PROPERTYKEY&, PROPVARIANT*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetPropertyValue(PCWSTR, const PROPERTYKEY&, PROPVARIANT*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDefaultEndpoint(PCWSTR wszDeviceId, ERole eRole) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetEndpointVisibility(PCWSTR, INT) = 0;
};

MIDL_INTERFACE("568b9108-44bf-40b4-9006-86afe5b5a620")
ILVPolicyConfigVista : public IUnknown {
public:
    virtual HRESULT STDMETHODCALLTYPE GetMixFormat(PCWSTR, WAVEFORMATEX**) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDeviceFormat(PCWSTR, INT, WAVEFORMATEX**) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDeviceFormat(PCWSTR, WAVEFORMATEX*, WAVEFORMATEX*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetProcessingPeriod(PCWSTR, INT, PINT64, PINT64) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetProcessingPeriod(PCWSTR, PINT64) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetShareMode(PCWSTR, LVDeviceShareMode*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetShareMode(PCWSTR, LVDeviceShareMode*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetPropertyValue(PCWSTR, const PROPERTYKEY&, PROPVARIANT*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetPropertyValue(PCWSTR, const PROPERTYKEY&, PROPVARIANT*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDefaultEndpoint(PCWSTR wszDeviceId, ERole eRole) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetEndpointVisibility(PCWSTR, INT) = 0;
};

static const CLSID CLSID_CPolicyConfigClient = {0x870af99c, 0x171d, 0x4f9e, {0xaf, 0x0d, 0xe6, 0x3d, 0xf4, 0x0c, 0x2b, 0xc9}};
static const IID IID_ILVPolicyConfigWin7 = {0xf8679f50, 0x850a, 0x41cf, {0x9c, 0x72, 0x43, 0x0f, 0x29, 0x02, 0x90, 0xc8}};
static const CLSID CLSID_CPolicyConfigVistaClient = {0x294935ce, 0xf637, 0x4e7c, {0xa4, 0x1b, 0xab, 0x25, 0x54, 0x60, 0xb8, 0x62}};
static const IID IID_ILVPolicyConfigVista = {0x568b9108, 0x44bf, 0x40b4, {0x90, 0x06, 0x86, 0xaf, 0xe5, 0xb5, 0xa6, 0x20}};

static bool lv_win32_is_wine_host() {
    HMODULE ntdll = GetModuleHandleA("ntdll.dll");
    if (ntdll == nullptr) {
        return false;
    }
    return GetProcAddress(ntdll, "wine_get_version") != nullptr;
}

static bool env_disable_vb_cable() {
    const char* v = std::getenv("LIVE_VOCODER_DISABLE_VB_CABLE");
    if (v == nullptr || v[0] == '\0') {
        return false;
    }
    return v[0] == '1' || v[0] == 't' || v[0] == 'T' || v[0] == 'y' || v[0] == 'Y';
}

static bool win_default_virt_mic_explicit_off() {
    const char* e = std::getenv("LIVE_VOCODER_WIN_DEFAULT_VIRT_MIC");
    return e != nullptr && e[0] == '0' && e[1] == '\0';
}

static wchar_t lv_wtolower(wchar_t c) {
    if (c >= L'A' && c <= L'Z') {
        return static_cast<wchar_t>(c + (L'a' - L'A'));
    }
    return c;
}

static bool wstr_contains_ci(const wchar_t* hay, const wchar_t* needle) {
    if (hay == nullptr || needle == nullptr || needle[0] == L'\0') {
        return false;
    }
    const size_t nlen = std::wcslen(needle);
    for (const wchar_t* p = hay; *p != L'\0'; ++p) {
        size_t j = 0;
        for (; j < nlen; ++j) {
            const wchar_t c1 = p[j];
            const wchar_t c2 = needle[j];
            if (c1 == L'\0') {
                return false;
            }
            if (lv_wtolower(c1) != lv_wtolower(c2)) {
                break;
            }
        }
        if (j == nlen) {
            return true;
        }
    }
    return false;
}

static bool friendly_name_is_vb_cable_capture(const wchar_t* name) {
    if (name == nullptr || name[0] == L'\0') {
        return false;
    }
    if (!wstr_contains_ci(name, L"vb-audio")) {
        return false;
    }
    if (!wstr_contains_ci(name, L"output")) {
        return false;
    }
    return wstr_contains_ci(name, L"cable");
}

static HRESULT set_default_capture_roles(ILVPolicyConfigWin7* pc, PCWSTR id) {
    HRESULT hr = S_OK;
    static const ERole kRoles[] = {eConsole, eMultimedia, eCommunications};
    for (ERole role : kRoles) {
        const HRESULT r = pc->SetDefaultEndpoint(id, role);
        if (FAILED(r)) {
            hr = r;
        }
    }
    return hr;
}

static HRESULT set_default_capture_roles_vista(ILVPolicyConfigVista* pc, PCWSTR id) {
    HRESULT hr = S_OK;
    static const ERole kRoles[] = {eConsole, eMultimedia, eCommunications};
    for (ERole role : kRoles) {
        const HRESULT r = pc->SetDefaultEndpoint(id, role);
        if (FAILED(r)) {
            hr = r;
        }
    }
    return hr;
}

std::string lv_win32_try_set_default_capture_to_vb_cable() {
    static bool attempted = false;
    static std::string cached;
    if (attempted) {
        return cached;
    }
    attempted = true;

    if (lv_win32_is_wine_host()) {
        return cached;
    }
    if (env_disable_vb_cable() || win_default_virt_mic_explicit_off()) {
        return cached;
    }

    HRESULT coinit = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(coinit) && coinit != static_cast<HRESULT>(0x80010106)) {  // RPC_E_CHANGED_MODE
        return cached;
    }
    const bool coinit_done = SUCCEEDED(coinit);

    IMMDeviceEnumerator* en = nullptr;
    HRESULT hr =
        CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_INPROC_SERVER,
                         __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&en));
    if (FAILED(hr) || en == nullptr) {
        if (coinit_done) {
            CoUninitialize();
        }
        return cached;
    }

    IMMDeviceCollection* coll = nullptr;
    hr = en->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, &coll);
    en->Release();
    if (FAILED(hr) || coll == nullptr) {
        if (coinit_done) {
            CoUninitialize();
        }
        return cached;
    }

    UINT n = 0;
    coll->GetCount(&n);
    LPWSTR cable_id = nullptr;
    std::wstring cable_name;
    for (UINT i = 0; i < n; ++i) {
        IMMDevice* dev = nullptr;
        if (FAILED(coll->Item(i, &dev)) || dev == nullptr) {
            continue;
        }
        IPropertyStore* ps = nullptr;
        if (FAILED(dev->OpenPropertyStore(STGM_READ, &ps)) || ps == nullptr) {
            dev->Release();
            continue;
        }
        PROPVARIANT pv;
        PropVariantInit(&pv);
        bool match = false;
        if (SUCCEEDED(ps->GetValue(PKEY_Device_FriendlyName, &pv)) && pv.vt == VT_LPWSTR &&
            pv.pwszVal != nullptr) {
            if (friendly_name_is_vb_cable_capture(pv.pwszVal)) {
                match = true;
                cable_name = pv.pwszVal;
            }
        }
        PropVariantClear(&pv);
        ps->Release();
        if (match) {
            if (SUCCEEDED(dev->GetId(&cable_id)) && cable_id != nullptr) {
                dev->Release();
                break;
            }
        }
        if (cable_id != nullptr) {
            CoTaskMemFree(cable_id);
            cable_id = nullptr;
        }
        dev->Release();
    }
    coll->Release();

    if (cable_id == nullptr) {
        if (coinit_done) {
            CoUninitialize();
        }
        return cached;
    }

    ILVPolicyConfigWin7* pc = nullptr;
    hr = CoCreateInstance(CLSID_CPolicyConfigClient, nullptr, CLSCTX_INPROC_SERVER, IID_ILVPolicyConfigWin7,
                          reinterpret_cast<void**>(&pc));
    if (SUCCEEDED(hr) && pc != nullptr) {
        hr = set_default_capture_roles(pc, cable_id);
        pc->Release();
    } else {
        ILVPolicyConfigVista* pv = nullptr;
        hr = CoCreateInstance(CLSID_CPolicyConfigVistaClient, nullptr, CLSCTX_INPROC_SERVER,
                              IID_ILVPolicyConfigVista, reinterpret_cast<void**>(&pv));
        if (SUCCEEDED(hr) && pv != nullptr) {
            hr = set_default_capture_roles_vista(pv, cable_id);
            pv->Release();
        }
    }

    CoTaskMemFree(cable_id);
    if (coinit_done) {
        CoUninitialize();
    }

    if (FAILED(hr)) {
        std::fprintf(stderr,
                     "[LiveVocoder] Native Windows: could not set default mic to VB-Audio CABLE Output (HRESULT "
                     "0x%08lx). Set it manually in Settings > Sound > Input, or in Discord/OBS.\n",
                     static_cast<unsigned long>(hr));
        return cached;
    }

    static bool logged = false;
    if (!logged) {
        logged = true;
        std::fprintf(stderr,
                     "[LiveVocoder] Native Windows: default recording device set to VB-Audio CABLE Output "
                     "(Discord/OBS can use Default). Disable: LIVE_VOCODER_WIN_DEFAULT_VIRT_MIC=0.\n");
    }
    std::string out = "Windows default mic → CABLE Output";
    if (!cable_name.empty()) {
        out += " (";
        const int need = WideCharToMultiByte(CP_UTF8, 0, cable_name.c_str(), -1, nullptr, 0, nullptr, nullptr);
        if (need > 1) {
            std::string utf8(static_cast<std::size_t>(need - 1), '\0');
            WideCharToMultiByte(CP_UTF8, 0, cable_name.c_str(), -1, utf8.data(), need, nullptr, nullptr);
            out += utf8;
            out += ')';
        }
    }
    cached = std::move(out);
    return cached;
}

#endif
