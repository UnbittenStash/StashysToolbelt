// SPDX-License-Identifier: GPL-3.0-or-later
// Stashy's Toolbelt — Copyright (C) 2026 Stashy. See LICENSE.
#include "StashysToolbelt.h"
#include "ToolbeltSwatch.h"
#include "ToolbeltZoop.h"
#include "ToolbeltStackHeight.h"
#include "ToolbeltGhost.h"
#include "ToolbeltPoleTier.h"

DEFINE_LOG_CATEGORY(LogStashysToolbelt);

void FStashysToolbeltModule::StartupModule()
{
	// Hooks only in the game/server: this module also loads inside editor commandlets (the build).
	if (!WITH_EDITOR)
	{
		ToolbeltSwatch::Install();   // feature 2
		ToolbeltZoop::Install();     // feature 3
		FToolbeltStackHooks::Install();   // feature 1
		FToolbeltGhost::Install();        // feature 4 (cleanup; driven by feature 1's hooks)
		ToolbeltPoleTier::Install();      // feature 5
	}
}

void FStashysToolbeltModule::ShutdownModule()
{
}

IMPLEMENT_MODULE(FStashysToolbeltModule, StashysToolbelt)
