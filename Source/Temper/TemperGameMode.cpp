#include "TemperGameMode.h"

#include "BastionMech.h"
#include "Engine/World.h"
#include "Misc/CommandLine.h"
#include "Temper.h"
#include "TemperArena.h"
#include "TemperPlayerController.h"
#include "TemperScenarioRunner.h"

ATemperGameMode::ATemperGameMode()
{
	DefaultPawnClass = ABastionMech::StaticClass();
	PlayerControllerClass = ATemperPlayerController::StaticClass();
}

void ATemperGameMode::StartPlay()
{
	Arena = GetWorld()->SpawnActor<ATemperArena>(ATemperArena::StaticClass(), FTransform::Identity);
	Super::StartPlay();

	FString Scenario;
	if (FParse::Value(FCommandLine::Get(), TEXT("TemperScenario="), Scenario))
	{
		Runner = GetWorld()->SpawnActor<ATemperScenarioRunner>(ATemperScenarioRunner::StaticClass(), FTransform::Identity);
		Runner->Start(Scenario);
	}
}

void ATemperGameMode::RestartPlayer(AController* NewPlayer)
{
	RestartPlayerAtTransform(NewPlayer, ATemperArena::PlayerSpawn());
}
