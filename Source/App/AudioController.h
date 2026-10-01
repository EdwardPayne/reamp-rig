#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "../Engine/AudioDeviceInterface.h"
#include "../Engine/DeviceSession.h"
#include "../Engine/DuplexEngine.h"
#include "../Engine/SourceLoader.h"
#include "../UI/SidebarSections.h"
#include "../UI/StatusBar.h"
#include "../UI/TopBar.h"
#include "../UI/WaveformPanel.h"
#include "CommandLine.h"
#include "MicrophonePermission.h"
#include "Settings.h"

namespace rf::app
{
    /*  Message-thread glue for the audio device layer (phase 3):

        - opens the saved device through engine::DeviceSession (with graceful fallback and
          status-bar warnings) and keeps the AUDIO section, the top-bar summary/sync chip and
          the SYNC section's driver-latency readout in step with the device;
        - persists every device/channel/level change the user makes (Settings);
        - asks for microphone access before opening an input (macOS) and explains a denial;
        - preloads the lead file's played channel on the loader thread (for audition and the
          "peak at output" readout) and runs audition through the DuplexEngine;
        - polls the engine snapshot at 30 Hz for the meters and the waveform playhead.

        It never touches engine internals: device state comes from DeviceStatus plus
        AudioDeviceInterface::Listener, engine state from EngineSnapshot.
    */
    class AudioController final : private engine::AudioDeviceInterface::Listener,
                                  private juce::Timer
    {
    public:
        struct Views
        {
            ui::AudioSection& audio;
            ui::SyncSection& sync;
            ui::TopBar& topBar;
            ui::StatusBar& statusBar;
            ui::WaveformPanel& waveform;
        };

        /** `needsMicrophonePermission` is false for devices that never touch the audio
            hardware (the --virtual-device LoopbackTestDevice). */
        AudioController (Settings&, engine::AudioDeviceInterface&, Views, bool needsMicrophonePermission = true);
        ~AudioController() override;

        /** Opens the saved device, overridden (for this run only) by the --device... flags. */
        void openInitialDevice (const LaunchOptions&);

        /** The lead file changed (or its L/R channel). An empty file means no lead. */
        void setLead (const juce::File&, int channel);

        void toggleAudition();
        bool startAudition();
        void stopAudition();
        bool isAuditioning() const noexcept   { return duplex.isAuditioning() || auditionPending; }

        /** True while the lead is loading or an audition check has not been running long enough
            for a snapshot. */
        bool isBusy() const;

        /** Development aid for --audition-check (see CommandLine.h). */
        void runAuditionCheck (double seconds, std::function<void (bool ok)> onDone);

        //==============================================================================
        // Batch support (phase 4). The batch drives the same engine through these.
        engine::DuplexEngine& getEngine() noexcept                  { return duplex; }
        engine::DeviceStatus getDeviceStatus()                      { return device.getStatus(); }
        const engine::DeviceConfig& getSelectedConfig() const noexcept   { return selected; }
        juce::String describeDevice (const engine::DeviceStatus&) const;

        /** True if a take can record now: device open, output and input channel open (the
            input needs microphone access on macOS). Otherwise shows why in the status bar
            (the same message as everywhere else, e.g. the microphone-denied one). */
        bool checkCanRecord();

        /** Reopens the device at `rate` for the batch (not saved). Returns the rate it runs at. */
        double switchSampleRate (double rate);

        /** Reopens `config` (not saved) if the device differs from it, e.g. after a batch
            switched the sample rate. */
        void restoreConfig (const engine::DeviceConfig&);

        /** While a batch runs: audition is stopped and the AUDIO controls are disabled (the
            meters stay live); the lead's audition preview is not reloaded on rate changes. */
        void setBatchActive (bool);
        bool isBatchActive() const noexcept                         { return batchActive; }

        /** Called with every engine snapshot (30 Hz, message thread). */
        std::function<void (const engine::EngineSnapshot&)> onSnapshot;

        /** Called when the open device stops or disappears (message thread). */
        std::function<void()> onDeviceStopped;

    private:
        void audioDeviceChanged() override;
        void timerCallback() override;

        void applyConfig (const engine::DeviceConfig& wanted, bool persist);
        void requestMicrophoneIfNeeded();
        void showMicrophoneDenied();
        void refreshDeviceUi();
        void refreshPeakReadout();
        void wireControls();
        void userChangedConfig (const std::function<void (engine::DeviceConfig&)>& change);
        void requestSourceLoad();
        void sourceLoaded (std::shared_ptr<const engine::LoadedSource>, const juce::String& error);
        void updateAuditionCheck (const engine::EngineSnapshot&);
        void applyBatchLock();

        Settings& settings;
        engine::AudioDeviceInterface& device;
        Views views;

        engine::DeviceSession session { device };
        engine::DuplexEngine duplex;
        engine::SourceLoader loader;

        engine::DeviceConfig selected;          // what the UI shows (resolved)
        bool deviceWasOpen = false;
        bool applying = false;
        const bool needsPermission;
        bool outputOnly = false;                // --no-input
        bool micRequestInFlight = false;
        bool inputBlockedByPermission = false;

        // While microphone access is undetermined no device with inputs is opened (it would
        // block in coreaudiod until the macOS prompt is answered); the wanted configuration
        // waits here and is opened when the answer arrives.
        bool waitingForPermission = false;
        engine::DeviceConfig pendingConfig;
        bool pendingPersist = false;

        juce::File leadFile;
        int leadChannel = 0;
        engine::LoadRequest loadedRequest;      // what `loaded` (or the pending load) is for
        std::shared_ptr<const engine::LoadedSource> loaded;
        bool auditionPending = false;           // start as soon as the lead has loaded
        bool batchActive = false;

        struct AuditionCheck
        {
            bool active = false;
            double seconds = 0.0;
            double startedMs = 0.0;             // 0 until playback has started
            double lastPrintMs = 0.0;
            juce::int64 firstPlayhead = -1;
            juce::int64 lastPlayhead = 0;
            float maxOutputPeak = 0.0f;
            float maxInputPeak = 0.0f;
            std::function<void (bool)> onDone;
        };

        AuditionCheck check;

        std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioController)
    };
}
