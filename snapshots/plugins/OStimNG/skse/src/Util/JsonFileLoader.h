#pragma once

namespace Util {

	class JsonFileLoader {
	public:
		inline static void LoadFilesInFolder(std::string path, std::function<void(std::string, std::string, json)> callback) {
			fs::path rootPath{ path };
			if (!fs::exists(rootPath)) {
				logger::warn("path ({}) does not exist", path);
				return;
			}
			for (auto& file : fs::directory_iterator{ rootPath }) {
				auto& path = file.path();
				auto pathStr = path.string();
				if (pathStr.ends_with(".json")) {
					std::ifstream ifs(pathStr);
					json json = json::parse(ifs, nullptr, false);

					if (json.is_discarded()) {
						logger::warn("file {} is malformed", pathStr);
						continue;
					}
					callback(pathStr, path.filename().replace_extension("").string(), json);
				}
			}
		}

		inline static void LoadFilesInSubfolders(std::string path, std::function<void(std::string, std::string, json)> callback) {
			fs::path rootPath{ path };
			if (!fs::exists(rootPath)) {
				logger::warn("path ({}) does not exist", path);
				return;
			}
			for (auto& file : fs::recursive_directory_iterator{ rootPath }) {
				auto& path = file.path();
				auto pathStr = path.string();
				if (pathStr.ends_with(".json")) {
					std::ifstream ifs(pathStr);
					json json = json::parse(ifs, nullptr, false);

					if (json.is_discarded()) {
						logger::warn("file {} is malformed", pathStr);
						continue;
					}
					callback(pathStr, path.filename().replace_extension("").string(), json);
				}
			}
		}
	};
}