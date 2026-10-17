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

    // Same lookup as Get() without the report. A wrapper that was built before
    // the game registered its native retries the table on its own schedule, and
    // the first miss was already reported there, so repeating it would only bury
    // that line.
    const void* Find(const String& acNamespace, const String& acFunction) const noexcept;

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

// Looks a native up again for a wrapper that was built before the game had
// registered it. Declared here and defined next to the table in the .cpp rather
// than inline: reaching the service means going through the world's context, and
// this header is instantiated from game translation units that do not include
// World.h.
const void* Lookup(const char* acpNamespace, const char* acpFunction) noexcept;
} // namespace PapyrusDetail

template <class Return, class Type, class... Args> struct PapyrusFunction
{
    using TFunction = Return(__fastcall*)(BSScript::IVirtualMachine*, uint32_t, const Type*, Args...);

    PapyrusFunction(const char* acpName, const char* acpNamespace, const char* acpFunction, const void* apAddress)
        : m_pName(acpName)
        , m_pNamespace(acpNamespace)
        , m_pFunctionName(acpFunction)
        , m_pFunction(reinterpret_cast<TFunction>(apAddress))
    {
    }

    Return operator()(const Type* apThis, Args... args) const noexcept
    {
        if (const TFunction pFunction = Resolve())
        {
            auto* pVirtualMachine = GameVM::GetVirtualMachine();
            if (!pVirtualMachine)
                return PapyrusDetail::Unavailable<Return>(m_pName, "the papyrus vm does not exist yet", m_reported);

            return pFunction(pVirtualMachine, 0, apThis, std::forward<Args>(args)...);
        }

        return PapyrusDetail::Unavailable<Return>(m_pName, "the game has not registered it", m_reported);
    }

private:
    // The address used to be latched at construction, so a native that the game
    // had not registered yet at that moment stayed unusable for the rest of the
    // process: every later call quietly returned a default value, and the
    // registration hook kept capturing into a table nobody asked again. A miss is
    // now looked up a second time, and a hit is cached, so a native that arrives
    // late starts working from the next call on.
    //
    // The retry is counted in calls rather than in time, so the lookup costs
    // nothing until it is due. cRetryInterval calls per retry keeps a native the
    // game never registers from turning every single call into a table lookup:
    // a wrapper called once per frame asks about once a second at 60 fps, and one
    // called once per second asks about once a minute.
    static constexpr uint32_t cRetryInterval = 64;

    TFunction Resolve() const noexcept
    {
        if (m_pFunction)
            return m_pFunction;

        if (m_retriesLeft > 0)
        {
            --m_retriesLeft;
            return nullptr;
        }

        m_pFunction = reinterpret_cast<TFunction>(PapyrusDetail::Lookup(m_pNamespace, m_pFunctionName));

        if (!m_pFunction)
            m_retriesLeft = cRetryInterval;

        return m_pFunction;
    }

    const char* m_pName;
    const char* m_pNamespace;
    const char* m_pFunctionName;
    mutable TFunction m_pFunction;
    mutable bool m_reported{false};
    mutable uint32_t m_retriesLeft{0};
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

#define PAPYRUS_FUNCTION(returnType, scope, name, ...) static PapyrusFunction<returnType, scope, __VA_ARGS__> s_p##name(#scope "::" #name, #scope, #name, World::Get().ctx().at<PapyrusService>().Get(#scope, #name));
#define GLOBAL_PAPYRUS_FUNCTION(returnType, scope, name, ...) static GlobalPapyrusFunction<returnType, __VA_ARGS__> s_p##name(#scope "::" #name, World::Get().ctx().at<PapyrusService>().Get(#scope, #name));
#define LATENT_PAPYRUS_FUNCTION(returnType, scope, name, ...) static LatentPapyrusFunction<returnType, scope, __VA_ARGS__> s_p##name(#scope "::" #name, World::Get().ctx().at<PapyrusService>().Get(#scope, #name));
