#pragma once

#include <juce_data_structures/juce_data_structures.h>

#include "../Engine/AudioDeviceInterface.h"

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
