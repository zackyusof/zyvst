#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "HybridProcessor.h"

namespace zyv
{

//==============================================================================
/** Modern custom LookAndFeel for zyvst. */
class ModernLookAndFeel : public juce::LookAndFeel_V4
{
public:
    ModernLookAndFeel();
    ~ModernLookAndFeel() override = default;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&,
                         bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;
    void drawPopupMenuItem (juce::Graphics&, const juce::Rectangle<int>& area,
                            bool isSeparator, bool isActive, bool isHighlighted, bool isTicked,
                            bool hasSubMenu, const juce::String& text,
                            const juce::String& shortcutKeyText,
                            const juce::Drawable* icon, const juce::Colour* textColourToUse) override;
    void drawScrollbar (juce::Graphics&, juce::ScrollBar&, int x, int y, int width, int height,
                        bool isScrollbarVertical, int thumbStartPosition, int thumbSize,
                        bool isMouseOver, bool isMouseDown) override;
};

//==============================================================================
/** Interactive slot card component for the left chain list with drag support. */
class SlotRowComponent final : public juce::Component
{
public:
    class Listener
    {
    public:
        virtual ~Listener() = default;
        virtual void slotRowSelected (int slotIndex) = 0;
        virtual void slotRowBypassToggled (int slotIndex) = 0;
        virtual void slotRowMoved (int fromIndex, int toIndex) = 0;
        virtual void slotRowRemoved (int slotIndex) = 0;
    };

    explicit SlotRowComponent (Listener&);
    ~SlotRowComponent() override = default;

    void update (int rowNumber, bool isSelected, bool isBypassed,
                 const juce::String& name, const juce::String& vendor,
                 bool canMoveUp, bool canMoveDown);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;

private:
    Listener& listener;
    int slotIndex = -1;
    bool isSelected = false;
    bool isBypassed = false;
    juce::String slotName;
    juce::String slotVendor;

    juce::TextButton bypassButton;
    juce::TextButton moveUpButton;
    juce::TextButton moveDownButton;
    juce::TextButton removeButton;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SlotRowComponent)
};

//==============================================================================
/** Reorderable ListBox with drop insertion indicators. */
class ChainListBox final : public juce::ListBox,
                           public juce::DragAndDropTarget
{
public:
    class DragListener
    {
    public:
        virtual ~DragListener() = default;
        virtual void slotDropped (int fromIndex, int toIndex) = 0;
    };

    ChainListBox (const juce::String& name, juce::ListBoxModel* model, DragListener& listener);
    ~ChainListBox() override = default;

    bool isInterestedInDragSource (const SourceDetails& dragSourceDetails) override;
    void itemDragEnter (const SourceDetails& dragSourceDetails) override;
    void itemDragMove (const SourceDetails& dragSourceDetails) override;
    void itemDragExit (const SourceDetails& dragSourceDetails) override;
    void itemDropped (const SourceDetails& dragSourceDetails) override;
    void paintOverChildren (juce::Graphics& g) override;

private:
    DragListener& dragListener;
    int insertIndex = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChainListBox)
};

//==============================================================================
/** Modern wrapper editor: slots on the left, hosted plugin UI on the right. */
class HybridEditor final : public juce::AudioProcessorEditor,
                           public juce::DragAndDropContainer,
                           public juce::ComponentListener,
                           private juce::ListBoxModel,
                           private juce::Timer,
                           private SlotRowComponent::Listener,
                           private ChainListBox::DragListener
{
public:
    explicit HybridEditor (HybridProcessor&);
    ~HybridEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Keeps the wrapper window in step with plugins that resize their own editor. */
    void componentMovedOrResized (juce::Component&, bool wasMoved, bool wasResized) override;

    /** Destroys the hosted editor. Called before instances are replaced/destroyed. */
    void closeHostedWindows();

    // SlotRowComponent::Listener implementation
    void slotRowSelected (int slotIndex) override;
    void slotRowBypassToggled (int slotIndex) override;
    void slotRowMoved (int fromIndex, int toIndex) override;
    void slotRowRemoved (int slotIndex) override;

    // ChainListBox::DragListener implementation
    void slotDropped (int fromIndex, int toIndex) override;

    void clearAllSlots();

private:
    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
    juce::Component* refreshComponentForRow (int rowNumber, bool isRowSelected,
                                             juce::Component* existingComponentToUpdate) override;
    void selectedRowsChanged (int lastRowSelected) override;

    void timerCallback() override;

    void addPlugin();
    void browseForBundle();
    void addBundle (const juce::File&);
    void addPluginToChain (const juce::PluginDescription&);
    void showSelectedPlugin();
    void clearHostedEditor();
    void refreshList();
    void updateControls();
    void fitWindowToContent (juce::AudioProcessorEditor* hosted);
    /** Gives the hosted plugin the right-pane area, honouring its own resize rules. */
    void applyHostedEditorBounds();
    juce::Rectangle<int> rightPaneArea() const;
    void toggleMasterBypass();
    void updateMasterBypassState();

    ModernLookAndFeel modernLnf;
    HybridProcessor& wrapperProcessor;
    std::unique_ptr<juce::DrawableComposite> zyMarkDrawable;

    // Header controls
    juce::TextButton masterBypassButton;
    juce::Label latencyPill;

    // Left pane controls
    juce::Label chainHeaderLabel;
    juce::Label slotCountBadge;
    juce::TextButton addButton { "+ Add VST3" };
    juce::TextButton browseButton { "Browse..." };
    juce::TextButton clearButton { "Clear" };

    ChainListBox slotList { "chainSlots", this, *this };
    juce::Label status;

    std::unique_ptr<juce::AudioProcessorEditor> hostedEditor;
    int hostedSlot = -1;
    /** True while the wrapper itself is sizing the hosted editor, so the change
        listener does not treat it as a plugin-initiated resize. */
    bool updatingHostedBounds = false;

    juce::Component::SafePointer<juce::CallOutBox> vst3Picker;
    std::unique_ptr<juce::FileChooser> fileChooser;
    int cachedRows = 0;
    juce::Component::SafePointer<HybridEditor> safeThis { this };

    static constexpr int leftPaneWidth = 250;
    static constexpr int headerHeight  = 44;
    static constexpr int footerHeight  = 26;
    static constexpr int slotRowHeight = 48;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HybridEditor)
};

} // namespace zyv
