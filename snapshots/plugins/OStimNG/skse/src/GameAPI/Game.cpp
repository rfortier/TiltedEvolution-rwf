#include "Game.h"

#include "GameUtil.h"

#include "Game/LocaleManager.h"

namespace GameAPI {
    void Game::setGameSpeed(float speed) {
        const auto scriptFactory = RE::IFormFactory::GetConcreteFormFactoryByType<RE::Script>();
        const auto script = scriptFactory ? scriptFactory->Create() : nullptr;
        if (script) {
            script->SetCommand("sgtm " + std::to_string(speed));
            script->CompileAndRun(RE::PlayerCharacter::GetSingleton());
            delete script;
        }
    }

    void Game::shakeController(float leftStrength, float rightStrength, float duration) {
        ShakeController(true, leftStrength, duration);
        ShakeController(false, rightStrength, duration);
    }

    GameActor Game::getCrosshairActor() {
        if (GameLogic::GameTable::getCurrentCrosshairRef()) {
            return GameLogic::GameTable::getCurrentCrosshairRef()->As<RE::Actor>();
        } else {
            return {};
        }
    }

    void Game::showMessageBox(std::string content, std::vector<std::string> options, std::function<void(unsigned int)> callback) {
        content = LocaleManager::GetSingleton()->GetLocalization(content);
        for (std::string& option : options) {
            option = LocaleManager::GetSingleton()->GetLocalization(option);
        }

        if (options.size() != 1) {
            RE::TESDataHandler* handler = RE::TESDataHandler::GetSingleton();
            if (handler->GetLoadedModIndex("UIExtensions.esp")) {
                const auto skyrimVM = RE::SkyrimVM::GetSingleton();
                auto vm = skyrimVM ? skyrimVM->GetVMRuntimeData().impl : nullptr;
                if (vm) {
                    RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> scriptCallback(new UIExtMsgBoxCallbackFunctor(callback));
                    auto args = RE::MakeFunctionArguments(std::move(content), std::move(options));
                    vm->DispatchStaticCall("OSKSE", "UIExtMessageBox", args, scriptCallback);
                }
                return;
            }
        }
        

        auto* messagebox = RE::MessageDataFactoryManager::GetSingleton()->GetCreator<RE::MessageBoxData>(RE::InterfaceStrings::GetSingleton()->messageBoxData)->Create();
        messagebox->callback = RE::make_smart<MessageBoxCallback>(callback);
        messagebox->bodyText = content;
        for (std::string& text : options) {
            messagebox->buttonText.push_back(text.c_str());
        }
        RE::MessageBoxMenu::QueueMessage(messagebox);
    }
}