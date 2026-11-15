// SPDX-License-Identifier: GPL-3.0-or-later
// Stashy's Toolbelt — Copyright (C) 2026 Stashy. See LICENSE.
#pragma once

#include "CoreMinimal.h"

/**
 * Feature 1 — stackable conveyor poles / pipeline supports remember their height (per session).
 *
 *  - Remembered value = total stack levels (existing levels below + levels just built) of the
 *    last stack the local player actually built, per pole kind (build class). Committed when the
 *    server confirms the build (UFGBuildGunStateBuild::Client_OnRecipeBuilt -> numConstructed).
 *  - Next placement, zoop step: starts at the remembered total; aiming up/down adjusts
 *    *relative* to that start (never snaps to the crosshair).
 *  - Onto an existing stack: builds up to the remembered total; if the stack is already at or
 *    above it, vanilla behaviour.
 *
 * FToolbeltStackHooks is named as a Friend of UFGBuildGunStateBuild in AccessTransformers.ini,
 * because Client_OnRecipeBuilt is protected.
 */
class FToolbeltStackHooks
{
public:
	/** Install the hooks. Call from StartupModule inside if (!WITH_EDITOR). */
	static void Install();

	/** Remembered heights and pending clicks, for the console status command. */
	static FString DescribeState();
};
