#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

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

        void paint (juce::Graphics&) override;
        void resized() override;

    protected:
        struct Field
        {
            juce::String label;             // empty for no label
            juce::Component* component = nullptr;
        };

        /** Adds a row of fields. The component must be owned by the subclass. */
        void addRow (std::initializer_list<Field> fields, int controlHeight);

    private:
        struct Row
        {
            std::vector<Field> fields;
            std::vector<std::unique_ptr<juce::Label>> labels;
            int controlHeight = 0;

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

        void setValue (const juce::String&);
        void paint (juce::Graphics&) override;

    private:
        juce::String key, value;
    };
}
