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

	// ══════════════════════════════════════════════════════════════════
	//  激活期间挂上同一个标签
	// ══════════════════════════════════════════════════════════════════
	// GA_HeavyAttack 靠查询这个标签判断"当前是否处于轻击连段中"，
	// 以此决定走切手技分支还是蓄力分支。
	// 它同时也是 CancelAbilitiesWithTag 的目标 —— 重击激活时会自动取消轻击。
	//
	// 为什么用标签而不是 CombatComponent 的连段索引？
	// 索引在起手那一瞬间是 0（表示"正在打第 1 段"），无法区分
	// "不在连段中"和"正在打第 1 段"。标签才精确表达"某个能力此刻正在激活"。
	ActivationOwnedTags.AddTag(RPGTags::Ability_Attack_Light);

	// ══════════════════════════════════════════════════════════════════
	//  挂上状态标签 State.Attacking（动画蓝图的读取对象）
	// ══════════════════════════════════════════════════════════════════
	// 前面那个 Ability.Attack.Light 是"能力身份"，是给 GAS 内部用的
	// （CancelAbilitiesWithTag 的目标、重击判断切手技的依据）。
	// 动画要问的是另一个问题："这个角色此刻在不在攻击"——
	// 那是 State 域的语义，不该拿 Ability 域的身份标签去顶替。
	//
	// 两者的生命周期**碰巧**一样（都是激活期间），但语义不同。
	// 混用会在将来出问题：比如以后加一个"攻击时也能激活"的 Buff 能力，
	// 它也带 Ability.* 标签，动画就会误判成在攻击。
	//
	// State.Attacking 是 State.Attack.Windup / Active / Recovery 的父标签，
	// 所以 HasMatchingGameplayTag(State.Attacking) 对这些子标签同样成立 ——
	// 将来把分阶段标签挂上去时，这里一行都不用改。
	ActivationOwnedTags.AddTag(RPGTags::State_Attacking);

	// ══════════════════════════════════════════════════════════════════
	//  激活时取消重击（包括蓄力中）
	// ══════════════════════════════════════════════════════════════════
	// GA_HeavyAttack 声明了"激活时取消轻击"（那是切手技的机制），
	// 但反过来没有 —— 结果就是蓄力期间按左键，两个 GA 会**并行跑**：
	// 各自播各自的蒙太奇、各扣各的耐力，表现上是一团乱。
	//
	// 补上这个方向，让"轻击打断蓄力"也成立。这是动作游戏里的常见规则：
	// 蓄力不是不可打断的霸体状态，玩家随时可以改用轻击。
	CancelAbilitiesWithTag.AddTag(RPGTags::Ability_Attack_Heavy);
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

	// ── 消耗掉"触发本次激活"的那条输入 ──
	// PlayerController 在按键时会把输入推进缓存（为了让连段衔接能取到），
	// 但既然这次按键已经成功触发了激活，这条输入就已经被消费掉了。
	// 不消耗的话它会残留在缓存里，导致第一次衔接窗口一开就自动多打一段。
	//
	// 用 if 而不是直接调用是为了明确表达"没有缓存条目也是正常的"——
	// 比如 AI 直接触发能力时就没有输入缓存。
	FGameplayTag TriggerInputTag;
	Combat->ConsumeInputTag(TriggerInputTag);

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
	// ⚠️ 连段索引必须在这里也重置一次。
	//
	// FinishCombo() 里虽然已经重置过，但**被外部取消**这条路径不走 FinishCombo ——
	// 比如切手技通过 CancelAbilitiesWithTag 取消轻击时，GAS 直接调 EndAbility。
	// 少了这一句，连段索引会残留，下次按左键会从中间某段开始
	//（症状是"打着打着突然从第 3 段起手"）。
	//
	// ResetCombo 内部有"值没变就跳过"的判断，所以重复调用是安全的。
	if (URPG_CombatComponent* Combat = GetCombatComponent())
	{
		Combat->ResetCombo();
	}

	if (TraceTask)
	{
		TraceTask->EndTask();
		TraceTask = nullptr;
	}

	// 完整拆掉蒙太奇（包含停掉它）。
	//
	// 走 FinishCombo 进来时，上面已经摘过回调并把指针清空了，
	// 所以这里是空操作；**被外部取消**那条路径才有实际作用 ——
	// 比如切手技通过 CancelAbilitiesWithTag 取消轻击时，GAS 直接调 EndAbility，
	// 不经过 FinishCombo。不清理的话，轻击的蒙太奇会继续播，
	// 它的判定窗口 Notify 会在重击的动画上再打一次伤害。
	StopCurrentSegmentMontage();

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
		UAbilityTask_WaitGameplayEvent* Task = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(this, RPGTags::Event_Combat_AttackWindow_Open);
		Task->EventReceived.AddDynamic(this, &URPG_GA_LightAttack::OnAttackWindowOpen);
		Task->ReadyForActivation();
	}
	{
		UAbilityTask_WaitGameplayEvent* Task = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(this, RPGTags::Event_Combat_AttackWindow_Close);
		Task->EventReceived.AddDynamic(this, &URPG_GA_LightAttack::OnAttackWindowClose);
		Task->ReadyForActivation();
	}
	{
		UAbilityTask_WaitGameplayEvent* Task = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(this, RPGTags::Event_Combat_ComboWindow_Open);
		Task->EventReceived.AddDynamic(this, &URPG_GA_LightAttack::OnComboWindowOpen);
		Task->ReadyForActivation();
	}
	{
		UAbilityTask_WaitGameplayEvent* Task = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(this, RPGTags::Event_Combat_ComboWindow_Close);
		Task->EventReceived.AddDynamic(this, &URPG_GA_LightAttack::OnComboWindowClose);
		Task->ReadyForActivation();
	}
	{
		UAbilityTask_WaitGameplayEvent* Task = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(this, RPGTags::Event_Combat_AttackEnd);
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

	// ══════════════════════════════════════════════════════════════════
	//  段时长诊断
	// ══════════════════════════════════════════════════════════════════
	// 连着打的时候，"动画本身很短"和"动画被下一段截断了"看起来一模一样。
	// 这里把上一段**实际播了多久 / 动画全长多少**打出来，一眼就能分辨：
	//   · 两个数接近 → 动画本身就这么短，换更长的动画资产
	//   · 实际远小于全长 → 是被衔接窗口接走的，把窗口往后挪
	const UWorld* World = GetWorld();
	const float Now = World ? World->GetTimeSeconds() : 0.f;

	// ⚠️ 只在"确实接在上一段后面"时才报 —— 也就是 Index > 0。
	//
	// 一次全新的连段总是从 Index = 0 开始，那时上一段是**上一轮连段**的事，
	// 中间的间隔可能有好几秒。把它当成"这一段播放了 3.6 秒"报出来会误导人
	// （曾经真的误导过一次排查）。
	//
	// Index > 0 还隐含保证了"上一段就是 Index-1"：连段只能一段一段往上接，
	// 不存在从第 1 段直接跳到第 3 段的路径。
	if (Index > 0 && PreviousSegmentMontageLength > 0.f)
	{
		const float Played = Now - PreviousSegmentStartTime;
		const bool bWasCut = Played < PreviousSegmentMontageLength * 0.9f;

		UE_LOG(LogRPG_Combat, Log,
			TEXT("[%s] ↳ 上一段（第 %d 段）实际播放 %.2f 秒 / 全长 %.2f 秒 —— %s"),
			*GetNameSafe(GetAvatarActorFromActorInfo()),
			Index, Played, PreviousSegmentMontageLength,
			bWasCut ? TEXT("被下一段接走（衔接窗口开太早）") : TEXT("自然播完"));
	}

	const float MontageLength = Segment->Montage ? Segment->Montage->GetPlayLength() : 0.f;
	PreviousSegmentStartTime = Now;
	PreviousSegmentMontageLength = MontageLength;

	UE_LOG(LogRPG_Combat, Log, TEXT("[%s] 轻击第 %d 段（倍率 %.2f，耐力 %.1f，动画 %s %.2f 秒）"),
		*GetNameSafe(GetAvatarActorFromActorInfo()),
		Index + 1, Segment->DamageMultiplier, Segment->StaminaCost,
		Segment->Montage ? *Segment->Montage->GetName() : TEXT("【未配置】"),
		MontageLength);

	// 蒙太奇的混合时间体检在 PlayMontageOrSkip() 里做 —— 那是所有蒙太奇的
	// 必经之路，检查放一处就够了，不用每个 GA 各抄一遍。

	// 耐力消耗跟着"段的开始"走，而不是跟着伤害窗口 ——
	// 这样即使玩家在挥到一半时被打断，耐力也已经扣了，
	// 避免"打断了就能白嫖一次攻击"的漏洞。
	ConsumeStamina(Segment->StaminaCost);

	// ══════════════════════════════════════════════════════════════════
	//  切段前先把上一段拆干净 ★
	// ══════════════════════════════════════════════════════════════════
	// 少了这一句，连段永远打不全 —— 详见 StopCurrentSegmentMontage()。
	// 放在播新蒙太奇之前，是因为清理动作本身会停掉"当前蒙太奇"，
	// 必须赶在新蒙太奇成为"当前"之前做。
	StopCurrentSegmentMontage();

	// ── 播蒙太奇 ──
	UAbilityTask_PlayMontageAndWait* MontageTask =
		PlayMontageOrSkip(Segment->Montage, FName(*FString::Printf(TEXT("LightAttack_%d"), Index)));

	if (MontageTask)
	{
		MontageTask->OnCompleted.AddDynamic(this, &URPG_GA_LightAttack::OnMontageCompleted);
		MontageTask->OnInterrupted.AddDynamic(this, &URPG_GA_LightAttack::OnMontageInterrupted);
		MontageTask->ReadyForActivation();

		// 记下"当前这一段"的任务与蒙太奇，下一次切段时要把它们拆干净。
		// 不记的话，它们会一直活到这轮连段结束，然后在各自的动画播完时
		// 广播 OnCompleted，把正在播的后续段落误判成"打完了"。
		CurrentSegmentMontageTask = MontageTask;
		CurrentSegmentMontage = Segment->Montage;
	}
	else
	{
		// 没有蒙太奇 → 走模拟时序，让连段与伤害链路在动画就绪前就能验证
		StartSimulatedSegment();
	}
}

// ══════════════════════════════════════════════════════════════════════
//  蒙太奇任务的清理 ★ 连段正确性的关键
// ══════════════════════════════════════════════════════════════════════

void URPG_GA_LightAttack::DetachCurrentSegmentMontageTask()
{
	if (!CurrentSegmentMontageTask)
	{
		return;
	}

	// ★ 必须先 RemoveDynamic，只调 EndTask() 是不够的。
	//
	// UAbilityTask_PlayMontageAndWait 广播回调前的守卫是
	// ShouldBroadcastAbilityTaskDelegates()，而它判断的是 **能力** 是否激活：
	//
	//     bool ShouldBroadcast = (Ability && Ability->IsActive());   // AbilityTask.cpp:199
	//
	// 连段期间能力当然一直激活着 —— 所以旧任务哪怕已经结束，
	// 它的 OnCompleted 照样会把我们喊醒。
	// 摘掉绑定是唯一不依赖引擎内部状态判断的做法。
	CurrentSegmentMontageTask->OnCompleted.RemoveDynamic(this, &URPG_GA_LightAttack::OnMontageCompleted);
	CurrentSegmentMontageTask->OnInterrupted.RemoveDynamic(this, &URPG_GA_LightAttack::OnMontageInterrupted);

	CurrentSegmentMontageTask->EndTask();
	CurrentSegmentMontageTask = nullptr;
}

bool URPG_GA_LightAttack::IsEventFromCurrentSegment(const FGameplayEventData& Payload) const
{
	// Notify 里带的是"发送这条事件的那段蒙太奇"
	const UObject* SourceMontage = Payload.OptionalObject2.Get();

	// 任一侧为空就放行。
	//   · 老资产 / 手写的 Notify 没带来源 → 不能因为"没带"就完全不触发
	//   · 当前没有蒙太奇（走模拟时序分支）→ 没有可比对的对象
	// 宁可放过一条迟到事件，也不要让正常连段整个失灵 ——
	// 后者是"完全不能玩"，前者只是偶发跳段。
	if (!SourceMontage || !CurrentSegmentMontage)
	{
		return true;
	}

	// ⚠️ 已知局限：这里比的是**蒙太奇资产**，不是"哪一个播放实例"。
	// 如果连续两段用了同一个蒙太奇资产，上一段的迟到事件就认不出来。
	//
	// 引擎自己对付这个问题用的是 MontageInstanceID
	// （见 UAnimInstance::TriggerMontageEndedEvent 里的注释：
	//  "Compare against the montage instance ID to prevent ending notify states
	//   from other instances of the same montage"），
	// Notify 侧可以从 EventReference 里取到它。
	//
	// 但把它传出来要比对实例 ID —— 而 GA 侧拿不到"当前实例 ID"这个量，
	// 得再绕一圈去查 AnimInstance。本工程每段用的是各自独立的蒙太奇资产
	// （AM_Light_01~05），踩不到这个边界，所以先不做。
	// 如果将来出现"两段共用同一个蒙太奇"的设计，这里要一起改。
	return SourceMontage == CurrentSegmentMontage;
}

void URPG_GA_LightAttack::StopCurrentSegmentMontage()
{
	// ══════════════════════════════════════════════════════════════════
	//  为什么切段时要三件事一起做
	// ══════════════════════════════════════════════════════════════════
	// 连段的推进方式是"第 N 段的衔接窗口一开，立刻播第 N+1 段的蒙太奇"，
	// 但第 N 段的蒙太奇实例和它的任务**都还活着**。引擎不会因为
	// "同一个 Slot 上有新蒙太奇了"就把旧的作废。于是旧的那一份会在
	// 它自己动画播完的那一刻广播 OnCompleted，把正在播第 N+1 段的这次
	// 能力判定为"整套打完了"。
	//
	// 症状：连招永远打不全 —— 而且段数越多越明显，因为每一段都会
	// 留下一颗炸在它自己动画结束时刻的雷。
	//
	// 三件事缺一不可：
	//   ① DetachCurrentSegmentMontageTask() —— 摘掉**任务**的回调，见那里的说明
	//   ② 停掉旧蒙太奇 —— 让它立刻终止，不再继续推进。
	//      不停的话它会一路播到自己的「攻击结束」通知，同样会让连段提前收尾。
	//   ③ 清空引用 —— 避免下一段误用上一段的指针
	//
	// ⚠️ 但"停掉"**不等于**"它的 Notify 全都不会再来了"：
	// 引擎会为它身上**还活着的 NotifyState** 补发 NotifyEnd
	// （UAnimInstance::TriggerMontageEndedEvent，AnimInstance.cpp:2507）。
	// 所以窗口类的迟到事件仍然要靠 IsEventFromCurrentSegment() 按来源过滤 ——
	// 停蒙太奇和过滤是两件事，谁也替代不了谁。
	DetachCurrentSegmentMontageTask();

	if (CurrentSegmentMontage)
	{
		if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
		{
			// 用 StopMontageIfCurrent 而不是无脑 CurrentMontageStop：
			// 前者只在这个蒙太奇**确实是当前蒙太奇**时才停，
			// 万一将来有别的能力（闪避等）抢了动画，不会被我误停。
			// 混合时间传 0 —— 新一段紧接着就接上了，不需要它慢慢淡出。
			ASC->StopMontageIfCurrent(*CurrentSegmentMontage, 0.f);
		}

		CurrentSegmentMontage = nullptr;
	}
}

bool URPG_GA_LightAttack::TryStartNextSegment()
{
	URPG_CombatComponent* Combat = GetCombatComponent();
	URPG_AttackModuleData* Module = GetAttackModule();

	if (!Combat || !Module) return false;

	FGameplayTag BufferedTag;
	if (!Combat->ConsumeInputTag(BufferedTag)) return false;

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
		// ★ 打成 Log 而不是 Verbose。
		// "连招打不全"最常见的原因就是模组里段数不够，而 Verbose 在默认
		// 日志级别下看不见 —— 结果就是玩家按了没反应、日志一片空白，
		// 只能靠猜。这条日志直接把"差哪一段"说出来。
		UE_LOG(LogRPG_Combat, Log,
			TEXT("[%s] 连段到此为止：当前第 %d 段，而攻击模组 %s 里只配了 %d 段轻击。"
			     "想继续连就在该 DA 的 Light Attacks 数组里补上第 %d 条"),
			*GetName(), CurrentSegmentIndex + 1,
			*Module->GetName(), Module->GetLightSegmentCount(), NextIndex + 1);

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

	// 摘掉最后一段的回调，但**不停**它的蒙太奇 ——
	// 收尾时我们希望这一段的动画自然播完（尤其是 AttackEnd 之后那 5% 的后摇）。
	// 摘回调是因为：能力结束后蒙太奇还会继续播完，那时它会广播 OnInterrupted/
	// OnCompleted，如果没有摘掉就会再进一次 FinishCombo，属于自己喊自己。
	DetachCurrentSegmentMontageTask();
	CurrentSegmentMontage = nullptr;

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
	// 迟到事件：上一段被停掉的蒙太奇补发的 —— 忽略，见 IsEventFromCurrentSegment
	if (!IsEventFromCurrentSegment(Payload)) return;

	URPG_AttackModuleData* Module = GetAttackModule();
	if (!Module) return;

	// ── 默认用模组的检测配置 ──
	ERPG_TraceSource Source = Module->TraceSource;
	float Radius = Module->TraceRadius;
	FName SocketStart = Module->LeftHandSocket;
	FName SocketEnd = Module->RightHandSocket;

	// ── Notify 上如果配了覆盖，就用覆盖值 ──
	if (const URPG_AttackWindowPayload* WindowPayload = Cast<URPG_AttackWindowPayload>(Payload.OptionalObject.Get()))
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
	// ★ 这一条尤其重要：上一段的"判定窗口关闭"会在下一段刚播起来时到达，
	// 不过滤的话会把下一段刚开起来的轨迹检测任务直接掐掉 ——
	// 症状是"连招里后面几段打不到人"，而且看起来像判定窗口配错了。
	if (!IsEventFromCurrentSegment(Payload)) return;

	if (TraceTask)
	{
		TraceTask->EndTask();
		TraceTask = nullptr;
	}
}

void URPG_GA_LightAttack::OnComboWindowOpen(FGameplayEventData Payload)
{
	// 迟到事件过滤 —— 见 IsEventFromCurrentSegment
	if (!IsEventFromCurrentSegment(Payload)) return;

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
	// ★ 这条是"跳段"的元凶：上一段被停掉时引擎补发的「衔接窗口关闭」
	// 会在接上下一段后一帧到达。不过滤的话它会把缓存里的下一次按键吃掉，
	// 凭空多接一段 —— 表现为快速连打时跳段。
	if (!IsEventFromCurrentSegment(Payload)) return;

	bComboWindowOpen = false;

	if (TryStartNextSegment()) return;

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
	UE_LOG(LogRPG_Combat, Log,
		TEXT("[%s] 第 %d 段收到「攻击结束」通知 → 连段收尾"), *GetName(), CurrentSegmentIndex + 1);

	FinishCombo(false);
}

void URPG_GA_LightAttack::OnMontageCompleted()
{
	// 蒙太奇自然播完。正常情况下 AttackEnd Notify 会先触发，
	// 走到这里说明蒙太奇上没配那个 Notify —— 作为兜底结束能力。
	//
	// ⚠️ 排查提示：这条日志**只应该在最后一段真正播完时出现一次**。
	// 如果它出现在"你还在连打"的时候，说明有上一段的蒙太奇残留没被清掉，
	// 它的完成回调把当前这一段误判成整套打完了 —— 那正是连招打不全的原因。
	UE_LOG(LogRPG_Combat, Log,
		TEXT("[%s] 第 %d 段的蒙太奇自然播完 → 连段结束"
		     "（若此刻你还在连打，说明有上一段的蒙太奇残留没清干净）"),
		*GetName(), CurrentSegmentIndex + 1);

	FinishCombo(false);
}

void URPG_GA_LightAttack::OnMontageInterrupted()
{
	UE_LOG(LogRPG_Combat, Log,
		TEXT("[%s] 第 %d 段的蒙太奇被打断 → 连段结束"), *GetName(), CurrentSegmentIndex + 1);

	FinishCombo(true);
}

void URPG_GA_LightAttack::OnWeaponTraceHit(const TArray<FHitResult>& Hits)
{
	for (const FHitResult& Hit : Hits)
	{
		ApplyDamageToTarget(Hit.GetActor(), CurrentDamageMultiplier, &Hit);
	}
}
