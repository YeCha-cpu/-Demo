// Copyright Epic Games, Inc. All Rights Reserved.

#include "AbilitySystem/RPG_AbilitySystemComponent.h"

#include "Abilities/GameplayAbility.h"
#include "GameplayAbilitySpec.h"
#include "AbilitySystem/Abilities/RPG_GameplayAbilityBase.h"
#include "Core/RPG_LogChannels.h"

namespace
{
	/**
	 * 这个 ASC 当前跑在服务器还是客户端。**只用于日志措辞。**
	 *
	 * 为什么不用 `IsOwnerActorAuthoritative()`：它返回的是 `!bCachedIsNetSimulated`，
	 * 那是个在 `InitAbilityActorInfo` 时算出来并缓存的标志 ——
	 * 对日志来说这是一层不必要的间接，读代码的人还得再去查那个缓存是怎么算的。
	 *
	 * 也不用 `HasAuthority()`：那是 `AActor` 的方法，ASC 是**组件**，
	 * 根本没有这个函数（编译期就会报错）。
	 *
	 * 直接问"我的主人是不是权威"，语义最短，而且 `AActor::HasAuthority()`
	 * 是 `GetLocalRole() == ROLE_Authority`，是所有人都看得懂的那个判据。
	 */
	bool IsServerSideASC(const UAbilitySystemComponent& ASC)
	{
		const AActor* OwnerActor = ASC.GetOwnerActor();

		// 主人还没关联上时按"服务器"处理：这条只在 InitAbilityActorInfo
		// 之前才会发生，而那时候的日志本来就还没什么意义。
		return OwnerActor ? OwnerActor->HasAuthority() : true;
	}
}

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
	//
	// 引擎自己在 `OnRemoveAbility` 里对"没持锁就调"是**硬断言**
	// （`AbilitySystemComponent_Abilities.cpp:626` 的 ensureMsgf）；
	// `GiveAbility` 则宽容一些 —— 持锁时它会延迟到 `AbilityPendingAdds`
	// （同文件 :309-314），不持锁就自己加（:316）。
	// 遍历方主动持锁，才是和这两个写方都对称的写法。
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

void URPG_AbilitySystemComponent::RegisterInputAbilityMappings(
	const TMap<FGameplayTag, TSubclassOf<UGameplayAbility>>& InMappings)
{
	// 整表覆盖而不是逐条 Add：
	// 这个函数可能被多次调用（PossessedBy / OnRep_PlayerState / BeginPlay 三个时机
	// 都会走初始化），覆盖能保证"配置改了之后重新登记"是幂等的，
	// 不会留下上一次配置的残渣。
	InputTagToAbilityClass = InMappings;

	// 配置重新登记了 —— 之前那些"这个标签有问题"的去重标记全部作废。
	// 不清的话，改完配置也不会再收到提示（去重集合把新配置也一起挡了）。
	WarnedUnmappedTags.Reset();
	WarnedUnresolvedTags.Reset();

	// ── 重复类检查 ──
	// 两个不同的输入标签映射到**同一个 GA 类**时，服务器会授予两个独立 Spec，
	// 而客户端的索引重建是按类反查的（`FindAbilitySpecFromClass` 只返回第一个），
	// 于是两个标签都会指向同一个 Spec —— 表现是"按 A 键触发的是 B 键那个实例"。
	//
	// 现在不是问题（每个输入标签一个独立 GA 类），但这是个**静默**的坑：
	// 将来有人想让"轻击"和"重击"共用同一个 GA 类、靠参数区分时就会踩到。
	// 在配置登记这一步就报出来，比等到表现诡异时再查便宜得多。
	{
		TSet<TSubclassOf<UGameplayAbility>> SeenClasses;
		for (const TPair<FGameplayTag, TSubclassOf<UGameplayAbility>>& Pair : InMappings)
		{
			if (!Pair.Value)
			{
				continue;
			}

			bool bAlreadySeen = false;
			SeenClasses.Add(Pair.Value, &bAlreadySeen);

			if (bAlreadySeen)
			{
				UE_LOG(LogRPG_Ability, Warning,
					TEXT("输入标签 %s 与另一个标签映射到了同一个能力类 %s —— "
					     "客户端重建索引时按类反查只能找到其中一个，"
					     "会导致两个标签激活的是同一份能力实例"),
					*Pair.Key.ToString(), *Pair.Value->GetName());
			}
		}
	}

	UE_LOG(LogRPG_Ability, Log, TEXT("登记输入能力映射：%d 条（%s）"),
		InMappings.Num(), IsServerSideASC(*this) ? TEXT("服务器") : TEXT("客户端"));
}

void URPG_AbilitySystemComponent::GrantInputAbilities(int32 Level)
{
	// ⚠️ 遍历**副本**，不是 InputTagToAbilityClass 本身。
	//
	// 因为下面调的 RegisterInputAbility 里有一句
	// `InputTagToAbilityClass.Add(InputTag, AbilityClass)` ——
	// 在遍历一个容器的过程中往它里面写，就是迭代器失效的经典场景。
	//
	// 机制上要说准：UE 5.8 的 `TMap::Add` 走 `Emplace`，
	// 而默认的 `TSet` 现在是 `TSparseSet`，它的 `Emplace`
	// **先无条件 `AddUninitialized()` 一个新槽**，然后才去查重，
	// 命中已有键时把新值移动赋值过去再释放临时槽
	// （`SparseSet.h.inl` 的 Emplace）。
	// 也就是说键已存在时**不会重哈希**，但**确实会往元素存储里追加一次** ——
	// 那一下就可能触发扩容，让正在用的迭代器失效。
	//
	// 加上 5.8 的 range-for 迭代器是按下标取元素的（`Array[GetIndex()]`），
	// Realloc 之后继续遍历就是读已释放的内存。
	//
	// 结论：复制一份是几微秒的成本，换掉一整类"偶尔崩一次"的问题。
	TMap<FGameplayTag, TSubclassOf<UGameplayAbility>> MappingsToGrant = InputTagToAbilityClass;

	int32 GrantedCount = 0;
	for (const TPair<FGameplayTag, TSubclassOf<UGameplayAbility>>& Pair : MappingsToGrant)
	{
		if (RegisterInputAbility(Pair.Key, Pair.Value, Level))
		{
			++GrantedCount;
		}
	}

	UE_LOG(LogRPG_Ability, Log, TEXT("授予输入能力完成：%d/%d 条"),
		GrantedCount, MappingsToGrant.Num());
}

void URPG_AbilitySystemComponent::OnRep_ActivateAbilities()
{
	Super::OnRep_ActivateAbilities();

	// 能力列表复制到达（或又变了）—— 按类反查重建输入标签的索引。
	// 这是客户端唯一能拿到"输入标签 → SpecHandle"的时机，
	// 见 RebuildInputTagHandleMap 的说明。
	RebuildInputTagHandleMap();
}

bool URPG_AbilitySystemComponent::RebuildInputTagHandleMap()
{
	// ⚠️ 一个都没重建出来是**正常情况**，不是错误：
	//   · 映射表还没登记（初始化时序：能力 Spec 可能先于角色初始化复制到达）
	//   · 能力 Spec 还没复制到
	// 所以只在成功时打 Verbose，失败时不打任何东西 ——
	// 调用方（TryActivateAbilityByInputTag）会在真正需要时给出更有用的提示。
	if (InputTagToAbilityClass.IsEmpty())
	{
		return false;
	}

	// ⚠️ 不加 `FScopedAbilityListLock`，这是有意为之：
	//   · 引擎自己的 `FindAbilitySpecFromClass` 也不加锁
	//     （`AbilitySystemComponent_Abilities.cpp:988`），
	//     它是公开 API，调用方遍布引擎各处
	//   · 拿到的指针**立刻**就被消费掉了（只取 `Spec->Handle`），
	//     没有任何跨语句的持有
	//   · 引擎在"必须持锁"的地方会显式要求调用方传锁进去
	//     （见 `AbilitySystemComponent.h:1124` 的接口设计）
	// 本文件的 `ReactivatePassiveAbilities` 加锁，是因为它**要遍历整个数组**；
	// 这里只是单点查找，两者要求不同。

	// 建一张新表再整体换掉，而不是逐条 Add/覆盖。
	//
	// 为什么不在旧表上原地改：那样**永远删不掉过期的条目**。
	// 某个能力被撤销（或服务器重新授予、Handle 变了）之后，
	// 旧表里那条记录会一直留着 —— 而 `FGameplayAbilitySpecHandle::IsValid()`
	// 只判断"不等于 INDEX_NONE"，它**看不出这个 Handle 还有没有对应的能力**。
	// 于是缓存命中、`TryActivateAbility(Handle)` 在引擎内部找不到 Spec、
	// 静默失败，项目这边只留一条 Verbose 日志。
	//
	// 整体替换天然解决了这个问题：这次没找到对应 Spec 的标签，就不在新表里。
	TMap<FGameplayTag, FGameplayAbilitySpecHandle> Rebuilt;

	for (const TPair<FGameplayTag, TSubclassOf<UGameplayAbility>>& Pair : InputTagToAbilityClass)
	{
		if (!Pair.Value)
		{
			continue;
		}

		// 用引擎自己的按类查找，而不是自己遍历 ActivatableAbilities：
		// 它内部判空、比较的是 `Spec.Ability->GetClass()`，
		// 而客户端的 Spec.Ability 指向的就是复制过来的能力 CDO。
		//
		// ⚠️ 已知局限：它返回的是**第一个**类匹配的 Spec。
		// 如果两个不同的输入标签映射到**同一个 GA 类**，服务器上会授予两个
		// 独立的 Spec，而客户端这里会把两个标签都指向第一个 —— 于是其中一个
		// 标签激活的是"另一个标签的那份实例"。
		// 目前不是问题（每个输入标签对应一个独立的 GA 类），
		// 注册时有一道重复检查会警告这种配置，见 RegisterInputAbilityMappings。
		if (const FGameplayAbilitySpec* Spec = FindAbilitySpecFromClass(Pair.Value))
		{
			Rebuilt.Add(Pair.Key, Spec->Handle);
		}
	}

	const int32 RebuiltCount = Rebuilt.Num();
	InputTagToSpecHandle = MoveTemp(Rebuilt);

	if (RebuiltCount > 0)
	{
		UE_LOG(LogRPG_Ability, Verbose, TEXT("重建输入标签索引：%d 条"), RebuiltCount);
	}

	return RebuiltCount > 0;
}

bool URPG_AbilitySystemComponent::TryActivateAbilityByInputTag(FGameplayTag InputTag)
{
	FGameplayAbilitySpecHandle Handle;

	const FGameplayAbilitySpecHandle* Cached = InputTagToSpecHandle.Find(InputTag);
	if (Cached && Cached->IsValid())
	{
		Handle = *Cached;
	}
	else
	{
		// ══════════════════════════════════════════════════════════════
		//  缓存没命中 —— 分两种情况，不能混为一谈 ★
		// ══════════════════════════════════════════════════════════════
		//
		//  情况 A：映射表里**根本没有**这个输入标签
		//          → 配置错误。玩家按了一个没绑能力的键。
		//
		//  情况 B：映射表里有，只是 SpecHandle 还没拿到
		//          → 时序问题。客户端上能力是复制过来的，
		//            收到之前按下的那一两帧就会走到这里。
		//
		//  这两种的排查方向完全相反（改配置 vs 等复制），
		//  合成一条"没有绑定任何能力"的提示会把人带偏 ——
		//  这正是这个 bug 之前拖了那么久才被发现的原因之一。
		if (!InputTagToAbilityClass.Contains(InputTag))
		{
			// ── 情况 A：映射表里没有这个标签 ──
			//
			// ⚠️ 但"表里没有"有两种可能，不能一口咬定是配置错误：
			//   A1. 映射表**整张是空的** → 角色的 GAS 还没初始化完。
			//       `RPG_Player::InitializeAbilitySystem` 在 PlayerState 还没就绪时
			//       会直接返回等下个时机，这是**正常的启动时序**，不是配错。
			//   A2. 表里有别的标签，就是没有这一个 → 这才是真的没配。
			//
			// 一口咬定"请去 Startup Abilities 里加映射"，会把一个正常的启动竞争
			// 说成配置错误，把人往完全错的方向带。
			const bool bMappingsNotRegisteredYet = InputTagToAbilityClass.IsEmpty();

			if (!WarnedUnmappedTags.Contains(InputTag))
			{
				WarnedUnmappedTags.Add(InputTag);

				if (bMappingsNotRegisteredYet)
				{
					UE_LOG(LogRPG_Ability, Warning,
						TEXT("输入标签 %s 暂时无法激活：能力映射表还是空的 —— "
						     "角色的 GAS 尚未初始化完（通常是在等 PlayerState 复制）。"
						     "这是启动时序，稍后重按即可；如果一直这样，检查角色的"
						     "Startup Abilities 以及启动日志里有没有「登记输入能力映射」这行"),
						*InputTag.ToString());
				}
				else
				{
					UE_LOG(LogRPG_Ability, Warning,
						TEXT("输入标签 %s 没有绑定任何能力（映射表里已有 %d 条，不含这一条）—— "
						     "请在角色的 Startup Abilities 里加上这条映射"),
						*InputTag.ToString(), InputTagToAbilityClass.Num());
				}
			}

			return false;
		}

		// ── 情况 B：映射有，只是索引还没建起来 ──
		// 现场重建一次再试。
		//
		// 为什么值得在"按键"这条热路径上做：
		// 重建只在**缓存未命中**时才跑（命中时是一次 TMap 查找，成本可忽略），
		// 而未命中本身就是异常路径。真正常见的场景是"客户端第一次按键时
		// 能力刚复制到、但 OnRep 因为时序原因没排上队"，
		// 这时候重建一次就永久修好了 —— 比让玩家自己发现"等一秒再按"要好。
		RebuildInputTagHandleMap();

		const FGameplayAbilitySpecHandle* Retried = InputTagToSpecHandle.Find(InputTag);
		if (!Retried || !Retried->IsValid())
		{
			// 重建也拿不到 —— 能力确实还没复制到（或者服务器压根没授予）。
			// 这条提示指向的是**联机时序/授权**，不是配置。
			//
			// ⚠️ 用**另一个**去重集合（WarnedUnresolvedTags），不能和上面
			// "配置里没有"共用 —— 否则客户端进场时先报一次这个，那个标签
			// 就被永久标记了，之后真的配错也再不会报警。
			if (!WarnedUnresolvedTags.Contains(InputTag))
			{
				WarnedUnresolvedTags.Add(InputTag);

				UE_LOG(LogRPG_Ability, Warning,
					TEXT("输入标签 %s 已登记映射，但拿不到对应的能力实例 —— "
					     "通常是能力还没复制到（客户端刚进场，再按一次即可），"
					     "或者服务器根本没有授予它。运行在%s，当前已授予能力 %d 个"),
					*InputTag.ToString(),
					IsServerSideASC(*this) ? TEXT("服务器") : TEXT("客户端"),
					ActivatableAbilities.Items.Num());
			}

			return false;
		}

		Handle = *Retried;
	}

	// 用 Handle 而不是类去激活：同一个 GA 类可能被授予多次（不同来源/等级），
	// Handle 精确指向"这一次授予"，不会误激活另一个实例。
	const bool bActivated = TryActivateAbility(Handle);

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
	// 问的是"这个输入标签**配置了**能力吗"，所以查**映射表**，不是 Handle 缓存。
	//
	// 查缓存是错的（而且很容易写错）：客户端上 Handle 缓存要等能力 Spec
	// 复制到、且 `RebuildInputTagHandleMap` 跑过之后才有内容。
	// 在那之前查缓存会返回 false —— 但"配置了没有"和"索引建好没有"
	// 是两个问题，前者在两端、任何时刻答案都一样。
	return InputTagToAbilityClass.Contains(InputTag);
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
