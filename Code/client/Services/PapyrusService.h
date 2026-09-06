#pragma once

#include <Misc/GameVM.h>

#include <mutex>
#include <type_traits>

struct TESForm;
struct TESObjectREFR;

/**
 * @brief Handles registering and executing Papyrus functions.
 */
struct PapyrusService
{
    PapyrusService() noexcept = default;
    ~PapyrusService() noexcept = default;

    TP_NOCOPYMOVE(PapyrusService);

    const void* Get(const String& acNamespace, const String& acFunction) const noexcept;

    // Remembers a native the game just registered. Called straight from the
    // registration hook, which runs on the script extender's papyrus thread
    // while the game keeps rendering, so the store is locked.
    void Capture(const char* acpNamespace, const char* acpName, void* apFunction) noexcept;

    // How many natives the registration hook has captured; zero means no
    // papyrus call this client makes can work.
    size_t GetCapturedCount() const noexcept;

private:
    mutable std::mutex m_lock;
    Map<String, void*> m_functions;
};

namespace PapyrusDetail
{
// A native the game never registered, or a call made before the virtual machine
// exists, leaves the wrapper below with nothing to call. Doing it anyway is an
// instant crash with nothing in the log to name the function, so say which one
// it was and why, once, and let the call degrade instead.
void ReportUnavailable(const char* acpName, const char* acpReason, bool& aReported) noexcept;

template <class Return> Return Unavailable(const char* acpName, const char* acpReason, bool& aReported) noexcept
{
    ReportUnavailable(acpName, acpReason, aReported);

    if constexpr (!std::is_void_v<Return>)
        return Return{};
}

// Where the registration hook sits, checked against the virtual machine that
// is supposed to call it: the game reaches RegisterFunction through the VM's
// vtable, so if our target is not one of its entries then it is the wrong
// function and no amount of waiting will make the hook fire.
void ReportRegistrationTarget() noexcept;
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
            return PapyrusDetail::Unavailable<Return>(m_pName, "the game never registered it", m_reported);

        auto* pVirtualMachine = GameVM::GetVirtualMachine();
        if (!pVirtualMachine)
            return PapyrusDetail::Unavailable<Return>(m_pName, "the papyrus vm does not exist yet", m_reported);

        return m_pFunction(pVirtualMachine, 0, apThis, std::forward<Args>(args)...);
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
            return PapyrusDetail::Unavailable<Return>(m_pName, "the game never registered it", m_reported);

        auto* pVirtualMachine = GameVM::GetVirtualMachine();
        if (!pVirtualMachine)
            return PapyrusDetail::Unavailable<Return>(m_pName, "the papyrus vm does not exist yet", m_reported);

        return m_pFunction(pVirtualMachine, std::forward<Args>(args)...);
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
            return PapyrusDetail::Unavailable<Return>(m_pName, "the game never registered it", m_reported);

        auto* pVirtualMachine = GameVM::GetVirtualMachine();
        if (!pVirtualMachine)
            return PapyrusDetail::Unavailable<Return>(m_pName, "the papyrus vm does not exist yet", m_reported);

        RefrOrInventoryObj self{apThis, nullptr, 0};

        return m_pFunction(pVirtualMachine, 0, self, std::forward<Args>(args)...);
    }

private:
    const char* m_pName;
    TFunction m_pFunction;
    mutable bool m_reported{false};
};

#define PAPYRUS_FUNCTION(returnType, scope, name, ...) static PapyrusFunction<returnType, scope, __VA_ARGS__> s_p##name(#scope "::" #name, World::Get().ctx().at<PapyrusService>().Get(#scope, #name));
#define GLOBAL_PAPYRUS_FUNCTION(returnType, scope, name, ...) static GlobalPapyrusFunction<returnType, __VA_ARGS__> s_p##name(#scope "::" #name, World::Get().ctx().at<PapyrusService>().Get(#scope, #name));
#define LATENT_PAPYRUS_FUNCTION(returnType, scope, name, ...) static LatentPapyrusFunction<returnType, scope, __VA_ARGS__> s_p##name(#scope "::" #name, World::Get().ctx().at<PapyrusService>().Get(#scope, #name));
