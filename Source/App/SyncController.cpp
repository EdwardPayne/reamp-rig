#include "SyncController.h"
#include "../UI/Format.h"
#include "../UI/LookAndFeel.h"
#include "../UI/Theme.h"

#include <cstdio>

namespace rf::app
{
    namespace colour = ui::theme::colour;
    namespace format = ui::format;
    using Tone = ui::StatusBar::Tone;

    namespace
    {
        juce::String dot()      { return ui::utf8 (" \xc2\xb7 "); }
        juce::String ellipsis() { return ui::utf8 ("\xe2\x80\xa6"); }
        juce::String plusMinus() { return ui::utf8 ("\xc2\xb1"); }

        juce::String dbText (float db)  { return juce::String (db, 1) + " dBFS"; }

        constexpr auto defaultButtonTooltip = "Measure the round-trip latency of the current output/input pair: a click and "
                                              "a short sweep are played and recorded five times. Bypass the amp first.";
    }

    SyncController::SyncController (Settings& s, AudioController& a, Views v)
        : settings (s), audio (a), views (v)
    {
        auto& level = views.sync.getLevelSlider();
        level.setValue (settings.getSyncLevelDb(), juce::dontSendNotification);
        level.onValueChange = [this] { settings.setSyncLevelDb ((float) views.sync.getLevelSlider().getValue()); };

        views.sync.getSyncButton().onClick = [this] { toggle(); };
        views.sync.getForgetButton().onClick = [this] { forget(); };
        audio.onSyncSnapshot = [this] (const engine::EngineSnapshot& snap) { handleSnapshot (snap); };
        audio.onSyncDeviceStopped = [this] { deviceStopped(); };

        refreshControls();
    }

    SyncController::~SyncController()
    {
        alive->store (false);
        audio.onSyncSnapshot = nullptr;
        audio.onSyncDeviceStopped = nullptr;
        views.sync.getLevelSlider().onValueChange = nullptr;
        views.sync.getSyncButton().onClick = nullptr;
        views.sync.getForgetButton().onClick = nullptr;

        if (measurer.isRunning())
        {
            measurer.cancel();
            unlock();
        }
    }

    void SyncController::overrideLevel (float db)
    {
        views.sync.getLevelSlider().setValue (juce::jlimit (Settings::minSyncLevelDb, Settings::maxSyncLevelDb, db),
                                              juce::dontSendNotification);
    }

    //==============================================================================
    void SyncController::toggle()
    {
        if (measurer.isRunning())
            cancel();
        else
            start();
    }

    bool SyncController::start()
    {
        if (measurer.isRunning())
            return false;

        if (audio.isBatchActive())
        {
            views.statusBar.setMessage ("Sync is off while the batch runs", Tone::warning);
            return false;
        }

        if (audio.isAuditioning())
        {
            views.statusBar.setMessage ("Stop audition before measuring", Tone::warning);
            return false;
        }

        if (! audio.checkCanRecord ("sync"))
            return false;

        const auto status = audio.getDeviceStatus();
        measuringKey = engine::SyncKey::from (status);

        engine::SyncOptions options;
        options.levelDb = (float) views.sync.getLevelSlider().getValue();
        options.latencyHint = status.inputLatencySamples + status.outputLatencySamples;

        audio.setSyncActive (true);
        views.topBar.setStartAllowed (false, "Start is off while Sync measures.");
        views.sync.setMeasuring (true);
        views.sync.setProgress (0, options.repeats, 0.0);
        lastRepeatsReported = 0;

        if (const auto error = measurer.start (options, status.config.sampleRate, audio.getEngine()); error.isNotEmpty())
        {
            unlock();
            views.sync.setMeasuring (false);
            views.statusBar.setMessage ("Cannot sync: " + error, Tone::warning);
            return false;
        }

        const auto where = audio.describeKey (measuringKey);
        juce::Logger::writeToLog ("Sync: measuring " + where + " at " + dbText (options.levelDb));
        views.statusBar.setMessage ("Sync: measuring the round trip of " + where + " at " + dbText (options.levelDb)
                                        + dot() + "repeat 1 of " + juce::String (options.repeats) + ellipsis());
        refreshControls();
        return true;
    }

    void SyncController::cancel()
    {
        if (measurer.isRunning())
        {
            measurer.cancel();
            finished();
        }
    }

    void SyncController::unlock()
    {
        audio.setSyncActive (false);
        views.topBar.setStartAllowed (true);
        refreshControls();
    }

    //==============================================================================
    void SyncController::handleSnapshot (const engine::EngineSnapshot& snap)
    {
        if (measurer.isRunning())
        {
            const auto phase = measurer.update (snap);
            const auto done = measurer.getRepeatsDone();
            const auto total = measurer.getRepeatsRequested();

            views.sync.setProgress (done, total, measurer.getProgress() * total - done);

            if (phase == engine::SyncMeasurer::Phase::done)
            {
                finished();
            }
            else if (done != lastRepeatsReported)
            {
                lastRepeatsReported = done;
                views.statusBar.setMessage ("Sync: measuring the round trip of " + audio.describeKey (measuringKey) + dot()
                                            + "repeat " + juce::String (juce::jmin (done + 1, total)) + " of "
                                            + juce::String (total) + ellipsis());
            }
        }

        refreshControls();
    }

    void SyncController::refreshControls()
    {
        const auto measuring = measurer.isRunning();
        const auto reason = measuring               ? juce::String()
                          : audio.isBatchActive()   ? juce::String ("Sync is off while the batch runs.")
                          : audio.isAuditioning()   ? juce::String ("Sync is off while audition plays; stop it first.")
                                                    : juce::String();

        auto& button = views.sync.getSyncButton();
        button.setEnabled (reason.isEmpty());

        const auto tooltip = measuring ? juce::String ("Stop the measurement (nothing is stored).")
                           : reason.isNotEmpty() ? reason : juce::String (defaultButtonTooltip);

        if (button.getTooltip() != tooltip)
            button.setTooltip (tooltip);

        views.sync.getLevelSlider().setEnabled (! measuring && ! audio.isBatchActive());

        auto& forgetButton = views.sync.getForgetButton();
        const auto canForget = ! measuring && ! audio.isBatchActive() && views.sync.getHasMeasurement();

        if (forgetButton.isEnabled() != canForget)
            forgetButton.setEnabled (canForget);

        const auto forgetTip = canForget ? juce::String ("Delete the stored measurement for the current device configuration "
                                                         "(asks first). Other configurations keep theirs.")
                             : measuring ? juce::String ("Forget is off while Sync measures.")
                             : audio.isBatchActive() ? juce::String ("Forget is off while the batch runs.")
                                                     : juce::String ("Nothing to forget: the current device configuration "
                                                                     "has no stored measurement.");

        if (forgetButton.getTooltip() != forgetTip)
            forgetButton.setTooltip (forgetTip);
    }

    //==============================================================================
    void SyncController::forget()
    {
        if (measurer.isRunning() || audio.isBatchActive() || views.confirm.isShowing())
            return;

        const auto status = audio.getDeviceStatus();
        const auto m = audio.findSync (status);

        if (! m.has_value())
        {
            views.statusBar.setMessage ("Nothing to forget: no measurement is stored for this configuration");
            return;
        }

        const auto key = engine::SyncKey::from (status);
        const auto where = audio.describeKey (key);

        ui::ConfirmDialog::Content c;
        c.title = "Forget the sync measurement?";
        c.intro = "The stored round trip for this device configuration is deleted:";
        c.items = { where, format::latency (m->samples, m->ms) + dot() + "confidence " + engine::toString (m->confidence)
                               + dot() + format::dateTime (m->date) };
        c.note = "Takes in this configuration then use the driver's estimate and are marked NC until you press Sync "
                 "again. Measurements of other configurations are kept.";
        c.confirmText = "Forget";
        c.cancelText = "Cancel";

        views.confirm.show (c, [this, key, where] (bool confirmed)
        {
            if (! confirmed)
            {
                views.statusBar.setMessage ("Kept the measurement for " + where);
                return;
            }

            if (settings.removeSyncMeasurement (key))
            {
                settings.save();
                juce::Logger::writeToLog ("Sync: forgot the measurement for " + where);
                audio.refreshSyncUi();
                views.statusBar.setMessage ("Forgot the measurement for " + where + ". Bypass the amp and press Sync to "
                                            "measure again.", Tone::warning);

                if (onStoreChanged != nullptr)
                    onStoreChanged();
            }

            refreshControls();
        });
    }

    void SyncController::deviceStopped()
    {
        if (! measurer.isRunning())
            return;

        measurer.cancel();
        unlock();
        views.sync.setMeasuring (false);

        const auto where = audio.describeKey (measuringKey);
        const auto text = juce::String ("Sync stopped: the audio device stopped or was disconnected. Nothing was stored.");
        views.sync.setFailure ("Sync failed: the device stopped. Nothing was stored.",
                               "The device of " + where + " stopped during the measurement. It reopens by itself when it is "
                               "back; then press Sync again.",
                               colour::error);
        views.statusBar.setMessage (text + " It reopens by itself when it is back; then press Sync again.", Tone::error);
        juce::Logger::writeToLog ("Sync failed (device stopped) for " + where);

        if (check.active)
        {
            check.allOk = false;
            printCheck ("FAIL: the device stopped during the measurement");
            ++check.index;
            juce::MessageManager::callAsync ([this, flag = alive]
            {
                if (flag->load())
                    checkNext();
            });
        }
    }

    void SyncController::finished()
    {
        const auto r = measurer.getResult();
        const auto where = audio.describeKey (measuringKey);

        for (size_t i = 0; i < r.repeats.size(); ++i)
        {
            const auto& rep = r.repeats[i];
            const auto line = "repeat " + juce::String ((int) i + 1) + ": " + juce::String (rep.delay) + " smp, peak-to-sidelobe "
                            + juce::String (rep.peakToSidelobeDb, 1) + " dB, returned "
                            + dbText (juce::Decibels::gainToDecibels (rep.returnedPeak, -120.0f)) + ", recording peak "
                            + dbText (juce::Decibels::gainToDecibels (rep.recordingPeak, -120.0f)) + " ("
                            + engine::SyncMeasurer::describe (rep.outcome) + (rep.inverted ? ", inverted" : "") + ")";
            juce::Logger::writeToLog ("Sync " + line);

            if (check.active)
                printCheck (line);
        }

        if (r.ok)
        {
            // Stored before unlocking: the unlock refreshes the chip and readouts from the store.
            const auto& m = r.measurement;
            settings.setSyncMeasurement (measuringKey, m);
            settings.save();

            if (onStoreChanged != nullptr)
                onStoreChanged();

            unlock();
            views.sync.setMeasuring (false);
            views.sync.setFailure ({}, {}, colour::warn);
            audio.refreshSyncUi();

            const auto status = audio.getDeviceStatus();
            const auto driverTotal = status.inputLatencySamples + status.outputLatencySamples;
            const auto low = m.confidence == engine::SyncConfidence::low;
            const auto text = juce::String (low ? "Synced with low confidence " : "Synced ") + where + ": "
                            + format::latency (m.samples, m.ms) + dot() + "returned " + dbText (m.returnedPeakDb) + dot()
                            + "confidence " + engine::toString (m.confidence)
                            + (r.inverted ? dot() + "the loop inverts polarity" : juce::String())
                            + (low ? dot() + "consider measuring again" : juce::String());
            const auto detail = juce::String (m.repeatsUsed) + " of " + juce::String (m.repeatsTotal) + " repeats within "
                              + plusMinus() + "1 sample, peak-to-sidelobe ratio " + juce::String (m.peakToSidelobeDb, 1)
                              + " dB. The driver reports " + juce::String (driverTotal) + " smp (difference "
                              + (m.samples - driverTotal > 0 ? "+" : "") + juce::String (m.samples - driverTotal) + " smp).";

            views.statusBar.setMessage (text, low ? Tone::warning : Tone::normal, detail);
            juce::Logger::writeToLog ("Sync: " + text + ". " + detail);

            if (check.active)
            {
                const auto expected = check.expectedRoundTrip != nullptr ? check.expectedRoundTrip() : -1;
                const auto matches = expected < 0 || expected == m.samples;
                check.allOk = check.allOk && matches;

                printCheck ("OK: " + format::latency (m.samples, m.ms) + " at " + format::sampleRate (r.sampleRate) + ", returned "
                            + dbText (m.returnedPeakDb) + ", confidence " + engine::toString (m.confidence) + " ("
                            + juce::String (m.repeatsUsed) + "/" + juce::String (m.repeatsTotal) + " repeats within "
                            + plusMinus() + "1 smp, peak-to-sidelobe " + juce::String (m.peakToSidelobeDb, 1) + " dB)"
                            + (r.inverted ? ", polarity inverted" : "") + "; driver estimate " + juce::String (driverTotal)
                            + " smp; stored for " + measuringKey.typeName + dot() + where);

                if (expected >= 0)
                    printCheck ("expected round trip of the virtual loopback: " + juce::String (expected) + " smp -> "
                                + (matches ? "exact" : "MISMATCH"));
            }
        }
        else
        {
            unlock();
            views.sync.setMeasuring (false);

            const auto cancelled = r.failure == engine::SyncFailure::cancelled;
            const auto severe = r.failure == engine::SyncFailure::silent || r.failure == engine::SyncFailure::noPeak
                             || r.failure == engine::SyncFailure::deviceStopped;

            if (cancelled)
            {
                views.sync.setFailure ({}, {}, colour::warn);
                views.statusBar.setMessage ("Sync cancelled; nothing was stored");
            }
            else
            {
                views.sync.setFailure ("Sync failed: " + r.summary + ". Nothing was stored.", r.message,
                                       severe ? colour::error : colour::warn);
                views.statusBar.setMessage ("Sync failed for " + where + ": " + r.message, severe ? Tone::error : Tone::warning,
                                            r.message);
            }

            juce::Logger::writeToLog ("Sync failed (" + r.summary + "): " + r.message);

            if (check.active)
            {
                check.allOk = false;
                printCheck ("FAIL: " + r.summary + ": " + r.message);
            }
        }

        if (check.active)
        {
            ++check.index;
            juce::MessageManager::callAsync ([this, flag = alive]
            {
                if (flag->load())
                    checkNext();
            });
        }
    }

    //==============================================================================
    void SyncController::runCheck (juce::Array<double> extraRates, std::function<int()> expectedRoundTrip,
                                   std::function<void (bool)> onDone)
    {
        const auto status = audio.getDeviceStatus();

        check = {};
        check.active = true;
        check.expectedRoundTrip = std::move (expectedRoundTrip);
        check.onDone = std::move (onDone);
        check.startConfig = audio.getSelectedConfig();
        check.startConfig.sampleRate = status.config.sampleRate;
        check.startConfig.bufferSize = status.config.bufferSize;
        check.rates.add (status.config.sampleRate);

        for (auto r : extraRates)
            if (! check.rates.contains (r))
                check.rates.add (r);

        printCheck ("device: " + audio.describeDevice (status) + " | output " + audio.getSelectedConfig().outputChannelName
                    + " | input " + audio.getSelectedConfig().inputChannelName + " | sync level "
                    + dbText ((float) views.sync.getLevelSlider().getValue()) + " | driver in "
                    + juce::String (status.inputLatencySamples) + " + out " + juce::String (status.outputLatencySamples) + " smp");

        checkNext();
    }

    void SyncController::checkNext()
    {
        if (! check.active)
            return;

        if (check.index >= check.rates.size())
        {
            audio.restoreConfig (check.startConfig);
            check.active = false;
            printCheck (check.allOk ? "PASS" : "FAIL");

            if (auto done = std::move (check.onDone); done != nullptr)
                done (check.allOk);

            return;
        }

        const auto rate = check.rates[check.index];
        auto status = audio.getDeviceStatus();

        if (status.isOpen && ! juce::approximatelyEqual (status.config.sampleRate, rate))
        {
            if (! status.sampleRates.contains (rate))
            {
                printCheck ("FAIL: the device does not offer " + format::sampleRate (rate));
                check.allOk = false;
                ++check.index;
                checkNext();
                return;
            }

            printCheck ("switching the device to " + format::sampleRate (rate));
            audio.switchSampleRate (rate);
        }

        printCheck ("measuring " + audio.describeKey (engine::SyncKey::from (audio.getDeviceStatus())) + ellipsis());

        if (! start())
        {
            printCheck ("FAIL: Sync did not start: " + views.statusBar.getMessage());
            check.allOk = false;
            ++check.index;
            juce::MessageManager::callAsync ([this, flag = alive]
            {
                if (flag->load())
                    checkNext();
            });
        }
    }

    void SyncController::printCheck (const juce::String& text) const
    {
        std::fprintf (stderr, "[sync-check] %s\n", text.toRawUTF8());
        std::fflush (stderr);
    }
}
