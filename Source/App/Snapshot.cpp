#include "Snapshot.h"

namespace rf::app
{
    bool writeSnapshot (juce::Component& window, const juce::File& pngFile, float scale)
    {
        const auto windowBounds = window.getScreenBounds();

        juce::Image image (juce::Image::ARGB,
                           juce::roundToInt ((float) windowBounds.getWidth()  * scale),
                           juce::roundToInt ((float) windowBounds.getHeight() * scale),
                           true);
        {
            juce::Graphics g (image);
            g.addTransform (juce::AffineTransform::scale (scale));

            auto& desktop = juce::Desktop::getInstance();

            // Desktop components are ordered back to front, so popups land on top.
            for (int i = 0; i < desktop.getNumComponents(); ++i)
            {
                auto* c = desktop.getComponent (i);

                if (c == nullptr || ! c->isVisible())
                    continue;

                const auto relative = c->getScreenBounds() - windowBounds.getPosition();

                if (! relative.intersects (window.getLocalBounds()))
                    continue;

                juce::Graphics::ScopedSaveState state (g);
                g.setOrigin (relative.getPosition());
                c->paintEntireComponent (g, true);
            }
        }

        pngFile.deleteFile();
        juce::FileOutputStream stream (pngFile);

        return stream.openedOk() && juce::PNGImageFormat().writeImageToStream (image, stream);
    }
}
