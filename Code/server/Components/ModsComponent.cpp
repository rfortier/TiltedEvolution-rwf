
#include <Components.h>
#include <es_loader/ESLoader.h>

uint32_t ModsComponent::AddStandard(const String& acpFilename) noexcept
{
    const auto itor = m_standardMods.find(acpFilename);
    if (itor != std::end(m_standardMods))
    {
        itor.value().refCount++;
        return itor->second.id;
    }

    // The emplace allocates a map node and a copy of the file name; handing out the
    // id does not. The id is taken first so that the answer stays unique even when
    // the row cannot be stored: two mods sharing an id would make every form id the
    // client derives from it collide, which is worse than one mod that is not
    // deduplicated. Only the dedup row is lost - nothing on the server reads the
    // table except this lookup.
    const auto id = m_seed++;

    try
    {
        m_standardMods.emplace(acpFilename, Entry{id, 1});
    }
    catch (...)
    {
        Diagnostics::ReportFailure(__FUNCTION__, "ran out of memory while recording a client mod");
    }

    return id;
}

uint32_t ModsComponent::AddLite(const String& acpFilename) noexcept
{
    const auto itor = m_liteMods.find(acpFilename);
    if (itor != std::end(m_liteMods))
    {
        itor.value().refCount++;
        return itor->second.id;
    }

    // The emplace allocates a map node and a copy of the file name; handing out the
    // id does not. The id is taken first so that the answer stays unique even when
    // the row cannot be stored: two mods sharing an id would make every form id the
    // client derives from it collide, which is worse than one mod that is not
    // deduplicated. Only the dedup row is lost - nothing on the server reads the
    // table except this lookup.
    const auto id = m_seed++;

    try
    {
        m_liteMods.emplace(acpFilename, Entry{id, 1});
    }
    catch (...)
    {
        Diagnostics::ReportFailure(__FUNCTION__, "ran out of memory while recording a client mod");
    }

    return id;
}

void ModsComponent::AddServerMod(const ESLoader::PluginData& acData)
{
    // kind of a hack since we want to store both, so we take the two byte value
    m_serverMods.emplace(acData.m_filename, Entry{acData.m_liteId, 1});
}

bool ModsComponent::IsInstalled(const String& acpFilename) const noexcept
{
    auto it = std::find_if(m_serverMods.begin(), m_serverMods.end(), [&](const TModList::value_type& aEntry) { return aEntry.first == acpFilename; });

    return it != m_serverMods.end();
}
