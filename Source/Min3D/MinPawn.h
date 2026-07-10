#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "MinPawn.generated.h"

class UCameraComponent;

// Camera rig: orthographic plane view / perspective matrix overview.
UCLASS()
class AMinPawn : public APawn
{
	GENERATED_BODY()

public:
	AMinPawn();

	UPROPERTY() TObjectPtr<UCameraComponent> Camera;

	// plane-view state
	FVector2D PanCenter = FVector2D(4800.f, -2400.f); // world X, Z
	float OrthoWidth = 7000.f;
	int32 ViewPlane = 4;
	bool bMatrixView = false;
	float MatrixYaw = -38.f;
	float MatrixPitch = -22.f;

	void UpdateCamera(float DeltaSeconds);

private:
	float PlaneYCur = 0.f; // smoothed camera plane Y
	bool bSnapped = false;
};
