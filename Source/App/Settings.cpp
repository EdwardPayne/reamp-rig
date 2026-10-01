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

        static constexpr auto tailMs            = "tailMs";
        static constexpr auto prefix            = "prefix";
        static constexpr auto suffix            = "suffix";
        static constexpr auto destinationMode   = "destinationMode";
        static constexpr auto subfolderName     = "subfolderName";
        static constexpr auto outputFolder      = "outputFolder";
        static constexpr auto mirrorStructure   = "mirrorStructure";
        static constexpr auto channelTag        = "channelTag";
        static constexpr auto bitDepth          = "bitDepth";
        static constexpr auto collisionPolicy   = "collisionPolicy";
    }

    namespace
    {
        const model::NamingOptions namingDefaults;

        juce::String toKey (model::DestinationMode m)
        {
            return m == model::DestinationMode::singleFolder ? "singleFolder" : "subfolder";
        }

        juce::String toKey (model::CollisionPolicy p)
        {
            switch (p)
            {
                case model::CollisionPolicy::overwrite:  return "overwrite";
                case model::CollisionPolicy::skip:       return "skip";
                case model::CollisionPolicy::autoNumber: break;
            }

            return "autoNumber";
        }
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

    //==============================================================================
    int Settings::getTailMs() const
    {
        return juce::jlimit (0, maxTailMs, file->getIntValue (key::tailMs, 0));
    }

    void Settings::setTailMs (int ms)
    {
        file->setValue (key::tailMs, juce::jlimit (0, maxTailMs, ms));
    }

    juce::String Settings::getPrefix() const                { return file->getValue (key::prefix, namingDefaults.prefix); }
    void Settings::setPrefix (const juce::String& p)        { file->setValue (key::prefix, p); }
    juce::String Settings::getSuffix() const                { return file->getValue (key::suffix, namingDefaults.suffix); }
    void Settings::setSuffix (const juce::String& s)        { file->setValue (key::suffix, s); }

    model::DestinationMode Settings::getDestinationMode() const
    {
        return file->getValue (key::destinationMode) == toKey (model::DestinationMode::singleFolder)
                 ? model::DestinationMode::singleFolder
                 : model::DestinationMode::besideSource;
    }

    void Settings::setDestinationMode (model::DestinationMode m)
    {
        file->setValue (key::destinationMode, toKey (m));
    }

    juce::String Settings::getSubfolderName() const
    {
        const auto name = file->getValue (key::subfolderName).trim();
        return name.isNotEmpty() ? name : namingDefaults.subfolderName;
    }

    void Settings::setSubfolderName (const juce::String& name)
    {
        file->setValue (key::subfolderName, name.trim());
    }

    juce::File Settings::getOutputFolder() const
    {
        const auto path = file->getValue (key::outputFolder);
        return juce::File::isAbsolutePath (path) ? juce::File (path) : juce::File();
    }

    void Settings::setOutputFolder (const juce::File& folder)
    {
        file->setValue (key::outputFolder, folder.getFullPathName());
    }

    bool Settings::getMirrorStructure() const               { return file->getBoolValue (key::mirrorStructure, true); }
    void Settings::setMirrorStructure (bool b)              { file->setValue (key::mirrorStructure, b); }
    bool Settings::getChannelTag() const                    { return file->getBoolValue (key::channelTag, false); }
    void Settings::setChannelTag (bool b)                   { file->setValue (key::channelTag, b); }

    int Settings::getBitDepth() const
    {
        const auto bits = file->getIntValue (key::bitDepth, 24);
        return bits == 16 || bits == 32 ? bits : 24;
    }

    void Settings::setBitDepth (int bits)
    {
        file->setValue (key::bitDepth, bits == 16 || bits == 32 ? bits : 24);
    }

    model::CollisionPolicy Settings::getCollisionPolicy() const
    {
        const auto value = file->getValue (key::collisionPolicy);

        for (auto p : { model::CollisionPolicy::overwrite, model::CollisionPolicy::skip })
            if (value == toKey (p))
                return p;

        return model::CollisionPolicy::autoNumber;
    }

    void Settings::setCollisionPolicy (model::CollisionPolicy p)
    {
        file->setValue (key::collisionPolicy, toKey (p));
    }

    model::NamingOptions Settings::getNamingOptions() const
    {
        model::NamingOptions o;
        o.mode = getDestinationMode();
        o.subfolderName = getSubfolderName();
        o.outputFolder = getOutputFolder();
        o.mirrorStructure = getMirrorStructure();
        o.prefix = getPrefix();
        o.suffix = getSuffix();
        o.channelTag = getChannelTag();
        o.collision = getCollisionPolicy();
        return o;
    }
}
