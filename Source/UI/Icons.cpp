#include "Icons.h"

namespace stemsplitter::Icons
{

namespace
{
    /** Turns a stroked outline into a fillable shape, so every icon can be drawn with fillPath. */
    juce::Path stroked (const juce::Path& p, float thickness = 2.0f)
    {
        juce::Path out;
        juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded).createStrokedPath (out, p);
        return out;
    }

    /** All icons are designed on a 24x24 grid; this pins the bounds so they scale consistently. */
    juce::Path onGrid (juce::Path p)
    {
        p.startNewSubPath (0.0f, 0.0f);
        p.closeSubPath();
        p.startNewSubPath (24.0f, 24.0f);
        p.closeSubPath();
        return p;
    }
}

juce::Path play()
{
    juce::Path p;
    p.addTriangle (7.0f, 4.5f, 7.0f, 19.5f, 19.5f, 12.0f);
    return onGrid (p.createPathWithRoundedCorners (1.5f));
}

juce::Path pause()
{
    juce::Path p;
    p.addRoundedRectangle (6.0f, 5.0f, 4.0f, 14.0f, 1.2f);
    p.addRoundedRectangle (14.0f, 5.0f, 4.0f, 14.0f, 1.2f);
    return onGrid (p);
}

juce::Path stop()
{
    juce::Path p;
    p.addRoundedRectangle (6.5f, 6.5f, 11.0f, 11.0f, 2.0f);
    return onGrid (p);
}

juce::Path loop()
{
    juce::Path p;
    p.startNewSubPath (5.0f, 12.0f);
    p.lineTo (5.0f, 10.0f);
    p.quadraticTo (5.0f, 7.0f, 8.0f, 7.0f);
    p.lineTo (18.0f, 7.0f);
    p.startNewSubPath (15.5f, 4.5f);
    p.lineTo (18.0f, 7.0f);
    p.lineTo (15.5f, 9.5f);
    p.startNewSubPath (19.0f, 12.0f);
    p.lineTo (19.0f, 14.0f);
    p.quadraticTo (19.0f, 17.0f, 16.0f, 17.0f);
    p.lineTo (6.0f, 17.0f);
    p.startNewSubPath (8.5f, 14.5f);
    p.lineTo (6.0f, 17.0f);
    p.lineTo (8.5f, 19.5f);
    return onGrid (stroked (p, 1.8f));
}

juce::Path download()
{
    juce::Path p;
    p.startNewSubPath (12.0f, 4.0f);
    p.lineTo (12.0f, 15.0f);
    p.startNewSubPath (7.5f, 10.5f);
    p.lineTo (12.0f, 15.0f);
    p.lineTo (16.5f, 10.5f);
    p.startNewSubPath (5.0f, 19.5f);
    p.lineTo (19.0f, 19.5f);
    return onGrid (stroked (p));
}

juce::Path exportArrow()
{
    juce::Path p;
    p.startNewSubPath (12.0f, 15.0f);
    p.lineTo (12.0f, 4.0f);
    p.startNewSubPath (7.5f, 8.5f);
    p.lineTo (12.0f, 4.0f);
    p.lineTo (16.5f, 8.5f);
    p.startNewSubPath (5.0f, 13.0f);
    p.lineTo (5.0f, 19.0f);
    p.lineTo (19.0f, 19.0f);
    p.lineTo (19.0f, 13.0f);
    return onGrid (stroked (p));
}

juce::Path importArrow()
{
    juce::Path p;
    p.startNewSubPath (12.0f, 3.5f);
    p.lineTo (12.0f, 14.0f);
    p.startNewSubPath (7.5f, 9.5f);
    p.lineTo (12.0f, 14.0f);
    p.lineTo (16.5f, 9.5f);
    p.startNewSubPath (4.5f, 13.5f);
    p.lineTo (4.5f, 19.5f);
    p.lineTo (19.5f, 19.5f);
    p.lineTo (19.5f, 13.5f);
    return onGrid (stroked (p, 1.6f));
}

juce::Path folder()
{
    juce::Path p;
    p.startNewSubPath (3.5f, 7.0f);
    p.lineTo (3.5f, 18.0f);
    p.lineTo (20.5f, 18.0f);
    p.lineTo (20.5f, 8.5f);
    p.lineTo (11.5f, 8.5f);
    p.lineTo (9.5f, 6.0f);
    p.lineTo (3.5f, 6.0f);
    p.closeSubPath();
    return onGrid (stroked (p, 1.8f));
}

juce::Path settings()
{
    juce::Path p;
    // three horizontal sliders: a clean, minimal "settings" glyph
    const float ys[] = { 6.5f, 12.0f, 17.5f };
    const float knobs[] = { 15.0f, 8.5f, 13.0f };

    for (int i = 0; i < 3; ++i)
    {
        juce::Path line;
        line.startNewSubPath (4.0f, ys[i]);
        line.lineTo (20.0f, ys[i]);
        p.addPath (stroked (line, 1.6f));
        p.addEllipse (knobs[i] - 2.5f, ys[i] - 2.5f, 5.0f, 5.0f);
    }

    return onGrid (p);
}

juce::Path close()
{
    juce::Path p;
    p.startNewSubPath (7.0f, 7.0f);
    p.lineTo (17.0f, 17.0f);
    p.startNewSubPath (17.0f, 7.0f);
    p.lineTo (7.0f, 17.0f);
    return onGrid (stroked (p));
}

juce::Path dragHandle()
{
    juce::Path p;

    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 2; ++c)
            p.addEllipse (8.5f + (float) c * 5.0f, 6.0f + (float) r * 5.0f, 2.6f, 2.6f);

    return onGrid (p);
}

juce::Path speaker()
{
    juce::Path p;
    p.startNewSubPath (4.0f, 9.5f);
    p.lineTo (7.5f, 9.5f);
    p.lineTo (12.0f, 5.5f);
    p.lineTo (12.0f, 18.5f);
    p.lineTo (7.5f, 14.5f);
    p.lineTo (4.0f, 14.5f);
    p.closeSubPath();
    juce::Path wave;
    wave.startNewSubPath (15.5f, 9.0f);
    wave.quadraticTo (17.5f, 12.0f, 15.5f, 15.0f);
    p.addPath (stroked (wave, 1.6f));
    return onGrid (p);
}

juce::Path logo()
{
    // Four rounded bars of different heights: a waveform being split into stems
    juce::Path p;
    const float heights[] = { 10.0f, 18.0f, 13.0f, 7.0f };

    for (int i = 0; i < 4; ++i)
    {
        const float h = heights[i];
        p.addRoundedRectangle (3.0f + (float) i * 5.0f, 12.0f - h * 0.5f, 3.0f, h, 1.5f);
    }

    return onGrid (p);
}

juce::Path check()
{
    juce::Path p;
    p.startNewSubPath (5.5f, 12.5f);
    p.lineTo (10.0f, 17.0f);
    p.lineTo (18.5f, 7.5f);
    return onGrid (stroked (p, 2.2f));
}

juce::Path waveform()
{
    juce::Path p;
    const float heights[] = { 4.0f, 9.0f, 14.0f, 8.0f, 12.0f, 6.0f, 3.0f };

    for (int i = 0; i < 7; ++i)
    {
        juce::Path line;
        const float x = 3.0f + (float) i * 3.0f;
        line.startNewSubPath (x, 12.0f - heights[i] * 0.5f);
        line.lineTo (x, 12.0f + heights[i] * 0.5f);
        p.addPath (stroked (line, 1.6f));
    }

    return onGrid (p);
}

} // namespace stemsplitter::Icons
