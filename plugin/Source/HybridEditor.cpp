#include "HybridEditor.h"

#ifndef ZYV_BUILD_LABEL
 #define ZYV_BUILD_LABEL "dev"
#endif

namespace zyv
{

namespace Theme
{
constexpr auto bgDark         = 0xff0f1013;
constexpr auto headerBg       = 0xff14161b;
constexpr auto panelBg        = 0xff15171d;
constexpr auto cardBg         = 0xff1b1e25;
constexpr auto cardSelected   = 0xff162728;
constexpr auto cardBypassed   = 0xff131418;
constexpr auto borderSubtle   = 0xff262a35;
constexpr auto borderActive   = 0xff3b4354;
constexpr auto accentTeal     = 0xff00e5c0;
constexpr auto accentTealDim  = 0x3300e5c0;
constexpr auto accentAmber    = 0xffffa133;
constexpr auto accentRed      = 0xffff4d5a;
constexpr auto textPrimary    = 0xfff4f5f8;
constexpr auto textSecondary  = 0xff8c96a8;
constexpr auto textDim        = 0xff545c6e;

/** One control type scale for the whole editor chrome. */
constexpr float uiFontHeight  = 13.0f;
} // namespace Theme

namespace
{
constexpr const char* kZyMarkSvg =
    R"svg(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 44 20">)svg"
    R"svg(<rect x="0.75" y="0.75" width="18.5" height="18.5" rx="3" fill="#35262b" stroke="#f06470" stroke-width="1.5"/>)svg"
    R"svg(<path d="M4.5 5h11l-11 10h11" fill="none" stroke="#f06470" stroke-width="2.25" stroke-linecap="square" stroke-linejoin="miter"/>)svg"
    R"svg(<rect x="24.75" y="0.75" width="18.5" height="18.5" rx="3" fill="#19322f" stroke="#57d4c2" stroke-width="1.5"/>)svg"
    R"svg(<path d="m28.5 5 5.5 5 5.5-5M34 10v5" fill="none" stroke="#57d4c2" stroke-width="2.25" stroke-linecap="square" stroke-linejoin="miter"/>)svg"
    R"svg(</svg>)svg";
class Vst3Picker final : public juce::Component,
                         private juce::ListBoxModel,
                         private juce::KeyListener
{
public:
    struct Entry
    {
        juce::File bundle;
        juce::String name;
        juce::String vendor;
    };

    Vst3Picker (std::vector<Entry> choices, std::function<void (const juce::File&)> choose)
        : entries (std::move (choices)), onChoose (std::move (choose))
    {
        search.setName ("Search VST3 plug-ins");
        search.setColour (juce::TextEditor::backgroundColourId, juce::Colour (Theme::cardBg));
        search.setColour (juce::TextEditor::textColourId, juce::Colour (Theme::textPrimary));
        search.setColour (juce::TextEditor::focusedOutlineColourId, juce::Colour (Theme::accentTeal));
        search.setColour (juce::CaretComponent::caretColourId, juce::Colour (Theme::textPrimary));
        search.setTextToShowWhenEmpty ("Search VST3 plug-ins...", juce::Colour (Theme::textSecondary));
        search.onTextChange = [this]
        {
            openVendor.clear();
            filter();
        };
        search.onReturnKey = [this]
        {
            if (results.isVisible())
                chooseRow (results.getSelectedRow());
            else
                openVendorRow (vendors.getSelectedRow());
        };
        search.setEscapeAndReturnKeysConsumed (true);
        search.onEscapeKey = [this] { backOrDismiss(); };
        search.addKeyListener (this);
        addAndMakeVisible (search);

        for (const auto& entry : entries)
            if (vendorNames.isEmpty() || ! vendorNames[vendorNames.size() - 1].equalsIgnoreCase (entry.vendor))
                vendorNames.add (entry.vendor);

        for (auto* list : { &vendors, &results })
        {
            list->addKeyListener (this);
            list->setRowHeight (28);
            list->setColour (juce::ListBox::backgroundColourId, juce::Colour (Theme::panelBg));
            addAndMakeVisible (*list);
        }
        if (! vendorNames.isEmpty())
            vendors.selectRow (0);

        empty.setJustificationType (juce::Justification::centred);
        empty.setColour (juce::Label::textColourId, juce::Colour (Theme::textSecondary));
        empty.setInterceptsMouseClicks (false, false);
        addChildComponent (empty);
        setSize (540, 330);
        filter();
    }

    void focusSearch()
    {
        juce::Component::SafePointer<Vst3Picker> safe (this);
        juce::MessageManager::callAsync ([safe]
        {
            if (safe != nullptr && safe->isShowing())
                safe->search.grabKeyboardFocus();
        });
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (8);
        search.setBounds (area.removeFromTop (30));
        area.removeFromTop (6);
        if (vendors.isVisible())
        {
            vendors.setBounds (area.removeFromLeft (170));
            area.removeFromLeft (8);
        }

        results.setBounds (area);
        empty.setBounds (area);
    }

private:
    class VendorModel final : public juce::ListBoxModel
    {
    public:
        explicit VendorModel (Vst3Picker& picker) : owner (picker) {}

        int getNumRows() override { return owner.vendorNames.size(); }

        void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected) override
        {
            if (! juce::isPositiveAndBelow (row, getNumRows()))
                return;

            const auto& vendor = owner.vendorNames[row];
            const bool active = owner.openVendor.equalsIgnoreCase (vendor);
            if (selected || active)
                g.fillAll (juce::Colour (Theme::cardSelected));

            g.setColour (juce::Colour (active ? Theme::accentTeal : Theme::textPrimary));
            g.setFont (juce::FontOptions (13.0f));
            g.drawFittedText (vendor, 10, 0, width - 20, height, juce::Justification::centredLeft, 1);
        }

        void listBoxItemClicked (int row, const juce::MouseEvent&) override { owner.openVendorRow (row); }
        void returnKeyPressed (int row) override { owner.openVendorRow (row); }

    private:
        Vst3Picker& owner;
    };

    enum class RowKind { heading, back, plugin };

    struct Row
    {
        RowKind kind;
        const Entry* entry;
    };

    void filter()
    {
        rows.clear();
        auto words = juce::StringArray::fromTokens (search.getText(), true);
        words.removeEmptyStrings();
        const bool searching = ! words.isEmpty();
        const bool browsingVendor = openVendor.isNotEmpty();
        juce::String lastVendor;

        if (browsingVendor)
            rows.push_back ({ RowKind::back, nullptr });

        for (const auto& entry : entries)
        {
            if (browsingVendor)
            {
                if (openVendor.equalsIgnoreCase (entry.vendor))
                    rows.push_back ({ RowKind::plugin, &entry });
                continue;
            }

            if (! searching)
                break;

            const auto searchable = entry.name + " " + entry.vendor;
            if (! std::all_of (words.begin(), words.end(), [&] (const auto& word)
                               { return searchable.containsIgnoreCase (word); }))
                continue;

            if (rows.empty() || ! lastVendor.equalsIgnoreCase (entry.vendor))
            {
                rows.push_back ({ RowKind::heading, &entry });
                lastVendor = entry.vendor;
            }

            rows.push_back ({ RowKind::plugin, &entry });
        }

        vendors.setVisible (! searching && ! vendorNames.isEmpty());
        vendors.repaint();
        results.setVisible (searching || browsingVendor);
        results.updateContent();
        results.deselectAllRows();
        results.getViewport()->setViewPosition (0, 0);
        if (! rows.empty())
            results.selectRow (1);
        results.repaint();

        juce::String emptyMessage = "Select a vendor";
        if (entries.empty())
            emptyMessage = "No compatible VST3 bundles found";
        else if (searching)
            emptyMessage = "No matching VST3 plug-ins";

        empty.setText (emptyMessage, juce::dontSendNotification);
        empty.setVisible (rows.empty());
        resized();
    }

    void openVendorRow (int row)
    {
        if (! juce::isPositiveAndBelow (row, vendorNames.size()))
            return;

        openVendor = vendorNames[row];
        filter();
        focusSearch();
    }

    bool isSelectableRow (int row) const
    {
        return juce::isPositiveAndBelow (row, static_cast<int> (rows.size()))
               && rows[static_cast<size_t> (row)].kind != RowKind::heading;
    }

    void closeSubmenu()
    {
        openVendor.clear();
        filter();
    }

    void backOrDismiss()
    {
        if (openVendor.isNotEmpty())
        {
            closeSubmenu();
            focusSearch();
        }
        else if (auto* callout = findParentComponentOfClass<juce::CallOutBox>())
        {
            callout->dismiss();
        }
    }

    bool keyPressed (const juce::KeyPress& key, juce::Component* source) override
    {
        if (key == juce::KeyPress::escapeKey)
        {
            backOrDismiss();
            return true;
        }

        if (key == juce::KeyPress::downKey || key == juce::KeyPress::upKey)
        {
            const int step = key == juce::KeyPress::downKey ? 1 : -1;
            if (vendors.isVisible() && (! results.isVisible()
                                        || source == &vendors || vendors.isParentOf (source)))
            {
                if (! vendorNames.isEmpty())
                    vendors.selectRow (juce::jlimit (0, vendorNames.size() - 1,
                                                     vendors.getSelectedRow() + step));
                return true;
            }

            for (int row = results.getSelectedRow() + step;
                 juce::isPositiveAndBelow (row, getNumRows()); row += step)
            {
                if (isSelectableRow (row))
                {
                    results.selectRow (row);
                    break;
                }
            }
            return true;
        }

        return false;
    }

    int getNumRows() override { return static_cast<int> (rows.size()); }

    void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected) override
    {
        if (! juce::isPositiveAndBelow (row, getNumRows()))
            return;

        const auto& item = rows[static_cast<size_t> (row)];
        if (selected)
            g.fillAll (juce::Colour (Theme::cardSelected));

        if (item.kind != RowKind::plugin)
        {
            if (! selected)
                g.fillAll (juce::Colour (Theme::cardBg));

            const juce::String label = item.kind == RowKind::back ? "Back to vendors" : item.entry->vendor;
            g.setColour (juce::Colour (Theme::accentTeal));
            g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
            g.drawFittedText (label, 8, 0, width - 16, height,
                              juce::Justification::centredLeft, 1);
            return;
        }

        g.setColour (juce::Colour (Theme::textPrimary));
        g.setFont (juce::FontOptions (13.0f));
        g.drawFittedText (item.entry->name, 20, 0, width - 28, height,
                          juce::Justification::centredLeft, 1);
    }

    void selectedRowsChanged (int row) override
    {
        if (juce::isPositiveAndBelow (row, getNumRows()) && ! isSelectableRow (row))
            results.selectRow (row + 1);
    }

    void listBoxItemClicked (int row, const juce::MouseEvent&) override { chooseRow (row); }
    void returnKeyPressed (int row) override { chooseRow (row); }

    void chooseRow (int row)
    {
        if (! isSelectableRow (row))
            return;

        const auto& item = rows[static_cast<size_t> (row)];
        if (item.kind == RowKind::back)
        {
            closeSubmenu();
            focusSearch();
            return;
        }

        const auto bundle = item.entry->bundle;
        if (auto* callout = findParentComponentOfClass<juce::CallOutBox>())
            callout->dismiss();

        juce::MessageManager::callAsync ([choose = onChoose, bundle] { choose (bundle); });
    }

    const std::vector<Entry> entries;
    const std::function<void (const juce::File&)> onChoose;
    juce::String openVendor;
    juce::StringArray vendorNames;
    std::vector<Row> rows;
    juce::TextEditor search;
    VendorModel vendorModel { *this };
    juce::ListBox vendors { "VST3 vendors", &vendorModel };
    juce::ListBox results { "VST3 search results", this };
    juce::Label empty;
};
} // namespace

//==============================================================================
// ModernLookAndFeel
//==============================================================================
ModernLookAndFeel::ModernLookAndFeel()
{
    setColour (juce::ListBox::backgroundColourId, juce::Colour (Theme::panelBg));
    setColour (juce::ListBox::outlineColourId, juce::Colours::transparentBlack);
    setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff181a21));
    setColour (juce::PopupMenu::textColourId, juce::Colour (Theme::textPrimary));
    setColour (juce::PopupMenu::highlightedBackgroundColourId, juce::Colour (Theme::accentTealDim));
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
    setColour (juce::ScrollBar::thumbColourId, juce::Colour (0x44ffffff));
}

void ModernLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button,
                                              const juce::Colour& backgroundColour,
                                              bool shouldDrawButtonAsHighlighted,
                                              bool shouldDrawButtonAsDown)
{
    auto bounds = button.getLocalBounds().toFloat();
    const float cornerRadius = 5.0f;

    auto baseColour = backgroundColour;

    if (shouldDrawButtonAsDown)
        baseColour = baseColour.darker (0.2f);
    else if (shouldDrawButtonAsHighlighted)
        baseColour = baseColour.brighter (0.15f);

    g.setColour (baseColour);
    g.fillRoundedRectangle (bounds, cornerRadius);

    // Subtle border
    if (button.getName() == "accent")
    {
        g.setColour (juce::Colour (Theme::accentTeal).withAlpha (shouldDrawButtonAsHighlighted ? 0.9f : 0.6f));
        g.drawRoundedRectangle (bounds.reduced (0.5f), cornerRadius, 1.0f);
    }
    else
    {
        g.setColour (juce::Colour (Theme::borderSubtle).withAlpha (shouldDrawButtonAsHighlighted ? 0.9f : 0.6f));
        g.drawRoundedRectangle (bounds.reduced (0.5f), cornerRadius, 1.0f);
    }
}

void ModernLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button,
                                        bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown)
{
    juce::ignoreUnused (shouldDrawButtonAsHighlighted, shouldDrawButtonAsDown);
    auto font = getTextButtonFont (button, button.getHeight());
    g.setFont (font);
    g.setColour (button.findColour (button.getToggleState() ? juce::TextButton::textColourOnId
                                                           : juce::TextButton::textColourOffId));

    const int yIndent = juce::jmin (4, button.proportionOfHeight (0.3f));

    // Small symmetric padding: rectangular buttons do not need the round-button
    // half-corner inset, which used to squeeze (and visually shrink) short labels.
    const int fontLeft = 4;
    const int fontRight = button.getWidth() - 4;

    if (fontRight > fontLeft)
        g.drawFittedText (button.getButtonText(),
                          fontLeft, yIndent,
                          fontRight - fontLeft, button.getHeight() - yIndent * 2,
                          juce::Justification::centred, 2);
}

juce::Font ModernLookAndFeel::getTextButtonFont (juce::TextButton& button, int buttonHeight)
{
    // Every chrome button (header master bypass, pane actions, per-slot ON/Solo/×)
    // shares one explicit size so the UI reads as a single type scale.
    juce::ignoreUnused (button, buttonHeight);
    return juce::Font (juce::FontOptions (Theme::uiFontHeight, juce::Font::bold));
}

void ModernLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int width, int height)
{
    auto r = juce::Rectangle<float> (0, 0, (float) width, (float) height);
    g.setColour (juce::Colour (0xff181a21));
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (juce::Colour (Theme::borderActive));
    g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);
}

void ModernLookAndFeel::drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area,
                                           bool isSeparator, bool isActive, bool isHighlighted,
                                           bool isTicked, bool hasSubMenu, const juce::String& text,
                                           const juce::String& shortcutKeyText,
                                           const juce::Drawable* icon, const juce::Colour* textColourToUse)
{
    juce::ignoreUnused (isTicked, icon, textColourToUse);

    if (isSeparator)
    {
        auto r = area.reduced (6, 0);
        g.setColour (juce::Colour (Theme::borderSubtle));
        g.fillRect (r.removeFromTop (1).withY (area.getCentreY()));
        return;
    }

    auto itemArea = area.reduced (4, 1);

    if (isHighlighted && isActive)
    {
        g.setColour (juce::Colour (Theme::accentTealDim));
        g.fillRoundedRectangle (itemArea.toFloat(), 4.0f);
        g.setColour (juce::Colour (Theme::accentTeal).withAlpha (0.4f));
        g.drawRoundedRectangle (itemArea.toFloat().reduced (0.5f), 4.0f, 1.0f);
    }

    g.setFont (juce::FontOptions (13.0f));
    g.setColour (isActive ? (isHighlighted ? juce::Colours::white : juce::Colour (Theme::textPrimary))
                          : juce::Colour (Theme::textDim));

    auto textArea = itemArea.reduced (8, 0);
    g.drawText (text, textArea, juce::Justification::centredLeft, true);

    if (shortcutKeyText.isNotEmpty())
    {
        g.setFont (juce::FontOptions (11.0f));
        g.setColour (juce::Colour (Theme::textDim));
        g.drawText (shortcutKeyText, textArea, juce::Justification::centredRight, true);
    }

    if (hasSubMenu)
    {
        g.setFont (juce::FontOptions (10.0f));
        g.setColour (juce::Colour (Theme::textSecondary));
        g.drawText (juce::String::charToString (0x25B6), textArea, juce::Justification::centredRight, false);
    }
}

void ModernLookAndFeel::drawScrollbar (juce::Graphics& g, juce::ScrollBar& scrollbar,
                                       int x, int y, int width, int height,
                                       bool isScrollbarVertical, int thumbStartPosition,
                                       int thumbSize, bool isMouseOver, bool isMouseDown)
{
    juce::ignoreUnused (scrollbar, isMouseDown);
    if (thumbSize <= 0)
        return;

    g.setColour (juce::Colours::white.withAlpha (isMouseOver ? 0.35f : 0.18f));

    if (isScrollbarVertical)
    {
        auto thumbBounds = juce::Rectangle<float> ((float) x + (float) width - 5.0f,
                                                   (float) thumbStartPosition,
                                                   4.0f, (float) thumbSize);
        g.fillRoundedRectangle (thumbBounds, 2.0f);
    }
    else
    {
        auto thumbBounds = juce::Rectangle<float> ((float) thumbStartPosition,
                                                   (float) y + (float) height - 5.0f,
                                                   (float) thumbSize, 4.0f);
        g.fillRoundedRectangle (thumbBounds, 2.0f);
    }
}

//==============================================================================
// SlotRowComponent
//==============================================================================
SlotRowComponent::SlotRowComponent (Listener& l)
    : listener (l)
{
    removeButton.setButtonText (juce::String::charToString (0x00D7));
    bypassButton.setButtonText ("ON");
    soloButton.setButtonText ("SOLO");

    bypassButton.setTooltip ("Bypass this stage");
    soloButton.setTooltip ("Solo this stage (only soloed stages process)");
    removeButton.setTooltip ("Remove this stage");

    for (auto* b : { &bypassButton, &soloButton, &removeButton })
    {
        b->setColour (juce::TextButton::buttonColourId, juce::Colour (Theme::cardBg));
        b->setColour (juce::TextButton::textColourOffId, juce::Colour (Theme::textSecondary));
        addAndMakeVisible (*b);
    }

    bypassButton.onClick = [this] { listener.slotRowBypassToggled (slotIndex); };
    soloButton.onClick = [this] { listener.slotRowSoloToggled (slotIndex); };
    removeButton.onClick = [this] { listener.slotRowRemoved (slotIndex); };
}

void SlotRowComponent::update (int rowNumber, bool rowSelected, bool rowBypassed, bool rowSoloed,
                               bool anySoloed, const juce::String& name, const juce::String& vendor)
{
    slotIndex = rowNumber;
    isSelected = rowSelected;
    isBypassed = rowBypassed;
    isSoloed = rowSoloed;
    soloActive = anySoloed;
    slotName = name;
    slotVendor = vendor;

    if (isBypassed)
    {
        bypassButton.setButtonText ("BYP");
        bypassButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff271e19));
        bypassButton.setColour (juce::TextButton::textColourOffId, juce::Colour (Theme::accentAmber));
    }
    else
    {
        bypassButton.setButtonText ("ON");
        bypassButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff142624));
        bypassButton.setColour (juce::TextButton::textColourOffId, juce::Colour (Theme::accentTeal));
    }

    if (isSoloed)
    {
        soloButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2a2013));
        soloButton.setColour (juce::TextButton::textColourOffId, juce::Colour (Theme::accentAmber));
    }
    else
    {
        soloButton.setColour (juce::TextButton::buttonColourId, juce::Colour (Theme::cardBg));
        soloButton.setColour (juce::TextButton::textColourOffId,
                              juce::Colour (Theme::textSecondary).withAlpha (anySoloed ? 0.45f : 1.0f));
    }

    removeButton.setColour (juce::TextButton::textColourOffId, juce::Colour (Theme::accentRed).withAlpha (0.8f));

    repaint();
}

void SlotRowComponent::paint (juce::Graphics& g)
{
    auto card = getLocalBounds().reduced (2, 2).toFloat();
    const float radius = 5.0f;

    // Background fill
    if (isSelected)
    {
        juce::ColourGradient grad (juce::Colour (Theme::cardSelected), card.getX(), card.getY(),
                                   juce::Colour (0xff131f22), card.getRight(), card.getBottom(), false);
        g.setGradientFill (grad);
        g.fillRoundedRectangle (card, radius);

        // Electric cyan accent border
        g.setColour (juce::Colour (Theme::accentTeal).withAlpha (0.85f));
        g.drawRoundedRectangle (card.reduced (0.5f), radius, 1.2f);

        // Luminous left accent marker
        g.setColour (juce::Colour (Theme::accentTeal));
        g.fillRoundedRectangle (card.getX() + 2.0f, card.getY() + 6.0f, 3.0f, card.getHeight() - 12.0f, 1.5f);
    }
    else
    {
        g.setColour (juce::Colour (isBypassed ? Theme::cardBypassed : Theme::cardBg));
        g.fillRoundedRectangle (card, radius);

        g.setColour (juce::Colour (Theme::borderSubtle));
        g.drawRoundedRectangle (card.reduced (0.5f), radius, 1.0f);
    }

    const float alpha = isBypassed ? 0.45f : (soloActive && ! isSoloed ? 0.35f : 1.0f);

    // Drag handle grip dots (2 columns of 3 dots)
    const float gripX = card.getX() + 6.0f;
    const float gripY = card.getY() + (card.getHeight() - 14.0f) / 2.0f;
    g.setColour (juce::Colour (Theme::textDim).withAlpha (alpha * 0.8f));
    for (int col = 0; col < 2; ++col)
        for (int r = 0; r < 3; ++r)
            g.fillEllipse (gripX + (float) col * 4.0f, gripY + (float) r * 5.0f, 2.0f, 2.0f);

    // Index pill
    auto indexRect = juce::Rectangle<float> (card.getX() + 18.0f, card.getY() + (card.getHeight() - 18.0f) / 2.0f,
                                             20.0f, 18.0f);
    g.setColour (juce::Colour (0xff121419));
    g.fillRoundedRectangle (indexRect, 3.0f);
    g.setColour (juce::Colour (Theme::borderSubtle));
    g.drawRoundedRectangle (indexRect.reduced (0.5f), 3.0f, 1.0f);

    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    g.setColour (juce::Colour (Theme::textSecondary).withAlpha (alpha));
    g.drawText (juce::String (slotIndex + 1), indexRect, juce::Justification::centred, false);

    // Text labels area
    const int textX = (int) card.getX() + 44;
    const int textW = juce::jmax (10, (int) card.getWidth() - 44 - 112);

    // Plugin Name
    g.setFont (juce::FontOptions (Theme::uiFontHeight, juce::Font::bold));
    g.setColour (juce::Colour (Theme::textPrimary).withAlpha (alpha));
    g.drawText (slotName, textX, (int) card.getY() + 3, textW, 18, juce::Justification::centredLeft, true);

    // Vendor & Format subtitle
    g.setFont (juce::FontOptions (11.0f));
    g.setColour (juce::Colour (Theme::textSecondary).withAlpha (alpha * 0.9f));
    g.drawText (slotVendor + " • VST3", textX, (int) card.getY() + 21, textW, 15, juce::Justification::centredLeft, true);
}

void SlotRowComponent::resized()
{
    auto card = getLocalBounds().reduced (2, 2);

    auto right = card.removeFromRight (112).reduced (3, 8);

    removeButton.setBounds (right.removeFromRight (20));
    right.removeFromRight (3);

    soloButton.setBounds (right.removeFromRight (44));
    right.removeFromRight (3);

    bypassButton.setBounds (right.removeFromRight (30));
}

void SlotRowComponent::mouseDown (const juce::MouseEvent&)
{
    listener.slotRowSelected (slotIndex);
}

void SlotRowComponent::mouseDrag (const juce::MouseEvent& e)
{
    if (e.mouseWasDraggedSinceMouseDown() && e.getDistanceFromDragStart() > 3)
    {
        if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor (this))
        {
            if (! container->isDragAndDropActive())
            {
                juce::var desc ("slot:" + juce::String (slotIndex));
                auto snapshot = createComponentSnapshot (getLocalBounds());
                container->startDragging (desc, this, juce::ScaledImage (snapshot), true);
            }
        }
    }
}

//==============================================================================
// ChainListBox
//==============================================================================
ChainListBox::ChainListBox (const juce::String& name, juce::ListBoxModel* model, DragListener& listener)
    : juce::ListBox (name, model), dragListener (listener)
{
}

bool ChainListBox::isInterestedInDragSource (const SourceDetails& dragSourceDetails)
{
    return dragSourceDetails.description.toString().startsWith ("slot:");
}

void ChainListBox::itemDragEnter (const SourceDetails& dragSourceDetails)
{
    insertIndex = getInsertionIndexForPosition (dragSourceDetails.localPosition.x,
                                                dragSourceDetails.localPosition.y);
    repaint();
}

void ChainListBox::itemDragMove (const SourceDetails& dragSourceDetails)
{
    insertIndex = getInsertionIndexForPosition (dragSourceDetails.localPosition.x,
                                                dragSourceDetails.localPosition.y);
    repaint();
}

void ChainListBox::itemDragExit (const SourceDetails&)
{
    insertIndex = -1;
    repaint();
}

void ChainListBox::itemDropped (const SourceDetails& dragSourceDetails)
{
    insertIndex = -1;
    repaint();

    const auto desc = dragSourceDetails.description.toString();
    if (! desc.startsWith ("slot:"))
        return;

    const int totalRows = getListBoxModel() != nullptr ? getListBoxModel()->getNumRows() : 0;
    if (totalRows <= 0)
        return;

    const int fromIndex = desc.fromFirstOccurrenceOf ("slot:", false, false).getIntValue();
    int rawInsert = getInsertionIndexForPosition (dragSourceDetails.localPosition.x,
                                                  dragSourceDetails.localPosition.y);

    if (rawInsert < 0)
    {
        if (dragSourceDetails.localPosition.y < 0)
            rawInsert = 0;
        else
            rawInsert = totalRows;
    }

    int toIndex = rawInsert;
    if (toIndex > fromIndex)
        toIndex--;

    toIndex = juce::jlimit (0, juce::jmax (0, totalRows - 1), toIndex);

    if (fromIndex != toIndex)
        dragListener.slotDropped (fromIndex, toIndex);
}

void ChainListBox::paintOverChildren (juce::Graphics& g)
{
    const int totalRows = getListBoxModel() != nullptr ? getListBoxModel()->getNumRows() : 0;
    if (insertIndex >= 0 && totalRows > 0)
    {
        float lineY = 0.0f;

        if (insertIndex < totalRows)
        {
            auto rowPos = getRowPosition (insertIndex, true);
            lineY = (float) rowPos.getY();
        }
        else
        {
            auto rowPos = getRowPosition (totalRows - 1, true);
            lineY = (float) rowPos.getBottom();
        }

        const float left = 6.0f;
        const float right = (float) getWidth() - 6.0f;

        // Glowing electric cyan insertion line
        g.setColour (juce::Colour (Theme::accentTeal).withAlpha (0.4f));
        g.drawLine (left, lineY, right, lineY, 5.0f);

        g.setColour (juce::Colour (Theme::accentTeal));
        g.drawLine (left, lineY, right, lineY, 2.0f);

        // Terminal dots
        g.fillEllipse (left - 1.0f, lineY - 3.5f, 7.0f, 7.0f);
        g.fillEllipse (right - 6.0f, lineY - 3.5f, 7.0f, 7.0f);
    }
}

//==============================================================================
// HybridEditor
//==============================================================================
HybridEditor::HybridEditor (HybridProcessor& p)
    : juce::AudioProcessorEditor (p), wrapperProcessor (p)
{
    setLookAndFeel (&modernLnf);
    zyMarkDrawable = juce::Drawable::createFromSVGString (kZyMarkSvg);
    setResizable (true, false);

    // Header Controls
    masterBypassButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff1b1e25));
    masterBypassButton.onClick = [this] { toggleMasterBypass(); };
    addAndMakeVisible (masterBypassButton);

    latencyPill.setFont (juce::FontOptions (Theme::uiFontHeight, juce::Font::bold));
    latencyPill.setJustificationType (juce::Justification::centred);
    latencyPill.setColour (juce::Label::backgroundColourId, juce::Colour (0xff191c24));
    latencyPill.setColour (juce::Label::outlineColourId, juce::Colour (Theme::borderSubtle));
    latencyPill.setColour (juce::Label::textColourId, juce::Colour (Theme::accentTeal));
    addAndMakeVisible (latencyPill);

    // Left Pane Controls
    chainHeaderLabel.setText ("PROCESSING CHAIN", juce::dontSendNotification);
    chainHeaderLabel.setFont (juce::FontOptions (Theme::uiFontHeight, juce::Font::bold));
    chainHeaderLabel.setColour (juce::Label::textColourId, juce::Colour (Theme::textSecondary));
    addAndMakeVisible (chainHeaderLabel);

    slotCountBadge.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    slotCountBadge.setJustificationType (juce::Justification::centred);
    slotCountBadge.setColour (juce::Label::backgroundColourId, juce::Colour (0xff1b1e26));
    slotCountBadge.setColour (juce::Label::outlineColourId, juce::Colour (Theme::borderSubtle));
    slotCountBadge.setColour (juce::Label::textColourId, juce::Colour (Theme::accentTeal));
    addAndMakeVisible (slotCountBadge);

    addButton.setName ("accent");
    addButton.setComponentID ("paneAction");
    addButton.setColour (juce::TextButton::buttonColourId, juce::Colour (Theme::accentTeal));
    addButton.setColour (juce::TextButton::textColourOffId, juce::Colour (0xff091614));
    addButton.onClick = [this] { addPlugin(); };
    addAndMakeVisible (addButton);

    browseButton.setComponentID ("paneAction");
    browseButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff20242e));
    browseButton.setColour (juce::TextButton::textColourOffId, juce::Colour (Theme::textPrimary));
    browseButton.onClick = [this] { browseForBundle(); };
    addAndMakeVisible (browseButton);

    clearButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff20242e));
    clearButton.setColour (juce::TextButton::textColourOffId, juce::Colour (Theme::textSecondary));
    clearButton.onClick = [this] { clearAllSlots(); };
    addAndMakeVisible (clearButton);

    slotList.setRowHeight (slotRowHeight);
    slotList.setColour (juce::ListBox::backgroundColourId, juce::Colour (Theme::panelBg));
    slotList.setColour (juce::ListBox::outlineColourId, juce::Colours::transparentBlack);
    addAndMakeVisible (slotList);

    // Footer Status
    status.setFont (juce::FontOptions (Theme::uiFontHeight));
    status.setColour (juce::Label::textColourId, juce::Colour (Theme::textSecondary));
    addAndMakeVisible (status);

    setResizeLimits (620, 300, 3000, 2400);
    setSize (860, 480);
    startTimerHz (10);
    updateControls();
    updateMasterBypassState();
}

HybridEditor::~HybridEditor()
{
    stopTimer();
    if (vst3Picker != nullptr)
        vst3Picker->dismiss();

    fileChooser.reset();
    clearHostedEditor();
    setLookAndFeel (nullptr);
}

void HybridEditor::clearHostedEditor()
{
    JUCE_ASSERT_MESSAGE_MANAGER_IS_LOCKED
    if (hostedEditor != nullptr)
        hostedEditor->removeComponentListener (this);

    hostedEditor.reset();
    hostedSlot = -1;
}

void HybridEditor::closeHostedWindows()
{
    clearHostedEditor();
}

void HybridEditor::toggleMasterBypass()
{
    wrapperProcessor.setBypassed (! wrapperProcessor.isBypassed());
    updateMasterBypassState();
}

void HybridEditor::updateMasterBypassState()
{
    const bool bypassed = wrapperProcessor.isBypassed();
    if (bypassed)
    {
        masterBypassButton.setButtonText (juce::String::charToString (0x25CB) + " BYPASS");
        masterBypassButton.setColour (juce::TextButton::textColourOffId, juce::Colour (Theme::accentAmber));
        masterBypassButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff271e19));
    }
    else
    {
        masterBypassButton.setButtonText (juce::String::charToString (0x25CF) + " ACTIVE");
        masterBypassButton.setColour (juce::TextButton::textColourOffId, juce::Colour (Theme::accentTeal));
        masterBypassButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff142624));
    }
}

void HybridEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (Theme::bgDark));

    // Top Header
    auto headerBounds = getLocalBounds().removeFromTop (headerHeight);
    g.setColour (juce::Colour (Theme::headerBg));
    g.fillRect (headerBounds);
    g.setColour (juce::Colour (Theme::borderSubtle));
    g.drawHorizontalLine (headerHeight - 1, 0.0f, (float) getWidth());

    // zyvst ZY Monogram (official brand mark)
    if (zyMarkDrawable != nullptr)
    {
        zyMarkDrawable->drawWithin (g, juce::Rectangle<float> (14.0f, 12.0f, 38.0f, 20.0f),
                                    juce::RectanglePlacement::centred | juce::RectanglePlacement::onlyReduceInSize, 1.0f);
    }

    // zyvst Wordmark
    g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
    g.setColour (juce::Colours::white);
    g.drawText ("zyvst", 58, 12, 54, 20, juce::Justification::centredLeft, false);

    // Wrapper Subtitle
    g.setFont (juce::FontOptions (Theme::uiFontHeight, juce::Font::bold));
    g.setColour (juce::Colour (Theme::textSecondary));
#if JucePlugin_IsMidiEffect
    const char* typeLabel = "MIDI FX";
#elif JucePlugin_IsSynth
    const char* typeLabel = "INSTRUMENT";
#else
    const char* typeLabel = "FX";
#endif
    g.drawText (typeLabel, 116, 11, 96, 22, juce::Justification::centredLeft, false);

    // Version Badge Pill
    auto verRect = juce::Rectangle<float> (216.0f, 11.0f, 112.0f, 22.0f);
    g.setColour (juce::Colour (0xff191c24));
    g.fillRoundedRectangle (verRect, 11.0f);
    g.setColour (juce::Colour (Theme::borderSubtle));
    g.drawRoundedRectangle (verRect.reduced (0.5f), 11.0f, 1.0f);

    g.setFont (juce::FontOptions (Theme::uiFontHeight, juce::Font::bold));
    g.setColour (juce::Colour (Theme::textSecondary));
    g.drawText (ZYV_BUILD_LABEL, verRect, juce::Justification::centred, false);

    // Left Pane Background
    auto leftBounds = juce::Rectangle<int> (0, headerHeight, leftPaneWidth,
                                            getHeight() - headerHeight - footerHeight);
    g.setColour (juce::Colour (Theme::panelBg));
    g.fillRect (leftBounds);
    g.setColour (juce::Colour (Theme::borderSubtle));
    g.drawVerticalLine (leftPaneWidth - 1, (float) headerHeight, (float) (getHeight() - footerHeight));

    // Footer Background
    auto footerBounds = juce::Rectangle<int> (0, getHeight() - footerHeight, getWidth(), footerHeight);
    g.setColour (juce::Colour (0xff101115));
    g.fillRect (footerBounds);
    g.setColour (juce::Colour (0xff1d212b));
    g.drawHorizontalLine (getHeight() - footerHeight, 0.0f, (float) getWidth());

    // Right Canvas Placeholder
    auto pane = rightPaneArea();
    if (pane.getWidth() > 0 && pane.getHeight() > 0)
    {
        if (hostedEditor == nullptr)
        {
            g.setColour (juce::Colour (0xff121317));
            g.fillRoundedRectangle (pane.toFloat(), 6.0f);
            g.setColour (juce::Colour (Theme::borderSubtle));
            g.drawRoundedRectangle (pane.toFloat().reduced (0.5f), 6.0f, 1.0f);

            // Stylized Rack Silhouette Placeholder
            auto center = pane.getCentre();
            auto chassis = juce::Rectangle<float> ((float) center.x - 140.0f, (float) center.y - 70.0f,
                                                   280.0f, 70.0f);
            g.setColour (juce::Colour (0xff181a22));
            g.fillRoundedRectangle (chassis, 5.0f);
            g.setColour (juce::Colour (Theme::borderSubtle));
            g.drawRoundedRectangle (chassis.reduced (0.5f), 5.0f, 1.0f);

            // Rack Screw holes
            g.setColour (juce::Colour (0xff252936));
            g.fillEllipse (chassis.getX() + 8.0f, chassis.getY() + 8.0f, 6.0f, 6.0f);
            g.fillEllipse (chassis.getRight() - 14.0f, chassis.getY() + 8.0f, 6.0f, 6.0f);
            g.fillEllipse (chassis.getX() + 8.0f, chassis.getBottom() - 14.0f, 6.0f, 6.0f);
            g.fillEllipse (chassis.getRight() - 14.0f, chassis.getBottom() - 14.0f, 6.0f, 6.0f);

            // Grill lines
            g.setColour (juce::Colour (0xff202430));
            for (int i = 0; i < 4; ++i)
                g.drawHorizontalLine ((int) chassis.getY() + 20 + i * 8, chassis.getX() + 30.0f, chassis.getRight() - 30.0f);

            // Empty State Text
            g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
            g.setColour (juce::Colour (Theme::textSecondary));
            g.drawText ("No Plugin Selected", pane.getX(), center.y + 16, pane.getWidth(), 20,
                        juce::Justification::centred, false);

            g.setFont (juce::FontOptions (11.5f));
            g.setColour (juce::Colour (Theme::textDim));
            g.drawText ("Select a plugin from the chain to display its native interface",
                        pane.getX(), center.y + 38, pane.getWidth(), 18, juce::Justification::centred, false);

            g.setFont (juce::FontOptions (10.5f));
            g.setColour (juce::Colour (Theme::textDim).withAlpha (0.7f));
            g.drawText ("Serial VST3 Processing  •  Zero Remote Overhead  •  Host Delay Compensated",
                        pane.getX(), center.y + 58, pane.getWidth(), 16, juce::Justification::centred, false);
        }
        else
        {
            // Subtle frame around active plugin editor
            auto frame = hostedEditor->getBounds().expanded (1);
            g.setColour (juce::Colour (Theme::borderSubtle));
            g.drawRect (frame, 1);
        }
    }
}

juce::Rectangle<int> HybridEditor::rightPaneArea() const
{
    return juce::Rectangle<int> (leftPaneWidth + 10,
                                 headerHeight + 10,
                                 getWidth() - leftPaneWidth - 20,
                                 getHeight() - headerHeight - footerHeight - 20);
}

void HybridEditor::resized()
{
    // Header
    masterBypassButton.setBounds (getWidth() - 106, 9, 94, 26);
    latencyPill.setBounds (getWidth() - 240, 9, 126, 26);

    // Left Pane
    chainHeaderLabel.setBounds (12, headerHeight + 8, 150, 20);
    slotCountBadge.setBounds (166, headerHeight + 9, 24, 18);
    clearButton.setBounds (leftPaneWidth - 54, headerHeight + 8, 44, 20);

    const int actionY = headerHeight + 34;
    const int actionWidth = (leftPaneWidth - 26) / 2;
    addButton.setBounds (10, actionY, actionWidth, 26);
    browseButton.setBounds (10 + actionWidth + 6, actionY, actionWidth, 26);

    const int listY = actionY + 32;
    slotList.setBounds (6, listY, leftPaneWidth - 12, getHeight() - footerHeight - listY - 6);

    // Footer
    status.setBounds (14, getHeight() - footerHeight, getWidth() - 28, footerHeight);

    // Right Canvas
    applyHostedEditorBounds();
}

void HybridEditor::applyHostedEditorBounds()
{
    if (hostedEditor == nullptr)
        return;

    const auto pane = rightPaneArea();
    const juce::ScopedValueSetter<bool> guard (updatingHostedBounds, true);

    if (hostedEditor->isResizable())
    {
        hostedEditor->setBounds (pane);
    }
    else
    {
        // A fixed-size plugin keeps its own dimensions; centre it in the pane so
        // extra window space looks intentional instead of leaving a dead corner.
        auto target = juce::Rectangle<int> (0, 0, hostedEditor->getWidth(), hostedEditor->getHeight())
                          .withCentre (pane.getCentre());
        hostedEditor->setBounds (target);
    }

    hostedEditor->setVisible (true);
}

void HybridEditor::componentMovedOrResized (juce::Component& component, bool wasMoved, bool wasResized)
{
    juce::ignoreUnused (wasMoved);

    if (&component != hostedEditor.get() || ! wasResized || updatingHostedBounds)
        return;

    // The plugin resized its own editor (its corner resizer, or a size change
    // after loading/restoring state): grow or shrink the wrapper window to match.
    fitWindowToContent (hostedEditor.get());
    repaint();
}

int HybridEditor::getNumRows()
{
    return wrapperProcessor.getChain().getNumSlots();
}

void HybridEditor::paintListBoxItem (int, juce::Graphics&, int, int, bool)
{
    // Custom SlotRowComponent handles all rendering.
}

juce::Component* HybridEditor::refreshComponentForRow (int rowNumber, bool isRowSelected,
                                                       juce::Component* existingComponentToUpdate)
{
    auto& chain = wrapperProcessor.getChain();
    if (! juce::isPositiveAndBelow (rowNumber, chain.getNumSlots()))
    {
        delete existingComponentToUpdate;
        return nullptr;
    }

    auto* rowComp = dynamic_cast<SlotRowComponent*> (existingComponentToUpdate);
    if (rowComp == nullptr)
        rowComp = new SlotRowComponent (*this);

    const auto name = chain.getSlotName (rowNumber);
    const auto vendor = chain.getSlotVendor (rowNumber);
    const bool bypassed = chain.isSlotBypassed (rowNumber);
    const bool soloed = chain.isSlotSoloed (rowNumber);
    const bool soloActive = chain.isAnySlotSoloed();

    rowComp->update (rowNumber, isRowSelected, bypassed, soloed, soloActive, name, vendor);
    return rowComp;
}

void HybridEditor::selectedRowsChanged (int)
{
    showSelectedPlugin();
    updateControls();
}

void HybridEditor::slotRowSelected (int slotIndex)
{
    if (slotList.getSelectedRow() != slotIndex)
    {
        slotList.selectRow (slotIndex);
        showSelectedPlugin();
        updateControls();
    }
}

void HybridEditor::slotRowBypassToggled (int slotIndex)
{
    auto& chain = wrapperProcessor.getChain();
    if (! juce::isPositiveAndBelow (slotIndex, chain.getNumSlots()))
        return;

    const bool currentlyBypassed = chain.isSlotBypassed (slotIndex);
    chain.setSlotBypassed (slotIndex, ! currentlyBypassed);
    wrapperProcessor.reportLatencyToHost();
    slotList.updateContent();
    slotList.repaint();
}

void HybridEditor::slotRowSoloToggled (int slotIndex)
{
    auto& chain = wrapperProcessor.getChain();
    if (! juce::isPositiveAndBelow (slotIndex, chain.getNumSlots()))
        return;

    chain.setSlotSoloed (slotIndex, ! chain.isSlotSoloed (slotIndex));
    wrapperProcessor.reportLatencyToHost();
    slotList.updateContent();
    slotList.repaint();
}

void HybridEditor::moveSlot (int fromIndex, int toIndex)
{
    auto& chain = wrapperProcessor.getChain();
    const auto numSlots = chain.getNumSlots();
    if (! juce::isPositiveAndBelow (fromIndex, numSlots) || ! juce::isPositiveAndBelow (toIndex, numSlots))
        return;

    wrapperProcessor.withChainSuspended ([this, fromIndex, toIndex]
    {
        wrapperProcessor.getChain().moveSlot (fromIndex, toIndex);
    });

    slotList.updateContent();
    slotList.selectRow (toIndex);
    showSelectedPlugin();
    updateControls();
}

void HybridEditor::slotRowRemoved (int slotIndex)
{
    auto& chain = wrapperProcessor.getChain();
    if (! juce::isPositiveAndBelow (slotIndex, chain.getNumSlots()))
        return;

    if (hostedSlot == slotIndex)
        clearHostedEditor();
    else if (hostedSlot > slotIndex)
        hostedSlot--;

    wrapperProcessor.withChainSuspended ([this, slotIndex]
    {
        wrapperProcessor.getChain().removePlugin (slotIndex);
    });

    refreshList();
    wrapperProcessor.reportLatencyToHost();
}

void HybridEditor::slotDropped (int fromIndex, int toIndex)
{
    moveSlot (fromIndex, toIndex);
}

void HybridEditor::clearAllSlots()
{
    auto& chain = wrapperProcessor.getChain();
    if (chain.getNumSlots() == 0)
        return;

    clearHostedEditor();
    wrapperProcessor.withChainSuspended ([this]
    {
        while (wrapperProcessor.getChain().getNumSlots() > 0)
            wrapperProcessor.getChain().removePlugin (0);
    });

    refreshList();
    wrapperProcessor.reportLatencyToHost();
}

void HybridEditor::timerCallback()
{
    const int latency = wrapperProcessor.getLatencySamples();
    latencyPill.setText (juce::String::charToString (0x26A1) + "  " + juce::String (latency) + " SAMPLES",
                         juce::dontSendNotification);

    slotCountBadge.setText (juce::String (wrapperProcessor.getChain().getNumSlots()),
                            juce::dontSendNotification);

    status.setText ("● " + wrapperProcessor.getChain().statusText() + "   |   AUv2 Effect (aufx) arm64",
                    juce::dontSendNotification);

    updateMasterBypassState();

    if (wrapperProcessor.getChain().getNumSlots() != cachedRows)
        refreshList();
}

void HybridEditor::refreshList()
{
    slotList.updateContent();

    const auto rows = wrapperProcessor.getChain().getNumSlots();
    cachedRows = rows;

    if (slotList.getSelectedRow() >= rows)
    {
        if (rows > 0)
            slotList.selectRow (rows - 1);
        else
            slotList.deselectAllRows();
    }

    if (rows == 0)
        clearHostedEditor();

    slotList.repaint();
    updateControls();
    repaint();
}

void HybridEditor::updateControls()
{
    const auto rows = wrapperProcessor.getChain().getNumSlots();
    clearButton.setEnabled (rows > 0);
    addButton.setEnabled (true);
    browseButton.setEnabled (true);
}

void HybridEditor::fitWindowToContent (juce::AudioProcessorEditor* hosted)
{
    if (hosted == nullptr)
        return;

    const auto pluginWidth = juce::jmax (120, hosted->getWidth());
    const auto pluginHeight = juce::jmax (120, hosted->getHeight());

    // rightPaneArea() shrinks the editor by exactly the chrome below, so the
    // wrapper window is the plugin plus pane margins, header and footer.
    setSize (leftPaneWidth + 20 + pluginWidth,
             headerHeight + footerHeight + 20 + pluginHeight);
}

void HybridEditor::showSelectedPlugin()
{
    const auto row = slotList.getSelectedRow();

    if (! juce::isPositiveAndBelow (row, wrapperProcessor.getChain().getNumSlots()))
    {
        clearHostedEditor();
        repaint();
        resized();
        return;
    }

    if (hostedSlot == row && hostedEditor != nullptr)
        return;

    clearHostedEditor();

    auto* instance = wrapperProcessor.getChain().getSlotInstance (row);
    if (instance == nullptr)
    {
        repaint();
        resized();
        return;
    }

    auto* ui = instance->createEditorAndMakeActive();
    if (ui == nullptr)
    {
        status.setText (wrapperProcessor.getChain().getSlotName (row) + " has no custom editor",
                        juce::dontSendNotification);
        repaint();
        resized();
        return;
    }

    hostedSlot = row;
    hostedEditor.reset (ui);
    addAndMakeVisible (*hostedEditor);
    hostedEditor->addComponentListener (this);

    if (hostedEditor->getWidth() <= 0 || hostedEditor->getHeight() <= 0)
        hostedEditor->setSize (400, 300);

    fitWindowToContent (hostedEditor.get());
    repaint();
    resized();
}

void HybridEditor::addPlugin()
{
    if (vst3Picker != nullptr)
        return;

    const auto bundles = ChainEngine::findVST3Bundles();

    if (bundles.isEmpty())
    {
        status.setText ("No .vst3 bundles in the standard folders - use Browse...",
                        juce::dontSendNotification);
        return;
    }

    auto bundleMatchesTarget = [] (const juce::File& bundle)
    {
        const auto moduleInfo = bundle.getChildFile ("Contents/Resources/moduleinfo.json");

        if (moduleInfo.existsAsFile())
        {
            const auto text = moduleInfo.loadFileAsString();

#if JucePlugin_IsMidiEffect
            return text.containsIgnoreCase ("\"Midi\"") || text.containsIgnoreCase ("\"Instrument\"");
#elif JucePlugin_IsSynth
            return text.containsIgnoreCase ("\"Instrument\"") || text.containsIgnoreCase ("\"Synth\"");
#else
            return text.containsIgnoreCase ("\"Fx\"") && ! text.containsIgnoreCase ("\"Instrument\"") && ! text.containsIgnoreCase ("\"Midi\"");
#endif
        }

        // No metadata: keep the menu fast by including the bundle unfiltered.
        return true;
    };

    std::vector<Vst3Picker::Entry> entries;
    for (const auto& bundle : bundles)
        if (bundleMatchesTarget (bundle))
            entries.push_back ({ bundle, bundle.getFileNameWithoutExtension(),
                                 ChainEngine::vendorForBundle (bundle) });

    std::stable_sort (entries.begin(), entries.end(), [] (const auto& a, const auto& b)
    {
        return a.vendor.compareIgnoreCase (b.vendor) < 0;
    });

    auto picker = std::make_unique<Vst3Picker> (std::move (entries), [safe = safeThis] (const juce::File& bundle)
    {
        if (safe != nullptr)
            safe->addBundle (bundle);
    });

    auto* content = picker.get();
    vst3Picker = &juce::CallOutBox::launchAsynchronously (std::move (picker), addButton, nullptr);
    vst3Picker->setDismissalMouseClicksAreAlwaysConsumed (true);
    content->focusSearch();
}

void HybridEditor::browseForBundle()
{
    fileChooser = std::make_unique<juce::FileChooser> ("Choose a VST3 bundle",
                                                       juce::File ("/Library/Audio/Plug-Ins/VST3"),
                                                       "*.vst3");

    fileChooser->launchAsync (juce::FileBrowserComponent::openMode
                                 | juce::FileBrowserComponent::canSelectDirectories
                                 | juce::FileBrowserComponent::canSelectFiles,
                              [safe = safeThis] (const juce::FileChooser& chooser)
                              {
                                  const auto chosen = chooser.getResult();

                                  if (safe != nullptr && chosen != juce::File())
                                      safe->addBundle (chosen);
                              });
}

void HybridEditor::addBundle (const juce::File& bundle)
{
    juce::Array<juce::PluginDescription> types;
    juce::String error;

    if (! wrapperProcessor.getChain().describeBundle (bundle, types, error))
    {
        status.setText (error.isNotEmpty() ? error : "Could not read that bundle",
                        juce::dontSendNotification);
        return;
    }

    if (types.size() == 1)
    {
        addPluginToChain (types.getReference (0));
        return;
    }

    juce::PopupMenu menu;

    for (int i = 0; i < types.size(); ++i)
        menu.addItem (i + 1, types.getReference (i).name);

    menu.showMenuAsync (juce::PopupMenu::Options()
                            .withTargetComponent (&addButton)
                            .withMinimumWidth (260),
                        [safe = safeThis, types] (int result)
                        {
                            if (safe == nullptr || result <= 0 || result > types.size())
                                return;

                            safe->addPluginToChain (types.getReference (result - 1));
                        });
}

void HybridEditor::addPluginToChain (const juce::PluginDescription& desc)
{
    juce::String error;

    wrapperProcessor.withChainSuspended ([this, &desc, &error]
    {
        wrapperProcessor.getChain().addPlugin (desc, error);
    });

    if (error.isNotEmpty())
        status.setText ("Load failed: " + error, juce::dontSendNotification);

    refreshList();

    if (wrapperProcessor.getChain().getNumSlots() > 0)
        slotList.selectRow (wrapperProcessor.getChain().getNumSlots() - 1);

    wrapperProcessor.reportLatencyToHost();
}

} // namespace zyv
