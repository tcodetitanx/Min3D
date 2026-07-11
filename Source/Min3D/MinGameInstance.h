#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "MinTypes.h"
#include "MinGameInstance.generated.h"

UCLASS()
class UMinGameInstance : public UGameInstance
{
	GENERATED_BODY()

public:
	// set before (re)opening the Main map
	int32 WorldSeed = 824711;
	EMinGameRules PendingRules = EMinGameRules::SinglePlane;
	bool bSkipTitle = false; // true on restarts: start the run immediately
};
