#pragma once

#include <Messages/ClientMessageFactory.h>

static std::function<UniquePtr<ClientMessage>(TiltedPhoques::Buffer::Reader& aReader)> s_clientMessageExtractor[kClientOpcodeMax];

namespace details
{
static struct ClientMessageFactoryInit
{
    ClientMessageFactoryInit()
    {
        auto extractor = [](auto& x)
        {
            using T = typename std::remove_reference_t<decltype(x)>::Type;

            s_clientMessageExtractor[T::Opcode] = [](TiltedPhoques::Buffer::Reader& aReader)
            {
                auto ptr = TiltedPhoques::MakeUnique<T>();
                ptr->DeserializeRaw(aReader);
                return TiltedPhoques::CastUnique<ClientMessage>(std::move(ptr));
            };

            return false;
        };

        ClientMessageFactory::Visit(extractor);
    }
} s_ClientMessageFactoryInit;
} // namespace details

UniquePtr<ClientMessage> ClientMessageFactory::Extract(TiltedPhoques::Buffer::Reader& aReader) const noexcept
{
    uint64_t data;
    aReader.ReadBits(data, sizeof(ClientOpcode) * 8);

    if (data >= kClientOpcodeMax) [[unlikely]]
        return {nullptr};

    const auto opcode = static_cast<ClientOpcode>(data);

    // The range check above is not a completeness check. This table is filled by
    // ClientMessageFactory::Visit, so an opcode the visitor does not name leaves an
    // EMPTY std::function in its slot, and calling through it throws
    // std::bad_function_call - inside a noexcept function, which means
    // std::terminate and the process dies on a packet that merely named a message
    // this build does not know. A gap is a build error, and
    // check_message_factories.py fails the tree for one, but the packet that
    // arrives is not the place to find out.
    if (!s_clientMessageExtractor[opcode]) [[unlikely]]
        return {nullptr};

    // See ServerMessageFactory::Extract: the message bodies allocate and the frames are
    // noexcept, so this is the last place a throw can be turned into an unparsed packet.
    // The server reaches this from a connection it has not authenticated yet.
    try
    {
        return s_clientMessageExtractor[opcode](aReader);
    }
    catch (...)
    {
        return {nullptr};
    }
}
