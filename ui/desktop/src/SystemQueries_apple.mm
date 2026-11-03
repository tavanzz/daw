#include "SystemQueries.h"

#import <AppKit/AppKit.h>

namespace daw::ui::desktop::detail
{

bool querySystemReducesMotion()
{
    return [[NSWorkspace sharedWorkspace] accessibilityDisplayShouldReduceMotion] == YES;
}

struct WindowOcclusion::Native
{
    NSWindow* window = nil;
    id observer = nil;
};

WindowOcclusion::WindowOcclusion(juce::Component& component, std::function<void()> onChange)
    : native(std::make_unique<Native>())
{
    auto* peer = component.getPeer();
    auto* view = peer != nullptr ? static_cast<NSView*>(peer->getNativeHandle()) : nil;
    native->window = view != nil ? [view window] : nil;

    if (native->window == nil)
        return;

    auto callback = std::make_shared<std::function<void()>>(std::move(onChange));
    native->observer = [[NSNotificationCenter defaultCenter]
        addObserverForName:NSWindowDidChangeOcclusionStateNotification
                    object:native->window
                     queue:[NSOperationQueue mainQueue]
                usingBlock:^(NSNotification*) {
                  if (*callback)
                      (*callback)();
                }];
}

WindowOcclusion::~WindowOcclusion()
{
    if (native->observer != nil)
        [[NSNotificationCenter defaultCenter] removeObserver:native->observer];
}

bool WindowOcclusion::isCovered() const
{
    return native->window != nil
           && ([native->window occlusionState] & NSWindowOcclusionStateVisible) == 0;
}

} // namespace daw::ui::desktop::detail
