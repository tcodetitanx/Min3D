#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "MinPawn.generated.h"

class UCameraComponent;

// Camera rig: orthographic front view, orthographic side view (perpendicular
// planes), and a perspective matrix overview of the whole lattice.
UCLASS()
class AMinPawn : public APawn
{
	GENERATED_BODY()

public:
	AMinPawn();

	UPROPERTY() TObjectPtr<UCameraComponent> Camera;

	// plane-view state. PanCenter.X runs along the screen-right axis of the
	// current orientation (world X in front view, world Y in side view);
	// PanCenter.Y is world Z.
	FVector2D PanCenter = FVector2D(4500.f, -2500.f);
	float OrthoWidth = 7000.f;
	bool bSideView = false;
	int32 ViewFront = 1;  // 0..2
	int32 ViewSide = 1;   // 0..2
	bool bMatrixView = false;
	float MatrixYaw = -38.f;
	float MatrixPitch = -22.f;

	// current plane index in the global 0..5 numbering
	int32 CurrentPlane() const;
	void ClampPan();

	void UpdateCamera(float DeltaSeconds);

private:
	FVector LocCur = FVector::ZeroVector;
	float YawCur = -90.f;
	bool bSnapped = false;
};
