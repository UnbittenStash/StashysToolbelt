// SPDX-License-Identifier: GPL-3.0-or-later
// Stashy's Toolbelt — Copyright (C) 2026 Stashy. See LICENSE.
#pragma once

#include "CoreMinimal.h"

/**
 * Feature 2 — colour (swatch) inheritance for new placements.
 *
 *  2a  Power: a pole/outlet/tower built by dragging a power line from an existing
 *      pole/outlet/tower copies that one's swatch.        Hook: AFGWireHologram::Construct
 *  2b  A belt/lift or pipe whose end connects to an existing belt/lift or pipe copies its
 *      swatch. Through poles this is the belt/pipe on the pole's other side.
 *                                                          Hook: AFGBuildableHologram::Construct
 *  2c  A stackable pole placed onto an existing stack copies the stack's swatch.
 *                                                          Hook: AFGBuildableHologram::Construct
 *
 * Rules: server only; never repaints anything that already existed; copies from one neighbour
 * only (no chains); never from machines/buildings; skips upgrades and blueprint placement.
 * Copies only the swatch + custom colour slot (not pattern/material/skin).
 */
namespace ToolbeltSwatch
{
	/** Install the hooks. Call from StartupModule inside if (!WITH_EDITOR). */
	void Install();
}
