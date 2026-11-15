// SPDX-License-Identifier: GPL-3.0-or-later
// Stashy's Toolbelt — Copyright (C) 2026 Stashy. See LICENSE.
#include "ToolbeltPoleTier.h"
#include "StashysToolbelt.h"
#include "ToolbeltSettings.h"

#include "Patching/NativeHookManager.h"
#include "Hologram/FGWireHologram.h"
#include "FGRecipe.h"
#include "FGRecipeManager.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Controller.h"

namespace
{
	// Starter Project paths, index = tier - 1.
	const TCHAR* PoleRecipes[3] = {
		TEXT("/Game/FactoryGame/Recipes/Buildings/Recipe_PowerPoleMk1.Recipe_PowerPoleMk1_C"),
		TEXT("/Game/FactoryGame/Recipes/Buildings/Recipe_PowerPoleMk2.Recipe_PowerPoleMk2_C"),
		TEXT("/Game/FactoryGame/Recipes/Buildings/Recipe_PowerPoleMk3.Recipe_PowerPoleMk3_C"),
	};
	const TCHAR* WallRecipes[3] = {
		TEXT("/Game/FactoryGame/Recipes/Buildings/Recipe_PowerPoleWall.Recipe_PowerPoleWall_C"),
		TEXT("/Game/FactoryGame/Recipes/Buildings/Recipe_PowerPoleWallMk2.Recipe_PowerPoleWallMk2_C"),
		TEXT("/Game/FactoryGame/Recipes/Buildings/Recipe_PowerPoleWallMk3.Recipe_PowerPoleWallMk3_C"),
	};

	/** LoadClass once per path (wire holograms spawn often). */
	TSubclassOf<UFGRecipe> CachedLoad(const TCHAR* Path)
	{
		static TMap<FString, TWeakObjectPtr<UClass>> Cache;
		TWeakObjectPtr<UClass>& Slot = Cache.FindOrAdd(Path);
		if (!Slot.IsValid())
		{
			Slot = LoadClass<UFGRecipe>(nullptr, Path);
		}
		return Slot.Get();
	}

	/** Highest unlocked recipe at or below Tier (2-3). Null -> leave vanilla's Mk1 default. */
	TSubclassOf<UFGRecipe> PickUnlocked(const TCHAR* const (&Paths)[3], int32 Tier, const AFGRecipeManager* Recipes)
	{
		for (int32 T = Tier; T >= 2; --T)
		{
			TSubclassOf<UFGRecipe> Recipe = CachedLoad(Paths[T - 1]);
			if (Recipe && Recipes && Recipes->IsRecipeAvailable(Recipe))
			{
				return Recipe;
			}
		}
		return nullptr;   // nothing above Mk1 unlocked
	}

	void BeforeWireBeginPlay(AFGWireHologram* Wire)
	{
		const APawn* Pawn = Wire->GetConstructionInstigator();
		if (!Pawn)
		{
			Pawn = Wire->GetInstigator();
		}
		const AController* Controller = Pawn ? Pawn->GetController() : nullptr;
		if (!Controller)
		{
			return;
		}
		const int32 Tier = UToolbeltConfig::ForController(Controller).PowerPoleTierFromBuilding;
		if (Tier <= 1)
		{
			return;   // vanilla
		}
		const AFGRecipeManager* Recipes = AFGRecipeManager::Get(Wire->GetWorld());
		if (TSubclassOf<UFGRecipe> Pole = PickUnlocked(PoleRecipes, Tier, Recipes))
		{
			Wire->SetmDefaultPowerPoleRecipe(Pole);        // AT accessor
		}
		if (TSubclassOf<UFGRecipe> Wall = PickUnlocked(WallRecipes, Tier, Recipes))
		{
			Wire->SetmDefaultPowerPoleWallRecipe(Wall);    // AT accessor
		}
	}
}

void ToolbeltPoleTier::Install()
{
	// Before-hook: runs ahead of the wire hologram's own BeginPlay (where it sets up its auto-pole).
	SUBSCRIBE_UOBJECT_METHOD(AFGWireHologram, BeginPlay,
		[](auto& Scope, AFGWireHologram* Self)
		{
			BeforeWireBeginPlay(Self);
		});
	UE_LOG(LogStashysToolbelt, Log, TEXT("Power pole tier hook installed"));
}
