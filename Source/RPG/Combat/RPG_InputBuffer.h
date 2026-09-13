// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Combat/RPG_CombatTypes.h"
#include "RPG_InputBuffer.generated.h"

/**
 * 输入缓存容器 —— 动作游戏"跟手"手感的关键组件。
 *
 * ══════════════════════════════════════════════════════════════════════
 * 【它解决什么问题】
 * ══════════════════════════════════════════════════════════════════════
 * 玩家打出第 1 段轻击后，动画还有 0.6 秒才结束。这时候玩家按第 2 下：
 *
 *   没有缓存     → 按键被丢弃，玩家必须等动画播完再按，感觉"不跟手"
 *   有缓存       → 按键记下来，等衔接窗口一开就立刻打出第 2 段，连招流畅
 *
 * 【黑神话】、【只狼】这类游戏的手感，很大一部分来自这套机制。
 *
 * ══════════════════════════════════════════════════════════════════════
 * 【三个必须处理好的细节】
 * ══════════════════════════════════════════════════════════════════════
 *
 * 1. 生命周期（过期作废）
 *    玩家在硬直里乱按的按键不能无限期保留，否则硬直一结束会突然打出一串。
 *    每个条目带时间戳，超过 LifeTime 就作废。推荐 0.4~0.6 秒。
 *
 * 2. 容量上限（丢弃最旧）
 *    疯狂连打时不能无限堆积。满了就丢最旧的 —— 因为玩家**最新**按下的
 *    才代表当前意图，丢旧输入比拒绝新输入手感更好。
 *
 * 3. 惰性清理（不开 Timer）
 *    容器只在 Push / Consume 两个时刻被访问，所以在这两处【顺带清理】即可。
 *    不需要额外的 Timer 或 Tick —— 那纯属浪费，而且会引入"清理时机不可控"的问题。
 *
 * ══════════════════════════════════════════════════════════════════════
 * 【联机说明】
 * ══════════════════════════════════════════════════════════════════════
 * 输入缓存本质上属于**客户端本地**：服务器上没有"玩家按键"这件事，
 * 它只收到"请求激活能力 X"。所以：
 *   · 客户端用自己的缓存决定"下一段打什么"
 *   · 服务器直接执行被请求的能力，不读缓存
 *   · 连段索引（ComboIndex）两边各自维护，逻辑相同所以结果一致
 *
 * MVP 阶段不处理预测不一致的深度问题（那需要 PredictionKey 级别的处理）。
 */
UCLASS()
class RPG_API URPG_InputBuffer : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * 容量上限。
	 * 4 是个够用的值：能容纳"提前按了下一段 + 一点乱按"，又不会积压成灾难。
	 */
	static constexpr int32 MaxEntries = 4;

	void SetMode(ERPG_InputBufferMode NewMode) { Mode = NewMode; }
	ERPG_InputBufferMode GetMode() const { return Mode; }

	/**
	 * 压入一个输入意图。
	 * @param InputTag  输入标签，必须有效（无效标签会被忽略）
	 * @param LifeTime  生命周期（秒）
	 * @param Now       当前世界时间
	 */
	void Push(const FGameplayTag& InputTag, float LifeTime, float Now);

	/**
	 * 取出并移除一个输入意图。
	 * @param OutTag  取出的标签
	 * @param Now     当前世界时间（用于顺带清理过期条目）
	 * @return 容器为空或全部过期时返回 false
	 */
	bool Consume(FGameplayTag& OutTag, float Now);

	/** 清理所有已过期的条目 */
	void PruneExpired(float Now);

	void Clear() { Entries.Reset(); }

	bool IsEmpty() const { return Entries.IsEmpty(); }
	int32 Num() const { return Entries.Num(); }

	/** 调试用：把当前缓存内容格式化成一行字符串 */
	FString ToDebugString() const;

private:
	/**
	 * 缓存条目。
	 * 末尾是"最新压入"（栈顶）—— Consume 在 Stack 模式下从这里取。
	 */
	UPROPERTY()
	TArray<FRPG_BufferedInput> Entries;

	ERPG_InputBufferMode Mode = ERPG_InputBufferMode::Stack;
};
