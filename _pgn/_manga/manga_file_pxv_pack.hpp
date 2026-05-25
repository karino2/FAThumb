/* -*- coding: utf-8 -*- マルチバイト */

// Copyright 2026 PGN Inc. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef MANGA_FILE_PXV_PACK_H_
#define MANGA_FILE_PXV_PACK_H_

#include "libneet_file.h"
#include "libneet_thread.h"
#include "manga_file_pxv_pstream.hpp"
#include "zstd.h"
#include <memory>
#include <vector>
#include <queue>
#include <set>

// タイル別保存関連
#include <cmath>
#include <functional>

namespace pxvpack {

/*
  packer.Getの時のvidxが、対応するfidxが0の時の例外。
  このケースはファイルが壊れているというよりはvidxが確保されたが書かれていない、
  というvidxが残ってしまっているという割と理解出来る挙動な上にたまに起こるようなので、
  headNextが0になるバグのうち復旧可能なものとして特別に扱う。
  そのケースの場合の例外(#1824)
*/
struct EmptyVidxError : public neet::PXVError
{
  EmptyVidxError( const std::string& msg ) : neet::PXVError( msg ) {}
};

using namespace std;
using namespace pagestream;
using namespace pagestore;

/*
  file_packer.{h, cpp} に似た処理をする事で、保存周りのコードを変更少なく持ってくる。

  チャンクのヘッダ。
  - uint8_t ストリームデータタイプ (無圧縮 or ZLIB or OTHER)
  - (無圧縮じゃない時のみ) uint64_t 展開後サイズ

  無圧縮の時はストリームのサイズからデータタイプのサイズ（1バイト）を引けば求まるので保存しない。
  （BeginAddDataの実装が簡単になる）
*/
constexpr int NONCOMPRESSED_HEADER_SIZE=1;
constexpr int COMPRESSED_HEADER_SIZE=1+sizeof(uint64_t);

/*
  coarese grainedな並列キュー。ただし、それまでに確保したものを全部保持するvectorとキューの二つを持つ構造なのでプールと呼んでおく。
  主にZSTD_CCtxとZSTD_DCtxのポインタを保持
  
  テンプレート引数:
  T ZSTD_CCtxかZSTD_DCtx
  F コンテキストのファクトリ
  D コンテキストを削除する関数

*/
template<typename T, T*(*F)(), size_t(*D)(T*)>
struct ContextPool
{
  using CType = T;

  std::vector<T*> _allCtx; // コンテキストはこちらに持つ
  std::queue<T*> _ctxQueue; // こちらは参照のみ。現在使えるfree list的な扱い
  neet::NMutex _mutex;

  // poolにあればそれを返す。なければfactoryで作って返す。
  T* AcquireContext()
  {
    std::unique_ptr<int> ptr;
    {
      neet::NLockGuard lock(_mutex);
      if (!_ctxQueue.empty())
      {
        T* ctx = _ctxQueue.front();
        _ctxQueue.pop();
        return ctx;
      }
    }
    // 空きが無いので作る。
    T* ctx = F();
    {
      neet::NLockGuard lock(_mutex);
      _allCtx.push_back(ctx);
    }
    return ctx;
  }

  // フリーリストに戻す
  void ReleaseContext(T* ctx)
  {
    neet::NLockGuard lock(_mutex);
    _ctxQueue.push(ctx);
  }

  ~ContextPool()
  {
    for (auto ctx : _allCtx)
    {
      D(ctx);
    }
  }
};

template<typename P>
struct CPGuard
{
  using CType = typename P::CType;

  P& _pool;
  CType* _ctx;

  CPGuard( P& pool ) : _pool( pool ), _ctx( _pool.AcquireContext() ) {}
  ~CPGuard() { _pool.ReleaseContext( _ctx ); };  
};

struct ZstdEncoder
{
  using PoolType = ContextPool<ZSTD_CCtx, ZSTD_createCCtx, ZSTD_freeCCtx>;
  PoolType _pool;

  bool Encode( void *src, int srcSize, void *outBuf, int bufMaxSize, int *outEncedSize )
  {
    CPGuard<PoolType> cguard( _pool );

    assert( ZSTD_compressBound( srcSize ) <= (size_t)bufMaxSize );
    size_t const cSize = ZSTD_compressCCtx( cguard._ctx, outBuf, bufMaxSize, src, srcSize, 1 );
    *outEncedSize = (int)cSize;
    return !ZSTD_isError(cSize);
  }

  uint8_t GetCodecType() const { return (uint8_t)CodecType; }

  static constexpr int CodecType = 3;
};



/*
  Streamのvidx。ようするにisOverwriteのフラグを持つだけ。
*/
struct SVidx
{
  int_vidx_t _vidx;
  bool _isOverwrite;

  SVidx( int_vidx_t vidx, bool isOverwrite ) : _vidx( vidx ), _isOverwrite( isOverwrite ) {}
};

//////////////////////////////////////////////////////////////////////////////
// 圧縮用
//////////////////////////////////////////////////////////////////////////////

/*
  commitされているStreamをprevに持ち、現在書き込んでいるストリームをcurrentでトラックしていく。
  差分のStreamをdeleteすることで無くなったストリームを自動的に削除する、という機能を提供する目的。
  これをストリームのGCと呼ぶ事にする。
  ストリームはいつもPXVInfoから辿れる所でしか作っていないという前提。

  #1809 の抜本的な解決策参照。
*/
struct StreamTracker
{
  std::set<int_vidx_t> _prev; // すでにファイルにCommitされているバージョンでのストリーム一覧
  std::set<int_vidx_t> _current; // 現在作成途中のストリーム一覧（これからcommitするバージョンでのストリームの一覧）

  void Clear()
  {
    _prev.clear();
    _current.clear();
  }

  void AddP( int_vidx_t vidx )
  {
    // ignore
    if (vidx == 0 || vidx == -1)
      return;
    _prev.insert( vidx );
  }

  void AddC( int_vidx_t vidx )
  {
    // ignore
    if (vidx == 0 || vidx == -1)
      return;
    _current.insert( vidx );
  }

  /*
    現在の_currentを次の_prevにし、_currentを空にする
  */
  void Commit()
  {
    _prev = std::move(_current);
    _current.clear();
  }

  /*
    _prevにあって_currentに無いvidxを返す。
  */
  std::vector<int_vidx_t> Obsolete()
  {
    std::vector<int_vidx_t> ret;
    for (auto pv : _prev)
    {
      if (_current.end() == _current.find( pv ))
        ret.push_back(pv);
    }
    return ret;
  }
};

/*
  StreamStats:
  ストリームの新規作成とdeleteの情報を保持しておくクラス。
  デバッグ目的で使う。

  StreamTrackerとの違いがややこしいのでここに解説しておく。

  StreamStatsはGCかどうかよりも一段下の低レベルなストリームの作成と削除の統計を取る。
  これはGCの時と以前のアルゴリズムのどちらでも機能する。
  特に旧来のストリームの削除や作成の挙動を調べるのに使える。
  これはあくまでデバッグ目的であってこのstatsを元に動作する機能は無い。

  StreamTrackerはGCのためにトラックする情報で、GCの時にこの情報を使う。
  このStreamTrackerによるトラッキングはGCのためにあるもので、GCの一部でもある。
  また、旧来のストリームの削除は関知しないので、旧来の挙動を調べるのには使えない。
  （旧来のロジックで削除が走ると矛盾した結果となるが、その場合はこのトラッキング自体がオフになっているはず）
*/
struct StreamStats
{
  std::vector<int_vidx_t> _created;
  std::vector<int_vidx_t> _deleted;

  void ReportNew( int_vidx_t vidx ) { _created.push_back( vidx ); }
  void ReportDelete( int_vidx_t vidx ) { _deleted.push_back( vidx ); }
  void ReportDeleteList( const std::vector<int_vidx_t>& vidxVec )
  {
    std::copy( vidxVec.begin(), vidxVec.end(), std::back_inserter( _deleted ) );
  }

  void Clear()
  {
    _created.clear();
    _deleted.clear();
  }
};

// CPackerEncodeを真似する。
class PXVPackerEncode
{
public:
  // 0はzlibが使っている。
  static const uint8_t COMP_NONE = 255;

  neet::NMutex _mutex;
  StreamTracker _tracker;
  StreamStats _sstats;

  StreamStats& SStats() { return _sstats; }
  const StreamStats& SStats() const { return _sstats; }

private:
  unique_ptr<PageStore> _pageStore;

  // 変更をしてあるがCommitしてない状態ではtrueになる。
  // 現状は実装の都合で、Writerを作ったりするとtrueになる。
  // 本当はwriteするまでdirtyでは無いが、例外的なケースで無駄に一回コミットが発生するのは大きな問題では無いので。
  bool _dirty = false;

  bool _enableGC = true;

  // デバッグ用のStreamの作成や削除の情報を保持するフラグ
  bool _enableStats = false;
  void ReportNew( int_vidx_t vidx ) { if (_enableStats) _sstats.ReportNew( vidx ); }
  void ReportDelete( int_vidx_t vidx ) { if (_enableStats) _sstats.ReportDelete( vidx ); }
  void ReportDeleteList( const std::vector<int_vidx_t>& vidxVec ){ if (_enableStats) _sstats.ReportDeleteList( vidxVec ); }

public:
  explicit PXVPackerEncode() {}
  ~PXVPackerEncode() {}

  ZstdEncoder _zstdEncoder;

  bool IsDirty() const { return _dirty; }

  bool IsOpened() const { return _pageStore.get() != nullptr; }
  int_pageid_t LastCommitedPageId() const { return _pageStore->LastCommitedPageId(); }

  void SetStreamStatsEnabled( bool isEnable ) { _enableStats = isEnable; }

  // StreamGCに削除を任せる。途中からは今の所使えないので、
  // GMangaEngineにAttachする前にEnableにする事。
  void SetStreamGCEnabled( bool isEnable ) { _enableGC = isEnable; }
  bool GCEnabled() const { return _enableGC; }
  
  bool Open( nstring filename )
  {
    try
    {
      _pageStore.reset(new pagestore::PageStore{RawPageMapper{filename.c_str()}});
      return true;
    }
    catch(runtime_error)
    {
      return false;
    }
  }

  // 現状このRStoreはWriterの書いているマスターと同じマスターを見てしまう。
  // 本来はlast commitedな方を見るべき。
  // だがまだ書いてないページを読む分には問題無くてそのユースケースしか無いので当面は気をつけてこのまま使う。
  ReadOnlyPageStoreAdapter TempRStore() { return ReadOnlyPageStoreAdapter(*_pageStore); }

  PageStore &GetPageStore() { return *_pageStore; }

  // vidxを確保する。ただAllocVidxしただけではPageEntryのFidxは0なので空と区別がつかない。
  // commit前にPageが書き込まれる必要がある。
  // トラブルの起きそうなAPIだが、今は気をつけて使う事にする。
  int_vidx_t AllocVidx()
  { 
    _dirty = true;
    auto vidx = _pageStore->PopFreeVidx();

    ReportNew( vidx ); // PackerのAllocはたぶんいつもStream作成。

    return vidx;
  }

  // WithLock版
  int_vidx_t AllocVidxWL()
  {
    neet::NLockGuard guard( _mutex );
    return AllocVidx();
  }

  bool ReserveVidx(int_vidx_t vidx) { return _pageStore->ReserveVidx(vidx); }

  // データの追加
  bool AddDataCompress( void* data, int length, int_vidx_t vidx,  bool isOverwrite )
  {
    StreamWriter writer = GetWriter( vidx, isOverwrite );

    int encavail = length + 65536;
    BYTE* encoded = (BYTE*)malloc( encavail );
    if (encoded == NULL) return false;

    auto autoFree = neet::ScopeGuard([&]{ free( encoded ); });

    int encnum;
    auto res = _zstdEncoder.Encode( data, length, encoded, encavail, &encnum );

    if (!res)
    {
      return false;
    }

    // ヘッダを書く
    writer.WriteUInt8( _zstdEncoder.GetCodecType() );
    writer.WriteInt64(length);

    writer.Write(encoded, encnum);

    return true;
  }

  bool AddDataCompress( void* data, int length, SVidx svidx )
  {
    return AddDataCompress( data, length, svidx._vidx, svidx._isOverwrite );
  }

  // XML保存用に先頭にcompとか書かないwriterを取る方法を提供。
  StreamWriter GetWriter( int_vidx_t vidx, bool isOverwrite )
  {
    _dirty = true;
    if (isOverwrite)
      return StreamWriter::CreateOverwriteStream(*_pageStore, vidx);
    return StreamWriter::CreateNewStream(*_pageStore, vidx);
  }

  StreamWriter GetWriter( SVidx svidx )
  {
    return GetWriter( svidx._vidx, svidx._isOverwrite );
  }

  // C++の外から呼ぶ用。C++からはGetWriterを使う事。
  StreamWriter* NewWriter( int_vidx_t vidx, bool isOverwrite )
  {
    _dirty = true;
    if (isOverwrite)
      return StreamWriter::CreateOverwriteStreamPtr(*_pageStore, vidx);
    return StreamWriter::CreateNewStreamPtr(*_pageStore, vidx);
  }

  /*
    StreamのDelete関連。
  */
  void DeleteStream( int_vidx_t streamVidx )
  {
    ReportDelete( streamVidx );
    StreamWriter::DeleteStream( *_pageStore, streamVidx );
  }

  // DeleteStremaのWithLock版
  void DeleteStreamWL( int_vidx_t streamVidx )
  {
    neet::NLockGuard guard( _mutex );
    DeleteStream( streamVidx );
  }

  void DeleteStreams( const std::vector<int_vidx_t>& streamVidxVec )
  {
    ReportDeleteList( streamVidxVec );
    StreamWriter::DeleteStreams( *_pageStore, streamVidxVec );
  }

  // DeleteStremasのWithLock版
  void DeleteStreamsWL( const std::vector<int_vidx_t>& streamVidxVec )
  {
    neet::NLockGuard guard( _mutex );
    DeleteStreams( streamVidxVec );
  }

  /*
    DeleteStreams1などの1のsuffixでGC以前の削除を、2でGC以後の削除を表す。
    enableGCによって片方が記録用になってもう片方が実際の削除になり、両者をデバッグ時は比較する。
  */

  // これらは開発途中で使うだけ。最終的には不要。
  std::set<int_vidx_t> _del1Vidx;
  std::set<int_vidx_t> _del2Vidx;

  // GCでのdeleteと旧方式のdeleteが一致している事を確認して、
  // その削除情報をクリアする。
  void VerifyAndClearGCDebInfo()
  {
  #ifdef QT_DEBUG
    assert( GCEnabled() );
    // _del1Vidxは-1も許すので-1は削除して比較。
    auto filterd1 = neet::FilterFn( std::vector<int_vidx_t>(_del1Vidx.begin(), _del1Vidx.end() ), [](int_vidx_t vidx) { return vidx != -1; } );
    assert( filterd1.size() == _del2Vidx.size() );
    for( auto vidx1 : filterd1 )
    {
      assert( _del2Vidx.find( vidx1 ) != _del2Vidx.end() );
    }
  #endif

    _del1Vidx.clear();
    _del2Vidx.clear();
  }

  std::insert_iterator<std::set<int_vidx_t>> Del1Inserter() { return std::inserter( _del1Vidx, _del1Vidx.end() ); }
  std::insert_iterator<std::set<int_vidx_t>> Del12nserter() { return std::inserter( _del2Vidx, _del2Vidx.end() ); }

  void DeleteStreams1( const std::vector<int_vidx_t>& vvec )
  {
    if (GCEnabled())
      std::copy( vvec.begin(), vvec.end(), Del1Inserter() );
    else
      DeleteStreams( vvec );
  }

  void DeleteStreamsWL1( const std::vector<int_vidx_t>& vvec )
  {
    if (GCEnabled())
      std::copy( vvec.begin(), vvec.end(), Del1Inserter() );
    else
      DeleteStreamsWL( vvec );
  }

  void DeleteStreamWL1( int_vidx_t vidx )
  {
    if (GCEnabled())
      _del1Vidx.insert( vidx );
    else
      DeleteStreamWL( vidx );
  }


  // データの追加 (ファイルに直接追加したい場合)
  StreamWriter BeginAddData( int_vidx_t vidx, bool isOverwrite )
  {
    auto sw = GetWriter( vidx, isOverwrite);

    // 無圧縮。圧縮はこの先で（必要なら）やる。
    sw.WriteUInt8(PXVPackerEncode::COMP_NONE);
    return sw;
  }

  // データの追加 (ファイルに直接追加したい場合)、shared_ptr版
  std::shared_ptr<StreamWriter> BeginAddDataSP( int_vidx_t vidx, bool isOverwrite )
  {
    auto sw = std::shared_ptr<StreamWriter>( NewWriter( vidx, isOverwrite) );

    // 無圧縮。圧縮はこの先で（必要なら）やる。
    sw->WriteUInt8(PXVPackerEncode::COMP_NONE);
    return sw;
  }

  std::shared_ptr<StreamWriter> BeginAddDataSP( SVidx svidx )
  {
    return BeginAddDataSP( svidx._vidx, svidx._isOverwrite );
  }

  void Commit()
  {
    _dirty = false;
    _pageStore->Commit();
  }

  // Commitされてるかどうかチェックせず問答無用でClose。
  // Openされてなくても呼べる。
  void Close()
  {
    _pageStore.reset(nullptr);
  }

  bool CommitAndClose()
  {
    _dirty = false;
    _pageStore->CommitAndFinalize();
    _pageStore.reset(nullptr);
    return true;
  }

  // GCEnabledの時はStream GCの処理をしてからCommit。
  // GCEnabledでない時は単にCommit。
  // Stream GCの時はこれを呼ぶ前にPXVInfoを書き出す処理が走っている前提（そこで_trackerのCurrentを更新するので）。
  // その前提はこのメソッドを呼び出す側が気を付ける。
  void GCAndCommit()
  {
    if (GCEnabled())
    {
      auto obsolete = _tracker.Obsolete();
      std::copy( obsolete.begin(), obsolete.end(), std::inserter( _del2Vidx, _del2Vidx.end() ) );
      DeleteStreams( obsolete );
      _tracker.Commit();
    }
    Commit();
  }

  void TrackVidx( int_vidx_t vidx )
  {
    if (_enableGC)
      _tracker.AddC( vidx );
  }

  void TrackVidxVec( const std::vector<int_vidx_t>& vidxVec )
  {
    if (!_enableGC)
      return;

    for (int_vidx_t vidx : vidxVec)
    {
      TrackVidx( vidx );
    }
  }

  // SVidxかvidxかはコード上はあまり区別したくない事も多いので、
  // TrackSVidxでは無くTrackVidxとオーバーロードにしておく。
  void TrackVidx( const SVidx& svidx )
  {
    if (_enableGC)
      _tracker.AddC( svidx._vidx );
  }

  void TrackVidxVec( const std::vector<SVidx>& svidxVec )
  {
    if (!_enableGC)
      return;

    for (const SVidx& svidx : svidxVec)
    {
      TrackVidx( svidx );
    }
  }


};

struct ZstdDecoder
{
  using PoolType = ContextPool<ZSTD_DCtx, ZSTD_createDCtx, ZSTD_freeDCtx>;
  PoolType _pool;

  bool Decode(void* src, int srcSize, void* outBuf, int bufMaxSize)
  {
    CPGuard<PoolType> cguard( _pool );
    size_t const dSize = ZSTD_decompressDCtx( cguard._ctx, outBuf, bufMaxSize, src, srcSize);
    return !ZSTD_isError(dSize);
  }
};


//////////////////////////////////////////////////////////////////////////////
// 展開用
//////////////////////////////////////////////////////////////////////////////
class PXVPackerDecode
{
private:
  unique_ptr<ReadablePageStore> _pageStore;

  void ReadHeader(StreamReader &reader, uint8_t *comp, int64_t *decodedSize)
  {
    // ヘッダの読み込み。無圧縮の時だけサイズが入ってない。
    *comp = reader.ReadUInt8();
    if(*comp == PXVPackerEncode::COMP_NONE)
    {
      // もともと書き込みがintになっているのであふれる事は無い。uint64_tに直す時は書き込み側も直す必要あり。
      *decodedSize = static_cast<int64_t>(reader.GetSize()-NONCOMPRESSED_HEADER_SIZE);
    }
    else
    {
      // B. 圧縮済みデータ
      *decodedSize = reader.ReadInt64();
    }
  }

public:
  explicit PXVPackerDecode() = default;
  ~PXVPackerDecode() = default;

  ZstdDecoder _zstdDecoder;

  // Debug用
  ReadablePageStore& GetPageStore() const { return *_pageStore; }

  // pstoreはデストラクタでdeleteされるのに注意。
  void AttachPageStore(ReadablePageStore *pstore)
  {
    _pageStore.reset(pstore);
  }

  ReadablePageStore* DetachPageStore()
  {
    return _pageStore.release();
  }

  bool Open( nstring filename )
  {
    try
    {
      _pageStore = RPageStoreFactory::Create( RawPageMapper{filename.c_str(), true} );
      return true;
    }
    catch(const std::runtime_error)
    {
      return false;
    }
    
  }

  // streamの表すチャンクのデータを取得（存在してるなら、*dataにmallocしてセット)
  bool Get( StreamReader& reader, uint8_t *comp, int64_t *decodedSize, void** data )
  {
    ReadHeader(reader, comp, decodedSize);

    if(*comp == PXVPackerEncode::COMP_NONE)
    {
      *data = malloc( *decodedSize );
      if(*data == NULL)
        return false;
    
      reader.Read(reinterpret_cast<uint8_t *>(*data), (int)*decodedSize);
      return true;
    }
    else if(*comp == ZstdEncoder::CodecType)
    {
      // B. 圧縮済みデータ

      *data = malloc( *decodedSize );
      if (*data == NULL) return false;

      auto autoFreeData = neet::ScopeGuard([&]{ free( *data ); });

      auto streamSize = reader.GetSize()-COMPRESSED_HEADER_SIZE;
      void* tmp = malloc( streamSize);
      if (tmp == NULL) { return false; }

      auto autoFreeTmp = neet::ScopeGuard([&]{ free( tmp ); });

      reader.Read(static_cast<uint8_t*>(tmp), (int)streamSize);

      auto res = _zstdDecoder.Decode( tmp, (int)streamSize, *data, (int)(*decodedSize) );

      // 成功したらdataは解放しないで呼び出し元に返す。
      if (res)
      {
        autoFreeData.dismiss();
      }

      return res;
    }
    else
    {
      // 知らない圧縮形式。ここには来ないはず。壊れファイルなどの時に落ちないようにfalseを返す。
      return false;
    }
  }

  // vidxの表すチャンクのデータを取得（存在してるなら、*dataにmallocしてセット)
  bool Get( int_vidx_t vidx, uint8_t *comp, int64_t *decodedSize, void** data )
  {
    if (!IsMappedVidx( vidx ))
      throw EmptyVidxError( "Empty Vidx in Get" );

    try
    {
      StreamReader reader(*_pageStore, vidx);
      return Get( reader, comp, decodedSize, data );
    }
    catch(const neet::PXVError& e)
    {
      // GetでのエラーはheadのnextVidxが0のケースがほとんど。
      // この場合の解析に役に立ちそうな情報を集められるだけ集める。
      std::stringstream ss;
      ss << e.what();
      _pageStore->DebInfo( vidx, ss );
      throw neet::PXVError( ss.str() );
    } 
  }

  bool IsMappedVidx( int_vidx_t vidx ) const { return _pageStore->IsMappedVidx( vidx ); }

  StreamReader GetReader( int_vidx_t vidx )
  {
    return StreamReader(*_pageStore, vidx);
  }

  // C++の外から使う用。C++ではGetReaderを呼ぶ事。
  StreamReader* NewReader( int_vidx_t vidx )
  {
    return new StreamReader(*_pageStore, vidx);
  }

  // ヘッダだけ取得
  bool GetHeader( int_vidx_t vidx, uint8_t *comp, int64_t *decodedSize )
  {
    StreamReader reader(*_pageStore, vidx);
    
    ReadHeader(reader, comp, decodedSize);
    return true;
  }

};


} // namespace pxvpack
#endif
