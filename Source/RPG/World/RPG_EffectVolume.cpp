// Copyright Epic Games, Inc. All Rights Reserved.

#include "World/RPG_EffectVolume.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameplayEffect.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

#include "AbilitySystem/Abilities/RPG_GameplayAbilityBase.h"
#include "Character/RPG_BaseCharacter.h"
#include "Core/RPG_LogChannels.h"

ARPG_EffectVolume::ARPG_EffectVolume()
{
	// 效果要复制（bConsumed），所以 Actor 本身必须复制。
	// 少了这一句，"客户端捡了药水、服务器上还在" 或者反过来 ——
	// 而且不报错，只是表现和逻辑对不上。
	bReplicates = true;

	// ── 碰撞球（根组件）──
	TriggerSphere = CreateDefaultSubobject<USphereComponent>(TEXT("TriggerSphere"));
	SetRootComponent(TriggerSphere);
	TriggerSphere->InitSphereRadius(100.f);

	// 碰撞配置**全部显式写出来**，不依赖引擎的默认 profile。
	//
	// 依赖默认值是这个项目反复踩的一类坑（比如 InstancingPolicy、
	// PlayerState 的复制频率）：引擎改默认值，代码就坏，而且不报错。
	// 这里想要的是非常明确的一件事：**只产生重叠事件，不阻挡任何人**。
	TriggerSphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	TriggerSphere->SetCollisionObjectType(ECC_WorldDynamic);
	TriggerSphere->SetCollisionResponseToAllChannels(ECR_Ignore);
	TriggerSphere->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	TriggerSphere->SetGenerateOverlapEvents(true);

	// ── 外观 ──
	// 挂在碰撞球下面，不抢根组件的位子 —— 理由见头文件。
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(TriggerSphere);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetGenerateOverlapEvents(false);
}

void ARPG_EffectVolume::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// COND_None：所有客户端都要知道"这个东西被用掉了"，才能一起隐藏它。
	// 用 COND_OwnerOnly 的话只有拾取者自己看得见消失，别人屏幕上它还杵在那儿。
	DOREPLIFETIME_CONDITION(ARPG_EffectVolume, bConsumed, COND_None);
}

void ARPG_EffectVolume::BeginPlay()
{
	Super::BeginPlay();

	if (!TriggerSphere)
	{
		return;
	}

	TriggerSphere->OnComponentBeginOverlap.AddDynamic(this, &ARPG_EffectVolume::OnSphereBeginOverlap);

	// 先按当前状态刷一次 —— 联机下 bConsumed 可能在 BeginPlay 之前就已经复制过来了
	// （比如中途加入的客户端，看到的已经是"被捡走"的状态）。
	RefreshVisualState();

	// ── 周期模式：只有服务器需要定时器 ──
	// 客户端不施加任何效果，给它挂定时器纯属浪费。
	if (HasAuthority() && TriggerMode == ERPG_EffectTriggerMode::WhileInside)
	{
		GetWorldTimerManager().SetTimer(
			RepeatTimerHandle, this, &ARPG_EffectVolume::ApplyToAllOverlapping,
			FMath::Max(RepeatInterval, 0.05f), /*bLoop=*/true);
	}

	// ── 配置体检 ──
	// "TriggerEvent 没配"是一个**完全不报错**的失败：触发器在那儿、能碰到、
	// 就是什么都不会发生。值得在生成时就喊一声。
	if (!TriggerEvent.IsValid())
	{
		UE_LOG(LogRPG_Combat, Warning,
			TEXT("[%s] 效果触发器没有配置 TriggerEvent —— 它不会产生任何效果。"
			     "请在 BP 的 Class Defaults 里选一个 Event.Item.* 标签"),
			*GetName());
	}
}

void ARPG_EffectVolume::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 两个定时器都要清。不清的话，关卡切换后它们还会挂在 TimerManager 上 ——
	// 虽然对象销毁时引擎会兜住，但那是靠兜底而不是靠我们自己说清楚。
	GetWorldTimerManager().ClearTimer(RepeatTimerHandle);
	GetWorldTimerManager().ClearTimer(RespawnTimerHandle);

	if (TriggerSphere)
	{
		TriggerSphere->OnComponentBeginOverlap.RemoveDynamic(this, &ARPG_EffectVolume::OnSphereBeginOverlap);
	}

	Super::EndPlay(EndPlayReason);
}

void ARPG_EffectVolume::OnSphereBeginOverlap(
	UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex,
	bool bFromSweep, const FHitResult& SweepResult)
{
	// ⚠️ 这道守卫必须写在最前面。
	//
	// 重叠事件在**服务器和每个客户端上都会触发**（碰撞是各端各自算的），
	// 不能靠"客户端不会走到这里"来省 —— 那样会在每台机器上各施加一次效果，
	// 表现是治疗量翻倍、Buff 层数 N 倍，而且只在联机时出现。
	if (!HasAuthority())
	{
		return;
	}

	// `WhileInside` 模式由定时器统一处理，不在这里响应 ——
	// 否则"进圈那一下"会被立即触发一次，和周期触发叠加，
	// 表现是刚进圈就掉两次血。
	if (TriggerMode == ERPG_EffectTriggerMode::WhileInside)
	{
		return;
	}

	if (TargetMode == ERPG_EffectTargetMode::AllOverlapping)
	{
		// 一次性陷阱：范围内所有人一起吃
		ApplyToAllOverlapping();
	}
	else if (ARPG_BaseCharacter* Character = Cast<ARPG_BaseCharacter>(OtherActor))
	{
		ApplyEffectTo(Character);
	}

	// ── 用掉 ──
	// 放在施加之后：万一施加流程将来加了"条件不满足就不消耗"的逻辑，
	// 这个顺序不用改。
	if (bConsumeOnTrigger && !bConsumed)
	{
		ConsumeVolume();
	}
}

void ARPG_EffectVolume::ApplyToAllOverlapping()
{
	if (!HasAuthority() || !TriggerSphere)
	{
		return;
	}

	// 每次现查，不维护"谁在里面"的列表。
	//
	// 维护列表要多处理进入 / 离开 / 销毁三条路径，而且任一条漏了就是
	// "人走了还在被扣血"这类难查的问题。查一次的开销是遍历一小撮重叠对象，
	// 对"每隔一秒一次"的频率完全不值得优化。
	TArray<AActor*> Overlapping;
	TriggerSphere->GetOverlappingActors(Overlapping, ARPG_BaseCharacter::StaticClass());

	for (AActor* Actor : Overlapping)
	{
		if (ARPG_BaseCharacter* Character = Cast<ARPG_BaseCharacter>(Actor))
		{
			ApplyEffectTo(Character);
		}
	}
}

void ARPG_EffectVolume::ApplyEffectTo(ARPG_BaseCharacter* Character)
{
	if (!HasAuthority() || !CanAffect(Character))
	{
		return;
	}

	UAbilitySystemComponent* ASC = Character->GetAbilitySystemComponent();
	if (!ASC)
	{
		return;
	}

	if (!TriggerEvent.IsValid())
	{
		// BeginPlay 里已经警告过一次了，这里不再刷屏。
		return;
	}

	// ── 诊断：真的有人会响应这个事件吗？★ ──
	//
	// GAS 对"事件发出去没有人接"是**完全沉默**的 —— 没有回调、没有日志、
	// 没有警告。表现就是玩家走过去、东西还在、什么都没发生，
	// 而日志一片空白。这是本项目反复出现的一类故障。
	//
	// 所以这里主动查一遍：目标身上有没有哪个能力的 AbilityTriggers 匹配这个标签。
	// 只在真的没人接时才打，正常游戏不会刷屏。
	{
		bool bHasResponder = false;

		// 遍历 ActivatableAbilities 必须持锁 —— 引擎对"遍历中列表被改"
		// 有 ensure 检查（GiveAbility / OnRemoveAbility 都要求持锁）。
		//
		// 问能力的方式是调 `RespondsToGameplayEvent`，而不是自己去读
		// `AbilityTriggers` —— 那是 protected 的，外部读不到。
		FScopedAbilityListLock AbilityListLock(*ASC);
		for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
		{
			const URPG_GameplayAbilityBase* RPGAbility =
				Cast<URPG_GameplayAbilityBase>(Spec.Ability);

			if (RPGAbility && RPGAbility->RespondsToGameplayEvent(TriggerEvent))
			{
				bHasResponder = true;
				break;
			}
		}

		if (!bHasResponder)
		{
			UE_LOG(LogRPG_Ability, Warning,
				TEXT("[%s] 给 [%s] 发了事件 %s，但他身上**没有任何能力会响应它** —— 这次效果不会发生。"
				     "检查两件事：① 目标角色有没有把对应的 GA 加进 Startup Abilities；"
				     "② 那个 GA 的 Class Defaults → Triggers 里有没有配同一个标签"),
				*GetName(), *GetNameSafe(Character), *TriggerEvent.ToString());
			return;
		}
	}

	FGameplayEventData Payload;
	Payload.EventTag = TriggerEvent;

	// Instigator 指向**这个触发器**而不是某个角色 —— 语义上是"效果来自这口泉水"，
	// 将来要区分"被陷阱打的"和"被敌人打的"就靠它。
	Payload.Instigator = this;
	Payload.Target = Character;

	// 数值（治疗量 / 强度）。0 = 用 GA 自己配的默认值。
	Payload.EventMagnitude = Magnitude;

	// ★ 把 GE 塞进 OptionalObject。
	//
	// 这是本类唯一一处"不太优雅"的地方：`OptionalObject` 是 `UObject*`，
	// 装的是个 `UClass`，类型信息丢了。GA 那边会 `Cast<UClass>` 再校验它是不是
	// `UGameplayEffect` 的子类。
	//
	// 为什么不这么做就得耦合：让 GA 反过来去 `Cast<ARPG_EffectVolume>(Instigator)`
	// 读配置的话，AbilitySystem 层就依赖了 World 层；而为一个字段建一个
	// Payload UObject 又太重（每次触发都要 NewObject）。
	// 目前效果源只有这一个类，所以选了最轻的方案。
	// **如果将来效果源种类变多（法术、陷阱、光环各一套），就该抽接口了。**
	if (EffectClass)
	{
		Payload.OptionalObject = EffectClass;
	}

	UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(Character, TriggerEvent, Payload);

	UE_LOG(LogRPG_Combat, Verbose,
		TEXT("[%s] 对 [%s] 触发效果 %s（GE=%s，数值=%.1f）"),
		*GetName(), *GetNameSafe(Character), *TriggerEvent.ToString(),
		*GetNameSafe(EffectClass), Magnitude);
}

bool ARPG_EffectVolume::CanAffect(const ARPG_BaseCharacter* Character) const
{
	if (!Character)
	{
		return false;
	}

	// 已死的角色默认不吃效果。
	// 但毒池 / 熔岩那类"环境伤害"通常希望尸体也泡在里面（也吃），
	// 所以做成可配的 bAffectDead。
	if (!bAffectDead && !Character->IsAlive())
	{
		return false;
	}

	// 已经用掉的不再作用。理论上 ConsumeVolume 会同时关掉碰撞，
	// 但"关碰撞"和"重叠事件不再进来"之间隔着引擎的一帧，
	// 这里再判一次是防御性的 —— 一帧里重复触发会让治疗量翻倍。
	return !bConsumed;
}

void ARPG_EffectVolume::ConsumeVolume()
{
	if (!HasAuthority() || bConsumed)
	{
		return;
	}

	bConsumed = true;

	// ⚠️ 服务器上设 bConsumed **不会**触发 OnRep（OnRep 只在客户端收到复制时调），
	// 所以这里要自己刷一次表现 —— 否则服务器上（也就是 Listen Server 的主机）
	// 会看到一个已经被捡走的药水还杵在地上。
	RefreshVisualState();

	UE_LOG(LogRPG_Combat, Log, TEXT("[%s] 已使用%s"),
		*GetName(),
		RespawnDelay > 0.f ? *FString::Printf(TEXT("，%.1f 秒后重生"), RespawnDelay) : TEXT(""));

	if (RespawnDelay > 0.f)
	{
		GetWorldTimerManager().SetTimer(
			RespawnTimerHandle, this, &ARPG_EffectVolume::RespawnVolume,
			RespawnDelay, /*bLoop=*/false);
	}
}

void ARPG_EffectVolume::RespawnVolume()
{
	if (!HasAuthority())
	{
		return;
	}

	bConsumed = false;
	RefreshVisualState();
}

void ARPG_EffectVolume::OnRep_bConsumed()
{
	// 客户端：服务器说用掉了，跟着隐藏。
	RefreshVisualState();
}

void ARPG_EffectVolume::RefreshVisualState()
{
	// 表现与碰撞的开关**只在这一处**。
	// 拆成两处写迟早会不一致 —— 比如"隐藏了但还能碰到"，
	// 表现是空气墙：走过去什么都没有，但治疗照常发生。
	const bool bVisible = !bConsumed;

	if (Mesh)
	{
		Mesh->SetVisibility(bVisible, /*bPropagateToChildren=*/true);
	}

	if (TriggerSphere)
	{
		TriggerSphere->SetCollisionEnabled(
			bVisible ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
	}
}
