#pragma once

#include <functional>

namespace rf::app
{
    /*  macOS microphone access (PROMPT.md section 4.7). Without it CoreAudio delivers silence
        on every input, including audio interfaces, so the app asks before it opens an input
        and says so clearly when access is denied.

        Other platforms: always granted.
    */
    enum class MicrophoneAccess { granted, denied, undetermined };

    MicrophoneAccess getMicrophoneAccess();

    /** Asks the user (shows the system dialog once) without blocking. `onResult` is called on
        the message thread with true when access was granted. */
    void requestMicrophoneAccess (std::function<void (bool granted)> onResult);
}
