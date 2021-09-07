/**
* The MIT License (MIT)
* Copyright (c) 2021 Cedric Liaudet
*/
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "VolumetricAudioSource.generated.h"

/**
 * Audio volumetric to play looping ambient sounds.
 *
 * Sound will played along the edge of the spline and will follow the audio listener when he is inside.
 */
UCLASS()
class VOLUMETRICAUDIOSOURCE_API AVolumetricAudioSource : public AActor
{
	GENERATED_BODY()

public:
	AVolumetricAudioSource();

	/**
	* Returns true if the given location is inside the spline.
	* OutClosestLocation will contain the closest location on the spline.
	*/
	bool IsInside(const FVector& Location, FVector& OutClosestLocation) const;
	
	virtual bool ShouldTickIfViewportsOnly() const override { return !IsTemplate(); }
	
	virtual void TickActor(float DeltaTime, enum ELevelTick TickType, FActorTickFunction& ThisTickFunction) override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

protected:
	virtual void BeginPlay() override;

private:
	/** Sound to play in the volumetric. */
	UPROPERTY(EditAnywhere, Category="Audio")
	class USoundCue* SoundLoop = nullptr;

	/** Color used to display the volumetric. */
	UPROPERTY(EditAnywhere, Category="Audio")
	FColor Color = FColor::Orange;

	/** The height of the volumetric. */ 
	UPROPERTY(EditAnywhere, Category="Audio", Meta=(MinClamp=0))
	float MaxHeight = 400.0f;

	/**
	 * The distance max to play the sound and the random SFX.
	 * We automatically get the distance from the sound loop or the one from the random SFX if there is no loop.
	 */
	UPROPERTY()
	float MaxDistance = 0.0f;

	/** Random SFX to play around the closest point. */
	UPROPERTY(EditAnywhere, Category="Audio|Random")
	TArray<class USoundBase*> RandomSFX;

	/** Min Delay before playing another random SFX. */
	UPROPERTY(EditAnywhere, Category="Audio|Random")
	float MinDelay = 5.0f;

	/** Max Delay before playing another random SFX. */
	UPROPERTY(EditAnywhere, Category="Audio|Random")
	float MaxDelay = 10.0f;
	
	/** Box where the random SFX will play. */
	UPROPERTY(EditAnywhere, Category="Audio|Random", Meta=(MinClamp=100.0f))
	FBox Box = {FVector(-200.0f, -200.0f, -200.0f), FVector(200.0f, 200.0f, 200.0f)};

	/** Offset on the Z-Axis of the box from the closest point. */
	UPROPERTY(EditAnywhere, Category="Audio|Random")
	float Offset = 0.0f;
	
	UPROPERTY()
	class USplineComponent* Spline = nullptr;

	UPROPERTY()
	class UAudioComponent* AudioComponent = nullptr;

	/** Current delay before playing the next random SFX. */
	float CurrentDelay = 0.0f;

#if !UE_BUILD_SHIPPING
	struct FDrawDebugSFX
	{
		FVector Location = FVector::ZeroVector;
		float Duration = 0.0f;
	};

	TArray<FDrawDebugSFX> DrawDebugRandomSFXs;
	
	void Draw(float DeltaTime, const FVector& ClosestLocation);
#endif
};
