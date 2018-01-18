// Copyright 2008 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#pragma once

// BLOB

// Blobs in Dolphin are read only Binary Large OBjects. For example, a typical DVD image.
// Often, you may want to store these things in a highly compressed format, but still
// allow random access. Or you may store them on an odd device, like raw on a DVD.

// Always read your BLOBs using an interface returned by CreateBlobReader(). It will
// detect whether the file is a compressed blob, or just a big hunk of data, or a drive, and
// automatically do the right thing.

#include <array>
#include <memory>
#include <optional>
#include <string>

#include "Common/CommonTypes.h"
#include "Common/Swap.h"

namespace DiscIO
{
// Increment CACHE_REVISION (ISOFile.cpp & GameFile.cpp) if the enum below is modified
enum class BlobType
{
  PLAIN,
  DRIVE,
  DIRECTORY,
  GCZ,
  CISO,
  WBFS,
  TGC
};

class BlobReader
{
public:
  virtual ~BlobReader() {}
  virtual BlobType GetBlobType() const = 0;
  virtual u64 GetRawSize() const = 0;
  virtual u64 GetDataSize() const = 0;

  // NOT thread-safe - can't call this from multiple threads.
  virtual bool Read(u64 offset, u64 size, u8* out_ptr) = 0;
  template <typename T>
  std::optional<T> ReadSwapped(u64 offset)
  {
    T temp;
    if (!Read(offset, sizeof(T), reinterpret_cast<u8*>(&temp)))
      return {};
    return Common::FromBigEndian(temp);
  }

protected:
  BlobReader() {}
};

// Provides caching and byte-operation-to-block-operations facilities.
// Used for compressed blob and direct drive reading.
// NOTE: GetDataSize() is expected to be evenly divisible by the sector size.
class SectorReader : public BlobReader
{
public:
  virtual ~SectorReader() = 0;

  bool Read(u64 offset, u64 size, u8* out_ptr) override;

protected:
  void SetSectorSize(int blocksize);
  int GetSectorSize() const { return m_block_size; }
  // Set the chunk size -> the number of blocks to read at a time.
  // Default value is 1 but that is too low for physical devices
  // like CDROMs. Setting this to a higher value helps reduce seeking
  // and IO overhead by batching reads. Do not set it too high either
  // as large reads are slow and will take too long to resolve.
  void SetChunkSize(int blocks);
  int GetChunkSize() const { return m_chunk_blocks; }
  // Read a single block/sector.
  virtual bool GetBlock(u64 block_num, u8* out) = 0;

  // Read multiple contiguous blocks.
  // Default implementation just calls GetBlock in a loop, it should be
  // overridden in derived classes where possible.
  virtual bool ReadMultipleAlignedBlocks(u64 block_num, u64 num_blocks, u8* out_ptr);

private:
  struct Cache
  {
    std::vector<u8> data;
    u64 block_idx = 0;
    u32 num_blocks = 0;

    // [Pseudo-] Least Recently Used Shift Register
    // When an empty cache line is needed, the line with the lowest value
    // is taken and reset; the LRU register is then shifted down 1 place
    // on all lines (low bit discarded). When a line is used, the high bit
    // is set marking it as most recently used.
    u32 lru_sreg = 0;

    void Reset()
    {
      block_idx = 0;
      num_blocks = 0;
      lru_sreg = 0;
    }
    void Fill(u64 block, u32 count)
    {
      block_idx = block;
      num_blocks = count;
      // NOTE: Setting only the high bit means the newest line will
      //   be selected for eviction if every line in the cache was
      //   touched. This gives MRU behavior which is probably
      //   desirable in that case.
      MarkUsed();
    }
    bool Contains(u64 block) const { return block >= block_idx && block - block_idx < num_blocks; }
    void MarkUsed() { lru_sreg |= 0x80000000; }
    void ShiftLRU() { lru_sreg >>= 1; }
    bool IsLessRecentlyUsedThan(const Cache& other) const { return lru_sreg < other.lru_sreg; }
  };

  // Gets the cache line that contains the given block, or nullptr.
  // NOTE: The cache record only lasts until it expires (next GetEmptyCacheLine)
  const Cache* FindCacheLine(u64 block_num);

  // Finds the least recently used cache line, resets and returns it.
  Cache* GetEmptyCacheLine();

  // Combines FindCacheLine with GetEmptyCacheLine and ReadChunk.
  // Always returns a valid cache line (loading the data if needed).
  // May return nullptr only if the cache missed and the read failed.
  const Cache* GetCacheLine(u64 block_num);

  // Read all bytes from a chunk of blocks into a buffer.
  // Returns the number of blocks read (may be less than m_chunk_blocks
  // if chunk_num is the last chunk on the disk and the disk size is not
  // evenly divisible into chunks). Returns zero if it fails.
  u32 ReadChunk(u8* buffer, u64 chunk_num);

#if defined(_MSC_VER) && _MSC_VER <= 1800
#define CACHE_LINES 32
#else
	static constexpr int CACHE_LINES = 32;
#endif
	u32 m_block_size   = 0;  // Bytes in a sector/block
	u32 m_chunk_blocks = 1;  // Number of sectors/blocks in a chunk
	std::array<Cache, CACHE_LINES> m_cache;
};

// Factory function - examines the path to choose the right type of BlobReader, and returns one.
std::unique_ptr<BlobReader> CreateBlobReader(const std::string& filename);

typedef bool (*CompressCB)(const std::string& text, float percent, void* arg);

bool CompressFileToBlob(const std::string& infile_path, const std::string& outfile_path,
                        u32 sub_type = 0, int sector_size = 16384, CompressCB callback = nullptr,
                        void* arg = nullptr);
bool DecompressBlobToFile(const std::string& infile_path, const std::string& outfile_path,
                          CompressCB callback = nullptr, void* arg = nullptr);

}  // namespace
