// Copyright 2026 PGN Inc. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "stdafx.h"

#include <Guiddef.h>
#include <shlobj.h>                 // For SHChangeNotify

#include "ClassFactory.h"
#include "Reg.h"

constexpr auto MDZ_EXTENSION = L".mdz";

// {BCB3FE44-C50C-422F-A485-052F3BBAB8E8}
static const CLSID CLSID_PxvThumbnailProvider =
{ 0xbcb3fe44, 0xc50c, 0x422f, { 0xa4, 0x85, 0x5, 0x2f, 0x3b, 0xba, 0xb8, 0xe8 } };

///////////////////////////////////////////////////////////////////////////////
HINSTANCE   g_hInst     = NULL;
long        g_cDllRef   = 0;

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
BOOL APIENTRY DllMain( HMODULE hModule, DWORD  ul_reason_for_call, LPVOID lpReserved )
{
	switch (ul_reason_for_call)
	{
	case DLL_PROCESS_ATTACH:
        g_hInst = hModule;
        DisableThreadLibraryCalls(hModule);
        break;
	case DLL_THREAD_ATTACH:
	case DLL_THREAD_DETACH:
	case DLL_PROCESS_DETACH:
		break;
	}

	return TRUE;
}

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void **ppv)
{
    HRESULT hr = CLASS_E_CLASSNOTAVAILABLE;

    //////////////////////
    // MDZだけ対応
    //////////////////////
    if (IsEqualCLSID(CLSID_PxvThumbnailProvider, rclsid))
    {
      hr = E_OUTOFMEMORY;

      ClassFactory* pClassFactory = new ClassFactory();
      if (pClassFactory)
      {
        hr = pClassFactory->QueryInterface(riid, ppv);
        pClassFactory->Release();
      }
    }

    return hr;
}

STDAPI DllCanUnloadNow(void)
{
    return g_cDllRef > 0 ? S_FALSE : S_OK;
}


STDAPI DllRegisterServer(void)
{
    HRESULT hr;

    wchar_t szModule[MAX_PATH];
    if (GetModuleFileName(g_hInst, szModule, ARRAYSIZE(szModule)) == 0)
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        return hr;
    }

    //////////////////////
    // MDZのみ対応
    //////////////////////
    hr = RegisterInprocServer(szModule, CLSID_PxvThumbnailProvider, L"FAThumbDll.MdzThumbnailProvider Class", L"Apartment");
    if (SUCCEEDED(hr))
    {
      hr = RegisterShellExtThumbnailHandler(MDZ_EXTENSION, CLSID_PxvThumbnailProvider);
      if (SUCCEEDED(hr))
      {
        // シェルのサムネイルキャッシュをinvalidate
        SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, NULL, NULL);
      }
    }

    return hr;
}


STDAPI DllUnregisterServer(void)
{
    HRESULT hr = S_OK;

    wchar_t szModule[MAX_PATH];
    if (GetModuleFileName(g_hInst, szModule, ARRAYSIZE(szModule)) == 0)
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        return hr;
    }

    //////////////////////
    // MDZのみ対応
    //////////////////////
    hr = UnregisterInprocServer(CLSID_PxvThumbnailProvider);
    if (SUCCEEDED(hr))
    {
      hr = UnregisterShellExtThumbnailHandler(MDZ_EXTENSION);
    }

    return hr;
}

