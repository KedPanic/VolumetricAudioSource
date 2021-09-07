/**
* The MIT License (MIT)
* Copyright (c) 2021 Cedric Liaudet
*/
#pragma once

#include "CoreMinimal.h"
#include "VolumeAudioSourceSettings.generated.h"

/**
 * Settings to control the behavior of the Volumetric Audio Source.
 */
UCLASS(Config = Game)
class VOLUMETRICAUDIOSOURCE_API UVolumeAudioSourceSettings : public UDeveloperSettings
{
	GENERATED_BODY()
	
public:
	/** 
	 * Default size when creating a new Volumetric.
	 */
	UPROPERTY(Config, EditAnywhere)
	float DefaultSize = 400.0f;

	/**
	 * The sound played by the volumetric will stop when the listener is out of range + this offset. 
	 */
	UPROPERTY(Config, EditAnywhere)
	float StopPlayingSoundOffset = 500.0f;
	
	/**
	 * Control the tick interval of the volumetric.
	 */
	UPROPERTY(Config, EditAnywhere, meta=(DisplayName="Tick Curve", XAxisName="Distance", YAxisName="Tick Intervale"))
	FRuntimeFloatCurve DistanceToTickInterval;
};
