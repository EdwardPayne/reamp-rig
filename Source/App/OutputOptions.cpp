#include "OutputOptions.h"
#include "../UI/Format.h"
#include "../UI/LookAndFeel.h"
#include "../UI/Theme.h"

namespace rf::app
{
    namespace
    {
        /** "2", "0.5", "12.5" (tenths, no trailing ".0"). */
        juce::String formatSeconds (double s)
        {
            const auto text = juce::String (s, 1);
            return text.endsWith (".0") ? text.dropLastCharacters (2) : text;
        }

        constexpr int bitDepths[] = { 16, 24, 32 };   // order of the format combo
        constexpr model::CollisionPolicy policies[] = { model::CollisionPolicy::autoNumber, model::CollisionPolicy::overwrite,
                                                         model::CollisionPolicy::skip };   // order of the collision combo
    }

    OutputOptions::OutputOptions (Settings& s, ui::DestinationSection& d, ui::OptionsSection& o)
        : settings (s), destination (d), options (o)
    {
        load();
        wire();
        refresh();
    }

    OutputOptions::~OutputOptions()
    {
        for (auto* b : { &destination.getBesideSourceRadio(), &destination.getSingleFolderRadio(),
                         &destination.getMirrorToggle(), &options.getChannelTagToggle() })
            b->onClick = nullptr;

        for (auto* e : { &destination.getSubfolderEditor(), &destination.getPrefixEditor(),
                         &destination.getSuffixEditor(), &options.getTailEditor(), &options.getPauseEditor() })
            e->onTextChange = nullptr;

        options.getPauseEditor().onFocusLost = nullptr;

        destination.getFolderField().onClick = nullptr;
        destination.getFormatBox().onChange = nullptr;
        destination.getCollisionBox().onChange = nullptr;
    }

    void OutputOptions::load()
    {
        const auto mode = settings.getDestinationMode();
        destination.getBesideSourceRadio().setToggleState (mode == model::DestinationMode::besideSource, juce::dontSendNotification);
        destination.getSingleFolderRadio().setToggleState (mode == model::DestinationMode::singleFolder, juce::dontSendNotification);
        destination.getSubfolderEditor().setText (settings.getSubfolderName(), false);
        destination.getMirrorToggle().setToggleState (settings.getMirrorStructure(), juce::dontSendNotification);
        destination.getPrefixEditor().setText (settings.getPrefix(), false);
        destination.getSuffixEditor().setText (settings.getSuffix(), false);

        const auto bits = settings.getBitDepth();

        for (int i = 0; i < (int) std::size (bitDepths); ++i)
            if (bitDepths[i] == bits)
                destination.getFormatBox().setSelectedItemIndex (i, juce::dontSendNotification);

        const auto policy = settings.getCollisionPolicy();

        for (int i = 0; i < (int) std::size (policies); ++i)
            if (policies[i] == policy)
                destination.getCollisionBox().setSelectedItemIndex (i, juce::dontSendNotification);

        options.getChannelTagToggle().setToggleState (settings.getChannelTag(), juce::dontSendNotification);
        options.getTailEditor().setText (juce::String (settings.getTailMs()), false);
        options.getPauseEditor().setText (formatSeconds (settings.getPauseBetweenFilesSeconds()), false);
    }

    void OutputOptions::wire()
    {
        destination.getBesideSourceRadio().onClick = [this]
        {
            if (destination.getBesideSourceRadio().getToggleState())
            {
                settings.setDestinationMode (model::DestinationMode::besideSource);
                refresh();
            }
        };

        destination.getSingleFolderRadio().onClick = [this]
        {
            if (destination.getSingleFolderRadio().getToggleState())
            {
                settings.setDestinationMode (model::DestinationMode::singleFolder);
                refresh();

                if (settings.getOutputFolder() == juce::File())
                    chooseFolder();
            }
        };

        destination.getSubfolderEditor().onTextChange = [this]
        {
            settings.setSubfolderName (destination.getSubfolderEditor().getText());
            refresh();
        };

        destination.getFolderField().onClick = [this] { chooseFolder(); };

        destination.getMirrorToggle().onClick = [this]
        {
            settings.setMirrorStructure (destination.getMirrorToggle().getToggleState());
            refresh();
        };

        destination.getPrefixEditor().onTextChange = [this]
        {
            settings.setPrefix (destination.getPrefixEditor().getText());
            refresh();
        };

        destination.getSuffixEditor().onTextChange = [this]
        {
            settings.setSuffix (destination.getSuffixEditor().getText());
            refresh();
        };

        destination.getFormatBox().onChange = [this]
        {
            const auto index = destination.getFormatBox().getSelectedItemIndex();

            if (juce::isPositiveAndBelow (index, (int) std::size (bitDepths)))
                settings.setBitDepth (bitDepths[index]);
        };

        destination.getCollisionBox().onChange = [this]
        {
            const auto index = destination.getCollisionBox().getSelectedItemIndex();

            if (juce::isPositiveAndBelow (index, (int) std::size (policies)))
                settings.setCollisionPolicy (policies[index]);
        };

        options.getChannelTagToggle().onClick = [this]
        {
            settings.setChannelTag (options.getChannelTagToggle().getToggleState());
            refresh();
        };

        options.getTailEditor().onTextChange = [this]
        {
            settings.setTailMs (options.getTailEditor().getText().getIntValue());
        };

        options.getPauseEditor().onTextChange = [this]
        {
            settings.setPauseBetweenFilesSeconds (options.getPauseEditor().getText().getDoubleValue());
        };

        // Show what was stored (clamped to 0..60 s, tenths) once the field is left.
        options.getPauseEditor().onFocusLost = [this]
        {
            options.getPauseEditor().setText (formatSeconds (settings.getPauseBetweenFilesSeconds()), false);
        };
    }

    void OutputOptions::chooseFolder()
    {
        chooser = std::make_unique<juce::FileChooser> ("Choose the output folder", settings.getOutputFolder());

        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                              [this] (const juce::FileChooser& fc)
        {
            if (const auto folder = fc.getResult(); folder != juce::File())
                settings.setOutputFolder (folder);

            refresh();
        });
    }

    //==============================================================================
    model::NamingOptions OutputOptions::getNamingOptions() const
    {
        auto o = settings.getNamingOptions();

        if (overrideFolder.has_value())
        {
            o.mode = model::DestinationMode::singleFolder;
            o.outputFolder = *overrideFolder;
            o.mirrorStructure = true;
        }

        return o;
    }

    int OutputOptions::getBitsPerSample() const   { return settings.getBitDepth(); }
    int OutputOptions::getTailMs() const          { return settings.getTailMs(); }
    double OutputOptions::getPauseBetweenSeconds() const   { return settings.getPauseBetweenFilesSeconds(); }

    void OutputOptions::setExampleSource (const juce::File& source, const juce::File& root, std::optional<model::Channel> channel)
    {
        exampleSource = source;
        exampleRoot = root;
        exampleChannel = channel;
        refresh();
    }

    void OutputOptions::overrideDestination (const juce::File& folder)
    {
        overrideFolder = folder;
        refresh();
    }

    void OutputOptions::setEnabled (bool enabled)
    {
        for (auto* c : std::initializer_list<juce::Component*> {
                 &destination.getBesideSourceRadio(), &destination.getSingleFolderRadio(), &destination.getFolderField(),
                 &destination.getMirrorToggle(), &destination.getFormatBox(), &destination.getCollisionBox(),
                 &options.getChannelTagToggle(), &options.getIncludeSubfoldersToggle() })
            c->setEnabled (enabled);

        for (auto* e : { &destination.getSubfolderEditor(), &destination.getPrefixEditor(), &destination.getSuffixEditor(),
                         &options.getTailEditor(), &options.getPauseEditor() })
            ui::setTextEditorEnabled (*e, enabled);
    }

    void OutputOptions::refresh()
    {
        const auto o = getNamingOptions();
        const auto single = o.mode == model::DestinationMode::singleFolder;

        destination.showSingleFolderRows (single);

        if (overrideFolder.has_value())
        {
            destination.getSingleFolderRadio().setToggleState (true, juce::dontSendNotification);
            destination.getMirrorToggle().setToggleState (true, juce::dontSendNotification);
        }

        auto& field = destination.getFolderField();
        field.setPath (o.outputFolder != juce::File() ? ui::format::displayPath (o.outputFolder) : juce::String());
        field.setTooltip (o.outputFolder != juce::File() ? o.outputFolder.getFullPathName() + "\nClick to choose another folder."
                                                         : juce::String ("No output folder chosen yet. Click to choose one."));

        const auto source = exampleSource != juce::File() ? exampleSource
                                                          : juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                                                                .getChildFile ("DI/Riff 01.wav");
        const auto root = exampleSource != juce::File() ? exampleRoot : source.getParentDirectory();
        const auto channel = exampleSource != juce::File() ? exampleChannel : std::optional<model::Channel> (model::Channel::left);

        auto& example = destination.getExample();
        const auto problem = model::OutputNaming::validate (o);

        if (problem.isNotEmpty())
        {
            example.setValue (problem, ui::theme::colour::warn);
            example.setTooltip (problem);
            return;
        }

        example.setValue (model::OutputNaming::example (source, root, channel, o));
        example.setTooltip ("Output for " + source.getFileName() + ":\n"
                            + model::OutputNaming::folder (source, root, o)
                                  .getChildFile (model::OutputNaming::fileName (source, o, channel)).getFullPathName());
    }
}
