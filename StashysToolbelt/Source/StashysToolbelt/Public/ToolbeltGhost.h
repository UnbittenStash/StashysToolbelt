// SPDX-License-Identifier: GPL-3.0-or-later
// Stashy's Toolbelt — Copyright (C) 2026 Stashy. See LICENSE.
#pragma once

#include "CoreMinimal.h"
#include "Templates/SubclassOf.h"

class UWorld;
class UFGRecipe;
class AFGHologram;
class AFGBuildableHologram;
class AFGPoleHologram;
class AFGStackablePoleHologram;
class AFGSplineHologram;

/**
 * Feature 4 — ghost belt/pipe preview while placing a pole.
 *
 * Families: belt = Conveyor Pole + Stackable Conveyor Pole; pipe = Pipeline Support + Stackable
 * Pipeline Support (vanilla classes; other pole classes only pair with their own class and get
 * no ghost unless they're a known family). While the local player places a pole, a non-buildable
 * belt/pipe hologram is drawn from the last pole of the same family they built:
 *   stack <-> stack: bottom-bottom and top-top; single <-> anything: one ghost to the nearest
 *   level in height.
 * Only within the WorkingArea setting. Colour: yellow from 90% to 120% of the length limit (or when
 * vanilla's own checks would refuse the shape), normal otherwise. Never red (user decision).
 *
 * The ghosts are separate local holograms (never children of the pole hologram, so never built
 * or charged). Stackable frames come from ToolbeltStackHeight.cpp; single-pole frames and the
 * cleanup hooks are installed here. "Built" = server-confirmed (Client_OnRecipeBuilt, forwarded by
 * FToolbeltStackHooks), using a snapshot taken at the build click.
 *
 * FToolbeltGhost is a Friend of AFGConveyorBeltHologram, AFGPipelineHologram and
 * AFGSplineHologram (AccessTransformers.ini) to call their private routing/validation.
 */
class FToolbeltGhost
{
public:
	/** Single-pole frame + click hooks and cleanup hooks. Call from StartupModule inside if (!WITH_EDITOR). */
	static void Install();

	/** Each placement/zoop frame of a local player's stackable pole hologram. TopZ = current zoop Z. */
	static void OnStackFrame(AFGStackablePoleHologram* Hologram, int32 TopZ);

	/** Build click (placement complete) on any hologram; ignored unless it's a local player's pole. */
	static void OnPlacementCompleted(AFGBuildableHologram* Hologram);

	/** Server confirmed a build of Recipe for the local player (forwarded from the stack hooks). */
	static void OnRecipeBuilt(UWorld* World, TSubclassOf<UFGRecipe> Recipe);

	/** Remove any ghosts. */
	static void HideAll();

	/** One-line-per-family summary for the console status command. */
	static FString DescribeState();

	/** Route Ghost from Start to End; returns the EHologramMaterialState vanilla's checks imply.
	 *  A member so the Friend access transformers cover the private routing/validation calls. */
	static uint8 RouteAndValidate(AFGSplineHologram* Ghost, const FVector& Start, const FVector& StartNormal,
		const FVector& End, const FVector& EndNormal);

private:
	static void OnSinglePoleFrame(AFGPoleHologram* Hologram);
};
