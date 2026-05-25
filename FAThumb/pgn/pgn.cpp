/* -*- coding: utf-8 -*- マルチバイト */

// Copyright 2026 PGN Inc. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include <windows.h>
#include <string>
#include "img_32.h"

namespace neet
{

/*
* CImage32の実装
*/

//////////////////////////////////////////////////////////////////////////////
  //////////////////////////////////////////////////////////////////////////////
  CImage32::CImage32()
{
  m_buffer = &m_1px;
  Resize( 1, 1 );
}

//////////////////////////////////////////////////////////////////////////////
CImage32::~CImage32()
{
  Free();
}

//////////////////////////////////////////////////////////////////////////////
void CImage32::Free()
{
  // 新規確保されてない
  if (m_buffer == &m_1px) return;

  // 確保されてるバッファを解放
  if (m_buffer != NULL)
  {
    free( m_buffer );
    m_buffer = NULL;
  }
}

//////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////
bool CImage32::Resize( int width, int height )
{
  // サイズ変更なし
  if ((width == Width()) && (height == Height()))
  {
    return true;
  }

  ForceWidthHeight( width, height );

  Free();

  m_buffer = (TBpp32*)malloc( width * height * sizeof(TBpp32) );

  if (m_buffer != NULL)
  {
    // 確保できた
    SetWidthHeight( width, height, PixelByte() );
    return true;
  }

  // できなかったので、1x1px
  m_buffer = &m_1px;
  SetWidthHeight( 1, 1, PixelByte() );
  return false;
}

void CImage32::SetWidthHeight(int w, int h, double pixelByte)
{
  // サイズ設定
  m_width = w;
  m_height = h;
  m_widthByte = (int)(std::ceil(pixelByte * m_width));
}

//////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////
void CImage32::PixelSet( int x, int y, TBpp32 color )
{
  TBpp32* ptr = PixelAddress( x, y );
  if (ptr == NULL) return;

  *ptr = color;
}

//////////////////////////////////////////////////////////////////////////////
TBpp32 CImage32::PixelGet( int x, int y ) const
{
  TBpp32 res;
  res.Value = 0;

  const TBpp32* ptr = PixelAddress( x, y );
  if (ptr != NULL) res = *ptr;

  return res;
}

//////////////////////////////////////////////////////////////////////////////
void CImage32::PixelGet2x2( int x, int y, TBpp32& c0, TBpp32& c1, TBpp32& c2, TBpp32 &c3 ) const
{
  // 範囲外を参照しそう？
  bool b0 = (x + 1 >= m_width);
  bool b1 = (y + 1 >= m_height);
  bool b2 = (x < 0);
  bool b3 = (y < 0);

  // アドレス違反起きるので PixelGet
  if (b0 || b1 || b2 || b3)
  {
    c0 = PixelGet( x, y );
    c1 = PixelGet( x+1, y );
    c2 = PixelGet( x, y+1 );
    c3 = PixelGet( x+1, y+1 );
    return;
  }

  // 高速取得
  const TBpp32* adr = PixelAddressNC( x, y );
  c0 = *adr;
  c1 = *(adr + 1);
  c2 = *(adr + m_width);
  c3 = *(adr + m_width + 1);
}

//////////////////////////////////////////////////////////////////////////////
void CImage32::Fill(TBpp32 color)
{
  DWORD* destadr = (DWORD*)PixelAddress(0, 0);
  NStosd(destadr, color.Value, Width() * Height());
}

//////////////////////////////////////////////////////////////////////////////
bool CImage32::FillChecker( TBpp32 c0, TBpp32 c1, int size )
{
  const int mask = size - 1;
  const int hsize = size / 2;

  for (int j=0; j<Height(); j++)
  {
    int by = 0;
    const int my = j & mask;
    if (my < hsize) by = 1;

    for (int i=0; i<Width(); i++)
    {
      const int mx = i & mask;
      int bx = 0;
      if (mx < hsize) bx = 1;

      const int check = (bx + by) & 1;
      if (check == 0)
      {
        PixelSet( i, j, c0 );
      }
      else
      {
        PixelSet( i, j, c1 );
      }
    }
  }

  return true;
}

//////////////////////////////////////////////////////////////////////////////
bool CImage32::Blt( int dx, int dy, const CImage32* src )
{
  for (int j=0; j<src->Height(); j++)
  {
    for (int i=0; i<src->Width(); i++)
    {
      const TBpp32 col = src->PixelGet( i, j );
      PixelSet( dx + i, dy + j, col );
    }
  }

  return true;
}

//////////////////////////////////////////////////////////////////////////////
bool CImage32::PixelGetAA(int fixx, int fixy, TBpp32& c)
{
  typedef size_t sum_t;

  // 取得すべきピクセル位置
  const int px = fixx >> 16;
  const int py = fixy >> 16;

  // 回転時など、完全に範囲外を参照する場合は多いので
  c.Value = 0;

  if (px < -1) return false;
  if (py < -1) return false;
  if (px >= Width()) return false;
  if (py >= Height()) return false;

  // 4点取得
  CImage32::PixelType pix[4];
  PixelGet2x2(px, py, pix[0], pix[1], pix[2], pix[3]);

  // 4点が同じ色なら、混ぜる必要はない
  if (pix[0].Value == pix[1].Value)
  {
    if (pix[0].Value == pix[2].Value)
    {
      if (pix[0].Value == pix[3].Value)
      {
        c.Value = pix[0].Value;
        return true;
      }
    }
  }

  // ピクセルの小数位置
  const int fx = (fixx >> 8) & 0xFF;
  const int fy = (fixy >> 8) & 0xFF;

  /////////////////////////
  // 占有率を出す
  /////////////////////////
  // 除算カット版
  sum_t f[4];
  f[0] = ((255 - fx + 1) * (255 - fy)) >> 8;
  f[1] = ((fx + 1) * (255 - fy)) >> 8;
  f[2] = ((255 - fx + 1) * fy) >> 8;
  f[3] = 255 - f[0] - f[1] - f[2];

  sum_t a, r, g, b, count;
  a = r = g = b = count = 0;

  /////////////////////////
  // 4点を占有率で混ぜる
  /////////////////////////
  for (int i = 0; i < 4; i++)
  {
    if (pix[i].A != 0)
    {
      sum_t ca = pix[i].A * f[i];
      a += ca;
      r += pix[i].R * ca;
      g += pix[i].G * ca;
      b += pix[i].B * ca;
    }
  }

  if (a != 0)
  {
    c.R = (BYTE)(r / a);
    c.G = (BYTE)(g / a);
    c.B = (BYTE)(b / a);

    Div255(a);
    c.A = (BYTE)a;
  }

  return true;
}

/*
* バイリニアでのStretchBlt。
*/
bool CImage32::Stretch(CImage32& src)
{
  /////////////
  // 倍率
  /////////////
  double zx = 0;
  double zy = 0;

  if (Width() >= 2) zx = (double)(src.Width() - 1) / (Width() - 1);
  if (Height() >= 2) zy = (double)(src.Height() - 1) / (Height() - 1);

  // 16:16 の固定小数に
  int zx_ = (int)(zx * 65536);
  int zy_ = (int)(zy * 65536);

  for (int j = 0; j < Height(); j++)
  {
    for (int i = 0; i < Width(); i++)
    {
      const int fx = zx_ * i;
      const int fy = zy_ * j;

      CImage32::PixelType color;
      if (src.PixelGetAA(fx, fy, color))
      {
        PixelSet(i, j, color);
      }
    }
  }

  return true;
}

} ///< neet
