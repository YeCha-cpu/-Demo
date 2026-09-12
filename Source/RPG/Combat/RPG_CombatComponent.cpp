// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/RPG_CombatComponent.h"

#include "Combat/RPG_AttackModuleData.h"
#include "Combat/RPG_InputBuffer.h"
#include "Core/RPG_LogChannels.h"

URPG_CombatComponent::URPG_CombatComponent()
{
	// 输入缓存需要每帧处理吗？不需要。
	// 它只在"按键"和"消耗"两个时刻被访问，惰性清理足够。
	PrimaryComponentTick.bCanEverTick = false;
}

void URPG_CombatComponent::BeginPlay()
{
	Super::BeginPlay();

	// 在这里创建 InputBuffer 而不是构造函数：
	// 构造函数可能对 CDO 执行多次，而 NewObject 出来的对象不该挂在 CDO 上。
	InputBuffer = NewObject<URPG_InputBuffer>(this, TEXT("InputBuffer"));

	if (InputBuffer)
	{
		InputBuffer->SetMode(BufferMode);
	}

	// 默认模组
	CurrentModule = DefaultModule;

	if (!CurrentModule)
	{
		UE_LOG(LogRPG_Combat, Warning,
			TEXT("[%s] CombatComponent 没有配置 DefaultModule —— "
			     "攻击会因为没有招式表而无法执行。请在角色蓝图里指定攻击模组 DataAsset"),
			*GetNameSafe(GetOwner()));
	}
	else
	{
		UE_LOG(LogRPG_Combat, Verbose, TEXT("[%s] 战斗组件就绪，攻击模组：%s"),
			*GetNameSafe(GetOwner()), *CurrentModule->GetName());
	}
}

// ══════════════════════════════════════════════════════════════════════
//  输入缓存
// ══════════════════════════════════════════════════════════════════════

void URPG_CombatComponent::PushInputTag(FGameplayTag InputTag)
{
	if (!InputBuffer)
	{
		// BeginPlay 之前调用会走到这里。静默返回而不是报错——
		// 早期调用是时序问题不是配置问题，报错反而会误导。
		return;
	}

	InputBuffer->Push(InputTag, InputLifeTime, GetNow());
}

bool URPG_CombatComponent::ConsumeInputTag(FGameplayTag& OutTag)
{
	return InputBuffer && InputBuffer->Consume(OutTag, GetNow());
}

int32 URPG_CombatComponent::GetBufferedInputCount() const
{
	return InputBuffer ? InputBuffer->Num() : 0;
}

void URPG_CombatComponent::ClearInputBuffer()
{
	if (InputBuffer)
	{
		InputBuffer->Clear();
	}
}

// ══════════════════════════════════════════════════════════════════════
//  连段状态
// ══════════════════════════════════════════════════════════════════════

void URPG_CombatComponent::SetComboIndex(int32 NewIndex)
{
	if (ComboIndex == NewIndex)
	{
		return;
	}

	UE_LOG(LogRPG_Combat, Verbose, TEXT("[%s] 连段索引 %d → %d"),
		*GetNameSafe(GetOwner()), ComboIndex, NewIndex);

	ComboIndex = NewIndex;
}

void URPG_CombatComponent::ResetCombo()
{
	if (ComboIndex != 0)
	{
		UE_LOG(LogRPG_Combat, Verbose, TEXT("[%s] 连段重置（原本在第 %d 段）"),
			*GetNameSafe(GetOwner()), ComboIndex);
	}

	ComboIndex = 0;
}

// ══════════════════════════════════════════════════════════════════════
//  攻击模组
// ══════════════════════════════════════════════════════════════════════

void URPG_CombatComponent::SetAttackModule(URPG_AttackModuleData* NewModule)
{
	// 传 nullptr 时回落到默认模组，而不是把当前模组清空 ——
	// "卸下武器"应该回到徒手，而不是变成没有招式表。
	//
	// 注意这里要 .Get()：TObjectPtr 和裸指针混在三元表达式里
	// 编译器无法推断公共类型（C2445），显式取出裸指针即可。
	URPG_AttackModuleData* Resolved = NewModule ? NewModule : DefaultModule.Get();
	CurrentModule = Resolved;

	UE_LOG(LogRPG_Combat, Log, TEXT("[%s] 切换攻击模组：%s"),
		*GetNameSafe(GetOwner()),
		CurrentModule ? *CurrentModule->GetName() : TEXT("(无)"));

	// 换模组时连段必须重置 —— 否则从徒手第 3 段切到武器会直接从武器第 4 段开始，
	// 这在设计上没有意义，而且会取到越界的招式配置。
	ResetCombo();
}

// ══════════════════════════════════════════════════════════════════════
//  调试
// ══════════════════════════════════════════════════════════════════════

FString URPG_CombatComponent::GetCombatDebugString() const
{
	const FString ModuleName = CurrentModule ? CurrentModule->GetName() : TEXT("(无模组)");

	return FString::Printf(TEXT("连段=%d | 缓存=%s | 模组=%s"),
		ComboIndex,
		InputBuffer ? *InputBuffer->ToDebugString() : TEXT("(未初始化)"),
		*ModuleName);
}

float URPG_CombatComponent::GetNow() const
{
	const UWorld* World = GetWorld();
	return World ? World->GetTimeSeconds() : 0.f;
}
