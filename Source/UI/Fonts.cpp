#include "Fonts.h"
#include "Theme.h"

#include <BinaryData.h>

#include <array>

namespace rf::ui
{
    namespace
    {
        juce::Typeface::Ptr loadTypeface (const char* data, int size)
        {
            auto typeface = juce::Typeface::createSystemTypefaceFor (data, (size_t) size);
            jassert (typeface != nullptr);
            return typeface;
        }

        size_t weightIndex (FontWeight weight)
        {
            return static_cast<size_t> (weight);
        }

        juce::Font makeFont (juce::Typeface::Ptr typeface, float pointSize, float tracking)
        {
            return juce::Font (juce::FontOptions (std::move (typeface))
                                   .withPointHeight (pointSize)
                                   .withKerningFactor (tracking));
        }
    }

    struct Fonts::Typefaces
    {
        std::array<juce::Typeface::Ptr, 3> mono
        {
            loadTypeface (BinaryData::JetBrainsMonoRegular_ttf, BinaryData::JetBrainsMonoRegular_ttfSize),
            loadTypeface (BinaryData::JetBrainsMonoMedium_ttf,  BinaryData::JetBrainsMonoMedium_ttfSize),
            loadTypeface (BinaryData::JetBrainsMonoBold_ttf,    BinaryData::JetBrainsMonoBold_ttfSize)
        };

        std::array<juce::Typeface::Ptr, 3> sans
        {
            loadTypeface (BinaryData::InterRegular_ttf, BinaryData::InterRegular_ttfSize),
            loadTypeface (BinaryData::InterMedium_ttf,  BinaryData::InterMedium_ttfSize),
            loadTypeface (BinaryData::InterBold_ttf,    BinaryData::InterBold_ttfSize)
        };
    };

    Fonts::Fonts() = default;

    juce::Typeface::Ptr Fonts::getMonoTypeface (FontWeight weight) const
    {
        return typefaces->mono[weightIndex (weight)];
    }

    juce::Typeface::Ptr Fonts::getSansTypeface (FontWeight weight) const
    {
        return typefaces->sans[weightIndex (weight)];
    }

    juce::Font Fonts::mono (float pointSize, FontWeight weight, float tracking)
    {
        return makeFont (Fonts().getMonoTypeface (weight), pointSize, tracking);
    }

    juce::Font Fonts::sans (float pointSize, FontWeight weight, float tracking)
    {
        return makeFont (Fonts().getSansTypeface (weight), pointSize, tracking);
    }

    juce::Font Fonts::sectionLabel()
    {
        return mono (theme::type::sectionLabelSize, FontWeight::regular, theme::type::sectionLabelTracking);
    }
}
