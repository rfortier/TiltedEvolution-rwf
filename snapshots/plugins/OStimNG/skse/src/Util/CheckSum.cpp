#include "CheckSum.h"

#include <openssl/md5.h>

#include "Util/StringUtil.h"

namespace CheckSum {
    std::string createCheckSum(std::string path) {
        unsigned char result[MD5_DIGEST_LENGTH];

        std::string fileName = path.substr(path.find_last_of("\\") + 1);
        StringUtil::toLower(fileName);

        std::ifstream ifs;
        ifs.open(path, std::ios::binary | std::ios::in);

        ifs.seekg(0, std::ios::end);
        long length = ifs.tellg();
        ifs.seekg(0, std::ios::beg);

        char* content = new char[length + fileName.length()];
        fileName.copy(content, fileName.length());
        ifs.read(content + fileName.length(), length);

        const unsigned char* constContent = reinterpret_cast<unsigned char*>(content);

        MD5(constContent, length, result);

        return std::string(reinterpret_cast<char*>(result), MD5_DIGEST_LENGTH);
    }
}
