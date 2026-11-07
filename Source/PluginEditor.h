#pragma once

#include "PluginProcessor.h"
#include "UI/DropZone.h"
#include "UI/ExportPanel.h"
#include "UI/HeaderBar.h"
#include "UI/ModelOverlay.h"
#include "UI/StemLane.h"
#include "UI/TransportBar.h"

namespace stemsplitter
{

class StemSplitterEditor final : public juce::AudioProcessorEditor,
                                 public juce::FileDragAndDropTarget,
                                 public juce::DragAndDropContainer,
                                 private juce::ChangeListener,
                                 private juce::Timer
{
public:
    explicit StemSplitterEditor (StemSplitterProcessor&);
    ~StemSplitterEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray&, int, int) override;
    void fileDragExit (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray& files, int, int) override;

private:
    class Toast;
    class ExportThread;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;

    void refreshAll();
    void rebuildLanesIfNeeded();
    void layoutLanes();
    void updateLaneStates();
    void updatePlayheads();

    void openFileChooser();
    void separateOrPromptForModel();
    void showModelOverlay (bool separateWhenReady);
    void locateModelFiles();
    void startExport();
    void runExport (std::function<juce::Result (juce::Array<juce::File>&, std::function<bool (float)>)> job);
    void dragStemOut (StemType type);
    void showToast (const juce::String& message, const juce::File& revealTarget = {});
    void seekToFraction (double fraction);

    juce::Rectangle<int> getMainArea() const;
    juce::Rectangle<int> getFileRowArea() const;
    juce::Rectangle<int> getProgressArea() const;

    StemSplitterProcessor& proc;
    StemSplitterLookAndFeel lookAndFeel;

    HeaderBar header;
    TransportBar transport;
    ExportPanel exportPanel;
    DropZone dropZone;

    juce::TextButton separateButton, cancelButton { "Cancel" };
    std::unique_ptr<IconButton> closeFileButton;

    juce::Viewport laneViewport;
    juce::Component laneContainer;
    std::unique_ptr<StemLane> originalLane;
    juce::OwnedArray<StemLane> stemLanes;

    ModelOverlay modelOverlay;
    std::unique_ptr<Toast> toast;
    juce::TooltipWindow tooltipWindow { this, 700 };
    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<ExportThread> exportThread;

    std::shared_ptr<const SourceAudio> shownSource;
    std::shared_ptr<const StemSet> shownStems;
    EngineType shownEngine = EngineType::numEngines;
    bool fileDragOver = false;
    int tick = 0;

    std::map<StemType, juce::File> dragOutCache;
    const StemSet* dragOutCacheOwner = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StemSplitterEditor)
};

} // namespace stemsplitter
