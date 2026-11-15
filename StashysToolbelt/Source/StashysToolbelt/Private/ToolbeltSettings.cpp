// SPDX-License-Identifier: GPL-3.0-or-later
// Stashy's Toolbelt — Copyright (C) 2026 Stashy. See LICENSE.
#include "ToolbeltSettings.h"
#include "StashysToolbelt.h"
#include "ToolbeltRCO.h"
#include "Configuration/ConfigManager.h"
#include "Configuration/Properties/ConfigPropertyBool.h"
#include "Configuration/Properties/ConfigPropertyFloat.h"
#include "Configuration/Properties/ConfigPropertyInteger.h"
#include "Configuration/Properties/ConfigPropertySection.h"
#include "Configuration/Properties/WidgetExtension/CP_Section.h"
#include "Configuration/Properties/WidgetExtension/CP_Float.h"
#include "Configuration/Properties/WidgetExtension/CP_Integer.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "FGPlayerController.h"

#define LOCTEXT_NAMESPACE "StashysToolbelt"

namespace
{
	// SML's Blueprint subclasses of the native property classes. Only these create widgets in the
	// Mod Settings screen (the native classes' CreateEditorWidget returns null).
	const TCHAR* SectionClassPath = TEXT("/SML/Interface/UI/Menu/Mods/ConfigProperties/BP_ConfigPropertySection.BP_ConfigPropertySection_C");
	const TCHAR* BoolClassPath    = TEXT("/SML/Interface/UI/Menu/Mods/ConfigProperties/BP_ConfigPropertyBool.BP_ConfigPropertyBool_C");
	const TCHAR* FloatClassPath   = TEXT("/SML/Interface/UI/Menu/Mods/ConfigProperties/BP_ConfigPropertyFloat.BP_ConfigPropertyFloat_C");
	const TCHAR* IntClassPath     = TEXT("/SML/Interface/UI/Menu/Mods/ConfigProperties/BP_ConfigPropertyInteger.BP_ConfigPropertyInteger_C");

	// Same flags as subobject templates inside a Blueprint config asset, so SML's
	// NewObject(..., Template) instancing treats them the same way (unverified assumption).
	constexpr EObjectFlags TemplateFlags = RF_Public | RF_ArchetypeObject;

	template <typename TNative>
	UClass* LoadPropertyClass(const TCHAR* Path)
	{
		UClass* Cls = LoadClass<TNative>(nullptr, Path);
		if (!Cls)
		{
			// Values still load/save from the .cfg file; the settings screen just can't show them.
			UE_LOG(LogStashysToolbelt, Warning, TEXT("Could not load %s; falling back to %s (no settings UI for it)"),
				Path, *TNative::StaticClass()->GetName());
			Cls = TNative::StaticClass();
		}
		return Cls;
	}

	void AddBool(UConfigPropertySection* Section, UClass* Cls, const TCHAR* Key,
		const FText& Name, const FText& Tooltip, bool bDefault)
	{
		UConfigPropertyBool* P = NewObject<UConfigPropertyBool>(Section, Cls, FName(Key), TemplateFlags);
		P->DisplayName = Name;
		P->Tooltip = Tooltip;
		P->DefaultValue = bDefault;
		P->Value = bDefault;
		Section->SectionProperties.Add(Key, P);
	}

	void AddFloat(UConfigPropertySection* Section, UClass* Cls, const TCHAR* Key,
		const FText& Name, const FText& Tooltip, float Default, float Min, float Max)
	{
		UConfigPropertyFloat* P = NewObject<UConfigPropertyFloat>(Section, Cls, FName(Key), TemplateFlags);
		P->DisplayName = Name;
		P->Tooltip = Tooltip;
		P->DefaultValue = Default;
		P->Value = Default;
		// SML's widget reads these; left at 0/0 every value counts as out of range (red outline).
		if (UCP_Float* W = Cast<UCP_Float>(P))
		{
			W->WidgetType = ECP_FloatWidgetType::CPF_Spinbox;
			W->MinValue = Min;
			W->MaxValue = Max;
		}
		Section->SectionProperties.Add(Key, P);
	}

	void AddInt(UConfigPropertySection* Section, UClass* Cls, const TCHAR* Key,
		const FText& Name, const FText& Tooltip, int32 Default, int32 Min, int32 Max)
	{
		UConfigPropertyInteger* P = NewObject<UConfigPropertyInteger>(Section, Cls, FName(Key), TemplateFlags);
		P->DisplayName = Name;
		P->Tooltip = Tooltip;
		P->DefaultValue = Default;
		P->Value = Default;
		if (UCP_Integer* W = Cast<UCP_Integer>(P))
		{
			W->WidgetType = ECP_IntegerWidgetType::CPI_Spinbox;
			W->MinValue = Min;
			W->MaxValue = Max;
		}
		Section->SectionProperties.Add(Key, P);
	}

	int32 GetInt(const UConfigPropertySection* Root, const TCHAR* Key, int32 Fallback)
	{
		const UConfigPropertyInteger* P = Cast<UConfigPropertyInteger>(Root->SectionProperties.FindRef(Key));
		return P ? P->Value : Fallback;
	}

	bool GetBool(const UConfigPropertySection* Root, const TCHAR* Key, bool Fallback)
	{
		const UConfigPropertyBool* P = Cast<UConfigPropertyBool>(Root->SectionProperties.FindRef(Key));
		return P ? P->Value : Fallback;
	}

	float GetFloat(const UConfigPropertySection* Root, const TCHAR* Key, float Fallback)
	{
		const UConfigPropertyFloat* P = Cast<UConfigPropertyFloat>(Root->SectionProperties.FindRef(Key));
		return P ? P->Value : Fallback;
	}
}

void FToolbeltPrefs::Sanitize()
{
	WorkingArea = FMath::Clamp(WorkingArea, 0.f, 1000.f);
	PowerPoleTierFromBuilding = FMath::Clamp(PowerPoleTierFromBuilding, 1, 3);
}

UToolbeltConfig::UToolbeltConfig()
{
	ConfigId = GetConfigId();
	DisplayName = LOCTEXT("ConfigName", "Stashy's Toolbelt");
	Description = LOCTEXT("ConfigDesc", "Per-player build gun defaults. Each player's own settings apply to what they build.");
}

FConfigId UToolbeltConfig::GetConfigId()
{
	FConfigId Id;
	Id.ModReference = TEXT("StashysToolbelt");
	Id.ConfigCategory = TEXT("");
	return Id;
}

void UToolbeltConfig::EnsureRootSection()
{
	UToolbeltConfig* CDO = GetMutableDefault<UToolbeltConfig>();
	if (CDO->RootSection)
	{
		return;
	}

	UClass* SectionCls = LoadPropertyClass<UConfigPropertySection>(SectionClassPath);
	UClass* BoolCls    = LoadPropertyClass<UConfigPropertyBool>(BoolClassPath);
	UClass* FloatCls   = LoadPropertyClass<UConfigPropertyFloat>(FloatClassPath);
	UClass* IntCls     = LoadPropertyClass<UConfigPropertyInteger>(IntClassPath);

	UConfigPropertySection* Root = NewObject<UConfigPropertySection>(CDO, SectionCls, TEXT("RootSection"), TemplateFlags);
	// One setting per row (the section widget's default is a horizontal list).
	if (UCP_Section* W = Cast<UCP_Section>(Root))
	{
		W->WidgetType = ECP_SectionWidgetType::CPS_Vertical;
	}
	using namespace ToolbeltKeys;
	const FToolbeltPrefs D; // single source of defaults
	// Insertion order = display order.

	AddBool(Root, BoolCls, ZoopByDefault,
		LOCTEXT("Zoop", "Zoop by default"),
		LOCTEXT("ZoopTip", "Buildings that support zoop start in zoop mode each session. Picking another mode still sticks for the rest of the session."),
		D.bZoopByDefault);

	AddBool(Root, BoolCls, RememberStackHeight,
		LOCTEXT("Height", "Stackable poles: remember height"),
		LOCTEXT("HeightTip", "Stackable conveyor poles / pipeline / hypertube supports start at the height of the last one you built this session, if it still exists and is within the working area. Otherwise they start at 1."),
		D.bRememberStackHeight);
	AddBool(Root, BoolCls, ScrollToHeight,
		LOCTEXT("Scroll", "Stackable poles: scroll wheel sets height"),
		LOCTEXT("ScrollTip", "After placing the base (zoop step), the mouse wheel adds or removes one level."),
		D.bScrollToHeight);
	AddBool(Root, BoolCls, AimToHeight,
		LOCTEXT("Aim", "Stackable poles: aim sets height"),
		LOCTEXT("AimTip", "Looking up/down changes the height, like vanilla. Off = the height only changes with the scroll wheel. With remember height and scroll off, this is plain vanilla."),
		D.bAimToHeight);
	AddFloat(Root, FloatCls, WorkingArea,
		LOCTEXT("Area", "Working area (m)"),
		LOCTEXT("AreaTip", "Distance from the last pole you built within which the remembered height and the ghost preview apply."),
		D.WorkingArea, 0.f, 1000.f);

	AddBool(Root, BoolCls, GhostPreview,
		LOCTEXT("Ghost", "Ghost belt/pipe to last pole"),
		LOCTEXT("GhostTip", "While placing a conveyor pole or pipeline support (single or stackable), preview the belt or pipe to the last one you built. Yellow = near or just past the length limit."),
		D.bGhostPreview);

	AddBool(Root, BoolCls, InheritPowerSwatch,
		LOCTEXT("PowerSwatch", "Colour: power poles"),
		LOCTEXT("PowerSwatchTip", "A pole, outlet or tower built by dragging a power line from another pole/outlet/tower takes that one's colour. Never from machines; never repaints existing ones."),
		D.bInheritPowerSwatch);
	AddBool(Root, BoolCls, InheritBeltPipeSwatch,
		LOCTEXT("BeltSwatch", "Colour: belts, pipes, hypertubes"),
		LOCTEXT("BeltSwatchTip", "A belt/lift, pipe or hypertube continued from another of its kind, or from a splitter/merger/junction/pump/valve, takes its colour. Never from machines; never repaints existing ones."),
		D.bInheritBeltPipeSwatch);
	AddBool(Root, BoolCls, InheritAttachmentSwatch,
		LOCTEXT("AttachSwatch", "Colour: splitters, mergers, pumps, valves"),
		LOCTEXT("AttachSwatchTip", "A splitter, merger, pump, valve or pipe junction placed onto a belt or pipe takes that belt's or pipe's colour."),
		D.bInheritAttachmentSwatch);
	AddBool(Root, BoolCls, InheritStackSwatch,
		LOCTEXT("StackSwatch", "Colour: stacked poles"),
		LOCTEXT("StackSwatchTip", "Stackable poles placed onto an existing stack take that stack's colour."),
		D.bInheritStackSwatch);
	AddBool(Root, BoolCls, KeepColourOnUpgrade,
		LOCTEXT("UpgradeSwatch", "Colour: keep on upgrade"),
		LOCTEXT("UpgradeSwatchTip", "Upgrading anything (belts, lifts, pipes, power poles, ...) keeps the old piece's colour."),
		D.bKeepColourOnUpgrade);

	AddInt(Root, IntCls, PowerPoleTierFromBuilding,
		LOCTEXT("PoleTier", "Power pole tier from buildings (1-3)"),
		LOCTEXT("PoleTierTip", "Tier of the pole or wall outlet auto-built when you drag a power line from a building. 1 = vanilla (Mk1). Falls back to the highest unlocked tier below it."),
		D.PowerPoleTierFromBuilding, 1, 3);

	CDO->RootSection = Root;
	UE_LOG(LogStashysToolbelt, Log, TEXT("Settings tree built (%d entries)"), Root->SectionProperties.Num());
}

FToolbeltPrefs UToolbeltConfig::ReadLocal(const UObject* WorldContext)
{
	FToolbeltPrefs Out;
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
	const UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
	const UConfigManager* Config = GI ? GI->GetSubsystem<UConfigManager>() : nullptr;
	const UConfigPropertySection* Root = Config ? Config->GetConfigurationRootSection(GetConfigId()) : nullptr;
	if (!Root)
	{
		return Out;
	}
	using namespace ToolbeltKeys;
	Out.bZoopByDefault            = GetBool(Root, ZoopByDefault, Out.bZoopByDefault);
	Out.bRememberStackHeight      = GetBool(Root, RememberStackHeight, Out.bRememberStackHeight);
	Out.bScrollToHeight           = GetBool(Root, ScrollToHeight, Out.bScrollToHeight);
	Out.bAimToHeight              = GetBool(Root, AimToHeight, Out.bAimToHeight);
	Out.WorkingArea               = GetFloat(Root, WorkingArea, Out.WorkingArea);
	Out.bGhostPreview             = GetBool(Root, GhostPreview, Out.bGhostPreview);
	Out.bInheritPowerSwatch       = GetBool(Root, InheritPowerSwatch, Out.bInheritPowerSwatch);
	Out.bInheritBeltPipeSwatch    = GetBool(Root, InheritBeltPipeSwatch, Out.bInheritBeltPipeSwatch);
	Out.bInheritAttachmentSwatch  = GetBool(Root, InheritAttachmentSwatch, Out.bInheritAttachmentSwatch);
	Out.bInheritStackSwatch       = GetBool(Root, InheritStackSwatch, Out.bInheritStackSwatch);
	Out.bKeepColourOnUpgrade      = GetBool(Root, KeepColourOnUpgrade, Out.bKeepColourOnUpgrade);
	Out.PowerPoleTierFromBuilding = GetInt(Root, PowerPoleTierFromBuilding, Out.PowerPoleTierFromBuilding);
	Out.Sanitize();
	return Out;
}

FToolbeltPrefs UToolbeltConfig::ForController(const AController* Controller)
{
	if (!Controller)
	{
		return FToolbeltPrefs();
	}
	if (Controller->IsLocalController())
	{
		return ReadLocal(Controller);
	}
	// Server side, remote player. GetRemoteCallObjectOfClass isn't const.
	AFGPlayerController* PC = Cast<AFGPlayerController>(const_cast<AController*>(Controller));
	const UToolbeltRCO* RCO = PC ? PC->GetRemoteCallObjectOfClass<UToolbeltRCO>() : nullptr;
	// Until the client's settings arrive, behave like vanilla rather than assume "all on".
	return (RCO && RCO->HasReceivedPrefs()) ? RCO->GetReceivedPrefs() : FToolbeltPrefs::Vanilla();
}

#undef LOCTEXT_NAMESPACE
