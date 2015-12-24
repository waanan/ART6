/* -*- mode: c++ -*-
 * Copyright (C) 2015 Yu Hengyang.
 *
 * ac_defs.h - Constant definitions used by tracker.
 *
 * AC stands for Address Chain.
 *
 * History:
 *
 * Created on 2015-1-3 by Hengyang Yu.
 *   Initial craft.
 */
#ifndef ART_RUNTIME_LEAKTRACER_AC_DEFS_H_
#define ART_RUNTIME_LEAKTRACER_AC_DEFS_H_


enum {
  kObjTagSize = 2,
  kObjTagMask = 0x3,
  kObjAddrMask = ~kObjTagMask,

  kLastAccessTimeShiftBits = 0,
  kLastAccessTimeSize = 15,
  kLastAccessTimeMask = (1 << kLastAccessTimeShiftBits) - 1,

  kStalenessShiftBits = 15,
  kStalenessSize = 15,
  kStalenessMask = (1 << kStalenessSize) - 1,

  kLargeBitShiftBits  = 30,
  kLargeBitMask = 0x1,
  kLargeBit = 1 << kLargeBitShiftBits,

  kMoveBitShiftBits   = 31,
  kMoveBitMask  = 0x1,
  kMoveBit = 1 << kMoveBitShiftBits,

  kTableSize = 0x10000,
  kTableSizeMask = kTableSize - 1,

  kMaxAllocSite = 15000,
  kMaxSuspectSite = 50,
  kMaxObjLifeTime = 10000, /* max life time of objects in number of GCs */
};

/* lowest two bits are always zero */
#define HASH(key) (((key) >> 2) & kTableSizeMask)
#define OBJ(obj)  ((obj) & kObjAddrMask)

enum ObjectKind {
  /* make the least hex digit odd ensure it won't be object size. */
  kNormalObject = 0x0,
  kLargeObject  = 0x1,
  kArrayObject  = 0x2,
};

enum ObjectEvent {
  /*
   * Taking advantage of the fact that the least significant two bits of
   * objects' addresses are always zero due to address alignment.
   */
  kNewObject     = 0x0,
  kAccessObject  = 0x1,
  kMoveObject    = 0x2,
  kReclaimObject = 0x3,

#define TAG(obj)  ((obj) & kObjTagMask)
};


enum {
  /*
   * The least significant hex digit 'F' ensures the following 4 constants
   * are not valid object addresses.
   */
  kAppStart   = 0xAAAAAAAF,
  kAppEnd     = 0xBBBBBBBF,
  kGcStart    = 0xCCCCCCCF,
  kGcEnd      = 0xDDDDDDDF,
};

typedef unsigned int u32;

#endif  // ART_RUNTIME_LEAKTRACER_AC_DEFS_H_

