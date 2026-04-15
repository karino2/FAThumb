// Copyright 2026 PGN Inc. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

/* -*- coding: utf-8 -*- マルチバイト */

#include "libneet_geom.h"

namespace neet
{

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
void NRECT::Adjust( int& x, int &y, int& w, int& h )
{
  if (w < 0)
  {
    x += w;
    w = -w;
  }

  if (h < 0)
  {
    y += h;
    h = -h;
  }
}

/////////////////////////////////////////////////////////////////////////////
int NRECT::PosAlign( int pos, int align )
{
  if (pos > 0)
  {
    // 常に整合だ
    pos = pos / align * align;
  }
  else
  {
    if ((-pos) % align != 0)
    {
      // 整合の必要あり
      pos = pos / align * align;
      pos -= align;
    }
  }

  return pos;
}

int NRECT::SizeAlign( int size, int align )
{
  // 整合してる
  if (size % align == 0) return size;

  // 整合させよう
  size = size / align * align + align;

  return size;
}

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
NRECT::NRECT()
{
  SetNull();
}

NRECT::NRECT( int x_, int y_, int w_, int h_ )
{
  x = x_;
  y = y_;
  w = w_;
  h = h_;
}

NRECT::NRECT( int w_, int h_ )
{
  x = 0;
  y = 0;
  w = w_;
  h = h_;
}

NRECT::NRECT( const NRECT* src )
{
  Set( src );
}

NRECT::NRECT( const NRECT* src, int moveOfsX, int moveOfsY )
{
  Set( src );
  Move( moveOfsX, moveOfsY );
}

/////////////////////////////////////////////////////////////////////////////
void NRECT::Set( int x_, int y_, int w_, int h_ )
{
  x = x_;
  y = y_;
  w = w_;
  h = h_;
}

void NRECT::Set( int w_, int h_ )
{
  x = 0;
  y = 0;
  w = w_;
  h = h_;
}

void NRECT::Set( const NRECT* r )
{
  x = r->x;
  y = r->y;
  w = r->w;
  h = r->h;
}

/////////////////////////////////////////////////////////////////////////////
void NRECT::SetNull()
{
  x = 0;
  y = 0;
  w = 0;
  h = 0;
}

bool NRECT::IsNull() const
{
  if ((x == 0) && (y == 0) && (w == 0) && (h == 0))
  {
    return true;
  }
  return false;
}

bool NRECT::Same( int width, int height ) const
{
  if (x != 0) return false;
  if (y != 0) return false;
  if (w != width) return false;
  if (h != height) return false;

  return true;
}

bool NRECT::Same( const NRECT* r ) const
{
  if (x != r->x) return false;
  if (y != r->y) return false;
  if (w != r->w) return false;
  if (h != r->h) return false;

  return true;
}

/////////////////////////////////////////////////////////////////////////////
bool NRECT::Inside( int x_, int y_ ) const
{
  // x_,y_ は this の範囲内？
  if (x > x_) return false;
  if (y > y_) return false;
  if (x + w <= x_) return false;
  if (y + h <= y_) return false;

  return true;
}

bool NRECT::Inside( const NRECT* r ) const
{
  // r は完全に this の範囲内？ (ちょっとでも外れてたらアウト)
  if (x > r->x) return false;
  if (y > r->y) return false;

  if (r->x + r->w > x + w) return false;
  if (r->y + r->h > y + h) return false;

  return true;
}

/////////////////////////////////////////////////////////////////////////////
void NRECT::Align( int align )
{
  Align( align, align );
}

void NRECT::Align( int alignx, int aligny )
{
  if (IsNull()) return;

  // 元のは取っとく
  NRECT r = *this;

  // 位置を縮める (負も考慮)
  this->x = PosAlign( this->x, alignx );
  this->y = PosAlign( this->y, aligny );

  // 縮まった分を考慮して、幅を整合させる
  int shw = r.x - this->x;
  int shh = r.y - this->y;
  this->w = SizeAlign( this->w + shw, alignx );
  this->h = SizeAlign( this->h + shh, aligny );
}

void NRECT::Mul( int value )
{
  this->x *= value;
  this->y *= value;
  this->w *= value;
  this->h *= value;
}

void NRECT::Div( int value )
{
  this->x /= value;
  this->y /= value;
  this->w /= value;
  this->h /= value;
}

void NRECT::AlignDiv( int align_div )
{
  // Align と Div を連続して行う
  Align( align_div );
  Div( align_div );
}

void NRECT::Move( int dx, int dy )
{
  this->x += dx;
  this->y += dy;
}

/////////////////////////////////////////////////////////////////////////////
void NRECT::Extend( int ew, int eh )
{
  if (IsNull()) return;

  this->x -= ew;
  this->y -= eh;
  this->w += ew*2;
  this->h += eh*2;
}

/////////////////////////////////////////////////////////////////////////////
void NRECT::Add( const NRECT* src )
{
  if (src->IsNull()) return;

  if (IsNull())
  {
    *this = *src;
  }
  else
  {
    if (src->x < this->x)
    {
      this->w += (this->x - src->x);
      this->x = src->x;
    }
    if (src->y < this->y)
    {
      this->h += (this->y - src->y);
      this->y = src->y;
    }

    if (src->x + src->w > this->x + this->w )
    {
      this->w = src->x + src->w - this->x;
    }
    if (src->y + src->h > this->y + this->h )
    {
      this->h = src->y + src->h - this->y;
    }
  }
}

void NRECT::Add( int x, int y )
{
  NRECT src( x, y, 1, 1 );
  Add( &src );
}

/////////////////////////////////////////////////////////////////////////////
int NRECT::Left() const
{
  return x;
}

int NRECT::Top() const
{
  return y;
}

int NRECT::Right() const
{
  return x + w;
}

int NRECT::Bottom() const
{
  return y + h;
}

double NRECT::CenterX() const
{
  return x + 0.5 * w;
}

double NRECT::CenterY() const
{
  return y + 0.5 * h;
}

int NRECT::Size() const
{
  return w * h;
}

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
void FitRect( int fitwidth, int fitheight, int srcwidth, int srcheight, NRECT* r )
{
  double vrate = 1.0;
  double wrate = 1.0;
  if (srcheight != 0) vrate = (double)srcwidth / srcheight;
  if (fitheight != 0) wrate = (double)fitwidth / fitheight;

  r->x = 0;
  r->y = 0;

  if (fitwidth >= fitheight)
  {
    if (srcwidth >= srcheight)
    {
      if (wrate > vrate)
      {
        r->w = (int)(vrate * fitheight);
        r->h = fitheight;
      }
      else
      {
        r->w = fitwidth;
        r->h = (int)( (double)fitwidth / vrate );
      }
    }
    else
    {
      r->w = (int)( vrate * fitheight );
      r->h = fitheight;
    }
  }
  else
  {
    if (srcwidth >= srcheight)
    {
      r->w = fitwidth;
      r->h = (int)( (double)fitwidth / vrate );
    }
    else
    {
      if (wrate > vrate)
      {
        r->w = (int)(vrate * fitheight );
        r->h = fitheight;
      }
      else
      {
        r->w = fitwidth;
        r->h = (int)( (double)fitwidth / vrate );
      }
    }
  }
}

/////////////////////////////////////////////////////////////////////////////
NRECT FitRect( int fitwidth, int fitheight, int srcwidth, int srcheight )
{
  NRECT r;
  FitRect( fitwidth, fitheight, srcwidth, srcheight, &r );
  return r;
}

} // namespace neet

