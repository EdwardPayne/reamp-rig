#include "MainComponent.h"
#include "../Engine/LoopbackTestDevice.h"
#include "../UI/Format.h"
#include "../UI/LookAndFeel.h"
#include "../UI/Theme.h"

namespace rf::app
{
    namespace metric = ui::theme::metric;
    namespace format = ui::format;
    using Tone = ui::StatusBar::Tone;

    namespace
    {
        constexpr int maxNamesInStatus = 3;

        juce::String describeSkipped (const std::vector<model::SkippedFile>& skipped)
        {
            if (skipped.size() == 1)
                return "Skipped " + skipped.front().file.getFileName() + " (" + skipped.front().reason + ")";

            juce::StringArray names;

            for (size_t i = 0; i < skipped.size() && i < (size_t) maxNamesInStatus; ++i)
                names.add (skipped[i].file.getFileName());

            auto text = "Skipped " + juce::String ((int) skipped.size()) + " unreadable files: " + names.joinIntoString (", ");

            if (skipped.size() > (size_t) maxNamesInStatus)
                text << ", " << ui::utf8 ("\xe2\x80\xa6");

            return text;
        }
    }

    namespace
    {
        juce::String explainOutputFolders (const juce::StringArray& paths)
        {
            return "Folders named like the DESTINATION subfolder hold results, so they are never scanned as "
                   "sources:\n" + paths.joinIntoString ("\n");
        }

        /** The development device (--virtual-device): the loopback test device, paced in real
            time, silent unless --virtual-loopback cables its output back to its input. */
        std::unique_ptr<engine::AudioDeviceInterface> makeVirtualDevice (const LaunchOptions& launch)
        {
            engine::LoopbackTestDevice::Options o;
            o.typeName = "Virtual";
            o.deviceName = "Virtual Interface";
            o.inputs = { "Virtual In 1", "Virtual In 2" };
            o.outputs = { "Virtual Out 1", "Virtual Out 2", "Virtual Out 3", "Virtual Out 4" };
            o.bufferSizes = { 64, 128, 256, 512, 1024 };
            o.loop = launch.virtualLoopbackDelay.has_value();
            o.delay = launch.virtualLoopbackDelay.value_or (0);

            if (launch.virtualReportedLatency.has_value())
            {
                o.reportedInputLatency = *launch.virtualReportedLatency / 2;
                o.reportedOutputLatency = *launch.virtualReportedLatency - *launch.virtualReportedLatency / 2;
            }
            o.paced = true;
            o.speed = launch.virtualSpeed;

            if (! launch.virtualRates.isEmpty())
            {
                o.sampleRates = launch.virtualRates;
                o.defaultSampleRate = launch.virtualRates.getFirst();
            }

            return std::make_unique<engine::LoopbackTestDevice> (o);
        }
    }

    MainComponent::MainComponent (Settings& s, const LaunchOptions& launch)
        : settings (s)
    {
        const auto useVirtualDevice = launch.virtualDevice;

        if (useVirtualDevice)
            audioDevice = makeVirtualDevice (launch);
        else
            audioDevice = std::make_unique<engine::JuceAudioDevice>();

        audio = std::make_unique<AudioController> (settings, *audioDevice,
                                                   AudioController::Views { sidebar.getAudioSection(), sidebar.getSyncSection(),
                                                                            topBar, statusBar, waveformPanel },
                                                   ! useVirtualDevice);

        sync = std::make_unique<SyncController> (settings, *audio,
                                                 SyncController::Views { sidebar.getSyncSection(), topBar, statusBar,
                                                                         confirmDialog });

        outputOptions = std::make_unique<OutputOptions> (settings, sidebar.getDestinationSection(), sidebar.getOptionsSection());
        batch = std::make_unique<BatchController> (*audio, fileModel, *outputOptions,
                                                   BatchController::Views { topBar, statusBar, waveformPanel, fileTree, confirmDialog });
        batch->onFinished = [this] { fileTreeChanged(); };

        // "Redo files with warnings" includes NC files once their configuration is synced, so
        // the header button follows the sync store as well as the list.
        fileTree.getRedoableIds = [this] { return batch != nullptr ? batch->getRedoableIds() : std::vector<model::ItemId>(); };
        sync->onStoreChanged = [this] { fileTree.refreshRedoButton(); };
        audio->onSyncUiRefreshed = [this]
        {
            fileTree.refreshRedoButton();

            if (sync != nullptr)
                sync->refreshControls();
        };

        splitLayout.setItemLayout (1, metric::splitterSize, metric::splitterSize, metric::splitterSize);

        for (auto* c : std::initializer_list<juce::Component*> { &topBar, &fileTree, &splitter,
                                                                 &waveformPanel, &sidebar, &statusBar })
            addAndMakeVisible (c);

        addChildComponent (confirmDialog);     // last: above everything when shown

        auto& includeSubfolders = sidebar.getOptionsSection().getIncludeSubfoldersToggle();
        includeSubfolders.setToggleState (settings.getIncludeSubfolders(), juce::dontSendNotification);
        includeSubfolders.onClick = [this, &includeSubfolders]
        {
            settings.setIncludeSubfolders (includeSubfolders.getToggleState());
        };

        fileTree.onAddFiles  = [this] { chooseFiles (false); };
        fileTree.onAddFolder = [this] { chooseFiles (true); };

        fileModel.addListener (this);
        fileTreeChanged();

        // Buttons must not keep keyboard focus after a click, so Space (audition) and the list
        // shortcuts keep working; the list or this component receives the keys instead.
        std::function<void (juce::Component&)> noButtonFocus = [&] (juce::Component& parent)
        {
            for (auto* child : parent.getChildren())
            {
                if (auto* button = dynamic_cast<juce::Button*> (child))
                {
                    button->setWantsKeyboardFocus (false);
                    button->setMouseClickGrabsKeyboardFocus (false);
                }

                noButtonFocus (*child);
            }
        };

        noButtonFocus (*this);
        setWantsKeyboardFocus (true);

        setSize (1280, 800);
    }

    MainComponent::~MainComponent()
    {
        fileModel.removeListener (this);
        fileTree.getRedoableIds = nullptr;
        audio->onSyncUiRefreshed = nullptr;
        batch = nullptr;          // cancels a running take while the engine still exists
        sync = nullptr;           // cancels a running measurement likewise
        outputOptions = nullptr;
        audio = nullptr;          // detaches the callback
        audioDevice = nullptr;
    }

    //==============================================================================
    void MainComponent::addPaths (const juce::Array<juce::File>& paths, std::function<void()> onAdded)
    {
        if (paths.isEmpty())
            return;

        statusBar.setMessage ("Scanning " + juce::String (paths.size()) + (paths.size() == 1 ? " item" : " items")
                              + ui::utf8 ("\xe2\x80\xa6"));

        juce::Component::SafePointer<MainComponent> safeThis (this);

        // Results "next to the source" live in the DESTINATION subfolder (default "Reamped");
        // a recursive scan never picks them up as sources.
        scanner.scanAsync (paths, settings.getIncludeSubfolders(),
                           [safeThis, done = std::move (onAdded)] (model::ScanResult result)
        {
            if (safeThis == nullptr)
                return;

            safeThis->handleScanResult (result);

            if (done != nullptr)
                done();
        },
                           settings.getSubfolderName());
    }

    void MainComponent::handleScanResult (const model::ScanResult& result)
    {
        const auto added = fileModel.add (result.files);

        juce::StringArray parts;

        if (added.added > 0)
            parts.add ("Added " + format::fileCount (added.added));

        if (added.duplicates > 0)
            parts.add (juce::String (added.duplicates) + " already in the list");

        juce::StringArray outputFolders;

        for (const auto& f : result.skippedOutputFolders)
        {
            outputFolders.add (f.getFullPathName());
            juce::Logger::writeToLog ("Skipped " + f.getFullPathName() + " (output folder)");
        }

        if (outputFolders.size() == 1)
            parts.add ("Skipped " + result.skippedOutputFolders.front().getFileName() + " (output folder)");
        else if (outputFolders.size() > 1)
            parts.add ("Skipped " + juce::String (outputFolders.size()) + " output folders ("
                       + result.skippedOutputFolders.front().getFileName() + ")");

        if (! result.skipped.empty())
        {
            parts.add (describeSkipped (result.skipped));

            juce::StringArray detail;

            for (const auto& s : result.skipped)
            {
                detail.add (s.file.getFullPathName() + ": " + s.reason);
                juce::Logger::writeToLog ("Skipped " + detail[detail.size() - 1]);
            }

            for (const auto& f : outputFolders)
                detail.add (f + ": output folder, never scanned as a source");

            statusBar.setMessage (parts.joinIntoString (". "), Tone::warning, detail.joinIntoString ("\n"));
        }
        else if (added.added == 0 && added.duplicates == 0)
        {
            statusBar.setMessage (parts.isEmpty() ? juce::String ("No audio files found")
                                                  : "No audio files found. " + parts.joinIntoString (". "),
                                  Tone::warning, outputFolders.isEmpty() ? juce::String() : explainOutputFolders (outputFolders));
        }
        else
        {
            statusBar.setMessage (parts.joinIntoString (". "), Tone::normal,
                                  outputFolders.isEmpty() ? juce::String() : explainOutputFolders (outputFolders));
        }

        if (scanner.getNumPending() == 0 && added.added > 0)
            fileTree.focusList();
    }

    void MainComponent::applyLaunchOptions (const LaunchOptions& options, std::function<void (bool)> onCheckDone)
    {
        audio->openInitialDevice (options);

        if (options.syncLevelDb.has_value())
            sync->overrideLevel (*options.syncLevelDb);

        if (options.sidebarScroll.isNotEmpty())
            juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MainComponent> (this), name = options.sidebarScroll]
            {
                if (safe != nullptr)
                    safe->sidebar.scrollToSection (name);
            });

        if (options.batchCheckFolder.has_value())
            outputOptions->overrideDestination (*options.batchCheckFolder);

        auto apply = [this, options, onCheckDone]
        {
            std::vector<model::ItemId> ids;

            for (const auto& name : options.selectNames)
                for (const auto id : fileModel.getAllIds())
                    if (const auto* item = fileModel.find (id); item != nullptr
                        && (item->file.getFileName() == name || item->file.getFullPathName().endsWith (name)))
                        ids.push_back (id);

            if (! ids.empty())
                fileModel.setSelection (ids, ids.front());

            if (options.view.has_value())
                waveformPanel.setVisibleRange (*options.view);

            if (options.auditionStart.has_value())
                waveformPanel.setAuditionStart (*options.auditionStart);

            if (options.auditionCheckSeconds.has_value())
                audio->runAuditionCheck (*options.auditionCheckSeconds, onCheckDone);

            if (options.batchCheckFolder.has_value())
                batch->setCheckTransport (options.batchCheckTransport);

            auto runBatchCheck = [this, options] (std::function<void (bool)> done)
            {
                batch->runCheck (options.snapshotFile.has_value(), ! options.batchCheckHardware, std::move (done));
            };

            if (options.syncCheck)
            {
                // The virtual loopback knows its true round trip: the measurement must equal it.
                std::function<int()> expected;

                if (auto* loop = dynamic_cast<engine::LoopbackTestDevice*> (audioDevice.get()); loop != nullptr && options.virtualDevice)
                    expected = [loop] { return loop->getRoundTripSamples(); };

                sync->runCheck (options.syncCheckRates, expected, [options, onCheckDone, runBatchCheck] (bool syncOk)
                {
                    if (options.batchCheckFolder.has_value())
                    {
                        runBatchCheck ([syncOk, onCheckDone] (bool batchOk)
                        {
                            if (onCheckDone != nullptr)
                                onCheckDone (syncOk && batchOk);
                        });
                    }
                    else if (onCheckDone != nullptr)
                    {
                        onCheckDone (syncOk);
                    }
                });
            }
            else if (options.batchCheckFolder.has_value())
            {
                runBatchCheck (onCheckDone);
            }

            if (options.pressStart)
                batch->start();

            if (options.pressForget)
                sync->forget();
        };

        // --virtual-unplug=<at>:<for>: the virtual interface disappears <at> seconds after
        // launch and comes back <for> seconds later (reconnection check).
        if (auto* loop = dynamic_cast<engine::LoopbackTestDevice*> (audioDevice.get()); loop != nullptr && options.virtualUnplug.has_value())
        {
            const auto [at, duration] = *options.virtualUnplug;
            juce::Component::SafePointer<MainComponent> safe (this);

            juce::Timer::callAfterDelay ((int) (at * 1000.0), [safe, loop]
            {
                if (safe != nullptr)
                {
                    juce::Logger::writeToLog ("[virtual-unplug] interface unplugged");
                    loop->setPresent (false);
                }
            });

            juce::Timer::callAfterDelay ((int) ((at + duration) * 1000.0), [safe, loop]
            {
                if (safe != nullptr)
                {
                    juce::Logger::writeToLog ("[virtual-unplug] interface plugged back in");
                    loop->setPresent (true);
                }
            });
        }

        if (options.openPaths.isEmpty())
            apply();
        else
            addPaths (options.openPaths, apply);
    }

    bool MainComponent::isBusy() const
    {
        return scanner.getNumPending() > 0 || waveformPanel.isLoading() || audio->isBusy() || sync->isBusy()
            || batch->isWaitingForSnapshot();
    }

    void MainComponent::chooseFiles (bool folders)
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();

        const auto flags = juce::FileBrowserComponent::openMode
                         | juce::FileBrowserComponent::canSelectMultipleItems
                         | (folders ? juce::FileBrowserComponent::canSelectDirectories
                                    : juce::FileBrowserComponent::canSelectFiles);

        chooser = std::make_unique<juce::FileChooser> (folders ? "Add folder" : "Add files", juce::File(),
                                                       folders ? juce::String() : formats.getWildcardForAllFormats());

        chooser->launchAsync (flags, [this] (const juce::FileChooser& fc)
        {
            addPaths (fc.getResults());
        });
    }

    //==============================================================================
    void MainComponent::fileTreeChanged()
    {
        statusBar.setQueueText (format::fileCount (fileModel.countWithStatus (model::FileStatus::queued)) + " queued");

        const auto* lead = fileModel.find (fileModel.getLead());

        if (lead != nullptr)
            outputOptions->setExampleSource (lead->file, lead->root,
                                             lead->hasChannelChoice() ? std::optional<model::Channel> (lead->channel) : std::nullopt);
        else
            outputOptions->setExampleSource ({}, {}, std::nullopt);

        // While a batch runs, the waveform panel follows the batch, not the selection.
        if (batch != nullptr && batch->isActive())
            return;

        if (lead != nullptr)
        {
            const auto channel = lead->hasChannelChoice() && lead->channel == model::Channel::right ? 1 : 0;
            waveformPanel.setSource (lead->file, lead->info.numChannels, lead->info.sampleRate,
                                     lead->info.lengthInSamples, channel);

            // The recorded lane shows this file's last take (built from the written file).
            if (lead->status == model::FileStatus::done && lead->outputFile.existsAsFile())
                waveformPanel.showRecordedFile (lead->file, lead->outputFile);

            audio->setLead (lead->file, channel);
        }
        else
        {
            waveformPanel.clearSource();
            audio->setLead ({}, 0);
        }
    }

    //==============================================================================
    bool MainComponent::isInterestedInFileDrag (const juce::StringArray&)
    {
        return true;
    }

    void MainComponent::fileDragEnter (const juce::StringArray&, int, int)
    {
        fileTree.setDropHighlight (true);
    }

    void MainComponent::fileDragExit (const juce::StringArray&)
    {
        fileTree.setDropHighlight (false);
    }

    void MainComponent::filesDropped (const juce::StringArray& files, int, int)
    {
        fileTree.setDropHighlight (false);

        juce::Array<juce::File> paths;

        for (const auto& f : files)
            paths.add (juce::File (f));

        addPaths (paths);
    }

    //==============================================================================
    bool MainComponent::keyPressed (const juce::KeyPress& key)
    {
        // Reaches here when the focused component (usually the file list) does not use the key.
        if (confirmDialog.isShowing())
            return confirmDialog.keyPressed (key);

        if (key == juce::KeyPress::spaceKey)
        {
            audio->toggleAudition();
            return true;
        }

        return false;
    }

    //==============================================================================
    void MainComponent::paint (juce::Graphics& g)
    {
        g.fillAll (ui::theme::colour::bg);
    }

    void MainComponent::resized()
    {
        auto area = getLocalBounds();

        confirmDialog.setBounds (area);

        topBar.setBounds (area.removeFromTop (metric::topBarHeight));
        statusBar.setBounds (area.removeFromBottom (metric::statusBarHeight));
        sidebar.setBounds (area.removeFromRight (metric::sidebarWidth));

        layOutSplit (area);
    }

    void MainComponent::layOutSplit (juce::Rectangle<int> area)
    {
        // Keep the waveform panel at its current (or dragged) height when the window resizes;
        // the file tree takes the rest.
        if (const auto current = splitLayout.getItemCurrentAbsoluteSize (2); current > 0)
            waveformHeight = current;

        const auto maxWaveform = juce::jmax (metric::waveformMinHeight,
                                             area.getHeight() - metric::splitterSize - metric::fileTreeMinHeight);
        waveformHeight = juce::jlimit (metric::waveformMinHeight, maxWaveform, waveformHeight);

        const auto fileTreeHeight = area.getHeight() - metric::splitterSize - waveformHeight;
        splitLayout.setItemLayout (0, metric::fileTreeMinHeight, -1.0, fileTreeHeight);
        splitLayout.setItemLayout (2, metric::waveformMinHeight, -1.0, waveformHeight);

        juce::Component* stack[] = { &fileTree, &splitter, &waveformPanel };
        splitLayout.layOutComponents (stack, 3, area.getX(), area.getY(), area.getWidth(), area.getHeight(),
                                      true, true);
    }
}
