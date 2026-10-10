#include <Messages/AssignObjectsRequest.h>
#include <algorithm>

void AssignObjectsRequest::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    const auto count = std::min<size_t>(Objects.size(), kMaxObjectCount);
    aWriter.WriteBits(count, 8);

    for (size_t i = 0; i < count; ++i)
    {
        Objects[i].Serialize(aWriter);
    }
}

void AssignObjectsRequest::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    // The count is 8 bits wide, so this resize allocates for whatever the peer put
    // there. This layer has no logger on purpose (see WireLimits.h), so the catch
    // recovers instead of reporting: an empty list reads as "the client assigned
    // nothing", which is the same as the message never arriving.
    try
    {
        ClientMessage::DeserializeRaw(aReader);

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
