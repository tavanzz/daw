#include "daw/engine/io/MicrophoneAccess.h"

#include <juce_events/juce_events.h>

#import <AVFoundation/AVFoundation.h>

#include <memory>

namespace daw::engine::io
{

MicrophoneAccess::Status MicrophoneAccess::getStatus()
{
    switch ([AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeAudio])
    {
    case AVAuthorizationStatusAuthorized:
        return Status::granted;
    case AVAuthorizationStatusNotDetermined:
        return Status::notDetermined;
    case AVAuthorizationStatusDenied:
    case AVAuthorizationStatusRestricted:
        return Status::denied;
    }

    return Status::denied;
}

void MicrophoneAccess::request(std::function<void(bool granted)> callback)
{
    if (getStatus() != Status::notDetermined)
    {
        if (callback)
            callback(getStatus() == Status::granted);
        return;
    }

    // The system shows its dialog and answers on a queue of its own: the answer is
    // passed on to the message thread.
    auto shared = std::make_shared<std::function<void(bool)>>(std::move(callback));
    [AVCaptureDevice requestAccessForMediaType:AVMediaTypeAudio
                             completionHandler:^(BOOL granted) {
                               const bool answer = granted == YES;
                               juce::MessageManager::callAsync([shared, answer] {
                                   if (*shared)
                                       (*shared)(answer);
                               });
                             }];
}

} // namespace daw::engine::io
