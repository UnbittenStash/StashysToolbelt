// SPDX-License-Identifier: GPL-3.0-or-later
// Stashy's Toolbelt — Copyright (C) 2026 Stashy. See LICENSE.
#include "ToolbeltStackHeight.h"
#include "StashysToolbelt.h"
#include "ToolbeltSettings.h"
#include "ToolbeltGhost.h"

#include "Patching/NativeHookManager.h"
#include "Equipment/FGBuildGunBuild.h"
#include "Hologram/FGHologram.h"
#include "Hologram/FGBuildableHologram.h"
#include "Hologram/FGStackablePoleHologram.h"
#include "Buildables/FGBuildablePoleStackable.h"
#include "EngineUtils.h"                     // TActorIterator
#include "FGRecipe.h"
#include "GameFramework/Pawn.h"
#include "Engine/World.h"
#include "Misc/Paths.h"

/*
 * Height control for stackable poles in the zoop step (after the base is placed). Three
 * independent settings:
 *   RememberStackHeight  start at the last built total (if that stack still exists and is within
 *                        the working area), else at 1 level
 *   ScrollToHeight       mouse wheel +/- 1 level
 *   AimToHeight          looking up/down changes the height relative to the start (vanilla-like)
 * Height = start + aim change (if on) + scroll offset, clamped to vanilla's zoop limit.
 * Remember off + scroll off + aim on = untouched vanilla.
 */
namespace
{
	constexpr float StackXYTolerance = 10.f;     // uu; same column

	/** Per-hologram zoop state. */
	struct FActiveStack
	{
		TWeakObjectPtr<AFGStackablePoleHologram> Hologram;
		bool bNeedsInit = true;
		bool bSteer = false;             // we control Z this zoop step
		int32 StartZ = 0;                // starting Z (remembered height or one level)
		int32 ScrollOffset = 0;          // wheel steps
		int32 Baseline = 0;              // vanilla's raw Z on the first zoop frame (aim fallback)
		bool bAimBased = false;          // aim change from the camera ray (can go below the start)
		float FirstAimHeight = 0.f;
		TWeakObjectPtr<AFGBuildable> CountedSnap;   // cache for ExistingLevels
		int32 ExistingLevels = 0;        // levels already in the column we're building onto
		int32 LastZ = 0;                 // Z of the last frame (what gets built)
		uint64 LastScrollFrame = 0;      // one wheel step per frame even if two scroll paths fire
	};

	struct FRemembered
	{
		int32 Total = 0;                 // levels in the column after that build
		FVector Base = FVector::ZeroVector;
	};

	/** Per-session state for the one local player on this machine. Game thread only. */
	struct FStackSession
	{
		TWeakObjectPtr<UWorld> World;
		TMap<FString, FRemembered> Remembered;   // per pole kind (build class path)
		FActiveStack Current;
		/** Live state of the current stack hologram, refreshed every frame. */
		struct FPending
		{
			FString Key;                         // build class path; empty = nothing
			TSubclassOf<UFGRecipe> Recipe;
			int32 Existing = 0;
			int32 LevelsFromZ = 1;
			FVector Base = FVector::ZeroVector;
		};
		FPending Live;
		/** Frozen copies taken at the build click, oldest first; each server confirmation consumes
		 *  the oldest matching one (quick repeated builds, frames during the RPC round-trip). */
		TArray<FPending> Clicked;

		void EnsureWorld(UWorld* InWorld)
		{
			if (World.Get() != InWorld)
			{
				*this = FStackSession();
				World = InWorld;
			}
		}
	};
	FStackSession GStackSession;

	FString KindKey(const AFGHologram* Hologram)
	{
		return GetPathNameSafe(Hologram->GetBuildClass().Get());
	}

	bool IsLocalPlayers(const AFGHologram* Hologram)
	{
		const APawn* Pawn = Hologram ? Hologram->GetConstructionInstigator() : nullptr;
		return Pawn && Pawn->IsLocallyControlled();
	}

	float StackHeightOf(const AFGHologram* Hologram)
	{
		const UClass* BuildClass = Hologram->GetBuildClass();
		const AFGBuildablePoleStackable* CDO = BuildClass ? Cast<AFGBuildablePoleStackable>(BuildClass->GetDefaultObject()) : nullptr;
		return CDO ? CDO->GetStackHeight() : 0.f;
	}

	/** Levels a given zoop Z produces (1 = a single pole), from the hologram's own geometry.
	 *  Uses the difference of two ConvertZoopToWorldLocation calls, so it doesn't matter whether
	 *  that returns world or pole-relative positions. */
	int32 LevelsForZ(const AFGStackablePoleHologram* Hologram, int32 Z)
	{
		const float H = StackHeightOf(Hologram);
		if (H <= KINDA_SMALL_NUMBER)
		{
			return Z + 1;   // fallback assumption: zoop 0 = one pole
		}
		const float Rise = Hologram->ConvertZoopToWorldLocation(FIntVector(0, 0, Z)).Z
		                 - Hologram->ConvertZoopToWorldLocation(FIntVector(0, 0, 0)).Z;
		return 1 + FMath::RoundToInt(FMath::Abs(Rise) / H);
	}

	int32 ZForLevels(const AFGStackablePoleHologram* Hologram, int32 Levels)
	{
		const int32 Max = Hologram->GetMaxZoopAmount();   // callers ensure > 0
		for (int32 Z = 0; Z <= Max; ++Z)
		{
			if (LevelsForZ(Hologram, Z) >= Levels)
			{
				return Z;
			}
		}
		return Max;
	}

	/**
	 * Poles of PoleClass in the column at XY. TActorIterator with a class only visits actors of that
	 * class (UObject class hash), so the cost scales with the number of poles of this kind, not with
	 * every buildable in the world (AFGBuildableSubsystem::GetTypedBuildable casts all of them).
	 */
	TArray<float> ColumnHeights(UWorld* World, const UClass* PoleClass, const FVector& AtXY)
	{
		TArray<float> Z;
		if (!World || !PoleClass)
		{
			return Z;
		}
		for (TActorIterator<AFGBuildablePoleStackable> It(World, const_cast<UClass*>(PoleClass)); It; ++It)
		{
			const AFGBuildablePoleStackable* Pole = *It;
			if (Pole && Pole->GetClass() == PoleClass
				&& FVector2D::Distance(FVector2D(Pole->GetActorLocation()), FVector2D(AtXY)) <= StackXYTolerance)
			{
				Z.Add(Pole->GetActorLocation().Z);
			}
		}
		return Z;
	}

	/** Contiguous levels in the column at and below Top (Top counts as 1). 0 if no Top. */
	int32 CountLevelsAtAndBelow(const AFGBuildable* Top)
	{
		const AFGBuildablePoleStackable* TopPole = Cast<AFGBuildablePoleStackable>(Top);
		if (!TopPole)
		{
			return 0;
		}
		const float H = TopPole->GetStackHeight();
		if (H <= KINDA_SMALL_NUMBER)
		{
			return 1;
		}
		const FVector TopLoc = TopPole->GetActorLocation();
		TArray<float> ColumnZ = ColumnHeights(TopPole->GetWorld(), TopPole->GetClass(), TopLoc);
		ColumnZ.RemoveAll([&TopLoc](float Z) { return Z > TopLoc.Z + 1.f; });
		ColumnZ.Sort([](float A, float B) { return A > B; });   // top first
		int32 Levels = 1;
		float Expected = TopLoc.Z - H;
		for (int32 i = 1; i < ColumnZ.Num(); ++i)
		{
			if (FMath::Abs(ColumnZ[i] - Expected) <= H * 0.25f)
			{
				++Levels;
				Expected -= H;
			}
			else if (ColumnZ[i] < Expected - H * 0.25f)
			{
				break;   // gap: column ends
			}
		}
		return Levels;
	}

	/**
	 * Height (world Z) where the aim ray passes closest to the vertical line through the stack base.
	 * Continuous and can go below the base, unlike vanilla's clamped zoop. False if unusable.
	 */
	bool AimHeightOnColumn(const FHitResult& Hit, const FVector& Base, float& OutHeight)
	{
		const FVector D = (Hit.TraceEnd - Hit.TraceStart).GetSafeNormal();
		if (D.IsNearlyZero())
		{
			return false;   // trace data missing
		}
		const FVector U = FVector::UpVector;
		const FVector W0 = Hit.TraceStart - Base;
		const float B = FVector::DotProduct(D, U);
		const float Denom = 1.f - B * B;          // |D| = |U| = 1
		if (Denom < 1e-3f)
		{
			return false;   // looking straight up/down the column
		}
		const float Dd = FVector::DotProduct(D, W0);
		const float E = FVector::DotProduct(U, W0);
		const float T = (E - B * Dd) / Denom;     // parameter along the column line
		OutHeight = Base.Z + T;
		return true;
	}

	FActiveStack& StateFor(AFGStackablePoleHologram* Hologram)
	{
		GStackSession.EnsureWorld(Hologram->GetWorld());
		if (GStackSession.Current.Hologram.Get() != Hologram)
		{
			GStackSession.Current = FActiveStack();
			GStackSession.Current.Hologram = Hologram;
		}
		return GStackSession.Current;
	}

	void RefreshExisting(FActiveStack& State, AFGStackablePoleHologram* Hologram)
	{
		AFGBuildable* Snap = Hologram->GetSnappedBuilding();
		if (State.CountedSnap.Get() != Snap)
		{
			State.CountedSnap = Snap;
			State.ExistingLevels = CountLevelsAtAndBelow(Snap);
		}
	}

	void RecordPending(const FActiveStack& State, AFGStackablePoleHologram* Hologram)
	{
		auto& Live = GStackSession.Live;
		Live.Key = KindKey(Hologram);
		Live.Recipe = Hologram->GetRecipe();
		Live.Existing = State.ExistingLevels;
		Live.LevelsFromZ = LevelsForZ(Hologram, State.LastZ);
		Live.Base = Hologram->GetActorLocation();
	}

	/**
	 * The remembered total for this kind, if it should apply here: the stack it came from still
	 * exists, and this placement is within the working area of it. A dismantled stack is forgotten.
	 */
	const FRemembered* UsableRemembered(AFGStackablePoleHologram* Hologram, const FToolbeltPrefs& Prefs)
	{
		const FString Key = KindKey(Hologram);
		const FRemembered* R = GStackSession.Remembered.Find(Key);
		if (!R)
		{
			return nullptr;
		}
		if (ColumnHeights(Hologram->GetWorld(), Hologram->GetBuildClass(), R->Base).Num() == 0)
		{
			GStackSession.Remembered.Remove(Key);   // that stack is gone: start fresh
			return nullptr;
		}
		if (FVector::Dist2D(R->Base, Hologram->GetActorLocation()) > Prefs.WorkingArea * 100.f)
		{
			return nullptr;                         // working somewhere else: start at 1 (kept for coming back)
		}
		return R;
	}

	bool InZoopStep(const AFGStackablePoleHologram* Hologram)
	{
		return Hologram->GetmBuildableHologramBuildStep() == EBuildableHologramBuildStep::BHBS_Zoop;   // AT accessor
	}

	// Placement step (before the first click): positioning and snapping.
	void OnPlacementFrame(AFGStackablePoleHologram* Hologram)
	{
		if (!IsLocalPlayers(Hologram) || InZoopStep(Hologram))
		{
			return;   // never reset the zoop step's start mid-adjustment
		}
		FActiveStack& State = StateFor(Hologram);
		State.bNeedsInit = true;   // next zoop frame starts fresh
		State.LastZ = 0;
		RefreshExisting(State, Hologram);
		RecordPending(State, Hologram);   // a single pole built from this step
		FToolbeltGhost::OnStackFrame(Hologram, 0);
	}

	void ApplyZ(FActiveStack& State, AFGStackablePoleHologram* Hologram, int32 Z, int32 RawZ)
	{
		if (Z != RawZ)
		{
			Hologram->SetZoopAmount(FIntVector(0, 0, Z));
		}
		State.LastZ = Z;
		RecordPending(State, Hologram);
		FToolbeltGhost::OnStackFrame(Hologram, Z);
	}

	// Zoop step (after the first click): vanilla has just set Z from the aim.
	void OnZoopFrame(AFGStackablePoleHologram* Hologram, const FHitResult& Hit)
	{
		if (!IsLocalPlayers(Hologram))
		{
			return;
		}
		FActiveStack& State = StateFor(Hologram);
		const FToolbeltPrefs Prefs = UToolbeltConfig::ReadLocal(Hologram);
		const int32 RawZ = Hologram->GetmDesiredZoop().Z;   // AT accessor
		const int32 Max = Hologram->GetMaxZoopAmount();
		const float H = StackHeightOf(Hologram);
		const FVector Base = Hologram->GetActorLocation();

		if (State.bNeedsInit)
		{
			State.bNeedsInit = false;
			State.ScrollOffset = 0;
			State.Baseline = RawZ;
			State.bAimBased = H > KINDA_SMALL_NUMBER && AimHeightOnColumn(Hit, Base, State.FirstAimHeight);
			RefreshExisting(State, Hologram);
			// Never exceed vanilla's zoop limit; if the hologram reports none, leave it to vanilla.
			State.bSteer = Max > 0 && !Prefs.IsVanillaStackHeight();
			State.StartZ = 0;
			if (State.bSteer && Prefs.bRememberStackHeight)
			{
				if (const FRemembered* R = UsableRemembered(Hologram, Prefs))
				{
					const int32 NewLevels = R->Total - State.ExistingLevels;
					// Existing stack already at/above the remembered total: start at one new level.
					State.StartZ = NewLevels >= 1 ? ZForLevels(Hologram, NewLevels) : 0;
				}
			}
		}
		if (!State.bSteer)
		{
			ApplyZ(State, Hologram, RawZ, RawZ);
			return;
		}
		int32 AimDelta = 0;
		if (Prefs.bAimToHeight)
		{
			float AimHeight = 0.f;
			AimDelta = (State.bAimBased && AimHeightOnColumn(Hit, Base, AimHeight))
				? FMath::RoundToInt((AimHeight - State.FirstAimHeight) / H)
				: RawZ - State.Baseline;
		}
		const int32 Z = FMath::Clamp(State.StartZ + AimDelta + State.ScrollOffset, 0, Max);
		State.ScrollOffset = Z - State.StartZ - AimDelta;   // no hidden overshoot past the limits
		ApplyZ(State, Hologram, Z, RawZ);
	}

	/** Mouse wheel on a stack hologram. True = consumed (vanilla's scroll handling skipped). */
	bool OnScroll(AFGHologram* Hologram, int32 Delta)
	{
		AFGStackablePoleHologram* Stack = Cast<AFGStackablePoleHologram>(Hologram);
		if (!Stack || Delta == 0 || !IsLocalPlayers(Stack) || !InZoopStep(Stack) || Stack->GetMaxZoopAmount() <= 0)
		{
			return false;
		}
		if (!UToolbeltConfig::ReadLocal(Stack).bScrollToHeight)
		{
			return false;
		}
		FActiveStack& State = StateFor(Stack);
		if (!State.bSteer)
		{
			return false;   // no zoop frame seen yet (or vanilla mode): leave it alone
		}
		if (State.LastScrollFrame == GFrameCounter)
		{
			return true;    // the other scroll path already handled this wheel step
		}
		State.LastScrollFrame = GFrameCounter;
		const int32 Step = Delta > 0 ? 1 : -1;
		const int32 Z = FMath::Clamp(State.LastZ + Step, 0, Stack->GetMaxZoopAmount());
		State.ScrollOffset += Z - State.LastZ;
		ApplyZ(State, Stack, Z, Stack->GetmDesiredZoop().Z);   // instant feedback; next frame agrees
		return true;
	}

	// Placement completed (the build click) on the local player's stack hologram: freeze the state.
	void OnPlacementCompleted(AFGBuildableHologram* Hologram)
	{
		AFGStackablePoleHologram* Stack = Cast<AFGStackablePoleHologram>(Hologram);
		if (!Stack || !IsLocalPlayers(Stack) || GStackSession.Current.Hologram.Get() != Stack)
		{
			return;
		}
		constexpr int32 MaxQueued = 4;
		if (GStackSession.Clicked.Num() >= MaxQueued)
		{
			GStackSession.Clicked.RemoveAt(0);   // confirmation never came (e.g. couldn't afford)
		}
		GStackSession.Clicked.Add(GStackSession.Live);
		GStackSession.Current.bNeedsInit = true;   // the build gun may reuse this hologram for the next stack
	}

	// Server confirmed what we built (runs on the building client; locally on host/single player).
	void OnRecipeBuilt(UFGBuildGunStateBuild* State, TSubclassOf<UFGRecipe> Recipe, int32 NumConstructed)
	{
		FToolbeltGhost::OnRecipeBuilt(State->GetWorld(), Recipe);   // ghost tracks single and stacked poles itself
		GStackSession.EnsureWorld(State->GetWorld());
		const int32 Index = Recipe ? GStackSession.Clicked.IndexOfByPredicate(
			[&Recipe](const FStackSession::FPending& P) { return P.Recipe == Recipe && !P.Key.IsEmpty(); }) : INDEX_NONE;
		if (Index == INDEX_NONE)
		{
			return;   // not a stackable pole build
		}
		const FStackSession::FPending Built = GStackSession.Clicked[Index];
		GStackSession.Clicked.RemoveAt(Index);
		// numConstructed should be the poles built; fall back to our own count if it reports 1.
		const int32 NewLevels = FMath::Max(NumConstructed, Built.LevelsFromZ);
		FRemembered& R = GStackSession.Remembered.FindOrAdd(Built.Key);
		R.Total = Built.Existing + NewLevels;
		R.Base = Built.Base;
		UE_LOG(LogStashysToolbelt, Verbose, TEXT("Remembered stack height %d for %s"), R.Total, *Built.Key);
	}
}

FString FToolbeltStackHooks::DescribeState()
{
	FString Out;
	if (GStackSession.Remembered.Num() == 0)
	{
		Out += TEXT("  Stack heights: none remembered this session\n");
	}
	for (const TPair<FString, FRemembered>& Entry : GStackSession.Remembered)
	{
		Out += FString::Printf(TEXT("  Stack height: %s = %d levels (at %s)\n"),
			*FPaths::GetBaseFilename(Entry.Key), Entry.Value.Total, *Entry.Value.Base.ToCompactString());
	}
	Out += FString::Printf(TEXT("  Stack build clicks awaiting confirmation: %d\n"), GStackSession.Clicked.Num());
	return Out;
}

void FToolbeltStackHooks::Install()
{
	SUBSCRIBE_UOBJECT_METHOD_AFTER(AFGStackablePoleHologram, SetHologramLocationAndRotation,
		[](AFGStackablePoleHologram* Self, const FHitResult& Hit)
		{
			OnPlacementFrame(Self);
		});
	// Snapping onto an existing stack replaces SetHologramLocationAndRotation for that frame
	// (FGHologram.h: "Will only be called if we have a valid hit result and did not snap").
	SUBSCRIBE_UOBJECT_METHOD_AFTER(AFGStackablePoleHologram, TrySnapToActor,
		[](const bool& bSnapped, AFGStackablePoleHologram* Self, const FHitResult& Hit)
		{
			if (bSnapped)
			{
				OnPlacementFrame(Self);
			}
		});
	SUBSCRIBE_UOBJECT_METHOD_AFTER(AFGStackablePoleHologram, SetZoopFromHitresult,
		[](AFGStackablePoleHologram* Self, const FHitResult& Hit)
		{
			OnZoopFrame(Self, Hit);
		});
	// Mouse wheel. Hook both paths (generic Scroll, and ScrollRotate as overridden by buildable
	// holograms); OnScroll ignores a second call in the same frame.
	SUBSCRIBE_UOBJECT_METHOD(AFGHologram, Scroll,
		[](auto& Scope, AFGHologram* Self, int32 Delta)
		{
			if (OnScroll(Self, Delta))
			{
				Scope.Cancel();
			}
		});
	SUBSCRIBE_UOBJECT_METHOD(AFGBuildableHologram, ScrollRotate,
		[](auto& Scope, AFGBuildableHologram* Self, int32 Delta, int32 Step)
		{
			if (OnScroll(Self, Delta))
			{
				Scope.Cancel();
			}
		});
	// Final click of a placement (returns true when the hologram is ready to build).
	SUBSCRIBE_UOBJECT_METHOD_AFTER(AFGBuildableHologram, DoMultiStepPlacement,
		[](const bool& bComplete, AFGBuildableHologram* Self, bool bIsInputFromARelease)
		{
			if (bComplete)
			{
				OnPlacementCompleted(Self);
				FToolbeltGhost::OnPlacementCompleted(Self);
			}
		});
	// Protected client RPC: reachable because this class is a Friend (AccessTransformers.ini).
	SUBSCRIBE_UOBJECT_METHOD_AFTER(UFGBuildGunStateBuild, Client_OnRecipeBuilt_Implementation,
		[](UFGBuildGunStateBuild* Self, TSubclassOf<UFGRecipe> Recipe, int32 NumConstructed)
		{
			OnRecipeBuilt(Self, Recipe, NumConstructed);
		});
	UE_LOG(LogStashysToolbelt, Log, TEXT("Stack height hooks installed"));
}
