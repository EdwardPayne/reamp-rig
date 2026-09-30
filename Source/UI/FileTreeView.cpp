#include "FileTreeView.h"
#include "Fonts.h"
#include "LookAndFeel.h"
#include "Theme.h"

namespace rf::ui
{
    namespace colour = theme::colour;
    namespace metric = theme::metric;
    namespace type   = theme::type;

    namespace
    {
        constexpr int headerHeight = 36;
    }

    void FileTreeView::paint (juce::Graphics& g)
    {
        g.fillAll (colour::panel);

        auto area = getLocalBounds();

        // Header strip.
        auto header = area.removeFromTop (headerHeight);
        g.setColour (colour::line);
        g.fillRect (header.removeFromBottom (1));

        header.reduce (metric::sectionPadding, 0);
        drawSectionLabel (g, "Files", header);

        g.setColour (colour::muted);
        g.setFont (Fonts::mono (type::fieldLabelSize));
        g.drawText ("0 files", header, juce::Justification::centredRight, false);

        // Empty-state drop zone.
        const auto dropZone = area.reduced (metric::sectionPadding);
        g.setColour (colour::line);
        g.drawRect (dropZone.toFloat(), metric::borderWidth);

        auto text = dropZone.withSizeKeepingCentre (dropZone.getWidth(), 48);

        g.setColour (colour::heading);
        g.setFont (Fonts::mono (type::bodySize, FontWeight::medium));
        g.drawText ("Drop DI files or folders here", text.removeFromTop (24),
                    juce::Justification::centred, true);

        g.setColour (colour::faint);
        g.setFont (Fonts::sans (type::helpSize));
        g.drawText ("WAV, AIFF or FLAC. Folders are scanned and grouped by location.",
                    text, juce::Justification::centred, true);
    }
}
