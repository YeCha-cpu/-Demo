# RPG 项目 · 代码审查导览

> **这份文档回答一个问题：一个没参与过这个项目的人，怎么在最短时间里把它 review 明白。**

---

## 0. 定位：这份文档是什么，不是什么

| | 文档 | 回答什么 |
|---|---|---|
| ✅ | **本文档** | 项目是怎么一步步长成现在这样的；review 时该看哪、什么别怀疑、什么要重点怀疑 |
| | [`ARCHITECTURE.md`](./ARCHITECTURE.md) | 现在**是什么样**：目录结构、类依赖、GAS 链路、标签体系、面试亮点 |
| | `PHASE*_SETUP.md` | **怎么配**：编辑器里点哪里、填什么 |
| | `PHASE8_NETWORKING.md` | 联机的验收清单与调试手册 |
| | [`REVIEW_PHASES_0-4.md`](./REVIEW_PHASES_0-4.md) | 阶段 0~4 的**复盘**：这些零件怎么咬合成一条链、五条贯穿性原则。比本文档更偏"原理"，更适合**动手改代码前**读 |

本文档**不复述架构**。它讲的是**过程**：每个决定是在什么背景下做的、踩了什么坑、
坑是怎么修好的、哪些结论已经被验证过所以不用再验一遍。

---

## 1. 30 分钟上手路线

按这个顺序读，每一步都建立在前一步之上：

| 顺序 | 读什么 | 花多久 | 读完你会知道 |
|---|---|---|---|
| 1 | `ARCHITECTURE.md` §1「设计目标与范围」 | 3 min | 这是个什么项目、做多大、**明确不做什么** |
| 2 | `ARCHITECTURE.md` §2「目录结构设计」 | 5 min | 代码在哪、为什么这么分 |
| 3 | `ARCHITECTURE.md` §4「GAS 核心链路」 | 7 min | 输入→能力→效果→表现 的全链路 |
| 4 | `ARCHITECTURE.md` §14「网络联机设计」 | 5 min | 复制策略、权威边界 |
| 5 | 本文档 §4「贯穿全项目的设计决定」 | 5 min | 上表的**理由**，以及每个决定的代价 |
| 6 | 本文档 §5「审查锚点」 | 5 min | 哪些地方看着可疑但其实是**对的** |
| 7 | 挑一条链路读代码 | 不限 | 建议从 `输入 → 轻击 → 命中 → 扣血 → 飘字` 走一遍 |

**想直接看代码的话**，下面这三条链路覆盖了项目 80% 的核心机制：

```
链路 A（能力全流程）
  ARPG_PlayerController::OnAbilityInputPressed
    → URPG_AbilitySystemComponent::TryActivateAbilityByInputTag
    → URPG_GameplayAbilityBase::ActivateAbility
    → URPG_AbilityTask_WeaponTrace（自定义 AbilityTask）
    → URPG_DamageExecution（ExecutionCalculation）

链路 B（伤害落地，全项目唯一的扣血入口）
  URPG_AttributeSet::PostGameplayEffectExecute
    → SetHealth → 属性委托 → 血条
    → Multicast_ShowDamageNumber → HUD → 飘字
    → SendGameplayEvent(Event.Combat.Hit / Death) → GA_HitReact / GA_Death

链路 C（联机的权威与预测）
  ClientEndAbility / ServerTryActivateAbility / Server_PushInputTag
    → RepAnimMontageInfo 复制
    → ReplicatedUsing + OnRep_* 属性复制
```

---

## 2. 项目规模与技术栈

| 项 | 值 |
|---|---|
| 引擎 | UE 5.8（`D:\UE5\UE_5.8`） |
| 语言 | C++（无蓝图逻辑，蓝图只做资产配置与动画图） |
| 模块 | 单模块 `RPG`，Feature Folder 布局 |
| 提交数 | 19 |
| 联机范围 | L1 基础复制 + L2 权威与预测（**不做 L3**） |
| 主模式 | Listen Server；Standalone 自动降级 |

### 源码目录职责

| 目录 | 职责 | 一句话 |
|---|---|---|
| `Core/` | 日志类别、GameplayTag、PlayerState、PlayerController、GameMode、InputConfig | 框架层 |
| `Character/` | BaseCharacter / Player / Enemy | 角色层 |
| `AbilitySystem/` | ASC 扩展、AttributeSet、GA 基类、各具体能力、ExecutionCalculation | GAS 层 |
| `Combat/` | 输入缓存、攻击模组 DataAsset、伤害类型 | 战斗规则 |
| `Animation/` | AnimInstance 基类、AnimNotify 桥接 | 动画 |
| `AI/` | AIController、BT 任务/服务/装饰器、黑板键常量 | 敌人 AI |
| `UI/` | HUD、HUDWidget、属性条、头顶血条、飘字 | 表现层 |
| `Interfaces/` | 只补充引擎接口没有的东西 | 接口层 |

> **一个刻意的取舍**：`.h` / `.cpp` 同目录，不用 UE 默认的 `Public/Private` 镜像层。
> 代价是与"标准 UE 项目"长得不一样；收益是找一个类不用在两个树里跳。
> 靠 `Build.cs` 里的 `PublicIncludePaths.Add(ModuleDirectory)` 让
> `#include "AbilitySystem/Abilities/Xxx.h"` 这种模块根相对路径生效。

---

## 3. 编年史：19 次提交

### 3.0 先说工作流

每一次功能交付都走同一条路，**这本身就是值得 review 的部分**：

```
① 先查证引擎行为（读 UE 源码，不凭印象）
      ↓
② 出架构/设计文档 → 确认
      ↓
③ 写代码（关键处写"为什么"，不复述代码字面）
      ↓
④ 编译验证（Result: Succeeded）
      ↓
⑤ 写配置指南文档（蓝图/资产部分由人操作）
      ↓
⑥ 提交（附详细 commit message，记录取舍与踩坑）
```

**⑦ 独立代码审查**是后加的，触发点是阶段 6 那次审查一口气抓出了五个问题
（其中一个让整个受击功能是死代码）。

> ⚠️ **诚实记录**：阶段 8 的六个提交**一次都没跑过独立审查** ——
> 是写这份文档时才发现的（文档承诺的工作流和实际做到的对不上）。
> 补跑的结果在 [`REVIEW_PHASE8.md`](./REVIEW_PHASE8.md)：15 个确认问题，
> 其中最重的是「诊断代码自己有假阳性」和「注释里的因果写反了」。
> 从那以后固定为提交前动作。

> **为什么强调这个**：这个项目的 commit message 异常长，不是话痨——
> 是因为**踩坑的原因是代码里看不出来的**。比如"为什么 `InstancingPolicy` 必须显式设成
> `InstancedPerActor`"，代码上只是构造函数里一行赋值，看不出它是被一个 bug 逼出来的。
> 这类信息只能写在提交信息里。

---

### 3.1 地基期（3 次提交）

#### `f619998` First commit

UE 第三人称模板工程的原始状态。850 个文件，基本全是模板内容。

---

#### `a5212fe` 阶段 0：地基搭建

**做了什么**

| 项 | 内容 |
|---|---|
| 模板清理 | 删掉 `Variant_Combat` / `Variant_Platforming` / `Variant_SideScrolling` 三套变体代码与资产 |
| 目录重构 | 从 `Public/Private` 镜像层改成 Feature Folder |
| 模块依赖 | `GameplayAbilities` / `GameplayTags` / `GameplayTasks` / `EnhancedInput` / `AIModule` / `NavigationSystem` / `Niagara` / `Slate` / `SlateCore` |
| 日志 | 拆成 `LogRPG` / `LogRPG_Ability` / `LogRPG_Combat` / `LogRPG_AI` / `LogRPG_Animation`，可按域单独开 Verbose |
| 标签体系 | `Core/RPG_GameplayTags`，8 个域约 68 个原生标签 |

**为什么标签用 `UE_DEFINE_GAMEPLAY_TAG_COMMENT` 而不是 ini**

编译期检查 + IDE 可跳转 + 不用在两处重复注册。代价是改标签要重新编译——
对这个规模的项目完全可接受。

**★ 这一步真正的价值：核实出了 3 处 UE 5.8 的 API 已经不存在**

原设计里有三处是照着旧资料写的，读引擎源码后发现全都用不了：

| 原设计 | 5.8 的实际情况 | 改成 |
|---|---|---|
| `UAbilityTask_PlayMontageAndWaitForEvent` | **不存在**（UE4 插件遗留） | `PlayMontageAndWait` + `WaitGameplayEvent` 两件套 |
| GE 上的 `OngoingTagRequirements` 内联字段 | 5.3 起废弃 | `UTargetTagRequirementsGameplayEffectComponent` 组件 |
| `FGameplayAbilitySpec::InputTag` | **已移除** | 自建 `InputTag → FGameplayAbilitySpecHandle` 映射表 |

**审查时看这里**：`ARCHITECTURE.md` 附录 B「UE 5.8 API 核查速查表」。
项目里所有"和网上教程不一样"的写法，基本都能在那张表里找到原因。

---

#### `ff00aaa` 阶段 1：角色层 + GAS 骨架

**做了什么**

- `IRPG_AbilitySystemInterface` + `URPG_AbilitySystemLibrary`
- `URPG_AttributeSet`：8 属性 + 元属性 `IncomingDamage`
- ASC 扩展：输入标签 → Handle 映射表
- 角色层：`RPG_BaseCharacter`（接口默认实现）/ `RPG_Player` / `RPG_Enemy`
- 框架层：`RPG_PlayerState` / `RPG_PlayerController` / `RPG_GameModeBase` / `URPG_InputConfig`

**★ 两个定调子决定**

**① 用 Handle 而不是类来激活能力**

映射表存的是 `FGameplayAbilitySpecHandle`，不是 `TSubclassOf<>`。
理由：同一个类可能有多个实例（不同等级、不同来源），按类找会触发错的那个。

**② `ARPG_BaseCharacter` 提供接口的唯一默认实现**

子类只需要实现 `GetASCInternal()` 一个虚函数，`GetAbilitySystemComponent()` /
`GetRPGAttributeSet()` 全在基类里实现一次。这是"玩家 ASC 在 PlayerState、
敌人在自身"这个差异被**收敛到一处**的地方。

**踩坑**（已写进代码注释）

- `AController` 自带 `Character` 成员，局部变量同名触发 C4458（UE 当错误处理）
- `UInputMappingContext` 必须显式 include，前向声明不足以调用 `GetName()`

---

### 3.2 联机骨架（3 次提交）

#### `2bb70f6` 阶段 1.5：加入 L1 网络联机复制

> 这一阶段的背景值得记一笔：联机原本定的是**不做**，中途改成做 L1+L2。
> 时机选择是"必须在阶段 2 战斗层之前"——否则战斗写完再返工，代价大得多。

**做了什么**

| 项 | 内容 |
|---|---|
| 属性复制 | 8 个属性（**不含**元属性）加 `ReplicatedUsing` + `OnRep_Xxx` |
| 复制条件 | `COND_None` + `REPNOTIFY_Always` |
| ASC 复制 | 构造函数 `SetIsReplicated(true)` |
| 权威分支 | `InitializeAbilitySystem` 分两半：`InitAbilityActorInfo` 两端都做，授予能力只服务器做 |
| PlayerController | `BeginPlay` 加 `IsLocalController` 守卫 |

**★ 三个必须理解的取舍**

**① 为什么 `COND_None` 而不是 `COND_OwnerOnly`**

血条、队友状态、敌人血量**都要给别人看**。用 `OwnerOnly` 的话只有自己收得到。

**② 为什么 `REPNOTIFY_Always` 而不是 `REPNOTIFY_OnChanged`**

GAS 属性有 `BaseValue` / `CurrentValue` **两层**。表面数值没变时，底层状态可能已经变了
（比如 Buff 叠加又抵消）。用 `OnChanged` 会漏更新。

**③ 为什么 `IncomingDamage` 有意不复制**

它只是服务器上伤害计算的临时投递口，用完立即清零，不承载任何持久状态。

**★ 一个不报错但致命的疏漏**

`SetIsReplicated(true)` 少这一句，联机下会同时出现三个症状：
能力激活不同步、GE 不复制、GameplayCue 只有自己看得见。**三件事都不报错。**

**★ 客户端自己 `GiveAbility` 会发生什么**

会产生两个同能力实例，症状是"放一次技能触发两遍效果"，且**只在联机时出现**。
所以授予能力必须加 `HasAuthority` 守卫。

---

#### `2432077` docs: 修正 DefaultEngine.ini 中过时的 GameMode 注释

纯注释修正，跳过。

---

#### `55b2583` 修复：补齐 Jump 输入绑定

**现象**：按空格完全没反应。

**根因（两个）**

1. `Jump` 标签定义了但**根本没绑定**——按空格会尝试激活一个还不存在的
   `GA_Jump`（阶段 3 才有），`TryActivateAbilityByInputTag` 静默返回 false
2. `Sprint` 被**绑定了两次**：既在能力循环里、又在原生绑定里，按一次触发两遍

**修法：引入"过渡期输入"概念**

`Jump` / `Sprint` 最终都是 GA，但 GA 还没写。于是临时绑到原生行为上，
用 `IsInterimNativeInput` 统一判断，能力循环跳过这些标签以避免双重绑定。

> **★ 这个设计值得单独看**：阶段 3 做好 GA 后，只需从 `IsInterimNativeInput`
> 里删掉对应标签，它会**自动回到能力循环那条正规路径**，不需要改动其他代码。
> 这是"用一处判断表达临时状态"而不是散落各处的 `if (bHasJumpGA)`。

**★ 这次漏掉 Jump 的根本原因**

"定义了标签但没逐条核对处理位置"。所以文档里补了一张
「每个输入标签的处理位置」对照表，7 个 `Input.*` 标签逐一列出处理位置与可用状态。

**审查建议**：任何"定义了枚举/标签/配置项"的地方，都要问一句
"有没有配套的**逐条核对表**"。

---

### 3.3 战斗核心（4 次提交）

#### `3a75563` 阶段 2 批次 1：战斗核心链路

26 个文件，3249 行。这是项目里最密的一次提交。

**交付的链路**

```
输入缓存（CombatComponent）
  → 连段推进（GA_LightAttack 的 5 段）
  → 武器轨迹检测（URPG_AbilityTask_WeaponTrace，自定义 AbilityTask）
  → 伤害计算（URPG_DamageExecution）
  → 扣血（AttributeSet）
```

**★ 为什么输入要进缓存而不是直接激活**

黑神话那类动作游戏的核心手感：**在攻击后摇里按下的键不能丢**。
缓存让"这一刀快结束时按下的下一刀"在衔接窗口打开时能被取走。

缓存设计上做了 LIFO/FIFO 的取舍与容量上限，以及**惰性清理**
（不主动扫，取的时候才发现过期）。

**★ 为什么伤害用 ExecutionCalculation 而不是 Modifier**

| | Modifier | ExecutionCalculation |
|---|---|---|
| 适合 | 加攻、上 Buff | 需要**同时读攻防双方属性**的公式 |
| 本项目 | 治疗、增益 | 伤害 |

防御减伤用的是 `D / (D + K)` 曲线而不是减法或百分比——
数值可控（防御再高也不会免伤），且不会出现负数伤害。

---

#### `224cc20` 修复：`CanActivateAbility` 在 CDO 上调用

**这是一个教科书级的"错误信息会骗人"的案例。**

**现象**：按攻击键无反应。

**诊断日志给出的确切位置**：
```
[Default__GA_LightAttack_C] CanActivateAbility 失败：拿不到 ASC
```

注意 `Default__` 前缀——那是 **CDO（类默认对象）**，不是能力实例。

**根因**：`CanActivateAbility` 的调用时机比直觉更早——它在**能力实例化之前**
就要判断"这个能力现在能不能激活"。此时 `this` 指向 CDO，而 CDO 的
`CurrentActorInfo` 永远是空的。

原实现调用了 `GetAbilitySystemComponentFromActorInfo()`（依赖 `CurrentActorInfo`），
在 CDO 上必然返回 nullptr → 能力永远激活不了。

**更糟的是错误信息极具误导性**：显示成 "ActorInfo 没初始化"，
让人往角色初始化方向去查，而实际上**调用方传进来的 ActorInfo 完全是好的**。

**修法**：改用参数传入的 ActorInfo，并固化一条规则：

> **`CanActivateAbility` 是 const 函数，只能依赖参数，不能依赖任何实例状态。**

**★ 这次能定位，全靠一批诊断设施**

| 设施 | 作用 |
|---|---|
| ASC 接管 `AbilityFailedCallbacks` | GAS 默认对激活失败**完全沉默**，这个回调能带出失败原因标签 |
| `CanActivateAbility` 每步失败都打日志 | 从"没反应"变成"卡在哪一步" |
| "未绑定能力"从 Verbose 提到 **Warning** | Verbose 默认不可见，是"按键没反应且日志一片空白"的元凶 |
| `OnAbilityInputPressed` 入口日志 | 作为输入链路的检查点 |

> **这次之后定下的项目习惯**：**主观描述（"没反应"）必须先被转成数字或状态**，
> 再开始查。这条规则在后面反复起作用。

---

#### `afffff0` 修复：连按无衔接

**问题 1：用了引擎默认的 `InstancedPerExecution`**

UE 5.8 的 `UGameplayAbility` 构造函数把 `InstancingPolicy` 默认设成了
`InstancedPerExecution`（`GameplayAbility.cpp:102`）——这对连段系统是灾难：

- 连按三次会创建三个 GA 实例同时跑（日志里可见 `_0` / `_1` / `_2`）
- 连段进度存在实例成员里，新实例永远从第 1 段开始
- 窗口状态 `bComboWindowOpen` 全部丢失

**修法**：在 `URPG_GameplayAbilityBase` 构造函数显式设回 `InstancedPerActor`。
该策略的语义正是连段需要的：每个 Actor 一个实例、同一时刻只能有一个激活、
状态跨激活保留。

> ⚠️ **审核时注意**：这是一个**依赖引擎构造函数的默认值**的坑。
> 引擎改默认值，代码就坏，而且不报错。凡是这类地方，本项目一律
> **显式写出来并在注释里说明为什么**。

**问题 2：输入从未进入缓存**

`OnAbilityInputPressed` 只调了 `TryActivateAbilityByInputTag`，**漏了 `PushInputTag`**。
而 `TryStartNextSegment()` 完全依赖读缓存——缓存永远是空的。

**修法**：PC 先推缓存、再尝试激活。两步配合的逻辑：

| 当前状态 | 结果 |
|---|---|
| 不在攻击中 | 激活成功，GA 内部消耗掉刚推入的那条输入 |
| 正在攻击中 | 激活失败（无害），输入留在缓存里等衔接窗口取走 |

> **★ 这段"先推再激活"的顺序是理解整个连段系统的钥匙。**
> 后面阶段 8 修联机连段（H1）时，服务器缺的就是这个"推"的动作。

---

#### `a9ee295` 阶段 2 批次 2：重击蓄力 + 切手技 + 闪避无敌帧

| 能力 | 机制 |
|---|---|
| `GA_HeavyAttack` | 3 段蓄力，靠 `State.Attack.Charging` 标签 + GE_Duration 表达计时 |
| 切手技 | 轻击中途接重击切段 |
| `GA_Dodge` | 冲量位移 + 无敌帧（`AnimNotifyState` 驱动 `State.Invulnerable`） |

**★ 无敌帧用标签而不是 bool**

`State.Invulnerable` 由 `AnimNotifyState` 的 `NotifyBegin` / `NotifyEnd` 挂上和摘掉。
好处：**动画被打断时引擎会为存活的 NotifyState 补发 `NotifyEnd`**
（`AnimInstance.cpp:2507`），所以不会出现"无敌帧永久生效"这种灾难。

---

### 3.4 资源与动画（2 次提交）

#### `2d55098` 阶段 3：资源系统

**交付**：耐力消耗与恢复、奔跑、跳跃、治疗、增益。同时把阶段 1.5 的
"过渡期输入"转正成真正的 GA。

**★ 最值得看的一个设计：耐力"停手 3 秒恢复"，零计时代码**

```
GE_Duration            → 表达"停手 3 秒"这个时间
Target Tag Requirements → 表达"攻击中不恢复"这个开关
```

没有一行 `SetTimer`、没有一个倒计时变量。

**对比传统写法**：传统写法要在每次攻击开始/结束时 `SetTimer` / `ClearTimer`，
一旦漏掉某个结束路径（被打断、死亡、切场景），计时器就永远不会触发——
表现是"耐力再也不恢复了"。用标签 + GE 的话，**能力的生命周期由 GAS 托管**，
被打断时标签自动清理，恢复自动重启。

**顺带修的坑：属性钳制**

| 事实 | 出处 |
|---|---|
| `PreAttributeChange` **拦不住 GE 的 Modifier**，GE 走 `PreAttributeBaseChange` | `AttributeSet.cpp:82/95`、`GameplayEffect.cpp:4001` |
| 引擎注释明确要求**两个回调都钳制** | `AttributeSet.h:226-228` |

**症状**：`BaseValue` 变负 → 读数卡在 0；周期恢复无上限 → 涨到几百。
**两种都不报错。**

本项目让两个回调共用同一个 `ClampAttribute()`，保证两条路径结果必然一致。

---

#### `ecfc63e` 阶段 4：动画系统

（526 个文件是因为包含动画资产）

**交付**：`URPG_AnimInstanceBase`、动画蓝图搭建指南、蒙太奇制作指南。

**★ 一个关键取舍：AnimInstance 每帧查 ASC 标签，不缓存 ASC**

```cpp
// ❌ 不要这样
UPROPERTY() TObjectPtr<UAbilitySystemComponent> CachedASC;

// ✅ 每帧现查
const UAbilitySystemComponent* ASC = Character->GetAbilitySystemComponent();
```

理由：**PlayerState 会被替换**（重生、重连）。缓存了就会指向一个已经没用的对象，
而且不报错——表现是"复活后动画状态全乱"。

**★ 已知的引擎约束（都核实过源码）**

| 事实 | 出处 |
|---|---|
| 状态机**不能嵌套**（右键菜单只有 Add State / State Alias / Conduit / Entry Point / Comment） | `AnimationStateMachineSchema.cpp:377-418` |
| 状态类型只有 `Single animation` / `Blend graph` | `AnimStateNode.h:16-20` |
| 蒙太奇 `Blend In`/`Blend Out` 默认各 0.25s；**淡入+淡出 > 全长时权重永远到不了 1** | `AnimMontage.cpp:76-77`、`:2628-2636` |
| `Montage_PlayInternal` 的 `bStopAllMontages` 默认 true | `AnimInstance.cpp:2758-2772` |

本项目用 `SM_Main` + 4 个 `Blend graph` 状态来绕过"不能嵌套"。

**★ 体检位置的选择**

| 检查什么 | 放在哪 | 为什么 |
|---|---|---|
| 混合时间是否合理 | `PlayMontageOrSkip()` | 一处覆盖**所有能力、所有段** |
| Notify 位置是否越界 | `DA::IsDataValid` | 数据资产自己校验，编辑器里就能发现 |

> 这是"把检查放在覆盖最多路径的那个点上"的思路，比在每个 GA 里各查一遍更可靠。

---

### 3.5 AI / 死亡 / UI（1 次提交，85 文件 / 8529 行）

#### `6fef4c6` 阶段 5-7

> ⚠️ **这次提交的粒度是偏大的**（原本应该分成 3 次）。
> 从阶段 8 起改成了更小的批次。如果只看一次提交，看这次就够——
> 但要按下面的三块分开看。

**块 1：敌人 AI（阶段 5）**

| 交付 | 内容 |
|---|---|
| `ARPG_AIController` | 感知配置、黑板 |
| `URPG_BTService_CombatUpdate` | 每帧更新战斗相关黑板键 |
| `URPG_BTTask_Attack` | Latent Task 模式 |
| `URPG_BTTask_Patrol` / `URPG_BTTask_MoveToTarget` | 巡逻与追击 |
| `URPG_BTDecorator_CanAttack` | 攻击条件判断 |

**★ 核实出来的 AI 感知"三连坑"**

| 坑 | 事实 | 出处 |
|---|---|---|
| 1 | `FAISenseAffiliationFilter` 三个开关**默认全 false**，且 `UAISenseConfig` 构造函数不改它们 → 位掩码 0 → `ShouldSenseTeam` 恒 false → **AI 什么都感知不到** | `AIPerceptionTypes.h:218-226`、`AISense.cpp:118-121/175-178` |
| 2 | 默认态度求解器是 `A != B ? Hostile : Friendly`——**永远不会返回 Neutral**。AI 和玩家默认都是 `NoTeam`(255) → 算出来是 **Friendly**，所以只开 `bDetectNeutrals` 没用 | `AIInterfaces.cpp:30-33` |
| 3 | `AAIController` 默认**不创建** `PerceptionComponent`，要自己建 | `AIController.cpp` ctor |

**★ AI 攻击走和玩家完全相同的入口（重要设计）**

```
BT 任务推 Input.* 到缓存 → TryActivateAbilityByInputTag → GA
```

**不是**给 AI 另写一条"AI 专用攻击路径"。

收益：连段、输入缓存、耐力消耗、状态标签**自动全部生效**，
因为走的根本就是同一套代码。

> **★ 代价与取舍**：这让 AI 的行为受"输入缓存"这条链路影响。
> 阶段 8 修 H1（服务器输入缓存是空的）时，AI 在服务器上不受影响，
> 因为 AI 本来就在服务器跑，`PushInputTag` 是本地直调。
> 而客户端玩家的输入需要额外的 RPC 补——**这个差异正是 H1 的成因**。

**★ BT 任务怎么知道攻击结束：轮询 `State.Attacking` 标签，不是等事件**

| 方案 | 覆盖范围 |
|---|---|
| 等 `Event.Combat.AttackEnd` 事件 | **只覆盖"正常播完"** |
| 轮询 `State.Attacking` 标签 | 覆盖**所有结束路径**（被打断、死亡、取消） |

**★ 用 `bCreateNodeInstance = true` 而不是 NodeMemory**

行为树节点默认是**共享实例**（`bCreateNodeInstance = false`）。
存逐实例状态必须开 `bCreateNodeInstance = true`，否则多个敌人共用一份状态。

**★ 黑板键名用 C++ 常量集中定义**

`AI/RPG_BlackboardKeys.h`。理由：`FName` 拼错**编译期不报错**，
运行时表现为"AI 什么都不做"，极难查。

**块 2：受击 / 死亡 / 重生（阶段 6）**

| 交付 | 内容 |
|---|---|
| `GA_HitReact` | 受击反应 |
| `GA_Death` | 死亡五步编排 |
| 布娃娃 | `ARPG_BaseCharacter::EnterRagdoll` / `OnRep_RagdollEnabled` |
| 重生 | 玩家回 PlayerStart，敌人按配置 |

**★ 死亡流程用"事件驱动编排"，没有调用点**

`GA_Death` 五步：挂 `State.Dead` → `CancelAllAbilities` → `OnDeathStarted` →
蒙太奇 → 布娃娃 + 倒计时。

好处：**加一个"死亡掉装备"只需要新增一个监听者**，不用去改这五步。

**★ `State.Dead` 用 loose tag 而不是 GE**

复活时要 `RemoveActiveEffects` 清空所有 GE——死亡标记如果是 GE 会被顺手清掉。

**★ 布娃娃只复制 `bRagdollEnabled` 开关**

物理不是复制属性，各端各自模拟。L3 才需要逐帧一致。

**★★ 这次提交后的独立审查，一口气抓出 5 个问题**

其中一个**让整个受击功能是死代码**：`GA_HitReact` 的构造函数里**漏了
`AbilityTriggers.Add(...)`**，能力永远不会被事件触发。而头文件注释和文档
都写着"已配置"——**注释在撒谎**。

另外四个：`IsValid()` 判不出 `RemoveFromParent()`、Multicast 没做权威守卫、
`UWidgetComponent` 的 owner 解析错误、事件广播重复。

> **这就是"提交前跑一遍独立代码审查"成为固定动作的原因。**
> 这五个问题我自己反复看都没看出来。

**块 3：战斗 HUD（阶段 7）**

| 交付 | 内容 |
|---|---|
| `ARPG_HUD` | HUD 载体 + 飘字 |
| `URPG_HUDWidget` | 左下属性区 + 右下招式区 + 死亡面板 |
| `URPG_AttributeBarWidget` | 单条属性条 |
| `URPG_OverheadHealthBarWidget` | 头顶血条 |
| `URPG_DamageNumberWidget` | 伤害飘字 |

**★ UI 数据源的两类问题，两种解法**

| 数据类型 | 方案 | 理由 |
|---|---|---|
| 连续量（血量、耐力） | 属性变化**委托** | 变了才通知，不用轮询 |
| 离散状态（是否在蓄力、招式名） | **每帧读标签** | 状态种类多、变化频繁，委托要绑一堆 |

**★ 蓄力条：标签当节拍器 + UI 自己计时**

不读 GA 的私有成员。理由：GA 实例会被回收、私有成员读不到、0.1s 才更新一次。

**★ 表现类 RPC 一律 `NetMulticast, Unreliable`**

飘字的位置用 `FVector_NetQuantize`。GameplayEvent 只在服务器广播，
用在只有服务器关心的地方（受击、死亡）；飘字是每个客户端各自要画的，
必须走 Multicast。

---

### 3.6 联机补完（阶段 8，6 次提交）

> 这一阶段的主题是**还债**：把已经写在文档里的联机承诺兑现。
> 判断标准不是"好不好玩"，而是"文档说的和代码做的是不是一回事"。

#### `1af3127` 阶段 8-①：修复客户端无法激活输入能力

**根因**：`RegisterInputAbilities(mappings)` 只在服务器的 `HasAuthority` 分支里调，
**客户端永远拿不到映射表**。

**修法：把"配置数据"和"运行时状态"拆开**

```
RegisterInputAbilityMappings(mappings)  ← 两端都调（纯配置数据）
GrantInputAbilities()                   ← 仅服务器
RebuildInputTagHandleMap()              ← 客户端：从复制的 Spec 反查重建
   └─ 由 OnRep_ActivateAbilities() 触发
```

**★ 这是全项目最值得讲的一个架构洞察**：

| | 例子 | 两端都要有吗 |
|---|---|---|
| **配置数据** | 标签→能力的映射关系 | ✅ 两端都要（是数据，不是状态） |
| **运行时状态** | `FGameplayAbilitySpecHandle` | ❌ 只能服务器产生 |

把两者混在一个函数里，就会得到"客户端永远拿不到映射表"这种 bug。

**相关引擎事实**：`ActivatableAbilities` 是 `COND_ReplayOrOwner`，
**会复制到 owning client**；但**任何你自己维护的索引都不会**。
复制过来的东西需要有人重建索引——`OnRep_ActivateAbilities` 就是那个时机
（它是 virtual，覆盖时必须调 `Super::`）。

**顺带修的诊断缺陷**：原来"标签压根没绑能力"和"能力还没复制到"共用一句
`没有绑定任何能力`，但这两者的排查方向**完全相反**。拆成三条警告，
而且**各自用独立的去重集合**——共用的话，客户端进场时先报一次时序问题，
那个标签就被永久标记，之后真的配错也再不会报警。

---

#### `75842dc` 阶段 8-②：修复客户端动画顿挫 + 头顶血条挨打才亮

**根因 H1：服务器的输入缓存是空的**

`URPG_CombatComponent::InputBuffer` 是 `NewObject` 建的本机对象，从不复制。
而连段推进靠的正是这个缓存。

```
服务器连不上第二段 → 第 1 段播完就 EndAbility
  → ClientEndAbility → EndAbility → StopCurrentSegmentMontage()
  → Montage_Stop(0.f)   ← 零混合时间的硬切
```

**主机为什么没事**：引擎只在 `!IsLocallyControlled()` 时才发 `ClientEndAbility`，
主机自己控制的 Pawn 收不到这条 RPC。

**修法**：`Server_PushInputTag`（`Reliable`）——客户端按键时额外发一条，
服务器往**同一个缓存**里推一条。

> 注意这里的"同一个入口"：服务器收到 RPC 后调的是 `PushInputTag`，
> 和本地按键**完全相同的函数**。这是项目从一开始就定下的约定的价值。

**根因 H2：蒙太奇复制被 PlayerState 拖到约 1Hz**

`APlayerState` 构造函数硬编码 `SetNetUpdateFrequency(1)`（`PlayerState.cpp:28`），
而玩家的 ASC 挂在 PlayerState 上 → `RepAnimMontageInfo` 跟着 PlayerState 的通道走。

**修法**：`SetNetUpdateFrequency(30.f)`（与 `NetServerMaxTickRate` 默认值持平）。

> 🕳️ GAS 在蒙太奇路径里确实调了 `AvatarActor->ForceNetUpdate()`，
> 但属性在 **PlayerState** 上——**刷 Avatar 不解决 Owner 的频率问题**。

**新功能：头顶血条"挨打才亮"**

| 决策 | 选择 | 理由 |
|---|---|---|
| 触发源 | **血量下降**，不是 `Event.Combat.Hit` | 后者有权威守卫、只在服务器广播；血量下降在两端走同一个属性委托 |
| 计时 | `FTimerManager` 而不是 tick | 控件常规是 `Collapsed` 的，"隐藏的 Widget 还 tick 不 tick"是 Slate 实现细节 |
| 死亡时不亮 | `CanBeRevealed()` 判血量 ≤ 0 | 否则尸体头上会挂一根空血条 5 秒 |

---

#### `551cc38` 阶段 8-③：诊断日志 + 二分排查法

这不是修复，是**加诊断**。背景：

```
server 打客户端玩家 → 亮 ✅
客户端 打 AI        → 亮 ✅
客户端 打 host 玩家 → 不亮 ❌
```

**四个环节全部核实无误**（都对着引擎源码核过）：属性复制条件是 `COND_None`、
`ReplicateSubobjects` 无模式判断、`IsLocalController()` 第三条挡得住、
ASC 分派正确。

**也就是说：理论上它就该工作。** 所以停止猜测，加日志：

- 订阅成功时自检**属性集在不在**
- 掉血检测打印**变化前后两个值**（只看当前值分不清"没掉血"和"没收到"）
- 补上"该亮但被拦下"这个原本静默的分支

> **★ 这次的方法论值得单独记**：四个环节都验证无误时，正确的动作是
> **把"看不见的东西"变成日志**，而不是继续想。
> 这条规则在整个阶段 8 里反复起作用。

---

#### `72ffa5e` 阶段 8-④：订阅重试是死代码

**上一轮的诊断加完还没跑，用户就给了更精确的对比数据**：

```
客户端 打 AI       → 亮 ✅
客户端 打 玩家     → 不亮 ❌（host 玩家、另一个客户端玩家，都不亮）
```

**规律：客户端看"玩家角色"全都不亮。** 两者只差一个变量——ASC 挂在哪。

**根因**：

```cpp
// ── 错的写法 ──
if (ASC == BoundASC.Get())   // 初始状态下两边都是 null，判断**成立**
{
    ClearTimer(BindRetryHandle);
    return;                  // ← 重试定时器**从没被挂起来过**
}
...
if (!ASC) { SetTimer(..., /*bLoop=*/true); }   // 永远走不到
```

用一个判等同时表达**两种**状态，而它们在初始值上**撞车**：

| 想表达 | 实际判据 |
|---|---|
| 还没拿到 ASC | 两边都是 null ← **和下一行撞车** |
| 已经订上了 | 两边是同一个非空指针 |

于是"能不能订上"完全取决于 Widget 被构造的那一刻 ASC 在不在：

| | ASC 何时可得 | 结果 |
|---|---|---|
| AI | 自己身上，本地一构造就有 | 订得上 ✅ |
| 玩家 | PlayerState 上，**要等复制到达** | 订不上 ❌ |
| 服务器看玩家 | 权威本地，一构造就有 | 订得上 ✅ |

**★ 这是我自己引入的 bug，教训比 bug 本身重要**

起因是"隐藏的控件还 tick 不 tick"是 Slate 实现细节，不该把订阅押在上面，
于是从 `NativeTick` 改成 `TimerManager` 重试。**方向是对的，
但顺手把"每帧免费重试"这个副作用一起删掉了，却没补上等价的首次重试。**

> **换机制时，要明确列出"旧机制顺带提供了哪些保障"，逐条在新机制里补上。**
> "每帧都跑"就是那种不起眼但顶用的保障。

---

#### `fa81cef` 阶段 8-⑤：PvP 伤害飘字 + 本地玩家受击本机隐藏

**修复：打玩家没有飘字**

```cpp
if (ARPG_BaseCharacter* OwningCharacter = Cast<ARPG_BaseCharacter>(OwningActor))
```

`OwningActor` 是 ASC 的 **OwnerActor**：
- **敌人**：ASC 在自己身上 → cast 成功
- **玩家**：ASC 在 `RPG_PlayerState` 上 → **cast 静默失败**，整个 if 被跳过

**修法**：改用 `Data.Target.GetAvatarActor()`。

> ⚠️ `Data.Target` 的类型是 **`UAbilitySystemComponent&`**
> （`GameplayEffectExtension.h:18-28`），**不是** `FGameplayAbilityActorInfo`——
> 所以没有 `Target.GetAvatarActor()` 那种字段式写法，要走 ASC 的方法
> （`AbilitySystemComponent.h:1529`）。

**新功能：自己挨打，不在自己的屏幕上冒数字**

```cpp
if (IsLocallyControlledPlayer()) { return; }   // Multicast_ShowDamageNumber 开头
```

判据放这里就够，因为**这个 Multicast 是在受击者身上发起的**。

⚠️ 必须用 `IsLocallyControlledPlayer()`——用 `IsLocallyControlled()` 的话，
服务器上的 AI 也会命中，结果是**打谁都没数字**。

---

#### `ce080a5` 阶段 8-⑥：收敛 + 全库审计

把上面那个 `Cast<ARPG_BaseCharacter>(OwningActor)` 的坑**收敛成一处**
（`AvatarActor` 在函数开头解析一次，后面都用它），并把 `Payload.Target`
也改成 Avatar 作为预防。

然后**全库审计**了所有 `Cast<ARPG_BaseCharacter>` 和 `GetOwningActor()`：

| 位置 | 取的是 | 结论 |
|---|---|---|
| `RPG_AttributeSet` 飘字 / `Payload.Target` | ~~OwningActor~~ → Avatar | **曾是 bug**，已修 |
| `RPG_GameplayAbilityBase` | `GetAvatarActorFromActorInfo()` | ✅ 本来就对 |
| BT 装饰器 / 服务 / 任务 | `GetPawn()` / 黑板 Actor | ✅ |
| `RPG_AnimInstanceBase` | `TryGetPawnOwner()` | ✅ |
| `RPG_PlayerController` | `GetCharacter()` | ✅ |
| `RPG_HUDWidget` | `GetOwningPlayerPawn()` | ✅（要的就是本地玩家） |

**结论：只有属性集这一处踩了坑。**

> **★ 为什么值得做这次审计**：这个 bug 的性质不是"飘字写错了"，而是
> **"玩家 ASC 放 PlayerState"这个架构决定的连带成本**。
> 敌人上 Owner 和 Avatar 恰好相同，所以**单机测不出来**——
> 这类 bug 会反复出现，除非把规则显式写下来。

---

## 4. 贯穿全项目的设计决定（review 时的判断依据）

这 10 个决定是理解整个代码库的钥匙。**看具体实现前先看这张表**，
否则很多写法会显得莫名其妙。

| # | 决定 | 理由 | **代价**（review 时要留意的地方） |
|---|---|---|---|
| 1 | **玩家 ASC 在 PlayerState，敌人在自身** | 玩家死亡重生不丢状态；敌人不需要 PlayerState | ⚠️ **任何"从 ASC 拿角色实体"的地方都必须走 Avatar**——这是一整类 bug 的来源 |
| 2 | **"是否攻击中"的真相源是 GameplayTag** | 生命周期由 GAS 托管；被打断自动清理；联机复制 | 标签多了之后要有一张"谁挂谁摘"的对照表 |
| 3 | **`Ability.*` 和 `State.*` 分开** | 前者是能力身份（给 GAS 机制用），后者是角色状态（给动画/AI/UI 查） | 同一个概念可能有两个标签，要清楚各自给谁用 |
| 4 | **输入 → 标签 → 能力，两级解耦** | 新增技能不用改输入代码 | 多一层间接，调试时要多跳一步（有日志缓解） |
| 5 | **AI 和玩家走完全相同的输入入口** | 连段/缓存/耐力自动一致 | 客户端玩家的输入需要额外 RPC 补（H1 的成因） |
| 6 | **攻击模组用 `PrimaryDataAsset`** | 嵌套结构、软引用校验、`IsDataValid` | 换配置要重新编译或改资产，不如 DataTable 灵活 |
| 7 | **属性钳制两个 Pre\* 回调共用一处** | 只写一个必然漏掉另一条路径 | 两处调用同一个函数，读代码时要意识到它们必然一致 |
| 8 | **表现类 RPC 一律 `NetMulticast, Unreliable`** | 表现丢了无所谓，重要的是不卡 | **必须做权威守卫**（预测路径会重复广播） |
| 9 | **UI 不碰玩法逻辑** | 蓄力条读标签不读 GA 私有成员 | UI 更新有延迟（标签复制），但换来了正确的分层 |
| 10 | **诊断优先于猜测** | 主观问题先加日志变成数字 | 代码里有不少诊断日志，不是冗余 |

---

## 5. 审查锚点：看起来可疑但**正确**的地方

**这一节是为了避免误报。** 下面每一处都是核实过引擎源码的结论，
不是"看起来应该没问题"。

| 看着可疑的地方 | 为什么其实是**对的** |
|---|---|
| `IsLocallyControlledPlayer()` 里那个 `Ctrl->IsA<APlayerController>()` 判断 | 不能省。`AController::IsLocalController()` 对**服务器上的 AIController 也返回 true**（`Controller.cpp:94-110`：Standalone 无脑 true；权威且非 AutonomousProxy 也 true）。少了这个判断，单机下所有敌人都被当成"本地玩家" |
| 主机看远端玩家时，`IsLocalController()` 返回 false | `Controller.cpp:106-110` 的第三条要求 `GetRemoteRole() != ROLE_AutonomousProxy`，而服务器上托管的远端玩家 PC 的 RemoteRole 正是 `AutonomousProxy` |
| `GetSet<URPG_AttributeSet>()` 在 Minimal/Mixed 复制模式下还能拿到 | `ReplicateSubobjects` **无条件**遍历 `SpawnedAttributes`（`AbilitySystemComponent.cpp:1940-1946`），不分复制模式 |
| 属性用 `COND_None` 而不是 `COND_OwnerOnly` | 血条/队友/敌人血量**都要给别人看** |
| 属性用 `REPNOTIFY_Always` 而不是 `OnChanged` | GAS 属性有 Base/Current **两层**，表面没变时底层可能已变 |
| `CanActivateAbility` 里不碰任何成员变量 | 它在**能力实例化之前**就被调用，`this` 是 CDO。碰实例状态必然失败 |
| `URPG_GameplayAbilityBase` 显式设 `InstancedPerActor` | 5.8 的引擎默认是 `InstancedPerExecution`（`GameplayAbility.cpp:102`），对连段是灾难 |
| AnimInstance 每帧现查 ASC，不缓存 | PlayerState 会被替换（重生/重连），缓存了会指向废弃对象 |
| `IncomingDamage` 不复制 | 它只是服务器上伤害计算的临时投递口，用完即清 |
| 蒙太奇诊断用"实播 < 全长 × 0.9"而不是 `bInterrupted` | `bStopWhenAbilityEnds = true` 时，**正常播完**也会报 `bInterrupted=true` |
| 头顶血条订阅走 `TimerManager` 而不是 `NativeTick` | 控件常规是 `Collapsed` 的；"隐藏的 Widget 还 tick 不 tick"是 Slate 实现细节，没有保证 |
| 本地玩家看不到自己挨打的飘字 | **需求如此**，不是 bug。判据在受击者的 Multicast 里 |
| 打人的人看得到飘字、被打的人看不到 | **故意相反的**，见 `PHASE7_UI_SETUP.md` §1.6.1 |

---

## 6. 复发风险清单

**本项目已经踩过的坑。review 时重点看有没有复发。**

### 6.1 "不报错但永远不生效"类 ★ 最高危

| 坑 | 症状 | 现在防住了吗 |
|---|---|---|
| `GA_HitReact` 漏配 `AbilityTriggers` | 整个受击功能是死代码 | 独立审查抓出，已修 |
| `SetIsReplicated(true)` 漏了 | 能力/GE/Cue 三件事全不同步，都不报错 | 已加 |
| `CanActivateAbility` 在 CDO 上跑 | 所有能力激活不了，错误信息还指向错误方向 | 已加规则 |
| 订阅重试是死代码（`null == null`） | 客户端看玩家没血条 | 已修（阶段 8-④） |
| `Cast<ARPG_BaseCharacter>(OwningActor)` 对玩家失败 | 打玩家没飘字 | 已修（阶段 8-⑤） |
| `IsValid()` 判不出 `RemoveFromParent()` | 飘字控件永远清不掉，越打越卡 | 改用 `IsInViewport()` |

**共性**：全都是**静默失败**，而且**单机测不出来**。

> **自检问题**：这段代码如果失效，我会看到什么？
> 如果答案是"什么都看不到"，那它需要一条日志或一个断言。

### 6.2 "依赖引擎默认值"类

| 坑 | 引擎默认值 | 出处 |
|---|---|---|
| `InstancingPolicy` | `InstancedPerExecution`（对连段是灾难） | `GameplayAbility.cpp:102` |
| `APlayerState` 的复制频率 | `1` Hz | `PlayerState.cpp:28` |
| `FAISenseAffiliationFilter` 三个开关 | 全 `false`（AI 什么都感知不到） | `AIPerceptionTypes.h:218-226` |
| 蒙太奇的 Blend In/Out | 各 `0.25` 秒 | `AnimMontage.cpp:76-77` |
| `bStopAllMontages` | `true` | `AnimInstance.cpp:2758-2772` |
| `AAIController` 的 `PerceptionComponent` | **不创建** | `AIController.cpp` ctor |

> **自检问题**：这个行为是我指定的，还是引擎恰好这么默认的？
> 凡是后者，本项目一律**显式写出来并注明原因**。

### 6.3 "属性钳制"类

`PreAttributeChange` **拦不住 GE 的 Modifier**，GE 走 `PreAttributeBaseChange`。
两个都要写，共用同一个 `ClampAttribute()`。

**症状**：`BaseValue` 变负 → 读数卡在 0；周期恢复无上限 → 涨到几百。**都不报错。**

### 6.4 "广播不回滚"类

属性修改是预测的（客户端本地也会跑 `PostGameplayEffectExecute`），
但**事件广播不会回滚**。所以：

- `Event.Combat.Hit` / `Death` 只在服务器广播
- `NetMulticast` 调用必须加权威守卫（非服务器上调用会**本地执行** `_Implementation`）

**症状**：表现闪一下或播两遍，**只在延迟高的客户端出现**。

### 6.5 "换机制丢了隐性保障"类

阶段 8-④ 的教训：把订阅重试从 `NativeTick` 换成 `TimerManager` 时，
**"每帧都跑"这个不起眼的保障被一起删掉了**，而它恰好掩盖了另一个判等顺序问题。

> **自检问题**：旧方案有没有"虽然不优雅但顶用"的副作用？逐条列出来，在新方案里补上。

---

## 7. 已知的未验证清单

> **这一节是本文档最重要的部分之一。**
> 写清楚"什么是没验证的"，比假装什么都验证过更专业。

### 7.1 需要跑 PIE 双客户端才能验的

全部集中在 `PHASE8_NETWORKING.md` §3，共 28 项。其中**最近四个提交**对应的：

| 提交 | 要验什么 | 看哪 |
|---|---|---|
| 8-② | 客户端连段是否流畅（H1）、看远程玩家动画是否不再幻灯片（H2） | §3.1 第 1-6 项 |
| 8-④ | 客户端能否看到玩家的头顶血条（含 host 和另一个客户端） | §3.4 第 17-20 项 |
| 8-⑤ | PvP 飘字的两端矩阵（**第 22-25 项两端结果故意相反**） | §3.4 第 22-26 项 |
| 8-⑥ | 同上（预防性改动，不应有行为变化） | — |

### 7.2 已知的假设，没有实测

| 假设 | 出处 | 怎么验 |
|---|---|---|
| 客户端第一次按键一定在 `OnRep_ActivateAbilities` 之后 | `PHASE8_NETWORKING.md` §1 | 客户端进场后第一次按键可能落空（会报特定警告，再按一次即可） |
| 两个输入标签映射到同一个 GA 类时行为正确 | 同上 | 目前每个标签一个独立 GA 类，不会触发；注册时有 Warning 提醒 |

### 7.3 明确不做的（不是"没做"，是"决定不做"）

| 项 | 为什么 |
|---|---|
| L3（延迟补偿 / 位置回滚 / 专用服务器） | MVP 阶段投入产出比差，且未经真实网络环境验证容易在面试中被问穿 |
| 格挡 | 本期范围外 |
| 网络平滑调参 | 同上 |

---

## 8. Review 检查清单

按这个顺序过一遍，能覆盖 90% 的风险面：

### 结构层

- [ ] 新增文件是否放在正确的 Feature 目录下
- [ ] 依赖方向是否单向（UI → Character → AbilitySystem → Core）
- [ ] 有没有跨层直接调用（比如 UI 直接改属性）

### GAS 层

- [ ] 新能力的 `InstancingPolicy` 是否显式指定
- [ ] 事件触发的能力有没有配 `AbilityTriggers`
- [ ] `EndAbility` 的所有路径是否都能走到（用 `State.*` 标签兜底）
- [ ] 属性修改是否两个 `Pre*` 回调都钳制
- [ ] 死亡/受击事件是否只在服务器广播

### 联机层

- [ ] 用到 ASC 的 OwnerActor 的地方，是否应该用 Avatar
- [ ] `NetMulticast` 是否加了权威守卫
- [ ] 表现类 RPC 是否 `Unreliable`
- [ ] 客户端本机状态（如输入缓存）是否考虑了服务器拿不到的情况
- [ ] `OnRep_*` 覆盖时是否调了 `Super::`

### 表现层

- [ ] 静默失败的地方是否有日志
- [ ] 隐藏的控件是否依赖 tick（不该依赖）
- [ ] UI 是否读了 GA 的私有成员（不该读）

### 通用

- [ ] 注释里引用的引擎行号是否**真的核对过**
- [ ] 依赖引擎默认值的地方，是否显式写出并注明原因
- [ ] 有没有"换机制时丢了旧机制的隐性保障"

---

## 9. 文档索引

| 想了解 | 看 |
|---|---|
| 现在是什么样 | [`ARCHITECTURE.md`](./ARCHITECTURE.md) |
| 怎么一步步变成这样 | **本文档** |
| 这些零件怎么咬合成一条链（原理向） | [`REVIEW_PHASES_0-4.md`](./REVIEW_PHASES_0-4.md) |
| 阶段 8 的独立代码审查记录（含 15 个确认问题） | [`REVIEW_PHASE8.md`](./REVIEW_PHASE8.md) |
| 各阶段的编辑器配置步骤 | `PHASE1_EDITOR_SETUP.md` ~ `PHASE7_UI_SETUP.md` |
| 联机验收与调试 | [`PHASE8_NETWORKING.md`](./PHASE8_NETWORKING.md) |
| UE 5.8 API 与网上教程不一样的地方 | `ARCHITECTURE.md` 附录 B |

---

*最后更新：阶段 8-⑥（`ce080a5`）*
