// SPDX-License-Identifier: GPL-3.0-or-later
// Stashy's Toolbelt — Copyright (C) 2026 Stashy. See LICENSE.
#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

// Compile-time verbosity must be All: SML's Blueprint logging creates Log<ModRef> with All too.
STASHYSTOOLBELT_API DECLARE_LOG_CATEGORY_EXTERN(LogStashysToolbelt, Verbose, All);

class FStashysToolbeltModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
