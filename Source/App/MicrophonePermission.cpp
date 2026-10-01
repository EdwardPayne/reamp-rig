#include "MicrophonePermission.h"

#include <juce_events/juce_events.h>

// Non-Apple platforms have no microphone permission gate for desktop apps.
namespace rf::app
{
    MicrophoneAccess getMicrophoneAccess()
    {
        return MicrophoneAccess::granted;
    }

    void requestMicrophoneAccess (std::function<void (bool)> onResult)
    {
        juce::MessageManager::callAsync ([callback = std::move (onResult)]
        {
            if (callback != nullptr)
                callback (true);
        });
    }
}
