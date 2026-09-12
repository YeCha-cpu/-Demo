// Copyright Epic Games, Inc. All Rights Reserved.

#include "AbilitySystem/Abilities/RPG_GA_LightAttack.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h"
#include "Animation/AnimMontage.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "TimerManager.h"

#include "AbilitySystem/Tasks/RPG_AbilityTask_WeaponTrace.h"
#include "Combat/RPG_AttackModuleData.h"
#include "Combat/RPG_AttackWindowPayload.h"
#include "Combat/RPG_CombatComponent.h"
#include "Core/RPG_GameplayTags.h"
#include "Core/RPG_LogChannels.h"

URPG_GA_LightAttack::URPG_GA_LightAttack()
{
	// 用资产标签标记这是"轻击"类能力。
	// 其他系统可以据此做查询（比如"正在攻击时禁止移动"这类规则），
	// 也方便在 GameplayDebugger 里辨认。
	SetAssetTags(FGameplayTagContainer(RPGTags::Ability_Attack_Light));
}

void URPG_GA_LightAttack::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	// ── 提交能力 ──
	// 应用 Cost / Cooldown GE。本项目的耐力消耗是手动调 ConsumeStamina 走的，
	// 保留 CommitAbility 是为了将来加"攻击冷却"时结构不用变。
	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		UE_LOG(LogRPG_Ability, Verbose, TEXT("[%s] 轻击提交失败（冷却中或被标签阻断）"), *GetName());
		FinishCombo(true);
		return;
	}

	URPG_CombatComponent* Combat = GetCombatComponent();
	URPG_AttackModuleData* Module = GetAttackModule();

	if (!Combat || !Module || Module->GetLightSegmentCount() == 0)
	{
		// 这条日志把三种不同的失败原因分开说 —— 它们的修法完全不同，
		// 混在一起报"无法攻击"会让人无从下手。
		UE_LOG(LogRPG_Ability, Warning, TEXT("[%s] 无法执行轻击：%s"),
			*GetName(),
			!Combat   ? TEXT("角色上没有 CombatComponent")
			          : (!Module ? TEXT("CombatComponent 没有配置攻击模组 DataAsset")
			                     : TEXT("攻击模组的轻击段列表为空")));

		FinishCombo(true);
		return;
	}

	// ══════════════════════════════════════════════════════════════════
	//  决定从第几段起手
	// ══════════════════════════════════════════════════════════════════
	// CombatComponent 的连段索引语义是"下一段该打的索引"：
	//   0 = 还没打过任何一段 → 打索引 0（起手式）
	//   2 = 已经打完第 2 段   → 打索引 2（第 3 段）
	int32 StartIndex = Combat->GetComboIndex();

	// 越界说明上一轮连段已经打到底了（比如第 5 段结束时衔接窗口又收到了输入），
	// 此时应该重新起手而不是报错。
	if (!Module->GetLightSegment(StartIndex))
	{
		UE_LOG(LogRPG_Combat, Verbose, TEXT("[%s] 连段索引 %d 越界，重新起手"), *GetName(), StartIndex);
		Combat->ResetCombo();
		StartIndex = 0;
	}

	CurrentSegmentIndex = StartIndex;
	CurrentDamageMultiplier = 1.f;
	bComboWindowOpen = false;

	Combat->SetComboIndex(CurrentSegmentIndex);

	// 事件监听每次激活只需建立一次，覆盖这一整轮连段的所有段
	BindGameplayEventListeners();

	StartSegment(CurrentSegmentIndex);
}

void URPG_GA_LightAttack::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	// 统一在这里清理，避免每条结束路径都要记得清一遍。
	// （Task 会随能力结束自动销毁，但显式 EndTask 能让行为更可预测。）
	if (TraceTask)
	{
		TraceTask->EndTask();
		TraceTask = nullptr;
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(SimulatedComboTimer);
	}

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

// ══════════════════════════════════════════════════════════════════════
//  事件监听
// ══════════════════════════════════════════════════════════════════════

void URPG_GA_LightAttack::BindGameplayEventListeners()
{
	// 每个事件单独建一个监听 Task，用确切的 Tag 过滤。
	//
	// 注意这里不能写成循环 + 变量传函数名：AddDynamic 宏内部会把函数名
	// 字符串化（#FuncName）用于反射查找，所以必须是**字面量**函数名。
	// 写成 5 条重复语句虽然啰嗦，但这是宏的硬性要求。
	//
	// OnlyTriggerOnce 保持默认的 false —— 一轮连段里每个事件都会触发多次
	// （5 段就有 5 组窗口开/关）。
	{
		UAbilityTask_WaitGameplayEvent* Task =
			UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(this, RPGTags::Event_Combat_AttackWindow_Open);
		Task->EventReceived.AddDynamic(this, &URPG_GA_LightAttack::OnAttackWindowOpen);
		Task->ReadyForActivation();
	}
	{
		UAbilityTask_WaitGameplayEvent* Task =
			UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(this, RPGTags::Event_Combat_AttackWindow_Close);
		Task->EventReceived.AddDynamic(this, &URPG_GA_LightAttack::OnAttackWindowClose);
		Task->ReadyForActivation();
	}
	{
		UAbilityTask_WaitGameplayEvent* Task =
			UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(this, RPGTags::Event_Combat_ComboWindow_Open);
		Task->EventReceived.AddDynamic(this, &URPG_GA_LightAttack::OnComboWindowOpen);
		Task->ReadyForActivation();
	}
	{
		UAbilityTask_WaitGameplayEvent* Task =
			UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(this, RPGTags::Event_Combat_ComboWindow_Close);
		Task->EventReceived.AddDynamic(this, &URPG_GA_LightAttack::OnComboWindowClose);
		Task->ReadyForActivation();
	}
	{
		UAbilityTask_WaitGameplayEvent* Task =
			UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(this, RPGTags::Event_Combat_AttackEnd);
		Task->EventReceived.AddDynamic(this, &URPG_GA_LightAttack::OnAttackEndEvent);
		Task->ReadyForActivation();
	}
}

// ══════════════════════════════════════════════════════════════════════
//  段落推进
// ══════════════════════════════════════════════════════════════════════

void URPG_GA_LightAttack::StartSegment(int32 Index)
{
	URPG_AttackModuleData* Module = GetAttackModule();
	if (!Module)
	{
		FinishCombo(true);
		return;
	}

	const FRPG_AttackSegment* Segment = Module->GetLightSegment(Index);
	if (!Segment)
	{
		// 没有这一段（比如只有 3 段的武器打到了第 4 段）
		FinishCombo(false);
		return;
	}

	CurrentDamageMultiplier = Segment->DamageMultiplier;
	bComboWindowOpen = false;

	UE_LOG(LogRPG_Combat, Log, TEXT("[%s] 轻击第 %d 段（倍率 %.2f，耐力 %.1f）"),
		*GetNameSafe(GetAvatarActorFromActorInfo()),
		Index + 1, Segment->DamageMultiplier, Segment->StaminaCost);

	// 耐力消耗跟着"段的开始"走，而不是跟着伤害窗口 ——
	// 这样即使玩家在挥到一半时被打断，耐力也已经扣了，
	// 避免"打断了就能白嫖一次攻击"的漏洞。
	ConsumeStamina(Segment->StaminaCost);

	// ── 播蒙太奇 ──
	UAbilityTask_PlayMontageAndWait* MontageTask =
		PlayMontageOrSkip(Segment->Montage, FName(*FString::Printf(TEXT("LightAttack_%d"), Index)));

	if (MontageTask)
	{
		MontageTask->OnCompleted.AddDynamic(this, &URPG_GA_LightAttack::OnMontageCompleted);
		MontageTask->OnInterrupted.AddDynamic(this, &URPG_GA_LightAttack::OnMontageInterrupted);
		MontageTask->ReadyForActivation();
	}
	else
	{
		// 没有蒙太奇 → 走模拟时序，让连段与伤害链路在动画就绪前就能验证
		StartSimulatedSegment();
	}
}

bool URPG_GA_LightAttack::TryStartNextSegment()
{
	URPG_CombatComponent* Combat = GetCombatComponent();
	URPG_AttackModuleData* Module = GetAttackModule();

	if (!Combat || !Module)
	{
		return false;
	}

	FGameplayTag BufferedTag;
	if (!Combat->ConsumeInputTag(BufferedTag))
	{
		return false;
	}

	// 缓存里可能存的是**别的意图**（玩家在攻击后摇里按了闪避）。
	// 那种情况不该由轻击 GA 处理 —— 放回缓存让对应的能力去取。
	if (BufferedTag != RPGTags::Input_Attack_Light)
	{
		Combat->PushInputTag(BufferedTag);
		return false;
	}

	const int32 NextIndex = CurrentSegmentIndex + 1;

	if (!Module->GetLightSegment(NextIndex))
	{
		UE_LOG(LogRPG_Combat, Verbose,
			TEXT("[%s] 已是最后一段（共 %d 段），连段到此为止"),
			*GetName(), Module->GetLightSegmentCount());

		return false;
	}

	// ── 续段 ──
	CurrentSegmentIndex = NextIndex;
	Combat->SetComboIndex(NextIndex);

	StartSegment(NextIndex);

	return true;
}

void URPG_GA_LightAttack::FinishCombo(bool bWasCancelled)
{
	// 连段索引清零 —— 下次攻击从起手式开始。
	// 放在这里而不是"衔接窗口关闭时"是因为：可能有多条结束路径
	// （AttackEnd / 蒙太奇完成 / 被打断），统一在收尾处清最可靠。
	if (URPG_CombatComponent* Combat = GetCombatComponent())
	{
		Combat->ResetCombo();
	}

	if (TraceTask)
	{
		TraceTask->EndTask();
		TraceTask = nullptr;
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(SimulatedComboTimer);
	}

	// bReplicateEndAbility = true（让服务器/客户端都知道能力结束了）
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo,
		/*bReplicateEndAbility*/ true, bWasCancelled);
}

// ══════════════════════════════════════════════════════════════════════
//  模拟时序（没有蒙太奇时使用）
// ══════════════════════════════════════════════════════════════════════

void URPG_GA_LightAttack::StartSimulatedSegment()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		FinishCombo(false);
		return;
	}

	UE_LOG(LogRPG_Combat, Verbose,
		TEXT("[%s] 第 %d 段没有蒙太奇，使用模拟时序（%.2f 秒）"),
		*GetName(), CurrentSegmentIndex + 1, SimulatedSegmentDuration);

	// 模拟一次命中（相当于伤害窗口瞬间开合）
	PerformSimulatedHit();

	// 开启衔接窗口，持续一小段时间后关闭
	bComboWindowOpen = true;

	World->GetTimerManager().SetTimer(
		SimulatedComboTimer,
		this,
		&URPG_GA_LightAttack::OnSimulatedWindowClosed,
		FMath::Min(SimulatedComboWindowDuration, SimulatedSegmentDuration),
		false);
}

void URPG_GA_LightAttack::OnSimulatedWindowClosed()
{
	bComboWindowOpen = false;

	// 模拟模式下没有"后摇结束"的动画事件，所以窗口一关就收尾
	if (!TryStartNextSegment())
	{
		FinishCombo(false);
	}
}

void URPG_GA_LightAttack::PerformSimulatedHit()
{
	AActor* Avatar = GetAvatarActorFromActorInfo();
	UWorld* World = GetWorld();

	if (!Avatar || !World)
	{
		return;
	}

	// 在角色身前 150cm、半径 80cm 的范围内找目标。
	// 这只是为了验证伤害链路能跑通，不是真实的手感设计 ——
	// 动画接上后由 WeaponTrace 接管。
	const FVector Origin = Avatar->GetActorLocation() + Avatar->GetActorForwardVector() * 150.f;

	TArray<FOverlapResult> Overlaps;

	FCollisionQueryParams Params(TEXT("RPGSimulatedHit"), /*bTraceComplex*/ false, Avatar);
	const FCollisionShape Shape = FCollisionShape::MakeSphere(80.f);

	World->OverlapMultiByChannel(Overlaps, Origin, FQuat::Identity, ECC_Pawn, Shape, Params);

	// 去重：Overlap 可能对同一个 Actor 返回多个结果（多个碰撞体）
	TSet<AActor*> UniqueTargets;

	for (const FOverlapResult& Overlap : Overlaps)
	{
		AActor* HitActor = Overlap.GetActor();
		if (HitActor && !UniqueTargets.Contains(HitActor))
		{
			UniqueTargets.Add(HitActor);
			ApplyDamageToTarget(HitActor, CurrentDamageMultiplier, nullptr);
		}
	}

	if (UniqueTargets.Num() == 0)
	{
		UE_LOG(LogRPG_Combat, Verbose, TEXT("[%s] 模拟命中：身前没有目标"), *GetName());
	}
}

// ══════════════════════════════════════════════════════════════════════
//  事件回调
// ══════════════════════════════════════════════════════════════════════

void URPG_GA_LightAttack::OnAttackWindowOpen(FGameplayEventData Payload)
{
	URPG_AttackModuleData* Module = GetAttackModule();
	if (!Module)
	{
		return;
	}

	// ── 默认用模组的检测配置 ──
	ERPG_TraceSource Source = Module->TraceSource;
	float Radius = Module->TraceRadius;
	FName SocketStart = Module->LeftHandSocket;
	FName SocketEnd = Module->RightHandSocket;

	// ── Notify 上如果配了覆盖，就用覆盖值 ──
	if (const URPG_AttackWindowPayload* WindowPayload =
			Cast<URPG_AttackWindowPayload>(Payload.OptionalObject.Get()))
	{
		if (WindowPayload->bOverrideTrace)
		{
			Source = WindowPayload->TraceSource;
			Radius = WindowPayload->TraceRadius;
			SocketStart = WindowPayload->SocketStart;
			SocketEnd = WindowPayload->SocketEnd;
		}

		UE_LOG(LogRPG_Combat, VeryVerbose, TEXT("[%s] 判定窗口开启（%s）"),
			*GetName(),
			WindowPayload->AttackTag.IsValid() ? *WindowPayload->AttackTag.ToString() : TEXT("未标标签"));
	}

	// 刀锋检测用武器上的 Socket，不用手部
	if (Source == ERPG_TraceSource::WeaponBlade)
	{
		SocketStart = Module->BladeStartSocket;
		SocketEnd = Module->BladeEndSocket;
	}

	// 远程模组不走轨迹检测 —— 发射物由 GA 生成后自己处理碰撞
	if (Source == ERPG_TraceSource::Projectile)
	{
		UE_LOG(LogRPG_Combat, Verbose,
			TEXT("[%s] 远程模组的发射物逻辑尚未实现（阶段 6）"), *GetName());
		return;
	}

	// 上一段的检测任务可能还在跑（比如蒙太奇重叠），先停掉
	if (TraceTask)
	{
		TraceTask->EndTask();
		TraceTask = nullptr;
	}

	TraceTask = URPG_AbilityTask_WeaponTrace::CreateWeaponTraceTask(
		this, Source, Radius, SocketStart, SocketEnd);

	if (TraceTask)
	{
		TraceTask->OnHit.AddDynamic(this, &URPG_GA_LightAttack::OnWeaponTraceHit);
		TraceTask->ReadyForActivation();
	}
}

void URPG_GA_LightAttack::OnAttackWindowClose(FGameplayEventData Payload)
{
	if (TraceTask)
	{
		TraceTask->EndTask();
		TraceTask = nullptr;
	}
}

void URPG_GA_LightAttack::OnComboWindowOpen(FGameplayEventData Payload)
{
	bComboWindowOpen = true;

	// 窗口一开就检查缓存 —— 玩家可能早在动画前半段就按了下一段，
	// 那时输入已经在缓存里等着。等窗口关闭才检查会让操作感觉迟滞。
	if (!TryStartNextSegment())
	{
		UE_LOG(LogRPG_Combat, VeryVerbose, TEXT("[%s] 衔接窗口开启，缓存暂时为空"), *GetName());
	}
}

void URPG_GA_LightAttack::OnComboWindowClose(FGameplayEventData Payload)
{
	bComboWindowOpen = false;

	if (TryStartNextSegment())
	{
		return;
	}

	// ── 没有可续的段 ──
	// 这里**不结束能力** —— 动画还在播后摇，玩家可能还要做别的操作。
	// 只把连段索引清掉：这样等后摇结束再来一次攻击会从起手式开始，
	// 而不是莫名其妙地从第 4 段接上。
	if (URPG_CombatComponent* Combat = GetCombatComponent())
	{
		Combat->ResetCombo();
	}

	UE_LOG(LogRPG_Combat, VeryVerbose, TEXT("[%s] 衔接窗口关闭，连段重置（等待后摇结束）"), *GetName());
}

void URPG_GA_LightAttack::OnAttackEndEvent(FGameplayEventData Payload)
{
	// 动画最后一帧的 Notify 广播出来的 —— 这是这一段真正的结束点
	FinishCombo(false);
}

void URPG_GA_LightAttack::OnMontageCompleted()
{
	// 蒙太奇自然播完。正常情况下 AttackEnd Notify 会先触发，
	// 走到这里说明蒙太奇上没配那个 Notify —— 作为兜底结束能力。
	UE_LOG(LogRPG_Combat, VeryVerbose,
		TEXT("[%s] 蒙太奇播放完成（若没配 AttackEnd 通知，这里是兜底路径）"), *GetName());

	FinishCombo(false);
}

void URPG_GA_LightAttack::OnMontageInterrupted()
{
	UE_LOG(LogRPG_Combat, Verbose, TEXT("[%s] 蒙太奇被打断"), *GetName());

	FinishCombo(true);
}

void URPG_GA_LightAttack::OnWeaponTraceHit(const TArray<FHitResult>& Hits)
{
	for (const FHitResult& Hit : Hits)
	{
		ApplyDamageToTarget(Hit.GetActor(), CurrentDamageMultiplier, &Hit);
	}
}
