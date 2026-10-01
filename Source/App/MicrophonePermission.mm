#include "MicrophonePermission.h"

#include <juce_events/juce_events.h>

#import <AVFoundation/AVFoundation.h>

namespace rf::app
{
    MicrophoneAccess getMicrophoneAccess()
    {
        switch ([AVCaptureDevice authorizationStatusForMediaType: AVMediaTypeAudio])
        {
            case AVAuthorizationStatusAuthorized:       return MicrophoneAccess::granted;
            case AVAuthorizationStatusNotDetermined:    return MicrophoneAccess::undetermined;
            case AVAuthorizationStatusDenied:
            case AVAuthorizationStatusRestricted:
            default:                                    return MicrophoneAccess::denied;
        }
    }

    void requestMicrophoneAccess (std::function<void (bool)> onResult)
    {
        auto callback = std::make_shared<std::function<void (bool)>> (std::move (onResult));

        [AVCaptureDevice requestAccessForMediaType: AVMediaTypeAudio
                                 completionHandler: ^(BOOL granted)
        {
            // The completion handler runs on an arbitrary queue.
            juce::MessageManager::callAsync ([callback, ok = (bool) granted]
            {
                if (*callback != nullptr)
                    (*callback) (ok);
            });
        }];
    }
}
