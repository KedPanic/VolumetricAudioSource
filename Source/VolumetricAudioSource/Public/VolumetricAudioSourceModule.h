/**
* The MIT License (MIT)
* Copyright (c) 2021 Cedric Liaudet
*/
#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class FVolumetricAudioSourceModule : public IModuleInterface
{
public:

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
