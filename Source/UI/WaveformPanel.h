#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "LookAndFeel.h"

#include <mutex>
#include <optional>

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
        lanes sets the audition start point (accent marker with a flag). While a file plays
        (audition or the batch) a 2 px accent playhead moves across the lanes and the view
        follows it when zoomed in.

        Recorded lane (PROMPT.md 3.5.2): the result of the current or last take, under the
        source on the same time axis (the writer delivers it latency-compensated, so sample 0
        lines up with the source's sample 0). While a take records, the writer thread feeds
        the samples it writes into a second AudioThumbnail (addRecordedSamples, thread-safe);
        a finished take of another file is drawn from its written file, built on the
        thumbnail cache's thread. Only one recorded thumbnail exists at a time.
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

        /** Playback position in seconds, or nullopt when nothing plays. */
        void setPlayhead (std::optional<double> seconds);
        std::optional<double> getPlayhead() const noexcept     { return playhead; }

        juce::Range<double> getVisibleRange() const noexcept   { return visible; }
        void setVisibleRange (juce::Range<double>);

        /** True while the thumbnail is still being generated. */
        bool isLoading() const;

        //==============================================================================
        /** Starts a live recorded thumbnail for `source` (message thread). */
        void beginRecording (const juce::File& source, double sampleRate, juce::int64 expectedLength);

        /** Any thread (the writer thread): samples written to the take's file, file rate. */
        void addRecordedSamples (juce::int64 start, const float* data, int numSamples);

        /** Shows a finished take of `source` from its file (unless it is already shown). */
        void showRecordedFile (const juce::File& source, const juce::File& recordedFile);

        void clearRecorded();

        /** True when the recorded lane has something to draw for the shown source. */
        bool hasRecorded() const;

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
        void paintRecordedLane (juce::Graphics&, juce::Rectangle<int> label, juce::Rectangle<int> wave) const;
        void paintEmptyLane (juce::Graphics&, juce::Rectangle<int> label, juce::Rectangle<int> wave,
                             const juce::String& name, const juce::String& text) const;
        void paintMarker (juce::Graphics&) const;
        void paintPlayhead (juce::Graphics&) const;

        double xToTime (float x) const;
        float timeToX (double t) const;
        void zoomAround (double factor, float anchorX);
        void setAuditionFromX (float x);
        void updateScrollBar();
        double getMinVisibleLength() const;

        juce::AudioFormatManager formats;
        juce::AudioThumbnailCache thumbnailCache { 16 };
        mutable juce::AudioThumbnail thumbnail { 64, formats, thumbnailCache };   // drawChannel() is non-const

        // Recorded result (current/last take only). `recordedLock` serialises the writer
        // thread's addRecordedSamples with resets on the message thread.
        juce::AudioThumbnailCache recordedCache { 1 };
        mutable juce::AudioThumbnail recorded { 64, formats, recordedCache };
        std::mutex recordedLock;
        bool recordedLive = false;
        juce::File recordedSource, recordedFile;

        TooltipScrollBar scrollBar { false };

        juce::File sourceFile;
        int numChannels = 0;
        int activeChannel = 0;
        double sampleRate = 0.0;
        double duration = 0.0;
        double auditionStart = 0.0;
        std::optional<double> playhead;
        juce::Range<double> visible { 0.0, 10.0 };

        // Laid out in resized().
        juce::Rectangle<int> headerArea, rulerArea, sourceLabelArea, sourceWaveArea,
                             recordedLabelArea, recordedWaveArea, scrollArea;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WaveformPanel)
    };
}
