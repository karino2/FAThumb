// Copyright 2026 PGN Inc. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#define NOMINMAX
#include "FAThumbnailProvider.h"

// COM Surrogate のロックが起きないか検証
//#define SIMPLE_TEST

#include <Shlwapi.h>
#pragma comment(lib, "Shlwapi.lib")

#include "libneet.h"

#include "GetPXVThumbnail.h"

using namespace neet;

extern HINSTANCE g_hInst;
extern long g_cDllRef;

void SetBadThumb(CImage32& thumb)
{
  thumb.Resize(64, 64);
  thumb.FillChecker(Bpp32(0xFFFFFFFF), Bpp32(0xFFE0E0E0), 16);
}

FAThumbnailProvider::FAThumbnailProvider( std::function<bool(neet::CImage32& thumbImg, IStream* stream)> getThumbnailFunc) : m_getThumbnailFunc( std::move(getThumbnailFunc) ), m_cRef(1), m_pStream(NULL)
{
    InterlockedIncrement(&g_cDllRef);
}


FAThumbnailProvider::~FAThumbnailProvider()
{
    InterlockedDecrement(&g_cDllRef);
}

FAThumbnailProvider* FAThumbnailProvider::CreatePxvThumbnailProvider()
{
  return new FAThumbnailProvider(GetPxvThumbFromIStream);
}


#pragma region IUnknown

IFACEMETHODIMP FAThumbnailProvider::QueryInterface(REFIID riid, void **ppv)
{
    static const QITAB qit[] = 
    {
        QITABENT(FAThumbnailProvider, IThumbnailProvider),
        QITABENT(FAThumbnailProvider, IInitializeWithStream), 
        { 0 },
    };
    return QISearch(this, qit, riid, ppv);
}

IFACEMETHODIMP_(ULONG) FAThumbnailProvider::AddRef()
{
    return InterlockedIncrement(&m_cRef);
}

IFACEMETHODIMP_(ULONG) FAThumbnailProvider::Release()
{
    ULONG cRef = InterlockedDecrement(&m_cRef);
    if (0 == cRef)
    {
        delete this;
    }

    return cRef;
}

#pragma endregion


#pragma region IInitializeWithStream

IFACEMETHODIMP FAThumbnailProvider::Initialize(IStream *pStream, DWORD grfMode)
{
  // ハンドラのInitializeはインスタンスについき一回のみ
  HRESULT hr = HRESULT_FROM_WIN32(ERROR_ALREADY_INITIALIZED);
  if (m_pStream == NULL)
  {
      hr = pStream->QueryInterface(&m_pStream);
  }
  return hr;
}

#pragma endregion


#pragma region IThumbnailProvider

// cxはlargest desired sizeで、xかyのうち大きい方とのこと。
IFACEMETHODIMP FAThumbnailProvider::GetThumbnail(UINT cx, HBITMAP *phbmp, 
    WTS_ALPHATYPE *pdwAlpha)
{
  CImage32 thumb;
  thumb.Resize( cx, cx );
  thumb.Fill( Bpp32(0) );

  if (m_pStream != NULL)
  {
    CImage32 tmp,tmp2;
    if (m_getThumbnailFunc(tmp, m_pStream))
    {
      // cxサイズに収める
      NRECT r;
      FitRect(cx, cx, tmp.Width(), tmp.Height(), &r);

      tmp2.Resize(r.w, r.h);
      tmp2.Stretch(tmp);

      // 中央に
      thumb.Blt(cx / 2 - tmp2.Width() / 2, cx / 2 - tmp2.Height() / 2, &tmp2);

      // Releaseする (これがないと COM Surrogate がロックする)
      m_pStream->Release();
    }
    else
    {
      // サムネイル読み込み失敗
      return S_FALSE;
    }
  }
  else
  {
    // m_pStream が取得出来てなかった
    return S_FALSE;
  }

  // HBITMAPに画像をセット
	LPBYTE           lp;
	LPVOID           lpBits;
	BITMAPINFO       bmi;
	BITMAPINFOHEADER bmiHeader;

	ZeroMemory(&bmiHeader, sizeof(BITMAPINFOHEADER));
	bmiHeader.biSize      = sizeof(BITMAPINFOHEADER);
	bmiHeader.biWidth     = cx;
	bmiHeader.biHeight    = cx;
	bmiHeader.biPlanes    = 1;
	bmiHeader.biBitCount  = 32;

	bmi.bmiHeader = bmiHeader;

	*phbmp  = CreateDIBSection( NULL, (LPBITMAPINFO)&bmi, DIB_RGB_COLORS, &lpBits, NULL, 0 );
  if (*phbmp == NULL) return S_FALSE;

	lp = (LPBYTE)lpBits;
  if (lp == NULL) return S_FALSE;

  for (int y=0; y<(int)cx; y++)
  {
    for (int x=0; x<(int)cx; x++)
    {
      TBpp32 col = thumb.PixelGet( x, cx - y - 1 );
      lp[0] = col.B;
      lp[1] = col.G;
      lp[2] = col.R;
      lp[3] = col.A;
      lp += 4;
    }
  }

	*pdwAlpha = WTSAT_ARGB;

	return S_OK;
}

#pragma endregion