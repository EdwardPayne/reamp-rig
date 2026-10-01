#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "../Engine/AudioDeviceInterface.h"
#include "../Engine/DeviceSession.h"
#include "../Engine/DuplexEngine.h"
#include "../Engine/SourceLoader.h"
#include "../Engine/SyncMeasurement.h"
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
        - polls the engine snapshot at 30 Hz for the meters and the waveform playhead;
        - phase 5: shows the stored sync measurement of the current configuration (top-bar
          chip, SYNC readouts) after every device, rate or buffer change, and locks the AUDIO
          controls while a batch or a sync measurement owns the device;
        - phase 6: a device that disappears stops audition, pauses the batch and stops Sync
          (callbacks), and is reopened by itself when it is listed again (as is a saved device
          that was missing at launch), unless a batch is running.

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
            ("Cannot <action>: ...", the same reasons as everywhere else, e.g. the
            microphone-denied message). */
        bool checkCanRecord (const juce::String& action = "start");

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

        //==============================================================================
        // Sync support (phase 5)

        /** The stored measurement for the configuration the device runs in (none when closed). */
        std::optional<engine::SyncMeasurement> findSync (const engine::DeviceStatus&) const;
        std::optional<engine::SyncMeasurement> findSync (const engine::SyncKey&) const;

        /** "Apollo Twin · 48 kHz · 256", "Out: … · In: … · 48 kHz · 512". */
        juce::String describeKey (const engine::SyncKey&) const;

        /** Re-reads the stored measurement for the current configuration into the top-bar
            chip and the SYNC readouts (also done after every device change). */
        void refreshSyncUi();

        /** While Sync measures: audition stopped, AUDIO controls locked (like a batch). */
        void setSyncActive (bool);
        bool isSyncActive() const noexcept                          { return syncActive; }

        /** Called with every engine snapshot before onSnapshot (the SyncController). */
        std::function<void (const engine::EngineSnapshot&)> onSyncSnapshot;

        /** Called when the open device stops or disappears (message thread): the batch
            (onDeviceStopped) and a running sync measurement (onSyncDeviceStopped). */
        std::function<void()> onDeviceStopped, onSyncDeviceStopped;

        /** Called when a device that had been lost (or was missing at launch) is open again
            (message thread), with its description. */
        std::function<void (const juce::String&)> onDeviceReopened;

        /** Called after the SYNC readouts and the chip were refreshed from the store (device,
            rate or buffer change, or a measurement stored or forgotten). */
        std::function<void()> onSyncUiRefreshed;

        /** Asked before the saved device is reopened by itself: while a batch runs (on a
            fallback device) it is not switched; a batch paused by the loss is fine. */
        std::function<bool()> isBatchRunning;

    private:
        void audioDeviceChanged() override;
        void timerCallback() override;
        void deviceLost();
        void checkReconnect();

        void applyConfig (const engine::DeviceConfig& wanted, bool persist);
        void requestMicrophoneIfNeeded();
        void showMicrophoneDenied();
        void refreshDeviceUi();
        void refreshSyncUi (const engine::DeviceStatus&);
        void refreshPeakReadout();
        void wireControls();
        void userChangedConfig (const std::function<void (engine::DeviceConfig&)>& change);
        void requestSourceLoad();
        void sourceLoaded (std::shared_ptr<const engine::LoadedSource>, const juce::String& error);
        void updateAuditionCheck (const engine::EngineSnapshot&);
        void applyBatchLock();
        void setLocks (bool batch, bool sync);

        Settings& settings;
        engine::AudioDeviceInterface& device;
        Views views;

        engine::DeviceSession session { device };
        engine::DuplexEngine duplex;
        engine::SourceLoader loader;

        engine::DeviceConfig selected;          // what the UI shows (resolved)

        // Reconnection (phase 6): the devices the user wants (saved or picked, or the ones the
        // app chose on a first launch); reopened by themselves when they are listed again
        // after being lost or missing at launch. Checked on every device-list change and on
        // a slow poll (every 2 s).
        engine::DeviceConfig preferred;
        engine::ReconnectWatch reconnect;
        int slowTicks = 0;
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
        bool syncActive = false;

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
