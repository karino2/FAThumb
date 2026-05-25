/* -*- coding: utf-8 -*- マルチバイト */

// Copyright 2026 PGN Inc. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef LIBNEET_CHARCODE_H_
#define LIBNEET_CHARCODE_H_

#include "libneet.h"

///////////////////////////////////////////////////////////////////////////
// 文字コード変換
///////////////////////////////////////////////////////////////////////////

namespace neet
{

inline std::string nstring_to_utf8( nstring src )
{
  if (src.empty()) return std::string();

  int dest_size = WideCharToMultiByte(
    CP_UTF8,
    0,                  // dwFlags
    &src[0], (int)src.size(),
    NULL, 0,            // lpMultiByteStr, cbMultiByte
    NULL, NULL          // optional
  );

  std::string dest( dest_size, 0 );
  WideCharToMultiByte(
    CP_UTF8,
    0,                  // dwFlags
    &src[0],(int)src.size(),
    &dest[0], dest_size,
    NULL, NULL          // optional
  );

  return dest;
}

} ///< neet

#endif

