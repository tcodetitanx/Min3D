#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "MinGameInstance.generated.h"

UCLASS()
class UMinGameInstance : public UGameInstance
{
	GENERATED_BODY()

public:
	// set from the title screen; MinGameMode reads it on map start
	bool bRequestLoadSave = false;
	int32 WorldSeed = 824711;
};
