#include "Integrity.h"

#include "CheckSum.h"

#include <openssl/md5.h>

#include "Config/Config.h"
#include "Util/StringUtil.h"

namespace Integrity {
    bool verifySceneIntegrity() {
        std::vector<std::string> files;
        for (auto& file : std::filesystem::recursive_directory_iterator{"Data\\SKSE\\Plugins\\OStim\\scenes"}) {
            if (file.is_directory()) {
                continue;
            }

            std::string pathStr = file.path().string();
            StringUtil::toLower(pathStr);
            if (pathStr.starts_with("data\\skse\\plugins\\ostim\\scenes\\ostim") && pathStr.ends_with(".json")) {
                files.push_back(pathStr);
            }
        }
        std::sort(files.begin(), files.end());

        std::ifstream integrity;
        integrity.open("Data\\OStim\\integrity\\scenes", std::ios::binary | std::ios::in);

        integrity.seekg(0, std::ios::end);
        long length = integrity.tellg();
        integrity.seekg(0, std::ios::beg);

        if (length / MD5_DIGEST_LENGTH != files.size()) {
            logger::warn("scene count mismatch: expected {}, encountered {}", length / MD5_DIGEST_LENGTH, files.size());
            return false;
        }

        char fileSum[MD5_DIGEST_LENGTH];
        std::string fileSumStr;

        for (std::string file : files) {
            integrity.read(fileSum, MD5_DIGEST_LENGTH);
            fileSumStr = std::string(fileSum, MD5_DIGEST_LENGTH);
            std::string checkSum = CheckSum::createCheckSum(file);

            if (fileSumStr != checkSum) {
                logger::warn("checksum mismatch for {}: expected {}, encountered {}", file, fileSumStr, checkSum);
                return false;
            }
        }

        return true;
    }

    bool verifyTranslationIntegrity() {
        if (!Config::Config::verifyTranslationIntegrity) {
            logger::info("skipping translation integrity check");
            return true;
        }

        std::vector<std::string> files{"Data\\Interface\\translations\\ONav_ENGLISH.txt", "Data\\Interface\\translations\\OScenes_ENGLISH.txt"};

        std::ifstream integrity;
        integrity.open("Data\\OStim\\integrity\\translations", std::ios::binary | std::ios::in);

        integrity.seekg(0, std::ios::end);
        long length = integrity.tellg();
        integrity.seekg(0, std::ios::beg);

        if (length / MD5_DIGEST_LENGTH != files.size()) {
            logger::warn("translation count mismatch: expected {}, encountered {}", length / MD5_DIGEST_LENGTH, 2);
            return false;
        }

        char fileSum[MD5_DIGEST_LENGTH];
        std::string fileSumStr;

        for (std::string file : files) {
            integrity.read(fileSum, MD5_DIGEST_LENGTH);
            fileSumStr = std::string(fileSum, MD5_DIGEST_LENGTH);
            std::string checkSum = CheckSum::createCheckSum(file);

            if (fileSumStr != checkSum) {
                logger::warn("checksum mismatch for {}: expected {}, encountered {}", file, fileSumStr, checkSum);
                return false;
            }
        }

        return true;
    }
}