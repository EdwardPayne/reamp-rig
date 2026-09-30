#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

namespace rf::ui
{
    /*  Bottom panel: source waveform above, recorded result below, on a shared time axis
        (PROMPT.md section 3.5).

        The source is drawn from a juce::AudioThumbnail whose data is generated on the
        AudioThumbnailCache's own background thread (only the file header is read on the
        message thread when a file is selected); finished thumbnails are kept in the cache so
        reselecting a file is instant. Stereo files get one lane per channel with the channel
        that is not played dimmed.

        Time axis: the ruler follows the visible range. Cmd+scroll or pinch zooms around the
        mouse; plain scroll (or the scrollbar) pans while zoomed. Clicking or dragging in the
        lanes sets the audition start point (accent marker); nothing plays until phase 3.
        The "Recorded" lane is a placeholder until phase 4.
    */
    class WaveformPanel final : public juce::Component,
                                public juce::SettableTooltipClient,
                                private juce::ChangeListener,
                                private juce::ScrollBar::Listener
    {
    public:
        WaveformPanel();
        ~WaveformPanel() override;

        /** Shows a file. `channel` is the channel that will be played (0 = L, 1 = R). */
        void setSource (const juce::File&, int numChannels, double sampleRate, juce::int64 lengthInSamples, int channel);
        void clearSource();
        bool hasSource() const noexcept                   { return sourceFile != juce::File(); }
        const juce::File& getSourceFile() const noexcept  { return sourceFile; }

        void setActiveChannel (int channel);

        /** Audition start point in seconds (phase 3 plays from here). */
        double getAuditionStart() const noexcept          { return auditionStart; }
        void setAuditionStart (double seconds);
        std::function<void (double)> onAuditionStartChanged;

        juce::Range<double> getVisibleRange() const noexcept   { return visible; }
        void setVisibleRange (juce::Range<double>);

        /** True while the thumbnail is still being generated. */
        bool isLoading() const;

        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
        void mouseMagnify (const juce::MouseEvent&, float scaleFactor) override;

    private:
        void changeListenerCallback (juce::ChangeBroadcaster*) override;
        void scrollBarMoved (juce::ScrollBar*, double newRangeStart) override;

        void paintHeader (juce::Graphics&, juce::Rectangle<int>) const;
        void paintRuler (juce::Graphics&, juce::Rectangle<int>) const;
        void paintSourceLane (juce::Graphics&, juce::Rectangle<int> label, juce::Rectangle<int> wave) const;
        void paintEmptyLane (juce::Graphics&, juce::Rectangle<int> label, juce::Rectangle<int> wave,
                             const juce::String& name, const juce::String& text) const;
        void paintMarker (juce::Graphics&) const;

        double xToTime (float x) const;
        float timeToX (double t) const;
        void zoomAround (double factor, float anchorX);
        void setAuditionFromX (float x);
        void updateScrollBar();
        double getMinVisibleLength() const;

        juce::AudioFormatManager formats;
        juce::AudioThumbnailCache thumbnailCache { 16 };
        mutable juce::AudioThumbnail thumbnail { 64, formats, thumbnailCache };   // drawChannel() is non-const

        juce::ScrollBar scrollBar { false };

        juce::File sourceFile;
        int numChannels = 0;
        int activeChannel = 0;
        double sampleRate = 0.0;
        double duration = 0.0;
        double auditionStart = 0.0;
        juce::Range<double> visible { 0.0, 10.0 };

        // Laid out in resized().
        juce::Rectangle<int> headerArea, rulerArea, sourceLabelArea, sourceWaveArea,
                             recordedLabelArea, recordedWaveArea, scrollArea;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WaveformPanel)
    };
}
