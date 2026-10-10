#pragma once
// The three UPnP NAT interfaces upnp.cpp uses, in natupnp.h's vtable order. MinGW-w64 ships no natupnp.h, so both
// toolchains use this one. GUIDs as in the Windows SDK (and Wine's hnetcfg.dll).
#include <windows.h>
#include <oaidl.h>

namespace dn {

inline constexpr CLSID kClsidUPnPNAT = {0xae1e00aa, 0x3fd5, 0x403c, {0x8a, 0x27, 0x2b, 0xbd, 0xc3, 0x0c, 0xd0, 0xe1}};
inline constexpr IID kIidUPnPNAT = {0xb171c812, 0xcc76, 0x485a, {0x94, 0xd8, 0xb6, 0xb3, 0xa2, 0x79, 0x4e, 0x99}};

struct IStaticPortMapping : IDispatch {
    virtual HRESULT STDMETHODCALLTYPE get_ExternalIPAddress(BSTR* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_ExternalPort(long* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_InternalPort(long* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Protocol(BSTR* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_InternalClient(BSTR* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Enabled(VARIANT_BOOL* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Description(BSTR* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE EditInternalClient(BSTR value) = 0;
    virtual HRESULT STDMETHODCALLTYPE Enable(VARIANT_BOOL value) = 0;
    virtual HRESULT STDMETHODCALLTYPE EditDescription(BSTR value) = 0;
    virtual HRESULT STDMETHODCALLTYPE EditInternalPort(long value) = 0;
};

struct IStaticPortMappingCollection : IDispatch {
    virtual HRESULT STDMETHODCALLTYPE get__NewEnum(IUnknown** value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Item(long externalPort, BSTR protocol, IStaticPortMapping** mapping) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Count(long* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE Remove(long externalPort, BSTR protocol) = 0;
    virtual HRESULT STDMETHODCALLTYPE Add(long externalPort, BSTR protocol, long internalPort, BSTR internalClient,
                                          VARIANT_BOOL enabled, BSTR description, IStaticPortMapping** mapping) = 0;
};

struct IUPnPNAT : IDispatch {
    virtual HRESULT STDMETHODCALLTYPE get_StaticPortMappingCollection(IStaticPortMappingCollection** value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_DynamicPortMappingCollection(IUnknown** value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_NATEventManager(IUnknown** value) = 0;
};

}  // namespace dn
