// SPDX-License-Identifier: GPL-3.0-or-later
// Stashy's Toolbelt — Copyright (C) 2026 Stashy. See LICENSE.
#pragma once

#include "CoreMinimal.h"

/**
 * Feature 3 — zoop as the default build mode, per session.
 *
 * Reacts *after* vanilla has picked a mode for a new hologram (from UFGBuildGunStateBuild's
 * tick), instead of relying on vanilla's private spawn path:
 *  - New hologram that supports zoop, not in zoop, and the player hasn't picked a mode for
 *    this hologram's memory key this session -> switch to zoop.
 *  - A mode change while the same hologram stays up is the player's choice -> remembered for
 *    the rest of the session, and we never override it again.
 * Memory key mirrors vanilla: the hologram's build-mode category (Architecture, Pipe), or its
 * build class for HBMC_ActorClass. Local player only; cleared when the world changes.
 */
namespace ToolbeltZoop
{
	/** Install the hook. Call from StartupModule inside if (!WITH_EDITOR). */
	void Install();

	/** Build-mode groups the player picked a mode for this session, for the console status command. */
	FString DescribeState();
}
