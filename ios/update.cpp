// No store to ask on iOS: a newer .ipa is the update, so the row only says where it comes from.
#include "core/log.h"

namespace host {

void CheckForUpdates() { LOGI("updates: install a newer .ipa to update"); }

}  // namespace host
