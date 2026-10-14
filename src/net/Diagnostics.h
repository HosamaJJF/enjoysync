#pragma once
#include <juce_core/juce_core.h>
#include <iostream>

namespace enjoysync {
// Event metadata only: never log pairing keys, control payloads, or audio.
inline void logNetwork(const juce::String& detail) {
    const auto line = juce::Time::getCurrentTime().toISO8601(true) + " " + detail;
    if (juce::Logger::getCurrentLogger()) juce::Logger::writeToLog(line);
    static const bool trace = juce::SystemStats::getEnvironmentVariable("ENJOYSYNC_TRACE", {}).isNotEmpty();
    if (trace) std::cerr << line << "\n";
}
}
