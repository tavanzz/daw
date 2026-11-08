#include "daw/ui/desktop/PluginEditorWindow.h"

#include "daw/controller/PluginCatalogue.h"
#include "daw/engine/plugins/PluginEffect.h"
#include "daw/ui/desktop/GenericEffectEditor.h"
#include "daw/ui/desktop/ThemedLookAndFeel.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <algorithm>
#include <cmath>

namespace daw::ui::desktop
{

namespace
{

constexpr int minimumWidth = 480;
constexpr int margin = 8;
constexpr int headerHeight = margin + 24 + 4 + 20 + 6;
constexpr int placeholderHeight = 110;

juce::String percent(float fraction)
{
    return juce::String(static_cast<int>(std::lround(fraction * 100.0f))) + " %";
}

bool isOnSomeDisplay(juce::Point<int> topLeft)
{
    // The title bar must be reachable: a display that was there may be gone.
    for (const auto& display : juce::Desktop::getInstance().getDisplays().displays)
        if (display.userBounds.contains(topLeft.translated(40, 10).toFloat()))
            return true;
    return false;
}

} // namespace

class PluginEditorWindow::Content final : public juce::Component, private juce::ComponentListener
{
public:
    Content(controller::Session& sessionToEdit, model::ObjectId effect, const controller::PluginLoads& pluginLoads)
        : session(sessionToEdit)
        , effectId(effect)
        , loads(pluginLoads)
    {
        ThemedLookAndFeel::setFontRole(title, appearance::FontRole::heading);
        addAndMakeVisible(title);
        ThemedLookAndFeel::setFontRole(info, appearance::FontRole::caption);
        ThemedLookAndFeel::setColourRole(info, appearance::ColourRole::textSecondary);
        addAndMakeVisible(info);
        ThemedLookAndFeel::setFontRole(slow, appearance::FontRole::caption);
        ThemedLookAndFeel::setColourRole(slow, appearance::ColourRole::warning);
        addChildComponent(slow);

        bypass.setClickingTogglesState(false);
        ThemedLookAndFeel::setColourRole(bypass, appearance::ColourRole::warning);
        bypass.onClick = [this] { session.setEffectBypassed(effectId, !bypass.getToggleState()); };
        addAndMakeVisible(bypass);

        view.onClick = [this] {
            showPluginEditor = !showPluginEditor;
            build();
        };
        addChildComponent(view);

        reactivate.onClick = [this] { session.reviveEffect(effectId); };
        addChildComponent(reactivate);
        ThemedLookAndFeel::setFontRole(failure, appearance::FontRole::caption);
        ThemedLookAndFeel::setColourRole(failure, appearance::ColourRole::error);
        failure.setText("Silenced: it produced invalid output.", juce::dontSendNotification);
        addChildComponent(failure);

        message.setJustificationType(juce::Justification::centred);
        addChildComponent(message);
        load.onClick = [this] { session.allowPlugin(effectId); };
        addChildComponent(load);

        body.setScrollBarsShown(true, false);
        addChildComponent(body);

        build();
    }

    ~Content() override { closeEditor(); }

    void refresh()
    {
        const auto location = session.findEffect(effectId);
        const auto model = session.getEffect(effectId);
        if (!location || !model)
            return;
        const auto reference = model->getPlugin().value_or(model::PluginReference{});

        const auto name = reference.name + " - " + chainOwnerName(session, location->owner);
        title.setText(name, juce::dontSendNotification);
        if (auto* window = findParentComponentOfClass<juce::DocumentWindow>(); window != nullptr && window->getName() != name)
            window->setName(name);
        bypass.setToggleState(model->isBypassed(), juce::dontSendNotification);

        if (session.sharePlugin(effectId) != plugin || session.getPluginStatus(effectId) != status)
            build();

        juce::StringArray facts{reference.format};
        if (reference.manufacturer.isNotEmpty())
            facts.add(reference.manufacturer);
        if (plugin != nullptr)
        {
            const int latency = plugin->getLatency();
            facts.add(latency == 0 ? juce::String("no latency") : "latency " + juce::String(latency) + " samples");
        }
        const auto* cost = loads.find(effectId);
        if (cost != nullptr)
            facts.add("CPU " + percent(cost->average));
        info.setText(facts.joinIntoString("  |  "), juce::dontSendNotification);

        const bool isSlow = cost != nullptr && cost->slow;
        if (isSlow)
            slow.setText(cost->average > controller::PluginLoads::slowAverage
                             ? "Slow: it takes " + percent(cost->average) + " of each block"
                             : juce::String("Slow: it overran a block"),
                         juce::dontSendNotification);
        const auto instance = session.getEffectInstance(effectId);
        const bool failed = instance != nullptr && instance->hasFailed();
        if (isSlow != slow.isVisible() || failed != failure.isVisible())
        {
            slow.setVisible(isSlow);
            failure.setVisible(failed);
            reactivate.setVisible(failed);
            resized();
        }

        if (generic != nullptr)
            generic->refresh();
    }

    [[nodiscard]] bool isResizable() const noexcept
    {
        if (editor != nullptr)
            return editor->isResizable();
        return generic != nullptr;
    }

    [[nodiscard]] bool isShowingPluginEditor() const noexcept { return editor != nullptr; }

    void resized() override
    {
        auto area = getLocalBounds().reduced(margin, 0);
        area.removeFromTop(margin);
        auto top = area.removeFromTop(24);
        bypass.setBounds(top.removeFromRight(70));
        if (view.isVisible())
        {
            top.removeFromRight(6);
            view.setBounds(top.removeFromRight(110));
        }
        title.setBounds(top);
        area.removeFromTop(4);

        auto second = area.removeFromTop(20);
        if (reactivate.isVisible())
        {
            reactivate.setBounds(second.removeFromRight(90));
            second.removeFromRight(6);
        }
        if (failure.isVisible())
            failure.setBounds(second.removeFromRight(240));
        if (slow.isVisible())
            slow.setBounds(second.removeFromRight(240));
        info.setBounds(second);

        auto bodyArea = getLocalBounds().withTrimmedTop(headerHeight);
        if (editor != nullptr)
        {
            const juce::ScopedValueSetter<bool> laying(layingOut, true);
            if (editor->isResizable())
                editor->setBounds(bodyArea);
            else
                editor->setTopLeftPosition(bodyArea.getX() + std::max(0, (bodyArea.getWidth() - editor->getWidth()) / 2),
                                           bodyArea.getY());
        }
        body.setBounds(bodyArea.reduced(margin, 0).withTrimmedBottom(margin));
        if (generic != nullptr)
            generic->setSize(body.getMaximumVisibleWidth(), generic->getPreferredHeight());

        auto placeholder = bodyArea.reduced(margin * 2);
        load.setBounds(placeholder.removeFromBottom(26).withSizeKeepingCentre(120, 26));
        message.setBounds(placeholder);
    }

    void paint(juce::Graphics& g) override
    {
        ThemedLookAndFeel::get(*this).drawPanel(g, getLocalBounds().toFloat(), appearance::ColourRole::panel, false);
    }

private:
    // The plugin's editor, the generic one, or why there is none; and the size for it.
    void build()
    {
        closeEditor();
        body.setViewedComponent(nullptr, false);
        generic.reset();

        plugin = session.sharePlugin(effectId);
        status = session.getPluginStatus(effectId);
        const bool hasEditor = plugin != nullptr && plugin->getPlugin().hasEditor();
        view.setVisible(hasEditor);
        view.setButtonText(showPluginEditor ? "Generic Editor" : "Plugin Editor");

        if (plugin != nullptr && hasEditor && showPluginEditor)
        {
            // The editor goes before the plugin: it is a member declared after it.
            editor.reset(plugin->getPlugin().createEditorAndMakeActive());
            if (editor != nullptr)
            {
                addAndMakeVisible(*editor);
                editor->addComponentListener(this);
            }
        }
        if (plugin != nullptr && editor == nullptr)
        {
            std::vector<int> shown;
            const auto& parameters = plugin->getDescription().parameters;
            for (std::size_t i = 0; i < parameters.size(); ++i)
                if (parameters[i].automatable)
                    shown.push_back(static_cast<int>(i));
            auto* hosted = plugin.get();
            GenericEffectEditor::PluginHooks hooks;
            hooks.value = [hosted](int index) { return hosted->getParameter(index); };
            hooks.text = [hosted](int index, float normalised) { return hosted->getParameterText(index, normalised); };
            generic = std::make_unique<GenericEffectEditor>(session, effectId, plugin->getDescription(), shown, 1,
                                                            std::move(hooks));
            body.setViewedComponent(generic.get(), false);
        }
        body.setVisible(generic != nullptr);

        message.setText(placeholderText(), juce::dontSendNotification);
        message.setVisible(plugin == nullptr);
        load.setVisible(plugin == nullptr && status == controller::PluginStatus::off);

        fitToBody();
        resized();
        repaint();
    }

    [[nodiscard]] juce::String placeholderText() const
    {
        const auto problem = session.getPluginProblem(effectId);
        const juce::String kept = "Its settings are kept and saved as they are.";
        switch (status)
        {
        case controller::PluginStatus::loading:
            return "Loading...";
        case controller::PluginStatus::missing:
            return "Missing: " + (problem.isNotEmpty() ? problem : juce::String("not installed")) + ".\n" + kept;
        case controller::PluginStatus::failed:
            return "Failed: " + (problem.isNotEmpty() ? problem : juce::String("it did not load")) + ".\n" + kept;
        case controller::PluginStatus::off:
            return "Not loaded: kept from loading (after a crash, or opened without plugins).";
        case controller::PluginStatus::ready:
            break;
        }
        return {};
    }

    void fitToBody()
    {
        int width = minimumWidth;
        int height = headerHeight + placeholderHeight;
        if (editor != nullptr)
        {
            width = std::max(width, editor->getWidth());
            height = headerHeight + editor->getHeight();
        }
        else if (generic != nullptr)
        {
            width = std::max(width, getWidth());
            height = headerHeight + std::clamp(generic->getPreferredHeight() + margin, 120, 640);
        }
        const juce::ScopedValueSetter<bool> laying(layingOut, true);
        setSize(width, height);
    }

    void closeEditor()
    {
        if (editor == nullptr)
            return;
        editor->removeComponentListener(this);
        editor.reset(); // tells the plugin itself

    }

    // The plugin resized its editor.
    void componentMovedOrResized(juce::Component& component, bool, bool wasResized) override
    {
        if (wasResized && &component == editor.get() && !layingOut)
            fitToBody();
    }

    controller::Session& session;
    const model::ObjectId effectId;
    const controller::PluginLoads& loads;
    std::shared_ptr<engine::plugins::PluginEffect> plugin; // outlives its editor
    controller::PluginStatus status = controller::PluginStatus::loading;
    std::unique_ptr<juce::AudioProcessorEditor> editor;
    std::unique_ptr<GenericEffectEditor> generic;
    bool showPluginEditor = true;
    bool layingOut = false;

    juce::Label title;
    juce::Label info;
    juce::Label slow;
    juce::Label failure;
    juce::TextButton bypass{"Bypass"};
    juce::TextButton view;
    juce::TextButton reactivate{"Reactivate"};
    juce::Label message;
    juce::TextButton load{"Load"};
    juce::Viewport body;
};

PluginEditorWindow::PluginEditorWindow(controller::Session& session, model::ObjectId effect,
                                       WindowPositions& positionsToUse, const controller::PluginLoads& loads)
    : EditorWindow("Plugin")
    , effectId(effect)
    , positions(positionsToUse)
{
    if (const auto model = session.getEffect(effect))
        if (const auto reference = model->getPlugin())
            positionKey = controller::PluginKey{reference->format, reference->uniqueId}.toString();

    auto owned = std::make_unique<Content>(session, effect, loads);
    content = owned.get();
    setUsingNativeTitleBar(true);
    setContentOwned(owned.release(), true);
    setResizable(content->isResizable(), false);
    setResizeLimits(minimumWidth, headerHeight + 80, 8192, 8192);

    if (const auto saved = positions.get(positionKey); saved && isOnSomeDisplay(*saved))
        setTopLeftPosition(*saved);
    else
        centreWithSize(getWidth(), getHeight());
    // Over the app's windows; refresh() hides it while another app is in front.
    setAlwaysOnTop(true);
    setVisible(true);
    placed = true;
}

PluginEditorWindow::~PluginEditorWindow()
{
    // The plugin's editor goes while the window is still whole.
    clearContentComponent();
}

void PluginEditorWindow::refresh(double)
{
    content->refresh();
    if (isResizable() != content->isResizable())
        setResizable(content->isResizable(), false);

    const bool inFront = juce::Process::isForegroundProcess();
    if (!inFront && isVisible())
    {
        hiddenInBackground = true;
        setVisible(false);
    }
    else if (inFront && hiddenInBackground)
    {
        hiddenInBackground = false;
        setVisible(true);
    }
}

void PluginEditorWindow::moved()
{
    EditorWindow::moved();
    if (placed && positionKey.isNotEmpty())
        positions.set(positionKey, getPosition());
}

bool PluginEditorWindow::isShowingPluginEditor() const noexcept
{
    return content->isShowingPluginEditor();
}

} // namespace daw::ui::desktop
