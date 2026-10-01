#pragma once

#include <juce_data_structures/juce_data_structures.h>

#include "../Engine/AudioDeviceInterface.h"
#include "../Model/OutputNaming.h"

namespace rf::app
{
    /*  Per-user persistent settings (PROMPT.md section 3.7), an XML properties file at
        ~/Library/Application Support/Reamp Forge/Reamp Forge.settings (macOS) or
        %APPDATA%\Reamp Forge\Reamp Forge.settings (Windows).

        Message thread only. Typed accessors are added here as each phase needs them;
        the file list is deliberately never persisted. Changes are written shortly after they
        are made and when the object is destroyed.
    */
    class Settings
    {
    public:
        /** The per-user settings file. */
        Settings();

        /** A specific file (tests). */
        explicit Settings (const juce::File&);

        ~Settings();

        //==============================================================================
        bool getIncludeSubfolders() const;
        void setIncludeSubfolders (bool);

        /** Device type, input/output devices, sample rate, buffer size and the input/output
            channels (index and driver name). Empty/0/-1 fields mean "not chosen yet". */
        engine::DeviceConfig getDeviceConfig() const;
        void setDeviceConfig (const engine::DeviceConfig&);

        /** Output level in dB, -60..+12, default 0. */
        static constexpr float minOutputGainDb = -60.0f;
        static constexpr float maxOutputGainDb = 12.0f;
        float getOutputGainDb() const;
        void setOutputGainDb (float);

        //==============================================================================
        // Output files and batch options (phase 4, PROMPT.md 3.3.2 and 3.4)

        /** Extra recording after the source, 0..60000 ms, default 0. */
        static constexpr int maxTailMs = 60000;
        int getTailMs() const;
        void setTailMs (int);

        /** Default "" and "_reamp". */
        juce::String getPrefix() const;
        void setPrefix (const juce::String&);
        juce::String getSuffix() const;
        void setSuffix (const juce::String&);

        /** Default: subfolder next to the source. */
        model::DestinationMode getDestinationMode() const;
        void setDestinationMode (model::DestinationMode);

        /** Default "Reamped"; an empty saved value reads as the default. */
        juce::String getSubfolderName() const;
        void setSubfolderName (const juce::String&);

        /** Single output folder mode; empty (not chosen) by default. */
        juce::File getOutputFolder() const;
        void setOutputFolder (const juce::File&);

        /** Mirror the source folder structure under the output folder; default on. */
        bool getMirrorStructure() const;
        void setMirrorStructure (bool);

        /** Append _L / _R for stereo sources; default off. */
        bool getChannelTag() const;
        void setChannelTag (bool);

        /** 16, 24 (default) or 32 (float). */
        int getBitDepth() const;
        void setBitDepth (int);

        /** Default auto-number. */
        model::CollisionPolicy getCollisionPolicy() const;
        void setCollisionPolicy (model::CollisionPolicy);

        /** All naming fields in one struct. */
        model::NamingOptions getNamingOptions() const;

        //==============================================================================
        /** Writes pending changes now (also done automatically shortly after each change). */
        void save();

        juce::PropertiesFile& getPropertiesFile() noexcept   { return *file; }
        const juce::PropertiesFile& getPropertiesFile() const noexcept   { return *file; }

    private:
        static juce::PropertiesFile::Options makeOptions();

        std::unique_ptr<juce::PropertiesFile> file;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Settings)
    };
}
