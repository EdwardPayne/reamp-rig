#include "WaveformPanel.h"
#include "Fonts.h"
#include "Format.h"
#include "LookAndFeel.h"
#include "Theme.h"

#include <cmath>

namespace rf::ui
{
    namespace colour = theme::colour;
    namespace metric = theme::metric;
    namespace type   = theme::type;

    namespace
    {
        constexpr int headerHeight      = 32;
        constexpr int rulerHeight       = 20;
        constexpr int laneLabelWidth    = 112;
        constexpr int minMajorTickPx    = 80;
        constexpr int markerFlagSize    = 7;
        constexpr float sourceLaneShare = 0.6f;
        constexpr double minVisibleSeconds = 0.05;

        /** Major tick steps in seconds; the ruler picks the first that is wide enough. */
        constexpr double tickSteps[] = { 0.001, 0.002, 0.005, 0.01, 0.02, 0.05, 0.1, 0.2, 0.5,
                                         1.0, 2.0, 5.0, 10.0, 15.0, 30.0, 60.0, 120.0, 300.0,
                                         600.0, 900.0, 1800.0, 3600.0 };

        int minorDivisions (double step)
        {
            const auto mantissa = step / std::pow (10.0, std::floor (std::log10 (step)));

            if (step == 15.0 || step == 30.0 || step == 900.0 || step == 1800.0)
                return 3;

            return std::abs (mantissa - 2.0) < 1.0e-6 || step == 120.0 ? 4 : 5;
        }

        juce::String tickLabel (double t, double step)
        {
            if (step >= 60.0)
            {
                const auto total = (int) std::llround (t);
                return juce::String (total / 60) + ":" + juce::String (total % 60).paddedLeft ('0', 2);
            }

            if (step >= 1.0)
                return juce::String ((int) std::llround (t)) + "s";

            const auto decimals = juce::jmax (1, (int) std::ceil (-std::log10 (step) - 1.0e-9));
            return juce::String (t, decimals) + "s";
        }
    }

    //==============================================================================
    WaveformPanel::WaveformPanel()
    {
        formats.registerBasicFormats();
        thumbnail.addChangeListener (this);
        recorded.addChangeListener (this);

        // Clicking to place the audition marker must not take keyboard focus from the file list.
        setMouseClickGrabsKeyboardFocus (false);

        scrollBar.setAutoHide (false);
        scrollBar.addListener (this);
        addChildComponent (scrollBar);

        setTooltip ("Click to set the audition start point. Cmd-scroll or pinch to zoom; "
                    "scroll to move along the file when zoomed in.");
        scrollBar.setTooltip ("Drag to move along the file while zoomed in (or scroll in the waveform).");
    }

    WaveformPanel::~WaveformPanel()
    {
        scrollBar.removeListener (this);
        recorded.removeChangeListener (this);
        thumbnail.removeChangeListener (this);
    }

    //==============================================================================
    void WaveformPanel::setSource (const juce::File& file, int channels, double rate, juce::int64 length, int channel)
    {
        if (file == sourceFile)
        {
            setActiveChannel (channel);
            return;
        }

        sourceFile    = file;
        numChannels   = channels;
        sampleRate    = rate;
        duration      = rate > 0.0 ? (double) length / rate : 0.0;
        activeChannel = channel;
        auditionStart = 0.0;
        playhead.reset();

        // Reads the header here; the waveform data is built on the cache's thread. The cache
        // key includes the file's modification time, so a file edited outside the app is drawn
        // anew instead of from a stale cache entry (review 2026-10-01, U4).
        thumbnail.setSource (new juce::FileInputSource (file, true));

        setVisibleRange ({ 0.0, duration });
        repaint();
    }

    void WaveformPanel::clearSource()
    {
        if (! hasSource())
            return;

        thumbnail.clear();
        sourceFile = juce::File();
        playhead.reset();
        numChannels = 0;
        duration = 0.0;
        auditionStart = 0.0;
        visible = { 0.0, juce::jmax (1.0, (double) sourceWaveArea.getWidth() / minMajorTickPx) };
        updateScrollBar();
        repaint();
    }

    void WaveformPanel::setActiveChannel (int channel)
    {
        if (activeChannel != channel)
        {
            activeChannel = channel;
            repaint();
        }
    }

    void WaveformPanel::setAuditionStart (double seconds)
    {
        const auto t = juce::jlimit (0.0, juce::jmax (0.0, duration), seconds);

        if (! juce::exactlyEqual (t, auditionStart))
        {
            auditionStart = t;
            repaint();

            if (onAuditionStartChanged != nullptr)
                onAuditionStartChanged (auditionStart);
        }
    }

    void WaveformPanel::setPlayhead (std::optional<double> seconds)
    {
        if (seconds.has_value())
            seconds = juce::jlimit (0.0, juce::jmax (0.0, duration), *seconds);

        if (seconds == playhead)
            return;

        playhead = seconds;

        // Follow the playhead when zoomed in: page forward once it leaves the view.
        if (playhead.has_value() && hasSource() && visible.getLength() < duration - 1.0e-9
            && (*playhead > visible.getEnd() || *playhead < visible.getStart()))
            setVisibleRange (visible.movedToStartAt (*playhead));

        repaint();
    }

    double WaveformPanel::getMinVisibleLength() const
    {
        return juce::jmin (duration, minVisibleSeconds);
    }

    void WaveformPanel::setVisibleRange (juce::Range<double> r)
    {
        if (hasSource() && duration > 0.0)
        {
            const auto length = juce::jlimit (getMinVisibleLength(), duration, r.getLength());
            const auto start  = juce::jlimit (0.0, duration - length, r.getStart());
            r = { start, start + length };
        }

        if (r != visible)
        {
            visible = r;
            repaint();
        }

        updateScrollBar();
    }

    bool WaveformPanel::isLoading() const
    {
        return hasSource() && ! thumbnail.isFullyLoaded();
    }

    //==============================================================================
    void WaveformPanel::beginRecording (const juce::File& source, double rate, juce::int64 expectedLength)
    {
        {
            const std::scoped_lock lock (recordedLock);
            recorded.reset (1, rate, expectedLength);
            recordedLive = true;
            recordedSource = source;
            recordedFile = juce::File();
        }

        repaint();
    }

    void WaveformPanel::addRecordedSamples (juce::int64 start, const float* data, int numSamples)
    {
        const std::scoped_lock lock (recordedLock);

        if (! recordedLive || numSamples <= 0)
            return;

        // A non-owning view of the writer's block (addBlock only reads it).
        float* channels[] = { const_cast<float*> (data) };
        const juce::AudioBuffer<float> block (channels, 1, numSamples);
        recorded.addBlock (start, block, 0, numSamples);
    }

    void WaveformPanel::showRecordedFile (const juce::File& source, const juce::File& file)
    {
        {
            const std::scoped_lock lock (recordedLock);

            if (source == recordedSource && (recordedLive || file == recordedFile))
                return;   // already shown (the live thumbnail of the same take is as good)

            recordedLive = false;
            recordedSource = source;
            recordedFile = file;
            // Hashed with the file time: an overwritten take (same output path) is never shown
            // from the cache entry of the previous one (U4).
            recorded.setSource (new juce::FileInputSource (file, true));
        }

        repaint();
    }

    void WaveformPanel::clearRecorded()
    {
        {
            const std::scoped_lock lock (recordedLock);
            recordedLive = false;
            recordedSource = recordedFile = juce::File();
            recorded.clear();
        }

        repaint();
    }

    bool WaveformPanel::hasRecorded() const
    {
        return hasSource() && recordedSource == sourceFile && recorded.getNumChannels() > 0;
    }

    void WaveformPanel::updateScrollBar()
    {
        const auto zoomed = hasSource() && visible.getLength() < duration - 1.0e-9;

        if (zoomed)
        {
            scrollBar.setRangeLimits (0.0, duration, juce::dontSendNotification);
            scrollBar.setCurrentRange (visible.getStart(), visible.getLength(), juce::dontSendNotification);
        }

        scrollBar.setVisible (zoomed);
    }

    //==============================================================================
    double WaveformPanel::xToTime (float x) const
    {
        const auto w = (double) juce::jmax (1, sourceWaveArea.getWidth());
        return visible.getStart() + ((double) x - sourceWaveArea.getX()) / w * visible.getLength();
    }

    float WaveformPanel::timeToX (double t) const
    {
        const auto length = juce::jmax (1.0e-9, visible.getLength());
        return (float) sourceWaveArea.getX() + (float) ((t - visible.getStart()) / length * sourceWaveArea.getWidth());
    }

    void WaveformPanel::zoomAround (double factor, float anchorX)
    {
        if (! hasSource() || factor <= 0.0)
            return;

        const auto anchor = xToTime (anchorX);
        const auto length = juce::jlimit (getMinVisibleLength(), duration, visible.getLength() / factor);
        const auto start = anchor - (anchor - visible.getStart()) * (length / visible.getLength());
        setVisibleRange ({ start, start + length });
    }

    void WaveformPanel::setAuditionFromX (float x)
    {
        if (hasSource())
            setAuditionStart (xToTime (juce::jlimit ((float) sourceWaveArea.getX(), (float) sourceWaveArea.getRight(), x)));
    }

    //==============================================================================
    void WaveformPanel::mouseDown (const juce::MouseEvent& e)
    {
        if (e.x >= sourceWaveArea.getX() && e.y >= rulerArea.getY() && e.y < scrollArea.getY())
            setAuditionFromX ((float) e.x);
    }

    void WaveformPanel::mouseDrag (const juce::MouseEvent& e)
    {
        if (e.getMouseDownX() >= sourceWaveArea.getX() && e.getMouseDownY() >= rulerArea.getY()
            && e.getMouseDownY() < scrollArea.getY())
            setAuditionFromX ((float) e.x);
    }

    void WaveformPanel::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
    {
        if (! hasSource())
        {
            Component::mouseWheelMove (e, wheel);
            return;
        }

        if (e.mods.isCommandDown() || e.mods.isCtrlDown())
        {
            zoomAround (std::pow (2.0, (double) wheel.deltaY * 5.0), (float) e.x);
            return;
        }

        const auto zoomed = visible.getLength() < duration - 1.0e-9;

        if (! zoomed)
        {
            Component::mouseWheelMove (e, wheel);
            return;
        }

        const auto delta = std::abs (wheel.deltaX) > std::abs (wheel.deltaY) ? wheel.deltaX : wheel.deltaY;
        setVisibleRange (visible - (double) delta * visible.getLength());
    }

    void WaveformPanel::mouseMagnify (const juce::MouseEvent& e, float scaleFactor)
    {
        zoomAround ((double) scaleFactor, (float) e.x);
    }

    void WaveformPanel::scrollBarMoved (juce::ScrollBar*, double newRangeStart)
    {
        setVisibleRange (visible.movedToStartAt (newRangeStart));
    }

    void WaveformPanel::changeListenerCallback (juce::ChangeBroadcaster*)
    {
        repaint();
    }

    //==============================================================================
    void WaveformPanel::resized()
    {
        auto area = getLocalBounds();

        headerArea = area.removeFromTop (headerHeight);
        rulerArea  = area.removeFromTop (rulerHeight);
        scrollArea = area.removeFromBottom (metric::grid);

        auto source = area.removeFromTop (juce::roundToInt ((float) area.getHeight() * sourceLaneShare));
        area.removeFromTop (1); // separator
        auto recordedLane = area;

        sourceLabelArea   = source.removeFromLeft (laneLabelWidth);
        sourceWaveArea    = source;
        recordedLabelArea = recordedLane.removeFromLeft (laneLabelWidth);
        recordedWaveArea  = recordedLane;

        scrollBar.setBounds (scrollArea.withTrimmedLeft (laneLabelWidth));

        if (! hasSource())
            visible = { 0.0, juce::jmax (1.0, (double) sourceWaveArea.getWidth() / minMajorTickPx) };
    }

    void WaveformPanel::paint (juce::Graphics& g)
    {
        g.fillAll (colour::panel);

        paintHeader (g, headerArea);
        paintRuler (g, rulerArea);

        if (hasSource())
            paintSourceLane (g, sourceLabelArea, sourceWaveArea);
        else
            paintEmptyLane (g, sourceLabelArea, sourceWaveArea, "Source", "No file selected");

        g.setColour (colour::lineSoft);
        g.fillRect (sourceLabelArea.getX(), sourceLabelArea.getBottom(), getWidth(), 1);

        if (hasRecorded())
            paintRecordedLane (g, recordedLabelArea, recordedWaveArea);
        else
            paintEmptyLane (g, recordedLabelArea, recordedWaveArea, "Recorded",
                            hasSource() ? "Not recorded yet" : "The take shows here, under its source");

        // Label column continues down beside the scrollbar.
        g.setColour (colour::lineSoft);
        g.fillRect (laneLabelWidth - 1, scrollArea.getY(), 1, scrollArea.getHeight());

        paintMarker (g);
        paintPlayhead (g);
    }

    void WaveformPanel::paintHeader (juce::Graphics& g, juce::Rectangle<int> area) const
    {
        g.setColour (colour::line);
        g.fillRect (area.removeFromBottom (1));
        area.reduce (metric::sectionPadding, 0);

        const auto label = area.removeFromLeft (laneLabelWidth - metric::sectionPadding);
        drawSectionLabel (g, "Waveform", label);

        // While playing, the readout follows the playhead (accent) instead of the marker.
        const auto shown = playhead.has_value() ? *playhead : (hasSource() ? auditionStart : 0.0);
        const auto readout = format::time (shown) + " / " + format::time (duration);
        const auto readoutFont = Fonts::mono (type::controlSize, FontWeight::medium);
        const auto readoutWidth = juce::GlyphArrangement::getStringWidthInt (readoutFont, readout);

        g.setColour (playhead.has_value() ? colour::accent : colour::heading);
        g.setFont (readoutFont);
        g.drawText (readout, area.removeFromRight (readoutWidth), juce::Justification::centredRight, false);

        if (hasSource())
        {
            area.removeFromRight (metric::sectionPadding);
            g.setColour (colour::faint);
            g.setFont (Fonts::mono (type::controlSize));
            g.drawText (sourceFile.getFileName() + utf8 (" \xc2\xb7 ") + format::channels (numChannels)
                            + utf8 (" \xc2\xb7 ") + format::sampleRate (sampleRate),
                        area, juce::Justification::centredLeft, true);
        }
    }

    void WaveformPanel::paintRuler (juce::Graphics& g, juce::Rectangle<int> area) const
    {
        g.setColour (colour::lineSoft);
        g.fillRect (area.removeFromBottom (1));

        const auto wave = area.withLeft (sourceWaveArea.getX()).withRight (sourceWaveArea.getRight());

        if (wave.getWidth() <= 0 || visible.getLength() <= 0.0)
            return;

        const auto pxPerSecond = (double) wave.getWidth() / visible.getLength();

        auto major = tickSteps[std::size (tickSteps) - 1];

        for (auto step : tickSteps)
        {
            if (step * pxPerSecond >= minMajorTickPx)
            {
                major = step;
                break;
            }
        }

        const auto divisions = minorDivisions (major);
        const auto minor = major / divisions;
        const auto majorPx = (int) (major * pxPerSecond);

        g.setFont (Fonts::mono (type::rulerSize));
        g.setColour (colour::muted);

        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (wave);

        for (auto k = (juce::int64) std::floor (visible.getStart() / minor); ; ++k)
        {
            const auto t = (double) k * minor;

            if (t > visible.getEnd() + minor * 0.5)
                break;

            const auto x = juce::roundToInt (timeToX (t));
            const auto isMajor = (k % divisions) == 0;

            g.fillRect (x, area.getBottom() - (isMajor ? 8 : 4), 1, isMajor ? 8 : 4);

            if (isMajor)
            {
                // Only draw labels that fit completely; a clipped label reads as a wrong value.
                const auto text = tickLabel (t, major);
                const auto textWidth = juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), text);

                if (x + 4 + textWidth <= wave.getRight())
                    g.drawText (text, x + 4, area.getY(), juce::jmax (textWidth, majorPx - 8), area.getHeight() - 6,
                                juce::Justification::centredLeft, false);
            }
        }
    }

    void WaveformPanel::paintSourceLane (juce::Graphics& g, juce::Rectangle<int> label, juce::Rectangle<int> wave) const
    {
        g.setColour (colour::lineSoft);
        g.fillRect (label.removeFromRight (1));
        drawSectionLabel (g, "Source", label.reduced (metric::sectionPadding, metric::grid), juce::Justification::topLeft);

        const auto lanes = juce::jmax (1, juce::jmin (2, numChannels));
        const auto laneHeight = wave.getHeight() / lanes;

        for (int ch = 0; ch < lanes; ++ch)
        {
            const auto lane = ch == lanes - 1 ? wave : wave.removeFromTop (laneHeight);
            const auto isActive = lanes == 1 || ch == activeChannel;

            if (ch > 0)
            {
                g.setColour (colour::lineSoft);
                g.fillRect (label.getX(), lane.getY(), getWidth(), 1);
            }

            // Zero line.
            g.setColour (colour::lineSoft);
            g.fillRect (lane.withSizeKeepingCentre (lane.getWidth(), 1));

            g.setColour (isActive ? colour::text : colour::muted.withAlpha (0.35f));
            thumbnail.drawChannel (g, lane.reduced (0, 3), visible.getStart(), visible.getEnd(), ch, 1.0f);

            if (lanes == 2)
            {
                const auto letterArea = juce::Rectangle<int> (label.getRight() - metric::sectionPadding - 12, lane.getY(),
                                                              12, lane.getHeight());
                g.setFont (Fonts::mono (type::controlSize, FontWeight::bold));
                g.setColour (isActive ? colour::accent : colour::muted);
                g.drawText (ch == 0 ? "L" : "R", letterArea, juce::Justification::centredRight, false);
            }
        }

        if (! thumbnail.isFullyLoaded())
        {
            const auto percent = juce::roundToInt (thumbnail.getProportionComplete() * 100.0);
            g.setColour (colour::muted);
            g.setFont (Fonts::mono (type::rulerSize));
            g.drawText ("Building waveform " + juce::String (percent) + "%",
                        sourceWaveArea.reduced (metric::grid, 4), juce::Justification::topRight, false);
        }
    }

    void WaveformPanel::paintRecordedLane (juce::Graphics& g, juce::Rectangle<int> label, juce::Rectangle<int> wave) const
    {
        g.setColour (colour::lineSoft);
        g.fillRect (label.removeFromRight (1));
        drawSectionLabel (g, "Recorded", label.reduced (metric::sectionPadding, metric::grid), juce::Justification::topLeft);

        g.setColour (colour::lineSoft);
        g.fillRect (wave.withSizeKeepingCentre (wave.getWidth(), 1));

        g.setColour (colour::text);
        recorded.drawChannel (g, wave.reduced (0, 3), visible.getStart(), visible.getEnd(), 0, 1.0f);
    }

    void WaveformPanel::paintEmptyLane (juce::Graphics& g, juce::Rectangle<int> label, juce::Rectangle<int> wave,
                                        const juce::String& name, const juce::String& emptyText) const
    {
        g.setColour (colour::lineSoft);
        g.fillRect (label.removeFromRight (1));
        drawSectionLabel (g, name, label.reduced (metric::sectionPadding, metric::grid), juce::Justification::topLeft);

        // Zero line.
        g.setColour (colour::lineSoft);
        g.fillRect (wave.withSizeKeepingCentre (wave.getWidth(), 1));

        g.setColour (colour::muted);
        g.setFont (Fonts::mono (type::controlSize));
        g.drawText (emptyText, wave.withSizeKeepingCentre (wave.getWidth(), 20).translated (0, -14),
                    juce::Justification::centred, false);
    }

    void WaveformPanel::paintMarker (juce::Graphics& g) const
    {
        if (! hasSource() || auditionStart < visible.getStart() || auditionStart > visible.getEnd())
            return;

        const auto x = juce::roundToInt (timeToX (auditionStart));
        const auto top = rulerArea.getBottom() - markerFlagSize;

        g.setColour (colour::accent);
        g.fillRect (x, top, 1, recordedWaveArea.getBottom() - top);

        juce::Path flag;
        flag.addTriangle ((float) x - (float) markerFlagSize * 0.5f, (float) top - (float) markerFlagSize,
                          (float) x + (float) markerFlagSize * 0.5f + 1.0f, (float) top - (float) markerFlagSize,
                          (float) x + 0.5f, (float) top);
        g.fillPath (flag);
    }

    void WaveformPanel::paintPlayhead (juce::Graphics& g) const
    {
        if (! hasSource() || ! playhead.has_value() || *playhead < visible.getStart() || *playhead > visible.getEnd())
            return;

        const auto x = juce::roundToInt (timeToX (*playhead));
        const auto top = rulerArea.getY();

        g.setColour (colour::accent);
        g.fillRect (x - 1, top, 2, recordedWaveArea.getBottom() - top);
    }
}
