// Copyright 2026 PGN Inc. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

/* -*- coding: utf-8 -*- マルチバイト */

#ifndef LIBNEET_FILE_H_
#define LIBNEET_FILE_H_

#include "libneet.h"
#include <stdexcept>


namespace neet
{

/*
* サムネイルハンドラではIStreamを使うのでファイル関連は不要だが、コンパイルを通すのに必要。
*/
inline size_t GetFileSize(nstring filepath) { throw std::runtime_error("Never called"); }
inline bool IsFileExists(nstring filepath) { throw std::runtime_error("Never called"); }
inline void NRemoveFile(nstring filepath) { throw std::runtime_error("Never called"); }


typedef ::HANDLE N_FHANDLE;
const ::HANDLE N_FHANDLE_INVALID = INVALID_HANDLE_VALUE;
inline bool NIsOpenFail(N_FHANDLE fhandle) { return fhandle == N_FHANDLE_INVALID; }

// ReadOnlyでのopen
inline N_FHANDLE NOpenR(nstring filepath) { throw std::runtime_error("Never called"); }

// writeするopen（なければcreate）
inline N_FHANDLE NOpenW( nstring filepath ) { throw std::runtime_error("Never called"); }

inline void NClose( N_FHANDLE fd ) { throw std::runtime_error("Never called"); }
inline size_t NGetFileSize( N_FHANDLE fd ) { throw std::runtime_error("Never called"); }
inline bool NExtendFile(N_FHANDLE fd, int64_t flen ) { throw std::runtime_error("Never called"); }
inline void* NMMapRead(N_FHANDLE fileDesc, int64_t fpos, size_t len) { throw std::runtime_error("Never called"); }
inline void* NMMapWrite(N_FHANDLE fileDesc, int64_t fpos, size_t len) { throw std::runtime_error("Never called"); }
inline void NMunmap(void *addr, size_t len) { throw std::runtime_error("Never called"); }

inline bool NFSync( N_FHANDLE fd ) { throw std::runtime_error("Never called"); }
inline bool NMSync( void *addr, size_t len ) { throw std::runtime_error("Never called"); }

class CFileSeek
{
public:
  bool OpenRead(nstring filepath) { throw std::runtime_error("Never called"); }
  bool Read(void* buf, int length) { throw std::runtime_error("Never called"); }
};

} // namespace neet

#endif

