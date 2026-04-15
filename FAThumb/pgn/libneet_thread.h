// Copyright 2026 PGN Inc. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

/* -*- coding: utf-8 -*- マルチバイト */

#ifndef PGN_LIBNEET_THREAD_H_
#define PGN_LIBNEET_THREAD_H_

#include "libneet.h"
#include <queue>

namespace neet
{

struct NMutex
{
  CRITICAL_SECTION _cs;
  NMutex()
  {
    InitializeCriticalSectionAndSpinCount( &_cs, 0x400 );
  }
  ~NMutex()
  {
    DeleteCriticalSection( &_cs );
  }
  void Lock() { EnterCriticalSection( &_cs ); }
  void Unlock() { LeaveCriticalSection( &_cs ); }

  NMutex(NMutex&&) = delete;
  NMutex(NMutex const&) = delete;
  NMutex& operator=(NMutex const&) = delete;
  NMutex& operator=(NMutex&&) = delete;
};

// end: プラットフォームごとに提供する物

struct NLockGuard {
  NMutex& _mutex;
  NLockGuard(NMutex &src) : _mutex(src)
  {
    _mutex.Lock();
  }
  ~NLockGuard()
  {
    _mutex.Unlock();
  }
};

} // namespace neet

#endif

