// SPDX-License-Identifier: GPL-3.0-or-later
// Stashy's Toolbelt — Copyright (C) 2026 Stashy. See LICENSE.
#include "ToolbeltSwatch.h"
#include "StashysToolbelt.h"
#include "ToolbeltSettings.h"

#include "Patching/NativeHookManager.h"
#include "FGBuildableSubsystem.h"            // FNetConstructionID
#include "FGFactoryColoringTypes.h"
#include "FGCircuitConnectionComponent.h"
#include "FGFactoryConnectionComponent.h"
#include "FGPipeConnectionComponent.h"
#include "Hologram/FGBuildableHologram.h"
#include "Hologram/FGWireHologram.h"
#include "Hologram/FGBlueprintHologram.h"
#include "Hologram/FGPowerPoleHologram.h"
#include "Hologram/FGPowerPoleWallHologram.h"
#include "Buildables/FGBuildable.h"
#include "Buildables/FGBuildableWire.h"
#include "Buildables/FGBuildablePowerPole.h"
#include "Buildables/FGBuildablePowerTower.h"
#include "Buildables/FGBuildableConveyorBase.h"
#include "Buildables/FGBuildablePipeline.h"
#include "Buildables/FGBuildablePoleStackable.h"
#include "Buildables/FGBuildablePipeBase.h"
#include "Buildables/FGBuildablePipeHyper.h"
#include "Buildables/FGBuildableConveyorAttachment.h"
#include "Buildables/FGBuildablePipelineAttachment.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Controller.h"

namespace
{
	/** >0 while a blueprint is being constructed: its pieces keep the blueprint's colours. Game thread only. */
	int32 GBlueprintConstructDepth = 0;

	/** Settings of the player who built it. No instigator -> treat as "feature off". */
	bool GetBuilderPrefs(const AFGHologram* Hologram, FToolbeltPrefs& Out)
	{
		const APawn* Pawn = Hologram ? Hologram->GetConstructionInstigator() : nullptr;
		const AController* Controller = Pawn ? Pawn->GetController() : nullptr;
		if (!Controller)
		{
			return false;
		}
		Out = UToolbeltConfig::ForController(Controller);
		return true;
	}

	/** Copy swatch + custom colour from one buildable to another; leaves pattern/material/skin alone. */
	void CopySwatch(const AFGBuildable* From, AFGBuildable* To)
	{
		if (!From || !To || From == To)
		{
			return;
		}
		const FFactoryCustomizationData& Src = From->GetCustomizationData_Native();
		if (!Src.SwatchDesc)
		{
			return;
		}
		FFactoryCustomizationData New = To->GetCustomizationData_Native();
		if (New.SwatchDesc == Src.SwatchDesc && New.OverrideColorData == Src.OverrideColorData)
		{
			return;
		}
		New.SwatchDesc = Src.SwatchDesc;
		New.OverrideColorData = Src.OverrideColorData;
		// Unverified: whether Set alone also applies visuals; Apply afterwards is harmless if so.
		To->SetCustomizationData_Native(New);
		To->ApplyCustomizationData_Native(To->GetCustomizationData_Native());
		UE_LOG(LogStashysToolbelt, Verbose, TEXT("Swatch %s -> %s"), *GetNameSafe(From), *GetNameSafe(To));
	}

	/** Swatch + custom colour captured from the piece being upgraded, per hologram (Construct is synchronous). */
	TMap<const AFGHologram*, FFactoryCustomizationData> GUpgradeCapture;

	void BeforeConstruct(AFGHologram* Self)
	{
		if (!Self || Self->GetNetMode() == NM_Client || !Self->IsUpgrade() || GUpgradeCapture.Contains(Self))
		{
			return;   // nested call (pole hologram -> Super) already captured
		}
		const AFGBuildable* Old = Cast<AFGBuildable>(Self->GetUpgradedActor());
		FToolbeltPrefs Prefs;
		if (!Old || !GetBuilderPrefs(Self, Prefs) || !Prefs.bKeepColourOnUpgrade)
		{
			return;
		}
		GUpgradeCapture.Add(Self, Old->GetCustomizationData_Native());   // before vanilla removes the old piece
	}

	/** Applies a captured upgrade colour. True if this construct was a captured upgrade. */
	bool AfterConstruct(AActor* Ret, AFGHologram* Self)
	{
		FFactoryCustomizationData Captured;
		if (!GUpgradeCapture.RemoveAndCopyValue(Self, Captured))
		{
			return false;
		}
		AFGBuildable* New = Cast<AFGBuildable>(Ret);
		if (!New || !Captured.SwatchDesc)
		{
			return true;
		}
		FFactoryCustomizationData Data = New->GetCustomizationData_Native();
		Data.SwatchDesc = Captured.SwatchDesc;
		Data.OverrideColorData = Captured.OverrideColorData;
		New->SetCustomizationData_Native(Data);
		New->ApplyCustomizationData_Native(New->GetCustomizationData_Native());
		UE_LOG(LogStashysToolbelt, Verbose, TEXT("Upgrade kept swatch on %s"), *GetNameSafe(New));
		return true;
	}

	bool IsPowerFamily(const AActor* Actor)
	{
		// AFGBuildablePowerPole covers poles of every Mk and wall/ceiling outlets (BP subclasses).
		return Actor && (Actor->IsA<AFGBuildablePowerPole>() || Actor->IsA<AFGBuildablePowerTower>());
	}

	bool IsBeltAttachment(const AActor* A)
	{
		// Real splitter/merger actors only (the lightweight variant has no components).
		return A && A->IsA<AFGBuildableConveyorAttachment>() && !A->IsA<AFGBuildableConveyorAttachmentLightweight>();
	}

	/**
	 * Existing piece a new belt/lift takes its colour from: a belt/lift it continues, else a
	 * splitter/merger it starts or ends at. Never a machine.
	 */
	const AFGBuildable* FindConveyorSource(const AFGBuildableConveyorBase* Conveyor, const TSet<const AActor*>& NewActors)
	{
		const AFGBuildable* Attachment = nullptr;
		for (const UFGFactoryConnectionComponent* End : { Conveyor->GetConnection0(), Conveyor->GetConnection1() })
		{
			const UFGFactoryConnectionComponent* Other = End ? End->GetConnection() : nullptr;
			const AActor* Owner = Other ? Other->GetOwner() : nullptr;
			if (!Owner || NewActors.Contains(Owner))
			{
				continue;
			}
			if (Owner->IsA<AFGBuildableConveyorBase>())
			{
				return Cast<AFGBuildable>(Owner);   // a line piece wins
			}
			if (!Attachment && IsBeltAttachment(Owner))
			{
				Attachment = Cast<AFGBuildable>(Owner);
			}
		}
		return Attachment;
	}

	/** Same for pipes: a pipe it continues, else a junction/pump/valve it starts or ends at. */
	const AFGBuildable* FindPipeSource(const AFGBuildablePipeline* Pipe, const TSet<const AActor*>& NewActors)
	{
		const AFGBuildable* Attachment = nullptr;
		for (const UFGPipeConnectionComponent* End : { Pipe->GetPipeConnection0(), Pipe->GetPipeConnection1() })
		{
			const UFGPipeConnectionComponentBase* Other = End ? End->GetConnection() : nullptr;
			const AActor* Owner = Other ? Other->GetOwner() : nullptr;
			if (!Owner || NewActors.Contains(Owner))
			{
				continue;
			}
			if (Owner->IsA<AFGBuildablePipeline>())
			{
				return Cast<AFGBuildable>(Owner);
			}
			if (!Attachment && Owner->IsA<AFGBuildablePipelineAttachment>())
			{
				Attachment = Cast<AFGBuildable>(Owner);
			}
		}
		return Attachment;
	}

	/** First existing hypertube directly connected to one of the new hypertube's ends. */
	const AFGBuildablePipeHyper* FindHypertubeSource(const AFGBuildablePipeHyper* Tube, const TSet<const AActor*>& NewActors)
	{
		for (const UFGPipeConnectionComponentBase* End : { Tube->GetConnection0(), Tube->GetConnection1() })
		{
			const UFGPipeConnectionComponentBase* Other = End ? End->GetConnection() : nullptr;
			const AFGBuildablePipeHyper* Owner = Other ? Cast<AFGBuildablePipeHyper>(Other->GetOwner()) : nullptr;
			if (Owner && !NewActors.Contains(Owner))
			{
				return Owner;
			}
		}
		return nullptr;
	}

	/**
	 * Belt/lift (or pipe) connected to an attachment placed onto it. Prefers one that existed
	 * before; accepts a new one too — when an attachment splits a line, vanilla rebuilds the two
	 * halves with the original line's colour.
	 */
	const AFGBuildable* FindAttachmentSource(const AActor* Attachment, bool bPipe, const TSet<const AActor*>& NewActors)
	{
		const AFGBuildable* NewFallback = nullptr;
		auto Consider = [&](const AActor* Owner) -> bool
		{
			const AFGBuildable* Line = Cast<AFGBuildable>(Owner);
			const bool bIsLine = Line && (bPipe ? Line->IsA<AFGBuildablePipeline>() : Line->IsA<AFGBuildableConveyorBase>());
			if (!bIsLine)
			{
				return false;
			}
			if (!NewActors.Contains(Line))
			{
				NewFallback = Line;   // existing: best
				return true;
			}
			if (!NewFallback)
			{
				NewFallback = Line;
			}
			return false;
		};
		if (bPipe)
		{
			TInlineComponentArray<UFGPipeConnectionComponent*> Ends(const_cast<AActor*>(Attachment));
			for (const UFGPipeConnectionComponent* End : Ends)
			{
				const UFGPipeConnectionComponentBase* Other = End ? End->GetConnection() : nullptr;
				if (Other && Consider(Other->GetOwner())) { break; }
			}
		}
		else
		{
			TInlineComponentArray<UFGFactoryConnectionComponent*> Ends(const_cast<AActor*>(Attachment));
			for (const UFGFactoryConnectionComponent* End : Ends)
			{
				const UFGFactoryConnectionComponent* Other = End ? End->GetConnection() : nullptr;
				if (Other && Consider(Other->GetOwner())) { break; }
			}
		}
		return NewFallback;
	}

	TSet<const AActor*> CollectNew(AActor* Ret, const TArray<AActor*>& Children)
	{
		TSet<const AActor*> New;
		New.Add(Ret);
		for (const AActor* Child : Children)
		{
			New.Add(Child);
		}
		return New;
	}

	// Server-side only. GetNetMode() rather than HasAuthority(): a client-spawned hologram has
	// authority on that client.
	// ---- 2a: power line ---------------------------------------------------------------
	void OnWireConstructed(AActor* Ret, AFGWireHologram* Self, const TArray<AActor*>& Children)
	{
		AFGBuildableWire* Wire = Cast<AFGBuildableWire>(Ret);
		if (!Wire || Self->GetNetMode() == NM_Client || GBlueprintConstructDepth > 0)
		{
			return;
		}
		FToolbeltPrefs Prefs;
		if (!GetBuilderPrefs(Self, Prefs) || !Prefs.bInheritPowerSwatch)
		{
			return;
		}
		const TSet<const AActor*> NewActors = CollectNew(Ret, Children);
		const UFGCircuitConnectionComponent* C0 = Wire->GetConnection(0);
		const UFGCircuitConnectionComponent* C1 = Wire->GetConnection(1);
		AFGBuildable* A = C0 ? Cast<AFGBuildable>(C0->GetOwner()) : nullptr;
		AFGBuildable* B = C1 ? Cast<AFGBuildable>(C1->GetOwner()) : nullptr;
		if (!IsPowerFamily(A) || !IsPowerFamily(B))
		{
			return;   // one end is a machine/building (or missing): nothing to inherit
		}
		const bool bANew = NewActors.Contains(A);
		const bool bBNew = NewActors.Contains(B);
		if (bANew != bBNew)   // exactly one end was built just now (the auto-built pole)
		{
			bANew ? CopySwatch(B, A) : CopySwatch(A, B);
		}
		// Both existing (wired two old poles) or both new: no change.
	}

	// ---- 2b / 2c: belts, lifts, pipes, stackable poles -----------------------------------
	void OnBuildableConstructed(AActor* Ret, AFGBuildableHologram* Self, const TArray<AActor*>& Children)
	{
		// Upgrades first (one handler, so inheritance can never overwrite a kept colour).
		if (AfterConstruct(Ret, Self))
		{
			return;
		}
		if (!Ret || Self->GetNetMode() == NM_Client || Self->IsUpgrade() || GBlueprintConstructDepth > 0)
		{
			return;   // upgrades keep their own colour; blueprints keep theirs
		}
		const bool bConveyor  = Ret->IsA<AFGBuildableConveyorBase>();
		const bool bPipe      = Ret->IsA<AFGBuildablePipeline>();
		const bool bHypertube = Ret->IsA<AFGBuildablePipeHyper>();
		const bool bStack     = Ret->IsA<AFGBuildablePoleStackable>();
		// Real splitter/merger actors only (the lightweight variant has no components).
		const bool bBeltAttachment = IsBeltAttachment(Ret);
		const bool bPipeAttachment = Ret->IsA<AFGBuildablePipelineAttachment>();   // pumps, valves, junctions
		if (!bConveyor && !bPipe && !bHypertube && !bStack && !bBeltAttachment && !bPipeAttachment)
		{
			return;   // includes wires and power poles (handled by the wire hook)
		}
		FToolbeltPrefs Prefs;
		if (!GetBuilderPrefs(Self, Prefs))
		{
			return;
		}
		const TSet<const AActor*> NewActors = CollectNew(Ret, Children);

		if (bConveyor && Prefs.bInheritBeltPipeSwatch)
		{
			CopySwatch(FindConveyorSource(CastChecked<AFGBuildableConveyorBase>(Ret), NewActors), CastChecked<AFGBuildable>(Ret));
		}
		else if (bPipe && Prefs.bInheritBeltPipeSwatch)
		{
			CopySwatch(FindPipeSource(CastChecked<AFGBuildablePipeline>(Ret), NewActors), CastChecked<AFGBuildable>(Ret));
		}
		else if (bHypertube && Prefs.bInheritBeltPipeSwatch)
		{
			CopySwatch(FindHypertubeSource(CastChecked<AFGBuildablePipeHyper>(Ret), NewActors), CastChecked<AFGBuildable>(Ret));
		}
		else if ((bBeltAttachment || bPipeAttachment) && Prefs.bInheritAttachmentSwatch)
		{
			CopySwatch(FindAttachmentSource(Ret, bPipeAttachment, NewActors), CastChecked<AFGBuildable>(Ret));
		}
		else if (bStack && Prefs.bInheritStackSwatch)
		{
			// Placed onto an existing stack -> the hologram snapped to the pole below.
			const AFGBuildablePoleStackable* Below = Cast<AFGBuildablePoleStackable>(Self->GetSnappedBuilding());
			if (!Below || NewActors.Contains(Below))
			{
				return;   // on the ground: nothing to inherit
			}
			CopySwatch(Below, CastChecked<AFGBuildable>(Ret));
			// Extra levels built in the same placement (zoop), if they come back as children.
			for (AActor* Child : Children)
			{
				if (AFGBuildablePoleStackable* Level = Cast<AFGBuildablePoleStackable>(Child))
				{
					CopySwatch(Below, Level);
				}
			}
		}
	}
}

void ToolbeltSwatch::Install()
{
	// Blueprint placement guard (before + after so the depth counter brackets the whole call).
	SUBSCRIBE_UOBJECT_METHOD(AFGBlueprintHologram, Construct,
		[](auto& Scope, AFGBlueprintHologram* Self, TArray<AActor*>& Children, FNetConstructionID Id)
		{
			++GBlueprintConstructDepth;
		});
	SUBSCRIBE_UOBJECT_METHOD_AFTER(AFGBlueprintHologram, Construct,
		[](AActor* const& Ret, AFGBlueprintHologram* Self, TArray<AActor*>& Children, FNetConstructionID Id)
		{
			GBlueprintConstructDepth = FMath::Max(0, GBlueprintConstructDepth - 1);
		});

	SUBSCRIBE_UOBJECT_METHOD_AFTER(AFGWireHologram, Construct,
		[](AActor* const& Ret, AFGWireHologram* Self, TArray<AActor*>& Children, FNetConstructionID Id)
		{
			OnWireConstructed(Ret, Self, Children);
		});

	// Belt, lift, pipe and stackable pole holograms don't override Construct, so they run this one.
	SUBSCRIBE_UOBJECT_METHOD_AFTER(AFGBuildableHologram, Construct,
		[](AActor* const& Ret, AFGBuildableHologram* Self, TArray<AActor*>& Children, FNetConstructionID Id)
		{
			OnBuildableConstructed(Ret, Self, Children);
		});

	// Keep colour on upgrade: capture before, apply after. Power pole holograms override Construct,
	// so they get their own pair; the capture map ignores the nested Super call.
	SUBSCRIBE_UOBJECT_METHOD(AFGBuildableHologram, Construct,
		[](auto& Scope, AFGBuildableHologram* Self, TArray<AActor*>& Children, FNetConstructionID Id) { BeforeConstruct(Self); });
	// (AFGBuildableHologram's "after" side is OnBuildableConstructed, which applies the capture first.)
	SUBSCRIBE_UOBJECT_METHOD(AFGPowerPoleHologram, Construct,
		[](auto& Scope, AFGPowerPoleHologram* Self, TArray<AActor*>& Children, FNetConstructionID Id) { BeforeConstruct(Self); });
	SUBSCRIBE_UOBJECT_METHOD_AFTER(AFGPowerPoleHologram, Construct,
		[](AActor* const& Ret, AFGPowerPoleHologram* Self, TArray<AActor*>& Children, FNetConstructionID Id) { AfterConstruct(Ret, Self); });
	SUBSCRIBE_UOBJECT_METHOD(AFGPowerPoleWallHologram, Construct,
		[](auto& Scope, AFGPowerPoleWallHologram* Self, TArray<AActor*>& Children, FNetConstructionID Id) { BeforeConstruct(Self); });
	SUBSCRIBE_UOBJECT_METHOD_AFTER(AFGPowerPoleWallHologram, Construct,
		[](AActor* const& Ret, AFGPowerPoleWallHologram* Self, TArray<AActor*>& Children, FNetConstructionID Id) { AfterConstruct(Ret, Self); });

	UE_LOG(LogStashysToolbelt, Log, TEXT("Swatch inheritance hooks installed"));
}
