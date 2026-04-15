// Copyright 2026 PGN Inc. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

/* -*- coding: utf-8 -*- マルチバイト */

#ifndef MANGA_FILE_PXV_PSREAM_H_
#define MANGA_FILE_PXV_PSREAM_H_

/*
  PXVフォーマットのPageStreamのレイヤ（下から二番目のレイヤ）
  ヘッダのみでincludeして使う。
  このファイルはpxv実装の内部で使うだけで、外部からはmanga_file_pxv.hだけをincludeすれば十分。
*/

#include "manga_file_pxv_pstore.hpp"
// for debug info.
#include <stdexcept>
#include <sstream> 
#include <iomanip>

namespace neet
{

/*
  PXVのOpenとSaveで共通のエラー。
  メモリ不足、ファイルが壊れている、本来来るはずの無い分岐に入る（なにかのバグ）などはすべてこのエラーとしてthrowされる。
*/
struct PXVError : public std::runtime_error
{
  PXVError( const std::string& msg ) : std::runtime_error( msg ) {}
};

}

namespace pagestream {


enum class StreamType : uint8_t
{
  UNINITIALIZE = 0,
  SMALL,
  LARGE
};

using pagestore::PXV_PAGE_SIZE;
using pagestore::RawPageData;
using pagestore::int_vidx_t;

constexpr int SMALL_STREAM_BINARY_SIZE = PXV_PAGE_SIZE- (1+sizeof(uint64_t));

// type, size, nextの３つを引いてある。
constexpr int HEAD_VIDX_SIZE = (PXV_PAGE_SIZE/sizeof(int_vidx_t))-3;

// 先頭はnextへのvidxだから-1する。
constexpr int VIDXPAGE_VIDX_SIZE = (PXV_PAGE_SIZE/sizeof(int_vidx_t)) -1;

static_assert(1+sizeof(uint64_t)+ sizeof(int_vidx_t) +sizeof(int_vidx_t)*HEAD_VIDX_SIZE <= PXV_PAGE_SIZE, "Too large HEAD_VIDX_SIZE");
static_assert(1+sizeof(uint64_t)+SMALL_STREAM_BINARY_SIZE <= PXV_PAGE_SIZE, "Too large SMALL_STREAM_BINARY_SIZE");

class StreamHeadPage
{
  pagestore::Page _page;

  /*
  ページの中身はStreamTypeに応じて以下の二通り（alignmentは無しで詰めて）。
  
  struct SmallStreamData
  {
    StreamType _type;
    uint64_t _size;
    array<uint8_t, SMALL_STREAM_BINARY_SIZE> _binary;
  };

  struct LargeStreamData
  {
    StreamType _type;
    uint64_t _size;
    int_vidx_t _nextVidx;
    array<int_vidx_t, HEAD_VIDX_SIZE-1> _vidxArray;
  };
  */
 public:
  StreamHeadPage(RawPageData* pagePtr) : _page(pagePtr) {}
  StreamHeadPage(StreamHeadPage&& src) noexcept
   : StreamHeadPage(src._page.RawPagePtr()) {}

  // 新規の場合の初期化。どちらのケースか明示的にわかるようにしておく。
  void Init()
  {
    SetStreamType(StreamType::SMALL);
    SetSize(0);
    SetNextVidx(-1);
  }


  StreamType GetStreamType() const { return static_cast<StreamType>(_page.ReadUInt8(0)); }
  void SetStreamType(StreamType stype) { _page.WriteUInt8(0, static_cast<uint8_t>(stype)); }
  bool IsSmallStream() const { return GetStreamType() == StreamType::SMALL; }

  uint64_t GetSize() const { return _page.ReadUInt64(1); }
  void SetSize(uint64_t newSize) { _page.WriteUInt64(1, newSize); }
  RawPageData* GetRawPageData() { return _page.RawPagePtr(); }

  // ---- SmallStreamのAPI -----
  // STREAM_SMALL_BINARY_SIZEの配列
  uint8_t* SmallBinaryData() { return _page.data() + 1+ sizeof(uint64_t); }

  // ----- LargeStreamのAPI ------

  // 無い場合は-1。
  int_vidx_t GetNextVidx() const { return _page.ReadInt64(1+sizeof(uint64_t)); }
  void SetNextVidx(int_vidx_t newVidx) { _page.WriteInt64(1+sizeof(uint64_t), newVidx); }

  int_vidx_t GetVidxAt(int idx) const { return _page.ReadInt64(1+((int)sizeof(uint64_t))*(2+idx)); }
  void SetVidxAt(int idx, int_vidx_t newVidx) { _page.WriteInt64(1+((int)sizeof(uint64_t))*(2+idx), newVidx); }

};

class StreamVidxPage
{
  pagestore::Page _page;
  // ページの中身は
  // int_vidx_t _nextVidx;
  // array<int_vidx_t, VIDXPAGE_VIDX_SIZE> _vindices;

public:
  StreamVidxPage(RawPageData* pagePtr) : _page(pagePtr) { }

  // 新しいページでの初期化。コンストラクタのケースよりレアなのでメソッドにしておく。
  void Init() { SetNextVidx(-1); }

  // 無い場合は-1。
  int_vidx_t GetNextVidx() const { return _page.ReadInt64(0); }
  void SetNextVidx(int_vidx_t newVidx) { _page.WriteInt64(0, newVidx); }

  int_vidx_t GetVidxAt(int internalPos) const { return _page.ReadInt64(((int)sizeof(int_vidx_t))*(1+internalPos)); }
  void SetVidxAt(int internalPos, int_vidx_t newVidx) { _page.WriteInt64(((int)sizeof(int_vidx_t))*(1+internalPos), newVidx); }

  RawPageData* RawPagePtr() { return _page.RawPagePtr(); }
};

struct StreamVidxesIterator;

// Read関連しか使わないStreamVidxes。
// ただサブクラスではWrte用のPageStoreが渡ってくる事はあり、
// その場合はMapする時にCopyして新規ページを作る。
class RStreamVidxes
{
  friend StreamVidxesIterator end(const RStreamVidxes& vidxes);

protected:
  pagestore::ReadablePageStore& _rstore;
  StreamHeadPage* _head;
  vector<StreamVidxPage> _vidxPages;

  void CollectVidxPages()
  {
    if(_head->IsSmallStream())
      return;

    if(_head->GetNextVidx() == -1)
      return;
    
    auto nextVidx = _head->GetNextVidx();
    // 理論上0でもいいんだが実用上はPXVINFO_VIDXが0なのでここが0はありえない。
    // マイナスはバグ。
    AssertInit( nextVidx > 0, nextVidx );
    while(nextVidx != -1)
    {
      auto vidxPage = StreamVidxPage(_rstore.MapVPage(nextVidx));
      nextVidx = vidxPage.GetNextVidx();
      _vidxPages.push_back(vidxPage);
    }
  }

  void AssertInit( bool cond, int_vidx_t vidx )
  {
    if (cond)
      return;
    
    // headのnextVidxが0という異常事態。
    // このケースはたびたび発生していて手がかりが少ないので、なるべく多くの情報を集めるようにする。
    // headの最初の64バイトをダンプ。
    // StreamType: 1
    // size: 8
    // nextVidx 8 (largeの場合)
    auto page = _head->GetRawPageData();
    std::stringstream ss;
    ss << "Invalid nextVidx: 0x" << std::hex << vidx << "\n";
    ss << "head: ";
    for( int i = 0; i < 64; i++ )
    {
      ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>( page->at( i ) ) << " ";
    }
    ss << "\n";
    throw neet::PXVError( ss.str() );
  }

public:
  RStreamVidxes(pagestore::ReadablePageStore& store, StreamHeadPage* head) : _rstore(store), _head(head)
  {
    CollectVidxPages();
  }
  RStreamVidxes(RStreamVidxes&& src) noexcept : _rstore(src._rstore), _head(src._head)
  {
    src._head = nullptr;
  }

  // StreamWriterなどのmoveコンストラクタで参照先のheadが移動してしまった時に更新する用。特殊な用途なので注意して使う。
  void _UpdateHead(StreamHeadPage* head)
  {
    _head = head;
  }

  int_vidx_t GetVidxAt(int pos) const
  {
    assert(!_head->IsSmallStream());
    if(pos < HEAD_VIDX_SIZE)
      return _head->GetVidxAt(pos);
    auto target = pos - HEAD_VIDX_SIZE;
    auto vpageAt = target/VIDXPAGE_VIDX_SIZE;
    auto vpageInside = target%VIDXPAGE_VIDX_SIZE;
    return _vidxPages[vpageAt].GetVidxAt(vpageInside);
  }

  /*
  VidxPageはこのクラスでMapされるのでここでUnmapする。_headは外でmapされて渡ってくるので関知しない。
  */
  ~RStreamVidxes()
  {
    // moveされたケース。
    if (_head == nullptr)
      return;;

    if (_head->IsSmallStream())
      return;

    auto nextVidx = _head->GetNextVidx(); 
    for(auto& page : _vidxPages)
    {
      auto cur = nextVidx;
      nextVidx = page.GetNextVidx();

      _rstore.UnmapVPage(page.RawPagePtr(), cur);
    }
  }

  vector<int_vidx_t> CollectVidxPageVidxes()
  {
    vector<int_vidx_t> vidxes;
    if (_head->IsSmallStream())
      return vidxes;
    auto nextVidx = _head->GetNextVidx(); 
    if (nextVidx == -1)
      return vidxes;
    vidxes.push_back(nextVidx);
    for(auto& page : _vidxPages)
    {
      nextVidx = page.GetNextVidx();
      if(nextVidx != -1)
        vidxes.push_back(nextVidx);
    }
    return vidxes;
  }

  // Test目的でpublicにしておく。
  /*
    使われているデータページの数。
    有効なvidxの数でもある。
    SMALLの時はHeadにデータが含まれるので0を返す。
    Largeの時は1以上の数字。

    0からDataPageNumまでvidxを取っていけば、確保されてるデータページのvidxが順番に取れる。
  */
  int DataPageNum() const
  {
    if(_head->IsSmallStream())
      return 0;

    auto size = _head->GetSize();
    auto pageNum = (size+PXV_PAGE_SIZE-1)/PXV_PAGE_SIZE;

    // 64K*2G = 128テラでオーバーフロー。十分でしょう。
    return (int)pageNum;
  }

  // ツール用。このストリームでつかわれているページ数。
  int PageNum() const
  {
    auto dpnum = DataPageNum();
    if (dpnum == 0)
      return 1; // head pageだけ.
    return static_cast<int>( dpnum + _vidxPages.size() + 1 ); // +1はheadページ。ページ数がintを超える事は無い
  }


  /*
  確保されている中で最後のvidxを返す。
  */
  int_vidx_t LastVidx() const
  {
    auto lastPos = DataPageNum()-1;
    return GetVidxAt(lastPos);
  }

  // for unittest
  vector<StreamVidxPage>& VidxPages() { return _vidxPages; }


};

class StreamVidxes : public RStreamVidxes
{
  pagestore::PageStore& _store;

  // 最初の_nextVidxPosInPageをheadから計算する。
  // 次がHEAD_VIDX_SIZEやVIDXPAGE_VIDX_SIZEになる事はある。
  // 次が0になる事はSmallStream以外では無い。
  int CalcNextVidxPosInPage() const
  {
    if(_head->IsSmallStream())
      return 0;

    auto pageNum = DataPageNum();
    if(pageNum < HEAD_VIDX_SIZE)
      return pageNum+1;
    
    pageNum -= HEAD_VIDX_SIZE;
    return (pageNum%VIDXPAGE_VIDX_SIZE)+1;
  }


  //  _vidxPages.back()のページの中で何番目のエントリまで使ったかを表す。次のページに行く時0にリセットされる。
  int _nextVidxPosInPage;

  // 最後のページが_headの時と_vidxPages.back()の時の違いをここで吸収する。
  void SetNextVidxAtLast(int_vidx_t nextVidx)
  {
    if(_vidxPages.empty())
    {
      _head->SetNextVidx(nextVidx);
    }
    else
    {
      _vidxPages.back().SetNextVidx(nextVidx);
    }
  }
  void SetVidxAtLastPage(int innerPageIndex, int_vidx_t vidx)
  {
    if(_vidxPages.empty())
    {
      _head->SetVidxAt(innerPageIndex, vidx);      
    }
    else
    {
      _vidxPages.back().SetVidxAt(innerPageIndex, vidx);
    }
  }



public:
  StreamVidxes( pagestore::PageStore& store, StreamHeadPage* head ) : RStreamVidxes( store, head ), _store(store)
  {
    _nextVidxPosInPage = CalcNextVidxPosInPage();
  }
  // srcのmoveでは_storeはクリアしない。base classのinitializationを先にやる必要があるのでこんな危うい順番になっている。
  StreamVidxes( StreamVidxes&& src ) noexcept : RStreamVidxes( std::move( src ) ), _store( src._store ), _nextVidxPosInPage( src._nextVidxPosInPage ) {}

  int_vidx_t AllocDataVidx()
  {
    if((_vidxPages.empty() &&
        _nextVidxPosInPage == HEAD_VIDX_SIZE) ||
        _nextVidxPosInPage == VIDXPAGE_VIDX_SIZE)
    {
      auto newVidxPageVidx = _store.PopFreeVidx();
      SetNextVidxAtLast(newVidxPageVidx);
      _vidxPages.push_back(_store.MapVPage(newVidxPageVidx));
      _vidxPages.back().Init();
      _nextVidxPosInPage = 0;
    } 
    auto newVidx = _store.PopFreeVidx();
    SetVidxAtLastPage(_nextVidxPosInPage, newVidx);
    _nextVidxPosInPage++;

    return newVidx;
  }

  /*
    SmallStreamからLargeStreamになるのに少し変則的なので、最初だけ特別扱いのAPIを足す。
  */
  void AddFirstVidx( int_vidx_t vpageVidx )
  {
    assert(_vidxPages.empty() && _nextVidxPosInPage == 0);
    _head->SetVidxAt(0, vpageVidx);
    _nextVidxPosInPage++;
  }
};

struct StreamVidxesIterator
{
  int _at;
  const RStreamVidxes& _vidxes;

  StreamVidxesIterator(const RStreamVidxes& vidxes, int at) : _at(at), _vidxes(vidxes) {}

  int_vidx_t operator *() const { return _vidxes.GetVidxAt(_at); }
  const StreamVidxesIterator &operator ++()
  {
    ++_at;
    return *this;
  }
  bool operator ==(const StreamVidxesIterator &other) const { return _at == other._at; }
  bool operator !=(const StreamVidxesIterator &other) const { return _at != other._at; }
};

inline StreamVidxesIterator begin(const RStreamVidxes& vidxes) { return StreamVidxesIterator(vidxes, 0); }
inline StreamVidxesIterator end(const RStreamVidxes& vidxes) { return StreamVidxesIterator(vidxes, vidxes.DataPageNum()); }

// コンストラクタでMapし、デストラクタでUnmapする
class VPage
{
  int_vidx_t _vidx;
  pagestore::ReadablePageStore& _store;
  RawPageData* _rawPageData;

public:
  VPage(pagestore::ReadablePageStore& store): _vidx(-1), _store(store),  _rawPageData(nullptr) {}
  VPage(pagestore::ReadablePageStore& store, int_vidx_t vidx) : _vidx(vidx), _store(store), _rawPageData(store.MapVPage(vidx)) {}
  VPage(VPage&& src) noexcept : _vidx(src._vidx), _store(src._store), _rawPageData(src._rawPageData)
  {
    src._rawPageData = nullptr;
    src._vidx = -1;
  }
  VPage& operator=(VPage&& src) noexcept
  {
    if(_rawPageData != nullptr) {
      _store.UnmapVPage(_rawPageData, _vidx);
    }
    _rawPageData = src._rawPageData;
    _vidx = src._vidx;

    src._rawPageData = nullptr;
    src._vidx = -1;
    return *this;
  }

  RawPageData* RawPageDataPtr() { return _rawPageData; }
  uint8_t* Data() { return reinterpret_cast<uint8_t*>(_rawPageData); }
  int_vidx_t GetVidx() const { return _vidx; }

  bool IsEmpty() const { return _rawPageData == nullptr; }

  ~VPage()
  {
    if(_rawPageData != nullptr)
    {
      _store.UnmapVPage(_rawPageData, _vidx);
    }
  } 
};

class StreamWriter
{
  pagestore::PageStore& _store;
  VPage _headPage;
  StreamHeadPage _head;
  int64_t _currentPos;
  StreamVidxes _vidxes;
  VPage _currentPage;

  StreamWriter(pagestore::PageStore& store, VPage&& headPage, StreamHeadPage&& head):
     _store(store), _headPage(std::move(headPage)), _head(std::move(head)),  _currentPos(0),
     _vidxes( store, &_head ), _currentPage(store)
  {
  }

  void GotoLargePage()
  {
    assert(_head.IsSmallStream());

    auto dataVidx = _store.PopFreeVidx();
    _currentPage = VPage(_store, dataVidx);

    if(_head.GetSize() != 0)
    {
      memcpy(_currentPage.RawPageDataPtr(), _head.SmallBinaryData(), _head.GetSize());
    }
    _head.SetStreamType(StreamType::LARGE);
    _head.SetNextVidx(-1);
    _vidxes.AddFirstVidx(dataVidx);
  }

  void EnsureCurrentPageMap()
  {
    if(_currentPage.IsEmpty())
    {
      _currentPage = VPage(_store, _vidxes.LastVidx());
    }
  }

  static void ReleasePages( pagestore::PageStore& store, StreamHeadPage& head )
  {
    vector<int_vidx_t> vpIdxes;

    { // VidxPagesをunmapしてほしいので中括弧でくくる。
      pagestore::ReadOnlyPageStoreAdapter rstore(store);

      RStreamVidxes vidxes( rstore, &head );
      vpIdxes = vidxes.CollectVidxPageVidxes();

      // freeListに入る順番がallocの逆になってしまうが、まぁいいでしょう。
      for (auto vidx : vidxes)
      {
        store.ReleaseVidx(vidx);
      }
    }

    // VidxPagesをunmapしたあとにvidxをrelease。 
    for (auto vidx: vpIdxes)
    {
        store.ReleaseVidx(vidx);
    }
  }

public:
  StreamWriter(StreamWriter&& src) noexcept : _store(src._store), _headPage( std::move( src._headPage ) ), _head( std::move( src._head ) ),
    _currentPos( src._currentPos ), _vidxes( std::move(src._vidxes) ), _currentPage( std::move( src._currentPage ) )
  {
    _vidxes._UpdateHead( &_head );
  }

  static StreamWriter CreateNewStream( pagestore::PageStore& store, int_vidx_t headVidx )
  {
    VPage headPage( store, headVidx );
    StreamHeadPage head( headPage.RawPageDataPtr() );
    head.Init();
    StreamWriter writer( store, std::move(headPage), std::move(head) );
    return writer;
  }

  static void DeleteStream( pagestore::PageStore& store, int_vidx_t headVidx )
  {
    {
      VPage headPage(store, headVidx);
      StreamHeadPage head(headPage.RawPageDataPtr());
      ReleasePages(store, head);
    }
    store.ReleaseVidx(headVidx);
  }

  /*
    headVidxVecのストリームをすべて削除する。ただし-1のエントリは無視する。
  */
  static void DeleteStreams( pagestore::PageStore& store, const std::vector<int_vidx_t>& headVidxVec  )
  {
    for (auto vidx : headVidxVec)
    {
      if (vidx != -1)
        DeleteStream( store, vidx );
    }
  }

  static StreamWriter CreateOverwriteStream( pagestore::PageStore& store, int_vidx_t headVidx )
  {
    VPage headPage(store, headVidx);
    StreamHeadPage head(headPage.RawPageDataPtr());

    ReleasePages(store, head);

    // 新規のようにやり直す。
    head.Init();
    StreamWriter writer(store, std::move(headPage), std::move(head));
    return writer;    
  }

  // NewかOverwriteかよきにはからうCreateStream。
  // どちらか分かっている時は使わない方がバグを見つけやすいので、区別をしたくない時限定で使う事
  static StreamWriter CreateStream( pagestore::PageStore& store, int_vidx_t headVidx )
  {
    if (store.IsMappedVidx( headVidx ))
    {
      return CreateOverwriteStream( store, headVidx );
    }
    else
    {
      return CreateNewStream( store, headVidx );
    }
  }

  // C++の外から使う用
  static StreamWriter* CreateNewStreamPtr( pagestore::PageStore& store, int_vidx_t headVidx )
  {
    VPage headPage( store, headVidx );
    StreamHeadPage head( headPage.RawPageDataPtr() );
    head.Init();
    return new StreamWriter( store, std::move(headPage), std::move(head) );
  }

  // C++の外から使う用
  static StreamWriter* CreateOverwriteStreamPtr( pagestore::PageStore& store, int_vidx_t headVidx )
  {
    VPage headPage(store, headVidx);
    StreamHeadPage head(headPage.RawPageDataPtr());

    ReleasePages(store, head);

    // 新規のようにやり直す。
    head.Init();
    return new StreamWriter( store, std::move(headPage), std::move(head) );
  }


  // for debug.
  StreamHeadPage &Head() { return _head; }
  StreamVidxes &Vidxes() { return _vidxes; }
  int_vidx_t CurrentVidx() const { 
    if(_head.IsSmallStream())
      return _headPage.GetVidx();
    return _currentPage.GetVidx(); 
  }

  int64_t CurrentPos() const { return _currentPos; }

  void Write(const uint8_t *src, int len)
  {
    if( _currentPos+len <= SMALL_STREAM_BINARY_SIZE )
    {
      // small page.
      memcpy(_head.SmallBinaryData()+_currentPos, src, len);
      _currentPos += len;
      _head.SetSize(_currentPos);
      return;
    }

    if( _head.IsSmallStream() )
    {
      GotoLargePage();
    }
    EnsureCurrentPageMap();

    // normal write.
    auto remain = len;
    int written = 0;
    while(remain != 0)
    {
      int writtenInPage = _currentPos%PXV_PAGE_SIZE;
      auto restInPage = PXV_PAGE_SIZE-writtenInPage;

      // 次のページに最初に書く時。この瞬間にallocする。(allocされたがposが0、という状態が無いようにlazyにalloc)
      // だが最初のページはGotoLagePageでalloc済みなのでその場合はスキップ。
      if(_currentPos != 0 && writtenInPage == 0)
      {
        auto vidx = _vidxes.AllocDataVidx();
        _currentPage = VPage(_store, vidx);
        assert(restInPage == PXV_PAGE_SIZE);
      }

      auto writeLen = min(remain, restInPage);
      memcpy(_currentPage.Data()+writtenInPage, src+written, writeLen);

      _currentPos += writeLen;
      written += writeLen;
      remain -= writeLen;
    }
    _head.SetSize(_currentPos);
  }

  void WriteUInt8(uint8_t val)
  {
    Write(&val, 1);
  }

  // PageのWiteInt64と似てる
  void WriteInt64(int64_t val)
  {
    array<uint8_t, 8> buf;
    buf[7] = (0xff & (val >> 56));
    buf[6] = (0xff & (val >> 48));
    buf[5] = (0xff & (val >> 40));
    buf[4] = (0xff & (val >> 32));
    buf[3] = (0xff & (val >> 24));
    buf[2] = (0xff & (val >> 16));
    buf[1] = (0xff & (val >> 8));
    buf[0] = (0xff & val);

    Write(buf.data(), 8);
  }

  void WriteUInt32(uint32_t val)
  {
    array<uint8_t, 4> buf;
    buf[3] = (0xff & (val >> 24));
    buf[2] = (0xff & (val >> 16));
    buf[1] = (0xff & (val >> 8));
    buf[0] = (0xff & val);

    Write(buf.data(), 4);
  }

  void FillZeroTill4ByteAlign()
  {
    array<uint8_t, 4> buf;
    if(_currentPos%4 == 0)
      return;
    buf.fill(0);
    Write(buf.data(), 4-(_currentPos%4));
    assert((_currentPos%4) == 0);
  }

  // 文字列を書く便利関数
  // 基本的にはvoid Write(const uint8_t *src, int len)を使えばなんでも書けるが、
  // 文字列の読み書きは良く使うのでつけておく。
  // 末尾のヌル文字は書かない
  void WriteString( const std::string& str )
  {
    Write( (const uint8_t*)str.c_str(), static_cast<int>( str.size() ) );
  }
};


class StreamReader
{
  pagestore::ReadablePageStore& _store;
  VPage _headPage;
  StreamHeadPage _head;
  int64_t _currentPos;
  RStreamVidxes _vidxes;

  VPage _currentPage;

  int CurrentPagePos() const { return (int)(_currentPos / PXV_PAGE_SIZE); }
  int_vidx_t CurrentPageVidx() const { return _vidxes.GetVidxAt(CurrentPagePos()); }

  uint64_t UInt64At(const array<uint8_t, 8>& buf, int pos) const { return static_cast<uint64_t>(buf[pos]); }
  uint32_t UInt32At(const array<uint8_t, 4>& buf, int pos) const { return static_cast<uint32_t>(buf[pos]); }

public:
  StreamReader(pagestore::ReadablePageStore& store, int_vidx_t headVidx): _store(store), _headPage(store, headVidx),
    _head(_headPage.RawPageDataPtr()), _currentPos(0), _vidxes(_store, &_head), _currentPage(_store)
  {
  }

  bool IsEof() const { return _currentPos == static_cast<int64_t>( _head.GetSize() ); }

  uint64_t GetSize() const
  { 
    auto size = _head.GetSize();;
    return size;
  }

  // for debug.
  StreamHeadPage &Head() { return _head; }
  int PageNum() const { return _vidxes.PageNum(); }

  size_t Read(uint8_t *outBuf, int bufSize)
  {
    assert( _currentPos <= static_cast<int64_t>(_head.GetSize()) );

    if(IsEof())
      return 0;

    auto allReadLen = static_cast<int>(min(static_cast<uint64_t>(bufSize), _head.GetSize()-_currentPos));

    if(_head.IsSmallStream())
    {
      memcpy(outBuf, _head.SmallBinaryData()+_currentPos, allReadLen);

      _currentPos += allReadLen;
      return allReadLen;
    }


    int readLen = 0;
    auto remain = allReadLen;
    while(allReadLen != readLen)
    {
      int readInPage = _currentPos%PXV_PAGE_SIZE;
      int restInPage = PXV_PAGE_SIZE-readInPage;

      // _currentPosがページの先頭にある、という事を意味する。
      // この場合はこのあとのifブロックで次のVPageに切り替わるので、
      // restInPageは次のページすべてという事になる。
      if(restInPage == 0)
      {
        restInPage = PXV_PAGE_SIZE;
      }

      auto curPageVidx = CurrentPageVidx();
      if (_currentPage.GetVidx() != curPageVidx)
      {
        _currentPage = VPage(_store, curPageVidx);
      }

      auto readOne = min(remain, restInPage);
      memcpy(outBuf+readLen, _currentPage.Data()+readInPage, readOne);

      readLen += readOne;
      _currentPos += readOne;
      remain -= readOne;
    }

    return readLen;
  }

  uint8_t ReadUInt8()
  {
    uint8_t val;
    Read(&val, 1);
    return val;
  }

  // pagestore::Pageと似てる
  int64_t ReadInt64()
  {
    array<uint8_t, 8> buf;
    Read(buf.data(), 8);
    return static_cast<int64_t>(UInt64At(buf, 7)<<56 | UInt64At(buf, 6)<<48 | UInt64At(buf, 5)<<40 | UInt64At(buf,4)<<32
      | UInt64At(buf, 3)<<24 | UInt64At(buf, 2)<<16 | UInt64At(buf, 1)<<8 | UInt64At(buf, 0));
  }

  uint32_t ReadUInt32()
  {
    array<uint8_t, 4> buf;
    Read(buf.data(), 4);
    return (UInt32At(buf, 3)<<24 | UInt32At(buf, 2)<<16 | UInt32At(buf, 1)<<8 | UInt32At(buf, 0));
  }

  // 末尾までをstd::stringとみなして読み込む便利関数
  // 基本的にはsize_t Read(uint8_t *outBuf, int bufSize)でなんでも出来るが、
  // 文字列の読み書きは簡単な設定などの読み書きであると便利なのでつけておく。
  std::string ReadString()
  {
    int remain = (int) (GetSize() - _currentPos);

    std::string ret;
    ret.resize( remain );
    Read( (uint8_t*)&ret[0], remain );
    
    return ret;
  }
};

} // namespace pagestream



#endif
