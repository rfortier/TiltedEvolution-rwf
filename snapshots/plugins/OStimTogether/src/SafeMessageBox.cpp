#include "PCH.h"
#include "SafeMessageBox.h"

#include "OCumStateSync.h"

#include <RE/B/BSTCreateFactoryManager.h>
#include <RE/M/MessageBoxData.h>

namespace OStimTogether
{
    namespace
    {
        class Callback final : public RE::IMessageBoxCallback
        {
        public:
            explicit Callback(std::function<void(unsigned int)> callback) :
                _callback(std::move(callback))
            {}

            void Run(RE::IMessageBoxCallback::Message message) override
            {
                const auto choice = static_cast<unsigned int>(message);

                // Consent Accept is the ideal synchronization barrier for
                // appearance state: emit the real local player's current OCum
                // state before INVITE_RESPONSE is sent. STRPM reliable+ordered
                // delivery guarantees the owner sees ADDON packets first.
                if (choice == 0) {
                    OCumStateSync::GetSingleton().SendLocalSnapshot(
                        "consent-accept");
                }

                if (_callback) {
                    _callback(choice);
                }
            }

        private:
            std::function<void(unsigned int)> _callback;
        };
    }

    void ShowSafeMessageBox(
        std::string body,
        std::string primary,
        std::string secondary,
        std::function<void(unsigned int)> callback)
    {
        auto* manager = RE::MessageDataFactoryManager::GetSingleton();
        if (!manager) {
            SKSE::log::error(
                "OSTNET SAFE MESSAGEBOX MessageDataFactoryManager unavailable");
            if (callback) {
                callback(1);
            }
            return;
        }

        auto* creator = manager->GetCreator<RE::MessageBoxData>(
            RE::BSFixedString("MessageBoxData"));
        if (!creator) {
            SKSE::log::error(
                "OSTNET SAFE MESSAGEBOX MessageBoxData creator unavailable");
            if (callback) {
                callback(1);
            }
            return;
        }

        auto* data = creator->Create();
        if (!data) {
            SKSE::log::error(
                "OSTNET SAFE MESSAGEBOX allocation failed");
            if (callback) {
                callback(1);
            }
            return;
        }

        data->bodyText = body.c_str();
        data->buttonText.clear();
        data->buttonText.emplace_back(primary.c_str());
        data->buttonText.emplace_back(secondary.c_str());

        // Same fix validated in Trade Together: keep every factory-provided
        // MessageBoxData internal field untouched so Skyrim Souls / Unpaused
        // Menus can apply its normal MessageBoxMenu creator/flags.
        data->callback = RE::make_smart<Callback>(std::move(callback));

        SKSE::log::info(
            "OSTNET SAFE MESSAGEBOX queued buttons=2 nativeDefaults=1");
        data->QueueMessage();
    }
}
