#pragma once

#include <Misc/GameVM.h>

#include <type_traits>

struct TESForm;
struct TESObjectREFR;
struct PapyrusFunctionRegisterEvent;

/**
 * @brief Handles registering and executing Papyrus functions.
 */
struct PapyrusService
{
    PapyrusService(entt::dispatcher& aDispatcher) noexcept;
    ~PapyrusService() noexcept = default;

    TP_NOCOPYMOVE(PapyrusService);

    const void* Get(const String& acNamespace, const String& acFunction) const noexcept;

    // How many natives the registration hook has captured; zero means no
    // papyrus call this client makes can work.
    size_t GetCapturedCount() const noexcept { return m_functions.size(); }

    void HandlePapyrusFunctionEvent(const PapyrusFunctionRegisterEvent&) noexcept;

private:
    Map<String, void*> m_functions;

    entt::scoped_connection m_papyrusFunctionRegisterConnection;
};

namespace PapyrusDetail
{
// A native the game never registered leaves the wrapper below holding a null
// pointer, and calling that is an instant crash with nothing in the log to name
// the function. Say which one it was, once, and let the call degrade instead.
void ReportMissing(const char* acpName, bool& aReported) noexcept;

// Where the registration hook sits, checked against the virtual machine that
// is supposed to call it: the game reaches RegisterFunction through the VM's
// vtable, so if our target is not one of its entries then it is the wrong
// function and no amount of waiting will make the hook fire.
void ReportRegistrationTarget() noexcept;

template <class Return> Return Missing(const char* acpName, bool& aReported) noexcept
{
    ReportMissing(acpName, aReported);

    if constexpr (!std::is_void_v<Return>)
        return Return{};
}
} // namespace PapyrusDetail

template <class Return, class Type, class... Args> struct PapyrusFunction
{
    using TFunction = Return(__fastcall*)(BSScript::IVirtualMachine*, uint32_t, const Type*, Args...);

    PapyrusFunction(const char* acpName, const void* apAddress)
        : m_pName(acpName)
        , m_pFunction(reinterpret_cast<TFunction>(apAddress))
    {
    }

    Return operator()(const Type* apThis, Args... args) const noexcept
    {
        if (!m_pFunction)
            return PapyrusDetail::Missing<Return>(m_pName, m_reported);

        return m_pFunction(GameVM::Get()->virtualMachine, 0, apThis, std::forward<Args>(args)...);
    }

private:
    const char* m_pName;
    TFunction m_pFunction;
    mutable bool m_reported{false};
};

template <class Return, class... Args> struct GlobalPapyrusFunction
{
    using TFunction = Return(__fastcall*)(BSScript::IVirtualMachine*, Args...);

    GlobalPapyrusFunction(const char* acpName, const void* apAddress)
        : m_pName(acpName)
        , m_pFunction(reinterpret_cast<TFunction>(apAddress))
    {
    }

    Return operator()(Args... args) const noexcept
    {
        if (!m_pFunction)
            return PapyrusDetail::Missing<Return>(m_pName, m_reported);

        return m_pFunction(GameVM::Get()->virtualMachine, std::forward<Args>(args)...);
    }

private:
    const char* m_pName;
    TFunction m_pFunction;
    mutable bool m_reported{false};
};

struct RefrOrInventoryObj
{
    const TESObjectREFR* pRefr;
    TESForm* pInventoryForm;
    uint16_t itemCount;
};

template <class Return, class Type, class... Args> struct LatentPapyrusFunction
{
    using TFunction = Return(__fastcall*)(BSScript::IVirtualMachine*, uint32_t, const RefrOrInventoryObj&, Args...);

    LatentPapyrusFunction(const char* acpName, const void* apAddress)
        : m_pName(acpName)
        , m_pFunction(reinterpret_cast<TFunction>(apAddress))
    {
    }

    Return operator()(const Type* apThis, Args... args) const noexcept
    {
        if (!m_pFunction)
            return PapyrusDetail::Missing<Return>(m_pName, m_reported);

        RefrOrInventoryObj self{apThis, nullptr, 0};

        return m_pFunction(GameVM::Get()->virtualMachine, 0, self, std::forward<Args>(args)...);
    }

private:
    const char* m_pName;
    TFunction m_pFunction;
    mutable bool m_reported{false};
};

#define PAPYRUS_FUNCTION(returnType, scope, name, ...) static PapyrusFunction<returnType, scope, __VA_ARGS__> s_p##name(#scope "::" #name, World::Get().ctx().at<PapyrusService>().Get(#scope, #name));
#define GLOBAL_PAPYRUS_FUNCTION(returnType, scope, name, ...) static GlobalPapyrusFunction<returnType, __VA_ARGS__> s_p##name(#scope "::" #name, World::Get().ctx().at<PapyrusService>().Get(#scope, #name));
#define LATENT_PAPYRUS_FUNCTION(returnType, scope, name, ...) static LatentPapyrusFunction<returnType, scope, __VA_ARGS__> s_p##name(#scope "::" #name, World::Get().ctx().at<PapyrusService>().Get(#scope, #name));
