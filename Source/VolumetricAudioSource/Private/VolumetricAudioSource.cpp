/**
* The MIT License (MIT)
* Copyright (c) 2021 Cedric Liaudet
*/
#include "VolumetricAudioSource.h"

#include "AudioDevice.h"
#include "DrawDebugHelpers.h"
#include "Components/AudioComponent.h"
#include "Components/SplineComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundCue.h"
#include "Stats/Stats2.h"
#include "VolumeAudioSourceSettings.h"

// Debug commands.
static TAutoConsoleVariable<int32> CVarVolumetricAudioSourceVisualize(
	TEXT("au.Volumetric.Visualize"),
	0,
	TEXT("Enable/Disable visualization of volumetric audio source. \n")
	TEXT("0: Not Enabled, 1: Enabled"), 
	ECVF_Default);

#if WITH_EDITOR
static TAutoConsoleVariable<int32> CVarVolumetricAudioSourcePreview(
	TEXT("au.Volumetric.Preview"),
	0,
	TEXT("Enable/Disable preview of volumetric audio source in edit mode. \n")
	TEXT("0: Not Enabled, 1: Enabled"), 
	ECVF_Default);
#endif

// Stats
DECLARE_CYCLE_STAT(TEXT("Volumetric Audio Source"), STAT_VolumetricAudioSourceTick, STATGROUP_Audio);

AVolumetricAudioSource::AVolumetricAudioSource()
{
	PrimaryActorTick.bCanEverTick = true;

	const UVolumeAudioSourceSettings* PluginSettings = GetDefault<UVolumeAudioSourceSettings>();
	const float HalfSize = PluginSettings->DefaultSize / 2;
	
	// Add the spline component and create a defaulted size volume.
	Spline = CreateDefaultSubobject<USplineComponent>(TEXT("Spline"));
	Spline->SetClosedLoop(true);
	Spline->ClearSplinePoints();
	Spline->AddSplinePoint({HalfSize, HalfSize, 0.0f}, ESplineCoordinateSpace::Local, false);
	Spline->AddSplinePoint({HalfSize, -HalfSize, 0.0f}, ESplineCoordinateSpace::Local, false);
	Spline->AddSplinePoint({-HalfSize, -HalfSize, 0.0f}, ESplineCoordinateSpace::Local, false);
	Spline->AddSplinePoint({-HalfSize, HalfSize, 0.0f}, ESplineCoordinateSpace::Local, true);
	for(int32 Idx = 0; Idx < 4; ++Idx)
	{
		Spline->SetSplinePointType(Idx, ESplinePointType::Linear);
	}

	RootComponent = Spline;

#if WITH_EDITOR
	if(IsTemplate())
	{
		// Force the spline to be in linear curve mode.
		FCoreUObjectDelegates::OnObjectPropertyChanged.AddLambda([](UObject* Object, FPropertyChangedEvent& PropertyChangedEvent)
		{
			if(const AVolumetricAudioSource* Volumetric = Cast<AVolumetricAudioSource>(Object->GetOuter()))
			{
				if(Volumetric->Spline != nullptr)
				{
					for(FInterpCurvePoint<FVector>& Point : Volumetric->Spline->SplineCurves.Position.Points)
					{
						Point.InterpMode = EInterpCurveMode::CIM_Linear;
					}
				}
			}
		});
	}
#endif
}

void AVolumetricAudioSource::BeginPlay()
{
	Super::BeginPlay();

	// Enable tick if the data is valid.
	if(SoundLoop != nullptr || RandomSFX.Num() > 0)
	{
		SetActorTickEnabled(true);
	}
	else
	{
		UE_LOG(LogAudio, Error, TEXT("No Sound Loop and Random SFX on Volumetric Audio Source %s."), *GetName());

		// Keep ticking to be able to debug if we are not in shipping.		
#if UE_BUILD_SHIPPING
		SetActorTickEnabled(false);
#endif
	}
}

bool AVolumetricAudioSource::IsInside(const FVector& Location, FVector& OutClosestLocation) const
{
	const FVector LocalLocation = Spline->GetComponentTransform().InverseTransformPosition(Location);
	
	const TArray<FInterpCurvePoint<FVector>>& Points = Spline->SplineCurves.Position.Points;

	// Cache the closest location and the normal per line.
	// The normal will be used to detect if the listener is inside the spline.
	TArray<TTuple<const FVector, const FVector>, TInlineAllocator<16>> CachedLocationAndNormal;
	int32 ClosestLocationIndex = 0;
	{
		float ClosestLocationDistance = MAX_FLT;
		const int32 NbPoints = Points.Num();
		for (int32 Idx = 0; Idx < NbPoints; ++Idx)
		{
			FVector ClosestLocation = FVector::ZeroVector;
			FVector StartPoint = Points[Idx].OutVal;
			FVector EndPoint = Points[(Idx + 1) % NbPoints].OutVal;

			// Copy/Past of FMath::ClosestPointOnSegment
			const FVector Segment = EndPoint - StartPoint;
			const FVector VectToPoint = LocalLocation - StartPoint;

			// See if closest location is before StartPoint
			const float Dot1 = VectToPoint | Segment;
			if (Dot1 <= 0)
			{
				ClosestLocation = StartPoint;

				// The normal of the line of the previous point and end point will be used
				// to detect if the point is inside the spline.
				const FVector PreviousPoint = Points[Idx - 1 >= 0 ? Idx - 1 : NbPoints - 1].OutVal;
				const FVector Normal = ((StartPoint - PreviousPoint).GetSafeNormal() + (StartPoint - EndPoint).GetSafeNormal()) / 2.0f;
				CachedLocationAndNormal.Add(MakeTuple(ClosestLocation, Normal));
			}
			else
			{
				// See if closest location is beyond EndPoint
				const float Dot2 = Segment | Segment;
				if (Dot2 <= Dot1)
				{
					ClosestLocation = EndPoint;

					// The normal of the line of the start point and the next point will be used
					// to detect if the point is inside the spline.
					const FVector NextPoint = Points[Idx + 2 < NbPoints ? Idx + 2 : 0].OutVal;
					const FVector Normal = ((EndPoint - StartPoint).GetSafeNormal() + (EndPoint - NextPoint).GetSafeNormal()) / 2.0f;
					CachedLocationAndNormal.Add(MakeTuple(ClosestLocation, Normal));
				}
				else
				{
					// Closest location is within segment
					ClosestLocation = StartPoint + Segment * (Dot1 / Dot2);

					// Compute the normal of the line.
					const FVector Normal = FVector::CrossProduct((StartPoint - EndPoint).GetSafeNormal(), FVector::UpVector);
					CachedLocationAndNormal.Add(MakeTuple(ClosestLocation, Normal));
				}
			}

			// Check if the closest location is nearest to the given location than the previous one.
			const float Distance = FVector::DistSquared(ClosestLocation, LocalLocation);
			if (Distance < ClosestLocationDistance)
			{
				ClosestLocationDistance = Distance;
				ClosestLocationIndex = Idx;
			}
		}
	}

	OutClosestLocation = Spline->GetComponentTransform().TransformPosition(CachedLocationAndNormal[ClosestLocationIndex].Key);
	
	// Check if the location is too high or too low.
	const FBox Bounds = Spline->Bounds.GetBox();

	// we snap the closest location on the top or bottom.
	// note: it doesn't work well with a spline with different heights.
	bool bOutside = false;
	if(Location.Z < Bounds.Min.Z)
	{
		OutClosestLocation.Z = Bounds.Min.Z;
		bOutside = true;
	}
	else if(Location.Z > Bounds.Max.Z + MaxHeight)
	{
		OutClosestLocation.Z = Bounds.Max.Z + MaxHeight;
		bOutside = true;
	}
	else
	{
		OutClosestLocation.Z = Location.Z;
	}

	const FVector DirectionToClosestLocation = Location - OutClosestLocation;
	if(FVector::DotProduct(DirectionToClosestLocation, CachedLocationAndNormal[ClosestLocationIndex].Value) < 0.0f)
	{
		OutClosestLocation.X = Location.X;
		OutClosestLocation.Y = Location.Y;
		return !bOutside;
	}

	return false;
}

void AVolumetricAudioSource::TickActor(float DeltaTime, enum ELevelTick TickType, FActorTickFunction& ThisTickFunction)
{
	FVector ClosestLocation = FVector::ZeroVector;
	
	{
		SCOPE_CYCLE_COUNTER( STAT_VolumetricAudioSourceTick );
	
		Super::TickActor(DeltaTime, TickType, ThisTickFunction);

#if WITH_EDITOR
		// Draw the shape in edit mode.
		if(TickType == ELevelTick::LEVELTICK_ViewportsOnly)
		{
			if(CVarVolumetricAudioSourcePreview.GetValueOnGameThread() == 0)
			{
				const FVector SplineCenter = Spline->Bounds.GetBox().GetCenter();

				// Move the audio component in the center.
				// It is easier to understand the audio component belong to the volumetric when editing.
				// Also it is more clear to see the relation between the random SFX box and the volumetric. 
				if(AudioComponent)
				{
					AudioComponent->SetWorldLocation(SplineCenter);

					// Stop the loop if we were previewing.
					if(AudioComponent->IsPlaying())
					{
						AudioComponent->Stop();
					}
				}

				Draw(DeltaTime, SplineCenter);

				return;
			}
		}
#endif

		FVector AudioListenerLocation = FVector::ZeroVector;
		if(const UWorld* World = GetWorld())
		{
			if(World->GetNumPlayerControllers() > 0)
			{
				// Find the first local player controller.
				for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
				{
					APlayerController* PlayerController = It->Get();
					if (PlayerController && PlayerController->IsLocalController())
					{
						FVector FrontDir;
						FVector RightDir;
						PlayerController->GetAudioListenerPosition(AudioListenerLocation, FrontDir, RightDir);
						break;
					}
				}
			}
			else
			{
				World->AudioDeviceHandle->GetListenerPosition(0, AudioListenerLocation, false);
			}
		}

		bool bShouldPlay = false;
		if(IsInside(AudioListenerLocation, ClosestLocation))
		{
			ClosestLocation = AudioListenerLocation;
			bShouldPlay = true;
		}
		else
		{
			const UVolumeAudioSourceSettings* PluginSettings = GetDefault<UVolumeAudioSourceSettings>();

			const float SquaredDistance = (AudioListenerLocation - ClosestLocation).SizeSquared();
			const float SquaredMaxDistance = FMath::Square(MaxDistance);
			if(SquaredDistance <= SquaredMaxDistance)
			{
				bShouldPlay = true;
			}
			else if(SquaredDistance > SquaredMaxDistance + PluginSettings->StopPlayingSoundOffset)
			{
				bShouldPlay = false;

				// Increase the tick interval.
				const FRichCurve* Curve = PluginSettings->DistanceToTickInterval.GetRichCurveConst();
				PrimaryActorTick.TickInterval = Curve->Eval(FMath::Sqrt(SquaredDistance - SquaredMaxDistance));
			}
		}

		if(bShouldPlay)
		{
			PrimaryActorTick.TickInterval = 0.0f;
			
			if(AudioComponent)
			{
				AudioComponent->SetWorldLocation(ClosestLocation);

				if(!AudioComponent->IsPlaying())
				{
					AudioComponent->Play();
				}
			}

			// Update the Random SFX.
			if(RandomSFX.Num() > 0)
			{
				CurrentDelay -= DeltaTime;
				if(CurrentDelay <= 0.0f)
				{
					CurrentDelay = FMath::RandRange(MinDelay, MaxDelay);
				
					USoundBase* SFX = RandomSFX[FMath::RandRange(0, RandomSFX.Num() - 1)];
					if(IsValid(SFX))
					{
						const FVector RandomSFXLocation = ClosestLocation + FVector(0.0f, 0.0f, Offset) + FMath::RandPointInBox(Box);
						UGameplayStatics::PlaySoundAtLocation(GetWorld(), SFX, RandomSFXLocation);

#if !UE_BUILD_SHIPPING
						DrawDebugRandomSFXs.Add({RandomSFXLocation, SFX->Duration});
#endif
					}
				}
			}
		}
		else
		{
			if(AudioComponent && AudioComponent->IsPlaying())
			{
				AudioComponent->Stop();
			}
		}
	}
	
#if !UE_BUILD_SHIPPING
	if(CVarVolumetricAudioSourceVisualize.GetValueOnGameThread())
	{
		// force ticking each frame
		PrimaryActorTick.TickInterval = 0.0f;
		Draw(DeltaTime, ClosestLocation);
	}
#endif
}

#if WITH_EDITOR
void AVolumetricAudioSource::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	auto UpdateMaxDistance = [&]()
	{
		MaxDistance = 0.0f;
		
		if(SoundLoop == nullptr)
		{
			for(const USoundBase* Sound : RandomSFX)
			{
				if(Sound != nullptr && Sound->MaxDistance > MaxDistance)
				{
					MaxDistance = Sound->MaxDistance;
				}
			}
		}
		else
		{
			MaxDistance = SoundLoop->MaxDistance;
		}
	};
	
	if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(AVolumetricAudioSource, SoundLoop))
	{
		if(SoundLoop == nullptr)
		{
			// Destroy the audio component.
			if(AudioComponent)
			{
				// It will be garbage collected.
				AudioComponent = nullptr;
				
				UpdateMaxDistance();
			}
		}
		else
		{
			if(AudioComponent == nullptr)
			{
				// Create a new audio component.
				AudioComponent = NewObject<UAudioComponent>(this, TEXT("AudioComponent"));
				AudioComponent->bAutoActivate = true;
                AudioComponent->bStopWhenOwnerDestroyed = true;
                AudioComponent->bShouldRemainActiveIfDropped = true;
                AudioComponent->Mobility = EComponentMobility::Movable;
                AudioComponent->SetupAttachment(Spline);
			}

			AudioComponent->SetSound(SoundLoop);
			
			UpdateMaxDistance();
		}
	}
	else if(PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(AVolumetricAudioSource, RandomSFX))
	{
		UpdateMaxDistance();
	}
}
#endif

#if !UE_BUILD_SHIPPING
void AVolumetricAudioSource::Draw(float DeltaTime, const FVector& ClosestLocation)
{
	auto DrawPlane = [&](const int32 PointA, const int32 PointB)
	{
		// Create the plane on the two given spline points.
		const FVector VertexA = Spline->GetLocationAtSplinePoint(PointA, ESplineCoordinateSpace::World);
		const FVector VertexB = Spline->GetLocationAtSplinePoint(PointB, ESplineCoordinateSpace::World);
		const TArray<FVector> Vertices =
		{
			VertexA,
			VertexA + FVector(0.0f, 0.0f, MaxHeight),
			VertexB,
			VertexB + FVector(0.0f, 0.0f, MaxHeight)
		};
		const TArray<int32> Indices = {0, 2, 1, 1, 2, 3};

		FColor TransparentColor = Color;
		TransparentColor.A = 128;
		DrawDebugMesh(GetWorld(), Vertices, Indices, TransparentColor);

		// Draw borders.
		DrawDebugLine(GetWorld(), Vertices[0], Vertices[1], Color, false, -1.0f, 0, 1.5f);
		DrawDebugLine(GetWorld(), Vertices[1], Vertices[3], Color, false, -1.0f, 0, 1.5f);
		DrawDebugLine(GetWorld(), Vertices[0], Vertices[2], Color, false, -1.0f, 0, 1.5f);
	};

	// Draw a plane on each line.
	const int32 NumPoints = Spline->GetNumberOfSplinePoints();
	for (int32 Idx = 0; Idx < NumPoints - 1; ++Idx)
	{
		DrawPlane(Idx, Idx + 1);
	}

	// Draw a plan on the line from the last point to the first point.
	DrawPlane(NumPoints - 1, 0);

	// Draw audio component.
	if(AudioComponent && AudioComponent->IsPlaying())
	{
		DrawDebugSphere(GetWorld(), AudioComponent->GetComponentLocation(), 50.0f, 12, Color);
	}

	// Draw the random SFXs.
	if(RandomSFX.Num() > 0)
	{
		DrawDebugBox(GetWorld(), ClosestLocation + FVector(0.0f, 0.0f, Offset), Box.GetExtent(), Color);

		for(int32 Idx = DrawDebugRandomSFXs.Num() - 1; Idx >= 0; Idx--)
		{
			DrawDebugRandomSFXs[Idx].Duration -= DeltaTime;
			if(DrawDebugRandomSFXs[Idx].Duration <= 0.0f)
			{
				DrawDebugRandomSFXs.RemoveAtSwap(Idx);
				continue;
			}

			DrawDebugSphere(GetWorld(), DrawDebugRandomSFXs[Idx].Location, 50.0f, 12, Color);
		}
	}
}
#endif
