#include "daw/controller/PluginLibrary.h"

namespace daw::controller
{

PluginLibrary::PluginLibrary(Options newOptions)
    : options(std::move(newOptions))
    , catalogue(PluginCatalogue::loadFrom(options.catalogueFile))
    , scanner(options.scanner)
{
    if (options.addFormats)
        options.addFormats(formats);
    else
        juce::addHeadlessDefaultFormatsToManager(formats);
}

std::map<juce::String, juce::StringArray> PluginLibrary::scannerFolders()
{
    // LV2 plugins are found by URI in the folders a process has read: the scanner
    // process reads the same ones.
    std::map<juce::String, juce::StringArray> folders;
    for (auto* format : formats.getFormats())
        if (format->getName() == "LV2")
        {
            const auto path = catalogue.foldersFor(*format);
            juce::StringArray list;
            for (int i = 0; i < path.getNumPaths(); ++i)
                list.add(path.getRawString(i));
            folders[format->getName()] = list;
        }
    return folders;
}

void PluginLibrary::scan(bool everything)
{
    const auto plan = catalogue.planScan(formats, everything);
    if (!plan.empty())
        scanner.scan(plan, scannerFolders());
    if (catalogue.takeChanged())
        catalogue.saveTo(options.catalogueFile);
}

void PluginLibrary::retry(const ScanItem& item)
{
    catalogue.unblock(item);
    scanner.scan({item}, scannerFolders());
    if (catalogue.takeChanged())
        catalogue.saveTo(options.catalogueFile);
}

void PluginLibrary::cancelScan()
{
    scanner.cancel();
}

bool PluginLibrary::update()
{
    for (auto& result : scanner.takeResults())
    {
        if (result.outcome.failure)
            catalogue.block(result.item, *result.outcome.failure, result.outcome.detail);
        else
            catalogue.addScanned(result.item, std::move(result.outcome.plugins));
    }
    if (!catalogue.takeChanged())
        return false;
    catalogue.saveTo(options.catalogueFile);
    return true;
}

bool PluginLibrary::scanAndWait(bool everything, int timeoutMilliseconds)
{
    scan(everything);
    const bool done = scanner.waitUntilDone(timeoutMilliseconds);
    update();
    return done;
}

} // namespace daw::controller
