// Copyright 2026 PGN Inc. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

/* -*- coding: utf-8 -*- マルチバイト */

#ifndef MANGA_FILE_PXV_PSTORE_H_
#define MANGA_FILE_PXV_PSTORE_H_

/*
  PXVフォーマットのPageStoreのレイヤ（一番下のレイヤ）
  ヘッダのみでincludeして使う。
  このファイルはpxv実装の内部で使うだけで、外部からはmanga_file_pxv.hだけをincludeすれば十分。
*/

// お行儀の悪いマクロ対策
#if defined(max)
#undef max
#endif

#include <algorithm>
#include <array>
#include <cassert>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <sstream>
#include <vector>
#include <unordered_set>

extern "C" {
#include <sys/stat.h>
#include <stdint.h>
}

#include "libneet_file.h"
#include "libneet_charcode.h"
#include "libneet_thread.h"

// 内部実装でしかincludeされないのでいいでしょう。
using namespace std;

namespace pagestore {
/*
  ファイルを64KBごとのブロックで管理するレイヤ。
  ページには以下の三種類がある

  - MasterPage
  - PEPage
  - 普通のページ

  普通のページに実際のデータを含み、PEPageやMasterPageはそれらの管理う情報を含む。

  MasterPage
  先頭の２つのページで、現在更新中のものとコミットされたものの２つがある（両者は交互に入れ替える）
  マスターページは、ヘッダ、最も大きいpageId、PageEntryのリストを含む。PageEntryについては後述
  先頭のエントリは次のPEPageを（あれば）指す

  PEPage
  PageEntryのリストだけを含んだページ。0番目のエントリは次のPEPageを（あれば）  指す

  PageEntry
  pageIdとfidxのペア。それぞれ8バイトなので16バイト

  fix
  ファイルの先頭から何番目のページなのかを表す。0が入っていると空きエントリ。

  vidx
  PageEntryの先頭からのインデックス（次のPEPageを指すエントリは含まない）。
  このvidxがわかればPageEtnryがわかり、そうすればfidxがわかる。
*/

using int_pageid_t = int64_t;
// int64_tを想定したコードになっているが、読みやすさの為にaliasを作っておく。
using int_vidx_t = int64_t;
using int_fidx_t = int64_t;

struct PageEntryData
{
  int_pageid_t _pageId;
  int_fidx_t _fidx;
};

// v1: BM_ADD2追加に伴うブレンドモード互換対応
constexpr uint8_t PXV_FORMAT_VERSION = 1;
// constexpr int PXV_PAGE_SIZE=4*1024;
// constexpr int PXV_PAGE_SIZE=256*1024;
// constexpr int PXV_PAGE_SIZE=16*1024;
constexpr int PXV_PAGE_SIZE = 64 * 1024;
constexpr int ENTRY_SIZE=sizeof(PageEntryData); // byte, 16
constexpr int MAX_PEPAGE_ENTRY_NUM_INCLUDE_NEXT = PXV_PAGE_SIZE/ENTRY_SIZE;
// 最初のエントリは次のPEPageを指すので除外
constexpr int MAX_PEPAGE_ENTRY_NUM = MAX_PEPAGE_ENTRY_NUM_INCLUDE_NEXT-1;

using RawPageData = array<uint8_t, PXV_PAGE_SIZE>;

struct Page {
  RawPageData *_pagePtr;
  Page(RawPageData *pagePtr) : _pagePtr(pagePtr) {}

  Page( const Page& src ) = default;
  Page& operator=( const Page& src ) = default;

  // 単なるコピーでもいいんだけど、nullにしておく方が便利な事もあるのでmoveを作っておく。
  Page( Page&& src ) noexcept : _pagePtr( src._pagePtr )
  {
    src._pagePtr = nullptr;
  }

  RawPageData* RawPagePtr() { return _pagePtr; }
  uint8_t* data() { return _pagePtr->data(); }
  uint8_t ReadUInt8(int at) const { return (*_pagePtr)[at]; }
  void WriteUInt8(int at, uint8_t val) { (*_pagePtr)[at] = val; }
  uint64_t ReadUInt8AsU64(int at) const { return static_cast<uint64_t>((*_pagePtr)[at]); }

private:
  template<typename T>
  T ReadXInt64(int at) const
  {
    return static_cast<T>(ReadUInt8AsU64(at+7)<<56 | ReadUInt8AsU64(at+6)<<48 | ReadUInt8AsU64(at+5)<<40 | ReadUInt8AsU64(at+4)<<32
      | ReadUInt8AsU64(at+3)<<24 | ReadUInt8AsU64(at+2)<<16 | ReadUInt8AsU64(at+1)<<8 | ReadUInt8AsU64(at));
  }
public:

  // アラインメントに依存しないint64_tの読み書き。ただしExtractPackedTile2がリトルエンディアンを決め打ちするのでリトルエンディアンにしておく。
  int64_t ReadInt64(int at) const
  {
    return ReadXInt64<int64_t>(at);
  }

  uint64_t ReadUInt64(int at) const
  {
    return ReadXInt64<uint64_t>(at);
  }

  void WriteUInt64(int at, uint64_t val)
  {
    WriteUInt8(at+7, (0xff & (val>> 56)));
    WriteUInt8(at+6, (0xff & (val >> 48)));
    WriteUInt8(at+5, (0xff & (val >> 40)));
    WriteUInt8(at+4, (0xff & (val >> 32)));
    WriteUInt8(at+3, (0xff & (val >> 24)));
    WriteUInt8(at+2, (0xff & (val >> 16)));
    WriteUInt8(at+1, (0xff & (val >> 8)));
    WriteUInt8(at, (val & 0xff));
  }

  void WriteInt64(int at, int64_t val)
  {
    WriteUInt64(at, static_cast<uint64_t>(val));
  }

};

/*
 あるページの特定のオフセットをページエントリとして扱うアクセサ。
 ページエントリはpageIdとfidxのペアを持つもの。
 そしてこのPageEntryのインデックスがvidxとなる。
 （ただし次のPEPageを指すエントリは含まない）

 コピーもmoveもデフォルトで。
*/
class PageEntry
{
  Page& _page;
  int _pos;
public:
  PageEntry(Page& page, int at) : _page(page), _pos(at) {}

  int_pageid_t GetPageId() const { return _page.ReadInt64(_pos); }
  void SetPageId(int_pageid_t newId) { return _page.WriteInt64(_pos, newId); }

  int_fidx_t GetFidx() const { return _page.ReadInt64(_pos+sizeof(int_pageid_t)); }
  void SetFidx(int_fidx_t newFidx) { _page.WriteInt64(_pos+sizeof(int_pageid_t), newFidx); }

  void SetPageIdFidx(int_pageid_t newPid, int_fidx_t newFidx)
  {
    SetPageId(newPid);
    SetFidx(newFidx);
  }
};

// const版。書き込みは出来ない。
class CPageEntry
{
  const Page& _page;
  int _pos;
public:
  CPageEntry(const Page& page, int at) : _page(page), _pos(at) {}

  int_pageid_t GetPageId() const { return _page.ReadInt64(_pos); }
  int_fidx_t GetFidx() const { return _page.ReadInt64(_pos+sizeof(int_pageid_t)); }
};


/*
PageEntryだけを含んだページ（PageEntryPageの略）。masterから溢れたメタデータのページ。
pagePtrは参照するだけでUnmapなどはしない。
*/
class PEPage
{
  Page _page;
  /*
    ページは以下の形式。
    array<PageEntryData, MAX_PEPAGE_ENTRY_NUM_INCLUDE_NEXT> pageEntries;
  */

public:
  PEPage(RawPageData *pagePtr) : _page(pagePtr) {}

  PageEntry NextPageEntry() { return PageEntry(_page, 0); }
  int_fidx_t NextPageFidx() const { return CPageEntry(_page, 0).GetFidx(); }
  PageEntry PageEntryInside(int64_t internalVIndex) { return PageEntry(_page, static_cast<int>( sizeof(PageEntryData)*(internalVIndex+1) )); }
  const CPageEntry CPageEntryInside(int64_t internalVIndex) const { return CPageEntry(_page, static_cast<int>( sizeof(PageEntryData)*(internalVIndex+1) )); }

  RawPageData* PagePtr() { return _page.RawPagePtr(); }
};


constexpr int MASTER_PAGE_NUM=2;
constexpr int MASTER_HEADER_SIZE=4;
constexpr int MAX_MASTER_ENTRY_NUM_INCLUDE_NEXT=(PXV_PAGE_SIZE-sizeof(int_pageid_t)-MASTER_HEADER_SIZE)/ENTRY_SIZE;
constexpr int MAX_MASTER_ENTRY_NUM=MAX_MASTER_ENTRY_NUM_INCLUDE_NEXT-1;


constexpr int MASTER_ENTRY_BEGIN = MASTER_HEADER_SIZE+sizeof(int_pageid_t);
class MasterPage
{
  int _fidx;
  Page _page;

  /*
  ページの中身は以下の形式。
  struct MasterPageData 
  {
    char magic[3] = "pxv"
    uint8_t version = PXV_FORMAT_VERSION

    int_pageid_t pageId;
    array<PageEntry, MAX_MASTER_ENTRY_NUM_INCLUDE_NEXT> pageEntries;
  };
  */


public:
  MasterPage(int file_idx, RawPageData *pagePtr) : _fidx(file_idx), _page(pagePtr) {}

  // 新規ファイルのケース
  MasterPage(int_pageid_t pid, int file_idx, RawPageData *pagePtr) : _fidx(file_idx), _page(pagePtr)
  {
    SetPageId(pid);
    // マジックを埋める
    _page.WriteUInt8(0, 'p');
    _page.WriteUInt8(1, 'x');
    _page.WriteUInt8(2, 'v');
    _page.WriteUInt8(3, PXV_FORMAT_VERSION);
  }

  MasterPage() : MasterPage(0, nullptr) {}

  MasterPage( const MasterPage& src ) noexcept = default;
  MasterPage& operator=( const MasterPage& src ) = default;

  MasterPage( MasterPage&& src ) noexcept : _fidx( src._fidx ), _page( std::move( src._page ) )  { }

  // 使わないけど、_fidxのワーニングをだまらす為（デバッグ用にもたせているメンバ）
  void DebugDump() { std::cout<< _fidx << std::endl; }

  int64_t GetFidx() const { return _fidx; }

  uint8_t GetVersion() const { return _page.ReadUInt8(3); }
  void SetVersion(uint8_t version)
  {
    _page.WriteUInt8(3, version);
  }

  int_pageid_t GetPageId() const { return _page.ReadInt64(MASTER_HEADER_SIZE); }
  void SetPageId(int_pageid_t newId) { _page.WriteInt64(MASTER_HEADER_SIZE, newId); }

  PageEntry PageEntryInside(int64_t internalVIndex) { return PageEntry(_page, static_cast<int>( MASTER_ENTRY_BEGIN+sizeof(PageEntryData)*(internalVIndex+1) )); }
  const CPageEntry CPageEntryInside(int64_t internalVIndex) const { return CPageEntry(_page, static_cast<int>( MASTER_ENTRY_BEGIN+sizeof(PageEntryData)*(internalVIndex+1) )); }
  PageEntry NextPageEntry() { return PageEntry(_page, MASTER_ENTRY_BEGIN);}
  const CPageEntry CNextPageEntry() const { return CPageEntry(_page, MASTER_ENTRY_BEGIN); }

  void CopyMasterExceptPid(MasterPage& src) 
  {
    auto savedPid = GetPageId();
    memcpy(_page.data(), src._page.data(), PXV_PAGE_SIZE);
    SetPageId(savedPid);
  }

  // Unmap用に。Unmapは外でやる。
  RawPageData* GetRawPagePtr() { return _page.RawPagePtr(); }

};


class RawPageMapper
{
  bool _isReadOnly;
  neet::N_FHANDLE _fileDesc;
  size_t _currentSize;

  RawPageData *MapBlockAsRead(int64_t fpos)
  {
    void *ptr = neet::NMMapRead(_fileDesc, fpos, PXV_PAGE_SIZE);
    return reinterpret_cast<RawPageData*>(ptr);
  }

  RawPageData *MapBlock(int64_t fpos)
  {
    if(_isReadOnly)
      return MapBlockAsRead(fpos);

    void *ptr = neet::NMMapWrite(_fileDesc, fpos, PXV_PAGE_SIZE);
    return reinterpret_cast<RawPageData*>(ptr);
  }

public:
  RawPageMapper(nstring fname, bool readOnly=false) : _isReadOnly(readOnly), _fileDesc(readOnly? neet::NOpenR( fname ) : neet::NOpenW( fname ))
  {
    using namespace neet;
    if ( neet::NIsOpenFail( _fileDesc ) )
      throw runtime_error("can't open file " + nstring_to_utf8(fname));

    auto autoClose = ScopeGuard([&] { neet::NClose(_fileDesc); });

    _currentSize = NGetFileSize(_fileDesc);

    autoClose.dismiss();
  }

  ~RawPageMapper() 
  { 
    if(_fileDesc != neet::N_FHANDLE_INVALID)
        neet::NClose(_fileDesc);
  }

  RawPageMapper(RawPageMapper&& src) noexcept :  _isReadOnly(src._isReadOnly), _fileDesc(src._fileDesc), _currentSize(src._currentSize)
  {
    src._fileDesc = neet::N_FHANDLE_INVALID;
    src._currentSize = 0;
    src._isReadOnly = false;
  }

  RawPageMapper& operator=(RawPageMapper&& src) noexcept
  {
    swap(_fileDesc, src._fileDesc);
    swap(_currentSize, src._currentSize);
    swap(_isReadOnly, src._isReadOnly);
    return *this;
  }

  RawPageData* MapPage(int_fidx_t fidx)
  {
    if(IsInFile(fidx))
    {
      return MapBlock(ToFilePos(fidx));
    }
    // cout << "fidx: " << fidx << ", ToFilePos(fidx): " << ToFilePos(fidx) << ", cur+PXV_PAGE_SIZE: " << _currentSize << "\n";
    assert( static_cast<size_t>( ToFilePos( fidx ) ) == _currentSize );
    return MapNewPage();
  }

  RawPageData* MapPageWithZeroFill(int_fidx_t fidx)
  {
    if(IsInFile(fidx))
    {
      auto page = MapPage(fidx);
      memset(page, 0, PXV_PAGE_SIZE);
      return page;
    }
    assert( static_cast<size_t>( ToFilePos( fidx ) ) == _currentSize);
    return MapNewPage();
  }

  RawPageData* MapPageAsRead(int_fidx_t fidx)
  {
    assert(IsInFile(fidx));
    return MapBlockAsRead(ToFilePos(fidx));    
  }

  RawPageData* MapNewPage()
  {
    if(_isReadOnly)
      throw runtime_error("MapNewPage called in readOnly mode.");

    if(!neet::NExtendFile(_fileDesc,_currentSize+PXV_PAGE_SIZE))
      throw runtime_error("fail to enlarge file.");
    
    int64_t fpos = _currentSize;
    // cout << "MapNewPage, fpos=" << fpos << "\n";
    _currentSize+=PXV_PAGE_SIZE;
    return MapBlock(fpos);
  }

  void UnmapPage(RawPageData* address)
  {
    neet::NMunmap((void*)address, PXV_PAGE_SIZE);
  }

  bool FSync()
  {
    return neet::NFSync( _fileDesc );
  }

  bool MSync( void *ptr, size_t len )
  {
    return neet::NMSync( ptr, len );
  }

  bool IsInFile(int_fidx_t fidx)
  { 
    if(_currentSize == 0)
      return false;
    return static_cast<size_t>( ToFilePos( fidx ) ) < _currentSize;
  }

  int64_t ToFilePos(int_fidx_t fidx)
  {
    return fidx*PXV_PAGE_SIZE;
  }

  size_t FileSize() const  { return _currentSize; }
  int64_t FilePageNum() const { return _currentSize/PXV_PAGE_SIZE; }
  bool IsReadOnly() const { return _isReadOnly; }

};

// mapしたものは全て_PEPagesに入る。
class PageEntries
{
  MasterPage* _masterPage;
  vector<PEPage> _PEPages; // this vector would not so long. copy is fast enough.
  vector<int_vidx_t> _freeListVids;
  vector<int_fidx_t> _freeListFids;
  bool _unmapped = false;

  void CollectNextFidxs(unordered_set<int_fidx_t>& usedFidxs)
  {
    auto nextFidx = _masterPage->CNextPageEntry().GetFidx();
    if(nextFidx != 0)
    {
      usedFidxs.insert(nextFidx);
      for(const auto& pepage : _PEPages)
      {
        nextFidx = pepage.NextPageFidx();
        if(nextFidx != 0)
        {
          usedFidxs.insert(nextFidx);
        }
      }
    }
  }

public: 
  PageEntries(MasterPage* master) : _masterPage(master) {}
  PageEntries() : PageEntries(nullptr) {}
  PageEntries(PageEntries &&src) noexcept : _masterPage(src._masterPage), _PEPages(std::move(src._PEPages)),
    _freeListVids(std::move(src._freeListVids)), _freeListFids(std::move(src._freeListFids))
  {
    // 使う側がオーナーシップをPageEntriesに紐づけておきたい場合があるので、クリアしてmoveされた事が分かるようにしておく。
    src._masterPage = nullptr;
  }

  // PageMapperを持たないので、デストラクタでUnmapはしない。
  // 基本的には外でマップしてそのページをぶら下げておくだけ。
  // ただ外からunmapするメソッドはつけておく。
  // これを呼んだあとはもうこのクラスはつかわないこと(_freeListFidsなどを引き上げるのはOK)
  template<typename M>
  void UnmapAllPEPages( M& pageMapper )
  {
    // 一回しか実行しない
    if (_unmapped)
      return;

    for(auto &iter : _PEPages )
    {
      pageMapper.UnmapPage(iter.PagePtr());      
    }

    _unmapped = true;
  }

  void MSync( RawPageMapper& pageMapper)
  {
    for(auto &iter : _PEPages )
    {
      pageMapper.MSync( iter.PagePtr(), PXV_PAGE_SIZE );
    }
  }

  PageEntries& operator=( PageEntries&& src ) noexcept = default;

  // ファクトリ
  // 呼び出し側がページをunmapする必要がある。
  template<typename M>
  static PageEntries MapAsRead( M& mapper, MasterPage *master )
  {
    PageEntries entries( master );
    if(0 == master->CNextPageEntry().GetFidx())
    {
      entries.CollectCurrentFreeList( mapper.FilePageNum() );
      return entries;
    }
    auto page = PEPage( mapper.MapPageAsRead( master->CNextPageEntry().GetFidx() ) );
    entries.AddConnectedPEPage( page );
    while (page.NextPageFidx() != 0)
    {
      page = PEPage( mapper.MapPageAsRead( page.NextPageFidx() ) );
      entries.AddConnectedPEPage( page );
    }
    entries.CollectCurrentFreeList( mapper.FilePageNum() );
    return entries;
  }

  // 特殊な状況でしか使わない。
  void SetMasterPage(MasterPage* master) { _masterPage = master; }

  int_fidx_t NextOfMasterFidx() const
  {
    return _masterPage->CNextPageEntry().GetFidx();
  }

  // PEPageをイテレートする. Masterは含まない.
  vector<PEPage>::const_iterator cbegin() const { return _PEPages.cbegin(); }
  vector<PEPage>::const_iterator cend() const { return _PEPages.cend(); }
  vector<PEPage>::iterator begin() { return _PEPages.begin(); }
  vector<PEPage>::iterator end() { return _PEPages.end(); }

  bool IsNoPEPages() const { return _PEPages.empty(); }
  size_t PEPageNum() const { return _PEPages.size(); }

  int64_t PEPageIndex(int_vidx_t vidx) const
  {
    // 最初の一つ目は次のPEPageを指すので飛ばす
    if(vidx < MAX_MASTER_ENTRY_NUM)
    {
      return 0;
    }
    auto remain = vidx - (MAX_MASTER_ENTRY_NUM);
    return 1 + (remain / MAX_PEPAGE_ENTRY_NUM);
  }

  int PageEntryInternalIndex(int_vidx_t vidx) const
  { 
    if(vidx < MAX_MASTER_ENTRY_NUM)
    {
      return (int)vidx;
    }
    auto remain = vidx - (MAX_MASTER_ENTRY_NUM);
    return remain % MAX_PEPAGE_ENTRY_NUM;
  }

  int_vidx_t LastAllocedVidx() const
  {
    // 最初の一つ目は次のPEPageを指すので-1-1 = -2.
    if(_PEPages.empty())
      return MAX_MASTER_ENTRY_NUM-1;

    return MAX_MASTER_ENTRY_NUM+_PEPages.size()*MAX_PEPAGE_ENTRY_NUM-1;
  }

  const CPageEntry CPageEntryAt(int_vidx_t vidx) const
  {
    auto pid = PEPageIndex(vidx);
    auto internalIdx = PageEntryInternalIndex(vidx);

    if(pid == 0)
      return _masterPage->CPageEntryInside(internalIdx);

    return _PEPages[pid-1].CPageEntryInside(internalIdx);
  }

  PageEntry PageEntryAt(int_vidx_t vidx)
  {
    auto pid = PEPageIndex(vidx);
    auto internalIdx = PageEntryInternalIndex(vidx);

    if(pid == 0)
      return _masterPage->PageEntryInside(internalIdx);

    return _PEPages[pid-1].PageEntryInside(internalIdx);
  }

  void AddConnectedPEPage(PEPage page)
  {
    _PEPages.push_back(page);
  }

  PageEntry LastNextPageEntry()
  {
    if(IsNoPEPages())
      return _masterPage->NextPageEntry();
    return _PEPages.back().NextPageEntry();
  }

  void AppendNewPEPage(RawPageData *newPage, int_fidx_t newFidx, int_pageid_t newPid)
  {
    auto firstNextVidx = LastAllocedVidx()+1;
    auto lastEntry = LastNextPageEntry();
    lastEntry.SetPageIdFidx(newPid, newFidx);
    _PEPages.push_back(PEPage(newPage));
    AddVidxRangeToFreeList(firstNextVidx, firstNextVidx+MAX_PEPAGE_ENTRY_NUM);
  }

  void CollectCurrentFreeList(int64_t file_page_num)
  {
    vector<int_fidx_t> fids;
    vector<int_vidx_t> vids;
    unordered_set<int_fidx_t> usedFidxs;
    usedFidxs.insert(0);
    usedFidxs.insert(1);

    CollectNextFidxs(usedFidxs);

    for(auto i: neet::NRange(LastAllocedVidx()+1))
    {
      const auto entry = CPageEntryAt(i);

      if(entry.GetFidx() == 0)
      {
        vids.push_back(i);
      }
      else
      {
        usedFidxs.insert(entry.GetFidx());
      }
    }

    for(auto i : neet::NRange(MASTER_PAGE_NUM, file_page_num)) 
    {
      if(usedFidxs.find(i) == usedFidxs.end())
        fids.push_back(i);
    }
    _freeListFids = std::move(fids);
    _freeListVids = std::move(vids);
  }

  void MoveFreeList(PageEntries &&src)
  {
    _freeListFids = std::move(src._freeListFids);
    _freeListVids = std::move(src._freeListVids);
  }

  // endは含まない。
  void AddVidxRangeToFreeList(int_vidx_t beg, int_vidx_t end)
  {
    // 順番は本当は関係無いがデバッグしやすいので若い順に取り出されるように逆順に詰める。
    for(auto i : neet::NRange(end-beg))
    {
      _freeListVids.push_back(end-i-1);
    }
  }

  bool IsFreeVidx(int_vidx_t vidx) const { return _freeListVids.end() != find(_freeListVids.begin(), _freeListVids.end(), vidx); }

  // 見つからなかったら-1を返す。PageMapperを持ってないので。
  int_fidx_t PopFreeFidx()
  {
    if (_freeListFids.empty())
    {
      return -1;
    }
    auto fid = _freeListFids.back();
    _freeListFids.pop_back();
    return fid;
  }

  void AddFreeVidx(int_vidx_t vidx)
  {
    _freeListVids.push_back(vidx);
  }

  bool RemoveFreeVidx(int_vidx_t vidx)
  {
    auto iter =  find(_freeListVids.begin(), _freeListVids.end(), vidx);
    if (iter == _freeListVids.end())
      return false;

    _freeListVids.erase(iter);
    return true;
  }

  // 見つからなかったら-1を返す。PageMapperを持ってないので。
  int_vidx_t PopFreeVidx()
  {
    if (_freeListVids.empty())
    {
      return -1;
    }
    auto vid = _freeListVids.back();
    _freeListVids.pop_back();
    return vid;
  }

  // UnitTestやpxvdumpなどの解析用。普通は使わない。
  const vector<int_fidx_t>& GetFreeListFids() const
  {
    return  _freeListFids;
  }

};


static_assert(sizeof(MasterPage) <= PXV_PAGE_SIZE, "MAX_MASTER_ENTRY_NUM_INCLUDE_NEXT calculation is wrong.");

/*
FSeekを使ったreadonlyなRawPageMapper的なクラス。
RPageStoreで使える。
IStreamなどを開く時に使う。
*/
struct SeekPageMapper
{
  size_t _fileSize;
  std::function<int(size_t pos, uint8_t *dat, int len)> _readAt;
  std::set<RawPageData*> _pages;

  SeekPageMapper( size_t fileSize, std::function<int(size_t pos, uint8_t *dat, int len)>&& readAtFunc ) : _fileSize(fileSize), _readAt( std::move(readAtFunc) ){ }
  SeekPageMapper(SeekPageMapper&& src) noexcept : _fileSize( src._fileSize ), _readAt( std::move(src._readAt) ), _pages( std::move(src._pages) ) {
    src._pages.clear();
  }

  int64_t FilePageNum() const { return _fileSize/PXV_PAGE_SIZE; }

  void IO_ASSERT( bool cond )
  {
    if (!cond)
      throw runtime_error("IO error");
  }

  RawPageData* MapPageAsRead( int_fidx_t fidx )
  {
    size_t fpos = ToFilePos( fidx );
    uint8_t *ptr = (uint8_t*)malloc( PXV_PAGE_SIZE );
    IO_ASSERT( ptr != nullptr );
    int readLen = _readAt( fpos, ptr, PXV_PAGE_SIZE );
    IO_ASSERT( PXV_PAGE_SIZE == readLen );
    RawPageData *ret = reinterpret_cast<RawPageData*>( ptr );
    IO_ASSERT( _pages.find(ret) == _pages.end() );
    _pages.insert( ret );
    return ret;
  }

  void UnmapPage( RawPageData* page )
  {
    auto itr = _pages.find( page );
    IO_ASSERT( itr != _pages.end() );
    free( (void*)*itr );
    _pages.erase( itr );
  }

  ~SeekPageMapper()
  {
    // デストラクタでのexceptionはいろいろな問題を引き起こすので、
    // デバッグ版でのみこのassertは有効にしておく。
#ifdef N_DEBUG
    IO_ASSERT( _pages.empty() );
#endif
  }

  size_t ToFilePos(int_fidx_t fidx)
  {
    return (size_t)(fidx*PXV_PAGE_SIZE);
  }
};


// readonly のインターフェース
class ReadablePageStore
{
public:
  virtual RawPageData* MapVPage(int_vidx_t vidx) = 0;
  virtual void UnmapVPage(RawPageData *page, int_vidx_t vidx) = 0;
  virtual ~ReadablePageStore() = default;
  
  // 対応するfidxのあるvidxかどうかを判定
  virtual bool IsMappedVidx( int_vidx_t vidx ) const = 0;

  // vidx付近のデバッグ情報を集める。主に異常終了時。
  virtual void DebInfo( int_vidx_t vidx, std::stringstream& stream ) = 0;

  // pxvフォーマットバージョン
  virtual uint8_t FormatVersion() const = 0;
};

/*
  MはRawPageMapperかSeekPageMapper。具体的には以下の条件を持つもの

  - moveコンストラクタ
  - RawPageData* MapPageAsRead( int_fidx_t fidx )
  - void UnmapPage( RawPageData* page )
  - void DebInfo( int_vidx_t vidx, std::stringstream& stream )

  また、PageEntriesのMapAsReadなども使う場合はこれに追加して以下も必要
  - int64_t FilePageNum()
*/
template<typename M>
class RPageStore : public ReadablePageStore
{
  M _pageMapper;
  PageEntries _pageEntries;
  unique_ptr<MasterPage> _master;

public:
  ~RPageStore() override
  {
    // moveされてたらnullのはず。
    if (_master.get() != nullptr)
    {
      _pageMapper.UnmapPage( _master->GetRawPagePtr() );
    }

    _pageEntries.UnmapAllPEPages( _pageMapper );
  }

  // pageEntries内のmasterのpageのunmapはこのクラスがやる。
  RPageStore( M&& mapper, PageEntries&& pageEntries, unique_ptr<MasterPage>&& masterPtr ) : _pageMapper( std::move( mapper ) ), _pageEntries( std::move( pageEntries ) ), _master( std::move( masterPtr ))
  {
  }

  RPageStore( RPageStore&& src ) = default;

  // UnitTestの為
  size_t PEPageNum() const { return _pageEntries.PEPageNum(); }

  // 呼び出しもとがunmapする必要あり
  RawPageData* MapVPage(int_vidx_t vidx) override
  {
    assert(vidx <= _pageEntries.LastAllocedVidx());
    auto entry = _pageEntries.CPageEntryAt(vidx);
    return _pageMapper.MapPageAsRead(entry.GetFidx());
  }

  void UnmapVPage(RawPageData *page, int_vidx_t vidx) override
  {
    N_UNUSED( vidx );
    _pageMapper.UnmapPage(page);
  }

  bool IsMappedVidx( int_vidx_t vidx ) const override
  {
    assert(vidx <= _pageEntries.LastAllocedVidx());

    auto entry = _pageEntries.CPageEntryAt(vidx);

    return (entry.GetFidx() != 0);
  }

  void DebInfo( int_vidx_t vidx, std::stringstream& stream ) override
  {
    auto entry = _pageEntries.CPageEntryAt( vidx );
    stream << "vidx: " << vidx << ", fidx: " << entry.GetFidx() << "\n";
    stream << "pageNum: " << _pageMapper.FilePageNum() << "\n";
  }

  uint8_t FormatVersion() const override
  {
    return _master ? _master->GetVersion() : 0;
  }

};

struct RPageStoreFactory
{
  template<typename M>
  static unique_ptr<MasterPage> MapLatestMaster( M& mapper )
  {
    auto page0 = mapper.MapPageAsRead(0);
    auto page1 = mapper.MapPageAsRead(1);
    unique_ptr<MasterPage> master0 { new MasterPage{ 0, page0 } };
    unique_ptr<MasterPage> master1  {new MasterPage{ 1, page1 } };
    auto isPageId0Higher = master0->GetPageId() > master1->GetPageId();
    if (isPageId0Higher)
    {
      mapper.UnmapPage( page1 );
      return master0;
    }
    else
    {
      mapper.UnmapPage( page0 );
      return master1;
    }
  }
  
  template<typename M>
  static unique_ptr<RPageStore<M>> CreateNoCheck( M&& mapper )
  {
    auto masterPtr = MapLatestMaster( mapper );
    return CreateWith( std::move( mapper ), std::move( masterPtr ));
  }

  static unique_ptr<RPageStore<RawPageMapper>> Create( RawPageMapper&& mapper )
  {
    if (mapper.FileSize() == 0)
    {
      throw runtime_error("Open empty file with readOnly mode.");
    }

    return CreateNoCheck( std::move( mapper ) );
  }

  // 壊れファイル解析用
  template<typename M>
  static unique_ptr<RPageStore<M>> CreateWith( M&& mapper, unique_ptr<MasterPage>&& masterPtr )
  {
    auto pageEntries = PageEntries::MapAsRead( mapper, masterPtr.get() );
    return unique_ptr<RPageStore<M>>( new RPageStore<M>( std::move(mapper), std::move( pageEntries ), std::move( masterPtr ) ) );
  }
};


/*
書き込みを行うPageStore。
ActiveMaster: 現在のメモリ上のマスター。まだcommitされてないので普通はpageIdが小さい方（readOnlyの場合は大きい方だが）
LastCommitedMaster: 最後にvalidだと保証されているマスター。pageIdが大きい方。
*/
class PageStore : public ReadablePageStore
{
  int_pageid_t _lastPageId = 1;
  RawPageMapper _pageMapper;
  MasterPage _master0;
  MasterPage _master1;
  bool _isPageId0Higher;
  PageEntries _pageEntries;
  neet::NMutex _mutex;

  int_pageid_t NewPageId() { return _lastPageId++; }
  void SetNextNewPageId( int_pageid_t newPageId ) { _lastPageId = newPageId; }

  MasterPage& ActiveMaster()
  { 
    return _isPageId0Higher ? _master1 : _master0; 
  }

  int_fidx_t PopFreeFidx(PageEntries& pageEntries)
  {
    auto fid = pageEntries.PopFreeFidx();
    if (fid == -1)
    {
      // フリーなfidは無い。ファイルの末尾に新しいページを足す.
      // cout << _pageMapper.FilePageNum() << "\n";
      return _pageMapper.FilePageNum();
    }
    return fid;
  }

  /*
  srcEntriesはreadonlyなものか、writableだがこれからcommitして次のwritableな物に移行するタイミングの物。
  どちらにせよMapされている事を前提とし、中でunmapも行う。
  */
  void CopyAndMapPEPagesAsWrite(MasterPage &master, PageEntries &&srcEntries)
  {
    int_fidx_t newFid;
    RawPageData* newPage;
    auto masterNextEntry = master.NextPageEntry();

    PageEntries newPE(&master);

    if(!srcEntries.IsNoPEPages())
    {
      auto firstPEPageFid = srcEntries.NextOfMasterFidx();
      assert(firstPEPageFid != 0);

      tie(newFid, newPage) = CopyAsWriteMap(firstPEPageFid, srcEntries);
      masterNextEntry.SetPageIdFidx(NewPageId(), newFid);

      for(const auto& iter : srcEntries)
      {
        /*
          iterが最後のページを指している時はNextPageFidx()は0のはず。
          最初は最後のページ以外という条件（次のページがある）。
        */
        if(iter.NextPageFidx() != 0)
        {
          PEPage cur(newPage);

          tie(newFid, newPage) = CopyAsWriteMap(iter.NextPageFidx(), srcEntries);

          auto nextEntry = cur.NextPageEntry();

          nextEntry.SetPageIdFidx(NewPageId(), newFid);
          newPE.AddConnectedPEPage(cur);
        }
        else // 最後のページ
        {
          PEPage cur(newPage);
          newPE.AddConnectedPEPage(cur);
        }
      }
    }
    _pageEntries = std::move( newPE );

    // unmap readonly (or prev) PEPages.
    srcEntries.UnmapAllPEPages( _pageMapper );

    _pageEntries.MoveFreeList(std::move(srcEntries));
  }


  // 呼び出し側がnewFidページをunmapする必要あり。
  pair<int_fidx_t, RawPageData*> CopyAsWriteMap(int_fidx_t oldFid, PageEntries& pageEntries)
  {
    auto newFid = PopFreeFidx(pageEntries);
    auto oldPage = _pageMapper.MapPageAsRead(oldFid);
    auto newPage = _pageMapper.MapPage(newFid);

    memcpy(newPage, oldPage, PXV_PAGE_SIZE);
    _pageMapper.UnmapPage(oldPage);
    return pair<int_fidx_t, RawPageData*>(newFid, newPage);
  }

  RawPageData* MapNewVPage(PageEntry& entry)
  {
    auto newFid = PopFreeFidx(_pageEntries);
    auto page = _pageMapper.MapPageWithZeroFill(newFid);

    entry.SetPageIdFidx(NewPageId(), newFid);
    return page;
  }

  void CreateNewPEPage()
  {
    auto newFid = PopFreeFidx(_pageEntries);
    auto page = _pageMapper.MapPageWithZeroFill(newFid);
    _pageEntries.AppendNewPEPage(page, newFid, NewPageId());
  }

public:
  ~PageStore() override
  {
    // unmap master.
    _pageMapper.UnmapPage( _master0.GetRawPagePtr() );
    _pageMapper.UnmapPage( _master1.GetRawPagePtr() );

    // 重複呼び出しは合法（無視される）
    _pageEntries.UnmapAllPEPages( _pageMapper );
  }
  PageStore(RawPageMapper&& mapper) : _pageMapper(std::move(mapper))
  {
    assert( !_pageMapper.IsReadOnly() );

    if (_pageMapper.FileSize() == 0)
    {
      // init.
      /*
        regard _master0 as last commited page. (with zero page entry)
        Active master now becomes _master1.
      */
      _master0 = {NewPageId(), 0, _pageMapper.MapNewPage()};
      _master1 = {0, 1, _pageMapper.MapNewPage()};
      _isPageId0Higher = true;
      _pageEntries = PageEntries(&ActiveMaster());
      _pageEntries.AddVidxRangeToFreeList(0, MAX_MASTER_ENTRY_NUM);
    }
    else
    {
      // load.
      _master0 = {0, _pageMapper.MapPage(0)};
      _master1 = {1, _pageMapper.MapPage(1)};
      _isPageId0Higher = _master0.GetPageId() > _master1.GetPageId();
      SetNextNewPageId( std::max( _master0.GetPageId(), _master1.GetPageId() ) + 1 );

      auto pageEntries = PageEntries::MapAsRead( _pageMapper, &LastCommitedMaster() );

      auto& activeMaster = ActiveMaster();
      auto& last = LastCommitedMaster();
      assert(&activeMaster != &last);

      activeMaster.CopyMasterExceptPid(last);


      if(pageEntries.IsNoPEPages())
      {
        // no need to unmap and CopyAsWriteMap.
        _pageEntries = std::move( pageEntries );
        _pageEntries.SetMasterPage(&ActiveMaster());
        return;
      }
      CopyAndMapPEPagesAsWrite(ActiveMaster(), std::move(pageEntries));
    }
  }


  // UnitTestの為
  size_t PEPageNum() const { return _pageEntries.PEPageNum(); }

  // UnitTestの為publicに
  MasterPage& LastCommitedMaster() { return _isPageId0Higher? _master0: _master1; }
  const MasterPage& LastCommitedMaster() const { return _isPageId0Higher? _master0: _master1; }

  int_pageid_t LastCommitedPageId() const { return LastCommitedMaster().GetPageId(); }

  // 呼び出しもとがunmapする必要あり
  RawPageData* MapVPageAsRead(int_vidx_t vidx)
  {
    assert(vidx <= _pageEntries.LastAllocedVidx());
    auto entry = _pageEntries.CPageEntryAt(vidx);
    return _pageMapper.MapPageAsRead(entry.GetFidx());
  }

  // 指定されたvidxをフリーリストに戻す。
  void ReleaseVidx(int_vidx_t vidx)
  {
    auto entry = _pageEntries.PageEntryAt(vidx);
    entry.SetFidx(0);
    _pageEntries.AddFreeVidx(vidx);
  }

  // UnitTest用
  bool IsFreeVidx(int_vidx_t vidx) const { return _pageEntries.IsFreeVidx(vidx); }

  /*
    これだけマルチスレッドアクセスを保護する。
    vidxを取り出してそれをPXVInfoに保存する所と、Streamをwriteする所の両方でこれが使われて、
    それをシングルスレッドに限定するのが難しいので。
  */
  int_fidx_t PopFreeVidx()
  {
    neet::NLockGuard lock( _mutex );
    auto vidx = _pageEntries.PopFreeVidx();
    if (vidx != -1)
      return vidx;

    CreateNewPEPage();
    vidx = _pageEntries.PopFreeVidx();
    
    return vidx;
  }

  /*
    Useはmapした上にvidxを_freeVidxから取り除く。
    vidxはフリーじゃないといけない。
  */
  RawPageData* UseNewVPage(int_vidx_t vidx)
  {
    auto success = ReserveVidx(vidx);
    N_UNUSED( success );
    assert(success);
    return MapVPage(vidx);
  }

  /*
    フリーなvidxを使われているとして処理する。
    vidxはフリーじゃないといけない。失敗すると一応falseを返す。
  */
  bool ReserveVidx(int_vidx_t vidx)
  {
    assert(vidx <= _pageEntries.LastAllocedVidx());
    return _pageEntries.RemoveFreeVidx(vidx);
  }

  bool IsMappedVidx( int_vidx_t vidx ) const override
  {
    assert(vidx <= _pageEntries.LastAllocedVidx());

    auto entry = _pageEntries.CPageEntryAt(vidx);

    return (entry.GetFidx() != 0);
  }

  RawPageData* MapVPage(int_vidx_t vidx) override
  {
    assert(vidx <= _pageEntries.LastAllocedVidx());

    auto entry = _pageEntries.PageEntryAt(vidx);

    // 空きページ。
    if(entry.GetFidx() == 0)
    {
      return MapNewVPage(entry);
    }

    // 生きてるページ。
    // Copyしてwritableの方を返す
    auto oldFid = entry.GetFidx();

    int_fidx_t newFid;
    RawPageData* newPage;
    tie(newFid, newPage) = CopyAsWriteMap(oldFid, _pageEntries);

    entry.SetPageIdFidx(NewPageId(), newFid);
    return newPage;
  }

  void UnmapVPage(RawPageData *page, int_vidx_t vidx) override
  {
    N_UNUSED( vidx );
    _pageMapper.UnmapPage(page);
  }

  /*
    次のmasterのセットアップをせずコミットだけする。
    このメソッドを呼び出したあとはstoreはデストラクタ呼び出し以外は使わない事。
    ツールとかなどでつかわれるもので、このあとプロセス自体死ぬのでsyncとかはしない。
  */
  void CommitAndFinalize()
  {
    _pageEntries.UnmapAllPEPages( _pageMapper );

    auto& curMaster = ActiveMaster();
    curMaster.SetVersion( PXV_FORMAT_VERSION );
    curMaster.SetPageId(NewPageId());
  }

  void Commit()
  {
    auto& curMaster = ActiveMaster();
    auto& nextMaster = LastCommitedMaster();

    _pageMapper.FSync();
    _pageEntries.MSync( _pageMapper );

    curMaster.SetVersion( PXV_FORMAT_VERSION );
    curMaster.SetPageId(NewPageId());
    _pageMapper.MSync( curMaster.GetRawPagePtr(), PXV_PAGE_SIZE );


    _isPageId0Higher = !_isPageId0Higher;
    nextMaster.CopyMasterExceptPid(curMaster);

    auto prevPageEntries = std::move(_pageEntries);
    prevPageEntries.CollectCurrentFreeList(_pageMapper.FilePageNum());
    // この中でprevPageEntriesのunmapも行う。
    CopyAndMapPEPagesAsWrite(nextMaster, std::move(prevPageEntries));
  }

  void DebInfo( int_vidx_t vidx, std::stringstream& stream ) override
  {
    auto entry = _pageEntries.CPageEntryAt( vidx );
    stream << "vidx: " << vidx << ", fidx: " << entry.GetFidx() << "\n";
    stream << "pageNum: " << _pageMapper.FilePageNum() << "\n";
  }

  uint8_t FormatVersion() const override
  {
    return LastCommitedMaster().GetVersion();
  }

  // 内部解析用。普通は使わない。
  PageEntries& GetPageEntries()
  {
    return  _pageEntries;
  }


};

class ReadOnlyPageStoreAdapter : public ReadablePageStore
{
  PageStore &_store;
public:
  ~ReadOnlyPageStoreAdapter() override = default;
  ReadOnlyPageStoreAdapter( PageStore &store ) : _store( store ) {}
  ReadOnlyPageStoreAdapter( ReadOnlyPageStoreAdapter&& src ) : _store( src._store ) {}
  
  RawPageData* MapVPage(int_vidx_t vidx) override { return _store.MapVPageAsRead(vidx); }
  void UnmapVPage(RawPageData *page, int_vidx_t vidx) override { _store.UnmapVPage(page, vidx); }
  bool IsMappedVidx( int_vidx_t vidx ) const override { return _store.IsMappedVidx( vidx ); }
  void DebInfo( int_vidx_t vidx, std::stringstream& stream ) override { _store.DebInfo( vidx, stream ); }
  uint8_t FormatVersion() const override { return _store.FormatVersion(); }

};

} // namespace pagestore


#endif
