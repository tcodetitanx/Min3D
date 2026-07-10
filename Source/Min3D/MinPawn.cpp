#include "MinPawn.h"
#include "MinTypes.h"
#include "Camera/CameraComponent.h"

AMinPawn::AMinPawn()
{
	PrimaryActorTick.bCanEverTick = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(RootComponent);
	Camera->SetProjectionMode(ECameraProjectionMode::Orthographic);
	Camera->SetOrthoWidth(OrthoWidth);
	Camera->SetAutoCalculateOrthoPlanes(false);
	Camera->SetOrthoNearClipPlane(10.f);
	Camera->SetOrthoFarClipPlane(30000.f);
}

void AMinPawn::UpdateCamera(float DeltaSeconds)
{
	const float TargetPlaneY = ViewPlane * Min::PlaneGap;
	if (!bSnapped) { PlaneYCur = TargetPlaneY; bSnapped = true; }
	PlaneYCur = FMath::FInterpTo(PlaneYCur, TargetPlaneY, DeltaSeconds, 10.f);

	if (bMatrixView)
	{
		Camera->SetProjectionMode(ECameraProjectionMode::Perspective);
		Camera->SetFieldOfView(45.f);
		const FVector Center(Min::NX * Min::CellSize * 0.5f,
			(Min::NP - 1) * Min::PlaneGap * 0.5f, -Min::NY * Min::CellSize * 0.5f);
		const FRotator Rot(MatrixPitch, MatrixYaw, 0.f);
		const float Dist = 16000.f * (OrthoWidth / 7000.f);
		SetActorLocation(Center - Rot.Vector() * Dist);
		SetActorRotation(Rot);
	}
	else
	{
		Camera->SetProjectionMode(ECameraProjectionMode::Orthographic);
		Camera->SetOrthoWidth(OrthoWidth);
		// look along -Y so that screen-right is world +X (right-handed view;
		// a +Y-facing camera mirrors the X axis and made mouse picking flip)
		SetActorLocation(FVector(PanCenter.X, PlaneYCur + 2.5f * Min::PlaneGap, PanCenter.Y));
		SetActorRotation(FRotator(0.f, -90.f, 0.f));
	}
}
