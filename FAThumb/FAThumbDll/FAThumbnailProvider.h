// Copyright 2026 PGN Inc. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <windows.h>
#include <thumbcache.h>     // For IThumbnailProvider
#include <wincodec.h>       // Windows Imaging Codecs
#include <functional>

#include "img_32.h"

#pragma comment(lib, "windowscodecs.lib")

void SetBadThumb( neet::CImage32& thumb );

/*
* pxvとmdpの共通部分のThumbnailProvider
*/
class FAThumbnailProvider :
    public IInitializeWithStream, 
    public IThumbnailProvider
{
public:
    // IUnknown
    IFACEMETHODIMP QueryInterface(REFIID riid, void **ppv);
    IFACEMETHODIMP_(ULONG) AddRef();
    IFACEMETHODIMP_(ULONG) Release();

    // IInitializeWithStream
    IFACEMETHODIMP Initialize(IStream *pStream, DWORD grfMode);

    // IThumbnailProvider
    IFACEMETHODIMP GetThumbnail(UINT cx, HBITMAP *phbmp, WTS_ALPHATYPE *pdwAlpha);

    FAThumbnailProvider( std::function<bool(neet::CImage32& thumbImg, IStream* stream)> getThumbnailFunc );

    // static FAThumbnailProvider* CreateMdpThumbnailProvider();
    static FAThumbnailProvider* CreatePxvThumbnailProvider();

protected:
    ~FAThumbnailProvider();

private:
    std::function<bool(neet::CImage32& thumbImg, IStream* stream)> m_getThumbnailFunc;

    long m_cRef;

    IStream *m_pStream;
};