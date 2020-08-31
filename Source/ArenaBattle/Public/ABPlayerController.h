// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "ArenaBattle.h"
#include "GameFramework/PlayerController.h"
#include "ABPlayerController.generated.h"


/**
 * 
 */
UCLASS()
class ARENABATTLE_API AABPlayerController : public APlayerController
{
	GENERATED_BODY()
public:
	AABPlayerController();
	virtual void PostInitializeComponents() override; // 폰과 플레이어 컨트롤러(액터)가 생성되는 시점
	virtual void OnPossess(APawn * aPawn) override; // 빙의(Possess)를 진행하는 시점
	class UABHUDWidget* GetHUDWidget() const;
	void NPCKill(class AABCharacter* KilledNPC) const;
	void AddGameScore() const;

protected:
	virtual void BeginPlay() override;
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = UI)
	TSubclassOf<class UABHUDWidget> HUDWidgetClass;
	virtual void SetupInputComponent() override; // 단축키 바인딩

private:
	class UABHUDWidget* HUDWidget;

	UPROPERTY()
	class AABPlayerState* ABPlayerState;
	
	void OnGamePause(); // 게임 중지 
};
