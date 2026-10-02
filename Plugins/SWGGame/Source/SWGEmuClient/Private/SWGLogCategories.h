#pragma once

#include "Logging/LogMacros.h"

// Log categories shared between a subsystem and its console commands (Console/). Each is defined once, in the subsystem.
DECLARE_LOG_CATEGORY_EXTERN(LogSWGCrafting, Log, All);
DECLARE_LOG_CATEGORY_EXTERN(LogSWGCombat, Log, All);
DECLARE_LOG_CATEGORY_EXTERN(LogSWGWaypoint, Log, All);
DECLARE_LOG_CATEGORY_EXTERN(LogSWGMission, Log, All);
DECLARE_LOG_CATEGORY_EXTERN(LogSWGRadial, Log, All);
DECLARE_LOG_CATEGORY_EXTERN(LogSWGItemTransfer, Log, All);
