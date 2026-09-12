// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Abilities/RPG_GameplayAbilityBase.h"
#include "RPG_GA_Heal.generated.h"

/**
 * 治疗。
 *
 * 走 GE_Heal（Instant + SetByCaller(Data.Heal.Amount)），
 * 治疗量由 HealAmount 决定，可以在运行时用 SetHealAmount 覆盖 ——
 * 这样同一个 GA 蓝图能服务不同强度的治疗（比如小药和大药）。
 *
 * 属性集侧不需要任何改动：Health 增加时会自动被 PreAttributeChange 夹到
 * MaxHealth 以内，不会出现"治疗溢出成超血"。
 */
UCLASS()
class RPG_API URPG_GA_Heal : public URPG_GameplayAbilityBase
{
	GENERATED_BODY()

public:
	URPG_GA_Heal();

	/** 覆盖本次治疗量（供法术系统在激活前调用） */
	UFUNCTION(BlueprintCallable, Category = "RPG|Heal")
	void SetHealAmount(float NewAmount) { HealAmount = FMath::Max(NewAmount, 0.f); }

protected:
	virtual void ActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

protected:
	/** 治疗量 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|Heal", meta = (ClampMin = "0.0"))
	float HealAmount = 30.f;

	/** 治疗 GE（Instant，Modifier 读 SetByCaller(Data.Heal.Amount)） */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|Heal")
	TSubclassOf<UGameplayEffect> HealEffectClass;
};
