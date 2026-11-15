// SPDX-License-Identifier: GPL-3.0-or-later
// Stashy's Toolbelt — Copyright (C) 2026 Stashy. See LICENSE.
#pragma once

#include "CoreMinimal.h"
#include "Subsystem/ModSubsystem.h"
#include "ToolbeltSettings.h"
#include "ToolbeltPrefsSync.generated.h"

/**
 * Client side: sends this machine's settings to the server once the local player's RCO exists,
 * and again whenever they change (polled every 0.5 s — no reliance on config change events).
 * Spawned per world, never replicated; does nothing on a dedicated server.
 */
UCLASS()
class STASHYSTOOLBELT_API AToolbeltPrefsSync : public AModSubsystem
{
	GENERATED_BODY()
public:
	AToolbeltPrefsSync();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void Poll();

	FTimerHandle PollTimer;
	FToolbeltPrefs LastSent;
	bool bHasSent = false;
};
