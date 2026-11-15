// SPDX-License-Identifier: GPL-3.0-or-later
// Stashy's Toolbelt — Copyright (C) 2026 Stashy. See LICENSE.
#pragma once

#include "CoreMinimal.h"
#include "FGRemoteCallObject.h"
#include "ToolbeltSettings.h"
#include "ToolbeltRCO.generated.h"

/**
 * Client -> server channel for one player's settings. One instance per player controller.
 * On the server, the instance of a remote player's controller holds that player's settings.
 * Registered through UToolbeltRootInstance::RemoteCallObjects.
 */
UCLASS()
class STASHYSTOOLBELT_API UToolbeltRCO : public UFGRemoteCallObject
{
	GENERATED_BODY()
public:
	UFUNCTION(Server, Reliable)
	void ServerSetPrefs(const FToolbeltPrefs& InPrefs);

	bool HasReceivedPrefs() const { return bReceivedPrefs; }
	const FToolbeltPrefs& GetReceivedPrefs() const { return ReceivedPrefs; }

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	/** An RCO needs at least one replicated property or its RPCs don't work (SML docs). */
	UPROPERTY(Replicated)
	bool bDummy = false;

	/** Server-side only. */
	FToolbeltPrefs ReceivedPrefs;
	bool bReceivedPrefs = false;
};
