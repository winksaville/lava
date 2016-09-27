
// -todo: hash key data instead of the block index of the key
//       --create function to hash arbitrary bytes 
//       --create function to put into a hashmap with a pre-hashed value
// -todo: make bitwise compare function between blocks
// -todo: make a function to compare a block to an arbitrary byte buffer
// -todo: change store_kv to use compare exchange and return previous kv ?
// -todo: make remove function for ConcurrentStore? - just use free
// -todo: remove overwritten indices from the ConcurrentStore
// -todo: figure out why "wat" and "wut" match - 3 bugs: unsigned size_t in the blockcompare function, not flipping the signs of the next index when it is negative (for length), default value of true when while loop ends from the blocks comparing as false
// -todo: figure out why second insert is putting EMPTY_KEY for key and value in the hash map - comp returns true even though strings are different?
// -todo: use match function instead of block comparison for everything? better for continguous memory anyway?
// -todo: fix not finding a key too long to fit in block - wasn't updating the current block index in the loop
// -todo: return an error on running out of blocks - done with a negative blockcount if not enough blocks are available
// -todo: free blocks if not enough block are allocated
// -todo: fix infinite loop on free - last block wasn't getting next block index set when reaching LIST_END 
// -todo: deal with memory / allocate from  shared memory
// -todo: make a membuf class that encapsulates a shared memory buffer / memory mapped file on windows or linux
// -todo: make remove function

// todo: make remove function concurrent and account for the number of readers
// todo: remove decremented the readers but didn't actually delete, so that the last reader out would delete, maybe just make a flag as mark for deletion?
// todo: make take function the combines get and remove
// todo: Make block size for keys different than data?
// todo: redo concurrent store get to store length so that buffer can be returned
// todo: store lengths and check key lengths before trying bitwise comparison as an optimization? - would only make a difference for long keys that are larger than one block? no it would make a difference on every get?
// todo: make -1 an error instead of returning a length of 0? - distinguising a key with length 0 and no key could be useful
// todo: mark free cells as negative numbers so double free is caught?
// todo: lock init with mutex?
// todo: implement locking resize?

//Block based allocation
//-Checking if the head has been touched means either incrementing a counter every time it is written, or putting in a thread id every time it is read or written
//-Each ui32 in a vector holds the position of the next free block 
//-Alloc checks the head to read the next free position and puts it in head if head hasn't been touched. 
//-Free checks the head to read the next free position, stores the head value in the free position, then moves head to the just written new free position if head hasn't been touched

//
// Make frees happen from the last block to the first - don't remember what this means

// idea: use max_waste as a recipricol power of two giving the percentage of waste allowed for an allocation
// - 2 would be 25%, 3 would be 12.5% 
// - not allowed to be below 2
// idea: put atomic reader counter into each ConcurrentStore entry as a signed integer
// idea: figure out how to make ConcurrentHash a flat data structure so it can sit in shared memory


#ifndef __CONCURRENTMAP_HEADER_GUARD__
#define __CONCURRENTMAP_HEADER_GUARD__

#include <cstdint>
#include <cstring>
#include <atomic>
#include <mutex>
#include <memory>
#include <vector>

#include <windows.h>

using   ui8   =   uint8_t;
using   i64   =   int64_t;
using  ui64   =   uint64_t;
using   i32   =   int32_t;
using  ui32   =   uint32_t;
using   f32   =   float;
using   f64   =   double;
using aui64   =   std::atomic<ui64>;
using  ai32   =   std::atomic<i64>;
using  cstr   =   const char*;
using   str   =   std::string;

class   ConcurrentHash
{
private:

public:
  //using COMPARE_FUNC  =  decltype( [](ui32 key){} );

  union kv
  {
    struct
    {
      uint64_t  readers  :  8;
      uint64_t      key  : 28;
      uint64_t      val  : 28;
    };
    uint64_t asInt;
  };

  static const ui8   INIT_READERS  =     0;            // eventually make this 1 again? - to catch when readers has dropped to 0
  static const ui8   FREE_READY    =     0;
  static const ui8   MAX_READERS   =  0xFF;
  static const ui32  EMPTY_KEY     =  0x0FFFFFFF;      // 28 bits set 
  
  static bool DefaultKeyCompare(ui32 a, ui32 b)
  {
    return a == b;
  }

  //const static ui32 EMPTY_KEY = 0xFFFFFFFF;

private:
  using i8        =  int8_t;
  using ui32      =  uint32_t;
  using ui64      =  uint64_t;
  using Aui32     =  std::atomic<ui32>;  
  using Aui64     =  std::atomic<ui64>;  
  using KVs       =  std::vector<kv>;
  using Mut       =  std::mutex;
  using UnqLock   =  std::unique_lock<Mut>;

         ui32   m_sz;
  mutable KVs   m_kvs;

  kv           empty_kv()                   const
  {
    kv empty;
    empty.readers  =  INIT_READERS;
    empty.key      =  EMPTY_KEY;
    empty.val      =  EMPTY_KEY;
    return empty;
  }
  ui32          intHash(ui32  h)            const
  {
    h ^= h >> 16;
    h *= 0x85ebca6b;
    h ^= h >> 13;
    h *= 0xc2b2ae35;
    h ^= h >> 16;
    return h;
  }
  ui32     nextPowerOf2(ui32  v)            const
  {
    v--;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    v++;

    return v;
  }
  kv            load_kv(ui32  i)            const
  {
    using namespace std;
    
    kv keyval;
    keyval.asInt   =  atomic_load<ui64>( (Aui64*)&m_kvs.data()[i].asInt );              // Load the key that was there.
    return keyval;
  }
  kv           store_kv(ui32  i, kv keyval) const
  {
    using namespace std;
    
    //atomic_store<ui64>( (Aui64*)&m_kvs[i].asInt, _kv.asInt );

    kv ret;
    ret.asInt = atomic_exchange<ui64>( (Aui64*)&m_kvs[i].asInt, keyval.asInt);
    return ret;
  }
  bool  compexchange_kv(ui32  i, ui64* expected, ui64 desired) const
  {
    using namespace std;
    
    //kv    keyval;
    //keyval.asInt    =  atomic_load<ui64>( (Aui64*)&m_kvs.data()[i].asInt );
    //keyval.key      =  key;
    //keyval.val      =  val;
    //return success;

    return atomic_compare_exchange_strong( (Aui64*)&m_kvs.data()[i].asInt, expected, desired);                      // The entry was free. Now let's try to take it using a CAS. 
  }
  kv         addReaders(ui32  i, kv curKv, i8 readers)         const                         // increment the readers by one and return the previous kv from the successful swap 
  {
    kv readKv = curKv;
    do
    {
      if(curKv.key     == EMPTY_KEY  ||
         curKv.readers == FREE_READY || 
         curKv.readers == MAX_READERS) 
        return curKv;                                                                // not successful if the key is empty and not successful if readers is below the starting point - the last free function or last reader should be freeing this slot - this relies on nothing incrementing readers once it has reached FREE_READY
    
      readKv           =  curKv;
      readKv.readers  +=  readers;
    } while( !compexchange_kv(i, &curKv.asInt, readKv.asInt) );

    return curKv;
  }

public:
  ConcurrentHash(){}
  ConcurrentHash(ui32 sz)
  {
    init(sz);
  }
  ConcurrentHash(ConcurrentHash const& lval) = delete;
  ConcurrentHash(ConcurrentHash&&      rval) = delete;
  
  ConcurrentHash& operator=(ConcurrentHash const& lval) = delete;
  ConcurrentHash& operator=(ConcurrentHash&&      rval) = delete;

  template<class COMP_FUNC> 
  kv putHashed(ui32 hash, ui32 key, ui32 val, COMP_FUNC comp) const
  {
    using namespace std;
  
    kv desired;
    desired.key  =  key;
    desired.val  =  val;
    ui32      i  =  hash; // intHash(key);
    for(;; ++i)
    {
      i  &=  m_sz-1;
  
      kv probedKv = load_kv(i);

      //if(probedKv.key != EMPTY_KEY) continue;
      //if(probedKv.key == EMPTY_KEY){
      //  store_kv(i, desired); 
      //  return i;
      //}

      //if(probedKv.key != key)
      if(probedKv.key == EMPTY_KEY)
      {
        //if(probedKv.key != EMPTY_KEY) continue;                                               // The entry was either free, or contains another key.  // Usually, it contains another key. Keep probing.
                
        kv   expected   =  empty_kv();
  
        bool   success  =  compexchange_kv(i, &expected.asInt, desired.asInt);
        if( !success && (expected.key!=key) ) continue;                                       // Another thread just stole it from underneath us.
        else                                  return expected;
      }                                                                                       // Either we just added the key, or another thread did.
      
      //if( comp(probedKv.key, key) ){
      if( comp(probedKv.key) ){
        return store_kv(i, desired);
        //return i;
      }
    }

    return empty_kv();  // should never be reached
  }

  template<class MATCH_FUNC> 
  ui32 getHashed(ui32 hash, MATCH_FUNC match) const
  {
    ui32 i = hash;
    for(;; ++i)
    {
      i &= m_sz - 1;
      kv probedKv = load_kv(i);
      if(probedKv.key==EMPTY_KEY) return EMPTY_KEY;
      if( match(probedKv.key) )   return probedKv.val;
    }

    //return EMPTY_KEY;
  }
  
  template<class MATCH_FUNC> 
  ui32 findHashed(ui32 hash, MATCH_FUNC match) const
  {
    ui32 i = hash;
    for(;; ++i)
    {
      i &= m_sz - 1;
      kv probedKv = load_kv(i);
      if(probedKv.key==EMPTY_KEY) return EMPTY_KEY;
      if( match(probedKv.key) )   return i; // probedKv.val;
    }

    //return EMPTY_KEY;
  }


  bool       init(ui32   sz)
  {
    using namespace std;
    
    m_sz      =  nextPowerOf2(sz);

    kv defKv  =  empty_kv();
    // defKv.asInt    =  0;
    //defKv.key      =  EMPTY_KEY;
    //defKv.val      =  0;
    //defKv.readers  =  INIT_READERS;

    m_kvs.resize(m_sz, defKv);
    
    //for(ui32 i=0; i<m_sz; ++i) m_kvs[i] = defKv;

    //m_keys.reset(new Aui32[m_sz]);
    //m_vals.reset(new Aui32[m_sz]);
    //
    //ui32* keys = (ui32*)m_keys.get();
    //ui32* vals = (ui32*)m_vals.get();
    //TO(m_sz,i) {
    //  keys[i] = EMPTY_KEY;
    //}
    //memset(vals,0,m_sz);

    return true;
  }
  //kv          put(ui32  key, ui32 val)           const
  //{
  //  return putHashed(intHash(key), key, val, DefaultKeyCompare);
  //}
  //ui32        get(ui32  key)                     const
  //{
  //  ui32 i = intHash(key);
  //  for(;; ++i)
  //  {
  //    i  &=  m_sz - 1;
  //
  //    //ui32 probedKey =  m_keys.get()[i].load();        // atomic_load( (Aui32*)(&m_keys.get()[i]) );   //    // mint_load_32_relaxed(&m_entries[idx].key);
  //
  //    kv probedKv = load_kv(i);
  //    if(probedKv.key==key) return probedKv.val;         // m_vals.get()[i].load();                 // atomic_load( (Aui32*)(&m_keys.get()[i]) );         // mint_load_32_relaxed(&m_entries[idx].value);
  //
  //    //return m_vals.get()[i].load();                   // atomic_load( (Aui32*)(&m_keys.get()[i]) );         // mint_load_32_relaxed(&m_entries[idx].value);
  //
  //    if(probedKv.key==EMPTY_KEY)
  //      return EMPTY_KEY;
  //  }
  //
  //  return EMPTY_KEY;
  //}
  kv         read(ui32  key)                     const
  {
    ui32 i = intHash(key);
    for(;; ++i)
    {
      i  &=  m_sz - 1;

      kv probedKv = load_kv(i);
      if(probedKv.key == key) return addReaders(i, probedKv, 1);
             
      if(probedKv.key == EMPTY_KEY)                                 // needs to be taken out when deleting is implemented
        return empty_kv();
    }

    return empty_kv();
  }
  kv      endRead(ui32  key)                     const
  {
    ui32 i = intHash(key);
    for(;; ++i)
    {
      i  &=  m_sz - 1;

      kv probedKv = load_kv(i);
      if(probedKv.key == key){
        return addReaders(i, probedKv, -1);
      }
             
      if(probedKv.key == EMPTY_KEY)                                 // needs to be taken out when deleting is implemented
        return empty_kv();
    }
    return empty_kv();
  }
  kv           rm(ui32  idx)                     const
  {
    return store_kv(idx, empty_kv());
  }
  bool        del(ui32  key)                     const
  {
    ui32 i = intHash(key);
    for(;; ++i)
    {
      i  &=  m_sz - 1;

      kv probedKv = load_kv(i);
      if(probedKv.key == key) return compexchange_kv(i, &probedKv.asInt, empty_kv().asInt);
             
      if(probedKv.key == EMPTY_KEY)                                 // needs to be taken out when deleting is implemented
        return false;
    }
    return false;
  }
  ui32       size()                              const
  {
    return m_sz;
  }
  ui32  hashBytes(void* buf, ui32 len)           const
  {
    ui32  rethash  =  0;
    ui32* cur      =  (ui32*)buf;
    ui32  loops    =  len/sizeof(ui32);
    ui32* end      =  cur + loops + 1;
    for(; cur!=end; ++cur){ rethash ^= intHash(*cur); }

    ui32  rem      =  len - loops;
    ui32  lst      =  0;
    ui8*  end8     =  (ui8*)end;
    for(ui8 i=0; i<rem; ++i){ lst ^= *end8 << (rem-1-i); }
    
    rethash ^= intHash(lst);

    return rethash;
  }
};
class   ConcurrentList
{
public:
  union HeadUnion
  {
    struct { uint32_t cnt; uint32_t idx; };
    uint64_t asInt;
  };
  
  using    ui32  =  uint32_t;  // need to be i32 instead for the ConcurrentStore indices?
  using    ui64  =  uint64_t;
  using ListVec  =  std::vector< std::atomic<ui32> >;  // does this need to be atomic? all the contention should be over the head
  using HeadInt  =  ui64;
  using    Head  =  std::atomic<ui64>;

  const static ui32 LIST_END = 0xFFFFFFFF;

private:
  ListVec  m_lv;
  Head      m_h;

public:
  ConcurrentList(){}
  ConcurrentList(ui32 size) : 
    m_lv(size)
  {
    for(uint32_t i=0; i<(size-1); ++i) m_lv[i]=i+1;
    m_lv[size-1] = LIST_END;

    m_h = 0;
  }

  auto     nxt() -> uint32_t    // moves forward in the list and return the previous index
  {
    HeadUnion  curHead;
    HeadUnion  nxtHead;

    curHead.asInt  =  m_h.load();
    do 
    {
      if(curHead.idx==LIST_END) return LIST_END;

      nxtHead.idx  =  m_lv[curHead.idx];
      nxtHead.cnt  =  curHead.cnt + 1;
    } while( !m_h.compare_exchange_strong(curHead.asInt, nxtHead.asInt) );

    //return nxtHead.idx;
    return curHead.idx;
  }
  auto    free(ui32 idx) -> uint32_t   // not thread safe yet when reading from the list, but it doesn't matter because you shouldn't be reading while freeing anyway?
  {
    HeadUnion  curHead;
    HeadUnion  nxtHead;
    uint32_t    retIdx;

    curHead.asInt = m_h.load();
    do 
    {
      retIdx = m_lv[idx] = curHead.idx;
      nxtHead.idx  =  idx;
      nxtHead.cnt  =  curHead.cnt + 1;
    } while( !m_h.compare_exchange_strong(curHead.asInt, nxtHead.asInt) );

    return retIdx;
  }
  auto   count() const -> uint32_t
  {
    return ((HeadUnion*)(&m_h))->cnt;
  }
  auto     idx() -> uint32_t
  {
    return ((HeadUnion*)(&m_h))->idx;
  }            // not thread safe
  auto    list() -> ListVec const* 
  {
    return &m_lv;
  }            // not thread safe
  ui32  lnkCnt()                     // not thread safe
  {
    ui32    cnt = 0;
    auto      l = list();
    ui32 curIdx = idx();
    while( curIdx != LIST_END ){
      curIdx = l->at(curIdx).load();
      ++cnt;
    }
    
    return cnt;
  }
};
class  ConcurrentStore
{
public:
  using IDX  =  i32;

  const static ui32 LIST_END = ConcurrentList::LIST_END;

private:
  ui8*                    m_addr;
  ui32               m_blockSize;
  ui32              m_blockCount;
  ai32              m_blocksUsed;
  ConcurrentList            m_cl;

  i32*            stPtr(i32  blkIdx)
  {
    return (i32*)( ((ui8*)m_addr) + blkIdx*m_blockSize );
  }
  i32          nxtBlock(i32  blkIdx)
  {
    return *(stPtr(blkIdx));
  }
  i32     blockFreeSize()
  {
    return m_blockSize - sizeof(IDX);
  }
  ui8*     blockFreePtr(i32  blkIdx)
  {
    return ((ui8*)stPtr(blkIdx)) + sizeof(IDX);
  }
  i32      blocksNeeded(i32     len, i32* out_rem=nullptr)
  {
    i32  freeSz   = blockFreeSize();
    i32  byteRem  = len % freeSz;
    i32  blocks   = len / freeSz + (byteRem? 1 : 0);    // should never be 0 if blocksize is greater than the size of the index type

    if(out_rem) *out_rem = byteRem;

    return blocks;
  }
  i32          blockLen(i32  blkIdx)
  {
    IDX nxt = nxtBlock(blkIdx);
    if(nxt < 0) return -nxt;

    return blockFreeSize();
  }
  size_t     writeBlock(i32  blkIdx, void* bytes) // i64 len = -1)
  {
    i32   blkFree  =  blockFreeSize();
    ui8*        p  =  blockFreePtr(blkIdx);
    i32       nxt  =  nxtBlock(blkIdx);
    size_t cpyLen  =  nxt<0? -nxt : blkFree;           // if next is negative, then it will be the length of the bytes in that block
    memcpy(p, bytes, cpyLen);

    return cpyLen;

    //bool     fill = len < -1 || blkFree < len;
    //size_t cpyLen = fill? blkFree : len;
  }
  size_t      readBlock(i32  blkIdx, void* bytes)
  {
    i32   blkFree  =  blockFreeSize();
    ui8*        p  =  blockFreePtr(blkIdx);
    i32       nxt  =  nxtBlock(blkIdx);
    size_t cpyLen  =  nxt<0? -nxt : blkFree;           // if next is negative, then it will be the length of the bytes in that block
    memcpy(bytes, p, cpyLen);

    return cpyLen;
  }

public:
  ConcurrentStore(){}
  ConcurrentStore(ui8* addr, ui32 blockSize, ui32 blockCount) :
    m_addr(addr),
    m_blockSize(blockSize),
    m_blockCount(blockCount),
    m_blocksUsed(0),
    m_cl(m_blockCount)
  {
    assert(blockSize > sizeof(IDX));
  }

  i32        alloc(i32    size, i32* out_blocks=nullptr)
  {
    i32 byteRem = 0;
    i32 blocks  = blocksNeeded(size, &byteRem);
    //if(out_blocks) *out_blocks = blocks;

    i32   st = m_cl.nxt();                                     // stBlk  is starting block
    if(st==LIST_END){
      if(out_blocks) *out_blocks = 0; 
      return LIST_END; 
    }
    i32  cur = st;                                             // curBlk is current  block
    i32  cnt = 0;
    i32  nxt = 0;   // m_cl.nxt();
    for(i32 i=0; i<blocks-1; ++i)
    {
      i32* p = stPtr(cur);
      nxt    = m_cl.nxt();
      if(nxt==LIST_END){ 
        //if(out_blocks) *out_blocks = -cnt;                     // negative count if the full amount of blocks can't be allocated
        break;
      }

      cur     =  nxt;        // m_cl.nxt();
      ++cnt;
      m_blocksUsed.fetch_add(1);
      *p      =  cur;
    }
    i32* p  =  (i32*)stPtr(cur);
    *p      =  byteRem? -byteRem : -blockFreeSize();
    if(out_blocks){ *out_blocks = nxt==LIST_END? -cnt : cnt; } 

    return st;
  }
  void        free(i32     idx)        // frees a list/chain of blocks
  {
    i32   cur   =  idx;                // cur is the current block index
    i32    nxt  =  *stPtr(cur);        // nxt is the next block index
    for(; nxt>0; nxt=*stPtr(cur) ){ 
      m_cl.free(cur);
      m_blocksUsed.fetch_add(-1);
      cur  =  nxt;
    }
    m_cl.free(cur);

    //i32*    p   =  stPtr(cur);         // p is the pointer to the first bytes of the block
    //i32    nxt  =  *p;                 // nxt is the next block index
    //for(; *p>0; p=stPtr(cur) )    
    //{ 
    //  nxt  =  *p;
    //  m_cl.free(cur);
    //  cur  =  nxt;
    //}
    //m_cl.free(cur);
  }
  void         put(i32  blkIdx, void* bytes, i32 len)
  {
    ui8*       b   = (ui8*)bytes;
    i32    blocks  =  blocksNeeded(len);
    i32       cur  =  blkIdx;
    for(i32 i=0; i<blocks; ++i)
    {
      b   +=  writeBlock(cur, b);
      cur  =  nxtBlock(cur);
    }

    //i32  remBytes  =  0;
    //i32    blocks  =  blocksNeeded(len, &remBytes);
  }
  size_t       get(i32  blkIdx, void* bytes)
  {
    if(blkIdx == LIST_END){ return 0; }

    size_t    len = 0;
    size_t  rdLen = 0;
    ui8*        b = (ui8*)bytes;
    i32       cur = blkIdx;
    i32       nxt;
    while(true)
    {
      nxt    =  nxtBlock(cur);
      rdLen  =  readBlock(cur, b);                         // rdLen is read length
      b     +=  rdLen;
      len   +=  rdLen;
      if(nxt<0 || nxt==LIST_END) break;

      cur    =  nxt;
    }
    return len;
  }
  //bool     compare(IDX blkIdxA, IDX blkIdxB)
  //{
  //  // && (nxtA=nxtBlock(nxtA))>=0
  //  // && (nxtB=nxtBlock(nxtB))>=0 )
  //  // if(nxtA < 0) break;
  //  // if(nxtB < 0) break;
  //
  //  size_t alen=0; size_t blen=0; IDX nxtA=blkIdxA; IDX nxtB=blkIdxB; bool blkcmp=false;
  //  while( blockCompare(nxtA, nxtB, &alen, &blen) )
  //  {      
  //    nxtA = nxtBlock(nxtA);
  //    nxtB = nxtBlock(nxtB);
  //    bool lastA = nxtA<0;
  //    bool lastB = nxtB<0;
  //    if(lastA ^  lastB) return false;  // if one is on their last block but the other is not, return false - not actually needed? - it is needed because the blocks could be the same while one is the last and the other is not?
  //    if(lastA && lastB) return  true;
  //  }
  //
  //  return false;
  //}
  bool     compare(void* buf, size_t len, IDX blkIdx)
  {
    IDX   curidx  =  blkIdx;
    i32      nxt  =  nxtBlock(curidx);
    auto   blksz  =  blockFreeSize();
    ui8*  curbuf  =  (ui8*)buf;
    auto  curlen  =  len;
    while(true)
    {
      auto p = blockFreePtr(curidx);
      if(nxt >= 0){
        if(curlen < blksz){ return false; }
        else if( memcmp(curbuf, p, blksz)!=0 ){ return false; }
      }else if(-nxt != curlen){ return false; }
      else{ return memcmp(curbuf, p, curlen)==0; }

      curbuf  +=  blksz;
      curlen  -=  blksz;
      curidx   =  nxt;
      nxt      =  nxtBlock(curidx);
    }

    return true;     
  }
  auto        list() const -> ConcurrentList const&
  {
    return m_cl;
  }
  auto        data() const -> const void*
  {
    return (void*)m_addr;
  }
};
class     SharedMemory
{
private:
  //std::unique_ptr<void*> ptr;
  ui64            m_sz;
  void*          m_ptr;
  HANDLE  m_fileHandle;

public:
  static void FreeMem(void* p)
  {
  }

  SharedMemory(){}
  SharedMemory(ui64 sz) :
    m_sz(sz)
  {
    m_fileHandle = CreateFileMapping(
      INVALID_HANDLE_VALUE,
      NULL,
      PAGE_READWRITE,
      0,
      (DWORD)m_sz,
      "Global\\simdb_15");

    if(m_fileHandle==NULL){/*error*/}

    m_ptr = MapViewOfFile(m_fileHandle,   // handle to map object
      FILE_MAP_ALL_ACCESS,   // read/write permission
      0,
      0,
      m_sz);
  }

  ~SharedMemory()
  {
    UnmapViewOfFile(m_ptr);
    CloseHandle(m_fileHandle);
  }

  auto data() -> void*
  {
    return m_ptr;
  }
  ui64 size() const
  {
    return m_sz;
  }
};
class            simdb
{
private:
  using kv = ConcurrentHash::kv;

  //void*            m_mem;     // todo: make this a unique_ptr
  SharedMemory     m_mem;
  ConcurrentStore   m_cs;     // store data in blocks and get back indices
  ConcurrentHash    m_ch;     // store the indices of keys and values - contains a ConcurrentList

  static const ui32  EMPTY_KEY = ConcurrentHash::EMPTY_KEY;      // 28 bits set 
  //static bool  CompareBlocks(simdb* ths, i32 a, i32 b){ return ths->m_cs.compare(a,b); }
  static const ui32   LIST_END = ConcurrentStore::LIST_END;
  static bool   CompareBlock(simdb* ths, void* buf, size_t len, i32 blkIdx)
  { 
    return ths->m_cs.compare(buf, len, blkIdx);
  }

public:
  simdb(){}
  simdb(ui32 blockSize, ui32 blockCount) : 
    m_mem(blockSize*blockCount),
    m_cs( (ui8*)m_mem.data(), blockSize, blockCount),               // todo: change this to a void*
    m_ch( blockCount )
  {}
  //simdb(ui32 blockSize, ui32 blockCount) : 
  //  m_mem( malloc(blockSize*blockCount) ),
  //   m_cs( (ui8*)m_mem, blockSize, blockCount),            // todo: change this to a void*
  //   m_ch( blockCount )
  //{}

  i32      put(void*   key, ui32  klen, void* val, ui32 vlen)
  {
    // todo: need to clean up the allocation 
    i32 blkcnt = 0;
    i32   kidx = m_cs.alloc(klen, &blkcnt);    // kidx is key index
    if(kidx==LIST_END) return EMPTY_KEY;
    if(blkcnt<0){
      m_cs.free(kidx);
      return EMPTY_KEY;
    }
    i32   vidx = m_cs.alloc(vlen, &blkcnt);
    if(vidx==LIST_END) return EMPTY_KEY;   // vidx is value index
    if(blkcnt<0){
      m_cs.free(vidx);
      return EMPTY_KEY;
    }

    m_cs.put(kidx, key, klen);
    m_cs.put(vidx, val, vlen);

    ui32 keyhash = m_ch.hashBytes(key, klen);
    auto ths = this;                                          // this silly song and dance is because the this pointer can't be passed to a lambda
    auto  kv = m_ch.putHashed(keyhash, kidx, vidx, 
       // [ths](ui32 a, ui32 b){return CompareBlocks(ths,a,b); }); 
      [ths, key, klen](ui32 blkidx){ return CompareBlock(ths,key,klen,blkidx); });

    if(kv.key != ConcurrentHash::EMPTY_KEY){ m_cs.free(kv.val); m_cs.free(kv.key); }

    return kidx;
  }
  i32      get(void*   key, i32    len)
  {
    ui32 keyhash  =  m_ch.hashBytes(key, len);
    auto     ths  =  this;
    return m_ch.getHashed(keyhash, 
      [ths, key, len](ui32 blkidx){ return CompareBlock(ths,key,len,blkidx); });
  }
  auto     get(i32  blkIdx, void*  out_buf) -> size_t
  {
    if(blkIdx==EMPTY_KEY) return 0;

    return m_cs.get(blkIdx, out_buf);           // copy into the buf starting at the blkidx
  } 
  auto     get(const std::string key, void* out_buf) -> size_t
  {
    ui32 idx = get( (void*)key.data(), (ui32)key.length() );
    
    return get(idx, out_buf);
  }
  void      rm(const std::string key)
  {
    auto  len = (ui32)key.length();
    auto  ths = this;
    auto kbuf = (void*)key.data();
    auto hash = m_ch.hashBytes(kbuf, len);
    ui32  idx = m_ch.findHashed(hash,
      [ths, kbuf, len](ui32 blkidx){ return CompareBlock(ths,kbuf,len,blkidx); });
        
    kv  prev = m_ch.rm(idx);
    if(prev.key!=EMPTY_KEY) m_cs.free(prev.key);
    if(prev.val!=EMPTY_KEY) m_cs.free(prev.val);
  }
  auto    data() const -> const void*
  {
    return m_cs.data();
  }
  ui64    size() const
  {
    return m_mem.size();
  }
};

#endif






//bool     blockCompare(i32 blkIdxA, i32 blkIdxB, size_t* out_alen=nullptr, size_t* out_blen=nullptr)
//{
//  i32 alen = blockLen(blkIdxA);
//  i32 blen = blockLen(blkIdxB);
//  if(out_alen) *out_alen = alen;
//  if(out_blen) *out_blen = blen;
//  if(alen != blen) return false;         // if their lengths aren't even the same, they can't be the same
//
//  ui8* pa  =  blockFreePtr(blkIdxA);
//  ui8* pb  =  blockFreePtr(blkIdxB);
//  return memcmp(pa, pb, alen)==0;
//}

//i32  blocks = blocksNeeded(len);
//for(i32 i=0; i<blocks; ++i)
//i32 nxt;

//void         get(i32  blkIdx, void* bytes, i32 len)
//{
//  ui8*      b = (ui8*)bytes;
//  i32  blocks = blocksNeeded(len);
//  i32     cur = blkIdx;
//  for(i32 i=0; i<blocks; ++i)
//  {
//    b   +=  readBlock(cur, b);
//    cur  =  nxtBlock(cur);
//  }
//}

//
//mutable Mut        m_mut;

//ui32         load_key(ui32  i)          const
//{
//  using namespace std;
//  
//  kv keyval;
//  keyval.asInt    =  atomic_load<ui64>( (Aui64*)&m_kvs.data()[i].asInt );              // Load the key that was there.
//  ui32 probedKey  =  keyval.key;
//
//  return probedKey;
//}

//kv         incReaders(ui32  i, kv curKv) const                                     // increment the readers by one and return the previous kv from the successful swap 
//{
//  return addReaders(i, curKv, 1);
//}
//kv         decReaders(ui32  i, kv curKv) const                                     // increment the readers by one and return the previous kv from the successful swap 
//{
//  return addReaders(i, curKv, -1);
//}

//void         resize(size_t sz) const
//{
//  //UnqLock lock;
//  //  m_keys.resize(sz);
//  //  m_vals.resize(sz);
//  //lock.unlock();
//}

//bool     lock()                    const
//{
//  m_mut.lock();
//}
//bool   unlock()                    const
//{
//  m_mut.unlock();
//}

//ui32         put(ui32  key, ui32 val) const
//{
//  using namespace std;

//  kv desired;
//  desired.key  =  key;
//  desired.val  =  val;
//  ui32      i  =  intHash(key);
//  for(;; ++i)
//  {
//    i  &=  m_sz-1;

//    //ui32 probedKey = load_key(i);
//    kv probedKv = load_kv(i);
//    if(probedKv.key != key)
//    {
//      if(probedKv.key != EMPTY_KEY) continue;                                               // The entry was either free, or contains another key.  // Usually, it contains another key. Keep probing.
//              
//      kv   expected   =  empty_kv();
//      //expected.asInt  =  0;
//      //expected.key    =  EMPTY_KEY;

//      bool   success  =  compexchange_kv(i, &expected.asInt, desired.asInt);
//      //bool   success  =  m_keys.get()[i].compare_exchange_strong(desired, key);           // The entry was free. Now let's try to take it using a CAS. 
//      if( !success && (expected.key!=key) ) continue;                                       // Another thread just stole it from underneath us.
//    }                                                                                       // Either we just added the key, or another thread did.

//    //m_vals.get()[i].store(val);
//    store_kv(i, desired);
//    return i;
//  }
//  return i;
//}

