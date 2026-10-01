#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <optional>

namespace rf::ui
{
    /*  Base class for a stacked sidebar section: an uppercase mono section label followed by
        rows of fields. Each field is an optional small mono label above a control; fields in
        the same row share the width equally. Sections report their preferred height so the
        sidebar can stack them in a scrolling viewport.
    */
    class SidebarSection : public juce::Component
    {
    public:
        explicit SidebarSection (juce::String title);

        int getPreferredHeight() const;

        /** Called when a row is shown or hidden, so the sidebar can re-stack the sections. */
        std::function<void()> onPreferredHeightChanged;

        void paint (juce::Graphics&) override;
        void resized() override;

    protected:
        struct Field
        {
            juce::String label;             // empty for no label
            juce::Component* component = nullptr;
            int width = 0;                  // fixed width in px; 0 = share the rest equally
        };

        /** Adds a row of fields. The component must be owned by the subclass. */
        void addRow (std::initializer_list<Field> fields, int controlHeight);

        /** Shows or hides the row containing `component`; hidden rows take no space. */
        void setRowVisible (juce::Component& component, bool shouldBeVisible);

    private:
        struct Row
        {
            std::vector<Field> fields;
            std::vector<std::unique_ptr<juce::Label>> labels;
            int controlHeight = 0;
            bool visible = true;

            bool hasLabels() const;
            int getHeight() const;
        };

        juce::String title;
        std::vector<Row> rows;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SidebarSection)
    };

    //==============================================================================
    /** A read-only key/value line, e.g. "MEASURED   312 smp · 6.50 ms". */
    class ValueReadout final : public juce::Component,
                               public juce::SettableTooltipClient
    {
    public:
        ValueReadout (juce::String key, juce::String value);

        /** `colour` defaults to the heading colour; pass theme::colour::warn for warnings. */
        void setValue (const juce::String&, std::optional<juce::Colour> colour = std::nullopt);
        const juce::String& getValue() const noexcept   { return value; }

        /** Long values lose their start ("…/Session A/Riff 01_reamp.wav") instead of their end. */
        void setTruncateFromStart (bool shouldTruncateStart)   { truncateStart = shouldTruncateStart; repaint(); }

        void paint (juce::Graphics&) override;

    private:
        juce::String key, value;
        std::optional<juce::Colour> valueColour;
        bool truncateStart = false;
    };

    //==============================================================================
    /** A clickable, bordered field showing a path (start-truncated), e.g. the output folder.
        Shows `placeholder` in the muted colour while empty. */
    class PathField final : public juce::Component,
                            public juce::SettableTooltipClient
    {
    public:
        explicit PathField (juce::String placeholder);

        void setPath (const juce::String& displayPath);
        const juce::String& getPath() const noexcept   { return path; }

        std::function<void()> onClick;

        void paint (juce::Graphics&) override;
        void mouseEnter (const juce::MouseEvent&) override   { repaint(); }
        void mouseExit (const juce::MouseEvent&) override    { repaint(); }
        void mouseUp (const juce::MouseEvent&) override;

    private:
        juce::String placeholder, path;
    };

    /** Fits `text` into `width` by dropping characters from its start ("…/b/c.wav"). */
    juce::String fitFromStart (const juce::Font&, const juce::String& text, int width);

    //==============================================================================
    /** A one-line notice with a warning badge, e.g. "Not sample-synchronized, for testing only". */
    class NoticeLine final : public juce::Component,
                             public juce::SettableTooltipClient
    {
    public:
        explicit NoticeLine (juce::String text);

        void setText (const juce::String&);
        const juce::String& getText() const noexcept   { return text; }

        /** Badge and text colour: theme::colour::warn (default) or theme::colour::error. */
        void setTone (juce::Colour);

        void paint (juce::Graphics&) override;

    private:
        juce::String text;
        juce::Colour tone;
    };
}
