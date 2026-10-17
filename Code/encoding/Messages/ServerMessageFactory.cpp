#pragma once

#include <Messages/ServerMessageFactory.h>

static std::function<UniquePtr<ServerMessage>(TiltedPhoques::Buffer::Reader& aReader)> s_serverMessageExtractor[kServerOpcodeMax];

namespace details
{
static struct ServerMessageFactoryInit
{
    ServerMessageFactoryInit()
    {
        auto extractor = [](auto& x)
        {
            using T = typename std::remove_reference_t<decltype(x)>::Type;

            s_serverMessageExtractor[T::Opcode] = [](TiltedPhoques::Buffer::Reader& aReader)
            {
                auto ptr = TiltedPhoques::MakeUnique<T>();
                ptr->DeserializeRaw(aReader);
                return TiltedPhoques::CastUnique<ServerMessage>(std::move(ptr));
            };

            return false;
        };

        ServerMessageFactory::Visit(extractor);
    }
} s_ServerMessageFactoryInit;
} // namespace details

UniquePtr<ServerMessage> ServerMessageFactory::Extract(TiltedPhoques::Buffer::Reader& aReader) const noexcept
{
    uint64_t data;
    aReader.ReadBits(data, sizeof(ServerOpcode) * 8);

    if (data >= kServerOpcodeMax) [[unlikely]]
        return {nullptr};

    const auto opcode = static_cast<ServerOpcode>(data);

    // See ClientMessageFactory::Extract: the range check is not a completeness
    // check, and a slot the visitor never filled is an empty std::function whose
    // call would terminate the process from a noexcept function.
    if (!s_serverMessageExtractor[opcode]) [[unlikely]]
        return {nullptr};

    // Every DeserializeRaw below allocates - a string, a list, a resize - and most of them
    // do it without a guard of their own, because the frame is noexcept and there was
    // nowhere above it to catch a throw either. This is that place. The receive entry
    // points bound the packet size, which bounds the allocation but does not remove it,
    // and an allocation that fails should cost the packet rather than the process.
    try
    {
        return s_serverMessageExtractor[opcode](aReader);
    }
    catch (...)
    {
        return {nullptr};
    }
}
