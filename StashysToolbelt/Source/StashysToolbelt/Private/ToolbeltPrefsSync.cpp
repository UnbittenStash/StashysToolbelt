// SPDX-License-Identifier: GPL-3.0-or-later
// Stashy's Toolbelt — Copyright (C) 2026 Stashy. See LICENSE.
#include "ToolbeltPrefsSync.h"
#include "StashysToolbelt.h"
#include "ToolbeltRCO.h"
#include "Engine/World.h"
#include "FGPlayerController.h"
#include "TimerManager.h"

AToolbeltPrefsSync::AToolbeltPrefsSync()
{
	ReplicationPolicy = ESubsystemReplicationPolicy::SpawnLocal;
}

void AToolbeltPrefsSync::BeginPlay()
{
	Super::BeginPlay();
	if (IsRunningDedicatedServer())
	{
		return;   // no local player, nothing to send
	}
	// Short interval: until the first send the server treats this player as vanilla.
	GetWorldTimerManager().SetTimer(PollTimer, this, &AToolbeltPrefsSync::Poll, 0.5f, true, 0.1f);
}

void AToolbeltPrefsSync::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(PollTimer);
	Super::EndPlay(EndPlayReason);
}

void AToolbeltPrefsSync::Poll()
{
	// Client-only path (guarded in BeginPlay): the first player controller is the local one.
	AFGPlayerController* PC = Cast<AFGPlayerController>(GetWorld()->GetFirstPlayerController());
	if (!PC || !PC->IsLocalController())
	{
		return;
	}
	if (PC->HasAuthority())
	{
		return;   // single player / listen host: the server reads this machine's config directly
	}
	UToolbeltRCO* RCO = PC->GetRemoteCallObjectOfClass<UToolbeltRCO>();
	if (!RCO)
	{
		return;   // not replicated yet; try again next poll
	}
	const FToolbeltPrefs Now = UToolbeltConfig::ReadLocal(this);
	if (!bHasSent || Now != LastSent)
	{
		RCO->ServerSetPrefs(Now);
		LastSent = Now;
		bHasSent = true;
		UE_LOG(LogStashysToolbelt, Log, TEXT("Settings sent to server"));
	}
}
