#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace rf::ui
{
    /*  A themed, in-window confirmation dialog (no stock AlertWindow, no OS window, so no
        shadow and nothing JUCE-styled leaks through). It covers the whole window with a dimmed
        backdrop that swallows clicks, and shows a sharp-cornered panel with a warning badge
        and title, a line of text, a boxed list (e.g. the device configurations without a sync
        measurement), a note, and two buttons. Escape cancels, Return confirms.

        The owner adds it as the topmost child, keeps it sized to the whole window, and calls
        show(); the callback receives true for the confirm button.
    */
    class ConfirmDialog final : public juce::Component
    {
    public:
        struct Content
        {
            juce::String title;
            juce::String intro;
            juce::StringArray items;
            juce::String note;
            juce::String confirmText = "OK";
            juce::String cancelText = "Cancel";
        };

        ConfirmDialog();

        void show (Content, std::function<void (bool confirmed)> onResult);

        /** Closes it as if a button had been pressed. */
        void dismiss (bool confirmed);

        bool isShowing() const noexcept     { return isVisible(); }
        const Content& getContent() const noexcept   { return content; }

        void paint (juce::Graphics&) override;
        void resized() override;
        bool keyPressed (const juce::KeyPress&) override;

    private:
        juce::Rectangle<int> getPanelBounds() const;
        juce::TextLayout layoutText (const juce::String&, int width, juce::Colour) const;

        Content content;
        std::function<void (bool)> callback;
        juce::TextButton confirmButton, cancelButton;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ConfirmDialog)
    };
}
