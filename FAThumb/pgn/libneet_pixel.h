// Copyright 2026 PGN Inc. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

/* -*- coding: utf-8 -*- マルチバイト */

#ifndef LIBNEET_PIXEL_H_
#define LIBNEET_PIXEL_H_

#include "libneet.h"

namespace neet
{

//////////////////////////////////////////////////////////////////////////////
// ピクセル型
//////////////////////////////////////////////////////////////////////////////

#pragma pack(push,1)

union TBpp32
{
  DWORD Value;
  struct
  {
    BYTE B,G,R,A;
  };
  typedef BYTE ChannelType;
};

inline TBpp32 Bpp32( DWORD value )
{
  TBpp32 res;
  res.Value = value;
  return res;
}

inline TBpp32 Bpp32( BYTE a, BYTE r, BYTE g, BYTE b )
{
  TBpp32 res;
  res.A = a;
  res.R = r;
  res.G = g;
  res.B = b;
  return res;
}

inline TBpp32 Bpp32( BYTE r, BYTE g, BYTE b )
{
  TBpp32 res;
  res.A = 255;
  res.R = r;
  res.G = g;
  res.B = b;
  return res;
}

#pragma pack(pop)

} // namespace neet

#endif

