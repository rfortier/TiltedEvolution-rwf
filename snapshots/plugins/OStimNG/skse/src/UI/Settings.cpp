#include "UI/Settings.h"
#include <Util.h>

namespace UI {
	void LoadPosition(json& json, Position& setting) {
		if (json.contains("x"))
			setting.xPos = json["x"];
		if (json.contains("y"))
			setting.yPos = json["y"];
		if (json.contains("xScale"))
			setting.xScale = json["xScale"];
		if (json.contains("yScale"))
			setting.yScale = json["yScale"];
	}
	void CreateDefaultLocationSettings(json& json) {
		json["x"] = 0;
		json["y"] = 0;
		json["xScale"] = 100;
		json["yScale"] = 100;
	}
	void CreateDefaultSettingsFile(json& json) {

		json["scene"] = json::object();		
		json["scene"]["control"] = json::object();
		CreateDefaultLocationSettings(json["scene"]["control"]);
		json["scene"]["bars"] = json::object();
		CreateDefaultLocationSettings(json["scene"]["bars"]);

		json["align"] = json::object();
		json["align"]["control"] = json::object();
		CreateDefaultLocationSettings(json["align"]["control"]);
		json["align"]["info"] = json::object();
		CreateDefaultLocationSettings(json["align"]["info"]);

		json["search"] = json::object();
		json["search"]["menu"] = json::object();
		CreateDefaultLocationSettings(json["search"]["menu"]);

		json["fadeTime"] = 10.0;
	}
	void Settings::LoadSettings() {
		logger::info("loading ui settings");
		auto settingsPath = util::ui_settings_path();
		if (!fs::exists(*settingsPath)) {
			logger::info("ui settings file does not exist, creating");
			json json = json::object();
			CreateDefaultSettingsFile(json);
			auto ostimPath = util::ostim_path();
			std::filesystem::create_directory(*ostimPath);
			std::ofstream settingsFile(*settingsPath);
			settingsFile << std::setw(2) << json << std::endl;
		}
			
		std::ifstream ifs(*settingsPath);
		json json = json::parse(ifs, nullptr, false);

		if (json.is_discarded()) {
			logger::warn("ui settings file is malformed");
			return;
		}
		if (!json.contains("align")) {
			logger::warn("settings file malformed - missing align");
		} else {
			auto& align = json["align"];
			if (!align.contains("control")) {
				logger::warn("settings file malformed - missing align.control");
			} else {
				auto& control = align["control"];
				LoadPosition(control, positionSettings.AlignMenuPosition.ControlPosition);
			}
			if (!align.contains("info")) {
				logger::warn("settings file malformed - missing align.info");
			}
			else {
				auto& info = align["info"];
				LoadPosition(info, positionSettings.AlignMenuPosition.InfoPosition);
			}
		}

		if (!json.contains("scene")) {
			logger::warn("settings file maformed - missing scene");
		}
		else {
			auto& scene = json["scene"];
			if (!scene.contains("control")) {
				logger::warn("settings file malformed - missing scene.control");
			}
			else {
				auto& control = scene["control"];
				LoadPosition(control, positionSettings.ScenePositions.ControlPosition);
			}
			if (!scene.contains("bars")) {
				logger::warn("settings file malformed - missing scene.bars");
			}
			else {
				auto& info = scene["bars"];
				LoadPosition(info, positionSettings.ScenePositions.BarsPosition);
			}
		}

		if (!json.contains("search")) {
			logger::warn("settings file malformed - missing search");
		}
		else {
			auto& scene = json["search"];
			if (!scene.contains("menu")) {
				logger::warn("settings file malformed - missing search.menu");
			}
			else {
				auto& menu = scene["menu"];
				LoadPosition(menu, positionSettings.SearchPositions.MenuPosition);
			}
		}

		if (!json.contains("fadeTime")) {
			logger::info("settings file outdated - adding fadeTime");
			json["fadeTime"] = 10.0;
		}
		fadeTime = json["fadeTime"];
		
		ifs.close();
		//Write back any defaulted settings
		std::ofstream settingsFile(*settingsPath);
		settingsFile << std::setw(2) << json << std::endl;
	}
}