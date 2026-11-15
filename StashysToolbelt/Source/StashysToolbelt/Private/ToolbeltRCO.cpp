// SPDX-License-Identifier: GPL-3.0-or-later
// Stashy's Toolbelt — Copyright (C) 2026 Stashy. See LICENSE.
#include "ToolbeltRCO.h"
#include "StashysToolbelt.h"
#include "Net/UnrealNetwork.h"

void UToolbeltRCO::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UToolbeltRCO, bDummy);
}

void UToolbeltRCO::ServerSetPrefs_Implementation(const FToolbeltPrefs& InPrefs)
{
	ReceivedPrefs = InPrefs;
	ReceivedPrefs.Sanitize();   // never trust client values
	bReceivedPrefs = true;
	UE_LOG(LogStashysToolbelt, Verbose, TEXT("Received settings from %s"), *GetNameSafe(GetOuter()));
}
