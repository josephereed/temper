#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "TemperGameMode.generated.h"

class ATemperArena;
class ATemperScenarioRunner;

/**
 * Builds the arena, spawns the Bastion at the west street, and (with -TemperScenario=...)
 * starts the scripted acceptance runner.
 */
UCLASS()
class TEMPER_API ATemperGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ATemperGameMode();
	virtual void StartPlay() override;
	virtual void RestartPlayer(AController* NewPlayer) override;

	ATemperArena* GetArena() const { return Arena; }

private:
	UPROPERTY(Transient) TObjectPtr<ATemperArena> Arena;
	UPROPERTY(Transient) TObjectPtr<ATemperScenarioRunner> Runner;
};
