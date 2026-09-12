// Copyright Epic Games, Inc. All Rights Reserved.

#include "Character/RPG_BaseCharacter.h"

#include "AbilitySystemComponent.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/SpringArmComponent.h"

#include "AbilitySystem/RPG_AttributeSet.h"
#include "Combat/RPG_CombatComponent.h"
#include "Core/RPG_GameplayTags.h"
#include "Core/RPG_LogChannels.h"

ARPG_BaseCharacter::ARPG_BaseCharacter()
{
	// ══════════════════════════════════════════════════════════════════
	//  相机
	// ══════════════════════════════════════════════════════════════════
	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->TargetArmLength = CameraBoomLength;
	CameraBoom->SetRelativeRotation(CameraBoomOffset);
	// 让弹簧臂跟随控制器的旋转 —— 这是"鼠标控制视角"的关键。
	// 关掉它的话，相机就只会跟着角色身体转，变成固定追尾视角。
	CameraBoom->bUsePawnControlRotation = true;
	CameraBoom->bEnableCameraLag = true;
	CameraBoom->CameraLagSpeed = CameraLagSpeed;

	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	// 相机自己不跟随控制器旋转——它已经挂在会旋转的弹簧臂末端了，
	// 再转一次会变成双重旋转。
	FollowCamera->bUsePawnControlRotation = false;

	// ══════════════════════════════════════════════════════════════════
	//  朝向策略
	// ══════════════════════════════════════════════════════════════════
	// 身体不跟随控制器旋转（否则鼠标一动人物原地转圈），
	// 而是由"移动方向"驱动 —— 这才是第三人称动作游戏的手感。
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	UCharacterMovementComponent* Movement = GetCharacterMovement();
	Movement->bOrientRotationToMovement = true;
	Movement->RotationRate = FRotator(0.f, RotationRateYaw, 0.f);
	Movement->MaxWalkSpeed = WalkSpeed;
	Movement->MaxWalkSpeedCrouched = CrouchSpeed;
	Movement->JumpZVelocity = 600.f;
	Movement->AirControl = 0.35f;

	// 没有这一句，Crouch() 会静默失败（角色蹲不下去，也不报错，很难查）
	Movement->GetNavAgentPropertiesRef().bCanCrouch = true;

	// ── 战斗组件 ──
	// 敌我共用，且不依赖 ASC，所以放在基类而不是两个子类里各建一份。
	CombatComponent = CreateDefaultSubobject<URPG_CombatComponent>(TEXT("CombatComponent"));
}

void ARPG_BaseCharacter::BeginPlay()
{
	Super::BeginPlay();

	// ══════════════════════════════════════════════════════════════════
	//  把蓝图里配置的参数同步到组件上
	// ══════════════════════════════════════════════════════════════════
	// 为什么构造函数里设了还要在这里再设一遍？
	// 因为构造函数只在 CDO（类默认对象）创建时执行一次。你在蓝图子类里把
	// CameraBoomLength 从 400 改成 600，**不会**重新执行构造函数——
	// 那时组件上的值仍然是 400。必须在运行时同步一次，蓝图配置才真的生效。
	//
	// 这是 UE 里非常经典的一个坑，凡是"UPROPERTY 的值影响组件属性"都要这么处理。
	if (CameraBoom)
	{
		CameraBoom->TargetArmLength = CameraBoomLength;
		CameraBoom->SetRelativeRotation(CameraBoomOffset);
		CameraBoom->bEnableCameraLag = CameraLagSpeed > 0.f;
		CameraBoom->CameraLagSpeed = FMath::Max(CameraLagSpeed, 1.f);
	}

	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->RotationRate = FRotator(0.f, RotationRateYaw, 0.f);
		Movement->MaxWalkSpeed = WalkSpeed;
		Movement->MaxWalkSpeedCrouched = CrouchSpeed;
	}
}

// ══════════════════════════════════════════════════════════════════════
//  接口实现
// ══════════════════════════════════════════════════════════════════════

UAbilitySystemComponent* ARPG_BaseCharacter::GetAbilitySystemComponent() const
{
	return GetASCInternal();
}

URPG_AttributeSet* ARPG_BaseCharacter::GetRPGAttributeSet() const
{
	const UAbilitySystemComponent* ASC = GetASCInternal();
	if (!ASC)
	{
		return nullptr;
	}

	// GetSet 返回 const 指针——因为它只是"找到并返回"，不承诺可写。
	// 但属性集本身是需要被 GE 修改的（GE 内部持有非 const 指针），
	// 所以这里去掉 const 限定是安全且必要的。
	return const_cast<URPG_AttributeSet*>(ASC->GetSet<URPG_AttributeSet>());
}

bool ARPG_BaseCharacter::IsAlive() const
{
	const UAbilitySystemComponent* ASC = GetASCInternal();

	if (!ASC)
	{
		// ASC 尚未初始化完（BeginPlay 之前会被调用到）——按"存活"处理。
		// 如果这里返回 false，刚生成的角色会在第一帧被 AI 当成尸体忽略掉。
		return true;
	}

	return !ASC->HasMatchingGameplayTag(RPGTags::State_Dead);
}

// ══════════════════════════════════════════════════════════════════════
//  角色动作
// ══════════════════════════════════════════════════════════════════════

void ARPG_BaseCharacter::Move(const FInputActionValue& Value)
{
	const FVector2D MovementVector = Value.Get<FVector2D>();

	if (!Controller || MovementVector.IsNearlyZero()) return;

	// 用**控制器的** Yaw 而不是角色自身的 Yaw 作为参考系。
	// 这样"按 W"永远是"朝屏幕前方走"，而不是"朝角色面朝方向走"——
	// 后者在角色背对镜头时会变成往镜头方向跑，手感很差。
	const FRotator YawRotation(0.f, Controller->GetControlRotation().Yaw, 0.f);
	const FVector ForwardDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
	const FVector RightDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);

	// 增强输入的 2D 轴约定：X = 左右，Y = 前后。
	// 注意这里和第二行是交叉的（前后用 Y、左右用 X），别写反。
	AddMovementInput(ForwardDirection, MovementVector.Y);
	AddMovementInput(RightDirection, MovementVector.X);
}

void ARPG_BaseCharacter::Look(const FInputActionValue& Value)
{
	const FVector2D LookAxisVector = Value.Get<FVector2D>();

	if (!Controller) return;

	AddControllerYawInput(LookAxisVector.X);
	AddControllerPitchInput(LookAxisVector.Y);
}

void ARPG_BaseCharacter::StartSprint()
{
	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->MaxWalkSpeed = SprintSpeed;
	}
}

void ARPG_BaseCharacter::StopSprint()
{
	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		// 回落到"当前姿态对应"的速度，而不是无条件回 WalkSpeed——
		// 否则蹲着跑完松开按键，角色会突然站起来以行走速度移动。
		Movement->MaxWalkSpeed = bIsCrouched ? CrouchSpeed : WalkSpeed;
	}
}

void ARPG_BaseCharacter::ToggleCrouch()
{
	if (bIsCrouched) UnCrouch();
	else  Crouch();
}
