// SPDX-License-Identifier: GPL-3.0-or-later
// Stashy's Toolbelt — Copyright (C) 2026 Stashy. See LICENSE.
// Console commands (open the in-game console, usually the ` or ~ key):
//   Toolbelt.Status              settings in effect, remembered heights, ghost/zoop state
//   Toolbelt.Set <Key> <Value>   change a setting (saved to Configs/StashysToolbelt.cfg)
//   Toolbelt.Reset               all settings back to defaults
// Works whether or not the Mod Settings screen draws the settings — it edits the same values.

#include "StashysToolbelt.h"
#include "ToolbeltSettings.h"
#include "ToolbeltStackHeight.h"
#include "ToolbeltGhost.h"
#include "ToolbeltZoop.h"

#include "Configuration/ConfigManager.h"
#include "Configuration/Properties/ConfigPropertyBool.h"
#include "Configuration/Properties/ConfigPropertyFloat.h"
#include "Configuration/Properties/ConfigPropertyInteger.h"
#include "Configuration/Properties/ConfigPropertySection.h"
#include "Configuration/Properties/WidgetExtension/CP_Float.h"
#include "Configuration/Properties/WidgetExtension/CP_Integer.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/OutputDevice.h"

namespace ToolbeltConsoleImpl
{
	UConfigManager* GetConfig(UWorld* World)
	{
		UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
		return GI ? GI->GetSubsystem<UConfigManager>() : nullptr;
	}

	FString ValueToString(const UConfigProperty* P)
	{
		if (const UConfigPropertyBool* B = Cast<UConfigPropertyBool>(P))       { return B->Value ? TEXT("on") : TEXT("off"); }
		if (const UConfigPropertyInteger* I = Cast<UConfigPropertyInteger>(P)) { return FString::FromInt(I->Value); }
		if (const UConfigPropertyFloat* F = Cast<UConfigPropertyFloat>(P))     { return FString::SanitizeFloat(F->Value); }
		return TEXT("?");
	}

	bool ParseBool(const FString& In, bool& Out)
	{
		const FString V = In.ToLower();
		if (V == TEXT("1") || V == TEXT("on") || V == TEXT("true") || V == TEXT("yes"))  { Out = true;  return true; }
		if (V == TEXT("0") || V == TEXT("off") || V == TEXT("false") || V == TEXT("no")) { Out = false; return true; }
		return false;
	}

	void ListSettings(const UConfigPropertySection* Root, FOutputDevice& Ar)
	{
		for (const TPair<FString, TObjectPtr<UConfigProperty>>& Entry : Root->SectionProperties)
		{
			Ar.Logf(TEXT("  %s = %s   (%s)"), *Entry.Key, *ValueToString(Entry.Value), *Entry.Value->DisplayName.ToString());
		}
	}

	void Status(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
	{
		Ar.Log(TEXT("Stashy's Toolbelt — status"));
		const UConfigManager* Config = GetConfig(World);
		const UConfigPropertySection* Root = Config ? Config->GetConfigurationRootSection(UToolbeltConfig::GetConfigId()) : nullptr;
		if (Root)
		{
			ListSettings(Root, Ar);
		}
		else
		{
			Ar.Log(TEXT("  Settings not registered (mod config missing) — defaults in effect"));
		}
		FString State = FToolbeltStackHooks::DescribeState() + FToolbeltGhost::DescribeState() + ToolbeltZoop::DescribeState();
		TArray<FString> Lines;
		State.ParseIntoArrayLines(Lines);
		for (const FString& Line : Lines)
		{
			Ar.Log(Line);
		}
	}

	void Set(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
	{
		UConfigManager* Config = GetConfig(World);
		UConfigPropertySection* Root = Config ? Config->GetConfigurationRootSection(UToolbeltConfig::GetConfigId()) : nullptr;
		if (!Root)
		{
			Ar.Log(TEXT("Toolbelt: settings not registered"));
			return;
		}
		if (Args.Num() < 2)
		{
			Ar.Log(TEXT("Usage: Toolbelt.Set <Key> <Value>   e.g. Toolbelt.Set ZoopByDefault off"));
			ListSettings(Root, Ar);
			return;
		}
		UConfigProperty* Prop = nullptr;
		FString Key;
		for (const TPair<FString, TObjectPtr<UConfigProperty>>& Entry : Root->SectionProperties)
		{
			if (Entry.Key.Equals(Args[0], ESearchCase::IgnoreCase))
			{
				Prop = Entry.Value;
				Key = Entry.Key;
				break;
			}
		}
		if (!Prop)
		{
			Ar.Logf(TEXT("Toolbelt: unknown key '%s'. Keys:"), *Args[0]);
			ListSettings(Root, Ar);
			return;
		}
		bool bOk = true;
		bool bClamped = false;
		if (UConfigPropertyBool* B = Cast<UConfigPropertyBool>(Prop))
		{
			bool V = false;
			bOk = ParseBool(Args[1], V);
			if (bOk) { B->Value = V; }
		}
		else if (UConfigPropertyInteger* I = Cast<UConfigPropertyInteger>(Prop))
		{
			bOk = Args[1].IsNumeric();
			if (bOk)
			{
				int32 V = FCString::Atoi(*Args[1]);
				// Same range as the settings screen, so the stored value is never shown as invalid.
				// (Native fallback class without a range: ReadLocal's Sanitize still clamps on use.)
				if (const UCP_Integer* W = Cast<UCP_Integer>(I); W && W->MaxValue > W->MinValue)
				{
					const int32 C = FMath::Clamp(V, W->MinValue, W->MaxValue);
					bClamped = C != V;
					V = C;
				}
				I->Value = V;
			}
		}
		else if (UConfigPropertyFloat* F = Cast<UConfigPropertyFloat>(Prop))
		{
			bOk = Args[1].IsNumeric();
			if (bOk)
			{
				float V = FCString::Atof(*Args[1]);
				if (const UCP_Float* W = Cast<UCP_Float>(F); W && W->MaxValue > W->MinValue)
				{
					const float C = FMath::Clamp(V, W->MinValue, W->MaxValue);
					bClamped = C != V;
					V = C;
				}
				F->Value = V;
			}
		}
		else
		{
			bOk = false;
		}
		if (!bOk)
		{
			Ar.Logf(TEXT("Toolbelt: '%s' isn't a valid value for %s"), *Args[1], *Key);
			return;
		}
		Prop->MarkDirty();   // SML saves the config file
		Ar.Logf(TEXT("Toolbelt: %s = %s%s"), *Key, *ValueToString(Prop), bClamped ? TEXT("   (clamped to the allowed range)") : TEXT(""));
	}

	void Reset(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
	{
		UConfigManager* Config = GetConfig(World);
		if (Config && Config->ResetToDefault(UToolbeltConfig::GetConfigId()))
		{
			Ar.Log(TEXT("Toolbelt: settings reset to defaults"));
		}
		else
		{
			Ar.Log(TEXT("Toolbelt: nothing to reset (settings not registered or already default)"));
		}
	}

	// Registered at module load (harmless in editor commandlets: nothing runs unless typed).
	FAutoConsoleCommandWithWorldArgsAndOutputDevice StatusCmd(
		TEXT("Toolbelt.Status"),
		TEXT("Stashy's Toolbelt: show settings and session state."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&Status));
	FAutoConsoleCommandWithWorldArgsAndOutputDevice SetCmd(
		TEXT("Toolbelt.Set"),
		TEXT("Stashy's Toolbelt: Toolbelt.Set <Key> <Value> — change a setting (saved)."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&Set));
	FAutoConsoleCommandWithWorldArgsAndOutputDevice ResetCmd(
		TEXT("Toolbelt.Reset"),
		TEXT("Stashy's Toolbelt: reset all settings to defaults."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&Reset));
}
