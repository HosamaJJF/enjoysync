#include "Appearance.h"
#include "core/Settings.h"
#include "platform/Capture.h"

namespace enjoysync {
juce::Font uiFont(float size, bool bold) {
   #if JUCE_MAC
    return juce::Font(juce::FontOptions("PingFang SC", size, bold ? juce::Font::bold : juce::Font::plain));
   #else
    return juce::Font(juce::FontOptions("Microsoft YaHei UI", size, bold ? juce::Font::bold : juce::Font::plain));
   #endif
}

Appearance::Appearance() {
    update();
    juce::Desktop::getInstance().addDarkModeSettingListener(this);
}
Appearance::~Appearance() { juce::Desktop::getInstance().removeDarkModeSettingListener(this); }
void Appearance::setMode(const juce::String& value) { mode = value; update(); }
void Appearance::darkModeSettingChanged() { if (mode == "system") update(); }
void Appearance::update() {
    const bool dark = mode == "dark" || (mode == "system" && juce::Desktop::getInstance().isDarkModeActive());
    palette = dark
        ? Palette {juce::Colour(0xff14191c), juce::Colour(0xff1d2428), juce::Colour(0xff171e21),
                   juce::Colour(0xffe9efed), juce::Colour(0xff98a6a2), juce::Colour(0xff333f40),
                   juce::Colour(0xff78d9b2), juce::Colour(0xff293e36), juce::Colour(0xfff08d89)}
        : Palette {juce::Colour(0xfff3f5f4), juce::Colour(0xffffffff), juce::Colour(0xfff7f9f8),
                   juce::Colour(0xff202c27), juce::Colour(0xff64756d), juce::Colour(0xffdce4df),
                   juce::Colour(0xff237858), juce::Colour(0xffe5f2eb), juce::Colour(0xffbe4945)};
    const auto& p = palette;
    setColourScheme({p.background, p.field, p.surface, p.outline, p.text, p.accent,
                     p.text, p.accentSurface, p.text});
    setColour(juce::ResizableWindow::backgroundColourId, p.background);
    setColour(juce::Label::textColourId, p.text);
    setColour(juce::TextButton::buttonColourId, p.accentSurface);
    setColour(juce::TextButton::textColourOffId, p.accent);
    setColour(juce::ToggleButton::textColourId, p.text);
    setColour(juce::ToggleButton::tickColourId, p.accent);
    setColour(juce::ToggleButton::tickDisabledColourId, p.secondary);
    setColour(juce::TextEditor::backgroundColourId, p.field);
    setColour(juce::TextEditor::textColourId, p.text);
    setColour(juce::TextEditor::outlineColourId, p.outline);
    setColour(juce::TextEditor::focusedOutlineColourId, p.accent);
    setColour(juce::TextEditor::highlightColourId, p.accent.withAlpha(0.25f));
    setColour(juce::TextEditor::highlightedTextColourId, p.text);
    setColour(juce::CaretComponent::caretColourId, p.accent);
    setColour(juce::ComboBox::backgroundColourId, p.field);
    setColour(juce::ComboBox::textColourId, p.text);
    setColour(juce::ComboBox::arrowColourId, p.secondary);
    setColour(juce::Slider::thumbColourId, p.accent);
    setColour(juce::Slider::trackColourId, p.accent);
    setColour(juce::Slider::backgroundColourId, p.outline);
    setColour(juce::ScrollBar::thumbColourId, p.secondary.withAlpha(0.3f));
    setColour(juce::TooltipWindow::backgroundColourId, p.surface);
    setColour(juce::TooltipWindow::textColourId, p.text);
    setColour(juce::TooltipWindow::outlineColourId, p.outline);
    auto& desktop = juce::Desktop::getInstance();
    for (int i = 0; i < desktop.getNumComponents(); ++i)
        desktop.getComponent(i)->sendLookAndFeelChange();
    setApplicationTheme(mode);
}
juce::Font Appearance::getTextButtonFont(juce::TextButton&, int) { return uiFont(13, true); }
juce::Font Appearance::getComboBoxFont(juce::ComboBox&) { return uiFont(13); }
juce::Font Appearance::getPopupMenuFont() { return uiFont(13); }
void Appearance::drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour& colour, bool over, bool down) {
    auto area = button.getLocalBounds().toFloat().reduced(0.5f);
    g.setColour(colour.interpolatedWith(palette.accent, down ? 0.18f : over ? 0.08f : 0.0f));
    g.fillRoundedRectangle(area, 8);
    if (button.hasKeyboardFocus(false)) { g.setColour(palette.accent); g.drawRoundedRectangle(area, 8, 1.5f); }
}
void Appearance::fillTextEditorBackground(juce::Graphics& g, int w, int h, juce::TextEditor&) {
    g.setColour(palette.field); g.fillRoundedRectangle(0.5f, 0.5f, (float)w - 1, (float)h - 1, 8);
}
void Appearance::drawTextEditorOutline(juce::Graphics& g, int w, int h, juce::TextEditor& editor) {
    g.setColour(editor.hasKeyboardFocus(true) ? palette.accent : palette.outline);
    g.drawRoundedRectangle(0.5f, 0.5f, (float)w - 1, (float)h - 1, 8, editor.hasKeyboardFocus(true) ? 1.5f : 1.0f);
}
void Appearance::drawComboBox(juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box) {
    auto area = juce::Rectangle<float>(0.5f, 0.5f, (float)w - 1, (float)h - 1);
    g.setColour(palette.field); g.fillRoundedRectangle(area, 8);
    g.setColour(box.hasKeyboardFocus(true) ? palette.accent : palette.outline); g.drawRoundedRectangle(area, 8, 1);
    juce::Path arrow; arrow.startNewSubPath((float)w - 23, (float)h / 2 - 2);
    arrow.lineTo((float)w - 19, (float)h / 2 + 2); arrow.lineTo((float)w - 15, (float)h / 2 - 2);
    g.setColour(palette.secondary); g.strokePath(arrow, juce::PathStrokeType(1.5f));
}
void Appearance::positionComboBoxText(juce::ComboBox& box, juce::Label& item) {
    item.setBounds(10, 0, box.getWidth() - 38, box.getHeight()); item.setFont(uiFont(13));
}
void Appearance::drawLinearSlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float, float,
                                 juce::Slider::SliderStyle, juce::Slider& slider) {
    const auto mid = (float)y + (float)h / 2;
    g.setColour(palette.outline); g.fillRoundedRectangle((float)x, mid - 2, (float)w, 4, 2);
    g.setColour(palette.accent); g.fillRoundedRectangle((float)x, mid - 2, std::max(0.0f, pos - (float)x), 4, 2);
    if (slider.isMouseOverOrDragging()) { g.setColour(palette.accent.withAlpha(0.12f)); g.fillEllipse(pos - 10, mid - 10, 20, 20); }
    g.setColour(palette.accent); g.fillEllipse(pos - 5, mid - 5, 10, 10);
}

void drawIcon(juce::Graphics& g, Icon icon, juce::Rectangle<float> area, juce::Colour colour) {
    juce::Path path;
    auto line = [&](float x, float y, float endX, float endY) { path.startNewSubPath(x, y); path.lineTo(endX, endY); };
    switch (icon) {
        case Icon::receive:
        case Icon::share:
            line(5, 16, 5, 20); path.lineTo(19, 20); path.lineTo(19, 16);
            if (icon == Icon::receive) { line(12, 3, 12, 14); line(7, 9, 12, 14); path.lineTo(17, 9); }
            else { line(12, 15, 12, 4); line(7, 9, 12, 4); path.lineTo(17, 9); }
            break;
        case Icon::speaker:
        case Icon::muted:
            path.startNewSubPath(3, 9); path.lineTo(7, 9); path.lineTo(12, 5); path.lineTo(12, 19);
            path.lineTo(7, 15); path.lineTo(3, 15); path.closeSubPath();
            if (icon == Icon::speaker) {
                path.startNewSubPath(16, 8); path.quadraticTo(20, 12, 16, 16);
                path.startNewSubPath(19, 5); path.quadraticTo(25, 12, 19, 19);
            } else { line(16, 9, 22, 15); line(22, 9, 16, 15); }
            break;
        case Icon::automatic:
        case Icon::reconnect:
            path.startNewSubPath(20, 9); path.cubicTo(18, 2, 7, 2, 4, 9);
            line(20, 4, 20, 9); path.lineTo(15, 9);
            path.startNewSubPath(4, 15); path.cubicTo(6, 22, 17, 22, 20, 15);
            line(4, 20, 4, 15); path.lineTo(9, 15);
            if (icon == Icon::automatic) { line(13, 9, 10, 13); path.lineTo(14, 13); path.lineTo(11, 16); }
            break;
        case Icon::disconnect:
            path.startNewSubPath(10, 6); path.cubicTo(6, 2, 1, 8, 5, 12); path.lineTo(7, 14);
            path.startNewSubPath(14, 18); path.cubicTo(18, 22, 23, 16, 19, 12); path.lineTo(17, 10);
            line(3, 21, 21, 3);
            break;
        case Icon::remove:
            line(4, 6, 20, 6); line(9, 6, 9, 3); path.lineTo(15, 3); path.lineTo(15, 6);
            path.startNewSubPath(6, 6); path.lineTo(7, 21); path.lineTo(17, 21); path.lineTo(18, 6);
            line(10, 10, 10, 17); line(14, 10, 14, 17);
            break;
        case Icon::settings:
            for (int i = 0; i < 8; ++i) {
                const float angle = (float)i * juce::MathConstants<float>::twoPi / 8;
                const float x = std::sin(angle), y = std::cos(angle);
                line(12 + x * 8, 12 + y * 8, 12 + x * 10, 12 + y * 10);
            }
            path.addEllipse(5, 5, 14, 14); path.addEllipse(9, 9, 6, 6);
            break;
        case Icon::copy:
            path.addRoundedRectangle(8, 8, 12, 13, 2);
            path.startNewSubPath(15, 4); path.lineTo(5, 4); path.lineTo(5, 16);
            break;
        case Icon::device:
            path.addRoundedRectangle(3, 4, 18, 13, 2); line(12, 17, 12, 21); line(8, 21, 16, 21);
            break;
    }
    g.setColour(colour);
    g.strokePath(path, juce::PathStrokeType(1.65f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded),
                 juce::AffineTransform::scale(area.getWidth() / 24, area.getHeight() / 24).translated(area.getX(), area.getY()));
}
IconButton::IconButton(Appearance& a, Icon i, const juce::String& title, bool toggle, bool danger)
    : juce::Button(title), appearance(a), icon(i), destructive(danger) {
    setClickingTogglesState(toggle); setIcon(i, title); setMouseCursor(juce::MouseCursor::PointingHandCursor);
}
void IconButton::setIcon(Icon value, const juce::String& title) {
    icon = value; setName(title); setTitle(title); setButtonText(title); setTooltip(title); repaint();
}
void IconButton::paintButton(juce::Graphics& g, bool over, bool down) {
    const auto& p = appearance.colours();
    auto colour = destructive && over ? p.danger : getToggleState() ? p.accent : p.secondary;
    auto bounds = getLocalBounds().toFloat().reduced(1);
    if (getToggleState() || over || down) {
        g.setColour(destructive && over ? p.danger.withAlpha(0.1f) : getToggleState() ? p.accentSurface : p.secondary.withAlpha(down ? 0.16f : 0.08f));
        g.fillRoundedRectangle(bounds, 8);
    }
    if (hasKeyboardFocus(false)) { g.setColour(p.accent); g.drawRoundedRectangle(bounds, 8, 1.5f); }
    drawIcon(g, icon, bounds.withSizeKeepingCentre(20, 20), colour.withMultipliedAlpha(isEnabled() ? 1.0f : 0.35f));
}
void styleEditor(juce::TextEditor& editor, const Palette& palette, const juce::String& placeholder) {
    editor.applyColourToAllText(palette.text, true);
    editor.setTextToShowWhenEmpty(placeholder, palette.secondary);
}
void setupEditor(juce::TextEditor& editor, const juce::String& value, const juce::String& title, bool numeric) {
    editor.setFont(uiFont(13)); editor.setText(value); editor.setTitle(title); editor.setIndents(12, 10);
    editor.setSelectAllWhenFocused(true);
    if (numeric) editor.setInputRestrictions(5, "0123456789");
}
void setLabel(juce::Label& item, const juce::String& text, float size, bool bold) {
    item.setText(text, juce::dontSendNotification); item.setFont(uiFont(size, bold)); item.setBorderSize(juce::BorderSize<int>(0));
}
void drawPanel(juce::Graphics& g, juce::Rectangle<int> bounds, const Palette& palette) {
    const auto area = bounds.toFloat().reduced(0.5f);
    g.setColour(palette.surface); g.fillRoundedRectangle(area, 12);
    g.setColour(palette.outline.withAlpha(0.7f)); g.drawRoundedRectangle(area, 12, 1);
}
}
