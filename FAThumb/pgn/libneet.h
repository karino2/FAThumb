/* -*- coding: utf-8 -*- マルチバイト */

// Copyright 2026 PGN Inc. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef LIBNEET_H_
#define LIBNEET_H_

// * VisualStudio (Win32)
#define __NEET_API_WIN32__
#define __NEET_IDE_VS__

//////////////////////////////////////////////////////////////////////////////
// PixelLocker/CacheLocker が使える？ (C++14以降)
//////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////////
// libneetのデフォルトヘッダ
//////////////////////////////////////////////////////////////////////////////

#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <vector>
#include <map>
#include <string>
#include <typeinfo>
#include <cstring>

#include <assert.h>

#include <functional>

//////////////////////////////////////////////////////////////////////////////
// 細々したマクロ
//////////////////////////////////////////////////////////////////////////////

// よくあるfree/deleteマクロ
#define SAFE_FREE(a) if(a != NULL){ free(a); a = NULL; }
#define SAFE_DELETE(a) if(a != NULL){ delete a; a = NULL; }

// コンパイラ警告 "unused" 対応用マクロ
#define N_UNUSED(x) (void)x;

// VisualStudio用
#if defined(__NEET_IDE_VS__)
#include <tchar.h>
#endif

//////////////////////////////////////////////////////////////////////////////
// Win32環境の定義
//////////////////////////////////////////////////////////////////////////////
#if defined(__NEET_API_WIN32__)

  #define NOMINMAX
  #include <windows.h>
  #include <windowsx.h>
  #include <mmsystem.h>

  #define __NEET_WSTRING__

  typedef HWND view_t; // 画面表示対象は、HWND
  typedef HDC device_t; // 表示デバイスは、HDC
  typedef WORD keyboard_t; // キーボード番号型は、WORD
  typedef HCURSOR cursor_t; // カーソル型は、HCURSOR

  // Win32 API が使える
  #define __NEET_WIN32_ENABLED__

  ////////////////////////////////////////////
  // Visual C++
  ////////////////////////////////////////////
  #if defined(__NEET_IDE_VS__)
  typedef unsigned __int64 uint64_t;
  typedef __int64 int64_t;

  typedef unsigned short uint16_t;
  typedef signed short int16_t;
  #endif
#endif
//////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////////
// nstring 定義 (主に、ファイルパスや OS API の標準文字列型)
//////////////////////////////////////////////////////////////////////////////
#if defined(__NEET_WSTRING__)
  // nstring は std::wstring (UTF-16 または UTF-32)
  typedef std::wstring nstring;
  typedef wchar_t nchar;
  typedef std::wstringstream nstringstream;
#endif

#if defined(__NEET_STRING__)
  // nstring はUTF-8 (std::string)
  typedef std::string nstring;
  typedef char nchar;
  typedef std::stringstream nstringstream;
#endif

//////////////////////////////////////////////////////////////////////////////
// DEBUG
//////////////////////////////////////////////////////////////////////////////
#if defined(_DEBUG) || defined(QT_DEBUG)
  #define N_DEBUG
#endif

namespace neet
{

//////////////////////////////////////////////////////////////////////////////
// デフォルトの何もしないコールバック
//////////////////////////////////////////////////////////////////////////////
typedef void (*TLibneetCallback)();

//////////////////////////////////////////////////////////////////////////////
// 255で割る (Jim Blinn法)
//////////////////////////////////////////////////////////////////////////////
template <typename T>
inline void Div255( T& v )
{
  v++;
  v = (v + (v >> 8)) >> 8;
}

//////////////////////////////////////////////////////////////////////////////
// rep stos を期待 (memcpy は速いので、NMovsd/q は不要)
//////////////////////////////////////////////////////////////////////////////
inline void NStosd( DWORD* destadr, DWORD value, size_t count )
{
#if defined(_MSC_VER) && defined(_M_X64)
  __stosd( destadr, value, count );
#else
  for (size_t i=0; i<count; i++)
  {
    *destadr = value;
    destadr++;
  }
#endif
}

//////////////////////////////////////////////////////////////////////////////
// デバッグログ。デバッグビルドの時のみ有効
//////////////////////////////////////////////////////////////////////////////
#ifdef DEBUG
void NDebugLog( const char* fmt, ...);
#else
inline void NDebugLog( const char*, ...) {}
#endif

//////////////////////////////////////////////////////////////////////////////
// ScopeGuard
//////////////////////////////////////////////////////////////////////////////
/*
excceptionやreturnの時に自動で実行される後始末を行うクラス。
ラムダ式を渡す。

void * ptr = malloc(somesize);
if (ptr == nullptr)
   return nullptr;

auto guard = ScopeGuard([&]{ free( ptr ); });

// なにか処理

// ptrは解放せずに呼び出し元に返す、という場合。
guard.dismiss();
return ptr;
*/
struct ScopeGuard
{
  std::function<void(void)> _call;
  bool _dismiss = false;
  explicit ScopeGuard( std::function<void(void)> call ) : _call( call ) {}
  ~ScopeGuard()
  {
    if ( !_dismiss )
      _call();
  }
  void dismiss() { _dismiss = true; }
};

///////////////////////////////////
// NRange
///////////////////////////////////

/*
NCountableRange: NRangeのヘルパークラス。NRangeからしか使わない。（ユーザーはautoで受け取るので）
*/
template<typename T>
class NCountableRange
{
  T _rangeBegin;
  T _rangeEnd;

  class range_iterator
  {
    T _current;

  public:
    T operator *() const { return _current; }
    const range_iterator &operator ++()
    {
      ++_current;
      return *this;
    }

    bool operator ==(const range_iterator &other) const { return _current == other._current; }
    bool operator !=(const range_iterator &other) const { return _current != other._current; }

    range_iterator(T start) : _current (start) { }
  };


public:
  NCountableRange( T beg, T end ) : _rangeBegin(beg),_rangeEnd(end) {}
  range_iterator begin() const { return range_iterator(_rangeBegin); }
  range_iterator end() const { return range_iterator(_rangeEnd); }

  std::vector<T> ToVector()
  {
    std::vector<T> res;
    for (auto iter = begin(); iter != end(); ++iter)
    {
      res.push_back( *iter );
    }
    return res;
  }
};

/*
beg から end-1 までの数字を順番に返すiteratorを返す。(endは含まない)
range for文で使う。

例:
for (auto i : NRange(0, 100))
{
  ...
}
*/
template<typename T>
NCountableRange<T> NRange( T beg, T end ) { return NCountableRange<T>( beg, end ); }

/*
begに0を即値で指定すると、endがunsignedだったりint64_tだった時に型違いが発生するので、そのケースの型解決用のヘルパー。
*/
template<typename T>
NCountableRange<typename std::enable_if<!std::is_same<T, int>::value, T>::type> NRange( int beg, T end ) { return NCountableRange<T>( (T) beg, end ); }

/*
NRange(0, end)と同じ振る舞いをするヘルパー。０からend-1までのiteratorを返す。
*/
template<typename T>
NCountableRange<T> NRange( T end ) { return NCountableRange<T>( 0, end ); }

/*
 * Iterator実装の為の便利クラス。boostやfollyのiterator_facadeと似てる。
 * Iteratorを実装したい人はこのクラスを継承して以下のメソッドを実装すると、iter++, ++iter、など必要なモノをすべて実装してくれる。
 *
 *   void Increment();
 *   void Decrement(); // オプショナル、--したい人だけ実装
 *   V& Dereference() const;
 *   bool Equal( const D& other) const;
 *
 * Templateパラメータ:
 * D: 継承先クラス (CRTP)
 * V: 値の型
*/
template<class D, typename V>
class IteratorFacade
{
public:
  const V& operator*() const
  {
    return AsDerivedConst().Dereference();
  }

  V& operator*()
  {
    return AsDerived().Dereference();
  }

  D& operator++()
  {
    AsDerived().Increment();
    return AsDerived();
  }

  D operator++(int)
  {
    auto res = AsDerived(); // 進める前をコピー
    AsDerived().Increment();
    return res;
  }

  D& operator--()
  {
    AsDerived().Decrement();
    return AsDerived();
  }

  D operator--(int)
  {
    auto res = AsDerived(); // 戻す前をコピー
    AsDerived().Decrement();
    return res;
  }

  bool operator==(const D &other) const
  {
    return AsDerivedConst().Equal( other );
  }
  bool operator!=(const D &other) const
  {
    return !AsDerivedConst().Equal( other );
  }

 private:
  D& AsDerived() {
    return static_cast<D&>( *this );
  }

  const D & AsDerivedConst() const {
    return static_cast<const D&>( *this );
  }
};

/*
 * srcベクトルの各要素にfunを実行し、その結果を保持する同じサイズのvectorを返す。
 * いわゆるmap_fn。いつもpush_backを使う程度にはパフォーマンスは意識していない。
 *
 * 例:
 * std::vector<int> src { 1, 2, 3, 4 };
 * auto res = MapFn( src, [](int e) { return e*2; } );
 * for(auto i : res)
 * {
 *   cout << i << endl;
 * }
 *
 * 結果:
 * 2
 * 4
 * 6
 * 8
 *
*/
template<typename T, typename F>
auto MapFn( const std::vector<T>& src, F fun ) -> std::vector<decltype( fun(src[0]) )>
{
  std::vector<decltype( fun(src[0]) )> ret;
  for( auto i : NRange(src.size()) )
  {
    ret.push_back( fun( src[i] ) );
  }
  return ret;
}

// rvalueバージョン
template<typename T, typename F>
auto MapFn( std::vector<T>&& src, F fun ) -> std::vector<decltype( fun(std::move(src[0])) )>
{
  std::vector<decltype( fun(std::move(src[0])) )> ret;
  for( auto i : NRange(src.size()) )
  {
    ret.push_back( fun( std::move(src[i]) ) );
  }
  return ret;
}



/*
 * srcのベクトルにfunを実行し、trueを返したものだけを含んだベクトルを返す。
 * 元のsrcは変更せず新規のベクトルを返す。要素はコピー出来る事を前提とする。
 *
 * 例:
 * std::vector<int> src { 1, 2, 3, 4 };
 * auto res = FilterFn( src, [](int e) { return e%2 == 0; } );
 * for(auto i : res)
 * {
 *   cout << i << endl;
 * }
 *
 * 結果:
 * 2
 * 4
*/
template<typename T, typename F>
std::vector<T> FilterFn( const std::vector<T>& src, F fun )
{
  std::vector<T> ret;
  for( auto i : NRange(src.size()) )
  {
    if (fun(src[i]))
      ret.push_back( src[i] );
  }
  return ret;
}

//////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////

} /// neet

#endif
