#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "../Engine/SyncMeasurer.h"
#include "../UI/SidebarSections.h"
#include "../UI/StatusBar.h"
#include "../UI/TopBar.h"
#include "AudioController.h"
#include "Settings.h"

namespace rf::app
{
    /*  The SYNC section (PROMPT.md 3.6) on the message thread:

        - the sync level (persisted, -60..0 dBFS, default -12);
        - Sync runs an engine::SyncMeasurer on the current configuration (5 repeats with a
          progress indication; the button turns into Stop), with audition stopped and the
          AUDIO controls and Start locked meanwhile; it is disabled while a batch or an
          audition runs;
        - a passing measurement is stored in the keyed store (Settings) for exactly the
          configuration it was made in, and the chip/readouts are refreshed; a failure is shown
          in the section and the status bar and stores nothing (an older measurement for that
          configuration is kept).

        It drives the engine only through AudioController and engine::SyncMeasurer.
    */
    class SyncController final
    {
    public:
        struct Views
        {
            ui::SyncSection& sync;
            ui::TopBar& topBar;
            ui::StatusBar& statusBar;
        };

        SyncController (Settings&, AudioController&, Views);
        ~SyncController();

        /** The Sync button: starts a measurement, or stops the running one. */
        void toggle();
        bool start();
        void cancel();

        bool isMeasuring() const noexcept           { return measurer.isRunning(); }

        /** Development aid: sets the level for this run without saving it (--sync-level). */
        void overrideLevel (float db);

        //==============================================================================
        /** Development aid (--sync-check): measures the current configuration, then each of
            `extraRates` (reopening the device at that rate, restored at the end), printing
            every repeat and the result to stderr. A passing measurement is stored like the
            Sync button does. `expectedRoundTrip`, when given (the virtual loopback), must
            match every measurement. Reports success when every measurement passed. */
        void runCheck (juce::Array<double> extraRates, std::function<int()> expectedRoundTrip,
                       std::function<void (bool ok)> onDone);

        /** True while a measurement or a --sync-check sequence runs (snapshots wait). */
        bool isBusy() const noexcept                { return measurer.isRunning() || check.active; }

    private:
        void handleSnapshot (const engine::EngineSnapshot&);
        void finished();
        void refreshControls();
        void unlock();
        void checkNext();
        void printCheck (const juce::String&) const;

        Settings& settings;
        AudioController& audio;
        Views views;

        engine::SyncMeasurer measurer;
        engine::SyncKey measuringKey;
        int lastRepeatsReported = 0;

        struct Check
        {
            bool active = false;
            juce::Array<double> rates;
            int index = 0;
            bool allOk = true;
            engine::DeviceConfig startConfig;
            std::function<int()> expectedRoundTrip;
            std::function<void (bool)> onDone;
        };

        Check check;
        std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SyncController)
    };
}
