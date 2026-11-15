// SPDX-License-Identifier: GPL-3.0-or-later
// Stashy's Toolbelt — Copyright (C) 2026 Stashy. See LICENSE.
#pragma once

#include "CoreMinimal.h"
#include "Module/GameInstanceModule.h"
#include "Module/GameWorldModule.h"
#include "ToolbeltModules.generated.h"

/**
 * Root Game Instance module (native; found by SML via bRootModule on the CDO).
 * Registers the settings and the RCO. Only one root module per type in this mod.
 */
UCLASS()
class STASHYSTOOLBELT_API UToolbeltRootInstance : public UGameInstanceModule
{
	GENERATED_BODY()
public:
	UToolbeltRootInstance();
	virtual void DispatchLifecycleEvent(ELifecyclePhase Phase) override;
};

/** Root Game World module: spawns the settings sync subsystem. */
UCLASS()
class STASHYSTOOLBELT_API UToolbeltRootGameWorld : public UGameWorldModule
{
	GENERATED_BODY()
public:
	UToolbeltRootGameWorld();
};
