#include "daw/engine/io/MicrophoneAccess.h"

namespace daw::engine::io
{

// Windows and Linux do not ask per application here: inputs simply open (Windows'
// own privacy switch, when off, gives silence).

MicrophoneAccess::Status MicrophoneAccess::getStatus()
{
    return Status::granted;
}

void MicrophoneAccess::request(std::function<void(bool granted)> callback)
{
    if (callback)
        callback(true);
}

} // namespace daw::engine::io
