#include "daw/model/FormatVersion.h"

namespace daw::model
{

bool canReadFormatVersion(int version) noexcept
{
    return version >= 1 && version <= currentFormatVersion;
}

} // namespace daw::model
