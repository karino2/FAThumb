// Copyright 2026 PGN Inc. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

/* -*- coding: utf-8 -*- マルチバイト */

#include "manga_file_pxv_thumb.h"

#include "manga_file_pxv_pstore.hpp"
#include "manga_file_pxv_pack.hpp"

#include "pxvinfo_generated.h"

#include "libneet_file.h"
#include "img_32.h"

using namespace pxvpack;
using namespace pagestore;
using namespace pagestream;

namespace neet
{

namespace pi = pxvinfo;

uint8_t GetPXVFormatVersion( char* header, size_t size )
{
  if (size < 4)  return 0xFF;

  if (header[0] != 'p' || header[1] != 'x' || header[2] != 'v') return 0xFF;
  return static_cast<uint8_t>(header[3]);

}

static bool IsKnownVersion( uint8_t version )
{
  if (version == 0xFF) return false;
  return version <= PXV_FORMAT_VERSION;
}

bool IsValidPXVHeader( char* header, size_t size )
{
  if (size < 4)
    return false;

  const uint8_t version = GetPXVFormatVersion( header, size );
  return IsKnownVersion( version );
}

 //////////////////////////////////////////////////////////////////////////////
static uint8_t GetPXVFormatVersion( nstring filepath )
{
  array<char, 4> buf;

  if (!IsFileExists( filepath )) return 0xFF;

  CFileSeek fs;
  if (!fs.OpenRead( filepath )) return 0xFF;
  fs.Read( buf.data(), 4 );

  return GetPXVFormatVersion( buf.data(), buf.size() );
}

bool IsValidPXVFile( nstring filepath )
{
  // 存在する？
  if (!IsFileExists( filepath )) return false;

  // ファイルサイズは大丈夫？(少なくともmasterページ分あるか？)
  size_t gfs = GetFileSize( filepath );
  if (gfs < PXV_PAGE_SIZE*MASTER_PAGE_NUM) return false;

  const uint8_t version = GetPXVFormatVersion( filepath );
  return IsKnownVersion( version );
}

// XMLを入れているストリームのvidx。一番最初。
constexpr int_vidx_t INFO_VIDX=0;

// pair.firstは呼び出し側がfreeする。
pair<uint8_t*, const pi::PXVInfo*> LoadPXVInfo( StreamReader& sr )
{
  uint8_t *buf;

  buf = (uint8_t*)malloc(sr.GetSize());
  if(buf == nullptr)
    throw PXVError("Fail to alloc");
  sr.Read(buf, (int)sr.GetSize());

  // こんなassertがあったか、failするようになっている。
  // ただしこのassertが成り立つ理由が分からないので、たぶん問題無い気がする。
  // 以下のドキュメントによると
  // [FlatBuffers: FlatBuffer Internals](https://flatbuffers.dev/flatbuffers_internals.html)
  // 最後のバイトはサイズが入るはずなので、これは'\0'では無さそうだし、Streamの方にも別段最後に0を入れる処理は無さそう。
  // ただ何か覚えていない事があったのかもしれないので、しばらくコメントアウトを残し、問題無さそうならそのうち削除する。
  //
  // assert(buf[sr.GetSize()-1] == '\0');
  {
    auto verifier = flatbuffers::Verifier(buf, sr.GetSize());
    if (!pi::VerifyPXVInfoBuffer(verifier))
    {
      free(buf);
      throw PXVError("Wrong binary format");
    }
  }
  auto pxvinfo = pi::GetPXVInfo(buf);
  return pair<uint8_t*, const pi::PXVInfo*>(buf, pxvinfo);
}

// pair.firstは呼び出し側がfreeする。
pair<uint8_t*, const pi::PXVInfo*> LoadPXVInfo( PXVPackerDecode* packer )
{
  auto sr = packer->GetReader( INFO_VIDX );
  return LoadPXVInfo( sr );
}

bool OpenPXVThumbFromPacker( CImage32* img, PXVPackerDecode& packer, int* docWidth, int* docHeight )
{
  uint8_t *pinfoBuf;
  const pi::PXVInfo *pinfo;
  try
  {
    tie(pinfoBuf, pinfo) = LoadPXVInfo( &packer );
  }
  catch(PXVError)
  {
    return false;
  }
  auto autoRelease = ScopeGuard([&]{ free( pinfoBuf ); });

  auto thumb = pinfo->thumbnail();
  *docWidth = thumb->width();
  *docHeight = thumb->height();
  void *ptr;
  uint8_t comp;
  int64_t size;

  if ( packer.Get( thumb->vidx(), &comp, &size, &ptr ) )
  {
    img->Resize( thumb->width(), thumb->height() );    
    auto guard_i = PixelLocker( img );
    if (ptr != NULL)
    {
      memcpy( img->Buffer(), ptr, (size_t)(thumb->width()*thumb->height()*4) );
      SAFE_FREE( ptr );
    }
    return true;
  }
 return true;
}

bool OpenPXVThumb( CImage32* img, nstring filepath, int* docWidth, int* docHeight )
{
  if (!IsValidPXVFile(filepath)) return false;

  PXVPackerDecode packer;
  if (!packer.Open( filepath )) return false;

  return OpenPXVThumbFromPacker( img, packer, docWidth, docHeight );
}



} ///< neet

