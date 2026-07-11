#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "MinTypes.h"
#include "MinGameMode.generated.h"

class AMinPlaneActor;
class UMinWorld;
class UMinAssets;
class UMinSim;
class UMinAudio;
class AMinPawn;

UCLASS()
class AMinGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AMinGameMode();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	// starts the actual run (called from the title screen or immediately on restarts)
	void BeginRun(EMinGameRules Rules);

	AMinPlaneActor* GetPlaneActor(int32 P) const { return PlaneActors.IsValidIndex(P) ? PlaneActors[P] : nullptr; }
	void UpdatePlaneVisibility();

	UPROPERTY() TArray<TObjectPtr<AMinPlaneActor>> PlaneActors;
	UPROPERTY() TObjectPtr<UMinWorld> MinWorld;
	UPROPERTY() TObjectPtr<UMinAssets> Assets;
	UPROPERTY() TObjectPtr<UMinSim> Sim;
	UPROPERTY() TObjectPtr<UMinAudio> Audio;

	bool bGhostPlanes = true;

private:
	void RunAutoBuild();
	void SpawnBackdrops();
	float MarkerRefreshT = 0.f;

	// headless smoke-test hooks
	float AutoQuitAt = -1.f;
	float RunTime = 0.f;
	float HeartbeatAt = 0.f;
	int32 ShotsTaken = 0;
	float NextShotAt = -1.f;
};
