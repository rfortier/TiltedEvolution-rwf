#include <Messages/AssignObjectsResponse.h>
#include <algorithm>

void AssignObjectsResponse::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    const auto count = std::min<size_t>(Objects.size(), kMaxObjectCount);
    aWriter.WriteBits(count, 8);

    for (size_t i = 0; i < count; ++i)
    {
        Objects[i].Serialize(aWriter);
    }
}

void AssignObjectsResponse::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    // Same 8 bit count as the request above, and the same answer: an empty list
    // leaves the client with no object to create.
    try
    {
        ServerMessage::DeserializeRaw(aReader);

        uint64_t count = 0;
        aReader.ReadBits(count, 8);

        Objects.resize(count);

        for (auto i = 0u; i < count; ++i)
        {
            Objects[i].Deserialize(aReader);
        }
    }
    catch (...)
    {
        Objects.clear();
    }
}
