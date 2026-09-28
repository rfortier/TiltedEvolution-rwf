#include "OstimNG-API-Scene.h"
OstimNG_API::Scene::ISceneInterface* g_ostimSceneInterface = nullptr;
namespace OstimNG_API::Scene
{
     ///
    /// Gets pointer to API singleton from external plugin.
    ///
    ISceneInterface* GetAPI() 
    {
        if (g_ostimSceneInterface) return g_ostimSceneInterface;    

        const auto pluginHandle = GetModuleHandle(L"Ostim.dll");
        const auto requestAPIFunction = (_RequestPluginAPI_Scene)(GetProcAddress(pluginHandle, "RequestPluginAPI_Scene"));
         if (!requestAPIFunction) {
            return nullptr;
        }
        const auto plugin = SKSE::PluginDeclaration::GetSingleton();
        g_ostimSceneInterface = requestAPIFunction(InterfaceVersion::V1, std::string(plugin->GetName()).c_str(), plugin->GetVersion());
        return g_ostimSceneInterface; 
    }
}

