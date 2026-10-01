#include "MainComponent.h"
#include "VirtualAudioDevice.h"
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

    MainComponent::MainComponent (Settings& s, bool useVirtualDevice)
        : settings (s)
    {
        if (useVirtualDevice)
            audioDevice = std::make_unique<VirtualAudioDevice>();
        else
            audioDevice = std::make_unique<engine::JuceAudioDevice>();

        audio = std::make_unique<AudioController> (settings, *audioDevice,
                                                   AudioController::Views { sidebar.getAudioSection(), sidebar.getSyncSection(),
                                                                            topBar, statusBar, waveformPanel },
                                                   ! useVirtualDevice);

        splitLayout.setItemLayout (1, metric::splitterSize, metric::splitterSize, metric::splitterSize);

        for (auto* c : std::initializer_list<juce::Component*> { &topBar, &fileTree, &splitter,
                                                                 &waveformPanel, &sidebar, &statusBar })
            addAndMakeVisible (c);

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
        audio = nullptr;          // detaches the callback first
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

        scanner.scanAsync (paths, settings.getIncludeSubfolders(),
                           [safeThis, done = std::move (onAdded)] (model::ScanResult result)
        {
            if (safeThis == nullptr)
                return;

            safeThis->handleScanResult (result);

            if (done != nullptr)
                done();
        });
    }

    void MainComponent::handleScanResult (const model::ScanResult& result)
    {
        const auto added = fileModel.add (result.files);

        juce::StringArray parts;

        if (added.added > 0)
            parts.add ("Added " + format::fileCount (added.added));

        if (added.duplicates > 0)
            parts.add (juce::String (added.duplicates) + " already in the list");

        if (! result.skipped.empty())
        {
            parts.add (describeSkipped (result.skipped));

            juce::StringArray detail;

            for (const auto& s : result.skipped)
            {
                detail.add (s.file.getFullPathName() + ": " + s.reason);
                juce::Logger::writeToLog ("Skipped " + detail[detail.size() - 1]);
            }

            statusBar.setMessage (parts.joinIntoString (". "), Tone::warning, detail.joinIntoString ("\n"));
        }
        else if (parts.isEmpty())
        {
            statusBar.setMessage ("No audio files found", Tone::warning);
        }
        else
        {
            statusBar.setMessage (parts.joinIntoString (". "));
        }

        if (scanner.getNumPending() == 0 && added.added > 0)
            fileTree.focusList();
    }

    void MainComponent::applyLaunchOptions (const LaunchOptions& options, std::function<void (bool)> onAuditionCheckDone)
    {
        audio->openInitialDevice (options);

        auto apply = [this, options, onAuditionCheckDone]
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
                audio->runAuditionCheck (*options.auditionCheckSeconds, onAuditionCheckDone);
        };

        if (options.openPaths.isEmpty())
            apply();
        else
            addPaths (options.openPaths, apply);
    }

    bool MainComponent::isBusy() const
    {
        return scanner.getNumPending() > 0 || waveformPanel.isLoading() || audio->isBusy();
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

        if (const auto* lead = fileModel.find (fileModel.getLead()))
        {
            const auto channel = lead->hasChannelChoice() && lead->channel == model::Channel::right ? 1 : 0;
            waveformPanel.setSource (lead->file, lead->info.numChannels, lead->info.sampleRate,
                                     lead->info.lengthInSamples, channel);
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
