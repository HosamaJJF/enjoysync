#pragma once
#include <juce_graphics/juce_graphics.h>

namespace enjoysync {
inline juce::Image createTrayIcon() {
    // Native Windows images use deferred GPU rendering. Keep this tiny icon in
    // CPU memory, and finish drawing before JUCE copies it into a Win32 HICON.
    juce::Image icon(juce::Image::ARGB, 64, 64, true, juce::SoftwareImageType{});
    {
        juce::Graphics graphics(icon);
        graphics.setColour(juce::Colour(0xff71e7bd));
        for (int i = 0; i < 5; ++i) {
            const float height = i == 2 ? 48.0f : (i == 0 || i == 4 ? 18.0f : 34.0f);
            graphics.fillRoundedRectangle(8.0f + i * 10.0f, (64 - height) / 2, 6, height, 3);
        }
    }
    return icon;
}
}
