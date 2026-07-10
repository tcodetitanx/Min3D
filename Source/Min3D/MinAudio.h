#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "MinAudio.generated.h"

class USoundWaveProcedural;
class UAudioComponent;

enum class EMinSfx : uint8
{
	Click, Build, Dig, Remove, Delivery, Chime, Whoosh, Warn, Coin, Error, PlaneSwitch
};

// All Min3D audio is synthesized: one procedural stereo stream mixes SFX
// voices, an underground ambience bed and a gentle generative pad.
UCLASS()
class UMinAudio : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	void EnsurePlaying(UWorld* World);
	void Play(EMinSfx Sfx, float Volume = 1.f, float Pan = 0.f);
	void TickMusic(float Dt); // called from game thread

	float SfxVolume = 1.f;
	float MusicVolume = 1.f;

private:
	struct FVoice
	{
		float Freq0 = 440.f, Freq1 = 440.f;
		float Dur = 0.2f;
		float Vol = 0.2f;
		float Attack = 0.005f;
		float DecayPow = 2.f;
		float StartDelay = 0.f;
		float Pan = 0.f;
		int32 Wave = 0; // 0 sine, 1 tri, 2 noise, 3 square
		float T = 0.f;
		double Phase = 0.0;
		bool Done() const { return T >= Dur + StartDelay; }
	};

	void AddVoice(const FVoice& V);
	void OnUnderflow(USoundWaveProcedural* Wave, int32 SamplesNeeded);

	UPROPERTY() TObjectPtr<USoundWaveProcedural> Stream;
	UPROPERTY() TObjectPtr<UAudioComponent> Component;

	FCriticalSection Cs;
	TArray<FVoice> Voices;
	int32 SampleRate = 48000;
	uint32 Rng = 0x2545F491u;
	float NoiseLP_L = 0.f, NoiseLP_R = 0.f;
	double AmbPhase = 0.0;
	float MusicTimer = 1.f;
	float DripTimer = 3.f;
	int32 ChordStep = 0;
};
