#include "MinAudio.h"
#include "Min3D.h"
#include "Sound/SoundWaveProcedural.h"
#include "Components/AudioComponent.h"
#include "Kismet/GameplayStatics.h"

void UMinAudio::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
}

void UMinAudio::Deinitialize()
{
	if (Stream) Stream->OnSoundWaveProceduralUnderflow.Unbind();
	Super::Deinitialize();
}

void UMinAudio::EnsurePlaying(UWorld* World)
{
	if (Component && Component->IsPlaying()) return;
	if (!Stream)
	{
		Stream = NewObject<USoundWaveProcedural>(this);
		Stream->SetSampleRate(SampleRate);
		Stream->NumChannels = 2;
		Stream->Duration = INDEFINITELY_LOOPING_DURATION;
		Stream->SoundGroup = SOUNDGROUP_Default;
		Stream->bLooping = false;
		Stream->OnSoundWaveProceduralUnderflow =
			FOnSoundWaveProceduralUnderflow::CreateUObject(this, &UMinAudio::OnUnderflow);
	}
	Component = UGameplayStatics::SpawnSound2D(World, Stream, 1.f, 1.f, 0.f, nullptr, true, false);
}

void UMinAudio::AddVoice(const FVoice& V)
{
	FScopeLock Lock(&Cs);
	if (Voices.Num() < 64) Voices.Add(V);
}

void UMinAudio::Play(EMinSfx Sfx, float Volume, float Pan)
{
	auto V = [&](float F0, float F1, float Dur, float Vol, int32 Wave,
		float Delay = 0.f, float Attack = 0.004f, float DecayPow = 2.f)
	{
		FVoice Voice;
		Voice.Freq0 = F0; Voice.Freq1 = F1; Voice.Dur = Dur;
		Voice.Vol = Vol * Volume * SfxVolume;
		Voice.Wave = Wave; Voice.StartDelay = Delay;
		Voice.Attack = Attack; Voice.DecayPow = DecayPow; Voice.Pan = Pan;
		AddVoice(Voice);
	};

	switch (Sfx)
	{
	case EMinSfx::Click:    V(880, 640, 0.055f, 0.20f, 0); break;
	case EMinSfx::Build:    V(520, 380, 0.12f, 0.22f, 0); V(1040, 780, 0.05f, 0.10f, 1); break;
	case EMinSfx::Dig:      V(0, 0, 0.16f, 0.22f, 2, 0.f, 0.004f, 3.f); V(130, 70, 0.18f, 0.20f, 0); break;
	case EMinSfx::Remove:   V(0, 0, 0.10f, 0.16f, 2); V(320, 170, 0.12f, 0.16f, 1); break;
	case EMinSfx::Delivery: V(660, 660, 0.07f, 0.16f, 0); V(990, 990, 0.09f, 0.14f, 0, 0.07f); break;
	case EMinSfx::Chime:
		V(523, 523, 0.22f, 0.16f, 0, 0.00f, 0.01f);
		V(659, 659, 0.22f, 0.16f, 0, 0.09f, 0.01f);
		V(784, 784, 0.22f, 0.16f, 0, 0.18f, 0.01f);
		V(1047, 1047, 0.34f, 0.18f, 0, 0.27f, 0.01f);
		break;
	case EMinSfx::Whoosh:   V(0, 0, 0.32f, 0.10f, 2, 0.f, 0.10f, 1.6f); V(240, 900, 0.30f, 0.05f, 0, 0.f, 0.08f); break;
	case EMinSfx::Warn:     V(494, 494, 0.09f, 0.15f, 3); V(494, 494, 0.09f, 0.15f, 3, 0.14f); break;
	case EMinSfx::Coin:     V(1320, 1660, 0.06f, 0.14f, 0); V(1760, 1760, 0.10f, 0.12f, 0, 0.05f); break;
	case EMinSfx::Error:    V(220, 175, 0.16f, 0.16f, 3); break;
	case EMinSfx::PlaneSwitch: V(0, 0, 0.22f, 0.07f, 2, 0.f, 0.06f, 1.8f); V(520, 260, 0.20f, 0.08f, 1); break;
	}
}

void UMinAudio::TickMusic(float Dt)
{
	// gentle pentatonic pad + occasional cave drips, scheduled on the game thread
	MusicTimer -= Dt;
	DripTimer -= Dt;

	if (MusicTimer <= 0.f)
	{
		MusicTimer = 1.9f;
		static const float Scale[] = { 220.f, 261.63f, 293.66f, 349.23f, 392.f, 440.f, 523.25f };
		const int32 StepPattern[] = { 0, 4, 2, 5, 3, 6, 1, 4 };
		const int32 Root = StepPattern[ChordStep % UE_ARRAY_COUNT(StepPattern)];
		++ChordStep;

		FVoice Pad;
		Pad.Freq0 = Pad.Freq1 = Scale[Root % UE_ARRAY_COUNT(Scale)];
		Pad.Dur = 1.7f; Pad.Vol = 0.045f * MusicVolume; Pad.Wave = 0;
		Pad.Attack = 0.5f; Pad.DecayPow = 1.4f;
		Pad.Pan = (ChordStep % 2) ? -0.3f : 0.3f;
		AddVoice(Pad);
		if (ChordStep % 2 == 0)
		{
			Pad.Freq0 = Pad.Freq1 = Scale[(Root + 2) % UE_ARRAY_COUNT(Scale)] * 2.f;
			Pad.Vol = 0.03f * MusicVolume;
			Pad.Pan = -Pad.Pan;
			AddVoice(Pad);
		}
	}

	if (DripTimer <= 0.f)
	{
		DripTimer = 2.f + FMath::FRand() * 5.f;
		FVoice Drip;
		Drip.Freq0 = 1400.f + FMath::FRand() * 500.f;
		Drip.Freq1 = 380.f;
		Drip.Dur = 0.11f; Drip.Vol = 0.05f * MusicVolume; Drip.Wave = 0;
		Drip.DecayPow = 2.5f;
		Drip.Pan = FMath::FRandRange(-0.8f, 0.8f);
		AddVoice(Drip);
	}
}

void UMinAudio::OnUnderflow(USoundWaveProcedural* Wave, int32 SamplesNeeded)
{
	const int32 Frames = FMath::Max(SamplesNeeded / 2, 512);
	TArray<int16> Buffer;
	Buffer.SetNumUninitialized(Frames * 2);

	FScopeLock Lock(&Cs);
	const float InvSR = 1.f / SampleRate;
	for (int32 F = 0; F < Frames; ++F)
	{
		float L = 0.f, R = 0.f;

		// ambience bed: soft low-passed noise + very low drone
		Rng = Rng * 1664525u + 1013904223u;
		const float White = ((Rng >> 9) & 0xFFFF) / 32768.f - 1.f;
		NoiseLP_L += 0.02f * (White - NoiseLP_L);
		Rng = Rng * 1664525u + 1013904223u;
		const float White2 = ((Rng >> 9) & 0xFFFF) / 32768.f - 1.f;
		NoiseLP_R += 0.02f * (White2 - NoiseLP_R);
		AmbPhase += 2.0 * PI * 52.0 * InvSR;
		const float Drone = FMath::Sin((float)AmbPhase) * 0.012f;
		L += NoiseLP_L * 0.035f + Drone;
		R += NoiseLP_R * 0.035f + Drone;

		for (FVoice& Vc : Voices)
		{
			Vc.T += InvSR;
			const float Lt = Vc.T - Vc.StartDelay;
			if (Lt < 0.f || Lt >= Vc.Dur) continue;
			const float A = Lt / Vc.Dur;
			const float Freq = FMath::Lerp(Vc.Freq0, Vc.Freq1, A);
			Vc.Phase += 2.0 * PI * Freq * InvSR;
			float S;
			switch (Vc.Wave)
			{
			case 1: S = 2.f / PI * FMath::FastAsin(FMath::Sin((float)Vc.Phase)); break;
			case 2:
			{
				Rng = Rng * 1664525u + 1013904223u;
				S = ((Rng >> 9) & 0xFFFF) / 32768.f - 1.f;
				break;
			}
			case 3: S = FMath::Sin((float)Vc.Phase) > 0.f ? 0.7f : -0.7f; break;
			default: S = FMath::Sin((float)Vc.Phase); break;
			}
			const float Env = (Lt < Vc.Attack)
				? Lt / FMath::Max(Vc.Attack, 1e-4f)
				: FMath::Pow(1.f - (Lt - Vc.Attack) / FMath::Max(Vc.Dur - Vc.Attack, 1e-4f), Vc.DecayPow);
			const float Out = S * Env * Vc.Vol;
			L += Out * (1.f - FMath::Max(0.f, Vc.Pan));
			R += Out * (1.f + FMath::Min(0.f, Vc.Pan));
		}

		Buffer[F * 2] = (int16)(FMath::Clamp(L, -1.f, 1.f) * 32000.f);
		Buffer[F * 2 + 1] = (int16)(FMath::Clamp(R, -1.f, 1.f) * 32000.f);
	}
	Voices.RemoveAll([](const FVoice& V) { return V.Done(); });

	Wave->QueueAudio(reinterpret_cast<const uint8*>(Buffer.GetData()), Buffer.Num() * sizeof(int16));
}
