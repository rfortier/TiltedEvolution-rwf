#include "Config.h"

#include "Util.h"

namespace Config {
    void Config::loadConfig() {
        logger::info("loading config");
        auto configPath = util::config_path();

        if (!fs::exists(*configPath)) {
            logger::info("no config file found");
            return;
        }

        std::ifstream ifs(*configPath);
        json json = json::parse(ifs, nullptr, false);

        if (json.is_discarded()) {
            logger::warn("config file is malformed");
            return;
        }

        if (json.contains("verifyTranslationIntegrity")) {
            if (json["verifyTranslationIntegrity"].is_boolean()) {
                verifyTranslationIntegrity = json["verifyTranslationIntegrity"];
            } else {
                logger::warn("config setting 'verifyTranslationIntegrity' is not a bool");
            }
        }
    }
}