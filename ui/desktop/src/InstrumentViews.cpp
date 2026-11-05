#include "daw/ui/desktop/InstrumentViews.h"

#include "daw/controller/NotePreview.h"
#include "daw/engine/fx/DrumSampler.h"
#include "daw/engine/fx/Sampler.h"
#include "daw/ui/desktop/ThemedLookAndFeel.h"

#include <algorithm>
#include <map>

namespace daw::ui::desktop
{

namespace
{

constexpr int headingHeight = 22;
constexpr int sectionGap = 8;
constexpr const char* sampleFilePatterns = "*.wav;*.wave;*.aif;*.aiff;*.aifc";

bool isSampleFile(const juce::File& file)
{
    return file.hasFileExtension("wav;wave;aif;aiff;aifc");
}

// A parameter's value as the model has it (its default when never set), in its units.
float valueOf(const controller::Session& session, model::ObjectId effectId,
              const engine::fx::ParameterInfo& info)
{
    if (const auto effect = session.getEffect(effectId))
        if (const auto value = effect->getParameter(info.id))
            return static_cast<float>(*value);
    return info.defaultValue;
}

float valueOf(const controller::Session& session, model::ObjectId effectId,
              const engine::fx::EffectDescription& description, const juce::String& id)
{
    for (const auto& info : description.parameters)
        if (info.id == id)
            return valueOf(session, effectId, info);
    return 0.0f;
}

juce::File fileOf(const controller::Session& session, model::ObjectId effectId, const juce::String& key)
{
    const auto effect = session.getEffect(effectId);
    return effect ? effect->getFile(key) : juce::File();
}

// What a sample slot shows: the file's name, or how its loading goes.
juce::String describeFile(const controller::Session& session, const juce::File& file)
{
    if (file == juce::File())
        return "Empty";
    switch (session.getSamples().getStatus(file))
    {
    case controller::SampleCache::Status::loading:
        return file.getFileName() + " (loading)";
    case controller::SampleCache::Status::failed:
        return file.getFileName() + " (could not be read)";
    case controller::SampleCache::Status::none:
        return file.existsAsFile() ? file.getFileName() : file.getFileName() + " (missing)";
    case controller::SampleCache::Status::ready:
    default:
        return file.getFileName();
    }
}

// The track an instrument plays on: previews go there.
model::ObjectId trackOf(const controller::Session& session, model::ObjectId effectId)
{
    if (const auto location = session.findEffect(effectId);
        location && location->owner.kind == controller::ChainOwner::Kind::track)
        return location->owner.id;
    return 0;
}

} // namespace

// ----------------------------------------------------------------------------------

struct SectionedEffectEditor::Section
{
    juce::Label heading;
    std::unique_ptr<GenericEffectEditor> controls;
    int column = 0;
};

SectionedEffectEditor::SectionedEffectEditor(controller::Session& session, model::ObjectId effect,
                                             const engine::fx::EffectDescription& description,
                                             const std::vector<int>& parameters, int columns)
    : numColumns(std::max(1, columns))
{
    // Groups in the order they first appear.
    std::vector<juce::String> groups;
    std::map<juce::String, std::vector<int>> members;
    for (const int index : parameters)
    {
        if (index < 0 || static_cast<std::size_t>(index) >= description.parameters.size())
            continue;
        const auto& group = description.parameters[static_cast<std::size_t>(index)].group;
        if (!members.contains(group))
            groups.push_back(group);
        members[group].push_back(index);
    }

    // Each section into the shortest column so far.
    std::vector<int> heights(static_cast<std::size_t>(numColumns), 0);
    for (const auto& group : groups)
    {
        auto section = std::make_unique<Section>();
        section->heading.setText(group.isEmpty() ? juce::String("Controls") : group,
                                 juce::dontSendNotification);
        ThemedLookAndFeel::setFontRole(section->heading, appearance::FontRole::heading);
        addAndMakeVisible(section->heading);
        section->controls = std::make_unique<GenericEffectEditor>(session, effect, description, members[group]);
        addAndMakeVisible(*section->controls);

        const auto shortest = std::min_element(heights.begin(), heights.end());
        section->column = static_cast<int>(shortest - heights.begin());
        *shortest += headingHeight + section->controls->getPreferredHeight() + sectionGap;
        sections.push_back(std::move(section));
    }
}

SectionedEffectEditor::~SectionedEffectEditor() = default;

void SectionedEffectEditor::refresh()
{
    for (auto& section : sections)
        section->controls->refresh();
}

int SectionedEffectEditor::getPreferredHeight() const noexcept
{
    std::vector<int> heights(static_cast<std::size_t>(numColumns), 0);
    for (const auto& section : sections)
        heights[static_cast<std::size_t>(section->column)] +=
            headingHeight + section->controls->getPreferredHeight() + sectionGap;
    return *std::max_element(heights.begin(), heights.end());
}

void SectionedEffectEditor::resized()
{
    const int columnWidth = getWidth() / numColumns;
    std::vector<int> tops(static_cast<std::size_t>(numColumns), 0);
    for (auto& section : sections)
    {
        auto& top = tops[static_cast<std::size_t>(section->column)];
        const int x = section->column * columnWidth;
        section->heading.setBounds(x + 4, top, columnWidth - 8, headingHeight);
        top += headingHeight;
        const int height = section->controls->getPreferredHeight();
        section->controls->setBounds(x, top, columnWidth, height);
        top += height + sectionGap;
    }
}

// ----------------------------------------------------------------------------------

// The sample's peaks, its start and loop, its root key.
class SamplerView::Waveform final : public juce::Component
{
public:
    Waveform(controller::Session& sessionToShow, model::ObjectId effect,
             const engine::fx::EffectDescription& describedAs)
        : session(sessionToShow)
        , effectId(effect)
        , description(describedAs)
    {
        setInterceptsMouseClicks(false, false);
    }

    void refresh()
    {
        const auto file = fileOf(session, effectId, engine::fx::Sampler::sampleSlot);
        const Shown now{file,
                        session.getSamples().find(file).get(),
                        session.getSamples().getStatus(file),
                        valueOf(session, effectId, description, "start"),
                        valueOf(session, effectId, description, "loop"),
                        valueOf(session, effectId, description, "loopStart"),
                        valueOf(session, effectId, description, "loopEnd"),
                        valueOf(session, effectId, description, "rootKey")};
        if (now != shown)
        {
            if (now.sample != shown.sample)
                peaksWidth = 0;
            shown = now;
            repaint();
        }
    }

    void paint(juce::Graphics& g) override
    {
        const auto& look = ThemedLookAndFeel::get(*this);
        const auto bounds = getLocalBounds().toFloat();
        look.drawGraphBackground(g, bounds);
        const auto area = bounds.reduced(2.0f, 4.0f);

        const auto sample = session.getSamples().find(shown.file);
        if (sample == nullptr || sample->getLength() == 0)
        {
            const auto text = shown.file == juce::File()
                                  ? juce::String("Drop a WAV or AIFF file here, or Load...")
                                  : describeFile(session, shown.file);
            look.drawGraphLabel(g, text, area, juce::Justification::centred);
            return;
        }

        updatePeaks(*sample, juce::roundToInt(area.getWidth()));
        juce::Path outline;
        const float middle = area.getCentreY();
        const float half = area.getHeight() / 2.0f;
        for (std::size_t i = 0; i < peaks.size(); ++i)
        {
            const float x = area.getX() + static_cast<float>(i);
            const float y = middle - peaks[i].second * half;
            if (i == 0)
                outline.startNewSubPath(x, y);
            else
                outline.lineTo(x, y);
        }
        for (std::size_t i = peaks.size(); i-- > 0;)
            outline.lineTo(area.getX() + static_cast<float>(i), middle - peaks[i].first * half);
        outline.closeSubPath();

        const auto xOf = [&area](float percent) { return area.getX() + area.getWidth() * percent / 100.0f; };
        if (shown.loop > 0.5f)
        {
            const float x0 = xOf(shown.loopStart);
            const float x1 = xOf(std::max(shown.loopStart, shown.loopEnd));
            look.drawLoopShade(g, {x0, area.getY(), x1 - x0, area.getHeight()}, true);
        }
        look.drawSampleWaveform(g, outline);
        if (shown.loop > 0.5f)
        {
            look.drawSampleMarker(g, xOf(shown.loopStart), area.getY(), area.getBottom(), "Loop", true);
            look.drawSampleMarker(g, xOf(std::max(shown.loopStart, shown.loopEnd)), area.getY(),
                                  area.getBottom(), "End", true);
        }
        look.drawSampleMarker(g, xOf(shown.start), area.getY(), area.getBottom(), "Start", false);
        look.drawGraphLabel(g, "Root " + engine::fx::keyName(juce::roundToInt(shown.rootKey)),
                            area.reduced(6.0f, 2.0f), juce::Justification::bottomRight);
    }

private:
    struct Shown
    {
        juce::File file;
        const engine::Sample* sample = nullptr;
        controller::SampleCache::Status status = controller::SampleCache::Status::none;
        float start = 0.0f;
        float loop = 0.0f;
        float loopStart = 0.0f;
        float loopEnd = 0.0f;
        float rootKey = 0.0f;

        bool operator==(const Shown&) const = default;
    };

    // The lowest and highest sample of every column, over the channels.
    void updatePeaks(const engine::Sample& sample, int width)
    {
        width = std::max(1, width);
        if (width == peaksWidth)
            return;
        peaksWidth = width;
        peaks.assign(static_cast<std::size_t>(width), {0.0f, 0.0f});
        const auto& audio = sample.levels.front();
        const auto length = sample.getLength();
        for (int column = 0; column < width; ++column)
        {
            const auto first = length * column / width;
            const auto last = std::max(first + 1, length * (column + 1) / width);
            float low = 0.0f;
            float high = 0.0f;
            for (int channel = 0; channel < audio.getNumChannels(); ++channel)
            {
                const auto range = juce::FloatVectorOperations::findMinAndMax(
                    audio.getReadPointer(channel, static_cast<int>(first)), static_cast<int>(last - first));
                low = std::min(low, range.getStart());
                high = std::max(high, range.getEnd());
            }
            peaks[static_cast<std::size_t>(column)] = {std::max(-1.0f, low), std::min(1.0f, high)};
        }
    }

    controller::Session& session;
    const model::ObjectId effectId;
    const engine::fx::EffectDescription& description;
    Shown shown;
    std::vector<std::pair<float, float>> peaks;
    int peaksWidth = 0;
};

namespace
{

std::vector<int> allParameters(const engine::fx::EffectDescription& description)
{
    std::vector<int> indices(description.parameters.size());
    for (std::size_t i = 0; i < indices.size(); ++i)
        indices[i] = static_cast<int>(i);
    return indices;
}

} // namespace

SamplerView::SamplerView(controller::Session& sessionToEdit, model::ObjectId effect,
                         const engine::fx::EffectDescription& description)
    : session(sessionToEdit)
    , effectId(effect)
    , waveform(std::make_unique<Waveform>(sessionToEdit, effect, description))
    , controls(sessionToEdit, effect, description, allParameters(description), 3)
{
    addAndMakeVisible(*waveform);
    ThemedLookAndFeel::setColourRole(fileName, appearance::ColourRole::textSecondary);
    addAndMakeVisible(fileName);

    load.setTooltip("Choose the sample (or drop a file on the editor)");
    load.onClick = [this] {
        chooser = std::make_unique<juce::FileChooser>("Load a sample", juce::File(), sampleFilePatterns);
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                             [this](const juce::FileChooser& chosen) {
                                 if (const auto file = chosen.getResult(); file != juce::File())
                                     session.setInstrumentFile(effectId, engine::fx::Sampler::sampleSlot, file);
                             });
    };
    remove.onClick = [this] {
        session.setInstrumentFile(effectId, engine::fx::Sampler::sampleSlot, juce::File());
    };
    for (auto* button : {&load, &remove})
    {
        button->setWantsKeyboardFocus(false);
        addAndMakeVisible(*button);
    }
    addAndMakeVisible(controls);
    refresh();
}

SamplerView::~SamplerView() = default;

void SamplerView::refresh()
{
    const auto file = fileOf(session, effectId, engine::fx::Sampler::sampleSlot);
    fileName.setText(describeFile(session, file), juce::dontSendNotification);
    remove.setEnabled(file != juce::File());
    waveform->refresh();
    controls.refresh();
}

int SamplerView::getPreferredHeight() const noexcept
{
    return 140 + 6 + 26 + 8 + controls.getPreferredHeight();
}

void SamplerView::resized()
{
    auto area = getLocalBounds();
    waveform->setBounds(area.removeFromTop(140));
    area.removeFromTop(6);
    auto row = area.removeFromTop(26);
    remove.setBounds(row.removeFromRight(80));
    row.removeFromRight(4);
    load.setBounds(row.removeFromRight(80));
    row.removeFromRight(8);
    fileName.setBounds(row);
    area.removeFromTop(8);
    controls.setBounds(area);
}

bool SamplerView::isInterestedInFileDrag(const juce::StringArray& files)
{
    return std::any_of(files.begin(), files.end(), [](const auto& path) { return isSampleFile(juce::File(path)); });
}

void SamplerView::filesDropped(const juce::StringArray& files, int, int)
{
    for (const auto& path : files)
        if (const juce::File file(path); isSampleFile(file))
        {
            session.setInstrumentFile(effectId, engine::fx::Sampler::sampleSlot, file);
            return;
        }
}

// ----------------------------------------------------------------------------------

class DrumPadsView::Pad final : public juce::Component
{
public:
    Pad(DrumPadsView& view, int padIndex)
        : owner(view)
        , pad(padIndex)
    {
    }

    void show(const juce::String& newTitle, const juce::String& newDetail, bool isSelected)
    {
        if (newTitle == title && newDetail == detail && isSelected == selected)
            return;
        title = newTitle;
        detail = newDetail;
        selected = isSelected;
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        ThemedLookAndFeel::get(*this).drawDrumPad(g, getLocalBounds().toFloat().reduced(2.0f), title, detail,
                                                  selected, lit);
    }

    void mouseDown(const juce::MouseEvent&) override
    {
        owner.select(pad);
        key = juce::roundToInt(valueOf(owner.session, owner.effectId, owner.description,
                                       "pad" + juce::String(pad + 1) + "Key"));
        if (const auto track = trackOf(owner.session, owner.effectId); track != 0)
            preview.play(track, key, 100);
        lit = true;
        repaint();
    }

    void mouseUp(const juce::MouseEvent&) override
    {
        preview.stop();
        lit = false;
        repaint();
    }

private:
    DrumPadsView& owner;
    const int pad;
    juce::String title;
    juce::String detail;
    bool selected = false;
    bool lit = false;
    int key = 0;
    controller::NotePreview preview{owner.session};
};

namespace
{

constexpr int padRows = 4;
constexpr int padHeight = 54;

std::vector<int> kitParameters()
{
    using engine::fx::DrumSampler;
    return {DrumSampler::velocity, DrumSampler::polyphony, DrumSampler::level};
}

} // namespace

DrumPadsView::DrumPadsView(controller::Session& sessionToEdit, model::ObjectId effect,
                           const engine::fx::EffectDescription& describedAs)
    : session(sessionToEdit)
    , effectId(effect)
    , description(describedAs)
{
    for (int pad = 0; pad < engine::fx::DrumSampler::numPads; ++pad)
    {
        pads.push_back(std::make_unique<Pad>(*this, pad));
        addAndMakeVisible(*pads.back());
    }

    ThemedLookAndFeel::setFontRole(padTitle, appearance::FontRole::heading);
    addAndMakeVisible(padTitle);
    load.setTooltip("Choose the selected pad's sample (or drop a file on a pad)");
    load.onClick = [this] { chooseFile(); };
    remove.onClick = [this] {
        session.setInstrumentFile(effectId, engine::fx::DrumSampler::slotOf(selected), juce::File());
    };
    for (auto* button : {&load, &remove})
    {
        button->setWantsKeyboardFocus(false);
        addAndMakeVisible(*button);
    }

    kitControls = std::make_unique<SectionedEffectEditor>(session, effectId, description, kitParameters(), 1);
    addAndMakeVisible(*kitControls);
    select(0);
}

DrumPadsView::~DrumPadsView() = default;

void DrumPadsView::select(int pad)
{
    using engine::fx::DrumSampler;
    selected = std::clamp(pad, 0, DrumSampler::numPads - 1);
    std::vector<int> indices;
    for (int parameter = 0; parameter < DrumSampler::parametersPerPad; ++parameter)
        indices.push_back(DrumSampler::parameterOf(selected, static_cast<DrumSampler::PadParameter>(parameter)));
    padControls = std::make_unique<GenericEffectEditor>(session, effectId, description, indices, 2);
    addAndMakeVisible(*padControls);
    refresh();
    resized();
}

void DrumPadsView::chooseFile()
{
    chooser = std::make_unique<juce::FileChooser>("Load a sample for the pad", juce::File(), sampleFilePatterns);
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                         [this, pad = selected](const juce::FileChooser& chosen) {
                             if (const auto file = chosen.getResult(); file != juce::File())
                                 session.setInstrumentFile(effectId, engine::fx::DrumSampler::slotOf(pad), file);
                         });
}

void DrumPadsView::refresh()
{
    using engine::fx::DrumSampler;
    for (int pad = 0; pad < DrumSampler::numPads; ++pad)
    {
        const auto& group =
            description.parameters[static_cast<std::size_t>(DrumSampler::parameterOf(pad, DrumSampler::padKey))]
                .group;
        const int key = juce::roundToInt(
            valueOf(session, effectId, description, "pad" + juce::String(pad + 1) + "Key"));
        const auto file = fileOf(session, effectId, DrumSampler::slotOf(pad));
        pads[static_cast<std::size_t>(pad)]->show(group + "  " + engine::fx::keyName(key),
                                                  describeFile(session, file), pad == selected);
    }

    const auto& selectedGroup =
        description.parameters[static_cast<std::size_t>(DrumSampler::parameterOf(selected, DrumSampler::padKey))]
            .group;
    padTitle.setText(selectedGroup, juce::dontSendNotification);
    remove.setEnabled(fileOf(session, effectId, DrumSampler::slotOf(selected)) != juce::File());
    if (padControls != nullptr)
        padControls->refresh();
    kitControls->refresh();
}

int DrumPadsView::getPreferredHeight() const noexcept
{
    return padRows * padHeight + 8 + 26 + 4 + (padControls != nullptr ? padControls->getPreferredHeight() : 0) +
           4 + kitControls->getPreferredHeight();
}

void DrumPadsView::resized()
{
    auto area = getLocalBounds();
    auto grid = area.removeFromTop(padRows * padHeight);
    const int columns = engine::fx::DrumSampler::numPads / padRows;
    const int padWidth = grid.getWidth() / columns;
    for (int pad = 0; pad < engine::fx::DrumSampler::numPads; ++pad)
    {
        // Pad 1 at the bottom left, rows going up.
        const int row = padRows - 1 - pad / columns;
        const int column = pad % columns;
        pads[static_cast<std::size_t>(pad)]->setBounds(grid.getX() + column * padWidth,
                                                       grid.getY() + row * padHeight, padWidth, padHeight);
    }

    area.removeFromTop(8);
    auto row = area.removeFromTop(26);
    remove.setBounds(row.removeFromRight(80));
    row.removeFromRight(4);
    load.setBounds(row.removeFromRight(80));
    row.removeFromRight(8);
    padTitle.setBounds(row);
    area.removeFromTop(4);
    if (padControls != nullptr)
        padControls->setBounds(area.removeFromTop(padControls->getPreferredHeight()));
    area.removeFromTop(4);
    kitControls->setBounds(area.removeFromTop(kitControls->getPreferredHeight()));
}

int DrumPadsView::padAt(juce::Point<int> position) const
{
    for (std::size_t pad = 0; pad < pads.size(); ++pad)
        if (pads[pad]->getBounds().contains(position))
            return static_cast<int>(pad);
    return -1;
}

bool DrumPadsView::isInterestedInFileDrag(const juce::StringArray& files)
{
    return std::any_of(files.begin(), files.end(), [](const auto& path) { return isSampleFile(juce::File(path)); });
}

void DrumPadsView::filesDropped(const juce::StringArray& files, int x, int y)
{
    // Several files fill the pads from the one they were dropped on.
    int pad = padAt({x, y});
    if (pad < 0)
        pad = selected;
    for (const auto& path : files)
    {
        const juce::File file(path);
        if (!isSampleFile(file) || pad >= engine::fx::DrumSampler::numPads)
            continue;
        session.setInstrumentFile(effectId, engine::fx::DrumSampler::slotOf(pad), file);
        ++pad;
    }
}

} // namespace daw::ui::desktop
