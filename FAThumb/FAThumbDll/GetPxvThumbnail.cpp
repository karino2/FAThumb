// Copyright 2026 PGN Inc. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#define NOMINMAX

#include "GetPXVThumbnail.h"
#include "FAThumbnailProvider.h"

#include "manga_file_pxv_thumb.h"
#include <stdexcept>

using namespace neet;

struct ThumbError : public std::runtime_error
{
  ThumbError(const std::string& msg) : std::runtime_error(msg) {}
};

static void ThrowIfFail(HRESULT hr, const char* msg)
{
  if (!SUCCEEDED(hr))
    throw ThumbError(msg);
}
/*
* IStreamを使ってSeekPageMapperのReadAtを実装するのに使うadapter
*/
struct AdapterIStream
{
  IStream* _stream;

  AdapterIStream(IStream* stream) : _stream(stream) {}

  size_t GetStreamSize()
  {
    STATSTG stg;
    auto hr = _stream->Stat(&stg, STATFLAG_NONAME);
    ThrowIfFail(hr, "Fail to stat.");

    return (size_t)stg.cbSize.QuadPart;
  }

  int ReadAt(size_t pos, void* buf, int len)
  {
    LARGE_INTEGER dest;
    dest.QuadPart = (LONGLONG)pos;
    auto hr = _stream->Seek(dest, SEEK_SET, NULL);
    ThrowIfFail(hr, "Seek fail");

    ULONG readLen;
    hr = _stream->Read(buf, (ULONG)len, &readLen);
    ThrowIfFail(hr, "Read fail");
    if (readLen != len)
      throw ThumbError("Can't read enough");
    return readLen;
  }
};

namespace neet {
  extern std::pair<uint8_t*, const pxvinfo::PXVInfo*> LoadPXVInfo(pagestore::ReadablePageStore* rstore);
}

bool GetPxvThumbFromIStream(CImage32& thumbImg, IStream* stream)
{
  char header[4];

  ULONG rn;
  stream->Read(&header[0], 4, &rn);

  if (!IsValidPXVHeader( header, 4 ))
  {
    return false;
  }

  try
  {
    AdapterIStream adapter(stream);
    pagestore::SeekPageMapper sm(adapter.GetStreamSize(), [&](size_t pos, uint8_t* dat, int len) { return adapter.ReadAt(pos, dat, len); });

    auto rstore = pagestore::RPageStoreFactory::CreateNoCheck(std::move(sm));

    pxvpack::PXVPackerDecode packer;
    packer.AttachPageStore(rstore.release());

    int twidth, theight;
    return OpenPXVThumbFromPacker(&thumbImg, packer, &twidth, &theight);
  }
  catch (const std::runtime_error&)
  {
    MessageBox(NULL, L"throw error!", L"xxx", MB_ICONINFORMATION);
    SetBadThumb(thumbImg);
    return false;
  }
}
