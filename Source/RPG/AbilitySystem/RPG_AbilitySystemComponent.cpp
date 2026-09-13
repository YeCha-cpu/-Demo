// Copyright Epic Games, Inc. All Rights Reserved.

#include "AbilitySystem/RPG_AbilitySystemComponent.h"

#include "Abilities/GameplayAbility.h"
#include "GameplayAbilitySpec.h"
#include "AbilitySystem/Abilities/RPG_GameplayAbilityBase.h"
#include "Core/RPG_LogChannels.h"

URPG_AbilitySystemComponent::URPG_AbilitySystemComponent()
{
	// ── 开启复制 ──
	// 少这一句，联机下整个 GAS 等于失效：
	//   · 能力激活不会同步到服务器（客户端放技能，服务器不知道）
	//   · GE 不会复制到其他客户端（别人看不到你身上的 Buff）
	//   · GameplayCue 不会在其他客户端播放（特效只有自己看得见）
	// 而且这三件事都不会报错，只是"没反应"。
	SetIsReplicated(true);

	// ── 复制模式 ──
	// Mixed = 自己控制的角色走 Full，其他角色走 Minimal：
	//   Full    —— 客户端拿到完整的 GE 信息，这样才能做本地预测
	//   Minimal —— 只同步 GameplayCue 与标签，省带宽
	//             （别人身上的 Buff 具体是几层、还剩几秒，我不需要知道）
	// 这是玩家类 ASC 的标准配置。
	//
	// 敌人（AI 控制）在 Mixed 下走 Minimal，对 MVP 够用。
	// 若将来要让客户端对敌人做预测（比如"附身"机制），再单独调成 Full。
	SetReplicationMode(EGameplayEffectReplicationMode::Mixed);
}

void URPG_AbilitySystemComponent::BeginPlay()
{
	Super::BeginPlay();

	// 接管"能力激活失败"的汇报权。
	// 不接这个回调的话，GAS 会对失败保持完全沉默（见头文件里的说明）。
	AbilityFailedCallbacks.AddUObject(this, &URPG_AbilitySystemComponent::OnAbilityActivationFailed);
}

void URPG_AbilitySystemComponent::OnAbilityActivationFailed(
	const UGameplayAbility* Ability,
	const FGameplayTagContainer& FailureTags)
{
	// FailureTags 里是引擎定义的失败原因标签，常见取值：
	//   Ability.ActivateFail.CanActivate  —— GA 重写的 CanActivateAbility 返回了 false
	//   Ability.ActivateFail.BlockedTags  —— 被 ActivationBlockedTags 阻断
	//   Ability.ActivateFail.MissingTags  —— 缺少 ActivationRequiredTags
	//   Ability.ActivateFail.Cooldown     —— 冷却中
	//   Ability.ActivateFail.Cost         —— 资源不足（Cost GE 检查失败）
	//   Ability.ActivateFail.Networking   —— 网络策略不允许（比如 ServerOnly 的能力被客户端请求）
	const FString ReasonText = FailureTags.IsEmpty()
		? TEXT("(引擎未提供原因标签)")
		: FailureTags.ToStringSimple();

	UE_LOG(LogRPG_Ability, Warning, TEXT("能力 %s 激活失败。原因：%s"),
		*GetNameSafe(Ability), *ReasonText);
}

bool URPG_AbilitySystemComponent::RegisterInputAbility(
	FGameplayTag InputTag,
	TSubclassOf<UGameplayAbility> AbilityClass,
	int32 Level)
{
	if (!InputTag.IsValid())
	{
		UE_LOG(LogRPG_Ability, Warning, TEXT("RegisterInputAbility 失败：输入标签无效"));
		return false;
	}

	if (!AbilityClass)
	{
		UE_LOG(LogRPG_Ability, Warning, TEXT("RegisterInputAbility 失败：%s 没有指定能力类"),
			*InputTag.ToString());
		return false;
	}

	// 若重复注册同一个输入标签时，先撤销旧能力。
	// 不这么做的话，同一个标签会挂上多个 GA 实例，激活时可能同时触发两个，很难查。
	if (const FGameplayAbilitySpecHandle* OldHandle = InputTagToSpecHandle.Find(InputTag))
	{
		if (OldHandle->IsValid())
		{
			ClearAbility(*OldHandle);
			UE_LOG(LogRPG_Ability, Verbose, TEXT("输入标签 %s 重复注册，已撤销旧能力"),
				*InputTag.ToString());
		}
		InputTagToSpecHandle.Remove(InputTag);
	}

	// “2步走”创建并授予 GA 实例
	// Level 会传给 GA，进而影响它施加的 GE 的 Level——
	// 这是 GAS 里做"技能等级影响数值"的机制，现在固定 1 级，架构先留好。
	const FGameplayAbilitySpec NewSpec(AbilityClass, Level);
	const FGameplayAbilitySpecHandle Handle = GiveAbility(NewSpec);

	if (!Handle.IsValid())
	{
		UE_LOG(LogRPG_Ability, Error, TEXT("RegisterInputAbility 失败：授予能力 %s 未成功"), *AbilityClass->GetName());
		return false;
	}

	// 存储两个映射关系表，方便后续通过输入标签快速找到对应的 GA 类与 Handle
	InputTagToAbilityClass.Add(InputTag, AbilityClass);
	InputTagToSpecHandle.Add(InputTag, Handle);

	UE_LOG(LogRPG_Ability, Log, TEXT("注册输入能力：%s → %s"), *InputTag.ToString(), *AbilityClass->GetName());

	// ── 被动能力：授予后立刻激活 ──
	// 耐力恢复这类常驻能力没有输入触发，必须在这里主动拉起。
	// 激活后它们会一直保持激活（GA 内部不调 EndAbility），靠持续挂着的 GE 起作用。
	if (const URPG_GameplayAbilityBase* AbilityCDO = AbilityClass->GetDefaultObject<URPG_GameplayAbilityBase>())
	{
		if (AbilityCDO->ShouldActivateOnGranted())	// 是否授予后立即激活
		{
			TryActivateAbility(Handle);

			UE_LOG(LogRPG_Ability, Log, TEXT("  └─ 该能力标记为「授予即激活（被动技能）」，已自动激活"));
		}
	}

	return true;
}

bool URPG_AbilitySystemComponent::GivePassiveAbility(TSubclassOf<UGameplayAbility> AbilityClass, int32 Level)
{
	if (!AbilityClass)
	{
		UE_LOG(LogRPG_Ability, Warning, TEXT("GivePassiveAbility 失败：没有指定能力类"));
		return false;
	}

	// ══════════════════════════════════════════════════════════════════
	//  已经授予过就跳过 ★
	// ══════════════════════════════════════════════════════════════════
	// 和 RegisterInputAbility 里那个守卫是同一个理由，但这里更容易踩到：
	//
	// 玩家的 ASC 挂在 PlayerState 上，**重生时 PlayerState 不销毁** ——
	// 新 Pawn 的 InitializeAbilitySystem 会再跑一遍，把被动能力又授予一次。
	// 结果就是挂了两份 GA_StaminaRegen → 耐力恢复速度翻倍，
	// 而且它**不报错**，只是数值悄悄不对。
	//
	// 输入能力那边靠 InputTagToSpecHandle 这个映射表挡住了，
	// 被动能力没有那样的表，所以直接按类查。
	if (FindAbilitySpecFromClass(AbilityClass))
	{
		UE_LOG(LogRPG_Ability, Verbose,
			TEXT("被动能力 %s 已经授予过，跳过（重生时会出现这种情况，属正常）"),
			*AbilityClass->GetName());
		return true;
	}

	// “2步走”创建并授予 GA 实例
	const FGameplayAbilitySpec NewSpec(AbilityClass, Level);
	const FGameplayAbilitySpecHandle Handle = GiveAbility(NewSpec);

	if (!Handle.IsValid())
	{
		UE_LOG(LogRPG_Ability, Error, TEXT("GivePassiveAbility 失败：授予 %s 未成功"),
			*AbilityClass->GetName());
		return false;
	}

	UE_LOG(LogRPG_Ability, Log, TEXT("注册被动能力：%s"), *AbilityClass->GetName());

	// 被动能力通常需要立刻生效（耐力恢复从角色一出生就该工作）
	if (const URPG_GameplayAbilityBase* AbilityCDO = AbilityClass->GetDefaultObject<URPG_GameplayAbilityBase>())
	{
		if (AbilityCDO->ShouldActivateOnGranted())
		{
			TryActivateAbility(Handle);
			UE_LOG(LogRPG_Ability, Log, TEXT(" └─ %s 被动能力已自动激活"), *AbilityClass->GetName());
		}
	}

	return true;
}

void URPG_AbilitySystemComponent::ReactivatePassiveAbilities()
{
	int32 ReactivatedCount = 0;

	// ★ 加锁：遍历 ActivatableAbilities.Items 期间它不能被改大小。
	//
	// 就目前的代码路径而言（TryActivateAbility 只会 MarkAbilitySpecDirty，
	// 不增删元素）不加锁也是安全的 —— 但"安全"是靠"恰好没有能力在激活时
	// 授予/撤销别的能力"维持的，那是个会随时间失效的前提。
	// GiveAbility 和 OnRemoveAbility 自己都要求持有这把锁
	// （AbilitySystemComponent_Abilities.cpp:309 / :626），
	// 遍历方同样持有才是对称的写法。
	FScopedAbilityListLock AbilityListLock(*this);

	// 遍历已授予的能力表，而不是角色的 StartupPassiveAbilities 配置数组 ——
	// 后者只有角色自己知道，ASC 不该反向依赖角色。
	// ActivatableAbilities 就是"这个 ASC 现在到底有哪些能力"的权威来源。
	for (const FGameplayAbilitySpec& Spec : ActivatableAbilities.Items)
	{
		if (!Spec.Ability || Spec.IsActive())
		{
			continue;
		}

		// 判断依据取 CDO 上的标记 —— 它是配置，不是运行时状态。
		const URPG_GameplayAbilityBase* AbilityCDO =
			Cast<URPG_GameplayAbilityBase>(Spec.Ability);

		if (AbilityCDO && AbilityCDO->ShouldActivateOnGranted())
		{
			// 用 Handle 激活而不是标记 Spec 为 dirty：
			// TryActivateAbility 会走完整的 CanActivateAbility 检查
			// （刚摘掉 State.Dead，所以能过），语义上更正确。
			TryActivateAbility(Spec.Handle);
			++ReactivatedCount;
		}
	}

	UE_LOG(LogRPG_Ability, Log, TEXT("重生：重新激活 %d 个常驻被动能力"), ReactivatedCount);
}

void URPG_AbilitySystemComponent::RegisterInputAbilities(const TMap<FGameplayTag, TSubclassOf<UGameplayAbility>>& InMappings)
{
	for (const TPair<FGameplayTag, TSubclassOf<UGameplayAbility>>& Pair : InMappings)
	{
		RegisterInputAbility(Pair.Key, Pair.Value);
	}

	UE_LOG(LogRPG_Ability, Log, TEXT("批量注册输入能力完成，共 %d 条"), InMappings.Num());
}

bool URPG_AbilitySystemComponent::TryActivateAbilityByInputTag(FGameplayTag InputTag)
{
	const FGameplayAbilitySpecHandle* HandlePtr = InputTagToSpecHandle.Find(InputTag);

	if (!HandlePtr || !HandlePtr->IsValid())
	{
		// ⚠️ 用 Warning 而不是 Verbose。
		// 去重是为了防止玩家连打时刷屏：同一个标签只警告一次。
		if (!WarnedInputTags.Contains(InputTag))
		{
			WarnedInputTags.Add(InputTag);

			UE_LOG(LogRPG_Ability, Warning,
				TEXT("输入标签 %s 没有绑定任何能力 —— "
				     "请在角色的 Startup Abilities 里加上这条映射，然后检查启动日志里"
				     "是否有对应的「注册输入能力」记录"),
				*InputTag.ToString());
		}

		return false;
	}

	// 用 Handle 而不是类去激活：同一个 GA 类可能被授予多次（不同来源/等级），
	// Handle 精确指向"这一次授予"，不会误激活另一个实例。
	const bool bActivated = TryActivateAbility(*HandlePtr);

	// 激活失败在这里是**常态**而非异常 —— 冷却中、耐力不足、被状态标签阻断、
	// GA 自己的 CanActivateAbility 返回 false 都会走到这里。
	// 所以保持 Verbose 级别，需要时用 `Log LogRPG_Ability Verbose` 打开。
	// （真正的失败原因由 GA 内部用 Warning 报出来，那才是需要定位的信息。）
	UE_LOG(LogRPG_Ability, Verbose, TEXT("按输入标签激活 %s：%s"),
		*InputTag.ToString(), bActivated ? TEXT("成功") : TEXT("失败（详见 GA 内部的日志）"));

	return bActivated;
}

bool URPG_AbilitySystemComponent::HasAbilityForInputTag(FGameplayTag InputTag) const
{
	return InputTagToSpecHandle.Contains(InputTag);
}

void URPG_AbilitySystemComponent::NotifyInputReleased(FGameplayTag InputTag)
{
	const FGameplayAbilitySpecHandle* HandlePtr = InputTagToSpecHandle.Find(InputTag);
	if (!HandlePtr || !HandlePtr->IsValid())
	{
		// 这个输入可能压根没绑能力。静默返回 —— 按下时已经警告过一次了，
		// 松开再报一遍只是噪音。
		return;
	}

	FGameplayAbilitySpec* Spec = FindAbilitySpecFromHandle(*HandlePtr);
	if (!Spec)
	{
		return;
	}

	// 取能力实例。
	// 注意 NonInstanced 的能力没有实例（直接在 CDO 上执行），拿不到可调用的对象。
	// 我们所有 GA 都是 InstancedPerActor，所以正常都能取到。
	UGameplayAbility* Ability = Spec->GetPrimaryInstance();
	if (!Ability)
	{
		return;
	}

	if (URPG_GameplayAbilityBase* RPGAbility = Cast<URPG_GameplayAbilityBase>(Ability))
	{
		// 瞬发能力对这个调用无感（基类默认空实现）
		RPGAbility->OnInputReleased();
	}
}
