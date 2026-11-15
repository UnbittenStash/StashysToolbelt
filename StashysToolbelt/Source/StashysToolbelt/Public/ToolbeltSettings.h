// SPDX-License-Identifier: GPL-3.0-or-later
// Stashy's Toolbelt — Copyright (C) 2026 Stashy. See LICENSE.
#pragma once

#include "CoreMinimal.h"
#include "Configuration/ModConfiguration.h"
#include "ToolbeltSettings.generated.h"

/**
 * One player's settings. Read locally from the mod config; sent to the server through
 * UToolbeltRCO so server-side features (colour inheritance at construction) can honour
 * the building player's choice. Keep fields in sync with ToolbeltKeys and ReadLocal().
 */
USTRUCT(BlueprintType)
struct STASHYSTOOLBELT_API FToolbeltPrefs
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) bool bZoopByDefault = true;
	// Stackable pole height control (all three independent).
	UPROPERTY(BlueprintReadOnly) bool bRememberStackHeight = true;   // start at the last built height
	UPROPERTY(BlueprintReadOnly) bool bScrollToHeight = true;        // mouse wheel +/- one level in the zoop step
	UPROPERTY(BlueprintReadOnly) bool bAimToHeight = false;          // looking up/down changes height (vanilla-like)
	/** Metres around the last pole you built. Beyond it the remembered height and the ghost don't apply. */
	UPROPERTY(BlueprintReadOnly) float WorkingArea = 75.f;
	UPROPERTY(BlueprintReadOnly) bool bGhostPreview = true;
	UPROPERTY(BlueprintReadOnly) bool bInheritPowerSwatch = true;
	UPROPERTY(BlueprintReadOnly) bool bInheritBeltPipeSwatch = true;
	UPROPERTY(BlueprintReadOnly) bool bInheritAttachmentSwatch = true;   // splitters, mergers, pumps, valves, junctions
	UPROPERTY(BlueprintReadOnly) bool bInheritStackSwatch = true;
	UPROPERTY(BlueprintReadOnly) bool bKeepColourOnUpgrade = true;       // any upgrade keeps the old piece's colour
	/** Tier of the pole/wall outlet auto-built when dragging a power line from a building: 1-3 (1 = vanilla). */
	UPROPERTY(BlueprintReadOnly) int32 PowerPoleTierFromBuilding = 2;

	bool operator==(const FToolbeltPrefs& O) const
	{
		return bZoopByDefault == O.bZoopByDefault
			&& bRememberStackHeight == O.bRememberStackHeight
			&& bScrollToHeight == O.bScrollToHeight
			&& bAimToHeight == O.bAimToHeight
			&& WorkingArea == O.WorkingArea
			&& bGhostPreview == O.bGhostPreview
			&& bInheritPowerSwatch == O.bInheritPowerSwatch
			&& bInheritBeltPipeSwatch == O.bInheritBeltPipeSwatch
			&& bInheritAttachmentSwatch == O.bInheritAttachmentSwatch
			&& bInheritStackSwatch == O.bInheritStackSwatch
			&& bKeepColourOnUpgrade == O.bKeepColourOnUpgrade
			&& PowerPoleTierFromBuilding == O.PowerPoleTierFromBuilding;
	}
	bool operator!=(const FToolbeltPrefs& O) const { return !(*this == O); }

	/** True when none of the height controls change vanilla's stack zoop. */
	bool IsVanillaStackHeight() const { return !bRememberStackHeight && !bScrollToHeight && bAimToHeight; }

	/** Every server-side feature off / vanilla. Used for a remote player whose settings haven't arrived yet. */
	static FToolbeltPrefs Vanilla()
	{
		FToolbeltPrefs P;
		P.bInheritPowerSwatch = false;
		P.bInheritBeltPipeSwatch = false;
		P.bInheritStackSwatch = false;
		P.bInheritAttachmentSwatch = false;
		P.bKeepColourOnUpgrade = false;
		P.PowerPoleTierFromBuilding = 1;
		return P;
	}

	/** Server-side sanity limits for values received from clients. */
	void Sanitize();
};

/** Config keys == JSON keys in FactoryGame/Configs/StashysToolbelt.cfg. Never rename after release. */
namespace ToolbeltKeys
{
	inline const TCHAR* ZoopByDefault             = TEXT("ZoopByDefault");
	inline const TCHAR* RememberStackHeight       = TEXT("RememberStackHeight");
	inline const TCHAR* ScrollToHeight            = TEXT("ScrollToHeight");
	inline const TCHAR* AimToHeight               = TEXT("AimToHeight");
	inline const TCHAR* WorkingArea               = TEXT("WorkingArea");
	inline const TCHAR* GhostPreview              = TEXT("GhostPreview");
	inline const TCHAR* InheritPowerSwatch        = TEXT("InheritPowerSwatch");
	inline const TCHAR* InheritBeltPipeSwatch     = TEXT("InheritBeltPipeSwatch");
	inline const TCHAR* InheritAttachmentSwatch   = TEXT("InheritAttachmentSwatch");
	inline const TCHAR* InheritStackSwatch        = TEXT("InheritStackSwatch");
	inline const TCHAR* KeepColourOnUpgrade       = TEXT("KeepColourOnUpgrade");
	inline const TCHAR* PowerPoleTierFromBuilding = TEXT("PowerPoleTierFromBuilding");
}

/**
 * Mod Settings entry, defined in C++ (no config asset). The property tree is built at runtime
 * from SML's own Blueprint property classes so SML's settings screen can draw it — see
 * EnsureRootSection(). Registered by UToolbeltRootInstance.
 */
UCLASS()
class STASHYSTOOLBELT_API UToolbeltConfig : public UModConfiguration
{
	GENERATED_BODY()
public:
	UToolbeltConfig();

	/**
	 * Fills this class's CDO RootSection. Must run before SML registers the config
	 * (Game Instance module INITIALIZATION); call from CONSTRUCTION. Idempotent.
	 */
	static void EnsureRootSection();

	static FConfigId GetConfigId();

	/** This machine's settings (defaults if the config isn't registered yet). */
	static FToolbeltPrefs ReadLocal(const UObject* WorldContext);

	/**
	 * Settings of the player behind a controller. Local controller -> this machine's config;
	 * remote controller (server side) -> what that client last sent, or Vanilla() until it arrives;
	 * no controller -> defaults.
	 */
	static FToolbeltPrefs ForController(const class AController* Controller);
};
