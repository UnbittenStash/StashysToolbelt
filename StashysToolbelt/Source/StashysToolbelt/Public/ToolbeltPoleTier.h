// SPDX-License-Identifier: GPL-3.0-or-later
// Stashy's Toolbelt — Copyright (C) 2026 Stashy. See LICENSE.
#pragma once

#include "CoreMinimal.h"

/**
 * Feature 5 — tier of the pole / wall outlet auto-built at the end of a power line dragged from a
 * building (vanilla: Mk1). Setting PowerPoleTierFromBuilding: 1-3, default 2; 1 = untouched vanilla.
 * Falls back to the highest unlocked tier below the chosen one (Mk2 locked -> Mk1).
 *
 * Mechanism: before each AFGWireHologram's BeginPlay, overwrite that instance's
 * mDefaultPowerPoleRecipe / mDefaultPowerPoleWallRecipe (private -> Accessors in
 * AccessTransformers.ini). Dragging from an existing pole keeps vanilla's copy-the-source-pole
 * behaviour (unverified that vanilla's copy wins over the default — check in testing).
 */
namespace ToolbeltPoleTier
{
	/** Install the hook. Call from StartupModule inside if (!WITH_EDITOR). */
	void Install();
}
