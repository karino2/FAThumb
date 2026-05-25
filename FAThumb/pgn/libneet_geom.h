/* -*- coding: utf-8 -*- マルチバイト */

// Copyright 2026 PGN Inc. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef LIBNEET_GEOM_H_
#define LIBNEET_GEOM_H_

#include "libneet.h"

namespace neet
{

///////////////////////////////////////////////////////////////////////////
// 矩形型
///////////////////////////////////////////////////////////////////////////
class NRECT
{
public:
  // 幅と高さが負にならないように
  static void Adjust( int& x, int &y, int& w, int& h );

  // 位置とサイズを整合
  static int PosAlign( int pos, int align );
  static int SizeAlign( int size, int align );

public:
  int x,y; // 始点位置 (横・縦)
  int w,h; // サイズ (幅・高さ)

  NRECT();
  NRECT( int x_, int y_, int w_, int h_ );
  NRECT( int w_, int h_ );
  NRECT( const NRECT* src );
  NRECT( const NRECT* src, int moveOfsX, int moveOfsY );

  void Set( int x_, int y_, int w_, int h_ );
  void Set( int w_, int h_ );
  void Set( const NRECT* r );

  void SetNull(); // 矩形初期化 (x=y=w=h=0 なら、NULLなRect)
  bool IsNull() const;  // NULL矩形？
  bool Same( int width, int height ) const; // (0,0,width,height) と同じ？
  bool Same( const NRECT* r ) const; // rect と同じ？

  bool Inside( int x_, int y_ ) const;  // 矩形範囲内？＞x_,y_
  bool Inside( const NRECT* r ) const;  // r は this の中に収まっている？

  // 整合など
  void Align( int align );
  void Align( int alignx, int aligny );
  void Mul( int value );
  void Div( int value );
  void AlignDiv( int align_div );
  void Move( int dx, int dy );

  // 矩形を広げる
  void Extend( int ew, int eh );

  // 矩形範囲を加える
  void Add( const NRECT* src );
  void Add( int x, int y );

  int Left() const;
  int Top() const;
  int Right() const;
  int Bottom() const;

  double CenterX() const;
  double CenterY() const;

  int Size() const;
};

// srcがfit幅に収まる範囲を返す (r または 戻り値)
void FitRect( int fitwidth, int fitheight, int srcwidth, int srcheight, NRECT* r );
NRECT FitRect( int fitwidth, int fitheight, int srcwidth, int srcheight );

template <class Tfit, class Tsrc>
NRECT FitRect( Tfit* fitImg, Tsrc* srcImg )
{
  return neet::FitRect( fitImg->Width(), fitImg->Height(), srcImg->Width(), srcImg->Height() );
}

} // namespace neet

#endif

