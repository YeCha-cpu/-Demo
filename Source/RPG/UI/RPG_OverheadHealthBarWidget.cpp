// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/RPG_OverheadHealthBarWidget.h"

#include "AbilitySystemComponent.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "GameFramework/Pawn.h"

#include "AbilitySystem/RPG_AttributeSet.h"
#include "Character/RPG_BaseCharacter.h"
#include "Core/RPG_LogChannels.h"

void URPG_OverheadHealthBarWidget::SetOwningActor(AActor* InOwner)
{
	OwningActor = InOwner;

	// 一开始必须是收起来的 —— 常规状态下不显示血条。
	//
	// 放在这里（而不是只放 NativeConstruct）是因为两个调用时机不保证先后：
	// `UWidgetComponent::InitWidget()` 造出 Widget 后立刻就会调本函数，
	// 而 `NativeConstruct` 要等控件真正上屏才跑。只在一处收，
	// 另一处之前的那一两帧就会露出来。
	SetVisibility(ESlateVisibility::Collapsed);

	// 换主人了 → 之前记的血量作废，否则新主人的第一帧会被误判成"掉血了"
	LastSeenHealth = -1.f;

	// 先按新主人刷一次，让血条在**出生那一帧**就是对的。
	RefreshFromOwner();

	// 再立刻试着订阅（失败会自动挂上重试）。
	// 这一次多半是失败的 —— 组件 BeginPlay 早于角色的 GAS 初始化，
	// 此刻 ASC 还是空的。理由见 TryBindToOwnerASC 的说明。
	TryBindToOwnerASC();
}

UAbilitySystemComponent* URPG_OverheadHealthBarWidget::ResolveOwnerASC() const
{
	// ⚠️ **不要**用 GetOwningPlayerPawn()。
	//
	// UWidgetComponent 造 Widget 走的是 `CreateWidget(World, WidgetClass)`，
	// 那个重载把 Widget 的 PlayerContext 设成了**第一个本地玩家**
	// （UserWidget.cpp 里的 CreateWidgetInstance(UGameInstance&) → GetFirstGamePlayer），
	// Widget 本身还被 outer 到 GameInstance 上。
	//
	// 后果是每条敌人的血条都以为自己是玩家的血条 —— 显示玩家的血量、玩家的名字，
	// 而且完全不报错。所以主人只能由 URPG_OverheadHealthBarComponent 显式传进来。
	const AActor* Owner = OwningActor.Get();
	if (!Owner)
	{
		return nullptr;
	}

	// 角色基类实现了 IAbilitySystemInterface，
	// 它会把"玩家去 PlayerState 找、敌人从自己身上拿"这个差异抹平。
	if (const ARPG_BaseCharacter* Character = Cast<ARPG_BaseCharacter>(Owner))
	{
		return Character->GetAbilitySystemComponent();
	}

	return nullptr;
}

const URPG_AttributeSet* URPG_OverheadHealthBarWidget::ResolveOwnerAttributeSet() const
{
	// 统一从"主人现在是谁"这条路径去取，**不看订阅状态**。
	//
	// 之前这里是分叉的：CanBeRevealed() 只认 BoundASC，
	// 而 RefreshBarValues() 会在 BoundASC 为空时退回 ResolveOwnerASC()。
	// 两个判定于是可能不一致 —— 在"属性集拿得到、但还没订阅上"的窗口里，
	// RefreshBarValues 判出"掉血了"，CanBeRevealed 却因为 BoundASC 为空而拒绝亮起。
	// 那种窗口很短，但正是敌人刚出生就被打的时刻。
	// 统一走这一条路，两边就不可能打架。
	UAbilitySystemComponent* ASC = BoundASC.IsValid() ? BoundASC.Get() : ResolveOwnerASC();

	return ASC ? ASC->GetSet<URPG_AttributeSet>() : nullptr;
}

void URPG_OverheadHealthBarWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// 常规状态不显示 —— 详见头文件里"挨打才亮"的说明。
	SetVisibility(ESlateVisibility::Collapsed);

	// ★ `NativeConstruct` 是**会跑第二遍**的，不是"每个 Widget 只跑一次"。
	//
	// 组件不可见时 `UWidgetComponent::UpdateWidget()` 会
	// `RemoveWidgetFromScreen()` → Slate 对象析构 → `NativeDestruct()`；
	// 恢复可见时重新 `TakeWidget()` → 控件被**重建** → 再跑一次
	// `NativePreConstruct` + `NativeConstruct`（Widget.cpp 的 OnWidgetRebuild 路径）。
	//
	// 所以这里必须把血量基线清掉。不清的话：组件隐藏期间血量掉了，
	// 重建时 `RefreshBarValues` 拿旧基线和当前血量一比 → 判成"掉血了" →
	// 血条**凭空亮一下**。
	// `SetOwningActor` 里对同一类误判是显式防了的，这里原本漏了，两处语义不一致。
	LastSeenHealth = -1.f;

	// 先刷一次。属性委托只在**变化时**触发，
	// 而血条出现的那一刻（比如敌人刚进入视野）往往没有变化发生 ——
	// 不主动读一次的话，满血的敌人挨第一刀时亮出来的会是空条。
	RefreshFromOwner();

	// 订阅（多半要等重试，理由见 TryBindToOwnerASC）
	TryBindToOwnerASC();
}

bool URPG_OverheadHealthBarWidget::CanBeRevealed() const
{
	// 死了就不再亮。
	//
	// 少了这一条，尸体会在"血量归零"时亮一次然后挂满 5 秒 ——
	// 对着一具躺地的尸体显示一根空血条，既没意义又难看。
	//
	// 用血量判而不是查 State.Dead：血条本来就只关心血量，
	// 多引一个标签依赖不值得；而且"血量归零"和"死亡"在本项目里是同一件事
	// （GA_Death 就是被血量归零触发的）。
	const URPG_AttributeSet* AttributeSet = ResolveOwnerAttributeSet();

	return AttributeSet && AttributeSet->GetHealth() > 0.f;
}

void URPG_OverheadHealthBarWidget::RevealForDamage()
{
	if (!CanBeRevealed())
	{
		// 这里是个**静默**分支，专门打一条日志。
		// 不然"该亮没亮"和"压根没走到这儿"在日志上分不开。
		UE_LOG(LogRPG_Combat, Verbose,
			TEXT("[%s] 头顶血条该亮但被拦下（血量已 ≤ 0 或属性集读不到）"),
			*GetNameSafe(OwningActor.Get()));
		return;
	}

	// ★ 边沿检测：已经亮着就只重置计时，**不重发通知**。
	//
	// 少了这道检测，5 秒窗口内每挨一下 `BP_OnRevealChanged(true)` 都会重播一次，
	// WBP 那边的淡入动画被反复重启 —— 观感是"闪"。
	// 本文件末尾对 `BP_OnLowHealth` 是特意做了边沿检测的
	// （注释原话："每帧无脑调的话，WBP 里的闪烁动画会被反复重启 —— 看起来像在抽搐"），
	// 这里原本漏了同一条纪律。
	const bool bWasRevealed = (GetVisibility() == ESlateVisibility::HitTestInvisible);

	// HitTestInvisible 而不是 Visible：
	// 血条是纯展示，不该吃掉鼠标事件（将来加"点击选中敌人"时不希望被它挡住）。
	SetVisibility(ESlateVisibility::HitTestInvisible);

	if (!bWasRevealed)
	{
		BP_OnRevealChanged(true);
	}

	// ⚠️ 用 TimerManager 而不是自己 tick 计时。
	//
	// 三个理由：
	//   ① 控件被隐藏（Collapsed）之后，UMG 的 tick 不保证还会跑 ——
	//      用 tick 计时的话，收回的那一刻计时就死了，再也不会亮第二次
	//   ② 一次挨打只需要一个 5 秒后的回调，为此每帧跑一次是纯浪费
	//   ③ 定时器跟着 World 走，关卡切换/暂停时的行为是引擎定好的
	//
	// 同一个 handle 再 SetTimer 会**替换**掉上一个 ——
	// 这正是我们要的"连续挨打刷新计时，而不是叠加成 10 秒"。
	// ⚠️ 时长为 0（或负）时**必须**提前返回，否则血条会亮起来再也不收回。
	//
	// `meta = (ClampMin = "0.1")` 只约束编辑器输入框；运行时被设成 0 的话，
	// `FTimerManager::InternalSetTimer` 在 `InRate > 0.f` 为假时
	// **只清掉旧定时器、不建新的**（TimerManager.cpp），于是 HideAfterDamage 永不触发。
	if (RevealDuration <= 0.f)
	{
		return;
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			HideTimerHandle, this, &URPG_OverheadHealthBarWidget::HideAfterDamage,
			RevealDuration, /*bLoop=*/false);
	}
}

void URPG_OverheadHealthBarWidget::HideAfterDamage()
{
	// 角色在亮着的这 5 秒里死了 → 直接收掉，不用等下次判定
	SetVisibility(ESlateVisibility::Collapsed);
	BP_OnRevealChanged(false);
}

void URPG_OverheadHealthBarWidget::NativeDestruct()
{
	// 定时器必须先清掉。
	// 不清的话，控件销毁后那个 5 秒的回调还会到点触发一次 ——
	// UObject 委托对已销毁的对象会安全跳过（不会崩），但那是靠引擎兜底；
	// 而且如果 World 还在（换关卡时），它会一直挂在 TimerManager 里。
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(HideTimerHandle);

		// 重试定时器也要清。
		// 不清的话它是个**循环**定时器，会一直挂着直到 ASC 出现 ——
		// 而控件已经没了，白跑一辈子。
		World->GetTimerManager().ClearTimer(BindRetryHandle);
	}

	// ★ 解绑必须成对。
	// 不解的话，角色销毁后 ASC 上的委托还指着这个 Widget ——
	// UObject 委托对已 GC 的对象会安全跳过，但 handle 会一直堆积；
	// 更要紧的是"Widget 被复用（List View 之类）后重复 AddUObject"，
	// 那会让同一个回调被调两次。
	if (UAbilitySystemComponent* ASC = BoundASC.Get())
	{
		if (HealthChangedHandle.IsValid())
		{
			ASC->GetGameplayAttributeValueChangeDelegate(URPG_AttributeSet::GetHealthAttribute())
				.Remove(HealthChangedHandle);
		}
		if (MaxHealthChangedHandle.IsValid())
		{
			ASC->GetGameplayAttributeValueChangeDelegate(URPG_AttributeSet::GetMaxHealthAttribute())
				.Remove(MaxHealthChangedHandle);
		}
	}
	HealthChangedHandle.Reset();
	MaxHealthChangedHandle.Reset();
	BoundASC.Reset();

	Super::NativeDestruct();
}

void URPG_OverheadHealthBarWidget::TryBindToOwnerASC()
{
	UWorld* World = GetWorld();
	UAbilitySystemComponent* ASC = ResolveOwnerASC();

	// ══════════════════════════════════════════════════════════════════
	//  情形 1：还没就位 —— 挂上重试，等它出现 ★
	// ══════════════════════════════════════════════════════════════════
	// ⚠️⚠️ 这一段**必须**排在下面"主人没变就直接返回"的**前面**。顺序反了就是 bug。
	//
	// 之前是反的：用一个 `ASC == BoundASC.Get()` 同时表达"已经订上了"和
	// "还没拿到"。而这两件事在**初始状态**下值是一样的 —— 两边都是 null，
	// 判断成立，于是第一次调用就认定"已接上"并 return，
	// **重试定时器压根没被挂起来过**，重试逻辑是死代码。
	//
	// 后果："能不能订上"完全取决于 Widget 被构造的那一刻 ASC 在不在：
	//   · 敌人  —— ASC 在自己身上，本地一构造就有        → 订得上
	//   · 玩家  —— ASC 在 PlayerState 上，要等复制到达   → **订不上**
	// 于是表现为"客户端看 AI 有血条、看玩家没有"。
	// 而两次的日志长得一模一样，从日志上分不出区别。
	//
	// 教训：**用一个条件同时表达两种状态时，先问"它们在初始值上会不会撞车"。**
	// null == null 这种撞车在指针判等里是最常见的一种。
	//
	// ── 就位判定：ASC 和属性集**都**要有 ──
	//
	// ⚠️ 只判 ASC 是不够的。敌人的 ASC 是 CreateDefaultSubobject 建的，
	// 组件 BeginPlay 那一刻就已经存在；但它的**属性集**是在
	// ARPG_Enemy::InitializeAbilitySystem() 里才 AddSpawnedAttribute 的，
	// 而那要等角色的 BeginPlay —— **在组件 BeginPlay 之后**。
	//
	// 所以"ASC 在、属性集还没登记"是**每个敌人必经的中间态**，不是故障。
	// （上一版在这里直接判"订上了但没属性集"并打 Warning，结果是每刷一个敌人
	//   就报一次"属性集没复制过来" —— 内容还是错的，非常误导。）
	const URPG_AttributeSet* ReadySet = ASC ? ASC->GetSet<URPG_AttributeSet>() : nullptr;

	if (!ASC || !ReadySet)
	{
		++BindAttemptCount;

		// 试了足够多次**仍然**只有 ASC、没有属性集 → 这才是真故障。
		// 用"重试次数"而不是"是否为空"来判定，就是为了把上面那个
		// 必经的中间态和真故障分开。
		if (ASC && BindAttemptCount == AttributeSetWarnAfterAttempts)
		{
			UE_LOG(LogRPG_Combat, Warning,
				TEXT("[%s] 头顶血条：ASC（%s，挂在 %s 上）已就位 %d 次重试后**仍然拿不到 URPG_AttributeSet** —— "
				     "血条会一直是空的、挨打也不会亮。检查 %s 上的 AddSpawnedAttribute "
				     "和 SpawnedAttributes 的复制"),
				*GetNameSafe(OwningActor.Get()), *ASC->GetName(), *GetNameSafe(ASC->GetOwnerActor()),
				BindAttemptCount, *GetNameSafe(ASC->GetOwnerActor()));
		}

		if (World && !World->GetTimerManager().IsTimerActive(BindRetryHandle))
		{
			World->GetTimerManager().SetTimer(
				BindRetryHandle, this, &URPG_OverheadHealthBarWidget::TryBindToOwnerASC,
				BindRetryInterval, /*bLoop=*/true);
		}
		return;
	}

	// ── 情形 2：主人没变，已经订上了，不用重复绑 ──
	// 重复 AddUObject 会让同一个回调被调两次，表现是血条"莫名跳两下"。
	if (ASC == BoundASC.Get())
	{
		if (World)
		{
			World->GetTimerManager().ClearTimer(BindRetryHandle);
		}
		return;
	}

	// 主人换了（或第一次拿到）—— 先摘掉旧的再绑新的
	if (UAbilitySystemComponent* OldASC = BoundASC.Get())
	{
		if (HealthChangedHandle.IsValid())
		{
			OldASC->GetGameplayAttributeValueChangeDelegate(URPG_AttributeSet::GetHealthAttribute())
				.Remove(HealthChangedHandle);
		}
		if (MaxHealthChangedHandle.IsValid())
		{
			OldASC->GetGameplayAttributeValueChangeDelegate(URPG_AttributeSet::GetMaxHealthAttribute())
				.Remove(MaxHealthChangedHandle);
		}
		HealthChangedHandle.Reset();
		MaxHealthChangedHandle.Reset();
	}
	BoundASC.Reset();

	// 走到这里 ASC 一定非空 —— 空的那条已经在开头返回了（见"情形 1"）。
	// 这就是为什么那个 `if (!ASC)` 必须排在最前面：
	// 放在这里的话，初始状态会被上面的判等提前吃掉，永远走不到。
	BoundASC = ASC;

	// 引擎自带的属性变化委托，**非动态多播**，所以用 AddUObject 而不是 AddDynamic。
	// 它在两个时机被触发：
	//   ① 服务器上属性被 GE 修改时
	//   ② 客户端上 GAMEPLAYATTRIBUTE_REPNOTIFY 把复制来的值写进属性时
	// 后者正是客户端血条能跟着动的原因。
	HealthChangedHandle = ASC
		->GetGameplayAttributeValueChangeDelegate(URPG_AttributeSet::GetHealthAttribute())
		.AddUObject(this, &URPG_OverheadHealthBarWidget::HandleHealthChanged);

	// ⚠️ 上限也要监听。
	// 吃了个加生命上限的 Buff，**当前血量一点没变**，
	// 但血条该变短 —— 因为"满血"的定义变了。
	// 只监听 Health 的话，表现是"上限涨了但条没动，要挨一刀才对得齐"。
	// （URPG_HUDWidget 里对玩家那三条血条做了同样的事。）
	MaxHealthChangedHandle = ASC
		->GetGameplayAttributeValueChangeDelegate(URPG_AttributeSet::GetMaxHealthAttribute())
		.AddUObject(this, &URPG_OverheadHealthBarWidget::HandleHealthChanged);

	// 接上了就不用再试
	if (World)
	{
		World->GetTimerManager().ClearTimer(BindRetryHandle);
	}
	BindAttemptCount = 0;

	// 走到这里 ASC 和属性集**都**有了（"情形 1"已经把缺任何一个的挡在外面），
	// 所以这条日志是真的"链路通了"，可以直接拿它和游戏里的血量对数。
	UE_LOG(LogRPG_Combat, Log,
		TEXT("[%s] 头顶血条已接上（ASC 在 %s 上），当前血量 %.1f"),
		*GetNameSafe(OwningActor.Get()), *GetNameSafe(ASC->GetOwnerActor()),
		ReadySet->GetHealth());

	// 刚接上，立刻读一次当前值
	RefreshFromOwner();
}

void URPG_OverheadHealthBarWidget::HandleHealthChanged(const FOnAttributeChangeData& Data)
{
	// 委托本身只带了"哪一个属性、新旧值是多少"，够不够用取决于业务。
	// 血条的比例是 Health / MaxHealth 两个属性一起决定的，
	// 而委托一次只送一个 —— 所以统一去属性集读当前值，不看 Data。
	// （也因此 Health 和 MaxHealth 可以共用这一个回调。）
	RefreshBarValues();
}

void URPG_OverheadHealthBarWidget::RefreshBarValues()
{
	// ⚠️ 这里**不写 BoundASC**。
	// 订阅状态只有 TryBindToOwnerASC 一处负责 —— 如果这里顺手把 BoundASC 也设了，
	// 那边的 `ASC != BoundASC.Get()` 判定就会认为"已经接上了"，
	// 于是**永远不绑委托**：血条初始值是对的，但之后再也不更新。
	// 那种 bug 看起来像"血条卡住不动"，很难往订阅逻辑上想。
	const URPG_AttributeSet* AttributeSet = ResolveOwnerAttributeSet();

	if (!AttributeSet)
	{
		return;
	}

	const float Health = AttributeSet->GetHealth();
	const float MaxHealth = AttributeSet->GetMaxHealth();

	// ══════════════════════════════════════════════════════════════════
	//  ★ 掉血检测 —— "挨打才亮"的触发点
	// ══════════════════════════════════════════════════════════════════
	// 用**自己缓存的上一次血量**做比较，而不是用 FOnAttributeChangeData
	// 里的 OldValue。原因：
	//   · OldValue 在"客户端收到复制"这条路径上不一定被填
	//   · 而且这个函数还有第二个入口（刚接上 ASC 时主动读一次），
	//     那时候压根没有 Data 可看
	// 自己存一份，"从哪进来的"就都不影响了。
	//
	// 用 KINDA_SMALL_NUMBER 做容差而不是直接 `<`：
	// 浮点数的相等比较在本项目里已经踩过坑（属性钳制的那些），
	// 而且血量小幅震动不应该触发亮起。
	const bool bWasInitialized = LastSeenHealth >= 0.f;
	const bool bLostHealth = bWasInitialized && (Health < LastSeenHealth - KINDA_SMALL_NUMBER);

	if (bLostHealth)
	{
		// Verbose：只在排查"挨打亮不亮"时开（`Log LogRPG_Combat Verbose`）。
		// 记下**变化前后**两个值 —— 只看当前值判断不出是"没掉血"还是"没收到"。
		UE_LOG(LogRPG_Combat, Verbose, TEXT("[%s] 头顶血条：检测到掉血 %.1f → %.1f"),
			*GetNameSafe(OwningActor.Get()), LastSeenHealth, Health);
	}

	LastSeenHealth = Health;

	if (bLostHealth)
	{
		RevealForDamage();
	}

	// MaxHealth 为 0 的窗口期（属性集还没被 InitAttributesEffect 初始化）
	// 必须挡掉 —— 除零得到的 NaN 塞进 ProgressBar 会让血条整个不显示，
	// 看起来像"UI 没接上"，很容易往错的方向查。
	const float Percent = MaxHealth > KINDA_SMALL_NUMBER
		? FMath::Clamp(Health / MaxHealth, 0.f, 1.f)
		: 0.f;

	if (HealthBar)
	{
		HealthBar->SetPercent(Percent);
	}

	// 边沿检测：只在"跨过阈值"的那一次通知蓝图。
	// 每帧无脑调的话，WBP 里的闪烁动画会被反复重启 —— 看起来像在抽搐。
	const bool bIsLow = Percent > 0.f && Percent <= LowHealthThreshold;
	if (bIsLow != bWasLowHealth)
	{
		bWasLowHealth = bIsLow;
		BP_OnLowHealth(bIsLow);
	}
}

void URPG_OverheadHealthBarWidget::RefreshFromOwner()
{
	RefreshBarValues();

	// 名字文本：只有配了才刷。
	//
	// ⚠️ 用 OwningActor 而不是 GetOwningPlayerPawn() ——
	// 后者返回的是**本地玩家**，会让每个敌人的血条都顶着玩家的名字。
	// 原因见 RPG_OverheadHealthBarComponent.h。
	if (NameText)
	{
		if (const AActor* Owner = OwningActor.Get())
		{
			NameText->SetText(FText::FromString(Owner->GetName()));
		}
	}
}
