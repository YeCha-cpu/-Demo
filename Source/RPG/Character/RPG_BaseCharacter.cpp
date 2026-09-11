// Fill out your copyright notice in the Description page of Project Settings.


#include "Character/RPG_BaseCharacter.h"

// Sets default values
ARPG_BaseCharacter::ARPG_BaseCharacter()
{
 	// Set this character to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

}

// Called when the game starts or when spawned
void ARPG_BaseCharacter::BeginPlay()
{
	Super::BeginPlay();
	
}

// Called every frame
void ARPG_BaseCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

}

// Called to bind functionality to input
void ARPG_BaseCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

}

