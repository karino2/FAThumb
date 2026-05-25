/* -*- coding: utf-8 -*- マルチバイト */

// Copyright 2026 PGN Inc. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef MANGA_FILE_PXV_THUMB_H_
#define MANGA_FILE_PXV_THUMB_H_

#include "manga_file_pxv_pack.hpp"
#include "manga_file_pxv_pstore.hpp"

#include <vector>
#include <tuple>

namespace pxvinfo { struct PXVInfo; }

/*
* pxvファイルのうち、サムネイル関連と、そのサムネイル関連で使う最低限の関数。
* Explorerなどでサムネイルを表示する時に使う時に必要なものだけを含んだヘッダ。
*/
namespace neet
{

class CImage32;

//////////////////////////////////////////////////////////////////////////////

// ヘッダからpxvのバージョンを取得。pxvのヘッダが不正だった場合は0xFFを返す。
uint8_t GetPXVFormatVersion( char* header, size_t size );

// pxv ファイルが正しいかチェックする。
bool IsValidPXVFile( nstring filepath );

// 先頭4バイトが正しいpxvのヘッダかを確認する。
// sizeは4より大きい必要がある。（今の所4だけreadしてこの関数を呼ぶ事を想定）
bool IsValidPXVHeader( char* header, size_t size );

bool IsValidPXVFile( nstring filepath );
//////////////////////////////////////////////////////////////////////////////

// サムネイル取得
bool OpenPXVThumb( CImage32* img, nstring filepath, int* docWidth, int* docHeight );
// PXVPackerDecodeはすでにOpen済み。IStreamなどに指す時に使う。
bool OpenPXVThumbFromPacker( CImage32* img, pxvpack::PXVPackerDecode& packer, int* docWidth, int* docHeight );

// manga_file_pxv.cppからも使う共通のutility
// pair.firstは呼び出し側がfreeする。
std::pair<uint8_t*, const pxvinfo::PXVInfo*> LoadPXVInfo( pagestream::StreamReader& sr );
std::pair<uint8_t*, const pxvinfo::PXVInfo*> LoadPXVInfo( pxvpack::PXVPackerDecode* packer );

} ///< neet

#endif
