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

	AMinPlaneActor* GetPlaneActor(int32 P) const { return PlaneActors.IsValidIndex(P) ? PlaneActors[P] : nullptr; }
	void UpdatePlaneVisibility();

	UPROPERTY() TArray<TObjectPtr<AMinPlaneActor>> PlaneActors;
	UPROPERTY() TObjectPtr<UMinWorld> MinWorld;
	UPROPERTY() TObjectPtr<UMinAssets> Assets;
	UPROPERTY() TObjectPtr<UMinSim> Sim;
	UPROPERTY() TObjectPtr<UMinAudio> Audio;

	bool bGhostPlanes = true;

	// interactive tutorial (-1 = inactive)
	int32 TutStep = -1;
	FString TutTitle, TutText;
	bool bTutFinal = false;
	void TutorialNext();
	void TutorialSkip();

private:
	void RunAutoBuild();
	void SetTutStep(int32 Step);
	void TickTutorial(float DeltaSeconds);
	int32 TutBaselineEdges = 0;
	float TutPulse = 0.f;
	FIntVector TutTarget = FIntVector(-1, -1, -1);
	UPROPERTY() TObjectPtr<AActor> TutMarkerActor;
	UPROPERTY() TObjectPtr<class UStaticMeshComponent> TutMarker;

	// headless smoke-test hooks
	float AutoShotAt = -1.f;
	float AutoQuitAt = -1.f;
	float RunTime = 0.f;
	float HeartbeatAt = 0.f;
	int32 ShotsTaken = 0;
	float NextShotAt = -1.f;
};
