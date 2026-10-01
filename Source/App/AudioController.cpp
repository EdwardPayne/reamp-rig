#include "AudioController.h"
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
        constexpr int uiRefreshHz = 30;

        juce::String dot()      { return ui::utf8 (" \xc2\xb7 "); }
        juce::String emDash()   { return ui::utf8 ("\xe2\x80\x94"); }
        juce::String ellipsis() { return ui::utf8 ("\xe2\x80\xa6"); }

        juce::String formatDb (float db)
        {
            return (db > 0.0f ? "+" : "") + juce::String (db, 1);
        }

        juce::String channelLetter (int channel)
        {
            return channel == 0 ? "L" : channel == 1 ? "R" : "ch " + juce::String (channel + 1);
        }

        /** Replaces a combo box's items only when they changed (keeps an open popup stable)
            and selects `index` without sending a change notification. */
        void setItems (juce::ComboBox& box, const juce::StringArray& items, int index, const juce::String& emptyText)
        {
            juce::StringArray current;

            for (int i = 0; i < box.getNumItems(); ++i)
                current.add (box.getItemText (i));

            if (items.isEmpty())
            {
                if (current != juce::StringArray (emptyText))
                {
                    box.clear (juce::dontSendNotification);
                    box.addItem (emptyText, 1);
                }

                box.setSelectedItemIndex (0, juce::dontSendNotification);
                box.setEnabled (false);
                return;
            }

            if (current != items)
            {
                box.clear (juce::dontSendNotification);
                box.addItemList (items, 1);
            }

            box.setEnabled (true);

            if (juce::isPositiveAndBelow (index, items.size()))
                box.setSelectedItemIndex (index, juce::dontSendNotification);
            else
                box.setSelectedId (0, juce::dontSendNotification);
        }

        void logLine (const juce::String& text)
        {
            std::fprintf (stderr, "%s\n", text.toRawUTF8());
            std::fflush (stderr);
        }
    }

    //==============================================================================
    AudioController::AudioController (Settings& s, engine::AudioDeviceInterface& d, Views v, bool needsMicrophonePermission)
        : settings (s), device (d), views (v), needsPermission (needsMicrophonePermission)
    {
        const auto gain = settings.getOutputGainDb();
        views.audio.getOutputLevel().setValue (gain, juce::dontSendNotification);
        duplex.setGainDb (gain);

        device.addListener (this);
        device.setCallback (&duplex);

        wireControls();
        refreshDeviceUi();
        refreshPeakReadout();

        startTimerHz (uiRefreshHz);
    }

    AudioController::~AudioController()
    {
        stopTimer();
        alive->store (false);

        auto& a = views.audio;

        for (auto* box : { &a.getTypeBox(), &a.getOutputDeviceBox(), &a.getInputDeviceBox(), &a.getSampleRateBox(),
                           &a.getBufferSizeBox(), &a.getOutputChannelBox(), &a.getInputChannelBox() })
            box->onChange = nullptr;

        a.getOutputLevel().onValueChange = nullptr;
        a.getAuditionButton().onClick = nullptr;

        duplex.stopAudition();
        device.setCallback (nullptr);   // no audio callback runs after this
        device.removeListener (this);
        duplex.poll();                  // frees retired commands
    }

    //==============================================================================
    void AudioController::wireControls()
    {
        auto& a = views.audio;

        a.getTypeBox().onChange = [this]
        {
            const auto type = views.audio.getTypeBox().getText();
            userChangedConfig ([type] (engine::DeviceConfig& c)
            {
                c.typeName = type;
                c.inputDevice = c.outputDevice = {};   // the new driver's defaults
            });
        };

        a.getOutputDeviceBox().onChange = [this]
        {
            const auto name = views.audio.getOutputDeviceBox().getText();
            const auto inputs = device.getDeviceNames (selected.typeName, true);

            userChangedConfig ([&] (engine::DeviceConfig& c)
            {
                // Keep one duplex device when possible: the input follows the output if the
                // two were the same device and the new one has inputs too.
                const auto wasSingleDevice = c.inputDevice == c.outputDevice;
                c.outputDevice = name;

                if ((wasSingleDevice && inputs.contains (name)) || ! device.hasSeparateInputsAndOutputs (c.typeName))
                    c.inputDevice = name;
            });
        };

        a.getInputDeviceBox().onChange = [this]
        {
            const auto name = views.audio.getInputDeviceBox().getText();
            userChangedConfig ([&] (engine::DeviceConfig& c) { c.inputDevice = name; });
        };

        a.getSampleRateBox().onChange = [this]
        {
            const auto rates = device.getStatus().sampleRates;
            const auto index = views.audio.getSampleRateBox().getSelectedItemIndex();

            if (juce::isPositiveAndBelow (index, rates.size()))
                userChangedConfig ([&] (engine::DeviceConfig& c) { c.sampleRate = rates[index]; });
        };

        a.getBufferSizeBox().onChange = [this]
        {
            const auto sizes = device.getStatus().bufferSizes;
            const auto index = views.audio.getBufferSizeBox().getSelectedItemIndex();

            if (juce::isPositiveAndBelow (index, sizes.size()))
                userChangedConfig ([&] (engine::DeviceConfig& c) { c.bufferSize = sizes[index]; });
        };

        a.getOutputChannelBox().onChange = [this]
        {
            const auto& box = views.audio.getOutputChannelBox();
            const auto index = box.getSelectedItemIndex();
            const auto name = box.getText();
            userChangedConfig ([&] (engine::DeviceConfig& c) { c.outputChannel = index; c.outputChannelName = name; });
        };

        a.getInputChannelBox().onChange = [this]
        {
            const auto& box = views.audio.getInputChannelBox();
            const auto index = box.getSelectedItemIndex();
            const auto name = box.getText();
            userChangedConfig ([&] (engine::DeviceConfig& c) { c.inputChannel = index; c.inputChannelName = name; });
        };

        a.getOutputLevel().onValueChange = [this]
        {
            const auto db = (float) views.audio.getOutputLevel().getValue();
            duplex.setGainDb (db);
            settings.setOutputGainDb (db);
            refreshPeakReadout();
        };

        a.getAuditionButton().onClick = [this] { toggleAudition(); };
    }

    void AudioController::userChangedConfig (const std::function<void (engine::DeviceConfig&)>& change)
    {
        auto wanted = selected;
        change (wanted);
        applyConfig (wanted, true);

        // The user's choice is the device to come back to.
        preferred = device.getStatus().isOpen ? selected : wanted;
        reconnect.reset (session.isPresent (preferred));
    }

    //==============================================================================
    void AudioController::openInitialDevice (const LaunchOptions& options)
    {
        auto wanted = settings.getDeviceConfig();

        for (const auto& type : device.getTypeNames())
            juce::Logger::writeToLog ("Audio devices (" + type + "): outputs ["
                                      + device.getDeviceNames (type, false).joinIntoString (", ") + "], inputs ["
                                      + device.getDeviceNames (type, true).joinIntoString (", ") + "]");

        if (options.deviceType.has_value())     wanted.typeName = *options.deviceType;
        if (options.outputDevice.has_value())   wanted.outputDevice = *options.outputDevice;
        if (options.inputDevice.has_value())    wanted.inputDevice = *options.inputDevice;
        if (options.sampleRate.has_value())     wanted.sampleRate = *options.sampleRate;
        if (options.bufferSize.has_value())     wanted.bufferSize = *options.bufferSize;

        // A channel flag is a 1-based number or the driver's channel name.
        auto channelOverride = [] (const juce::String& value, int& index, juce::String& name)
        {
            if (value.containsOnly ("0123456789") && value.isNotEmpty())
            {
                index = value.getIntValue() - 1;
                name = {};
            }
            else
            {
                index = -1;
                name = value;
            }
        };

        if (options.outputChannel.has_value())
            channelOverride (*options.outputChannel, wanted.outputChannel, wanted.outputChannelName);

        if (options.inputChannel.has_value())
            channelOverride (*options.inputChannel, wanted.inputChannel, wanted.inputChannelName);

        outputOnly = options.noInput;

        if (options.outputLevelDb.has_value())
        {
            const auto db = juce::jlimit (Settings::minOutputGainDb, Settings::maxOutputGainDb, *options.outputLevelDb);
            views.audio.getOutputLevel().setValue (db, juce::dontSendNotification);
            duplex.setGainDb (db);
            refreshPeakReadout();
        }

        // Never persist at startup: a fallback (saved device missing) must not overwrite the
        // user's choice, so the device is used again as soon as it is back.
        preferred = wanted;
        reconnect.reset (session.isPresent (preferred));
        applyConfig (wanted, false);

        // First launch (nothing saved): come back to whatever was chosen for the user.
        if (preferred.outputDevice.isEmpty() && preferred.inputDevice.isEmpty() && device.getStatus().isOpen)
        {
            preferred = selected;
            reconnect.reset (true);
        }

        if (! reconnect.isPending() && ! session.isPresent (preferred) && preferred.outputDevice.isNotEmpty())
            juce::Logger::writeToLog ("Audio: " + preferred.outputDevice + " is not connected; it is opened by itself once it is");
    }

    void AudioController::applyConfig (const engine::DeviceConfig& wanted, bool persist)
    {
        const auto access = needsPermission ? getMicrophoneAccess() : MicrophoneAccess::granted;

        if (access == MicrophoneAccess::undetermined)
        {
            // JUCE opens every CoreAudio device through an aggregate that carries all of the
            // device's streams, so even an output-only open of a device that has inputs makes
            // coreaudiod wait for the microphone prompt, blocking this thread. Ask first
            // (without blocking) and open once the user has answered.
            juce::StringArray ignored;
            const auto resolved = session.resolveDevices (wanted, ignored);
            const auto outputHasInputs = device.getDeviceNames (resolved.typeName, true).contains (resolved.outputDevice);

            if (! outputOnly || outputHasInputs)
            {
                stopAudition();

                if (device.getStatus().isOpen)
                {
                    applying = true;
                    device.close();
                    applying = false;
                }

                waitingForPermission = true;
                pendingConfig = wanted;
                pendingPersist = pendingPersist || persist;
                selected = resolved;
                inputBlockedByPermission = true;
                refreshDeviceUi();

                views.statusBar.setMessage ("Waiting for microphone access: answer the macOS prompt to open "
                                                + (resolved.outputDevice.isNotEmpty() ? resolved.outputDevice : juce::String ("the audio device")),
                                            Tone::warning,
                                            "macOS asks once whether Reamp Rig may use the microphone (needed to record "
                                            "any audio input). The device opens as soon as the prompt is answered.");
                requestMicrophoneIfNeeded();
                return;
            }
        }

        waitingForPermission = false;
        pendingPersist = false;

        applying = true;
        const auto result = session.open (wanted, access == MicrophoneAccess::granted && ! outputOnly);
        applying = false;

        selected = result.config;
        inputBlockedByPermission = result.ok && selected.inputDevice.isNotEmpty() && ! result.inputOpen;

        if (result.ok && persist)
            settings.setDeviceConfig (selected);

        refreshDeviceUi();

        for (const auto& w : result.warnings)
            juce::Logger::writeToLog ("Audio: " + w);

        if (! result.ok)
        {
            juce::Logger::writeToLog ("Audio device error: " + result.error);
            stopAudition();
            views.statusBar.setMessage ("Audio device not opened: " + result.error.upToFirstOccurrenceOf ("\n", false, false),
                                        Tone::error, result.error);
        }
        else if (inputBlockedByPermission && access == MicrophoneAccess::denied && ! outputOnly)
        {
            showMicrophoneDenied();
        }
        else if (! result.warnings.isEmpty())
        {
            views.statusBar.setMessage (result.warnings.joinIntoString (". "), Tone::warning,
                                        result.warnings.joinIntoString ("\n"));
        }
        else if (persist)
        {
            views.statusBar.setMessage ("Audio device: " + describeDevice (device.getStatus()));
        }

        // The preview must be at the device rate: reload the lead after a rate change.
        const auto status = device.getStatus();

        if (! batchActive && ! syncActive && leadFile != juce::File() && status.isOpen
            && ! juce::approximatelyEqual (loadedRequest.targetSampleRate, status.config.sampleRate))
        {
            if (duplex.isAuditioning())
                stopAudition();

            requestSourceLoad();
        }
    }

    void AudioController::requestMicrophoneIfNeeded()
    {
        if (micRequestInFlight)
            return;

        micRequestInFlight = true;
        juce::Logger::writeToLog ("Requesting microphone access");

        requestMicrophoneAccess ([this, flag = alive] (bool granted)
        {
            if (! flag->load())
                return;

            micRequestInFlight = false;
            juce::Logger::writeToLog (granted ? "Microphone access granted" : "Microphone access denied");

            // Either answer lets the device open now (denied: without input).
            applyConfig (waitingForPermission ? pendingConfig : selected, waitingForPermission && pendingPersist);

            if (granted && views.statusBar.getTone() == Tone::normal)
                views.statusBar.setMessage ("Microphone access granted; input is on");
        });
    }

    void AudioController::showMicrophoneDenied()
    {
        views.statusBar.setMessage ("Microphone access denied, so the input is off. Allow Reamp Rig in System Settings > "
                                    "Privacy & Security > Microphone, then restart the app.",
                                    Tone::error,
                                    "macOS gives an app silence on every audio input, including audio interfaces, until "
                                    "it is allowed to use the microphone. Playback and audition still work.");
    }

    //==============================================================================
    juce::String AudioController::describeDevice (const engine::DeviceStatus& status) const
    {
        if (! status.isOpen)
            return "No device";

        const auto& c = selected;
        const auto names = c.isSplit() ? "Out: " + c.outputDevice + dot() + "In: " + c.inputDevice
                                       : (c.outputDevice.isNotEmpty() ? c.outputDevice : c.inputDevice);

        return names + dot() + format::sampleRate (status.config.sampleRate) + dot() + juce::String (status.config.bufferSize);
    }

    void AudioController::refreshDeviceUi()
    {
        const auto status = device.getStatus();
        auto& a = views.audio;

        const auto types = device.getTypeNames();
        setItems (a.getTypeBox(), types, types.indexOf (selected.typeName), "No driver");

        const auto outputs = device.getDeviceNames (selected.typeName, false);
        setItems (a.getOutputDeviceBox(), outputs, outputs.indexOf (selected.outputDevice), "No device");

        const auto inputs = device.getDeviceNames (selected.typeName, true);
        setItems (a.getInputDeviceBox(), inputs, inputs.indexOf (selected.inputDevice), "No device");

        if (! device.hasSeparateInputsAndOutputs (selected.typeName))
            a.getInputDeviceBox().setEnabled (false);

        juce::StringArray rates, sizes;

        for (auto r : status.sampleRates)
            rates.add (format::sampleRate (r));

        for (auto b : status.bufferSizes)
            sizes.add (juce::String (b));

        setItems (a.getSampleRateBox(), rates, status.sampleRates.indexOf (status.config.sampleRate), emDash());
        setItems (a.getBufferSizeBox(), sizes, status.bufferSizes.indexOf (status.config.bufferSize), emDash());
        setItems (a.getOutputChannelBox(), status.outputChannelNames, selected.outputChannel, emDash());
        setItems (a.getInputChannelBox(), status.inputChannelNames, selected.inputChannel,
                  outputOnly ? juce::String ("Input off")
                             : inputBlockedByPermission ? juce::String ("No mic access") : emDash());

        a.getOutputDeviceBox().setTooltip ("Device whose output feeds the amp: " + selected.outputDevice);
        a.getInputDeviceBox().setTooltip ("Device whose input records the amp: " + selected.inputDevice
                                          + ". Use the same device as the output for sample-accurate results.");
        a.getOutputChannelBox().setTooltip ("Output channel that feeds the amp"
                                            + (selected.outputChannelName.isNotEmpty() ? ": " + selected.outputChannelName : juce::String()));
        a.getInputChannelBox().setTooltip ("Input channel that records the amp"
                                           + (selected.inputChannelName.isNotEmpty() ? ": " + selected.inputChannelName : juce::String())
                                           + (outputOnly ? " (off: --no-input)"
                                                         : inputBlockedByPermission ? " (off: no microphone access)" : juce::String()));

        a.setSplitDevicesNoticeVisible (selected.isSplit());

        const auto summary = waitingForPermission ? juce::String ("Waiting for microphone access") : describeDevice (status);
        views.topBar.setDeviceSummary (summary, status.isOpen ? summary : "No audio device is open.");

        // Any device, rate or buffer change re-checks the stored sync value (4.4).
        refreshSyncUi (status);

        if (! status.isOpen)
        {
            a.getOutputMeter().reset();
            a.getInputMeter().reset();
        }

        deviceWasOpen = status.isOpen;
        applyBatchLock();
    }

    void AudioController::audioDeviceChanged()
    {
        if (applying)
            return;   // applyConfig refreshes once it is done

        const auto status = device.getStatus();

        if (deviceWasOpen && ! status.isOpen)
        {
            deviceLost();
            return;
        }

        refreshDeviceUi();
        checkReconnect();
    }

    void AudioController::deviceLost()
    {
        const auto status = device.getStatus();
        const auto name = selected.outputDevice.isNotEmpty() ? selected.outputDevice : selected.inputDevice;

        stopAudition();

        const auto reason = status.lastError.isNotEmpty() ? status.lastError : juce::String ("disconnected or stopped");
        juce::Logger::writeToLog ("Audio device stopped: " + name + ": " + reason);
        views.statusBar.setMessage ("Audio device stopped: " + name + " (" + reason + "). It reopens by itself when it is "
                                        "back; or pick another device in AUDIO.",
                                    Tone::warning,
                                    "Check the cable and the interface's power. Nothing has to be restarted: as soon as "
                                    "the device is listed again it is opened with the same settings.");
        refreshDeviceUi();

        // The batch pauses (its message replaces this one) and Sync stops; both say what to do.
        if (onDeviceStopped != nullptr)
            onDeviceStopped();

        if (onSyncDeviceStopped != nullptr)
            onSyncDeviceStopped();

        checkReconnect();
    }

    void AudioController::checkReconnect()
    {
        if (applying || waitingForPermission)
            return;

        const auto present = session.isPresent (preferred);
        const auto running = engine::DeviceSession::runs (device.getStatus(), preferred);
        const auto canReopen = ! syncActive && ! (isBatchRunning != nullptr && isBatchRunning());

        if (! reconnect.update (present, running, canReopen))
            return;

        const auto name = preferred.outputDevice.isNotEmpty() ? preferred.outputDevice : preferred.inputDevice;
        juce::Logger::writeToLog ("Audio: " + name + " is back; reopening it");
        applyConfig (preferred, false);

        const auto now = device.getStatus();

        if (! now.isOpen)
            return;   // applyConfig has said why

        if (views.statusBar.getTone() != Tone::error)
            views.statusBar.setMessage (name + " is back and open again: " + describeDevice (now));

        if (onDeviceReopened != nullptr)
            onDeviceReopened (describeDevice (now));
    }

    //==============================================================================
    void AudioController::setLead (const juce::File& file, int channel)
    {
        if (file == leadFile && channel == leadChannel)
            return;

        if (isAuditioning())
        {
            stopAudition();
            views.statusBar.setMessage ("Audition stopped");
        }

        leadFile = file;
        leadChannel = channel;

        if (batchActive || syncActive)
        {
            loader.cancel();
            loaded.reset();
            loadedRequest = {};
            return;   // loaded when the batch ends
        }

        if (file == juce::File())
        {
            loader.cancel();
            loaded.reset();
            loadedRequest = {};
            refreshPeakReadout();
            return;
        }

        requestSourceLoad();
    }

    void AudioController::requestSourceLoad()
    {
        const auto status = device.getStatus();

        loadedRequest = { leadFile, leadChannel, status.isOpen ? status.config.sampleRate : 0.0 };
        loaded.reset();
        refreshPeakReadout();

        loader.loadAsync (loadedRequest, [this] (std::shared_ptr<const engine::LoadedSource> source, juce::String error)
        {
            sourceLoaded (std::move (source), error);
        });
    }

    void AudioController::sourceLoaded (std::shared_ptr<const engine::LoadedSource> source, const juce::String& error)
    {
        if (source == nullptr)
        {
            views.statusBar.setMessage ("Could not load " + loadedRequest.file.getFileName() + " for audition: " + error,
                                        Tone::warning);
            auditionPending = false;
            views.audio.setAuditioning (false);
            refreshPeakReadout();
            return;
        }

        loaded = std::move (source);
        refreshPeakReadout();

        if (auditionPending)
        {
            auditionPending = false;
            startAudition();
        }
    }

    void AudioController::refreshPeakReadout()
    {
        auto& readout = views.audio.getPeakReadout();
        const auto gainDb = duplex.getGainDb();

        if (leadFile == juce::File())
        {
            readout.setValue (emDash());
            readout.setTooltip ("Peak of the selected file's played channel with the output level applied.");
            return;
        }

        if (loaded == nullptr)
        {
            readout.setValue ("measuring" + ellipsis(), colour::muted);
            return;
        }

        if (loaded->peak <= 0.0f)
        {
            readout.setValue ("silent", colour::muted);
            return;
        }

        const auto fileDb = juce::Decibels::gainToDecibels (loaded->peak);
        const auto outDb = fileDb + gainDb;
        const auto clips = outDb > 0.0f;

        readout.setValue (formatDb (outDb) + " dBFS" + (clips ? dot() + "clips" : juce::String()),
                          clips ? std::optional<juce::Colour> (colour::warn) : std::nullopt);
        readout.setTooltip ("Peak of " + loaded->file.getFileName() + " (" + channelLetter (loaded->channel) + ") is "
                            + formatDb (fileDb) + " dBFS; with the output level of " + formatDb (gainDb)
                            + " dB it reaches " + formatDb (outDb) + " dBFS at the output.");
    }

    //==============================================================================
    void AudioController::toggleAudition()
    {
        if (batchActive || syncActive)
        {
            views.statusBar.setMessage (batchActive ? "Audition is off while the batch runs" : "Audition is off while Sync measures",
                                        Tone::warning);
            return;
        }

        if (isAuditioning())
        {
            stopAudition();
            views.statusBar.setMessage ("Audition stopped");
        }
        else
        {
            startAudition();
        }
    }

    bool AudioController::startAudition()
    {
        if (leadFile == juce::File())
        {
            views.statusBar.setMessage ("Select a file to audition", Tone::warning);
            return false;
        }

        const auto status = device.getStatus();

        if (! status.isOpen || selected.outputChannel < 0)
        {
            views.statusBar.setMessage ("Cannot audition: no output channel is open. Pick an output device and channel in AUDIO.",
                                        Tone::warning);
            return false;
        }

        if (loaded == nullptr || ! juce::approximatelyEqual (loaded->sampleRate, status.config.sampleRate))
        {
            if (! loader.isLoading() || ! juce::approximatelyEqual (loadedRequest.targetSampleRate, status.config.sampleRate))
                requestSourceLoad();

            auditionPending = true;
            views.audio.setAuditioning (true);
            views.statusBar.setMessage ("Loading " + leadFile.getFileName() + " for audition" + ellipsis());
            return true;
        }

        auto start = (juce::int64) std::llround (views.waveform.getAuditionStart() * loaded->sampleRate);

        if (start >= loaded->getNumSamples())
            start = 0;

        if (! duplex.startAudition (loaded, start))
        {
            views.statusBar.setMessage ("Cannot audition " + leadFile.getFileName(), Tone::warning);
            return false;
        }

        views.audio.setAuditioning (true);

        auto message = "Auditioning " + leadFile.getFileName() + " (" + channelLetter (loaded->channel) + ") from "
                     + format::time ((double) start / loaded->sampleRate) + " at " + formatDb (duplex.getGainDb())
                     + " dB on " + selected.outputChannelName;

        if (loaded->resampled)
            message << " (resampled from " << format::sampleRate (loaded->fileSampleRate) << ")";

        views.statusBar.setMessage (message);
        return true;
    }

    void AudioController::stopAudition()
    {
        duplex.stopAudition();
        auditionPending = false;
        views.audio.setAuditioning (false);
        views.waveform.setPlayhead (std::nullopt);
    }

    bool AudioController::isBusy() const
    {
        if (loader.isLoading() || auditionPending)
            return true;

        if (check.active)
            return check.startedMs <= 0.0 || juce::Time::getMillisecondCounterHiRes() - check.startedMs < 1000.0;

        return false;
    }

    //==============================================================================
    void AudioController::timerCallback()
    {
        // Slow poll (every 2 s) in case a device-list notification was missed: a device that
        // vanished, or the saved device that came back.
        if (++slowTicks >= 2 * uiRefreshHz)
        {
            slowTicks = 0;

            if (! applying && deviceWasOpen && ! device.getStatus().isOpen)
                deviceLost();
            else
                checkReconnect();
        }

        const auto snap = duplex.poll();

        views.audio.getOutputMeter().push (snap.outputPeak, snap.outputClipped);
        views.audio.getInputMeter().push (snap.inputPeak, snap.inputClipped);

        if (snap.auditioning && snap.sourceSampleRate > 0.0)
            views.waveform.setPlayhead ((double) snap.playheadSample / snap.sourceSampleRate);

        updateAuditionCheck (snap);

        if (onSyncSnapshot != nullptr)
            onSyncSnapshot (snap);

        if (snap.auditionFinished)
        {
            stopAudition();
            views.statusBar.setMessage ("Audition finished");
        }

        if (onSnapshot != nullptr)
            onSnapshot (snap);
    }

    //==============================================================================
    std::optional<engine::SyncMeasurement> AudioController::findSync (const engine::DeviceStatus& status) const
    {
        if (! status.isOpen)
            return std::nullopt;

        return findSync (engine::SyncKey::from (status));
    }

    std::optional<engine::SyncMeasurement> AudioController::findSync (const engine::SyncKey& key) const
    {
        return settings.getSyncMeasurement (key);
    }

    juce::String AudioController::describeKey (const engine::SyncKey& k) const
    {
        const auto split = k.inputDevice.isNotEmpty() && k.outputDevice.isNotEmpty() && k.inputDevice != k.outputDevice;
        const auto names = split ? "Out: " + k.outputDevice + dot() + "In: " + k.inputDevice
                                 : (k.outputDevice.isNotEmpty() ? k.outputDevice : k.inputDevice);

        return names + dot() + format::sampleRate (k.sampleRate) + dot() + juce::String (k.bufferSize);
    }

    void AudioController::refreshSyncUi()
    {
        refreshSyncUi (device.getStatus());
    }

    void AudioController::refreshSyncUi (const engine::DeviceStatus& status)
    {
        auto& s = views.sync;
        auto& measured = s.getMeasuredReadout();
        auto& peak = s.getPeakReadout();
        auto& confidence = s.getConfidenceReadout();
        auto& date = s.getDateReadout();
        auto& driver = s.getDriverReadout();

        if (! status.isOpen)
        {
            views.topBar.setSyncStatus ("No device", colour::muted, "No audio device is open.");
            s.setHasMeasurement (false);

            for (auto* r : { &measured, &peak, &confidence, &date, &driver })
                r->setValue (emDash());

            if (onSyncUiRefreshed != nullptr)
                onSyncUiRefreshed();

            return;
        }

        const auto where = describeKey (engine::SyncKey::from (status));
        const auto driverTotal = status.inputLatencySamples + status.outputLatencySamples;
        const auto rate = juce::jmax (1.0, status.config.sampleRate);
        const auto driverText = "in " + juce::String (status.inputLatencySamples) + " + out "
                              + juce::String (status.outputLatencySamples) + " = " + juce::String (driverTotal) + " smp ("
                              + format::milliseconds (driverTotal * 1000.0 / rate) + ")";

        driver.setValue ("in " + juce::String (status.inputLatencySamples) + " + out " + juce::String (status.outputLatencySamples) + " smp");

        const auto stored = findSync (status);
        s.setHasMeasurement (stored.has_value());

        if (const auto& m = stored)
        {
            const auto value = format::latency (m->samples, m->ms);
            const auto difference = m->samples - driverTotal;
            const auto conf = engine::toString (m->confidence);
            const auto summary = "Measured round trip for " + where + ": " + value + ", returned peak "
                               + juce::String (m->returnedPeakDb, 1) + " dBFS, confidence " + conf + ", measured on "
                               + format::dateTime (m->date) + ".";

            views.topBar.setSyncStatus (value, colour::ok,
                                        summary + " Takes in this configuration discard exactly " + juce::String (m->samples)
                                            + " samples. The driver reports " + driverText + ".");

            measured.setValue (value, colour::ok);
            measured.setTooltip (summary);
            peak.setValue (juce::String (m->returnedPeakDb, 1) + " dBFS");
            confidence.setValue (conf, m->confidence == engine::SyncConfidence::high ? std::optional<juce::Colour> (colour::ok)
                                     : m->confidence == engine::SyncConfidence::low  ? std::optional<juce::Colour> (colour::warn)
                                                                                      : std::nullopt);
            confidence.setTooltip ("Peak-to-sidelobe ratio " + juce::String (m->peakToSidelobeDb, 1) + " dB; "
                                   + juce::String (m->repeatsUsed) + " of " + juce::String (m->repeatsTotal)
                                   + ui::utf8 (" repeats within \xc2\xb1") + "1 sample. High: every repeat agrees and the ratio is at "
                                     "least 20 dB. Medium: all but one agree and at least 12 dB. Low: otherwise.");
            date.setValue (format::dateTime (m->date));
            driver.setTooltip ("Input + output latency reported by the driver: " + driverText + ". Reference only: the "
                               "measured " + juce::String (m->samples) + " smp is used (difference "
                               + (difference > 0 ? "+" : "") + juce::String (difference) + " smp).");
        }
        else
        {
            views.topBar.setSyncStatus ("Not synced", colour::warn,
                                        "No sync measurement for " + where + ". Takes would use the driver's estimate ("
                                            + driverText + ") and be marked NC (not calibrated). Bypass the amp, connect the "
                                              "output to the input and press Sync.");

            measured.setValue ("not synced", colour::warn);
            measured.setTooltip ("No measurement stored for " + where + ".");
            confidence.setTooltip ("How clearly and how repeatably the test signal was found.");

            for (auto* r : { &peak, &confidence, &date })
                r->setValue (emDash());

            driver.setTooltip ("Input + output latency reported by the driver: " + driverText
                               + ". Used as an estimate until this configuration is measured.");
        }

        if (onSyncUiRefreshed != nullptr)
            onSyncUiRefreshed();
    }

    //==============================================================================
    bool AudioController::checkCanRecord (const juce::String& action)
    {
        const auto status = device.getStatus();
        auto refuse = [this, action] (const juce::String& why)
        {
            views.statusBar.setMessage ("Cannot " + action + ": " + why, Tone::warning);
            return false;
        };

        if (waitingForPermission)
            return refuse ("waiting for microphone access (answer the macOS prompt)");

        if (! status.isOpen)
            return refuse ("no audio device is open. Connect the interface (it opens by itself) or pick a device in AUDIO.");

        if (selected.outputChannel < 0 || status.config.outputChannel < 0)
            return refuse ("the output device has no output channel open. Pick an output device and channel in AUDIO.");

        if (outputOnly)
            return refuse ("the input is off (--no-input)");

        if (inputBlockedByPermission)
        {
            showMicrophoneDenied();
            return false;
        }

        if (status.config.inputChannel < 0)
            return refuse ("the input device has no input channel open. Pick an input device and channel in AUDIO.");

        return true;
    }

    double AudioController::switchSampleRate (double rate)
    {
        auto wanted = selected;
        wanted.sampleRate = rate;
        applyConfig (wanted, false);
        return device.getStatus().config.sampleRate;
    }

    void AudioController::restoreConfig (const engine::DeviceConfig& config)
    {
        const auto status = device.getStatus();

        if (! status.isOpen || ! juce::approximatelyEqual (status.config.sampleRate, config.sampleRate)
            || status.config.bufferSize != config.bufferSize)
            applyConfig (config, false);
    }

    void AudioController::setBatchActive (bool active)
    {
        setLocks (active, syncActive);
    }

    void AudioController::setSyncActive (bool active)
    {
        setLocks (batchActive, active);
    }

    void AudioController::setLocks (bool batch, bool sync)
    {
        const auto wasLocked = batchActive || syncActive;

        batchActive = batch;
        syncActive = sync;

        const auto locked = batchActive || syncActive;

        if (locked && ! wasLocked)
            stopAudition();

        applyBatchLock();

        if (wasLocked && ! locked)
        {
            views.audio.getOutputLevel().setEnabled (true);
            views.audio.getAuditionButton().setEnabled (true);
            refreshDeviceUi();   // restores the combos' enabled states

            // Reload the lead's audition preview at the (possibly restored) device rate.
            const auto file = leadFile;
            const auto channel = leadChannel;
            leadFile = juce::File();
            setLead (file, channel);
        }
    }

    void AudioController::applyBatchLock()
    {
        if (! batchActive && ! syncActive)
            return;   // refreshDeviceUi has set the normal enabled states

        auto& a = views.audio;

        for (auto* c : std::initializer_list<juce::Component*> { &a.getTypeBox(), &a.getOutputDeviceBox(), &a.getInputDeviceBox(),
                                                                 &a.getSampleRateBox(), &a.getBufferSizeBox(), &a.getOutputChannelBox(),
                                                                 &a.getInputChannelBox(), &a.getOutputLevel(), &a.getAuditionButton() })
            c->setEnabled (false);
    }

    //==============================================================================
    void AudioController::runAuditionCheck (double seconds, std::function<void (bool)> onDone)
    {
        check = {};
        check.active = true;
        check.seconds = seconds;
        check.onDone = std::move (onDone);

        logLine ("[audition-check] device: " + describeDevice (device.getStatus())
                 + " | output channel: " + selected.outputChannelName
                 + " | input channel: " + (selected.inputChannelName.isNotEmpty() ? selected.inputChannelName : juce::String ("none"))
                 + (inputBlockedByPermission ? " (not open: no microphone access)" : "")
                 + " | output level: " + formatDb (duplex.getGainDb()) + " dB");

        if (! startAudition())
        {
            logLine ("[audition-check] FAIL: audition did not start: " + views.statusBar.getMessage());
            check.active = false;

            if (check.onDone != nullptr)
                check.onDone (false);
        }
    }

    void AudioController::updateAuditionCheck (const engine::EngineSnapshot& snap)
    {
        if (! check.active)
            return;

        const auto now = juce::Time::getMillisecondCounterHiRes();

        auto finish = [this] (bool ok, const juce::String& why)
        {
            logLine (juce::String ("[audition-check] ") + (ok ? "OK: " : "FAIL: ") + why);
            stopAudition();
            check.active = false;

            if (auto done = std::move (check.onDone); done != nullptr)
                done (ok);
        };

        if (! snap.auditioning)
        {
            if (! auditionPending)
                finish (false, "audition is not running (" + views.statusBar.getMessage() + ")");

            return;   // still loading
        }

        if (check.startedMs <= 0.0)
        {
            check.startedMs = now;
            check.firstPlayhead = juce::roundToInt (views.waveform.getAuditionStart() * snap.sourceSampleRate);
        }

        check.lastPlayhead = snap.playheadSample;
        check.maxOutputPeak = juce::jmax (check.maxOutputPeak, snap.outputPeak);
        check.maxInputPeak = juce::jmax (check.maxInputPeak, snap.inputPeak);

        const auto elapsed = (now - check.startedMs) / 1000.0;

        if (now - check.lastPrintMs >= 100.0)
        {
            check.lastPrintMs = now;
            logLine ("[audition-check] t=" + juce::String (elapsed, 2) + " s  playhead="
                     + format::time ((double) snap.playheadSample / snap.sourceSampleRate)
                     + "  out peak=" + juce::String (juce::Decibels::gainToDecibels (snap.outputPeak), 1) + " dBFS"
                     + "  in peak=" + juce::String (juce::Decibels::gainToDecibels (snap.inputPeak), 1) + " dBFS");
        }

        if (! snap.auditionFinished && elapsed < check.seconds)
            return;

        const auto* source = duplex.getAuditionSource();
        const auto rate = snap.sourceSampleRate;
        const auto played = (double) (check.lastPlayhead - check.firstPlayhead) / rate;
        const auto expectedMax = source != nullptr ? source->peak * juce::Decibels::decibelsToGain (duplex.getGainDb()) : 0.0f;
        const auto outDb = juce::Decibels::gainToDecibels (check.maxOutputPeak);
        const auto expectedDb = juce::Decibels::gainToDecibels (expectedMax);

        const auto advanced = played >= 0.5 * elapsed;
        const auto levelOk = check.maxOutputPeak > 0.0f && check.maxOutputPeak <= expectedMax * 1.0001f + 1.0e-7f;

        finish (advanced && levelOk,
                "played " + juce::String (played, 2) + " s of " + (source != nullptr ? source->file.getFileName() : juce::String())
                + " in " + juce::String (elapsed, 2) + " s; output peak " + juce::String (outDb, 1)
                + " dBFS (file peak with gain " + juce::String (expectedDb, 1) + " dBFS); input peak "
                + juce::String (juce::Decibels::gainToDecibels (check.maxInputPeak), 1) + " dBFS"
                + (advanced ? "" : "; playhead did not advance") + (levelOk ? "" : "; output level mismatch"));
    }
}
