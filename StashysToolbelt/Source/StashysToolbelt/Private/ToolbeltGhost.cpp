// SPDX-License-Identifier: GPL-3.0-or-later
// Stashy's Toolbelt — Copyright (C) 2026 Stashy. See LICENSE.
#include "ToolbeltGhost.h"
#include "StashysToolbelt.h"
#include "ToolbeltSettings.h"

#include "Patching/NativeHookManager.h"
#include "Equipment/FGBuildGun.h"
#include "Equipment/FGBuildGunBuild.h"
#include "Hologram/FGHologram.h"
#include "Hologram/FGBuildableHologram.h"
#include "Hologram/FGSplineHologram.h"
#include "Hologram/FGConveyorBeltHologram.h"
#include "Hologram/FGPipelineHologram.h"
#include "Hologram/FGPoleHologram.h"
#include "Hologram/FGPipelinePoleHologram.h"
#include "Hologram/FGStackablePoleHologram.h"
#include "Buildables/FGBuildablePoleBase.h"
#include "Buildables/FGBuildablePole.h"
#include "Buildables/FGBuildablePoleStackable.h"
#include "EngineUtils.h"                     // TActorIterator
#include "FGFactoryConnectionComponent.h"
#include "FGPipeConnectionComponent.h"
#include "FGRecipe.h"
#include "Components/SplineComponent.h"
#include "GameFramework/Pawn.h"
#include "Engine/World.h"

namespace
{
	// Preview recipes (Starter Project paths). Tier doesn't matter: length limits are shared.
	const TCHAR* BeltRecipePath = TEXT("/Game/FactoryGame/Recipes/Buildings/Recipe_ConveyorBeltMk1.Recipe_ConveyorBeltMk1_C");
	const TCHAR* PipeRecipePath = TEXT("/Game/FactoryGame/Recipes/Buildings/Recipe_Pipeline.Recipe_Pipeline_C");

	// Vanilla pole classes per family (Starter Project paths; mod subclasses are matched via their parents).
	const TCHAR* BeltPoleClasses[] = {
		TEXT("/Game/FactoryGame/Buildable/Factory/ConveyorPole/Build_ConveyorPole.Build_ConveyorPole_C"),
		TEXT("/Game/FactoryGame/Buildable/Factory/ConveyorPoleStackable/Build_ConveyorPoleStackable.Build_ConveyorPoleStackable_C"),
	};
	const TCHAR* PipePoleClasses[] = {
		TEXT("/Game/FactoryGame/Buildable/Factory/PipelineSupport/Build_PipelineSupport.Build_PipelineSupport_C"),
		TEXT("/Game/FactoryGame/Buildable/Factory/PipelineSupport/Build_PipeSupportStackable.Build_PipeSupportStackable_C"),
	};

	constexpr float GhostXYTolerance   = 10.f;     // uu; same column
	constexpr float MinGhostDistance   = 100.f;    // uu; same/adjacent column -> no ghost
	constexpr float SameLevelTolerance = 5.f;      // uu
	constexpr float WarnLengthFraction = 0.9f;     // yellow from this share of the max length ...
	constexpr float WarnUntilFraction  = 1.2f;     // ... up to this share; beyond it, normal colour again
	constexpr int32 MaxQueuedClicks    = 6;

	enum class EFamily : uint8 { Belt, Pipe, None };

	EFamily FamilyOf(const UClass* PoleClass)
	{
		for (const UClass* C = PoleClass; C; C = C->GetSuperClass())
		{
			const FString Path = C->GetPathName();
			for (const TCHAR* P : BeltPoleClasses) { if (Path == P) { return EFamily::Belt; } }
			for (const TCHAR* P : PipePoleClasses) { if (Path == P) { return EFamily::Pipe; } }
		}
		return EFamily::None;   // hypertube supports, wall mounts, unknown modded poles: no ghost
	}

	struct FConnPoint
	{
		FVector Location = FVector::ZeroVector;
		FVector Direction = FVector::ForwardVector;
	};

	/** A pole class's belt/pipe attachment point relative to the actor (learned from a real pole). */
	struct FPoleOffset
	{
		bool bValid = false;
		FVector LocalLoc = FVector::ZeroVector;
		FVector LocalDir = FVector::ForwardVector;
		float RefHeight = 0.f;      // single adjustable poles: the reference pole's height
		double NextTry = 0.0;       // retry throttle when no pole of this class exists yet
	};

	struct FLastPole
	{
		TWeakObjectPtr<UClass> Class;
		FVector Base = FVector::ZeroVector;
	};

	struct FClick
	{
		TSubclassOf<UFGRecipe> Recipe;
		TWeakObjectPtr<UClass> Class;
		FVector Base = FVector::ZeroVector;
		EFamily Family = EFamily::None;
		TWeakObjectPtr<AFGHologram> Hologram;
		uint64 Frame = 0;
	};

	struct FGhostSession
	{
		TWeakObjectPtr<UWorld> World;
		TMap<uint8, FLastPole> Last;                   // by EFamily
		TArray<FClick> Clicks;                         // build clicks awaiting server confirmation
		TMap<FString, FPoleOffset> Offsets;            // by pole class path

		TWeakObjectPtr<AFGSplineHologram> Ghosts[2];
		TSubclassOf<UFGRecipe> GhostRecipe;
		TWeakObjectPtr<AFGHologram> Owner;             // pole hologram the ghosts belong to

		// Cache of the last pole's connection points (avoid walking every buildable every frame).
		uint8 CachedFamily = 255;
		FVector CachedBase = FVector::ZeroVector;
		TArray<FConnPoint> CachedPoints;
		TArray<TWeakObjectPtr<AActor>> CachedActors;
		double NextResolveTime = 0.0;
		double RecheckUntil = 0.0;                     // keep re-resolving briefly after a build

		// Cache of the snapped column's bottom pole.
		TWeakObjectPtr<AFGBuildable> CachedSnap;
		TWeakObjectPtr<AActor> CachedSnapBottom;

		void EnsureWorld(UWorld* InWorld)
		{
			if (World.Get() != InWorld)
			{
				FToolbeltGhost::HideAll();
				*this = FGhostSession();
				World = InWorld;
			}
		}
	};
	FGhostSession GGhostSession;

	bool IsLocalPlayers(const AFGHologram* Hologram)
	{
		const APawn* Pawn = Hologram ? Hologram->GetConstructionInstigator() : nullptr;
		return Pawn && Pawn->IsLocallyControlled();
	}

	bool FindConnection(const AActor* Pole, FConnPoint& Out)
	{
		if (!Pole)
		{
			return false;
		}
		if (const UFGFactoryConnectionComponent* Belt = Pole->FindComponentByClass<UFGFactoryConnectionComponent>())
		{
			Out.Location = Belt->GetComponentLocation();
			Out.Direction = Belt->GetForwardVector();
			return true;
		}
		if (const UFGPipeConnectionComponent* Pipe = Pole->FindComponentByClass<UFGPipeConnectionComponent>())
		{
			Out.Location = Pipe->GetComponentLocation();
			Out.Direction = Pipe->GetForwardVector();
			return true;
		}
		return false;
	}

	/**
	 * Poles of exactly PoleClass in the column at XY, lowest first. TActorIterator with a class only
	 * visits actors of that class, so the cost scales with the poles of this kind, not every buildable.
	 */
	TArray<AFGBuildablePoleBase*> FindPolesAt(UWorld* World, const UClass* PoleClass, const FVector& AtXY)
	{
		TArray<AFGBuildablePoleBase*> Out;
		if (!World || !PoleClass)
		{
			return Out;
		}
		for (TActorIterator<AFGBuildablePoleBase> It(World, const_cast<UClass*>(PoleClass)); It; ++It)
		{
			AFGBuildablePoleBase* Pole = *It;
			if (Pole && Pole->GetClass() == PoleClass
				&& FVector2D::Distance(FVector2D(Pole->GetActorLocation()), FVector2D(AtXY)) <= GhostXYTolerance)
			{
				Out.Add(Pole);
			}
		}
		Out.Sort([](const AFGBuildablePoleBase& A, const AFGBuildablePoleBase& B)
		{
			return A.GetActorLocation().Z < B.GetActorLocation().Z;
		});
		return Out;
	}

	void LearnOffset(const AActor* Pole)
	{
		FConnPoint P;
		if (!Pole || !FindConnection(Pole, P))
		{
			return;
		}
		FPoleOffset& O = GGhostSession.Offsets.FindOrAdd(Pole->GetClass()->GetPathName());
		const FTransform T = Pole->GetActorTransform();
		O.LocalLoc = T.InverseTransformPosition(P.Location);
		O.LocalDir = T.InverseTransformVectorNoScale(P.Direction);
		const AFGBuildablePole* Single = Cast<AFGBuildablePole>(Pole);
		O.RefHeight = Single ? Single->mHeight : 0.f;
		O.bValid = true;
	}

	/** Offset for a pole class: cached, else learned from any existing pole of that class (throttled). */
	const FPoleOffset* GetOffset(UWorld* World, const UClass* PoleClass)
	{
		const FString Key = PoleClass->GetPathName();
		FPoleOffset& O = GGhostSession.Offsets.FindOrAdd(Key);
		if (O.bValid)
		{
			return &O;
		}
		const double Now = World ? World->GetTimeSeconds() : 0.0;
		if (Now < O.NextTry)
		{
			return nullptr;
		}
		O.NextTry = Now + 2.0;
		if (World)
		{
			for (TActorIterator<AFGBuildablePoleBase> It(World, const_cast<UClass*>(PoleClass)); It; ++It)
			{
				if (*It && It->GetClass() == PoleClass)
				{
					LearnOffset(*It);
					break;
				}
			}
		}
		const FPoleOffset* Found = GGhostSession.Offsets.Find(Key);
		return (Found && Found->bValid) ? Found : nullptr;
	}

	FConnPoint ConnAtTransform(const FTransform& Level, const FVector& LocalLoc, const FVector& LocalDir)
	{
		FConnPoint P;
		P.Location = Level.TransformPosition(LocalLoc);
		P.Direction = Level.TransformVectorNoScale(LocalDir);
		return P;
	}

	/** Start normal toward the end, end normal toward the start (unverified convention). */
	void OrientNormals(const FConnPoint& A, const FConnPoint& B, FVector& OutAN, FVector& OutBN)
	{
		const FVector AB = (B.Location - A.Location).GetSafeNormal2D();
		OutAN = FVector::DotProduct(A.Direction, AB) >= 0.f ? A.Direction : -A.Direction;
		OutBN = FVector::DotProduct(B.Direction, -AB) >= 0.f ? B.Direction : -B.Direction;
	}

	TSubclassOf<UFGRecipe> CachedRecipe(EFamily Family)
	{
		static TWeakObjectPtr<UClass> Cache[2];
		const bool bPipe = Family == EFamily::Pipe;
		TWeakObjectPtr<UClass>& Slot = Cache[bPipe ? 1 : 0];
		if (!Slot.IsValid())
		{
			Slot = LoadClass<UFGRecipe>(nullptr, bPipe ? PipeRecipePath : BeltRecipePath);
		}
		return Slot.Get();
	}

	void DestroyGhost(int32 Index)
	{
		if (AFGSplineHologram* Ghost = GGhostSession.Ghosts[Index].Get())
		{
			Ghost->Destroy();
		}
		GGhostSession.Ghosts[Index] = nullptr;
	}

	AFGSplineHologram* EnsureGhost(int32 Index, TSubclassOf<UFGRecipe> Recipe, AFGHologram* For)
	{
		if (GGhostSession.GhostRecipe != Recipe)
		{
			DestroyGhost(0);
			DestroyGhost(1);
			GGhostSession.GhostRecipe = Recipe;
		}
		if (AFGSplineHologram* Existing = GGhostSession.Ghosts[Index].Get())
		{
			return Existing;
		}
		AFGHologram* Spawned = AFGHologram::SpawnHologramFromRecipe(Recipe, For->GetOwner(), FVector::ZeroVector,
			For->GetConstructionInstigator(),
			[](AFGHologram* Ghost) { Ghost->SetShouldSpawnChildHolograms(false); });   // no auto end poles
		AFGSplineHologram* Ghost = Cast<AFGSplineHologram>(Spawned);
		if (!Ghost)
		{
			if (Spawned) { Spawned->Destroy(); }
			return nullptr;
		}
		Ghost->SetActorEnableCollision(false);
		Ghost->SetActorHiddenInGame(false);   // the build gun normally manages hologram visibility
		GGhostSession.Ghosts[Index] = Ghost;
		return Ghost;
	}

	/** Connection points of the last pole of a family (1 for a single pole, 2 for a multi-level stack). */
	const TArray<FConnPoint>* ResolveLast(UWorld* World, EFamily Family, const FLastPole& Last)
	{
		const double Now = World ? World->GetTimeSeconds() : 0.0;
		bool bCacheOk = GGhostSession.CachedFamily == static_cast<uint8>(Family)
			&& GGhostSession.CachedBase.Equals(Last.Base) && GGhostSession.CachedPoints.Num() > 0;
		for (const TWeakObjectPtr<AActor>& A : GGhostSession.CachedActors)
		{
			bCacheOk &= A.IsValid();
		}
		const bool bRecheck = Now < GGhostSession.RecheckUntil && Now >= GGhostSession.NextResolveTime;
		if (bCacheOk && !bRecheck)
		{
			return &GGhostSession.CachedPoints;
		}
		const bool bSameTarget = GGhostSession.CachedFamily == static_cast<uint8>(Family) && GGhostSession.CachedBase.Equals(Last.Base);
		if (!bRecheck && bSameTarget && Now < GGhostSession.NextResolveTime)
		{
			return nullptr;   // not found recently; retry shortly
		}
		GGhostSession.CachedFamily = static_cast<uint8>(Family);
		GGhostSession.CachedBase = Last.Base;
		GGhostSession.CachedPoints.Reset();
		GGhostSession.CachedActors.Reset();
		GGhostSession.NextResolveTime = Now + 0.5;

		const TArray<AFGBuildablePoleBase*> Poles = FindPolesAt(World, Last.Class.Get(), Last.Base);
		if (Poles.Num() == 0)
		{
			return nullptr;
		}
		const bool bStack = Last.Class.IsValid() && Last.Class->IsChildOf(AFGBuildablePoleStackable::StaticClass());
		TArray<AFGBuildablePoleBase*> Use;
		if (bStack)
		{
			Use.Add(Poles[0]);
			if (Poles.Num() > 1) { Use.Add(Poles.Last()); }
		}
		else
		{
			Use.Add(Poles.Last());
		}
		for (AFGBuildablePoleBase* Pole : Use)
		{
			FConnPoint P;
			if (FindConnection(Pole, P))
			{
				GGhostSession.CachedPoints.Add(P);
				GGhostSession.CachedActors.Add(Pole);
			}
		}
		LearnOffset(Use.Last());
		return GGhostSession.CachedPoints.Num() > 0 ? &GGhostSession.CachedPoints : nullptr;
	}

	/** Shared by stack and single-pole frames: draw ghosts from the family's last pole to Current. */
	void UpdateGhosts(AFGHologram* Hologram, EFamily Family, const TArray<FConnPoint>& Current, float MaxDistanceUU);
}

uint8 FToolbeltGhost::RouteAndValidate(AFGSplineHologram* Ghost, const FVector& Start, const FVector& StartNormal,
	const FVector& End, const FVector& EndNormal)
{
	// Spline data is actor-local (FGSplineHologram.h: "local space"). With the ghost at the world
	// origin and no rotation, local == world, so it's right whichever space AutoRouteSpline expects.
	Ghost->SetActorLocationAndRotation(FVector::ZeroVector, FRotator::ZeroRotator);
	Ghost->ResetConstructDisqualifiers();

	float MaxLength = 0.f;
	bool bValid = true;
	if (AFGConveyorBeltHologram* Belt = Cast<AFGConveyorBeltHologram>(Ghost))
	{
		Belt->AutoRouteSpline(Start, StartNormal, End, EndNormal);
		Ghost->UpdateSplineComponent();
		MaxLength = Belt->GetMaxSplineLength();
		bValid = Belt->ValidateMinLength() && Belt->ValidateCurvature() && Belt->ValidateIncline();
	}
	else if (AFGPipelineHologram* Pipe = Cast<AFGPipelineHologram>(Ghost))
	{
		Pipe->AutoRouteSpline(Start, StartNormal, End, EndNormal);
		Ghost->UpdateSplineComponent();
		MaxLength = Pipe->mMaxSplineLength;
		bValid = Pipe->ValidateMinLength();
	}
	else
	{
		return static_cast<uint8>(EHologramMaterialState::HMS_OK);
	}
	const USplineComponent* Spline = Ghost->FindComponentByClass<USplineComponent>();
	const float Length = Spline ? Spline->GetSplineLength() : FVector::Dist(Start, End);
	// No red (user decision): yellow warns from 90% of the limit (or when vanilla would refuse the
	// shape) until 120%; past that you're clearly doing something else, so back to normal.
	if (MaxLength > 0.f && Length > MaxLength * WarnUntilFraction)
	{
		return static_cast<uint8>(EHologramMaterialState::HMS_OK);
	}
	if (!bValid || (MaxLength > 0.f && Length >= MaxLength * WarnLengthFraction))
	{
		return static_cast<uint8>(EHologramMaterialState::HMS_WARNING);
	}
	return static_cast<uint8>(EHologramMaterialState::HMS_OK);
}

namespace
{
	void UpdateGhosts(AFGHologram* Hologram, EFamily Family, const TArray<FConnPoint>& Current, float MaxDistanceUU)
	{
		UWorld* World = Hologram->GetWorld();
		const FLastPole* Last = GGhostSession.Last.Find(static_cast<uint8>(Family));
		const FVector Base = Hologram->GetActorLocation();
		const float Dist2D = Last ? FVector::Dist2D(Last->Base, Base) : 0.f;
		if (!Last || Current.Num() == 0 || Dist2D < MinGhostDistance || Dist2D > MaxDistanceUU)
		{
			FToolbeltGhost::HideAll();
			return;
		}
		const TArray<FConnPoint>* LastPoints = ResolveLast(World, Family, *Last);
		const TSubclassOf<UFGRecipe> Recipe = CachedRecipe(Family);
		if (!LastPoints || !Recipe)
		{
			FToolbeltGhost::HideAll();
			return;
		}
		if (GGhostSession.Owner.Get() != Hologram)
		{
			FToolbeltGhost::HideAll();
			GGhostSession.Owner = Hologram;
		}

		// Pairing: 2<->2 = bottom-bottom + top-top; otherwise one ghost to the nearest level in height.
		TArray<TPair<FConnPoint, FConnPoint>, TInlineAllocator<2>> Pairs;
		const TArray<FConnPoint>& L = *LastPoints;
		const auto Distinct = [](const TArray<FConnPoint>& P) { return P.Num() == 2 && FMath::Abs(P[1].Location.Z - P[0].Location.Z) > SameLevelTolerance; };
		if (Distinct(L) && Distinct(Current))
		{
			Pairs.Emplace(L[0], Current[0]);
			Pairs.Emplace(L[1], Current[1]);
		}
		else
		{
			const bool bLastSingle = !Distinct(L);
			const FConnPoint& One = bLastSingle ? L[0] : Current[0];
			const TArray<FConnPoint>& Other = bLastSingle ? Current : L;
			const FConnPoint* Best = &Other[0];
			for (const FConnPoint& P : Other)
			{
				if (FMath::Abs(P.Location.Z - One.Location.Z) < FMath::Abs(Best->Location.Z - One.Location.Z)) { Best = &P; }
			}
			bLastSingle ? Pairs.Emplace(One, *Best) : Pairs.Emplace(*Best, One);
		}

		for (int32 i = 0; i < 2; ++i)
		{
			if (!Pairs.IsValidIndex(i))
			{
				DestroyGhost(i);
				continue;
			}
			const FConnPoint& From = Pairs[i].Key;
			const FConnPoint& To = Pairs[i].Value;
			AFGSplineHologram* Ghost = EnsureGhost(i, Recipe, Hologram);
			if (!Ghost)
			{
				continue;
			}
			FVector FromN, ToN;
			OrientNormals(From, To, FromN, ToN);
			const uint8 State = FToolbeltGhost::RouteAndValidate(Ghost, From.Location, FromN, To.Location, ToN);
			Ghost->SetPlacementMaterialState(static_cast<EHologramMaterialState>(State));
		}
	}
}

void FToolbeltGhost::HideAll()
{
	DestroyGhost(0);
	DestroyGhost(1);
	GGhostSession.Owner = nullptr;
}

void FToolbeltGhost::OnStackFrame(AFGStackablePoleHologram* Hologram, int32 TopZ)
{
	UWorld* World = Hologram->GetWorld();
	GGhostSession.EnsureWorld(World);
	const FToolbeltPrefs Prefs = UToolbeltConfig::ReadLocal(Hologram);
	const EFamily Family = FamilyOf(Hologram->GetBuildClass());
	const FPoleOffset* Offset = (Prefs.bGhostPreview && Family != EFamily::None) ? GetOffset(World, Hologram->GetBuildClass()) : nullptr;
	if (!Offset)
	{
		HideAll();
		return;   // off, not a belt/pipe stack, or no pole of this class seen yet
	}
	const FRotator Rot = Hologram->GetActorRotation();
	TArray<FConnPoint> Current;
	// Bottom: the existing stack's bottom if snapped onto one, else the hologram base.
	FConnPoint Bottom = ConnAtTransform(FTransform(Rot, Hologram->GetActorLocation()), Offset->LocalLoc, Offset->LocalDir);
	if (AFGBuildablePoleStackable* Snapped = Cast<AFGBuildablePoleStackable>(Hologram->GetSnappedBuilding()))
	{
		if (GGhostSession.CachedSnap.Get() != Snapped || !GGhostSession.CachedSnapBottom.IsValid())
		{
			const TArray<AFGBuildablePoleBase*> Column = FindPolesAt(World, Snapped->GetClass(), Snapped->GetActorLocation());
			GGhostSession.CachedSnap = Snapped;
			GGhostSession.CachedSnapBottom = Column.Num() > 0 ? Column[0] : nullptr;
		}
		FindConnection(GGhostSession.CachedSnapBottom.Get(), Bottom);
	}
	Current.Add(Bottom);
	// Base + rise of the top level. A difference of two ConvertZoopToWorldLocation calls is right
	// whether it returns world or pole-relative positions (using it directly sent the top ghost
	// toward the world origin).
	const FVector Rise = Hologram->ConvertZoopToWorldLocation(FIntVector(0, 0, TopZ))
	                   - Hologram->ConvertZoopToWorldLocation(FIntVector(0, 0, 0));
	const FVector TopLoc = Hologram->GetActorLocation() + Rise;
	Current.Add(ConnAtTransform(FTransform(Rot, TopLoc), Offset->LocalLoc, Offset->LocalDir));
	UpdateGhosts(Hologram, Family, Current, Prefs.WorkingArea * 100.f);
}

void FToolbeltGhost::OnSinglePoleFrame(AFGPoleHologram* Hologram)
{
	if (!IsLocalPlayers(Hologram) || Hologram->GetParentHologram())
	{
		return;   // not ours, or an auto-pole child of a belt/pipe hologram
	}
	UWorld* World = Hologram->GetWorld();
	GGhostSession.EnsureWorld(World);
	const FToolbeltPrefs Prefs = UToolbeltConfig::ReadLocal(Hologram);
	const EFamily Family = FamilyOf(Hologram->GetBuildClass());
	if (!Prefs.bGhostPreview || Family == EFamily::None)
	{
		HideAll();
		return;
	}
	// Attachment point at the hologram's current height. With a reference pole of this class, use its
	// offset shifted by the height difference; otherwise assume "straight up by the height, facing forward".
	const float Height = Hologram->GetPoleHeight();
	FVector LocalLoc(0.f, 0.f, Height);
	FVector LocalDir = FVector::ForwardVector;
	if (const FPoleOffset* Offset = GetOffset(World, Hologram->GetBuildClass()))
	{
		LocalLoc = Offset->LocalLoc + FVector(0.f, 0.f, Height - Offset->RefHeight);
		LocalDir = Offset->LocalDir;
	}
	TArray<FConnPoint> Current;
	Current.Add(ConnAtTransform(Hologram->GetActorTransform(), LocalLoc, LocalDir));
	UpdateGhosts(Hologram, Family, Current, Prefs.WorkingArea * 100.f);
}

void FToolbeltGhost::OnPlacementCompleted(AFGBuildableHologram* Hologram)
{
	const bool bPole = Hologram && (Hologram->IsA<AFGStackablePoleHologram>() || Hologram->IsA<AFGPoleHologram>());
	if (!bPole || !IsLocalPlayers(Hologram) || Hologram->GetParentHologram())
	{
		return;
	}
	const EFamily Family = FamilyOf(Hologram->GetBuildClass());
	if (Family == EFamily::None)
	{
		return;
	}
	GGhostSession.EnsureWorld(Hologram->GetWorld());
	// Both the base and the pole-hologram DoMultiStepPlacement hooks may fire for one click.
	if (GGhostSession.Clicks.Num() > 0 && GGhostSession.Clicks.Last().Hologram.Get() == Hologram && GGhostSession.Clicks.Last().Frame == GFrameCounter)
	{
		return;
	}
	if (GGhostSession.Clicks.Num() >= MaxQueuedClicks)
	{
		GGhostSession.Clicks.RemoveAt(0);   // confirmation never came (e.g. couldn't afford)
	}
	FClick& Click = GGhostSession.Clicks.AddDefaulted_GetRef();
	Click.Recipe = Hologram->GetRecipe();
	Click.Class = Hologram->GetBuildClass().Get();
	Click.Base = Hologram->GetActorLocation();
	Click.Family = Family;
	Click.Hologram = Hologram;
	Click.Frame = GFrameCounter;
}

void FToolbeltGhost::OnRecipeBuilt(UWorld* World, TSubclassOf<UFGRecipe> Recipe)
{
	GGhostSession.EnsureWorld(World);
	const int32 Index = Recipe ? GGhostSession.Clicks.IndexOfByPredicate([&Recipe](const FClick& C) { return C.Recipe == Recipe; }) : INDEX_NONE;
	if (Index == INDEX_NONE)
	{
		return;
	}
	const FClick Click = GGhostSession.Clicks[Index];
	GGhostSession.Clicks.RemoveAt(Index);
	FLastPole& Last = GGhostSession.Last.FindOrAdd(static_cast<uint8>(Click.Family));
	Last.Class = Click.Class;
	Last.Base = Click.Base;
	GGhostSession.RecheckUntil = World ? World->GetTimeSeconds() + 5.0 : 0.0;   // levels may replicate late
}

FString FToolbeltGhost::DescribeState()
{
	FString Out;
	const TCHAR* Names[] = { TEXT("Belt"), TEXT("Pipe") };
	for (uint8 F = 0; F < 2; ++F)
	{
		if (const FLastPole* Last = GGhostSession.Last.Find(F))
		{
			Out += FString::Printf(TEXT("  Ghost %s: last pole %s at %s\n"), Names[F], *GetNameSafe(Last->Class.Get()), *Last->Base.ToCompactString());
		}
		else
		{
			Out += FString::Printf(TEXT("  Ghost %s: no pole built this session\n"), Names[F]);
		}
	}
	Out += FString::Printf(TEXT("  Ghosts shown: %d, pending build clicks: %d\n"),
		(GGhostSession.Ghosts[0].IsValid() ? 1 : 0) + (GGhostSession.Ghosts[1].IsValid() ? 1 : 0), GGhostSession.Clicks.Num());
	return Out;
}

void FToolbeltGhost::Install()
{
	// Single conveyor poles / pipeline supports: frames (placement and height step) and the build click.
	SUBSCRIBE_UOBJECT_METHOD_AFTER(AFGPoleHologram, SetHologramLocationAndRotation,
		[](AFGPoleHologram* Self, const FHitResult& Hit)
		{
			FToolbeltGhost::OnSinglePoleFrame(Self);
		});
	// The pipeline support hologram overrides it (if it calls Super, the frame just runs twice).
	SUBSCRIBE_UOBJECT_METHOD_AFTER(AFGPipelinePoleHologram, SetHologramLocationAndRotation,
		[](AFGPipelinePoleHologram* Self, const FHitResult& Hit)
		{
			FToolbeltGhost::OnSinglePoleFrame(Self);
		});
	SUBSCRIBE_UOBJECT_METHOD_AFTER(AFGPoleHologram, DoMultiStepPlacement,
		[](const bool& bComplete, AFGPoleHologram* Self, bool bIsInputFromARelease)
		{
			if (bComplete)
			{
				FToolbeltGhost::OnPlacementCompleted(Self);
			}
		});

	// Leaving build mode: remove ghosts (local player only; a listen host runs remote players' states too).
	SUBSCRIBE_UOBJECT_METHOD_AFTER(UFGBuildGunStateBuild, EndState_Implementation,
		[](UFGBuildGunStateBuild* Self)
		{
			const AFGBuildGun* Gun = Self->GetBuildGun();
			const APawn* Pawn = Gun ? Gun->GetInstigator() : nullptr;
			if (Pawn && Pawn->IsLocallyControlled())
			{
				FToolbeltGhost::HideAll();
			}
		});
	// Hologram switched to something else (or gone): remove ghosts.
	SUBSCRIBE_UOBJECT_METHOD_AFTER(UFGBuildGunStateBuild, TickState_Implementation,
		[](UFGBuildGunStateBuild* Self, float DeltaTime)
		{
			const AFGBuildGun* Gun = Self->GetBuildGun();
			const APawn* Pawn = Gun ? Gun->GetInstigator() : nullptr;
			if (!Pawn || !Pawn->IsLocallyControlled())
			{
				return;
			}
			const bool bAnyGhost = GGhostSession.Ghosts[0].IsValid() || GGhostSession.Ghosts[1].IsValid();
			if (bAnyGhost && (!GGhostSession.Owner.IsValid() || Self->GetHologram() != GGhostSession.Owner.Get()))
			{
				FToolbeltGhost::HideAll();
			}
		});
	UE_LOG(LogStashysToolbelt, Log, TEXT("Ghost preview hooks installed"));
}
