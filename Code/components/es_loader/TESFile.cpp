#include "TESFile.h"

#include <filesystem>
#include <fstream>

namespace ESLoader
{
TESFile::TESFile(Map<String, uint8_t>& aMasterFiles)
    : m_masterFiles(aMasterFiles)
{
}

void TESFile::Setup(uint8_t aStandardId)
{
    m_standardId = aStandardId;
    m_formIdPrefix = m_standardId * 0x1000000;
}

void TESFile::Setup(uint16_t aLiteId)
{
    m_liteId = aLiteId;
    m_formIdPrefix = 0xFE000000 + (m_liteId * 0x1000);
}

bool TESFile::LoadFile(const std::filesystem::path& acPath) noexcept
{
    m_filename = acPath.filename().string();

    const uintmax_t fileSize = std::filesystem::file_size(acPath);

    // Resize reports failure and leaves the buffer at its previous size when it fails, and
    // the read below then writes fileSize bytes - a number taken from the file itself -
    // into an allocation that does not hold them.
    if (!m_buffer.Resize(fileSize))
        return false;

    std::ifstream file(acPath, std::ios::binary);
    if (file.fail())
    {
        spdlog::error("Failed to open plugin {}", m_filename);
        return false;
    }

    file.read(reinterpret_cast<char*>(m_buffer.GetWriteData()), fileSize);

    return true;
}

bool TESFile::IndexRecords(RecordCollection& aRecordCollection) noexcept
{
    if (m_filename.size() == 0)
        return false;

    Buffer::Reader reader(&m_buffer);

    while (true)
    {
        if (!ReadGroupOrRecord(reader, aRecordCollection))
            break;
    }

    return true;
}

bool TESFile::ReadGroupOrRecord(Buffer::Reader& aReader, RecordCollection& aRecordCollection) noexcept
{
    if (aReader.Eof())
        return false;

    // Eof() only answers "the cursor is not at the end yet", so a record header can begin
    // inside the last few bytes of the file. Both branches below read a fixed-size header
    // straight out of m_buffer - the record branch forms a Record* by pointer arithmetic
    // and dereferences it immediately - so the whole header has to be present first.
    // The comparison is written as a subtraction, not "position + headerSize < bufferSize",
    // because Advance() does not clamp: a record whose declared size overruns leaves the
    // cursor past the end, and such a sum would then wrap around and report plenty of room.
    const size_t position = aReader.GetBytePosition();
    const size_t bufferSize = aReader.GetBuffer()->GetSize();
    const size_t headerSize = sizeof(Record) > sizeof(Group) ? sizeof(Record) : sizeof(Group);
    if (position > bufferSize || bufferSize - position < headerSize)
        return false;

    uint32_t type = 0;
    aReader.ReadBytes(reinterpret_cast<uint8_t*>(&type), 4);
    uint32_t size = 0;
    aReader.ReadBytes(reinterpret_cast<uint8_t*>(&size), 4);
    aReader.Reverse(8);

    if (type == static_cast<uint32_t>(FormEnum::GRUP))
    {
        // size is the group's declared length, read out of the file with nothing comparing
        // it against what is left, so "position + size" can address past the end and the
        // loop below would recurse into bytes that are not there. An overrunning group is
        // not clamped to the end either: one that lies about its extent cannot be walked
        // correctly, so the walk stops and the caller abandons the rest of the file.
        if (size > bufferSize - position)
            return false;

        const size_t endOfGroup = position + size;
        aReader.Advance(sizeof(Group));

        while (aReader.GetBytePosition() < endOfGroup)
        {
            // The child's result used to be ignored. It leaves the cursor untouched when it
            // cannot read another header, so ignoring it is also what would spin this loop
            // at a single position forever once such a header exists.
            if (!ReadGroupOrRecord(aReader, aRecordCollection))
                return false;
        }
    }
    else // Records
    {
        Record* pRecord = reinterpret_cast<Record*>(m_buffer.GetWriteData() + position);

        // size is the record's declared body length: nothing ties those four file bytes to
        // the file's real extent. Record::IterateChunks hands exactly that many bytes, read
        // from just after the header, to a Buffer of its own, so an overstated size turns
        // that into a read past the end of m_buffer. Checked before the record is parsed
        // rather than after, because the declaration is the only thing that says where this
        // record ends - there is no smaller body to fall back to.
        if (size > bufferSize - position - headerSize)
            return false;

        switch (pRecord->GetType())
        {
        case FormEnum::TES4:
        {
            TES4* pFileHeader = reinterpret_cast<TES4*>(pRecord);

            TES4 fileHeader;
            fileHeader.CopyRecordData(*pFileHeader);
            fileHeader.ParseChunks(*pFileHeader, m_parentToFormIdPrefix);

            uint8_t parentId = 0;
            for (const Chunks::MAST& master : fileHeader.m_masterFiles)
            {
                m_parentToFormIdPrefix[parentId] = ((uint32_t)m_masterFiles[master.m_masterName]) << 24;
                parentId++;
            }

            m_parentToFormIdPrefix[parentId] = m_formIdPrefix;

            break;
        }
        // case FormEnum::ACHR:
        case FormEnum::REFR:
        {
            REFR parsedRecord = CopyAndParseRecord<REFR>(pRecord);
            aRecordCollection.m_objectReferences[parsedRecord.GetFormId()] = parsedRecord;
            break;
        }
        case FormEnum::CELL: break;
        case FormEnum::CLMT:
        {
            CLMT parsedRecord = CopyAndParseRecord<CLMT>(pRecord);
            aRecordCollection.m_climates[parsedRecord.GetFormId()] = parsedRecord;
            break;
        }
        case FormEnum::NPC_:
        {
            NPC parsedRecord = CopyAndParseRecord<NPC>(pRecord);
            aRecordCollection.m_npcs[parsedRecord.GetFormId()] = parsedRecord;
            break;
        }
        case FormEnum::CONT:
        {
            CONT parsedRecord = CopyAndParseRecord<CONT>(pRecord);
            aRecordCollection.m_containers[parsedRecord.GetFormId()] = parsedRecord;
            break;
        }
        case FormEnum::GMST:
        {
            GMST parsedRecord = CopyAndParseRecord<GMST>(pRecord);
            aRecordCollection.m_gameSettings[parsedRecord.GetFormId()] = parsedRecord;
            break;
        }
        case FormEnum::WRLD:
        {
            WRLD parsedRecord = CopyAndParseRecord<WRLD>(pRecord);
            aRecordCollection.m_worlds[parsedRecord.GetFormId()] = parsedRecord;
        }
        case FormEnum::NAVM:
        {
            NAVM parsedRecord = CopyAndParseRecord<NAVM>(pRecord);
            aRecordCollection.m_navMeshes[parsedRecord.GetFormId()] = parsedRecord;
        }
        }

        // pRecord->DiscoverChunks();

        if (pRecord->GetType() != FormEnum::TES4)
        {
            Record record;
            record.CopyRecordData(*pRecord);
            record.SetBaseId(GetFormIdPrefix(pRecord->GetFormId(), m_parentToFormIdPrefix));
            aRecordCollection.m_allRecords[pRecord->GetFormId()] = *pRecord;
        }

        aReader.Advance(sizeof(Record) + size);
    }

    return true;
}

template <typename T>
concept ExpectsGRUP = requires(T t) { &T::ParseGRUP; };

template <class T> T TESFile::CopyAndParseRecord(Record* pRecordHeader)
{
    T* pRecord = reinterpret_cast<T*>(pRecordHeader);

    T parsedRecord;
    parsedRecord.CopyRecordData(*pRecord);
    parsedRecord.SetBaseId(TESFile::GetFormIdPrefix(pRecord->GetFormId(), m_parentToFormIdPrefix));
    parsedRecord.ParseChunks(*pRecord, m_parentToFormIdPrefix);

    // If the record expects a subgroup right after, parse it? Or do we not care since we load everything?
    if constexpr (ExpectsGRUP<T>)
    {
        //    ParseGRUP(pRecord, parsedRecord);
    }

    return parsedRecord;
}

template <class T> void TESFile::ParseGRUP(Record* pRecordHeader, T& aRecord)
{
    // aRecord.ParseGRUP();
}

uint32_t TESFile::GetFormIdPrefix(uint32_t aFormId, Map<uint8_t, uint32_t>& aParentToFormIdPrefix) noexcept
{
    auto baseId = (uint8_t)(aFormId >> 24);
    const auto masterId = aParentToFormIdPrefix.find(baseId);

    if (masterId == std::end(aParentToFormIdPrefix))
    {
        // TODO: this is weird, but for some reason, in Skyrim.esm,
        // the GMST record with EDID "iDaysToRespawnVendor" has a base id of 0x01
        spdlog::warn("Form id prefix not found: {:X}", baseId);
        return 0;
    }

    return masterId->second;
}

} // namespace ESLoader
