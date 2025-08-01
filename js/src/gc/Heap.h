/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * vim: set ts=8 sts=2 et sw=2 tw=80:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef gc_Heap_h
#define gc_Heap_h

#include "mozilla/DebugOnly.h"

#include "gc/AllocKind.h"
#include "gc/Memory.h"
#include "gc/Pretenuring.h"
#include "js/HeapAPI.h"
#include "js/TypeDecls.h"
#include "util/Poison.h"

namespace js {

class AutoLockGC;
class AutoLockGCBgAlloc;
class Nursery;

namespace gc {

class Arena;
class ArenaCellSet;
class ArenaList;
class GCRuntime;
class MarkingValidator;
class SortedArenaList;
class TenuredCell;

// Cells are aligned to CellAlignShift, so the largest tagged null pointer is:
const uintptr_t LargestTaggedNullCellPointer = (1 << CellAlignShift) - 1;

static_assert(ArenaSize % CellAlignBytes == 0,
              "Arena size must be a multiple of cell alignment");

/*
 * A FreeSpan represents a contiguous sequence of free cells in an Arena. It
 * can take two forms.
 *
 * - In an empty span, |first| and |last| are both zero.
 *
 * - In a non-empty span, |first| is the address of the first free thing in the
 *   span, and |last| is the address of the last free thing in the span.
 *   Furthermore, the memory pointed to by |last| holds a FreeSpan structure
 *   that points to the next span (which may be empty); this works because
 *   sizeof(FreeSpan) is less than the smallest thingSize.
 */
class FreeSpan {
  friend class Arena;
  friend class ArenaCellIter;
  friend class ArenaFreeCellIter;

  uint16_t first;
  uint16_t last;

 public:
  // This inits just |first| and |last|; if the span is non-empty it doesn't
  // do anything with the next span stored at |last|.
  void initBounds(uintptr_t firstArg, uintptr_t lastArg, const Arena* arena) {
    checkRange(firstArg, lastArg, arena);
    first = firstArg;
    last = lastArg;
  }

  void initAsEmpty() {
    first = 0;
    last = 0;
  }

  // This sets |first| and |last|, and also sets the next span stored at
  // |last| as empty. (As a result, |firstArg| and |lastArg| cannot represent
  // an empty span.)
  void initFinal(uintptr_t firstArg, uintptr_t lastArg, const Arena* arena) {
    initBounds(firstArg, lastArg, arena);
    FreeSpan* last = nextSpanUnchecked(arena);
    last->initAsEmpty();
    checkSpan(arena);
  }

  bool isEmpty() const { return !first; }

  Arena* getArenaUnchecked() { return reinterpret_cast<Arena*>(this); }
  inline Arena* getArena();

  static size_t offsetOfFirst() { return offsetof(FreeSpan, first); }

  static size_t offsetOfLast() { return offsetof(FreeSpan, last); }

  // Like nextSpan(), but no checking of the following span is done.
  FreeSpan* nextSpanUnchecked(const Arena* arena) const {
    MOZ_ASSERT(arena && !isEmpty());
    return reinterpret_cast<FreeSpan*>(uintptr_t(arena) + last);
  }

  const FreeSpan* nextSpan(const Arena* arena) const {
    checkSpan(arena);
    return nextSpanUnchecked(arena);
  }

  MOZ_ALWAYS_INLINE TenuredCell* allocate(size_t thingSize) {
    // Eschew the usual checks, because this might be the placeholder span.
    // If this is somehow an invalid, non-empty span, checkSpan() will catch it.
    Arena* arena = getArenaUnchecked();
    checkSpan(arena);
    uintptr_t thing = uintptr_t(arena) + first;
    if (first < last) {
      // We have space for at least two more things, so do a simple
      // bump-allocate.
      first += thingSize;
    } else if (MOZ_LIKELY(first)) {
      // The last space points to the next free span (which may be empty).
      const FreeSpan* next = nextSpan(arena);
      first = next->first;
      last = next->last;
    } else {
      return nullptr;  // The span is empty.
    }
    checkSpan(arena);
    DebugOnlyPoison(reinterpret_cast<void*>(thing),
                    JS_ALLOCATED_TENURED_PATTERN, thingSize,
                    MemCheckKind::MakeUndefined);
    return reinterpret_cast<TenuredCell*>(thing);
  }

  inline void checkSpan(const Arena* arena) const;
  inline void checkRange(uintptr_t first, uintptr_t last,
                         const Arena* arena) const;
};

/*
 * Arenas are the allocation units of the tenured heap in the GC. An arena
 * is 4kiB in size and 4kiB-aligned. It starts with several header fields
 * followed by some bytes of padding. The remainder of the arena is filled
 * with GC things of a particular AllocKind. The padding ensures that the
 * GC thing array ends exactly at the end of the arena:
 *
 * <----------------------------------------------> = ArenaSize bytes
 * +---------------+---------+----+----+-----+----+
 * | header fields | padding | T0 | T1 | ... | Tn |
 * +---------------+---------+----+----+-----+----+
 * <-------------------------> = first thing offset
 */
class alignas(ArenaSize) Arena {
  static JS_PUBLIC_DATA const uint8_t ThingSizes[];
  static JS_PUBLIC_DATA const uint8_t FirstThingOffsets[];
  static JS_PUBLIC_DATA const uint8_t ThingsPerArena[];
  /*
   * The first span of free things in the arena. Most of these spans are
   * stored as offsets in free regions of the data array, and most operations
   * on FreeSpans take an Arena pointer for safety. However, the FreeSpans
   * used for allocation are stored here, at the start of an Arena, and use
   * their own address to grab the next span within the same Arena.
   */
  FreeSpan firstFreeSpan;

  /*
   * One of the AllocKind constants or AllocKind::LIMIT when the arena does
   * not contain any GC things and is on the list of empty arenas in the GC
   * chunk.
   */
  AllocKind allocKind;

  /*
   * The zone that this Arena is contained within, when allocated. The offset
   * of this field must match the ArenaZoneOffset stored in js/HeapAPI.h,
   * as is statically asserted below.
   */
  JS::Zone* zone_;

 public:
  /*
   * Arena::next has two purposes: when unallocated, it points to the next
   * available Arena. When allocated, it points to the next Arena in the same
   * zone and with the same alloc kind.
   */
  Arena* next;

 private:
  static const size_t ARENA_FLAG_BITS = 4;
  static const size_t DELAYED_MARKING_ARENA_BITS =
      JS_BITS_PER_WORD - ArenaShift;
  static_assert(
      ARENA_FLAG_BITS + DELAYED_MARKING_ARENA_BITS <= JS_BITS_PER_WORD,
      "Not enough space to pack flags and nextDelayedMarkingArena_ pointer "
      "into a single word.");

  /*
   * True until the arena is swept for the first time.
   */
  size_t isNewlyCreated_ : 1;

  /*
   * When recursive marking uses too much stack we delay marking of arenas and
   * link them into a list for later processing. This uses the following fields.
   */
  size_t onDelayedMarkingList_ : 1;
  size_t hasDelayedBlackMarking_ : 1;
  size_t hasDelayedGrayMarking_ : 1;
  size_t nextDelayedMarkingArena_ : DELAYED_MARKING_ARENA_BITS;

  union {
    /*
     * For arenas in zones other than the atoms zone, if non-null, points
     * to an ArenaCellSet that represents the set of cells in this arena
     * that are in the nursery's store buffer.
     */
    ArenaCellSet* bufferedCells_;

    /*
     * For arenas in the atoms zone, the starting index into zone atom
     * marking bitmaps (see AtomMarking.h) of the things in this zone.
     * Atoms never refer to nursery things, so no store buffer index is
     * needed.
     */
    size_t atomBitmapStart_;
  };

 public:
  /*
   * The size of data should be |ArenaSize - offsetof(data)|, but the offset
   * is not yet known to the compiler, so we do it by hand. |firstFreeSpan|
   * takes up 8 bytes on 64-bit due to alignment requirements; the rest are
   * obvious. This constant is stored in js/HeapAPI.h.
   */
  uint8_t data[ArenaSize - ArenaHeaderSize];

  // Create a free arena in uninitialized committed memory.
  void init(GCRuntime* gc, JS::Zone* zone, AllocKind kind);

  JS::Zone* zone() const { return zone_; }

  // Sets |firstFreeSpan| to the Arena's entire valid range, and
  // also sets the next span stored at |firstFreeSpan.last| as empty.
  void setAsFullyUnused() {
    AllocKind kind = getAllocKind();
    firstFreeSpan.first = firstThingOffset(kind);
    firstFreeSpan.last = lastThingOffset(kind);
    FreeSpan* last = firstFreeSpan.nextSpanUnchecked(this);
    last->initAsEmpty();
  }

  // Unregister the associated atom marking bitmap index for an arena in the
  // atoms zone.
  inline void freeAtomMarkingBitmapIndex(GCRuntime* gc, const AutoLockGC& lock);

  // Return an allocated arena to its unallocated (free) state.
  // For arenas in the atoms zone, freeAtomMarkingBitmapIndex() must be called
  // first.
  inline void release();

  uintptr_t address() const {
    checkAddress();
    return uintptr_t(this);
  }

  inline void checkAddress() const;

  inline ArenaChunk* chunk() const;

  // Return whether this arena is in the 'allocated' state, meaning that it has
  // been initialized by calling init() and has a zone and alloc kind set.
  // This is mostly used for assertions.
  bool allocated() const;

  AllocKind getAllocKind() const {
    MOZ_ASSERT(IsValidAllocKind(allocKind));
    return allocKind;
  }

  FreeSpan* getFirstFreeSpan() { return &firstFreeSpan; }

  static size_t thingSize(AllocKind kind) { return ThingSizes[size_t(kind)]; }
  static size_t thingsPerArena(AllocKind kind) {
    return ThingsPerArena[size_t(kind)];
  }
  static size_t thingsSpan(AllocKind kind) {
    return thingsPerArena(kind) * thingSize(kind);
  }

  static size_t firstThingOffset(AllocKind kind) {
    return FirstThingOffsets[size_t(kind)];
  }
  static size_t lastThingOffset(AllocKind kind) {
    return ArenaSize - thingSize(kind);
  }

  size_t getThingSize() const { return thingSize(getAllocKind()); }
  size_t getThingsPerArena() const { return thingsPerArena(getAllocKind()); }
  size_t getThingsSpan() const { return getThingsPerArena() * getThingSize(); }
  size_t getFirstThingOffset() const {
    return firstThingOffset(getAllocKind());
  }

  uintptr_t thingsStart() const { return address() + getFirstThingOffset(); }
  uintptr_t thingsEnd() const { return address() + ArenaSize; }

  bool isEmpty() const {
    // Arena is empty if its first span covers the whole arena.
    firstFreeSpan.checkSpan(this);
    AllocKind kind = getAllocKind();
    return firstFreeSpan.first == firstThingOffset(kind) &&
           firstFreeSpan.last == lastThingOffset(kind);
  }

  bool isFull() const { return firstFreeSpan.isEmpty(); }
  bool hasFreeThings() const { return !isFull(); }

  size_t numFreeThings(size_t thingSize) const {
    firstFreeSpan.checkSpan(this);
    size_t numFree = 0;
    const FreeSpan* span = &firstFreeSpan;
    for (; !span->isEmpty(); span = span->nextSpan(this)) {
      numFree += (span->last - span->first) / thingSize + 1;
    }
    return numFree;
  }

  size_t countFreeCells() { return numFreeThings(getThingSize()); }
  size_t countUsedCells() { return getThingsPerArena() - countFreeCells(); }

#ifdef DEBUG
  bool inFreeList(uintptr_t thing) {
    uintptr_t base = address();
    const FreeSpan* span = &firstFreeSpan;
    for (; !span->isEmpty(); span = span->nextSpan(this)) {
      // If the thing comes before the current span, it's not free.
      if (thing < base + span->first) {
        return false;
      }

      // If we find it before the end of the span, it's free.
      if (thing <= base + span->last) {
        return true;
      }
    }
    return false;
  }
#endif

  static bool isAligned(uintptr_t thing, size_t thingSize) {
    /* Things ends at the arena end. */
    uintptr_t tailOffset = ArenaSize - (thing & ArenaMask);
    return tailOffset % thingSize == 0;
  }

  bool isNewlyCreated() const { return isNewlyCreated_; }

  bool onDelayedMarkingList() const { return onDelayedMarkingList_; }

  Arena* getNextDelayedMarking() const {
    MOZ_ASSERT(onDelayedMarkingList_);
    return reinterpret_cast<Arena*>(nextDelayedMarkingArena_ << ArenaShift);
  }

  void setNextDelayedMarkingArena(Arena* arena) {
    MOZ_ASSERT(!(uintptr_t(arena) & ArenaMask));
    MOZ_ASSERT(!onDelayedMarkingList_);
    MOZ_ASSERT(!hasDelayedBlackMarking_);
    MOZ_ASSERT(!hasDelayedGrayMarking_);
    MOZ_ASSERT(!nextDelayedMarkingArena_);
    onDelayedMarkingList_ = 1;
    if (arena) {
      nextDelayedMarkingArena_ = arena->address() >> ArenaShift;
    }
  }

  void updateNextDelayedMarkingArena(Arena* arena) {
    MOZ_ASSERT(!(uintptr_t(arena) & ArenaMask));
    MOZ_ASSERT(onDelayedMarkingList_);
    nextDelayedMarkingArena_ = arena ? arena->address() >> ArenaShift : 0;
  }

  bool hasDelayedMarking(MarkColor color) const {
    MOZ_ASSERT(onDelayedMarkingList_);
    return color == MarkColor::Black ? hasDelayedBlackMarking_
                                     : hasDelayedGrayMarking_;
  }

  bool hasAnyDelayedMarking() const {
    MOZ_ASSERT(onDelayedMarkingList_);
    return hasDelayedBlackMarking_ || hasDelayedGrayMarking_;
  }

  void setHasDelayedMarking(MarkColor color, bool value) {
    MOZ_ASSERT(onDelayedMarkingList_);
    if (color == MarkColor::Black) {
      hasDelayedBlackMarking_ = value;
    } else {
      hasDelayedGrayMarking_ = value;
    }
  }

  void clearDelayedMarkingState() {
    MOZ_ASSERT(onDelayedMarkingList_);
    onDelayedMarkingList_ = 0;
    hasDelayedBlackMarking_ = 0;
    hasDelayedGrayMarking_ = 0;
    nextDelayedMarkingArena_ = 0;
  }

  inline ArenaCellSet*& bufferedCells();
  inline size_t& atomBitmapStart();

  template <typename T, FinalizeKind finalizeKind>
  size_t finalize(JS::GCContext* gcx, AllocKind thingKind, size_t thingSize);

  static void staticAsserts();
  static void checkLookupTables();

  void unmarkAll();
  void unmarkPreMarkedFreeCells();

  void arenaAllocatedDuringGC();

#ifdef DEBUG
  void checkNoMarkedFreeCells();
  void checkAllCellsMarkedBlack();
#endif

#if defined(DEBUG) || defined(JS_GC_ZEAL)
  void checkNoMarkedCells();
#endif
};

inline Arena* FreeSpan::getArena() {
  Arena* arena = getArenaUnchecked();
  arena->checkAddress();
  return arena;
}

inline void FreeSpan::checkSpan(const Arena* arena) const {
#ifdef DEBUG
  if (!first) {
    MOZ_ASSERT(!first && !last);
    return;
  }

  arena->checkAddress();
  checkRange(first, last, arena);

  // If there's a following span, it must have a higher address,
  // and the gap must be at least 2 * thingSize.
  const FreeSpan* next = nextSpanUnchecked(arena);
  if (next->first) {
    checkRange(next->first, next->last, arena);
    size_t thingSize = arena->getThingSize();
    MOZ_ASSERT(last + 2 * thingSize <= next->first);
  }
#endif
}

inline void FreeSpan::checkRange(uintptr_t first, uintptr_t last,
                                 const Arena* arena) const {
#ifdef DEBUG
  MOZ_ASSERT(arena);
  MOZ_ASSERT(first <= last);
  AllocKind thingKind = arena->getAllocKind();
  MOZ_ASSERT(first >= Arena::firstThingOffset(thingKind));
  MOZ_ASSERT(last <= Arena::lastThingOffset(thingKind));
  MOZ_ASSERT((last - first) % Arena::thingSize(thingKind) == 0);
#endif
}

/*
 * A chunk in the tenured heap. ArenaChunks contain arenas and associated data
 * structures (mark bitmap, delayed marking state).
 */
class ArenaChunk : public ArenaChunkBase {
  Arena arenas[ArenasPerChunk];

  friend class GCRuntime;
  friend class MarkingValidator;

 public:
  static ArenaChunk* fromAddress(uintptr_t addr) {
    addr &= ~ChunkMask;
    return reinterpret_cast<ArenaChunk*>(addr);
  }

  static bool withinValidRange(uintptr_t addr) {
    uintptr_t offset = addr & ChunkMask;
    if (ArenaChunk::fromAddress(addr)->isNurseryChunk()) {
      return offset >= sizeof(ChunkBase) && offset < ChunkSize;
    }
    return offset >= offsetof(ArenaChunk, arenas) && offset < ChunkSize;
  }

  static size_t arenaIndex(const Arena* arena) {
    uintptr_t addr = arena->address();
    MOZ_ASSERT(!ArenaChunk::fromAddress(addr)->isNurseryChunk());
    MOZ_ASSERT(withinValidRange(addr));
    uintptr_t offset = addr & ChunkMask;
    return (offset - offsetof(ArenaChunk, arenas)) >> ArenaShift;
  }

  static size_t pageIndex(const Arena* arena) {
    return arenaToPageIndex(arenaIndex(arena));
  }

  static size_t arenaToPageIndex(size_t arenaIndex) {
    static_assert((offsetof(ArenaChunk, arenas) % PageSize) == 0,
                  "First arena should be on a page boundary");
    return arenaIndex / ArenasPerPage;
  }

  static size_t pageToArenaIndex(size_t pageIndex) {
    return pageIndex * ArenasPerPage;
  }

  explicit ArenaChunk(JSRuntime* runtime) : ArenaChunkBase(runtime) {}

  uintptr_t address() const {
    uintptr_t addr = reinterpret_cast<uintptr_t>(this);
    MOZ_ASSERT(!(addr & ChunkMask));
    return addr;
  }

  bool isEmpty() const { return info.numArenasFree == ArenasPerChunk; }

  bool hasAvailableArenas() const { return !isFull(); }
  bool isFull() const { return info.numArenasFree == 0; }

  bool isNurseryChunk() const { return storeBuffer; }

  Arena* allocateArena(GCRuntime* gc, JS::Zone* zone, AllocKind kind);

  void releaseArena(GCRuntime* gc, Arena* arena, const AutoLockGC& lock);

  void decommitFreeArenas(GCRuntime* gc, const bool& cancel, AutoLockGC& lock);
  [[nodiscard]] bool decommitOneFreePage(GCRuntime* gc, size_t pageIndex,
                                         const AutoLockGC& lock);
  void decommitAllArenas();

  // This will decommit each unused not-already decommitted arena. It performs a
  // system call for each arena but is only used during OOM.
  void decommitFreeArenasWithoutUnlocking(const AutoLockGC& lock);

  static void* allocate(GCRuntime* gc, StallAndRetry stallAndRetry);
  static ArenaChunk* init(void* ptr, GCRuntime* gc, bool allMemoryCommitted);

  /* Unlink and return the freeArenasHead. */
  Arena* fetchNextFreeArena(GCRuntime* gc);

  // Merge arenas freed by background sweeping into the main free arenas bitmap.
  void mergePendingFreeArenas(GCRuntime* gc, const AutoLockGC& lock);

#ifdef DEBUG
  void verify() const;
#else
  void verify() const {}
#endif

 private:
  void commitOnePage(GCRuntime* gc);

  void updateFreeCountsAfterAlloc(GCRuntime* gc, size_t numArenasAlloced,
                                  const AutoLockGC& lock);
  void updateFreeCountsAfterFree(GCRuntime* gc, size_t numArenasFreed,
                                 bool wasCommitted, const AutoLockGC& lock);

  // Like updateFreeCountsAfterFree, but operates on the GCRuntime's current
  // chunk. Does not take the lock unless the chunk is full or if we need to
  // move the chunk between pools.
  void updateCurrentChunkAfterAlloc(GCRuntime* gc);

  // Check if all arenas in a page are free.
  bool canDecommitPage(size_t pageIndex) const;

  // Check the arena from freeArenasList is located in a free page.
  // Unlike the isPageFree(size_t) version, this isPageFree(Arena*) will see the
  // following arenas from the freeArenasHead are also located in the same page,
  // to prevent not to access the arenas mprotect'ed during compaction in debug
  // build.
  bool isPageFree(const Arena* arena) const;

  void* pageAddress(size_t pageIndex) {
    return &arenas[pageToArenaIndex(pageIndex)];
  }
};

inline void Arena::checkAddress() const {
  mozilla::DebugOnly<uintptr_t> addr = uintptr_t(this);
  MOZ_ASSERT(addr);
  MOZ_ASSERT(!(addr & ArenaMask));
  MOZ_ASSERT(ArenaChunk::withinValidRange(addr));
}

inline ArenaChunk* Arena::chunk() const {
  return ArenaChunk::fromAddress(address());
}

// Cell header stored before all nursery cells.
struct alignas(gc::CellAlignBytes) NurseryCellHeader {
  // Store zone pointer with the trace kind in the lowest three bits.
  const uintptr_t allocSiteAndTraceKind;

  // We only need to store a subset of trace kinds so this doesn't cover the
  // full range.
  static const uintptr_t TraceKindMask = 3;

  static uintptr_t MakeValue(AllocSite* const site, JS::TraceKind kind) {
    MOZ_ASSERT(uintptr_t(kind) < TraceKindMask);
    MOZ_ASSERT((uintptr_t(site) & TraceKindMask) == 0);
    return uintptr_t(site) | uintptr_t(kind);
  }

  inline NurseryCellHeader(AllocSite* site, JS::TraceKind kind)
      : allocSiteAndTraceKind(MakeValue(site, kind)) {}

  AllocSite* allocSite() const {
    return reinterpret_cast<AllocSite*>(allocSiteAndTraceKind & ~TraceKindMask);
  }

  JS::Zone* zone() const { return allocSite()->zone(); }

  JS::TraceKind traceKind() const {
    return JS::TraceKind(allocSiteAndTraceKind & TraceKindMask);
  }

  static const NurseryCellHeader* from(const Cell* cell) {
    MOZ_ASSERT(IsInsideNursery(cell));
    return reinterpret_cast<const NurseryCellHeader*>(
        uintptr_t(cell) - sizeof(NurseryCellHeader));
  }
};

static_assert(uintptr_t(JS::TraceKind::Object) <=
              NurseryCellHeader::TraceKindMask);
static_assert(uintptr_t(JS::TraceKind::String) <=
              NurseryCellHeader::TraceKindMask);
static_assert(uintptr_t(JS::TraceKind::BigInt) <=
              NurseryCellHeader::TraceKindMask);

} /* namespace gc */

namespace debug {

// Utility functions meant to be called from an interactive debugger.
enum class MarkInfo : int {
  BLACK = 0,
  GRAY = 1,
  UNMARKED = -1,
  NURSERY_FROMSPACE = -2,
  NURSERY_TOSPACE = -3,  // Unused if semispace disabled.
  UNKNOWN = -4,
  BUFFER = -5,
};

// For calling from gdb only: given a pointer that is either in the nursery
// (possibly pointing to a buffer, not necessarily a Cell) or a tenured Cell,
// return its mark color or UNMARKED if it is tenured, otherwise the region of
// memory that contains it. UNKNOWN is only for non-Cell pointers, and means it
// is not in the nursery (so could be malloced or stack or whatever.)
MOZ_NEVER_INLINE MarkInfo GetMarkInfo(void* vp);

// Sample usage from gdb:
//
//   (gdb) p $word = js::debug::GetMarkWordAddress(obj)
//   $1 = (uintptr_t *) 0x7fa56d5fe360
//   (gdb) p/x $mask = js::debug::GetMarkMask(obj, js::gc::GRAY)
//   $2 = 0x200000000
//   (gdb) watch *$word
//   Hardware watchpoint 7: *$word
//   (gdb) cond 7 *$word & $mask
//   (gdb) cont
//
// Note that this is *not* a watchpoint on a single bit. It is a watchpoint on
// the whole word, which will trigger whenever the word changes and the
// selected bit is set after the change.
//
// So if the bit changing is the desired one, this is exactly what you want.
// But if a different bit changes (either set or cleared), you may still stop
// execution if the $mask bit happened to already be set. gdb does not expose
// enough information to restrict the watchpoint to just a single bit.

// Return the address of the word containing the mark bits for the given cell,
// or nullptr if the cell is in the nursery.
MOZ_NEVER_INLINE uintptr_t* GetMarkWordAddress(js::gc::Cell* cell);

// Return the mask for the given cell and color bit, or 0 if the cell is in the
// nursery.
MOZ_NEVER_INLINE uintptr_t GetMarkMask(js::gc::Cell* cell, uint32_t colorBit);

} /* namespace debug */
} /* namespace js */

#endif /* gc_Heap_h */
