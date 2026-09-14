// Copyright Epic Games, Inc. All Rights Reserved.

#include "AbilitySystem/RPG_AttributeSet.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "GameplayEffectExtension.h"
#include "Net/UnrealNetwork.h"
#include "Character/RPG_BaseCharacter.h"
#include "Core/RPG_GameplayTags.h"
#include "Core/RPG_LogChannels.h"

URPG_AttributeSet::URPG_AttributeSet()
{
	// ── 保底默认值 ──
	// 真正的数值应该由 GE_InitAttributes 在角色初始化时覆盖（数据驱动：改数值不用重新编译）。
	// 这里给值只保证一件事：即使忘了配那个 GE，角色也不会因为属性全为 0 而除零或瞬间暴毙。
	//
	// "构造函数给安全默认值 + GE 覆盖实际数值" 是实际项目的标准做法——
	// 纯靠 GE 初始化的话，一旦资产没配好，出现的是 NaN 之类的诡异问题，非常难查。
	InitHealth(100.f);
	InitMaxHealth(100.f);

	InitAttack(10.f);
	InitDefense(10.f);

	InitMana(100.f);
	InitMaxMana(100.f);

	InitStamina(100.f);
	InitMaxStamina(100.f);

	InitIncomingDamage(0.f);
}

void URPG_AttributeSet::PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue)
{
	Super::PreAttributeChange(Attribute, NewValue);

	ClampAttribute(Attribute, NewValue);
}

void URPG_AttributeSet::PreAttributeBaseChange(const FGameplayAttribute& Attribute, float& NewValue) const
{
	Super::PreAttributeBaseChange(Attribute, NewValue);

	// ★ 这里才是 GE 修改必经的那道关。
	// 只写 PreAttributeChange 拦不住 GE —— 详见头文件里的说明。
	//
	// 和 PreAttributeChange 共用同一套规则，保证
	// "直接赋值"和"GE 修改"两条路径的结果必然一致。
	const float Requested = NewValue;
	ClampAttribute(Attribute, NewValue);

	// ── 诊断：只在**真的**发生了越界时才打日志 ──
	// 这段是为了回答"耐力是不是跑到 [0,100] 外面去了"这类怀疑。
	// 正常情况下永不触发；一旦触发，说明数值平衡或某处配置有问题，
	// 而这条日志会直接告诉你是哪个属性、从多少被拉回多少。
	if (!FMath::IsNearlyEqual(NewValue, Requested))
	{
		UE_LOG(LogRPG_Ability, Log,
			TEXT("[%s] 属性 %s 越界被钳制：%.2f → %.2f（合法区间 [0, 对应的 Max]）"),
			*GetNameSafe(GetOwningActor()),
			*Attribute.GetName(), Requested, NewValue);
	}
}

void URPG_AttributeSet::ClampAttribute(const FGameplayAttribute& Attribute, float& NewValue) const
{
	// ── 为什么钳制放这里 ──
	// Pre*AttributeChange 的调用时机最早：在属性值被真正写入**之前**修改待写入的值。
	// 代价是拿不到"是谁改的"这类上下文（那要用 PostGameplayEffectExecute）。
	// 另外 NewValue 是引用参数，改它不会触发属性变化回调，也不会递归。

	if (Attribute == GetHealthAttribute())
	{
		NewValue = FMath::Clamp(NewValue, 0.f, GetMaxHealth());
	}
	else if (Attribute == GetManaAttribute())
	{
		NewValue = FMath::Clamp(NewValue, 0.f, GetMaxMana());
	}
	else if (Attribute == GetStaminaAttribute())
	{
		NewValue = FMath::Clamp(NewValue, 0.f, GetMaxStamina());
	}
	// 最大值类属性只需保证非负。
	// 如果被 Debuff 减成负数，上面几个 Clamp 的上下界会反转（Min > Max），
	// FMath::Clamp 在那种情况下行为未定义——所以这里必须挡住。
	else if (Attribute == GetMaxHealthAttribute()
		|| Attribute == GetMaxManaAttribute()
		|| Attribute == GetMaxStaminaAttribute())
	{
		NewValue = FMath::Max(NewValue, 0.f);
	}
}

void URPG_AttributeSet::PostGameplayEffectExecute(const FGameplayEffectModCallbackData& Data)
{
	Super::PostGameplayEffectExecute(Data);

	// ══════════════════════════════════════════════════════════════════
	//  伤害落地 —— 整个游戏里唯一的"扣血"入口
	// ══════════════════════════════════════════════════════════════════
	// 只有元属性 IncomingDamage 会走到下面的逻辑；其他属性的修改（加攻、扣耐力等）
	// 到这里就直接返回了，它们由各自的使用方去响应。
	if (Data.EvaluatedData.Attribute != GetIncomingDamageAttribute()) return;

	const float LocalDamage = GetIncomingDamage();
	SetIncomingDamage(0.f);   // 元属性用完立即清零，它不承载任何持久状态

	if (LocalDamage <= 0.f) return;

	AActor* OwningActor = GetOwningActor();

	// ══════════════════════════════════════════════════════════════════
	//  ★ "角色实体"一律取**化身**（Avatar），不要用 OwnerActor
	// ══════════════════════════════════════════════════════════════════
	// `OwningActor` 是 ASC 的 OwnerActor，而本项目的 ASC 归属是分开的：
	//   · 敌人 —— ASC 在自己身上，OwnerActor 就是角色
	//   · 玩家 —— ASC 在 RPG_PlayerState 上，OwnerActor 是 **PlayerState**
	//
	// 所以任何 `Cast<ARPG_BaseCharacter>(OwningActor)` 对**玩家**都会静默失败。
	// 这个坑已经踩过一次（伤害飘字整段被跳过，打玩家不冒数字），
	// 而且**不报任何错**。下面统一在这里解析一次，后面的代码都用 AvatarActor，
	// 免得每处各写各的再漏一个。
	//
	// `Data.Target` 是 UAbilitySystemComponent&（GameplayEffectExtension.h:18-28），
	// **不是** FGameplayAbilityActorInfo，所以没有 Data.Target.AvatarActor 那种写法，
	// 要走 ASC 的 GetAvatarActor()（AbilitySystemComponent.h:1529）。
	AActor* AvatarActor = Data.Target.GetAvatarActor();
	if (!AvatarActor)
	{
		// 兜底：Avatar 还没指派上的窗口期（比如 GE 在 Possess 之前就落地）。
		// 这时候 OwnerActor 是唯一能用的位置来源 —— 对敌人它是对的，
		// 对玩家它会退化成"用 PlayerState 的位置"，聊胜于无。
		AvatarActor = OwningActor;
	}

	// ── 无敌帧兜底检查 ──
	// 第一道防线在 GE_Damage 资产上（TargetTagRequirements 组件，GE 层面直接拒绝应用，
	// 连 Execution 都不会跑，更省性能）。这里再判一次是防御性编程：
	// 万一将来有别的途径直接写 IncomingDamage（陷阱、脚本伤害、DOT），也不至于打穿无敌。
	if (Data.Target.HasMatchingGameplayTag(RPGTags::State_Invulnerable))
	{
		UE_LOG(LogRPG_Combat, Verbose, TEXT("[%s] 伤害被无敌帧挡下：%.1f"), *GetNameSafe(OwningActor), LocalDamage);
		return;
	}

	const float OldHealth = GetHealth();
	const float NewHealth = FMath::Clamp(OldHealth - LocalDamage, 0.f, GetMaxHealth());
	SetHealth(NewHealth);

	UE_LOG(LogRPG_Combat, Log, TEXT("[%s] 受到 %.1f 点伤害：%.0f → %.0f"), *GetNameSafe(OwningActor), LocalDamage, OldHealth, NewHealth);

	// ══════════════════════════════════════════════════════════════════
	//  ★ 已经死了就不再广播任何事件
	// ══════════════════════════════════════════════════════════════════
	// 尸体是会被继续砍到的：连段的后几刀、范围伤害、别的敌人的攻击……
	// 而这时候血量已经是 0，下面两条分支会走到"受击"那一条（因为
	// 死亡判定的条件是"从有血变成没血"，这次不满足）。
	//
	// 后果是：每一刀都广播一次 Event.Combat.Hit → GA_HitReact 尝试激活
	// → 被 ActivationBlockedTags 里的 State.Dead 挡住 → 引擎通过
	// AbilityFailedCallbacks 报一条 Warning（见 URPG_AbilitySystemComponent）。
	// 表现是**打尸体时日志刷屏**，而且会掩盖真正有用的告警。
	//
	// 从语义上讲这也更对："受击"是活人才有的反应。
	if (OldHealth <= 0.f)
	{
		return;
	}

	// ══════════════════════════════════════════════════════════════════
	//  ⚠️ 下面两个事件**只在服务器广播** ★
	// ══════════════════════════════════════════════════════════════════
	// 属性值的修改本身是预测的：本地控制的角色在自己的客户端上会跑一遍
	// 这个函数（PredictivelyExecuteEffectSpec，GameplayEffect.cpp:3069），
	// 然后由 GAS 在预测失败时回滚。
	//
	// 但**事件广播不会回滚** —— 委托一旦发出去就收不回来了。
	// 如果不做这道判断，联机下会变成：
	//   · 客户端本地预测"我被打死了" → 播死亡蒙太奇、进布娃娃
	//   · 服务器说"没死" → 回滚血量，但人已经躺下了
	//   · 服务器后来真的判死 → 再躺一次
	// 表现是"死亡/受击表现偶尔闪一下或播两遍"，而且只在延迟高的客户端上出现，
	// 非常难查。
	//
	// 所以定下规矩：**属性集只负责广播事实，广播的公信力由服务器垄断。**
	// 客户端要知道发生了什么，靠复制（属性、标签、蒙太奇都是复制的），
	// 而不是靠自己也广播一遍。
	//
	// 单机（Standalone）下 HasAuthority() 恒为 true，这条判断不产生任何影响。
	const bool bAuthority = OwningActor && OwningActor->HasAuthority();

	// ── 伤害飘字 ──
	// 走 NetMulticast 而不是上面那种 GameplayEvent —— 因为飘字是**每个客户端
	// 各自要画**的东西，而 GameplayEvent 只在服务器广播。
	//
	// 这里能拿到的是"谁挨打了、掉了多少血、打在哪"，
	// 至于那个数字长什么样、往哪飘、飘多久，全是 HUD 的事。
	// 属性集只负责把事实送到每一端。
	//
	// ⚠️ 必须和下面的事件一样**只在服务器调用**。
	// 这个函数在预测路径上也会跑到（见上面的说明），而 NetMulticast 在
	// 非服务器上调用的编译结果就是**在本地直接执行 _Implementation**
	// （Actor.cpp 的 GetFunctionCallspace：非服务器且未标 BlueprintAuthorityOnly
	//  时返回 Callspace，而不是拒绝）。
	// 于是预测的客户端先本地冒一个数字，随后服务器的权威广播又冒一个 —— 两份。
	// 单机（NM_Standalone）返回的是 Local，所以要保留"本地执行"这条路，
	// 不能用"只在服务器才调 RPC"以外的办法绕开。
	if (bAuthority)
	{
		if (ARPG_BaseCharacter* VictimCharacter = Cast<ARPG_BaseCharacter>(AvatarActor))
		{
			// 优先用命中点：武器轨迹检测把 HitResult 塞进了 EffectContext
			// （见 URPG_GameplayAbilityBase::ApplyDamageToTarget 里的 AddHitResult）。
			// 拿不到就回落到角色胸口高度 —— 模拟命中、DOT、陷阱伤害都没有命中点，
			// 没有这个兜底的话那些伤害会一声不响，看起来像"打了没反应"。
			FVector NumberLocation;
			if (const FHitResult* Hit = Data.EffectSpec.GetContext().GetHitResult())
			{
				NumberLocation = Hit->ImpactPoint;
			}
			else
			{
				NumberLocation = AvatarActor->GetActorLocation()
					+ FVector(0.f, 0.f, VictimCharacter->GetSimpleCollisionHalfHeight());
			}

			VictimCharacter->Multicast_ShowDamageNumber(LocalDamage, NumberLocation);
		}
	}

	// ── 死亡判定 ──
	// 走到这里说明 OldHealth > 0（上面那道早退已经挡住了尸体），
	// 所以只需要看新血量是不是归零 —— "从活到死"这个跨越因此只触发一次。
	if (NewHealth <= 0.f)
	{
		if (bAuthority)
		{
			UE_LOG(LogRPG_Combat, Log, TEXT("[%s] 生命归零，广播死亡事件"), *GetNameSafe(OwningActor));

			// 用 GameplayEvent 而不是直接调用死亡逻辑——保持"表现层不做逻辑"的分层原则：
			// 死亡的表现（蒙太奇、布娃娃、AI 停止）由 GA_Death 去处理，
			// 属性集只管广播事实。
			FGameplayEventData Payload;
			Payload.EventTag = RPGTags::Event_Combat_Death;
			Payload.Instigator = Data.EffectSpec.GetContext().GetInstigator();
			// 用**化身**而不是 OwningActor —— Target 语义上是"被打的那个角色"，
			// 而玩家的 OwningActor 是 PlayerState。目前没有消费者读它，
			// 但留着就是同一个坑的下一次触发点（见上面 AvatarActor 的说明）。
			Payload.Target = AvatarActor;
			// 把"最后一击的伤害"带出去，死亡表现可以据此区分轻重击
			Payload.EventMagnitude = LocalDamage;

			UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(OwningActor, RPGTags::Event_Combat_Death, Payload);
		}
	}
	else if (bAuthority)
	{
		// ── 没死 → 广播受击事件 ──
		//
		// 和死亡事件二选一（else if）：致命伤走死亡分支，其余走受击分支。
		// 两个都发的话，受击反应会和死亡蒙太奇抢动画，表现上会闪一下。
		FGameplayEventData Payload;
		Payload.EventTag = RPGTags::Event_Combat_Hit;
		Payload.Instigator = Data.EffectSpec.GetContext().GetInstigator();
		// 用**化身**而不是 OwningActor —— Target 语义上是"被打的那个角色"，
		// 而玩家的 OwningActor 是 PlayerState。目前没有消费者读它，
		// 但留着就是同一个坑的下一次触发点（见上面 AvatarActor 的说明）。
		Payload.Target = AvatarActor;
		Payload.EventMagnitude = LocalDamage;

		UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(OwningActor, RPGTags::Event_Combat_Hit, Payload);

		UE_LOG(LogRPG_Combat, Verbose, TEXT("[%s] 广播受击事件（%.1f 点）"),
			*GetNameSafe(OwningActor), LocalDamage);
	}
}

// ══════════════════════════════════════════════════════════════════════
//  网络复制
// ══════════════════════════════════════════════════════════════════════

void URPG_AttributeSet::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// DOREPLIFETIME_CONDITION_NOTIFY 参数：类名 / 属性名 / 复制条件 / 通知策略
	//
	// COND_None —— 复制给所有客户端。
	//   这里不能图省事用 COND_OwnerOnly：血条、队友状态、敌人血量都需要被别人看到。
	//
	// REPNOTIFY_Always —— 即使新值和旧值相同也触发 OnRep。
	//   为什么不用默认的 REPNOTIFY_OnChanged？因为 GAS 的属性有 BaseValue / CurrentValue
	//   两层，有时表面数值没变但底层状态变了（比如 Buff 叠加的中间态）。用 Always 能保证
	//   客户端不漏更新，代价只是极少量冗余回调 —— 对属性同步完全值得。
	DOREPLIFETIME_CONDITION_NOTIFY(URPG_AttributeSet, Health,     COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(URPG_AttributeSet, MaxHealth,  COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(URPG_AttributeSet, Attack,     COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(URPG_AttributeSet, Defense,    COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(URPG_AttributeSet, Mana,       COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(URPG_AttributeSet, MaxMana,    COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(URPG_AttributeSet, Stamina,    COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(URPG_AttributeSet, MaxStamina, COND_None, REPNOTIFY_Always);

	// IncomingDamage 有意不注册 —— 它只是服务器上伤害计算的临时投递口，
	// 用完立即清零，同步它没有意义。
}

// ── 8 个 OnRep 实现完全同构 ──
// GAMEPLAYATTRIBUTE_REPNOTIFY 内部会：
//   1. SetBaseAttributeValueFromReplication —— 把服务器同步来的值写进本地属性
//   2. 广播 OnGameplayAttributeValueChange 委托 —— UI 因此自动刷新
//
// 每个属性单独写一个（而不是用宏批量生成），是为了让"哪个属性漏了 OnRep"
// 在代码里一眼可见：漏 OnRep 不会编译报错，但客户端界面永远不刷新。
void URPG_AttributeSet::OnRep_Health(const FGameplayAttributeData& OldHealth)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(URPG_AttributeSet, Health, OldHealth);
}

void URPG_AttributeSet::OnRep_MaxHealth(const FGameplayAttributeData& OldMaxHealth)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(URPG_AttributeSet, MaxHealth, OldMaxHealth);
}

void URPG_AttributeSet::OnRep_Attack(const FGameplayAttributeData& OldAttack)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(URPG_AttributeSet, Attack, OldAttack);
}

void URPG_AttributeSet::OnRep_Defense(const FGameplayAttributeData& OldDefense)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(URPG_AttributeSet, Defense, OldDefense);
}

void URPG_AttributeSet::OnRep_Mana(const FGameplayAttributeData& OldMana)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(URPG_AttributeSet, Mana, OldMana);
}

void URPG_AttributeSet::OnRep_MaxMana(const FGameplayAttributeData& OldMaxMana)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(URPG_AttributeSet, MaxMana, OldMaxMana);
}

void URPG_AttributeSet::OnRep_Stamina(const FGameplayAttributeData& OldStamina)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(URPG_AttributeSet, Stamina, OldStamina);
}

void URPG_AttributeSet::OnRep_MaxStamina(const FGameplayAttributeData& OldMaxStamina)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(URPG_AttributeSet, MaxStamina, OldMaxStamina);
}
