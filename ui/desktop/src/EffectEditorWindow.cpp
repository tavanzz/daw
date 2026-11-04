#include "daw/ui/desktop/EffectEditorWindow.h"

#include "daw/engine/fx/BuiltInEffects.h"
#include "daw/engine/fx/Compressor.h"
#include "daw/engine/fx/DrumSampler.h"
#include "daw/engine/fx/Equaliser.h"
#include "daw/engine/fx/Limiter.h"
#include "daw/engine/fx/Sampler.h"
#include "daw/ui/desktop/CompressorView.h"
#include "daw/ui/desktop/EffectSlots.h"
#include "daw/ui/desktop/EqualiserView.h"
#include "daw/ui/desktop/GenericEffectEditor.h"
#include "daw/ui/desktop/InstrumentViews.h"
#include "daw/ui/desktop/ThemedLookAndFeel.h"

#include <numeric>

namespace daw::ui::desktop
{

namespace
{

juce::String ownerName(const controller::Session& session, const controller::ChainOwner& owner)
{
    const auto& project = session.getProject();
    if (owner.kind == controller::ChainOwner::Kind::track)
        if (const int index = project.indexOfTrack(owner.id); index >= 0)
            return project.getTrack(index).getName();
    if (owner.kind == controller::ChainOwner::Kind::bus)
        if (const int index = project.indexOfBus(owner.id); index >= 0)
            return project.getBus(index).getName();
    return "Master";
}

// The parameters an effect shows in its controls: those it uses.
std::vector<int> shownParameters(const engine::fx::EffectDescription& description)
{
    std::vector<int> indices;
    for (std::size_t i = 0; i < description.parameters.size(); ++i)
        indices.push_back(static_cast<int>(i));
    return indices;
}

} // namespace

class EffectEditorWindow::Content final : public juce::Component
{
public:
    Content(controller::Session& sessionToEdit, model::ObjectId effect)
        : session(sessionToEdit)
        , effectId(effect)
    {
        const auto model = session.getEffect(effectId);
        type = model ? model->getType() : juce::String();
        if (auto described = engine::fx::createBuiltInEffect(type))
            description = described->getDescription();

        bypass.setClickingTogglesState(false);
        ThemedLookAndFeel::setColourRole(bypass, appearance::ColourRole::warning);
        bypass.onClick = [this] { session.setEffectBypassed(effectId, !bypass.getToggleState()); };
        addAndMakeVisible(bypass);

        reactivate.onClick = [this] { session.reviveEffect(effectId); };
        addChildComponent(reactivate);
        ThemedLookAndFeel::setColourRole(warning, appearance::ColourRole::error);
        warning.setText("Silenced: it produced invalid output.", juce::dontSendNotification);
        addChildComponent(warning);

        ThemedLookAndFeel::setFontRole(title, appearance::FontRole::heading);
        addAndMakeVisible(title);

        int width = 520;
        if (type == engine::fx::Sampler::typeId)
        {
            sampler = std::make_unique<SamplerView>(session, effectId, description);
            addAndMakeVisible(*sampler);
            width = 820;
        }
        else if (type == engine::fx::DrumSampler::typeId)
        {
            drums = std::make_unique<DrumPadsView>(session, effectId, description);
            addAndMakeVisible(*drums);
            width = 640;
        }
        else if (description.isInstrument)
        {
            // The synth (and any instrument without a view of its own): sections.
            sections = std::make_unique<SectionedEffectEditor>(session, effectId, description,
                                                               shownParameters(description), 3);
            addAndMakeVisible(*sections);
            width = 900;
        }
        else if (type == engine::fx::Equaliser::typeId)
        {
            equaliser = std::make_unique<EqualiserView>(session, effectId);
            equaliser->onBandSelected = [this](int band) { showBand(band); };
            addAndMakeVisible(*equaliser);
            showBand(2);
        }
        else
        {
            if (type == engine::fx::Compressor::typeId || type == engine::fx::Limiter::typeId)
            {
                dynamics = std::make_unique<CompressorView>(session, effectId);
                addAndMakeVisible(*dynamics);
            }
            controls = std::make_unique<GenericEffectEditor>(session, effectId, description,
                                                             shownParameters(description));
            addAndMakeVisible(*controls);
        }

        setSize(width, preferredHeight());
    }

    void refresh(double elapsedSeconds)
    {
        const auto location = session.findEffect(effectId);
        const auto model = session.getEffect(effectId);
        if (!location || !model)
            return;

        const auto name = effectTypeName(type) + " - " + ownerName(session, location->owner);
        title.setText(name, juce::dontSendNotification);
        if (auto* window = findParentComponentOfClass<juce::DocumentWindow>(); window != nullptr && window->getName() != name)
            window->setName(name);
        bypass.setToggleState(model->isBypassed(), juce::dontSendNotification);

        const auto instance = session.getEffectInstance(effectId);
        const bool failed = instance != nullptr && instance->hasFailed();
        if (failed != warning.isVisible())
        {
            warning.setVisible(failed);
            reactivate.setVisible(failed);
        }

        if (equaliser != nullptr)
            equaliser->refresh();
        if (dynamics != nullptr)
            dynamics->refresh(elapsedSeconds);
        if (controls != nullptr)
            controls->refresh();
        if (sections != nullptr)
            sections->refresh();
        if (sampler != nullptr)
            sampler->refresh();
        if (drums != nullptr)
        {
            const int before = drums->getPreferredHeight();
            drums->refresh();
            if (drums->getPreferredHeight() != before)
                resized();
        }
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(8);
        auto header = area.removeFromTop(26);
        bypass.setBounds(header.removeFromRight(70));
        header.removeFromRight(6);
        reactivate.setBounds(header.removeFromRight(90));
        title.setBounds(header.removeFromLeft(220));
        warning.setBounds(header);
        area.removeFromTop(6);

        if (equaliser != nullptr)
        {
            equaliser->setBounds(area.removeFromTop(240));
            area.removeFromTop(6);
        }
        if (dynamics != nullptr)
        {
            dynamics->setBounds(area.removeFromTop(170));
            area.removeFromTop(6);
        }
        if (controls != nullptr)
            controls->setBounds(area);
        if (sections != nullptr)
            sections->setBounds(area);
        if (sampler != nullptr)
            sampler->setBounds(area);
        if (drums != nullptr)
            drums->setBounds(area);
    }

    void paint(juce::Graphics& g) override
    {
        ThemedLookAndFeel::get(*this).drawPanel(g, getLocalBounds().toFloat(),
                                                appearance::ColourRole::panel, false);
    }

private:
    [[nodiscard]] int preferredHeight() const
    {
        int height = 8 + 26 + 6 + 8;
        if (equaliser != nullptr)
            height += 246;
        if (dynamics != nullptr)
            height += 176;
        if (controls != nullptr)
            height += controls->getPreferredHeight();
        if (sections != nullptr)
            height += sections->getPreferredHeight();
        if (sampler != nullptr)
            height += sampler->getPreferredHeight();
        if (drums != nullptr)
            height += drums->getPreferredHeight();
        return height;
    }

    // The equaliser shows the controls of one band (and the output) at a time.
    void showBand(int band)
    {
        const auto parameters = engine::fx::Equaliser::parametersOf(band);
        std::vector<int> indices{parameters.on, parameters.frequency};
        for (const int index : {parameters.gain, parameters.q, parameters.slope})
            if (index >= 0)
                indices.push_back(index);
        indices.push_back(engine::fx::Equaliser::outputGainParameter());

        controls = std::make_unique<GenericEffectEditor>(session, effectId, description, indices);
        addAndMakeVisible(*controls);
        resized();
    }

    controller::Session& session;
    const model::ObjectId effectId;
    juce::String type;
    engine::fx::EffectDescription description;
    juce::Label title;
    juce::TextButton bypass{"Bypass"};
    juce::TextButton reactivate{"Reactivate"};
    juce::Label warning;
    std::unique_ptr<EqualiserView> equaliser;
    std::unique_ptr<CompressorView> dynamics;
    std::unique_ptr<GenericEffectEditor> controls;
    std::unique_ptr<SectionedEffectEditor> sections;
    std::unique_ptr<SamplerView> sampler;
    std::unique_ptr<DrumPadsView> drums;
};

EffectEditorWindow::EffectEditorWindow(controller::Session& session, model::ObjectId effect)
    : juce::DocumentWindow("Effect",
                           juce::Desktop::getInstance().getDefaultLookAndFeel().findColour(
                               juce::ResizableWindow::backgroundColourId),
                           juce::DocumentWindow::closeButton)
    , effectId(effect)
{
    auto owned = std::make_unique<Content>(session, effect);
    content = owned.get();
    setUsingNativeTitleBar(true);
    setContentOwned(owned.release(), true);
    setResizable(true, false);
    centreWithSize(getWidth(), getHeight());
    setVisible(true);
}

EffectEditorWindow::~EffectEditorWindow() = default;

void EffectEditorWindow::refresh(double elapsedSeconds)
{
    content->refresh(elapsedSeconds);
}

void EffectEditorWindow::closeButtonPressed()
{
    if (onClose)
        onClose();
}

EffectEditors::EffectEditors(controller::Session& sessionToEdit)
    : session(sessionToEdit)
{
}

EffectEditors::~EffectEditors() = default;

void EffectEditors::open(model::ObjectId effectId)
{
    for (auto& window : windows)
    {
        if (window->getEffectId() == effectId)
        {
            window->toFront(true);
            return;
        }
    }

    if (!session.findEffect(effectId))
        return;

    auto window = std::make_unique<EffectEditorWindow>(session, effectId);
    window->onClose = [this, effectId] {
        // Later: the window is still in its own callback.
        juce::MessageManager::callAsync([this, effectId] {
            windows.erase(std::remove_if(windows.begin(), windows.end(),
                                         [effectId](const auto& w) { return w->getEffectId() == effectId; }),
                          windows.end());
        });
    };
    windows.push_back(std::move(window));
}

void EffectEditors::refresh(double elapsedSeconds)
{
    windows.erase(std::remove_if(windows.begin(), windows.end(),
                                 [this](const auto& window) {
                                     return !session.findEffect(window->getEffectId()).has_value();
                                 }),
                  windows.end());
    for (auto& window : windows)
        window->refresh(elapsedSeconds);
}

void EffectEditors::closeAll()
{
    windows.clear();
}

} // namespace daw::ui::desktop
