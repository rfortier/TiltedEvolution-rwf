#include "Chunks.h"

#include "Record.h"
#include <ESLoader.h>

namespace Chunks
{

namespace
{
// Bytes the reader still has in front of it. Advance() does not clamp, so after an
// overrunning step the position can sit past the end; the subtraction is spelled this way
// so that case reports zero instead of wrapping around into a huge count.
size_t RemainingBytes(const Buffer::Reader& aReader) noexcept
{
    const size_t position = aReader.GetBytePosition();
    const size_t bufferSize = aReader.GetBuffer()->GetSize();
    return position < bufferSize ? bufferSize - position : 0;
}
} // namespace

uint32_t ReadFormId(Buffer::Reader& aReader, Map<uint8_t, uint32_t>& aParentToFormIdPrefix)
{
    uint32_t formId = 0;
    aReader.ReadBytes(reinterpret_cast<uint8_t*>(&formId), 4);

    uint32_t realBaseId = ESLoader::TESFile::GetFormIdPrefix(formId, aParentToFormIdPrefix);

    formId &= 0x00FFFFFF;
    formId += realBaseId;

    return formId;
}

VMAD::VMAD(Buffer::Reader& aReader, Map<uint8_t, uint32_t>& aParentToFormIdPrefix)
{
    aReader.ReadBytes(reinterpret_cast<uint8_t*>(&m_version), 2);
    aReader.ReadBytes(reinterpret_cast<uint8_t*>(&m_objectFormat), 2);
    aReader.ReadBytes(reinterpret_cast<uint8_t*>(&m_scriptCount), 2);

    // Every script costs at least its name length (2 bytes), its status (1) and its
    // property count (2) - five bytes the chunk has to contain before the loop below may
    // start a round. A count larger than that allows drives the reserve and then the loop
    // past the end of the chunk, where each round pushes an entry nothing was read into.
    if (m_scriptCount > RemainingBytes(aReader) / 5)
        return;

    m_scripts.reserve(m_scriptCount);

    for (uint16_t i = 0; i < m_scriptCount; i++)
    {
        Script script;

        script.m_name = ESLoader::ReadWString(aReader);

        // Script::m_propertyCount has no initialiser, so a failed read would leave the loop
        // bound below decided by whatever happened to be on the stack rather than by the file.
        if (!aReader.ReadBytes(&script.m_status, 1) ||
            !aReader.ReadBytes(reinterpret_cast<uint8_t*>(&script.m_propertyCount), 2))
            return;

        // The same shape one level down: a property costs at least a name length (2 bytes)
        // plus its type and status bytes.
        if (script.m_propertyCount > RemainingBytes(aReader) / 4)
            return;

        for (uint16_t j = 0; j < script.m_propertyCount; j++)
        {
            ScriptProperty scriptProperty;

            scriptProperty.m_name = ESLoader::ReadWString(aReader);

            aReader.ReadBytes(reinterpret_cast<uint8_t*>(&scriptProperty.m_type), 1);
            aReader.ReadBytes(reinterpret_cast<uint8_t*>(&scriptProperty.m_status), 1);

            scriptProperty.ParseValue(aReader, m_objectFormat, aParentToFormIdPrefix);

            script.m_properties.push_back(scriptProperty);
        }

        m_scripts.push_back(script);
    }
}

void ScriptProperty::ParseValue(Buffer::Reader& aReader, int16_t aObjectFormat, Map<uint8_t, uint32_t>& aParentToFormIdPrefix) noexcept
{
    switch (m_type)
    {
    case Type::OBJECT:
        if (aObjectFormat == 1)
        {
            m_dataSingleValue.m_formId = ReadFormId(aReader, aParentToFormIdPrefix);
        }
        else if (aObjectFormat == 2)
        {
            aReader.Advance(4);
            m_dataSingleValue.m_formId = ReadFormId(aReader, aParentToFormIdPrefix);
        }
        break;

    case Type::INT: aReader.ReadBytes(reinterpret_cast<uint8_t*>(&m_dataSingleValue.m_integer), 4); break;
    case Type::FLOAT: aReader.ReadBytes(reinterpret_cast<uint8_t*>(&m_dataSingleValue.m_float), 4); break;
    case Type::BOOL: aReader.ReadBytes(reinterpret_cast<uint8_t*>(&m_dataSingleValue.m_boolean), 1); break;

    case Type::WSTRING:
    {
        uint32_t stringLength = 0;
        if (!aReader.ReadBytes(reinterpret_cast<uint8_t*>(&stringLength), 2))
            break;

        // The value is stored as a pointer into the reader's buffer plus a length, so a
        // length running past the end is a view of memory this parse never owned - and it
        // outlives the call that produced it.
        if (stringLength > RemainingBytes(aReader))
            break;

        m_dataSingleValue.m_string = {reinterpret_cast<const char*>(aReader.GetDataAtPosition()), stringLength};
        aReader.Advance(stringLength);
        break;
    }

    case Type::OBJECT_ARRAY:
    case Type::INT_ARRAY:
    case Type::FLOAT_ARRAY:
    case Type::BOOL_ARRAY:
    case Type::STRING_ARRAY:
    {
        uint32_t sizeOfArray = 0;
        if (!aReader.ReadBytes(reinterpret_cast<uint8_t*>(&sizeOfArray), 4))
            break;

        // An element costs at least one byte on the wire, so a count larger than the bytes
        // left cannot be honoured. Unguarded it drove the loop past the end, where every
        // round pushed a copy of a value nothing had read.
        if (sizeOfArray > RemainingBytes(aReader))
            break;

        for (uint32_t i = 0; i < sizeOfArray; i++)
        {
            const size_t elementStart = aReader.GetBytePosition();

            ScriptProperty scriptProperty;
            scriptProperty.m_type = GetPropertyType(m_type);
            ParseValue(aReader, aObjectFormat, aParentToFormIdPrefix);
            m_dataArray.push_back(scriptProperty.m_dataSingleValue);

            // An element type the switch in ParseValue has no case for consumes no bytes at
            // all, so an iteration that leaves the cursor where it was means the rest of the
            // array is not on the wire - and the count above is not to be trusted either.
            if (aReader.GetBytePosition() == elementStart)
                break;
        }

        break;
    }
    }
}

ScriptProperty::Type ScriptProperty::GetPropertyType(Type aArrayType) noexcept
{
    return static_cast<Type>(static_cast<int>(aArrayType) - 10);
}

CNTO::CNTO(Buffer::Reader& aReader)
{
    aReader.ReadBytes(reinterpret_cast<uint8_t*>(&m_formId), 4);
    aReader.ReadBytes(reinterpret_cast<uint8_t*>(&m_count), 4);
}

WLST::WLST(Buffer::Reader& aReader, Map<uint8_t, uint32_t>& aParentToFormIdPrefix)
{
    m_weatherId = ReadFormId(aReader, aParentToFormIdPrefix);
    aReader.ReadBytes(reinterpret_cast<uint8_t*>(&m_chance), 4);
    m_globalId = ReadFormId(aReader, aParentToFormIdPrefix);
}

TNAM::TNAM(Buffer::Reader& aReader)
{
    aReader.ReadBytes(reinterpret_cast<uint8_t*>(&m_sunriseBegin), 1);
    aReader.ReadBytes(reinterpret_cast<uint8_t*>(&m_sunriseEnd), 1);
    aReader.ReadBytes(reinterpret_cast<uint8_t*>(&m_sunsetBegin), 1);
    aReader.ReadBytes(reinterpret_cast<uint8_t*>(&m_sunsetEnd), 1);
    aReader.ReadBytes(reinterpret_cast<uint8_t*>(&m_volatility), 1);
    aReader.ReadBytes(reinterpret_cast<uint8_t*>(&m_moons), 1);
}

NAME::NAME(Buffer::Reader& aReader, Map<uint8_t, uint32_t>& aParentToFormIdPrefix)
{
    m_baseId = ReadFormId(aReader, aParentToFormIdPrefix);
}

DOFT::DOFT(Buffer::Reader& aReader, Map<uint8_t, uint32_t>& aParentToFormIdPrefix)
{
    m_formId = ReadFormId(aReader, aParentToFormIdPrefix);
}

ACBS::ACBS(Buffer::Reader& aReader)
{
    aReader.ReadBytes(reinterpret_cast<uint8_t*>(&m_flags), 4);
    aReader.ReadBytes(reinterpret_cast<uint8_t*>(&m_magickaOffset), 2);
    aReader.ReadBytes(reinterpret_cast<uint8_t*>(&m_staminaOffset), 2);
    aReader.ReadBytes(reinterpret_cast<uint8_t*>(&m_level), 2);
    aReader.ReadBytes(reinterpret_cast<uint8_t*>(&m_calcMinLevel), 2);
    aReader.ReadBytes(reinterpret_cast<uint8_t*>(&m_calcMaxLevel), 2);
    aReader.ReadBytes(reinterpret_cast<uint8_t*>(&m_speedMultiplier), 2);
    aReader.ReadBytes(reinterpret_cast<uint8_t*>(&m_dispositionBase), 2);
    aReader.ReadBytes(reinterpret_cast<uint8_t*>(&m_templateDataFlags), 2);
    aReader.ReadBytes(reinterpret_cast<uint8_t*>(&m_healthOffset), 2);
    aReader.ReadBytes(reinterpret_cast<uint8_t*>(&m_bleedoutOverride), 2);
}

MAST::MAST(Buffer::Reader& aReader)
{
    m_masterName = ESLoader::ReadZString(aReader);
}

WCTR::WCTR(Buffer::Reader& aReader)
{
    aReader.ReadBytes(reinterpret_cast<uint8_t*>(&m_x), 2);
    aReader.ReadBytes(reinterpret_cast<uint8_t*>(&m_y), 2);
}

DNAM::DNAM(Buffer::Reader& aReader)
{
    aReader.ReadBytes(reinterpret_cast<uint8_t*>(&m_landLevel), 4);
    aReader.ReadBytes(reinterpret_cast<uint8_t*>(&m_waterLevel), 4);
}

NVNM::NVNM(Buffer::Reader& aReader)
{
    aReader.ReadBytes(reinterpret_cast<uint8_t*>(&m_unknown), 4);
    aReader.ReadBytes(reinterpret_cast<uint8_t*>(&m_locactionMarker), 4);
    aReader.ReadBytes(reinterpret_cast<uint8_t*>(&m_worldSpaceId), 4);
    if (m_worldSpaceId == 0)
    {
        uint32_t id = 0;
        aReader.ReadBytes(reinterpret_cast<uint8_t*>(&id), 4);
        m_cellId = id;
    }
    else
    {
        int16_t tmp = 0;
        aReader.ReadBytes(reinterpret_cast<uint8_t*>(&tmp), 2);
        m_gridY = tmp;

        aReader.ReadBytes(reinterpret_cast<uint8_t*>(&tmp), 2);
        m_gridX = tmp;
    }
    int32_t vertexCount = 0;
    aReader.ReadBytes(reinterpret_cast<uint8_t*>(&vertexCount), 4);
    // The count is signed file data handed straight to resize(). A negative value converts
    // to a huge size_t, so the vector is asked for more memory than the address space has
    // and the throw that follows escapes a noexcept caller; a count that simply overstates
    // the payload sizes the vector from bytes that are not there. Both are refused rather
    // than clamped: the element data below is read as count * sizeof(element) bytes, and a
    // count that disagrees with the payload cannot be repaired by picking a smaller one.
    if (vertexCount < 0 || static_cast<size_t>(vertexCount) > RemainingBytes(aReader) / sizeof(glm::vec3))
        return;
    m_vertices.resize(vertexCount);
    for (auto& vertex : m_vertices)
    {
        aReader.ReadBytes(reinterpret_cast<uint8_t*>(&vertex), sizeof(vertex));
    }

    int32_t triangleCount = 0;
    aReader.ReadBytes(reinterpret_cast<uint8_t*>(&triangleCount), 4);
    // Same shape as the vertex count above: negative converts to a huge size_t, oversized
    // sizes the vector from bytes that are not in the chunk.
    if (triangleCount < 0 || static_cast<size_t>(triangleCount) > RemainingBytes(aReader) / sizeof(Tri))
        return;
    m_triangles.resize(triangleCount);
    for (auto& tri : m_triangles)
    {
        aReader.ReadBytes(reinterpret_cast<uint8_t*>(&tri), sizeof(tri));
    }

    int32_t connectionCount = 0;
    aReader.ReadBytes(reinterpret_cast<uint8_t*>(&connectionCount), 4);
    // Same shape again. The divisor is the number of bytes the loop below actually reads
    // per entry, not sizeof(Connection): the struct is padded, and comparing against the
    // padded size would refuse entries that do fit.
    const size_t connectionSize = sizeof(NVNM::Connection::m_unk) + sizeof(NVNM::Connection::m_navMeshId) + sizeof(NVNM::Connection::tri);
    if (connectionCount < 0 || static_cast<size_t>(connectionCount) > RemainingBytes(aReader) / connectionSize)
        return;
    m_connections.resize(connectionCount);
    for (auto& connection : m_connections)
    {
        aReader.ReadBytes(reinterpret_cast<uint8_t*>(&connection.m_unk), sizeof(connection.m_unk));
        aReader.ReadBytes(reinterpret_cast<uint8_t*>(&connection.m_navMeshId), sizeof(connection.m_navMeshId));
        aReader.ReadBytes(reinterpret_cast<uint8_t*>(&connection.tri), sizeof(connection.tri));
    }

    int32_t doorCount = 0;
    aReader.ReadBytes(reinterpret_cast<uint8_t*>(&doorCount), 4);
    // Same shape again, and for the same reason the divisor is the bytes the loop reads
    // per entry rather than the padded sizeof(Door).
    const size_t doorSize = sizeof(NVNM::Door::tri) + sizeof(NVNM::Door::m_unk) + sizeof(NVNM::Door::m_doorId);
    if (doorCount < 0 || static_cast<size_t>(doorCount) > RemainingBytes(aReader) / doorSize)
        return;
    m_doorTris.resize(doorCount);
    for (auto& doorTri : m_doorTris)
    {
        aReader.ReadBytes(reinterpret_cast<uint8_t*>(&doorTri.tri), sizeof(doorTri.tri));
        aReader.ReadBytes(reinterpret_cast<uint8_t*>(&doorTri.m_unk), sizeof(doorTri.m_unk));
        aReader.ReadBytes(reinterpret_cast<uint8_t*>(&doorTri.m_doorId), sizeof(doorTri.m_doorId));
    }

    int32_t coverTriangleCount = 0;
    aReader.ReadBytes(reinterpret_cast<uint8_t*>(&coverTriangleCount), 4);
    // Same shape again, except that the whole element block is read in one call below.
    if (coverTriangleCount < 0 || static_cast<size_t>(coverTriangleCount) > RemainingBytes(aReader) / sizeof(int16_t))
        return;
    m_coverTris.resize(coverTriangleCount);
    aReader.ReadBytes(reinterpret_cast<uint8_t*>(m_coverTris.data()), sizeof(int16_t) * m_coverTris.size());

    aReader.ReadBytes(reinterpret_cast<uint8_t*>(&m_divisor), 4);
    aReader.ReadBytes(reinterpret_cast<uint8_t*>(&m_maxDistance), sizeof(m_maxDistance));
    aReader.ReadBytes(reinterpret_cast<uint8_t*>(&m_min), sizeof(m_min));
    aReader.ReadBytes(reinterpret_cast<uint8_t*>(&m_max), sizeof(m_max));

    // Missing triangle divisor but we don't care about it
}

} // namespace Chunks
