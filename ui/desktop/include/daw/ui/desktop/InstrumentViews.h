#pragma once

#include "daw/controller/Session.h"
#include "daw/engine/fx/Effect.h"
#include "daw/ui/desktop/GenericEffectEditor.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <vector>

namespace daw::ui::desktop
{

/**
    Controls for an effect's parameters in sections, one per ParameterInfo::group (in
    the order the groups first appear; parameters without a group go together), each a
    heading over a GenericEffectEditor, packed into columns: the synth's editor.
*/
class SectionedEffectEditor final : public juce::Component
{
public:
    SectionedEffectEditor(controller::Session& session, model::ObjectId effect,
                          const engine::fx::EffectDescription& description,
                          const std::vector<int>& parameters, int columns);
    ~SectionedEffectEditor() override;

    void refresh();

    [[nodiscard]] int getPreferredHeight() const noexcept;

    void resized() override;

private:
    struct Section;

    std::vector<std::unique_ptr<Section>> sections;
    int numColumns = 1;
};

/**
    The sampler's editor: the sample's waveform with its start and loop marked and its
    root key, the file (Load..., Remove, or drop a file anywhere), then the controls.
*/
class SamplerView final : public juce::Component,
                          public juce::FileDragAndDropTarget
{
public:
    SamplerView(controller::Session& session, model::ObjectId effect,
                const engine::fx::EffectDescription& description);
    ~SamplerView() override;

    void refresh();

    [[nodiscard]] int getPreferredHeight() const noexcept;

    void resized() override;
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

private:
    class Waveform;

    controller::Session& session;
    const model::ObjectId effectId;
    std::unique_ptr<Waveform> waveform;
    juce::Label fileName;
    juce::TextButton load{"Load..."};
    juce::TextButton remove{"Remove"};
    SectionedEffectEditor controls;
    std::unique_ptr<juce::FileChooser> chooser;
};

/**
    The drum sampler's editor: sixteen pads (pad 1 at the bottom left, as on drum
    machines) showing their sound and file; a click selects a pad and plays it through
    the track, a file dropped on a pad loads it; under them the selected pad's controls
    and the kit's.
*/
class DrumPadsView final : public juce::Component,
                           public juce::FileDragAndDropTarget
{
public:
    DrumPadsView(controller::Session& session, model::ObjectId effect,
                 const engine::fx::EffectDescription& description);
    ~DrumPadsView() override;

    void refresh();

    [[nodiscard]] int getPreferredHeight() const noexcept;

    void resized() override;
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

private:
    class Pad;

    void select(int pad);
    void chooseFile();
    [[nodiscard]] int padAt(juce::Point<int> position) const;

    controller::Session& session;
    const model::ObjectId effectId;
    const engine::fx::EffectDescription description;
    std::vector<std::unique_ptr<Pad>> pads;
    int selected = 0;
    juce::Label padTitle;
    juce::TextButton load{"Load..."};
    juce::TextButton remove{"Remove"};
    std::unique_ptr<GenericEffectEditor> padControls;
    std::unique_ptr<SectionedEffectEditor> kitControls;
    std::unique_ptr<juce::FileChooser> chooser;
};

} // namespace daw::ui::desktop
