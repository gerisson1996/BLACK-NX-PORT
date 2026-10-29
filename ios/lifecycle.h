#pragma once

// iOS: starts listening to UIKit's active/inactive notifications, which arrive before SDL's background events.
void IosLifecycleStart();
