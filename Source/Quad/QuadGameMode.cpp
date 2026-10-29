// Copyright Epic Games, Inc. All Rights Reserved.

#include "QuadGameMode.h"
#include "QuadCharacter.h"
#include "UObject/ConstructorHelpers.h"

AQuadGameMode::AQuadGameMode()
{
	// set default pawn class to our Blueprinted character
	static ConstructorHelpers::FClassFinder<APawn> PlayerPawnBPClass(TEXT("/Game/ThirdPerson/Blueprints/BP_ThirdPersonCharacter"));
	if (PlayerPawnBPClass.Class != NULL)
	{
		DefaultPawnClass = PlayerPawnBPClass.Class;
	}
}
