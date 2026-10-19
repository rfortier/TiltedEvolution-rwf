#include "Record.h"

#include <zlib.h>

void Record::CopyRecordData(Record& aRhs)
{
    m_formType = aRhs.m_formType;
    m_dataSize = aRhs.m_dataSize;
    m_flags = aRhs.m_flags;
    m_formId = aRhs.m_formId;
    m_versionControl = aRhs.m_versionControl;
    m_formVersion = aRhs.m_formVersion;
    m_vcVersion = aRhs.m_vcVersion;
}

void Record::SetBaseId(uint32_t aBaseId)
{
    m_formId &= 0x00FFFFFF;
    m_formId += aBaseId;
}

void Record::IterateChunks(const std::function<void(ChunkId, Buffer::Reader&)>& aCallback)
{
    Buffer buffer(reinterpret_cast<uint8_t*>(this) + sizeof(Record), m_dataSize);
    Buffer::Reader reader(&buffer);

    Buffer pDecompressed;
    if (Compressed())
    {
        uint32_t size = 0;
        // m_dataSize is file data and nothing has checked it against 4 yet, so the
        // subtraction below would wrap to a huge value and hand inflate a compressed size
        // that runs off the end of the record. This also covers the read of the length
        // itself: a body shorter than four bytes has no size field to read.
        if (m_dataSize < 4 || !reader.ReadBytes(reinterpret_cast<uint8_t*>(&size), 4))
            return;

        // size is the declared decompressed length, so it sizes an allocation before a
        // single byte of it has been produced. The cap is a deliberate ceiling rather than
        // a format limit: an inflated claim here would otherwise ask the allocator for
        // whatever the file names, and the failure would surface as a bad_alloc thrown
        // through a noexcept caller. A record nowhere near this size is malformed anyway.
        static constexpr uint32_t kMaxDecompressedSize = 1 << 24;
        if (size == 0 || size > kMaxDecompressedSize)
            return;

        // Resize reports failure without changing the buffer, which would leave the reader
        // below wrapping a buffer whose size and data disagree.
        if (!pDecompressed.Resize(size))
            return;

        const uint32_t fieldSize = m_dataSize - 4;

        DecompressChunkData(reader.GetDataAtPosition(), fieldSize, pDecompressed.GetWriteData(), pDecompressed.GetSize());

        reader = Buffer::Reader(&pDecompressed);
    }

    uint32_t largeDataSize = 0;

    while (!reader.Eof())
    {
        // Eof() only says the cursor is not at the end yet, so a chunk header can start in
        // the last few bytes of the data. The header is six bytes formed by pointer
        // arithmetic and dereferenced on the next line, so it has to be wholly present.
        const size_t chunkPosition = reader.GetBytePosition();
        const size_t chunkDataSize = reader.GetBuffer()->GetSize();
        if (chunkPosition > chunkDataSize || chunkDataSize - chunkPosition < sizeof(Record::Chunk))
            return;

        Record::Chunk* pChunk = reinterpret_cast<Chunk*>(reader.GetDataAtPosition());
        reader.Advance(sizeof(Record::Chunk));

        uint32_t dataSize = pChunk->m_dataSize;
        if (pChunk->m_dataSize == 0)
        {
            dataSize = largeDataSize;
        }

        // Chunk XXXX will have the next data size stored at the start of the field
        // Doesn't ever seem to trigger, but it's in the spec so might as well leave it in
        // https://en.uesp.net/wiki/Skyrim_Mod:Mod_File_Format#Fields
        if (pChunk->m_chunkId == ChunkId::XXXX_ID)
        {
            reader.ReadBytes(reinterpret_cast<uint8_t*>(&largeDataSize), 4);
            reader.Reverse(4);
        }

        Buffer::Reader chunk(reader);

        reader.Advance(dataSize);

        aCallback(pChunk->m_chunkId, chunk);
    }
}

void Record::DecompressChunkData(const void* apCompressedData, size_t aCompressedSize, void* apDecompressedData, size_t aDecompressedSize)
{
    z_stream compressionStream;
    compressionStream.next_in = (Bytef*)apCompressedData;
    compressionStream.avail_in = (uInt)(aCompressedSize);
    compressionStream.next_out = (Bytef*)apDecompressedData;
    compressionStream.avail_out = (uInt)aDecompressedSize;
    compressionStream.zalloc = Z_NULL;
    compressionStream.zfree = Z_NULL;
    compressionStream.opaque = Z_NULL;

    inflateInit(&compressionStream);
    int res = inflate(&compressionStream, Z_NO_FLUSH);
    if (res < Z_OK)
        spdlog::error("Failed to decompress chunk of data (inflate): {}.", res);
    res = inflateEnd(&compressionStream);
    if (res < Z_OK)
        spdlog::error("Failed to decompress chunk of data: {}", res);
}

void Record::DiscoverChunks()
{
    IterateChunks(
        [&](ChunkId aChunkId, Buffer::Reader& aReader)
        {
            switch (aChunkId)
            {
                /*
            case ChunkId::XMRK_ID:
                spdlog::info("XMRK found in form {:X}", m_formId);
                break;
                */
            }
        });
}
