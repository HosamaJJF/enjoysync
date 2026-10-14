#pragma once
#include <juce_gui_extra/juce_gui_extra.h>

namespace enjoysync {
juce::Font uiFont(float size, bool bold = false);

struct Palette {
    juce::Colour background, surface, field, text, secondary, outline, accent, accentSurface, danger;
};

class Appearance final : public juce::LookAndFeel_V4, private juce::DarkModeSettingListener {
public:
    Appearance();
    ~Appearance() override;
    void setMode(const juce::String&);
    const Palette& colours() const { return palette; }
    juce::Font getTextButtonFont(juce::TextButton&, int) override;
    juce::Font getComboBoxFont(juce::ComboBox&) override;
    juce::Font getPopupMenuFont() override;
    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&, bool, bool) override;
    void fillTextEditorBackground(juce::Graphics&, int, int, juce::TextEditor&) override;
    void drawTextEditorOutline(juce::Graphics&, int, int, juce::TextEditor&) override;
    void drawComboBox(juce::Graphics&, int, int, bool, int, int, int, int, juce::ComboBox&) override;
    void positionComboBoxText(juce::ComboBox&, juce::Label&) override;
    void drawLinearSlider(juce::Graphics&, int, int, int, int, float, float, float,
                          juce::Slider::SliderStyle, juce::Slider&) override;
private:
    void darkModeSettingChanged() override;
    void update();
    juce::String mode {"system"};
    Palette palette;
};

enum class Icon { receive, speaker, muted, share, automatic, disconnect, reconnect, remove, settings, copy, device };
void drawIcon(juce::Graphics&, Icon, juce::Rectangle<float>, juce::Colour);

class IconButton final : public juce::Button {
public:
    IconButton(Appearance&, Icon, const juce::String& title, bool toggle = false, bool destructive = false);
    void setIcon(Icon, const juce::String& title);
    void paintButton(juce::Graphics&, bool, bool) override;
private:
    Appearance& appearance;
    Icon icon;
    bool destructive;
};

void styleEditor(juce::TextEditor&, const Palette&, const juce::String& placeholder = {});
void setupEditor(juce::TextEditor&, const juce::String& value, const juce::String& title, bool numeric = false);
void setLabel(juce::Label&, const juce::String&, float size = 13, bool bold = false);
void drawPanel(juce::Graphics&, juce::Rectangle<int>, const Palette&);
}
