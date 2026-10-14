#pragma once
#include "Appearance.h"
#include "Controller.h"

namespace enjoysync {
class SettingsWindow final : public juce::DocumentWindow {
public:
    SettingsWindow(Controller&, Appearance&);
    void closeButtonPressed() override;
    bool keyPressed(const juce::KeyPress&) override;
};
}
