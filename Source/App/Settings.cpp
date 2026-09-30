#include "Settings.h"

namespace rf::app
{
    namespace key
    {
        static constexpr auto includeSubfolders = "includeSubfolders";
    }

    Settings::Settings()
    {
        juce::PropertiesFile::Options options;
        options.applicationName     = "Reamp Forge";
        options.folderName          = "Reamp Forge";
        options.filenameSuffix      = "settings";
        options.osxLibrarySubFolder = "Application Support";
        options.storageFormat       = juce::PropertiesFile::storeAsXML;
        options.millisecondsBeforeSaving = 500;

        properties.setStorageParameters (options);
    }

    Settings::~Settings()
    {
        save();
    }

    juce::PropertiesFile& Settings::getPropertiesFile()
    {
        auto* file = properties.getUserSettings();
        jassert (file != nullptr);
        return *file;
    }

    void Settings::save()
    {
        properties.saveIfNeeded();
    }

    bool Settings::getIncludeSubfolders() const
    {
        return const_cast<Settings*> (this)->getPropertiesFile().getBoolValue (key::includeSubfolders, true);
    }

    void Settings::setIncludeSubfolders (bool shouldInclude)
    {
        getPropertiesFile().setValue (key::includeSubfolders, shouldInclude);
    }
}
