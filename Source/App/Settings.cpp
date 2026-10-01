#include "Settings.h"

#include <algorithm>
#include <cmath>

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

        static constexpr auto syncLevelDb       = "syncLevelDb";
        static constexpr auto syncMeasurements  = "syncMeasurements";
    }

    namespace
    {
        const model::NamingOptions namingDefaults;

        // Keyed sync store: <SYNC><MEASUREMENT type=".." input=".." output=".." rate=".." buffer=".."
        //                    samples=".." ms=".." peakDb=".." psrDb=".." used=".." total=".."
        //                    confidence="high|medium|low" date="ISO 8601"/>...</SYNC>
        namespace sync
        {
            static constexpr auto root = "SYNC", entry = "MEASUREMENT";
            static constexpr auto type = "type", input = "input", output = "output", rate = "rate", buffer = "buffer";
            static constexpr auto samples = "samples", ms = "ms", peakDb = "peakDb", psrDb = "psrDb", used = "used",
                                  total = "total", confidence = "confidence", date = "date";
        }

        bool isNumber (const juce::String& s, bool allowFraction)
        {
            const auto t = s.trim();
            return t.isNotEmpty() && t.containsOnly (allowFraction ? "-+.0123456789eE" : "-+0123456789")
                && t.containsAnyOf ("0123456789");
        }

        /** Parses one entry; nothing if any part is missing or out of range. */
        std::optional<std::pair<engine::SyncKey, engine::SyncMeasurement>> parseSyncEntry (const juce::XmlElement& e)
        {
            if (! e.hasTagName (sync::entry))
                return std::nullopt;

            for (auto* attr : { sync::type, sync::rate, sync::buffer, sync::samples, sync::confidence })
                if (! e.hasAttribute (attr))
                    return std::nullopt;

            const auto rateText = e.getStringAttribute (sync::rate), bufferText = e.getStringAttribute (sync::buffer),
                       samplesText = e.getStringAttribute (sync::samples);

            if (! isNumber (rateText, true) || ! isNumber (bufferText, false) || ! isNumber (samplesText, false))
                return std::nullopt;

            engine::SyncKey k;
            k.typeName = e.getStringAttribute (sync::type);
            k.inputDevice = e.getStringAttribute (sync::input);
            k.outputDevice = e.getStringAttribute (sync::output);
            k.sampleRate = rateText.getDoubleValue();
            k.bufferSize = bufferText.getIntValue();

            if (! k.isValid() || ! std::isfinite (k.sampleRate) || k.sampleRate > 1.0e6 || k.bufferSize > 1 << 16)
                return std::nullopt;

            engine::SyncMeasurement m;
            m.samples = samplesText.getIntValue();

            // A round trip is positive and shorter than ten seconds.
            if (m.samples <= 0 || (double) m.samples > 10.0 * k.sampleRate)
                return std::nullopt;

            const auto confidence = engine::confidenceFromString (e.getStringAttribute (sync::confidence));

            if (! confidence.has_value())
                return std::nullopt;

            m.confidence = *confidence;
            m.ms = (double) m.samples * 1000.0 / k.sampleRate;     // derived, never trusted from the file

            const auto peak = e.getDoubleAttribute (sync::peakDb, -100.0);
            m.returnedPeakDb = (float) (std::isfinite (peak) ? juce::jlimit (-200.0, 0.0, peak) : -100.0);

            const auto psr = e.getDoubleAttribute (sync::psrDb, 0.0);
            m.peakToSidelobeDb = std::isfinite (psr) ? juce::jlimit (0.0, 200.0, psr) : 0.0;

            m.repeatsTotal = juce::jlimit (0, 100, e.getIntAttribute (sync::total, 0));
            m.repeatsUsed = juce::jlimit (0, m.repeatsTotal, e.getIntAttribute (sync::used, 0));

            // An unreadable date leaves the measurement valid, with its date unknown.
            const auto dateText = e.getStringAttribute (sync::date).trim();
            m.date = dateText.isNotEmpty() && dateText.containsChar ('T') ? juce::Time::fromISO8601 (dateText) : juce::Time();

            if (m.date.toMilliseconds() < 0)
                m.date = juce::Time();

            return std::make_pair (k, m);
        }

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
        options.applicationName     = "Reamp Rig";
        options.folderName          = "Reamp Rig";
        options.filenameSuffix      = "settings";
        options.osxLibrarySubFolder = "Application Support";
        options.storageFormat       = juce::PropertiesFile::storeAsXML;
        options.millisecondsBeforeSaving = 500;
        return options;
    }

    namespace
    {
        /*  The app was called "Reamp Forge" until 2026-10-01. On the first launch under the new
            name, copy the old settings file (device config, sync measurements) so nothing is lost.
            Only runs when the new file does not exist yet; the old file is left in place. */
        juce::PropertiesFile::Options optionsWithLegacyMigration (juce::PropertiesFile::Options options)
        {
            const auto target = options.getDefaultFile();

            if (! target.existsAsFile())
            {
                auto legacy = options;
                legacy.applicationName = "Reamp Forge";
                legacy.folderName      = "Reamp Forge";

                const auto source = legacy.getDefaultFile();

                if (source.existsAsFile())
                {
                    target.getParentDirectory().createDirectory();
                    source.copyFileTo (target);
                }
            }

            return options;
        }
    }

    Settings::Settings()
        : file (std::make_unique<juce::PropertiesFile> (optionsWithLegacyMigration (makeOptions())))
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

    //==============================================================================
    float Settings::getSyncLevelDb() const
    {
        const auto db = (float) file->getDoubleValue (key::syncLevelDb, defaultSyncLevelDb);
        return std::isfinite (db) ? juce::jlimit (minSyncLevelDb, maxSyncLevelDb, db) : defaultSyncLevelDb;
    }

    void Settings::setSyncLevelDb (float db)
    {
        file->setValue (key::syncLevelDb, (double) juce::jlimit (minSyncLevelDb, maxSyncLevelDb, std::isfinite (db) ? db : defaultSyncLevelDb));
    }

    std::vector<std::pair<engine::SyncKey, engine::SyncMeasurement>> Settings::getSyncMeasurements() const
    {
        std::vector<std::pair<engine::SyncKey, engine::SyncMeasurement>> entries;
        const auto xml = file->getXmlValue (key::syncMeasurements);

        if (xml == nullptr || ! xml->hasTagName (sync::root))
            return entries;

        for (auto* e : xml->getChildIterator())
        {
            auto parsed = parseSyncEntry (*e);

            if (! parsed.has_value())
                continue;

            // A later duplicate replaces an earlier one.
            const auto existing = std::find_if (entries.begin(), entries.end(), [&] (const auto& p) { return p.first == parsed->first; });

            if (existing != entries.end())
                existing->second = parsed->second;
            else
                entries.push_back (std::move (*parsed));
        }

        return entries;
    }

    std::optional<engine::SyncMeasurement> Settings::getSyncMeasurement (const engine::SyncKey& k) const
    {
        for (const auto& [key, value] : getSyncMeasurements())
            if (key == k)
                return value;

        return std::nullopt;
    }

    void Settings::setSyncMeasurement (const engine::SyncKey& k, const engine::SyncMeasurement& m)
    {
        if (! k.isValid() || m.samples <= 0)
            return;

        auto entries = getSyncMeasurements();
        const auto existing = std::find_if (entries.begin(), entries.end(), [&] (const auto& p) { return p.first == k; });

        if (existing != entries.end())
            existing->second = m;
        else
            entries.emplace_back (k, m);

        juce::XmlElement root (sync::root);

        for (const auto& [key, value] : entries)
        {
            auto* e = root.createNewChildElement (sync::entry);
            e->setAttribute (sync::type, key.typeName);
            e->setAttribute (sync::input, key.inputDevice);
            e->setAttribute (sync::output, key.outputDevice);
            e->setAttribute (sync::rate, key.sampleRate);
            e->setAttribute (sync::buffer, key.bufferSize);
            e->setAttribute (sync::samples, value.samples);
            e->setAttribute (sync::ms, (double) value.samples * 1000.0 / key.sampleRate);
            e->setAttribute (sync::peakDb, juce::String (value.returnedPeakDb, 2));
            e->setAttribute (sync::psrDb, juce::String (value.peakToSidelobeDb, 2));
            e->setAttribute (sync::used, value.repeatsUsed);
            e->setAttribute (sync::total, value.repeatsTotal);
            e->setAttribute (sync::confidence, engine::toString (value.confidence));
            e->setAttribute (sync::date, value.date.toMilliseconds() > 0 ? value.date.toISO8601 (true) : juce::String());
        }

        file->setValue (key::syncMeasurements, &root);
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
