#pragma once

#include <juce_data_structures/juce_data_structures.h>
#include <juce_graphics/juce_graphics.h>

#include "../Engine/AudioDeviceInterface.h"
#include "../Engine/SyncMeasurement.h"
#include "../Model/OutputNaming.h"

#include <optional>
#include <utility>
#include <vector>

namespace rf::app
{
    /*  Per-user persistent settings (PROMPT.md section 3.7), an XML properties file at
        ~/Library/Application Support/Reamp Rig/Reamp Rig.settings (macOS) or
        %APPDATA%\Reamp Rig\Reamp Rig.settings (Windows).

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

        /** Pause between files (phase 6, owner request): after a file is written the batch
            waits this long before the next take, so amp and reverb tails die out.
            0..60 s, default 2 s, tenths of a second. */
        static constexpr double maxPauseBetweenFilesSeconds = 60.0;
        static constexpr double defaultPauseBetweenFilesSeconds = 2.0;
        double getPauseBetweenFilesSeconds() const;
        void setPauseBetweenFilesSeconds (double);

        /** All naming fields in one struct. */
        model::NamingOptions getNamingOptions() const;

        //==============================================================================
        // Sync (phase 5, PROMPT.md 3.6 and 3.7)

        /** Level of the sync test signal, -60..0 dBFS, default -12. */
        static constexpr float minSyncLevelDb = -60.0f;
        static constexpr float maxSyncLevelDb = 0.0f;
        static constexpr float defaultSyncLevelDb = -12.0f;
        float getSyncLevelDb() const;
        void setSyncLevelDb (float);

        /** The keyed sync store: one measurement per device type + input device + output
            device + sample rate + buffer size, kept as one XML value ("syncMeasurements").
            Entries that do not parse or make no sense are ignored on read, so a hand-edited
            file can never yield a bad measurement. */
        std::optional<engine::SyncMeasurement> getSyncMeasurement (const engine::SyncKey&) const;

        /** Adds or replaces the measurement for `key` (invalid keys are ignored). */
        void setSyncMeasurement (const engine::SyncKey&, const engine::SyncMeasurement&);

        /** Every valid entry, in stored order. */
        std::vector<std::pair<engine::SyncKey, engine::SyncMeasurement>> getSyncMeasurements() const;

        /** Deletes the measurement for `key` ("Forget" in the SYNC section). Returns false if
            there was none. */
        bool removeSyncMeasurement (const engine::SyncKey&);

        //==============================================================================
        // Window (phase 6, PROMPT.md 3.7 and section 5)

        /** The main window's last bounds (screen coordinates, without the title bar), or
            nothing when none were saved or the saved text does not parse. Not clamped: see
            clampWindowBounds. */
        std::optional<juce::Rectangle<int>> getWindowBounds() const;
        void setWindowBounds (juce::Rectangle<int>);

        //==============================================================================
        /** Writes pending changes now (also done automatically shortly after each change). */
        void save();

        juce::PropertiesFile& getPropertiesFile() noexcept   { return *file; }
        const juce::PropertiesFile& getPropertiesFile() const noexcept   { return *file; }

    private:
        static juce::PropertiesFile::Options makeOptions();
        void writeSyncMeasurements (const std::vector<std::pair<engine::SyncKey, engine::SyncMeasurement>>&);

        std::unique_ptr<juce::PropertiesFile> file;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Settings)
    };

    /** Fits saved window bounds onto the screen (PROMPT.md section 5: min 1100 x 700):
        the size is at least `minSize` and at most the display's usable area; a window that
        is not on any display (unplugged monitor, garbage) moves onto the display it overlaps
        most, else the first (main) one, keeping as much of its position as fits. Pure, so it
        is unit tested without a screen. Returns nothing when there is no display. */
    std::optional<juce::Rectangle<int>> clampWindowBounds (juce::Rectangle<int> saved,
                                                           const juce::Array<juce::Rectangle<int>>& displayUserAreas,
                                                           juce::Point<int> minSize);
}
