#pragma once

#include <juce_data_structures/juce_data_structures.h>

namespace rf::app
{
    /*  Per-user persistent settings (PROMPT.md section 3.7), stored with
        juce::ApplicationProperties in
        ~/Library/Application Support/Reamp Forge/Reamp Forge.settings (macOS) or
        %APPDATA%\Reamp Forge\Reamp Forge.settings (Windows).

        Message thread only. Typed accessors are added here as each phase needs them;
        the file list is deliberately never persisted.
    */
    class Settings
    {
    public:
        Settings();
        ~Settings();

        bool getIncludeSubfolders() const;
        void setIncludeSubfolders (bool);

        /** Writes pending changes now (also done automatically shortly after each change). */
        void save();

        /** The underlying file, for later phases that add keys. */
        juce::PropertiesFile& getPropertiesFile();

    private:
        juce::ApplicationProperties properties;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Settings)
    };
}
