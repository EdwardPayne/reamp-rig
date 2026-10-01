#include "Settings.h"

namespace rf::app
{
    namespace key
    {
        static constexpr auto includeSubfolders = "includeSubfolders";

        static constexpr auto deviceType        = "deviceType";
        static constexpr auto inputDevice       = "inputDevice";
        static constexpr auto outputDevice      = "outputDevice";
        static constexpr auto sampleRate        = "sampleRate";
        static constexpr auto bufferSize        = "bufferSize";
        static constexpr auto inputChannel      = "inputChannel";
        static constexpr auto inputChannelName  = "inputChannelName";
        static constexpr auto outputChannel     = "outputChannel";
        static constexpr auto outputChannelName = "outputChannelName";
        static constexpr auto outputGainDb      = "outputGainDb";
    }

    juce::PropertiesFile::Options Settings::makeOptions()
    {
        juce::PropertiesFile::Options options;
        options.applicationName     = "Reamp Forge";
        options.folderName          = "Reamp Forge";
        options.filenameSuffix      = "settings";
        options.osxLibrarySubFolder = "Application Support";
        options.storageFormat       = juce::PropertiesFile::storeAsXML;
        options.millisecondsBeforeSaving = 500;
        return options;
    }

    Settings::Settings()
        : file (std::make_unique<juce::PropertiesFile> (makeOptions()))
    {
    }

    Settings::Settings (const juce::File& f)
        : file (std::make_unique<juce::PropertiesFile> (f, makeOptions()))
    {
    }

    Settings::~Settings()
    {
        save();
    }

    void Settings::save()
    {
        file->saveIfNeeded();
    }

    //==============================================================================
    bool Settings::getIncludeSubfolders() const
    {
        return file->getBoolValue (key::includeSubfolders, true);
    }

    void Settings::setIncludeSubfolders (bool shouldInclude)
    {
        file->setValue (key::includeSubfolders, shouldInclude);
    }

    //==============================================================================
    engine::DeviceConfig Settings::getDeviceConfig() const
    {
        engine::DeviceConfig c;
        c.typeName          = file->getValue (key::deviceType);
        c.inputDevice       = file->getValue (key::inputDevice);
        c.outputDevice      = file->getValue (key::outputDevice);
        c.sampleRate        = juce::jmax (0.0, file->getDoubleValue (key::sampleRate, 0.0));
        c.bufferSize        = juce::jmax (0, file->getIntValue (key::bufferSize, 0));
        c.inputChannel      = juce::jmax (-1, file->getIntValue (key::inputChannel, -1));
        c.inputChannelName  = file->getValue (key::inputChannelName);
        c.outputChannel     = juce::jmax (-1, file->getIntValue (key::outputChannel, -1));
        c.outputChannelName = file->getValue (key::outputChannelName);
        return c;
    }

    void Settings::setDeviceConfig (const engine::DeviceConfig& c)
    {
        file->setValue (key::deviceType,        c.typeName);
        file->setValue (key::inputDevice,       c.inputDevice);
        file->setValue (key::outputDevice,      c.outputDevice);
        file->setValue (key::sampleRate,        c.sampleRate);
        file->setValue (key::bufferSize,        c.bufferSize);
        file->setValue (key::inputChannel,      c.inputChannel);
        file->setValue (key::inputChannelName,  c.inputChannelName);
        file->setValue (key::outputChannel,     c.outputChannel);
        file->setValue (key::outputChannelName, c.outputChannelName);
    }

    float Settings::getOutputGainDb() const
    {
        const auto db = (float) file->getDoubleValue (key::outputGainDb, 0.0);
        return std::isfinite (db) ? juce::jlimit (minOutputGainDb, maxOutputGainDb, db) : 0.0f;
    }

    void Settings::setOutputGainDb (float db)
    {
        file->setValue (key::outputGainDb, (double) juce::jlimit (minOutputGainDb, maxOutputGainDb, db));
    }
}
