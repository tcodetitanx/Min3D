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

int32 AMinPawn::CurrentPlane() const
{
	return bSideView ? Min::NumFront + ViewSide : ViewFront;
}

void AMinPawn::ClampPan()
{
	const float W = (bSideView ? Min::GD : Min::GX) * Min::CellSize;
	PanCenter.X = FMath::Clamp(PanCenter.X, 0.f, W);
	PanCenter.Y = FMath::Clamp(PanCenter.Y, -Min::GZ * Min::CellSize, 0.f);
}

void AMinPawn::UpdateCamera(float DeltaSeconds)
{
	if (bMatrixView)
	{
		Camera->SetProjectionMode(ECameraProjectionMode::Perspective);
		Camera->SetFieldOfView(45.f);
		const FVector Center(Min::GX * Min::CellSize * 0.5f,
			Min::GD * Min::CellSize * 0.5f, -Min::GZ * Min::CellSize * 0.5f);
		const FRotator Rot(MatrixPitch, MatrixYaw, 0.f);
		const float Dist = 14000.f * (OrthoWidth / 7000.f);
		SetActorLocation(Center - Rot.Vector() * Dist);
		SetActorRotation(Rot);
		bSnapped = false;
		return;
	}

	Camera->SetProjectionMode(ECameraProjectionMode::Orthographic);
	Camera->SetOrthoWidth(OrthoWidth);

	// front view: camera on the +Y side looking -Y (screen right = +X)
	// side view:  camera on the -X side looking +X (screen right = +Y)
	FVector Target;
	float TargetYaw;
	if (!bSideView)
	{
		Target = FVector(PanCenter.X, Min::FrontPlaneY(ViewFront) + 2500.f, PanCenter.Y);
		TargetYaw = -90.f;
	}
	else
	{
		Target = FVector(Min::SidePlaneX(ViewSide) - 3500.f, PanCenter.X, PanCenter.Y);
		TargetYaw = 0.f;
	}

	if (!bSnapped) { LocCur = Target; YawCur = TargetYaw; bSnapped = true; }
	LocCur = FMath::VInterpTo(LocCur, Target, DeltaSeconds, 10.f);
	YawCur = FMath::FInterpTo(YawCur, TargetYaw, DeltaSeconds, 10.f);
	SetActorLocation(LocCur);
	SetActorRotation(FRotator(0.f, YawCur, 0.f));
}
