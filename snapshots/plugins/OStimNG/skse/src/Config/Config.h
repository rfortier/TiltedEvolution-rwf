#pragma once

namespace Config {
	class Config {
    public:
        inline static bool verifyTranslationIntegrity = true;

		static void loadConfig();
	};
}