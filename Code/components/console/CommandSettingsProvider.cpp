// Copyright (C) 2022 TiltedPhoques SRL.
// For licensing information see LICENSE at the root of this distribution.

#include <base/Check.h>

#include <console/CommandSettingsProvider.h>
#include <console/ConsoleUtils.h>
#include <console/Setting.h>
#include <console/ConsoleRegistry.h>

namespace Console
{

void LoadSettingsFromCommand(ConsoleRegistry& aReg, int argc, char** argv)
{
    Map<std::string, std::string> args;
    for (auto i = 1; i < argc; ++i)
    {
        std::string value = argv[i];
        // substr(npos) throws, and an argument made of nothing but dashes (or an empty one)
        // leaves find_first_not_of at npos. Nothing above this call catches, so the whole
        // argument is skipped instead of taking the server down over it.
        const auto cFirstFlagChar = value.find_first_not_of('-');
        if (cFirstFlagChar == std::string::npos)
            continue;

        value = value.substr(cFirstFlagChar);
        const auto cSplitLoc = value.find_first_of('=');
        if (cSplitLoc != std::string::npos)
        {
            std::string name = value.substr(0, cSplitLoc);
            std::string arg = value.substr(cSplitLoc + 1);
            args[name] = arg;
        }
        else
            args[value] = "true";
    }

    aReg.ForAllSettings(
        [&](SettingBase* setting)
        {
            const auto pName = setting->name;
            if (!args.contains(pName))
                return;

            switch (setting->type)
            {
            // With scalar types we don't expect the size to change...
            // However, they should all call StoreValue in the future.
            // ConvertStringValue rather than the std::sto* family: those throw on a value they
            // cannot parse, and this runs from the runner constructor where nothing catches, so a
            // typo in an argument would take the server down before it ever listens. The "set"
            // console command already writes these same fields this way.
            case SettingBase::Type::kBoolean: setting->data.as_boolean = args[pName] == "TRUE" || args[pName] == "true"; break;
            case SettingBase::Type::kInt: setting->data.as_int32 = ConvertStringValue(args[pName].c_str(), setting->data.as_int32); break;
            case SettingBase::Type::kUInt: setting->data.as_uint32 = ConvertStringValue(args[pName].c_str(), setting->data.as_uint32); break;
            case SettingBase::Type::kInt64: setting->data.as_int64 = ConvertStringValue(args[pName].c_str(), setting->data.as_int64); break;
            case SettingBase::Type::kUInt64: setting->data.as_uint64 = ConvertStringValue(args[pName].c_str(), setting->data.as_uint64); break;
            case SettingBase::Type::kFloat: setting->data.as_float = ConvertStringValue(args[pName].c_str(), setting->data.as_float); break;
            // Strings however are a special case, as it has its own allocator.
            case SettingBase::Type::kString:
            {
                // This is not at all how i want it to be :/
                static_cast<StringSetting*>(setting)->StoreValue(*setting, args[pName].c_str());
                break;
            }
            default: BASE_ASSERT(true, "LoadSettingsFromCommand(): Unknown type index for {}", setting->name); break;
            }
        });
}
} // namespace Console
