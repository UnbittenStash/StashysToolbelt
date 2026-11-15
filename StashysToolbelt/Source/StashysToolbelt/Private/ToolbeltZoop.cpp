// SPDX-License-Identifier: GPL-3.0-or-later
// Stashy's Toolbelt — Copyright (C) 2026 Stashy. See LICENSE.
#include "ToolbeltZoop.h"
#include "StashysToolbelt.h"
#include "ToolbeltSettings.h"

#include "Patching/NativeHookManager.h"
#include "Equipment/FGBuildGun.h"
#include "Equipment/FGBuildGunBuild.h"
#include "Hologram/FGHologram.h"
#include "Hologram/FGBuildableHologram.h"
#include "Hologram/FGHologramBuildModeDescriptor.h"
#include "GameFramework/Pawn.h"
#include "Engine/World.h"

namespace
{
	/** Per-session state for the one local player on this machine. Game thread only. */
	struct FZoopSession
	{
		TWeakObjectPtr<UWorld> World;
		TWeakObjectPtr<AFGHologram> LastHologram;
		TSubclassOf<UFGBuildGunModeDescriptor> LastMode;
		/** Ticks since the current hologram appeared; mode changes while settling are vanilla's, not the player's. */
		int32 TicksSinceNew = 0;
		bool bDecided = false;

		/** Memory keys the player chose a mode for this session. */
		TSet<FString> PlayerChosen;

		/** After we request zoop, ignore mode changes until it arrives (replication) or we time out. */
		bool bAwaitingOurMode = false;
		double AwaitDeadline = 0.0;
		TSubclassOf<UFGBuildGunModeDescriptor> AwaitedMode;

		void ResetFor(UWorld* InWorld)
		{
			*this = FZoopSession();
			World = InWorld;
		}
	};
	FZoopSession GZoopSession;

	constexpr double AwaitTimeoutSeconds = 1.0;
	/** Let vanilla finish applying its remembered/default mode before we decide (it may take a tick). */
	constexpr int32 SettleTicks = 2;

	/** Same grouping vanilla uses for its per-session build-mode memory. */
	FString MemoryKey(const AFGHologram* Hologram)
	{
		const EHologramBuildModeCategory Category = Hologram->GetBuildModeCategory();
		if (Category == EHologramBuildModeCategory::HBMC_ActorClass)
		{
			return FString::Printf(TEXT("Class:%s"), *GetPathNameSafe(Hologram->GetBuildClass()));
		}
		return FString::Printf(TEXT("Cat:%d"), static_cast<int32>(Category));
	}

	void OnBuildStateTick(UFGBuildGunStateBuild* State)
	{
		AFGBuildGun* Gun = State->GetBuildGun();
		const APawn* Pawn = Gun ? Gun->GetInstigator() : nullptr;
		if (!Pawn || !Pawn->IsLocallyControlled())
		{
			return;   // only the player at this machine; the server sees the result via the build gun's own replication
		}

		UWorld* World = State->GetWorld();
		if (GZoopSession.World.Get() != World)
		{
			GZoopSession.ResetFor(World);   // new session (load, join, back to menu)
		}

		AFGHologram* Hologram = State->GetHologram();
		const TSubclassOf<UFGBuildGunModeDescriptor> Mode = Gun->GetCurrentBuildGunMode();

		// Waiting for our own zoop request to show up.
		if (GZoopSession.bAwaitingOurMode)
		{
			if (Mode.Get() == GZoopSession.AwaitedMode.Get() || (World && World->GetTimeSeconds() > GZoopSession.AwaitDeadline) || Hologram != GZoopSession.LastHologram.Get())
			{
				GZoopSession.bAwaitingOurMode = false;
				GZoopSession.LastMode = Mode;
			}
			if (Hologram == GZoopSession.LastHologram.Get())
			{
				return;
			}
		}

		if (Hologram != GZoopSession.LastHologram.Get())
		{
			// New hologram: let vanilla settle its mode first.
			GZoopSession.LastHologram = Hologram;
			GZoopSession.LastMode = Mode;
			GZoopSession.TicksSinceNew = 0;
			GZoopSession.bDecided = false;
			return;
		}
		if (Hologram && !GZoopSession.bDecided)
		{
			GZoopSession.LastMode = Mode;   // changes while settling are vanilla's own
			if (++GZoopSession.TicksSinceNew < SettleTicks)
			{
				return;
			}
			GZoopSession.bDecided = true;
			AFGBuildableHologram* Buildable = Cast<AFGBuildableHologram>(Hologram);
			if (!Buildable || !Buildable->IsZoopBuildModeSupported())
			{
				return;
			}
			const TSubclassOf<UFGHologramBuildModeDescriptor> Zoop = Buildable->GetmBuildModeZoop();   // AT accessor
			if (!Zoop || Mode.Get() == Zoop.Get() || GZoopSession.PlayerChosen.Contains(MemoryKey(Hologram)))
			{
				return;
			}
			// Also respect vanilla's own memory: we only ever set zoop, so a remembered mode that is
			// neither zoop nor the default was picked by the player (covers a respawned hologram).
			const TSubclassOf<UFGHologramBuildModeDescriptor> VanillaLast = State->GetLastBuildModeForCategory(
				static_cast<uint8>(Hologram->GetBuildModeCategory()), Hologram->GetBuildClass());
			if (VanillaLast && VanillaLast.Get() != Zoop.Get() && VanillaLast.Get() != Hologram->GetDefaultBuildGunMode().Get())
			{
				GZoopSession.PlayerChosen.Add(MemoryKey(Hologram));
				return;
			}
			if (!UToolbeltConfig::ReadLocal(Gun).bZoopByDefault)
			{
				return;
			}
			Gun->SetCurrentBuildGunMode(Zoop);
			GZoopSession.bAwaitingOurMode = true;
			GZoopSession.AwaitedMode = Zoop;
			GZoopSession.AwaitDeadline = World ? World->GetTimeSeconds() + AwaitTimeoutSeconds : 0.0;
			UE_LOG(LogStashysToolbelt, Verbose, TEXT("Zoop default applied to %s"), *GetNameSafe(Hologram));
			return;
		}

		// Same hologram, mode changed: the player picked it. Never override that key again this session.
		if (Hologram && Mode.Get() != GZoopSession.LastMode.Get())
		{
			GZoopSession.PlayerChosen.Add(MemoryKey(Hologram));
			GZoopSession.LastMode = Mode;
		}
	}
}

FString ToolbeltZoop::DescribeState()
{
	FString Out = FString::Printf(TEXT("  Zoop default: %d build-mode group(s) where you picked a mode this session\n"), GZoopSession.PlayerChosen.Num());
	for (const FString& Key : GZoopSession.PlayerChosen)
	{
		Out += FString::Printf(TEXT("    %s\n"), *Key);
	}
	return Out;
}

void ToolbeltZoop::Install()
{
	SUBSCRIBE_UOBJECT_METHOD_AFTER(UFGBuildGunStateBuild, TickState_Implementation,
		[](UFGBuildGunStateBuild* Self, float DeltaTime)
		{
			OnBuildStateTick(Self);
		});
	UE_LOG(LogStashysToolbelt, Log, TEXT("Zoop default hook installed"));
}
