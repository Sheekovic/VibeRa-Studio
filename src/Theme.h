// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <juce_gui_extra/juce_gui_extra.h>
namespace vibera
{
// Logical pixels: a six-pixel thumb inside a reserved twelve-pixel input gutter.
inline constexpr int scrollBarThickness = 12;
// UI compositing clips children too; overlapping panels retain transparent corners.
class RoundedPanelEffect final : public juce::ImageEffectFilter
{
  public:
    static bool contains(int width, int height, int x, int y)
    {
        if (x < 0 || y < 0 || x >= width || y >= height)
            return false;
        const float radius = std::min(8.f, std::min(width, height) * .5f);
        const float px = x + .5f, py = y + .5f;
        const auto dx = px - juce::jlimit(radius, width - radius, px);
        const auto dy = py - juce::jlimit(radius, height - radius, py);
        return dx * dx + dy * dy <= radius * radius;
    }
    void applyEffect(juce::Image &source, juce::Graphics &graphics, float scale, float alpha) override
    {
        juce::Graphics::ScopedSaveState saved(graphics);
        juce::Path outline;
        outline.addRoundedRectangle(0, 0, float(source.getWidth()), float(source.getHeight()), 8.f * scale);
        graphics.reduceClipRegion(outline);
        graphics.setOpacity(alpha);
        graphics.drawImageAt(source, 0, 0);
    }
};
inline juce::Colour bg(0xff10191f), panel(0xff1a272f), grid(0xff2c3e48), ink(0xffe4eef2), accent(0xff29d7c5),
    orange(0xffeeb76a), muted(0xff95a9b4);
struct ThemeRole
{
    const char *name;
    juce::uint32 initial;
    juce::Colour *colour;
};
inline std::array<juce::Colour, 8> channelColours{juce::Colour(0xffdb6b85),
                                                  juce::Colour(0xffeaaa68),
                                                  juce::Colour(0xffe7c56c),
                                                  juce::Colour(0xff669ce3),
                                                  accent,
                                                  juce::Colour(0xffa48ce1),
                                                  juce::Colour(0xffc588cc),
                                                  juce::Colour(0xff9cabba)};
inline std::array<juce::Colour, 9> detailColours{
    juce::Colour(0xffd7e0e4), juce::Colour(0xff28363e), juce::Colour(0xff304052),
    juce::Colour(0xff1d2937), juce::Colour(0xff943c4b), juce::Colour(0xffdae5ee),
    juce::Colour(0xff18222d), juce::Colour(0xff303e4b), juce::Colour(0xff111923)};
inline const std::array<ThemeRole, 24> themeRoles{{{"Workspace background", 0xff10191f, &bg},
                                                   {"Panel background", 0xff1a272f, &panel},
                                                   {"Grid / borders", 0xff2c3e48, &grid},
                                                   {"Primary text", 0xffe4eef2, &ink},
                                                   {"Accent / playhead", 0xff29d7c5, &accent},
                                                   {"Selection / warning", 0xffeeb76a, &orange},
                                                   {"Secondary text", 0xff95a9b4, &muted},
                                                   {"Channel 1", 0xffdb6b85, &channelColours[0]},
                                                   {"Channel 2", 0xffeaaa68, &channelColours[1]},
                                                   {"Channel 3", 0xffe7c56c, &channelColours[2]},
                                                   {"Channel 4", 0xff669ce3, &channelColours[3]},
                                                   {"Channel 5", 0xff29d7c5, &channelColours[4]},
                                                   {"Channel 6", 0xffa48ce1, &channelColours[5]},
                                                   {"Channel 7", 0xffc588cc, &channelColours[6]},
                                                   {"Channel 8", 0xff9cabba, &channelColours[7]},
                                                   {"Piano white keys", 0xffd7e0e4, &detailColours[0]},
                                                   {"Piano black keys", 0xff28363e, &detailColours[1]},
                                                   {"Settings selected row", 0xff304052, &detailColours[2]},
                                                   {"Settings row", 0xff1d2937, &detailColours[3]},
                                                   {"Recording button", 0xff943c4b, &detailColours[4]},
                                                   {"Title controls", 0xffdae5ee, &detailColours[5]},
                                                   {"Title background", 0xff18222d, &detailColours[6]},
                                                   {"Title border", 0xff303e4b, &detailColours[7]},
                                                   {"Window background", 0xff111923, &detailColours[8]}}};
// Interface font (Settings > Appearance). Saved in the appearance profile; a font that is not
// installed falls back to the default when the profile loads.
inline const juce::String defaultUiFont = "Segoe UI";
inline juce::String uiFont = defaultUiFont;
// Built-in palettes, in themeRoles order. High contrast: black/white text at 21:1, light grey
// secondary text and borders, saturated accents (an accessibility preset, not a WCAG audit).
struct ThemePreset
{
    const char *name;
    std::array<juce::uint32, 24> colours;
};
inline const std::array<ThemePreset, 1> themePresets{{{"High contrast",
    {0xff000000, 0xff0a0a0a, 0xff505050, 0xffffffff, 0xff00e5ff, 0xffffd400, 0xffd6d6d6, 0xffff4d6d, 0xffff9f1c,
     0xffffe600, 0xff3d8bff, 0xff00e5ff, 0xffb388ff, 0xffff66d9, 0xffe0e0e0, 0xffffffff, 0xff000000, 0xff003a8c,
     0xff141414, 0xffd00000, 0xffffffff, 0xff000000, 0xffffffff, 0xff000000}}}};
inline void applyThemePreset(const ThemePreset &preset)
{
    for (size_t i = 0; i < themeRoles.size(); ++i)
        *themeRoles[i].colour = juce::Colour(preset.colours[i]);
}
inline juce::Colour themeColour(juce::uint32 initial)
{
    for (const auto &role : themeRoles)
        if (role.initial == initial)
            return *role.colour;
    return juce::Colour(initial);
}
inline void resetTheme()
{
    for (const auto &role : themeRoles)
        *role.colour = juce::Colour(role.initial);
    uiFont = defaultUiFont;
}
inline juce::File themeFile()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("VibeRa Studio/appearance.json");
}
inline juce::Result saveTheme(const juce::File &file)
{
    auto *object = new juce::DynamicObject;
    object->setProperty("version", 1);
    for (const auto &role : themeRoles)
        object->setProperty(role.name, role.colour->toString());
    object->setProperty("font", uiFont);
    if (file.getParentDirectory().createDirectory().failed() ||
        !file.replaceWithText(juce::JSON::toString(juce::var(object))))
        return juce::Result::fail("Cannot save appearance profile");
    return juce::Result::ok();
}
inline juce::Result loadTheme(const juce::File &file)
{
    auto values = juce::JSON::parse(file);
    if (!values.isObject() || int(values["version"]) != 1)
        return juce::Result::fail("Invalid appearance profile");
    std::array<juce::Colour, 24> next;
    for (size_t i = 0; i < themeRoles.size(); ++i)
    {
        auto text = values[themeRoles[i].name].toString();
        if (text.length() != 8 || !text.containsOnly("0123456789abcdefABCDEF"))
            return juce::Result::fail("Invalid appearance colour");
        next[i] = juce::Colour::fromString(text);
    }
    juce::String font = defaultUiFont;
    if (values.hasProperty("font"))
    {
        font = values["font"].toString().trim();
        if (font.isEmpty() || font.length() > 64)
            return juce::Result::fail("Invalid appearance font");
        if (!juce::Font::findAllTypefaceNames().contains(font, true))
            font = defaultUiFont; // Uninstalled since it was saved.
    }
    for (size_t i = 0; i < next.size(); ++i)
        *themeRoles[i].colour = next[i];
    uiFont = font;
    return juce::Result::ok();
}
inline juce::Colour channelColour(int index)
{
    return channelColours[size_t(juce::jmax(0, index) % 8)];
}
inline void label(juce::Graphics &g, const juce::String &s, juce::Rectangle<int> r, juce::Colour colour = ink,
                  float size = 13)
{
    g.setColour(colour);
    g.setFont(juce::Font(uiFont, size, juce::Font::plain));
    g.drawText(s, r, juce::Justification::centredLeft, true);
}
inline void drawSlimScrollbar(juce::Graphics &g, juce::ScrollBar &bar, int x, int y, int width, int height,
                              bool vertical, int start, int size, bool over, bool down)
{
    g.setColour(bg);
    g.fillRect(x, y, width, height);
    if (size <= 0)
        return;
    auto thumb =
        vertical
            ? juce::Rectangle<float>(float(x + 3), float(start), float(std::max(1, width - 6)), float(size))
            : juce::Rectangle<float>(float(start), float(y + 3), float(size), float(std::max(1, height - 6)));
    g.setColour(bar.findColour(juce::ScrollBar::thumbColourId).withAlpha(down ? 1.f : over ? .85f : .6f));
    g.fillRoundedRectangle(thumb, 3.f);
}
class WorkspaceLook final : public juce::LookAndFeel_V4
{
  public:
    int getDefaultScrollbarWidth() override
    {
        return scrollBarThickness;
    }
    bool areScrollbarButtonsVisible() override
    {
        return false;
    }
    void drawScrollbar(juce::Graphics &g, juce::ScrollBar &bar, int x, int y, int width, int height,
                       bool vertical, int start, int size, bool over, bool down) override
    {
        drawSlimScrollbar(g, bar, x, y, width, height, vertical, start, size, over, down);
    }
    juce::Font getAlertWindowTitleFont() override
    {
        return {uiFont, 18.f, juce::Font::bold};
    }
    juce::Font getAlertWindowMessageFont() override
    {
        return {uiFont, 13.f, juce::Font::plain};
    }
    juce::Font getAlertWindowFont() override
    {
        return {uiFont, 13.f, juce::Font::plain};
    }
    juce::Font getTextButtonFont(juce::TextButton &, int) override
    {
        return {uiFont, 13.f, juce::Font::plain};
    }
    void drawButtonBackground(juce::Graphics &g, juce::Button &b, const juce::Colour &, bool over,
                              bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced(.5f);
        const bool custom = b.isColourSpecified(juce::TextButton::buttonOnColourId);
        if (b.getToggleState() && !custom)
        {
            // Modern active state: a soft accent tint with an accent underline instead of a
            // saturated fill, so many active toggles never overpower the work area.
            auto tint = panel.interpolatedWith(accent, b.isEnabled() ? (over ? .24f : .17f) : .07f);
            if (down)
                tint = tint.brighter(.1f);
            g.setColour(tint);
            g.fillRoundedRectangle(r, 4.f);
            g.setColour(accent.withAlpha(b.isEnabled() ? .55f : .2f));
            g.drawRoundedRectangle(r, 4.f, 1.f);
            if (b.isEnabled() && r.getWidth() > 24)
            {
                g.setColour(accent);
                g.fillRoundedRectangle(r.getX() + 6.f, r.getBottom() - 3.f, r.getWidth() - 12.f, 2.f, 1.f);
            }
            return;
        }
        auto onColour = custom ? b.findColour(juce::TextButton::buttonOnColourId) : accent.darker(.55f);
        auto colour = b.getToggleState() ? onColour : panel.brighter(over ? .09f : .025f);
        if (down)
            colour = colour.brighter(.12f);
        if (!b.isEnabled() && b.getToggleState())
            colour = colour.interpolatedWith(panel, .6f); // A disabled primary action must not look clickable.
        g.setColour(colour);
        g.fillRoundedRectangle(r, 4.f);
        g.setColour(b.getToggleState() && b.isEnabled() ? onColour.brighter(.5f) : grid);
        g.drawRoundedRectangle(r, 4.f, 1.f);
    }
    void drawComboBox(juce::Graphics &g, int w, int h, bool, int, int, int, int, juce::ComboBox &box) override
    {
        g.setColour(bg);
        g.fillRoundedRectangle(.5f, .5f, w - 1.f, h - 1.f, 3.f);
        g.setColour(grid);
        g.drawRoundedRectangle(.5f, .5f, w - 1.f, h - 1.f, 3.f, 1.f);
        juce::Path arrow;
        arrow.startNewSubPath(w - 17.f, h * .44f);
        arrow.lineTo(w - 13.f, h * .58f);
        arrow.lineTo(w - 9.f, h * .44f);
        g.setColour(box.isEnabled() ? muted : grid);
        g.strokePath(arrow, juce::PathStrokeType(1.3f));
    }
    juce::Font getComboBoxFont(juce::ComboBox &) override
    {
        return {uiFont, 13.f, juce::Font::plain};
    }
    // Labels left on JUCE's default 15 px sans font use the app UI font, so captions match
    // buttons and combo boxes everywhere; labels with an explicit font keep it.
    juce::Font getLabelFont(juce::Label &label) override
    {
        const auto font = label.getFont();
        if (std::abs(font.getHeight() - 15.f) < .01f && !font.isBold() &&
            font.getTypefaceName() == juce::Font::getDefaultSansSerifFontName())
            return {uiFont, 13.f, juce::Font::plain};
        return font;
    }
    // Flat tabs (Settings and other tabbed pages): muted text, active tab in ink with an accent underline.
    int getTabButtonBestWidth(juce::TabBarButton &button, int) override
    {
        return std::max(64, juce::roundToInt(juce::GlyphArrangement::getStringWidth(
                                 juce::Font(uiFont, 13.f, juce::Font::plain), button.getButtonText())) +
                                28);
    }
    void drawTabButton(juce::TabBarButton &button, juce::Graphics &g, bool over, bool) override
    {
        const auto r = button.getActiveArea().toFloat();
        const bool front = button.isFrontTab();
        if (front || over)
        {
            g.setColour(front ? panel.brighter(.06f) : panel.brighter(.03f));
            g.fillRect(r);
        }
        if (front)
        {
            g.setColour(accent);
            g.fillRect(r.withTop(r.getBottom() - 2.f));
        }
        g.setColour(front ? ink : over ? ink.withAlpha(.85f) : muted);
        g.setFont(juce::Font(uiFont, 13.f, front ? juce::Font::bold : juce::Font::plain));
        g.drawText(button.getButtonText(), button.getTextArea(), juce::Justification::centred, false);
    }
    void drawTabbedButtonBarBackground(juce::TabbedButtonBar &bar, juce::Graphics &g) override
    {
        g.setColour(panel);
        g.fillRect(bar.getLocalBounds());
        g.setColour(grid);
        g.fillRect(bar.getLocalBounds().removeFromBottom(1));
    }
    void drawTabAreaBehindFrontButton(juce::TabbedButtonBar &, juce::Graphics &, int, int) override {}
    juce::Font getPopupMenuFont() override
    {
        return {uiFont, 14.f, juce::Font::plain};
    }
    // File trees (sample browser): compact rows, small line icons and the app font instead of
    // JUCE's large text and bitmap folder icons.
    void drawFileBrowserRow(juce::Graphics &g, int width, int height, const juce::File &file,
                            const juce::String &filename, juce::Image *, const juce::String &, const juce::String &,
                            bool isDirectory, bool isItemSelected, int, juce::DirectoryContentsDisplayComponent &) override
    {
        if (isItemSelected)
        {
            g.setColour(accent.withAlpha(.18f));
            g.fillRoundedRectangle(0.f, 1.f, float(width), float(height - 2), 3.f);
        }
        const float s = std::min(16.f, height - 8.f), x = 4.f, y = (height - s) * .5f;
        juce::Path icon;
        if (isDirectory)
        {
            icon.startNewSubPath(x, y + s * .9f);
            icon.lineTo(x, y + s * .15f);
            icon.lineTo(x + s * .38f, y + s * .15f);
            icon.lineTo(x + s * .5f, y + s * .3f);
            icon.lineTo(x + s, y + s * .3f);
            icon.lineTo(x + s, y + s * .9f);
            icon.closeSubPath();
            g.setColour(orange.withAlpha(.85f));
            g.strokePath(icon, juce::PathStrokeType(1.3f));
        }
        else
        {
            const bool audio = file.hasFileExtension("wav;aif;aiff;flac;mp3;ogg");
            const float heights[]{.35f, .8f, .55f, 1.f, .45f};
            g.setColour(audio ? accent : muted);
            for (int i = 0; i < 5; ++i)
            {
                const float h = s * .8f * heights[i];
                g.fillRoundedRectangle(x + 1.f + i * s * .19f, y + (s - h) * .5f, s * .1f, h, 1.f);
            }
        }
        g.setColour(isItemSelected ? ink : ink.withAlpha(.88f));
        g.setFont(juce::Font(uiFont, 13.f, juce::Font::plain));
        g.drawFittedText(filename, int(x + s + 8.f), 0, width - int(x + s + 10.f), height,
                         juce::Justification::centredLeft, 1, 1.f);
    }
    void drawTreeviewPlusMinusBox(juce::Graphics &g, const juce::Rectangle<float> &area, juce::Colour, bool isOpen,
                                  bool isMouseOver) override
    {
        juce::Path chevron;
        const auto c = area.getCentre();
        if (isOpen)
        {
            chevron.startNewSubPath(c.x - 3.5f, c.y - 1.5f);
            chevron.lineTo(c.x, c.y + 2.f);
            chevron.lineTo(c.x + 3.5f, c.y - 1.5f);
        }
        else
        {
            chevron.startNewSubPath(c.x - 1.5f, c.y - 3.5f);
            chevron.lineTo(c.x + 2.f, c.y);
            chevron.lineTo(c.x - 1.5f, c.y + 3.5f);
        }
        g.setColour(isMouseOver ? ink : muted);
        g.strokePath(chevron, juce::PathStrokeType(1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    void drawToggleButton(juce::Graphics &g, juce::ToggleButton &button, bool over, bool) override
    {
        const auto bounds = button.getLocalBounds().toFloat();
        const float side = std::min(16.f, bounds.getHeight() - 4.f);
        const juce::Rectangle<float> box(3.f, (bounds.getHeight() - side) * .5f, side, side);
        const bool on = button.getToggleState(), enabled = button.isEnabled();
        g.setColour(on ? accent.withMultipliedAlpha(enabled ? 1.f : .4f) : bg);
        g.fillRoundedRectangle(box, 4.f);
        g.setColour(on ? accent : over && enabled ? muted : grid.brighter(.25f));
        g.drawRoundedRectangle(box.reduced(.5f), 4.f, 1.f);
        if (on)
        {
            juce::Path check;
            check.startNewSubPath(box.getX() + side * .25f, box.getCentreY());
            check.lineTo(box.getX() + side * .43f, box.getY() + side * .7f);
            check.lineTo(box.getX() + side * .76f, box.getY() + side * .3f);
            g.setColour(bg);
            g.strokePath(check, juce::PathStrokeType(1.9f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        g.setColour(button.findColour(juce::ToggleButton::textColourId).withMultipliedAlpha(enabled ? 1.f : .45f));
        g.setFont(juce::Font(uiFont, 13.f, juce::Font::plain));
        g.drawFittedText(button.getButtonText(), bounds.withTrimmedLeft(box.getRight() + 8.f).toNearestInt(),
                         juce::Justification::centredLeft, 2, 1.f);
    }
    void fillTextEditorBackground(juce::Graphics &g, int width, int height, juce::TextEditor &editor) override
    {
        if (dynamic_cast<juce::AlertWindow *>(editor.getParentComponent()) != nullptr)
        {
            LookAndFeel_V4::fillTextEditorBackground(g, width, height, editor);
            return;
        }
        g.setColour(editor.findColour(juce::TextEditor::backgroundColourId));
        g.fillRoundedRectangle(0.f, 0.f, float(width), float(height), 3.f);
    }
    void drawTextEditorOutline(juce::Graphics &g, int width, int height, juce::TextEditor &editor) override
    {
        if (!editor.isEnabled())
            return;
        const bool focused = editor.hasKeyboardFocus(true) && !editor.isReadOnly();
        g.setColour(editor.findColour(focused ? juce::TextEditor::focusedOutlineColourId
                                              : juce::TextEditor::outlineColourId));
        g.drawRoundedRectangle(.5f, .5f, width - 1.f, height - 1.f, 3.f, 1.f);
    }
};
} // namespace vibera
