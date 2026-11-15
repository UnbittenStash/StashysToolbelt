// SPDX-License-Identifier: GPL-3.0-or-later
// Stashy's Toolbelt — Copyright (C) 2026 Stashy. See LICENSE.
#include "ToolbeltModules.h"
#include "StashysToolbelt.h"
#include "ToolbeltSettings.h"
#include "ToolbeltRCO.h"
#include "ToolbeltPrefsSync.h"

UToolbeltRootInstance::UToolbeltRootInstance()
{
	bRootModule = true;
	ModConfigurations.Add(UToolbeltConfig::StaticClass());   // registered by Super in INITIALIZATION
	RemoteCallObjects.Add(UToolbeltRCO::StaticClass());
}

void UToolbeltRootInstance::DispatchLifecycleEvent(ELifecyclePhase Phase)
{
	if (Phase == ELifecyclePhase::CONSTRUCTION)
	{
		// The settings tree must exist on the config CDO before SML copies it at registration.
		UToolbeltConfig::EnsureRootSection();
	}
	Super::DispatchLifecycleEvent(Phase);
}

UToolbeltRootGameWorld::UToolbeltRootGameWorld()
{
	bRootModule = true;
	ModSubsystems.Add(AToolbeltPrefsSync::StaticClass());
}
