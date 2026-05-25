/* -*- coding: utf-8 -*- マルチバイト */

// Copyright 2026 PGN Inc. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef _PGN_IMG_32_H_
#define _PGN_IMG_32_H_

#include "libneet_pixel.h"
#include "libneet_geom.h"

namespace neet
{


/*
* PixelLockerは使わないのでダミー実装。
*/
template<typename T>
int PixelLocker( T* img )
{
  N_UNUSED( img );
  return 0;
}

template<typename T>
int CacheLocker( T* img )
{
  N_UNUSED( img );
  return 0;
}

#define NGRD int


////////////////////////////////////////////////////////////////////////////////
// 32bpp画像バッファ
////////////////////////////////////////////////////////////////////////////////
class CImage32
{
private:
  int m_width, m_height; // 幅・高さ
  int m_widthByte; // 幅のバイト数
  TBpp32* m_buffer; // 画像バッファへのptr
  TBpp32 m_1px; // 必ず確保される 1px

  void ForceWidthHeight(int& width, int& height)
  {
    if (width < 1) width = 1;
    if (height < 1) height = 1;
  }
  // 幅と高さを指定する
  void SetWidthHeight(int w, int h, double pixelByte);

  TBpp32* PixelAddress(int x, int y)
  {
    if ((size_t)x >= (size_t)m_width) return NULL;
    if ((size_t)y >= (size_t)m_height) return NULL;

    TBpp32* ptr = m_buffer + (m_width * y + x);
    return ptr;
  }

  const TBpp32* PixelAddress(int x, int y) const
  {
    if ((size_t)x >= (size_t)m_width) return NULL;
    if ((size_t)y >= (size_t)m_height) return NULL;

    const TBpp32* ptr = m_buffer + (m_width * y + x);
    return ptr;
  }

  const TBpp32* PixelAddressNC(int x, int y) const
  {
    TBpp32* ptr = m_buffer + (Width() * y + x);
    return ptr;
  }

public:
  // 幅、高さ、幅のバイト数、バッファのサイズ（バイト数）、2の累乗サイズ？
  int Width() const { return m_width; }
  int Height() const { return m_height; }
  int WidthByte() const { return m_widthByte; }
  int Size() const { return m_widthByte * m_height; }

  typedef TBpp32 PixelType;
  typedef TBpp32 PixelType2x2;

  typedef BYTE ChannelType;
  static double PixelByte(){ return 4.0; }

  CImage32(); // 画像作成 (デフォルトサイズで)
  ~CImage32();
  void Free();

  bool Resize( int width, int height );

  TBpp32 PixelGet(int x, int y) const;
  void PixelGet2x2(int x, int y, TBpp32& c0, TBpp32& c1, TBpp32& c2, TBpp32& c3) const;
  void PixelSet(int x, int y, TBpp32 color);


  // * 市松模様描画 (size は 2の累乗)
  bool FillChecker( TBpp32 c0, TBpp32 c1, int size );

  void Fill(TBpp32 color);


  // * 矩形転送 (等倍）
  bool Blt( int dx, int dy, const CImage32* src );


  // * 画像バッファへのptr
  TBpp32* Buffer(){ return m_buffer; }
  const TBpp32* Buffer() const { return m_buffer; }

  // サムネイル専用, libneetと少しインターフェースが違う

  bool PixelGetAA(int fixx, int fixy, TBpp32& c);
  bool Stretch( CImage32& src );
};

} /// neet

#endif
