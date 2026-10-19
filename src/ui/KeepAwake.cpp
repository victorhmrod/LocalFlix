#include "KeepAwake.h"

#ifdef _WIN32
#include <windows.h>
#endif

namespace lf::ui {

KeepAwake::~KeepAwake()
{
    setActive(false);
}

void KeepAwake::setActive(bool active)
{
    if (m_active == active) return;
    m_active = active;
#ifdef _WIN32
    // ES_CONTINUOUS keeps the request until it is replaced; a plain ES_CONTINUOUS clears it.
    SetThreadExecutionState(active ? (ES_CONTINUOUS | ES_DISPLAY_REQUIRED | ES_SYSTEM_REQUIRED) : ES_CONTINUOUS);
#endif
}

}  // namespace lf::ui
