# RPG 类关系图 · 每个类站在哪、跟谁打交道、被卷进哪些功能

> **这份文档的角度**：`CODE_REFERENCE.md` 讲"每个函数干什么"，
> 这份讲**类之间的关系** —— 谁依赖谁、谁被谁驱动、一个功能牵动哪些类。
>
> 读代码时最费劲的从来不是"这个函数做了什么"，而是
> **"这个类为什么在这里、它要是没了我该去哪儿找"**。这份文档补的就是这块。

---

## 怎么读

| 你想 | 去哪 |
|---|---|
| 先看整体架构 | [`ARCHITECTURE.md`](./ARCHITECTURE.md) |
| **搞清类之间的关系** | **本文档** |
| 查某个函数的具体实现 | [`CODE_REFERENCE.md`](./CODE_REFERENCE.md) |
| 知道某个决定**为什么这么做** | [`REVIEW_GUIDE.md`](./REVIEW_GUIDE.md) |

**建议路线**：先看下面的「全景」建立方位感 → 再看「一个功能牵动哪些类」把静态结构
和动态流程对上 → 然后按需要翻各章。

---

## 全景：分层与依赖方向

### 分层与依赖方向

**依赖是单向的**：上层知道下层，下层不知道上层。跨层往回调一律通过**委托、事件或标签**，
不直接 `#include` 上层头文件。

```
                        ┌──────────────────────────────┐
  表现层  UI/           │ HUD · HUDWidget · 属性条      │  只读 GAS 状态、只画东西，
                        │ 头顶血条(组件+控件) · 飘字    │  不产生任何游戏结果
                        └──────────────┬───────────────┘
                                       │ 读属性委托 / 读标签 / 接 Multicast
                        ┌──────────────▼───────────────┐
  场景层  World/        │ EffectVolume                 │  发 GameplayEvent 让 GA 干活
                        └──────────────┬───────────────┘
                                       │
   AI 层  AI/           ┌──────────────▼───────────────┐
                        │ AIController · BT 装饰器/服务/│  推 Input.* 到缓存 →
                        │ 任务 · 黑板键常量             │  和玩家走**同一个**输入入口
                        └──────────────┬───────────────┘
                                       │
  GAS 层                ┌──────────────▼───────────────┐
  AbilitySystem/        │ ASC 扩展 · AttributeSet ·    │  ★ 全项目唯一扣血入口在
                        │ GA 基类 + 10 个能力 ·         │     AttributeSet::
                        │ ExecutionCalculation ·       │     PostGameplayEffectExecute
                        │ AbilityTask                  │
                        └──────────────┬───────────────┘
                                       │
  规则层  Combat/       ┌──────────────▼───────────────┐
                        │ CombatComponent · InputBuffer│  输入缓存是**本机对象**，
                        │ AttackModuleData(DataAsset)  │  从不复制 ← 联机连段的根因
                        └──────────────┬───────────────┘
                                       │
  动画层  Animation/    ┌──────────────▼───────────────┐
                        │ AnimInstanceBase · 4 个      │  动画帧 → GameplayEvent
                        │ AnimNotify 桥接类            │  → GA 的 AbilityTask 回调
                        └──────────────┬───────────────┘
                                       │
  角色层  Character/    ┌──────────────▼───────────────┐
                        │ BaseCharacter / Player /     │  ASC 归属差异用**一个虚函数**
                        │ Enemy                        │  GetASCInternal() 抹平
                        └──────────────┬───────────────┘
                                       │
  框架层  Core/ Input/  ┌──────────────▼───────────────┐
  Interfaces/           │ PlayerState · PlayerController│  玩家的 ASC 挂在
                        │ GameMode · GameplayTags ·     │  **PlayerState** 上，
                        │ LogChannels · InputConfig     │  敌人在自己身上
                        └──────────────────────────────┘
```

### 三条最要紧的关系

**① `ARPG_BaseCharacter::GetASCInternal()` 是一个虚函数，也是整个项目的地基。**

玩家返回的是 `PlayerState` 上的 ASC，敌人返回自己身上的。上层的**所有**代码
（GA、UI、World、AI）都只调 `GetAbilitySystemComponent()`，不关心差异。

> ⚠️ 这个设计有一处代价，在阶段 8 连着爆过两次：**任何需要"角色实体"的地方
> 必须走 Avatar，不能走 OwnerActor**。因为玩家的 `OwnerActor` 是 PlayerState 而不是角色。
> 详见 [`REVIEW_GUIDE.md`](./REVIEW_GUIDE.md) §6。

**② 输入缓存是本机对象 —— 它决定了联机连段的所有设计。**

`URPG_InputBuffer` 用 `NewObject` 建在 `URPG_CombatComponent` 里，**从不复制**。
所以服务器那份永远是空的，连段推不下去 —— 这就是 `Server_PushInputTag` 存在的理由。

**③ "是否攻击中"的真相源是标签，不是 bool。**

`State.Attacking` 由 GAS 托管：被打断时自动清理，联机时自动复制，
动画 / AI / UI 全都查它。**没有一处 `bIsAttacking` 这样的成员变量**。

---

## 横切：一个功能牵动哪些类

静态结构看完，这一节把它和**动态流程**对上。每条都是"按一次键 / 发生一件事"的完整链路。

### 功能 1：按左键打出一刀（单机，最短路径）

```
① ARPG_PlayerController      增强输入回调 → 拿到 Input.Attack.Light 标签
② URPG_CombatComponent       先 PushInputTag 把意图推进缓存
③ URPG_AbilitySystemComponent 查映射表拿到 SpecHandle → TryActivateAbility
④ URPG_GA_LightAttack        ActivateAbility → 读 AttackModuleData → StartSegment
⑤ URPG_AttackModuleData      给出这一段的蒙太奇、倍率、耐力消耗
⑥ URPG_GameplayAbilityBase   播蒙太奇 + 扣耐力
⑦ URPG_AnimInstanceBase      蒙太奇开始/结束的通知（诊断）
⑧ URPG_AnimNotifyState_AttackWindow   动画播到某一帧 → 广播判定窗口开启
⑨ URPG_AbilityTask_WeaponTrace        连续扫掠检测命中（防隧穿、去重）
⑩ URPG_GameplayAbilityBase::ApplyDamageToTarget   构造 GE_Damage 的 Spec
⑪ URPG_DamageExecution       同时读攻防双方属性 → 算出最终伤害
⑫ URPG_AttributeSet::PostGameplayEffectExecute  ★ 唯一扣血入口
                              ├─→ SetHealth → 属性委托 → 血条 / HUD 属性条
                              ├─→ Multicast_ShowDamageNumber → ARPG_HUD → 飘字控件
                              └─→ SendGameplayEvent(Event.Combat.Hit) → GA_HitReact
```

**牵动 12 个类。** 注意第 ⑧-⑩ 步是**动画通知驱动**的 —— 攻击判定不在代码里写死时间，
而是由动画资产上的 Notify 位置决定。改手感只需要动动画，不用改代码。

### 功能 2：5 段连段衔接

在功能 1 的基础上多出：

| 参与 | 作用 |
|---|---|
| `URPG_AnimNotifyState_ComboWindow` | 在动画上划出"可以接下一段"的窗口 |
| `URPG_GA_LightAttack::OnComboWindowOpen/Close` | 窗口开/关的响应 |
| `URPG_GA_LightAttack::TryStartNextSegment` | **从缓存取一条输入** —— 取到就续段 |
| `URPG_CombatComponent::GetComboIndex/SetComboIndex` | 连段进度的持有者（**不在 GA 里**） |
| `URPG_HUDWidget::UpdateSkillPanel` | 从 `GetComboIndex()` 读权威段位显示"第 N 段" |

> **为什么连段索引放在 `CombatComponent` 而不是 GA 里？**
> 因为 GA 实例会被回收、会被取消，而连段进度必须跨激活保留。
> 同理，`bComboWindowOpen` 那类窗口状态放在 GA 私有成员里 —— 这也是
> `InstancingPolicy` 必须是 `InstancedPerActor` 的原因。

### 功能 3：蓄力重击 + 切手技

| 参与 | 作用 |
|---|---|
| `URPG_GA_HeavyAttack` | 3 段蓄力，按住右键涨、松开释放 |
| `State.Attack.Charging` 标签 | **当节拍器用** —— UI 靠它的出现/消失计时，不读 GA 私有成员 |
| `RPGTags::State_Attack_Transition` | 区分切手技和普通蓄力（判定依据是"激活瞬间在不在轻击连段里"） |
| `URPG_HUDWidget::UpdateSkillPanel` | 判定顺序 **切手 > 蓄力 > 轻击**，顺序反了就会显示错 |
| `URPG_CombatComponent` | 切手技靠 `CancelAbilitiesWithTag` 取消轻击 |

### 功能 4：闪避与无敌帧

| 参与 | 作用 |
|---|---|
| `URPG_GA_Dodge` | 施加冲量位移 |
| `URPG_AnimNotifyState_Invulnerability` | 在动画上划出无敌窗口 → 挂/摘 `State.Invulnerable` |
| `URPG_AttributeSet::PostGameplayEffectExecute` | 无敌兜底检查（第二道防线） |
| `GE_Damage` 的 `TargetTagRequirements` | 第一道防线，GE 层面直接拒绝应用 |

> 用标签而不是 bool 的好处：动画被打断时引擎会为存活的 NotifyState **补发 NotifyEnd**，
> 不会出现"无敌帧永久生效"这种灾难。

### 功能 5：受击反应

```
URPG_AttributeSet::PostGameplayEffectExecute （只算一次，权威）
   └─ SendGameplayEvent(Event.Combat.Hit)  ← 只在服务器广播
        └─ GA_HitReact 的 AbilityTriggers 响应
             ├─ CancelAbilitiesWithTag 取消攻击/闪避
             ├─ 播受击蒙太奇
             └─ 挂 State.Hit 标签（动画/AI 查）
```

**客户端怎么知道？** 靠复制 —— 属性、标签、蒙太奇都会复制过去。
**事件广播不回滚**，所以不能让客户端也广播一遍（否则延迟高的客户端会播两遍）。

### 功能 6：死亡 → 布娃娃 → 重生

| 参与 | 作用 |
|---|---|
| `URPG_AttributeSet` | 血量归零 → 广播 `Event.Combat.Death` |
| `URPG_GA_Death` | **五步编排**：挂 `State.Dead` → 取消所有能力 → `OnDeathStarted` → 蒙太奇 → 布娃娃 + 倒计时 |
| `ARPG_BaseCharacter::EnterRagdoll/OnRep_RagdollEnabled` | 只复制 `bRagdollEnabled` **开关**，物理各端各自模拟 |
| `ARPG_Player::PerformRespawn` | 回 PlayerStart + `ClientSetLocation` 推位置给客户端 |
| `ARPG_Enemy` | 按 `RespawnDelay` 决定重不重生 |
| `ARPG_AIController::StopAI/RestartAI` | 死亡时停 AI，重生时重启 |

> **死亡流程没有调用点** —— 它是事件驱动的编排。加一个"死亡掉装备"
> 只需要新增一个监听者，不用改这五步。

### 功能 7：敌人 AI 的一次攻击

```
ARPG_AIController（感知 + 黑板 + 跑行为树）
   └─ URPG_BTService_CombatUpdate   每帧更新黑板（距离、能否攻击、目标）
        └─ URPG_BTDecorator_CanAttack  条件判断
             └─ URPG_BTTask_Attack     Latent Task（bCreateNodeInstance = true）
                  ├─ 推 Input.Attack.Light 到缓存
                  ├─ TryActivateAbilityByInputTag  ← **和玩家完全相同的入口**
                  └─ 轮询 State.Attacking 标签直到结束
```

> **为什么轮询标签而不是等 `Event.Combat.AttackEnd`？**
> 事件只覆盖"正常播完"；标签覆盖**所有结束路径**（被打断、死亡、取消）。

### 功能 8：拾取治疗 / 增益 / 减益

```
ARPG_EffectVolume （可碰撞，只在服务器生效）
   └─ SendGameplayEvent(Event.Item.*)  ← 不直接施加 GE
        ├─ URPG_GA_Heal       → GE_Heal（SetByCaller 传治疗量）
        └─ URPG_GA_ApplyBuff  → GE_Buff_* / GE_Debuff_*（从载荷里取 GE）
```

> 走 GA 而不是直接 `ApplyGameplayEffectToTarget`，是为了让消耗、动画、
> 打断、能力层标签都有地方放。**代价**：`GA_ApplyBuff` 只能授予一次。

### 功能 9：联机下客户端按一次键

在功能 1 的基础上多出三处：

| 参与 | 作用 |
|---|---|
| `ARPG_PlayerController::Server_PushInputTag` | 把输入意图送到服务器，让两边**缓存一致** |
| `URPG_AbilitySystemComponent::OnRep_ActivateAbilities` | 客户端重建"标签 → Handle"索引 |
| `ARPG_PlayerState::SetNetUpdateFrequency(30)` | 玩家的 ASC 在 PlayerState 上，默认 1Hz，蒙太奇复制会卡 |

> 详细推导见 [`PHASE8_NETWORKING.md`](./PHASE8_NETWORKING.md) §1.5。

---

## 各章：逐个类展开

下面按层逐个类展开。每个类给出：**依赖谁 / 谁依赖它 / 参与的功能 / 关键成员 / 协作要点**。

---

## 一、框架层：Core / Input / Interfaces / World

本章覆盖的是"骨架"层：日志、标签、GameMode / PlayerState / PlayerController 三个框架类、输入配置资产、能力系统接口与静态库、以及场景效果触发器。它们的共同点是**不实现具体玩法**，而是决定"别的类能不能正确地彼此找到、能不能说同一种语言"。阅读时建议顺着两条主线看：一条是**输入的走向**（`URPG_InputConfig` → `ARPG_PlayerController` → `URPG_AbilitySystemComponent` → GA），另一条是**ASC 位置的差异**（`ARPG_PlayerState` 持有玩家 ASC，`IRPG_AbilitySystemInterface` + `GetASCInternal()` 把它抹平，`URPG_AbilitySystemLibrary` 再包一层给蓝图）。

---

### `LogRPG` / `LogRPG_Ability` / `LogRPG_Combat` / `LogRPG_AI` / `LogRPG_Animation` —— 按功能域拆分的 5 个日志类别

**文件**：`Source/RPG/Core/RPG_LogChannels.h` / `.cpp`　**继承**：无（`DECLARE_LOG_CATEGORY_EXTERN` / `DEFINE_LOG_CATEGORY` 宏生成的全局类别对象，不是类）

| | |
|---|---|
| **它依赖谁** | 引擎 `Logging/LogMacros.h`（宏定义）。**项目内零依赖** —— 它是整个模块的最底层，任何文件都可以包含它，它不包含任何项目文件 |
| **谁依赖它** | 全模块的日志出口。检索计数（含注释与字符串，非精确语句数）：`LogRPG_Combat` 76 处 / 22 文件、`LogRPG_Ability` 61 处 / 15 文件、`LogRPG_AI` 22 处 / 7 文件、`LogRPG_Animation` 12 处 / 6 文件、裸 `LogRPG` 19 处；合计 40 个 .h/.cpp 文件引用。最大单点使用者是 `RPG_PlayerController.cpp`（23 处）与 `RPG_AbilitySystemComponent.cpp`（22 处） |
| **参与的功能** | 全项目诊断：GAS 初始化失败、输入链路断点定位、连段缓存同步排查、联机 RPC 排查、EffectVolume 配置体检、动画通知广播 |
| **关键成员** | 五个类别：`LogRPG`（通用/未分类）、`LogRPG_Ability`（GAS 层）、`LogRPG_Combat`（战斗层）、`LogRPG_AI`（AI 层）、`LogRPG_Animation`（动画层）。默认级别统一为 `Log` 而不是 `Verbose` —— 交付版本不刷屏，开发期用控制台临时打开 |

**协作要点**：拆成 5 个类别而不是一个 `LogRPG`，原因是 GAS 的日志量（一次能力激活可能刷十几条），想看战斗判定就得在几百行里翻；拆开后可以 `Log LogRPG_Combat VeryVerbose` 单独开一类、`Log LogRPG_Combat Off` 单独关一类，也能写进 `Config/DefaultEngine.ini` 的 `[Core.Log]` 段开机生效。这套日志在本项目里不是"可选的可观测性"，而是**配置类故障的唯一线索来源** —— 因为项目里大量失败是静默的（IMC 里没配按键、`AddSpawnedAttribute` 漏调、PlayerStateClass 指错、`TriggerEvent` 为空），代码不报错但功能全失效，所以多个关键节点会主动打一条带"修复方法"的日志（如 `RPG_PlayerController::BeginPlay` 里对空 IMC 的检查、`ARPG_EffectVolume::BeginPlay` 对未配置 TriggerEvent 的警告）。

---

### `RPGTags` —— 全项目原生 GameplayTag 的集中声明与定义处

**文件**：`Source/RPG/Core/RPG_GameplayTags.h` / `.cpp`　**继承**：无（命名空间 `namespace RPGTags` + `UE_DECLARE_GAMEPLAY_TAG_EXTERN` / `UE_DEFINE_GAMEPLAY_TAG_COMMENT` 宏）

| | |
|---|---|
| **它依赖谁** | `NativeGameplayTags.h`（标签声明/定义宏）与 GameplayTags 模块。**项目内零依赖**，与日志类别同属最底层 |
| **谁依赖它** | 23 个 .cpp 文件、约 90 处 `RPGTags::Xxx` 形式的引用，实际用到 36 个不同标签。按层分布：**GA 层**（`RPG_GameplayAbilityBase`、`GA_LightAttack/HeavyAttack/Dodge/Sprint/Jump/Heal/ApplyBuff/StaminaRegen/Death/HitReact`）；**属性与伤害**（`RPG_DamageExecution` 用 `Data.Damage.Multiplier`、`RPG_AttributeSet` 用 `State.Dead` / `State.Stamina.Blocked`）；**角色**（`RPG_BaseCharacter` 用 `State.Dead` 做存活判定）；**动画**（`RPG_AnimInstanceBase` 用 `State.Sprinting/Attacking/Dead`，3 个 `AnimNotifyState` + `AnimNotify_AttackEnd` 广播 `Event.Combat.*` / `Event.Character.*`）；**AI**（`RPG_BTTask_Attack` 用 `Input.Attack.Light` 作默认输入标签、轮询 `State.Attacking`）；**UI**（`RPG_HUDWidget` 用 `State.Attack.Transition` 等）；**本层**（`RPG_PlayerState`、`RPG_AbilitySystemLibrary` 的 `State.Dead` 兜底）。资产侧按标签名引用：`DA_RPG_InputConfig` 的 `Input.*` 映射、各 GA 的 `AssetTags`、GE 的 `GrantedTags`、蒙太奇上的 AnimNotify（未逐一核实） |
| **参与的功能** | 输入→能力的映射（`Input.*`）、能力身份与取消组（`Ability.*`）、连段衔接窗口与伤害判定窗口（`State.Attack.ComboWindow` / `State.Attack.Active`）、无敌帧（`State.Invulnerable`）、耐力恢复阻断（`State.Stamina.Blocked`）、受击与死亡（`Event.Combat.Hit` / `State.Dead`）、拾取效果（`Event.Item.*`）、SetByCaller 传参（`Data.*`）、冷却（`Cooldown.*`）、攻击模组（`Attack.Module.*`）、特效音效（`GameplayCue.*`） |
| **关键成员** | 共 **80 个标签**（.h 声明 80 条，对应的 .cpp 定义了 80 条），按域分组：`Input`(9) / `Ability`(20) / `State`(20) / `Event`(20) / `Data`(5) / `Cooldown`(3) / `Attack.Module`(3) / `GameplayCue`(6)。命名规范：`<域>.<类别>.<子类别>.<具体>`，不超过 5 段、单数名词 |

**协作要点**：用 C++ 声明而不是纯 `DefaultGameplayTags.ini`，核心收益是**拼错标签名编译不过** —— ini 方案的 `RequestGameplayTag(FName("Input.Attack.Light"))` 写错只在运行时炸，而且炸在很远的地方；代价是改标签要重新编译，所以工程约定"影响代码逻辑的骨架标签用 C++，纯内容配置标签可另建 ini"。`Input.*` 与 `Ability.*` 刻意分成两套，是**两级解耦**：按键 → `Input.Attack.Light` →（`URPG_InputConfig` 里的映射表）→ `Ability.Attack.Light`，改键位不碰代码、一个技能可绑多个键；若按键直接绑能力，这两个域就焊死了。`Ability.Attack` 这个父标签本身不挂任何能力，只作 `CancelAbilities()` 的取消组 —— 靠标签的层级匹配，将来加 `Ability.Attack.Special` 时"受击打断攻击"那处代码不用回来改。`Ability.ApplyEffect` 被增益和减益**共用**，区分具体效果靠 GE 的 `GrantedTags`，避免"减防 GE 顶着 AttackUp 标签"这类误导。

---

### `ARPG_GameModeBase` —— 项目默认 GameMode，只做一件事：把各默认类型指向本项目的 C++ 类

**文件**：`Source/RPG/Core/RPG_GameModeBase.h` / `.cpp`　**继承**：`AGameModeBase`

| | |
|---|---|
| **它依赖谁** | `ARPG_Player`（`DefaultPawnClass`）、`ARPG_PlayerState`（`PlayerStateClass`）、`ARPG_PlayerController`（`PlayerControllerClass`）、`ARPG_HUD`（`HUDClass`）、`LogRPG`。全部只在构造函数里用一次，是整个 Core 层里依赖面最宽、但每个依赖都最浅的类 |
| **谁依赖它** | `Config/DefaultEngine.ini` 的 `GlobalDefaultGameMode` 指向 `/Game/_My/Core/BP_RPG_GameModeBase`（蓝图子类，实际运行时用的是它）；`ARPG_Player::GetRespawnTransform()` 通过 `World->GetAuthGameMode<AGameModeBase>()` + `FindPlayerStart()` 查询出生点（依赖的是引擎基类，不是本类）；引擎的 `ClientSetHUD` 流程间接决定 HUD 实例的生成时机 |
| **参与的功能** | 启动装配（默认 Pawn / PlayerState / PlayerController / HUD 四件套）、重生点查询、联机（每个本地玩家一个 HUD 实例的语义） |
| **关键成员** | 构造函数里 4 行 `*Class = Xxx::StaticClass()`；**没有任何 `UPROPERTY`、没有任何运行时状态**，全类只有一个构造函数 |

**协作要点**：这个类存在的意义是"**即使不做任何蓝图子类也能跑起来**"—— C++ 侧给一套可用的默认值，而实际项目几乎一定会在 `BP_RPG_GameModeBase` 里覆盖 `DefaultPawnClass` 指向 `BP_RPG_Player`，因为角色上要配 `InputConfig` / `StartupAbilities` / `InitAttributesEffect` 这些资产引用，只有在编辑器里能可视化指定；`HUDClass` 同理，`WBP_RPG_HUD` 是蓝图资产，C++ 引用不到。最容易踩的坑写在构造函数注释里：**`PlayerStateClass` 必须是 `ARPG_PlayerState`**，否则玩家的 ASC 无处安放，`GetASCInternal()` 恒返回 `nullptr`，症状是"技能全失效但不报任何错"—— 项目里最隐蔽的一个配置陷阱。另外 `HUDClass` 给的是每个本地玩家一个 `AHUD` 的引擎语义（listen server 主机上远程玩家的 PC 也有自己的 HUD，靠 `ARPG_HUD::BeginPlay` 里的 `IsLocalController()` 挡掉）。

---

### `ARPG_PlayerState` —— 玩家 ASC 的宿主，也是"死亡后状态延续"这一设计决策的落点

**文件**：`Source/RPG/Core/RPG_PlayerState.h` / `.cpp`　**继承**：`APlayerState` + `IAbilitySystemInterface`（引擎）+ `IRPG_AbilitySystemInterface`（本项目）

| | |
|---|---|
| **它依赖谁** | `URPG_AbilitySystemComponent`（`CreateDefaultSubobject` 创建 + `AddSpawnedAttribute` 登记）、`URPG_AttributeSet`（同上，敌我共用同一个类）、`RPGTags::State_Dead`（`IsAlive` 的判据）、`LogRPG`；接口侧依赖 `Interfaces/RPG_AbilitySystemInterface.h` 与引擎 `AbilitySystemInterface.h` |
| **谁依赖它** | `ARPG_Player`（`GetASCInternal()` 里 `Cast<ARPG_PlayerState>(GetPlayerState())` 取 ASC；`InitializeAbilitySystem()` 里把 `PS` 作为 `InitAbilityActorInfo` 的 **Owner**、把角色自身作为 **Avatar**）、`ARPG_GameModeBase`（`PlayerStateClass`）、资产侧 `BP_RPG_PlayerState`（蓝图子类）与 `BP_RPG_GameModeBase`（指定前者的引用）。GAS 内部机制也依赖它 —— 因为 ASC 的宿主是它，属性/标签/GE 的复制通道都挂在 PlayerState 上 |
| **参与的功能** | 玩家 GAS 初始化 / 死亡与重生 / 联机属性与蒙太奇同步 / 技能冷却与 Buff 的跨重生延续 / UI 的数据来源（间接，UI 走角色接口而不是直接找它） |
| **关键成员** | `AbilitySystemComponent`（`TObjectPtr<URPG_AbilitySystemComponent>`，`VisibleAnywhere BlueprintReadOnly`，挂在 PlayerState 而非角色上）、`AttributeSet`（`TObjectPtr<URPG_AttributeSet>`）、`SetNetUpdateFrequency(30.f)`（★ 见下）、`GetRPGAbilitySystemComponent()`（`BlueprintPure`，省去每次 `Cast`）、`GetAbilitySystemComponent()` / `GetRPGAttributeSet()` / `IsAlive()` 三个接口实现 |

**协作要点**：ASC 放 PlayerState 上有三个理由，最实际的是**死亡重生**：角色死亡时 Character 会被销毁，ASC 挂它身上的话冷却、Buff 剩余时间、属性数值一并消失；挂 PlayerState 上则重生后自然延续，让"死亡惩罚清不清 Buff"变成设计可选项而不是硬约束。第二个理由是 GAS 里 `OwnerActor`（能力归属，用于来源/团队判定）与 `AvatarActor`（表现载体，用于动画/GameplayCue）本就独立，"魂还在、身体没了"这个语义天然对应 `Owner=PlayerState`、`Avatar=角色`。第三个是复制正确性 —— ASC 挂 Character 会导致"重生后客户端拿不到正确 ASC 引用"这类经典联机 bug。**`SetNetUpdateFrequency(30.f)` 是本类最容易被忽略的一行**：`APlayerState` 构造函数硬编码了 `SetNetUpdateFrequency(1)`，而 ASC 的 `RepAnimMontageInfo`（蒙太奇同步）跟着 PlayerState 的通道走，不提速就会出现"看另一个玩家出招像幻灯片、看敌人却正常"（敌人的 ASC 在角色身上，走默认频率）。注意这是整个 PlayerState 的复制频率而非只是蒙太奇，所以设为 30 与引擎 `NetServerMaxTickRate` 默认值持平即可，再高也发不出去。另外 `AddSpawnedAttribute(AttributeSet)` 这一行不能省 —— 漏了不报错，但症状是"伤害没反应、血条不动"，极难查。

---

### `ARPG_PlayerController` —— 所有玩家输入的入口：把增强输入事件翻译成 `Input.*` 标签并路由到 ASC

**文件**：`Source/RPG/Core/RPG_PlayerController.h` / `.cpp`　**继承**：`APlayerController`

| | |
|---|---|
| **它依赖谁** | `URPG_InputConfig`（`InputConfig` 成员；`BeginPlay` 读 `DefaultMappingContext` / `MappingContextPriority` 注册 IMC，`SetupInputComponent` 遍历 `AbilityInputMappings` 批量绑定）、`UEnhancedInputComponent`（`BindAction`，支持附加参数）、`ULocalPlayer` + `UEnhancedInputLocalPlayerSubsystem`（`AddMappingContext`）、`ARPG_BaseCharacter`（`Move` / `Look` / `ToggleCrouch` / `IsAlive` / `GetCombatComponent` / `GetRPGAttributeSet` / `GetAbilitySystemComponent`）、`URPG_CombatComponent`（`PushInputTag` / `GetBufferedInputCount`）、`URPG_AbilitySystemComponent`（`TryActivateAbilityByInputTag` / `HasAbilityForInputTag` / `NotifyInputReleased` / `GetOwnedGameplayTags`）、`URPG_AttributeSet`（调试命令读 8 个属性）、`GEngine`（`DebugPrint` 的屏幕输出）、`LogRPG` / `LogRPG_Ability` |
| **谁依赖它** | `ARPG_GameModeBase`（`PlayerControllerClass`）、`BP_RPG_PlayerController`（蓝图子类，在 Details 面板里指定 `DA_RPG_InputConfig`）、`BP_RPG_GameModeBase`（引用前者）；**引擎输入管线**依赖它保持 Tick 开启：`APlayerController::TickActor` → `TickPlayerInput` → `UPlayerInput::Tick` 求值所有 IMC 的 Trigger 才产生 `Started`/`Triggered`/`Completed` 事件。此外 `URPG_AbilitySystemComponent::TryActivateAbilityByInputTag` 与 `URPG_BTTask_Attack` 的注释都把它标为"玩家侧的那一半链路" |
| **参与的功能** | 移动 / 视角 / 蹲伏（不经 GAS）、轻击连段（输入缓存 + 服务器 RPC）、重击蓄力与松手释放（`Completed` 事件）、闪避 / 跳跃 / 奔跑 / 法术激活、死亡期间输入拦截、联机输入同步、控制台调试命令 |
| **关键成员** | `InputConfig`（`EditDefaultsOnly`，蓝图指定）；`Server_PushInputTag(FGameplayTag)`（`UFUNCTION(Server, Reliable)` —— 无 `_Validate`，参数校验写在实现体里的两道门）；`OnAbilityInputPressed` / `OnAbilityInputReleased`（**所有**能力输入共用，靠绑定时附加的 `Mapping.InputTag` 区分）；`OnMove` / `OnLook` / `OnCrouch`；`GetRPGCharacter()` / `GetRPGAbilitySystemComponent()`；`RPGPrintAttributes` / `RPGPrintTags`（`UFUNCTION(Exec)`） |

**协作要点**：这个类体现的是**两类输入的分工**：移动/视角/蹲伏直接调角色方法（没有冷却、消耗、不可被打断，走 GAS 是纯负担），能力类输入转成 `Input.*` 标签交给 ASC（需要冷却、耐力、前摇后摇、被状态标签阻断）。能力输入只有一个回调函数，靠 `BindAction(..., Mapping.InputTag)` 把标签作为附加参数绑进去 —— 新增技能时只改 `DA_RPG_InputConfig`，这个文件一行都不用动。按键处理里最关键的顺序是**先 `Combat->PushInputTag()` 再 `ASC->TryActivateAbilityByInputTag()`**：只激活不推缓存的话，攻击进行中的按键会激活失败然后被彻底丢弃，表现是"连按没有衔接，只能等上一段播完再按"。`Server_PushInputTag` 这个 RPC 解决的是一个具体的联机故障：`URPG_CombatComponent` 的输入缓存是**本机运行时对象，从来不复制**，而服务器那份轻击 GA 的连段推进（`TryStartNextSegment` → `ConsumeInputTag`）读的正是这个缓存 —— 缓存空着，服务器永远连不上第二段，第 1 段一结束就 `EndAbility` 并通过 `ClientEndAbility` 把客户端正在播的动画用 `Montage_Stop(0.f)` **硬切**掉；因为引擎只在 `!IsLocallyControlled()` 时才发这条 RPC，主机的 Pawn 收不到，所以症状是"客户端出招一顿一顿、主机完全正常"。RPC 用 `Reliable` 是刻意的：丢一条输入就是"这一段被吞了"，玩家会感觉按键失灵，而人手按键的频率极低，代价可以忽略。服务器侧设了两道门（`HasAbilityForInputTag` 白名单防止垃圾标签占满容量为 4 的缓存、`IsAlive` 存活判断），但它们防的是"改过的客户端"而不是"别的玩家"（后者由 `UNetDriver::ShouldCallRemoteFunction` 的 Owner 校验和 `bOnlyRelevantToOwner` 挡住）。死亡拦截放在控制器而不是各个 GA 里，理由是"控制器是玩家意图的入口，死人没有意图应该在最靠前的地方被挡掉"，而 GA 的 `ActivationBlockedTags` 仍然保留，给 AI 和其他非输入路径兜底 —— 两道防线管的不是同一件事。最后，构造函数里那行"绝对不要写"的注释（`PrimaryActorTick.bCanEverTick = false`）也值得单独看：增强输入的事件恰恰是在每帧的输入处理里评估 IMC Trigger 才产生的，关掉 Tick 等于掐断整条输入管线，症状是所有按键无反应且不报错。

---

### `URPG_InputConfig`（含 `FRPG_InputActionMapping`） —— 把"哪个按键对应哪个输入标签"从代码挪到资产里的映射表

**文件**：`Source/RPG/Input/RPG_InputConfig.h` / `.cpp`　**继承**：`UPrimaryDataAsset`（结构体 `FRPG_InputActionMapping` 为普通 `USTRUCT`）

| | |
|---|---|
| **它依赖谁** | 引擎增强输入资产 `UInputAction` / `UInputMappingContext`、`FGameplayTag`（`Input.*` 命名空间）、`UPrimaryDataAsset`。**不依赖任何项目类** —— 连 `ARPG_PlayerController` 都不包含，是纯粹的数据载体 |
| **谁依赖它** | `ARPG_PlayerController`（唯一 C++ 使用者：`BeginPlay` 取 `DefaultMappingContext` / `MappingContextPriority`，`SetupInputComponent` 读 `MoveAction` / `LookAction` / `CrouchAction` 并遍历 `AbilityInputMappings`）；资产侧 `DA_RPG_InputConfig`（唯一实例），被 `BP_RPG_PlayerController` 引用 |
| **参与的功能** | 输入方案配置（键鼠 / 手柄 / 触屏换一套资产即可）、按键重绑定、战斗模式覆盖探索模式的 IMC 优先级、新技能的接入（只改资产） |
| **关键成员** | `DefaultMappingContext`（IMC）、`MappingContextPriority`、`MoveAction` / `LookAction` / `CrouchAction`（Locomotion 分类，不走 GAS）、`AbilityInputMappings`（`TArray<FRPG_InputActionMapping>`，每项含 `InputAction` + `InputTag` + `Description`）；三个查询函数 `FindInputTagForAction` / `FindActionForInputTag` / `GetAllAbilityActions` |

**协作要点**：映射方向是 **Action → Tag** 而不是反过来，因为同一个动作可能在不同情境下代表不同意图（长按/短按），而动作资产是唯一的配置点，在它这里指定标签最自然；内部查表用**指针相等**而不是名字比较，因为 `UInputAction` 是资产，指针就是它的身份。`FindInputTagForAction` / `FindActionForInputTag` / `GetAllAbilityActions` 这三个查询函数**当前在 `Source/` 与 `Content/` 下都检索不到调用点**（未核实二进制资产的完整引用表）—— `ARPG_PlayerController` 是直接遍历 `AbilityInputMappings` 数组的，这三个函数是为"按键重绑定 UI / 运行时切换输入方案"预留的接口。另外要注意 `DefaultMappingContext` 与 `AbilityInputMappings` 管的是**两件不同的事**：IMC 决定"哪个键触发哪个 InputAction"，本资产决定"哪个 InputAction 对应哪个输入标签"，`RPG_PlayerController::BeginPlay` 里专门检查了"IMC 添加成功但里面一条映射都没有"这种情况（一个空 IMC 照样能添加成功，但按什么键都不会有反应）。

---

### `IRPG_AbilitySystemInterface` —— 把"ASC 可能挂在 PlayerState、也可能挂在自己身上"这个差异封装掉

**文件**：`Source/RPG/Interfaces/RPG_AbilitySystemInterface.h`（**无 .cpp**，纯接口）　**继承**：无（`UINTERFACE` + `IRPG_AbilitySystemInterface`，`MinimalAPI` + `BlueprintType`）

| | |
|---|---|
| **它依赖谁** | **项目内零依赖**，只前向声明 `URPG_AttributeSet`。刻意不依赖引擎的 `IAbilitySystemInterface` —— 两者是并列关系，不是继承关系 |
| **谁依赖它** | **实现者**：`ARPG_BaseCharacter`（玩家与敌人共用一份实现，通过虚函数 `GetASCInternal()` 把差异下推给子类）、`ARPG_PlayerState`（玩家 ASC 的真正宿主）。**消费者**：`URPG_AbilitySystemLibrary::IsAlive()` 用 `Cast<IRPG_AbilitySystemInterface>` 优先走接口。间接消费者：`ARPG_AIController`、`RPG_BTDecorator_CanAttack`、`RPG_BTService_CombatUpdate`、`ARPG_PlayerController`、`ARPG_EffectVolume` 都通过 `ARPG_BaseCharacter::IsAlive()` 走这条路 |
| **参与的功能** | ASC 位置差异封装、存活判定（AI 选目标 / 伤害前置检查 / 死亡输入拦截 / 拾取是否生效）、属性集查询（调试命令、UI、GA 读属性） |
| **关键成员** | 两个纯虚函数：`GetRPGAttributeSet()`（返回裸指针，可能为 `nullptr`，调用方必须判空）、`IsAlive()`。**刻意不提供 `GetAbilitySystemComponent()`** —— 那是引擎接口的职责 |

**协作要点**：这个接口与引擎的 `IAbilitySystemInterface` 是**分工而不是重复**：引擎那个只有一个 `GetAbilitySystemComponent()`，且 GAS 内部机制（`UAbilitySystemGlobals::GetAbilitySystemComponentFromActor`）会 Cast 它，所以**必须实现**；本项目这个只补充游戏层需要的查询，不重复提供 ASC 访问器。`IsAlive()` 的判据是 `State.Dead` 标签而不是 Health 数值 —— 因为标签是"状态"的唯一真相源（它会复制，数值可能被各种 Buff 短暂改写）。两个实现者都遵循同一条兜底规则：**ASC 尚未初始化时返回 `true`**，否则刚生成的角色会在第一帧被 AI 当成尸体忽略掉。这个接口与 `ARPG_BaseCharacter::GetASCInternal()` 的配合是"少写一份实现"的典型：接口实现只有一份、行为绝对一致，新增角色类型（召唤物等）只需实现 `GetASCInternal()` 一个虚函数。

---

### `URPG_AbilitySystemLibrary` —— GAS 相关的静态查询工具，专为蓝图和"不想每次判空"的调用点准备

**文件**：`Source/RPG/AbilitySystem/RPG_AbilitySystemLibrary.h` / `.cpp`　**继承**：`UBlueprintFunctionLibrary`

| | |
|---|---|
| **它依赖谁** | `UAbilitySystemComponent::GetSetOnActor<T>()`（取属性集的实际实现，它内部走 `IAbilitySystemInterface`，所以玩家和敌人都能正确解析）、`UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent()`（引擎库，直接用，不重复造）、`URPG_AttributeSet`、`IRPG_AbilitySystemInterface`（`IsAlive` 优先走接口）、`RPGTags::State_Dead`（接口缺失时的兜底判定） |
| **谁依赖它** | **在 `Source/` 与 `Content/` 下均检索不到调用点**（未核实二进制资产的完整引用表）—— 四个函数全是 `UFUNCTION(BlueprintPure, meta = (DefaultToSelf = "Actor"))`，设计意图是给蓝图（HUD Widget、未来的拾取/法术蓝图）用，目前 C++ 侧尚未启用。`ARPG_PlayerController` 的调试命令走的是 `RPGChar->GetRPGAttributeSet()` 这条更直接的路径 |
| **参与的功能** | 蓝图侧读属性条 / AI 与伤害的存活前置检查（设计用途）/ 收口"ASC 挂在哪"的样板代码 |
| **关键成员** | 4 个 `static` 函数：`GetRPGAttributeSet(Actor)`、`IsAlive(Actor)`、`HasGameplayTag(Actor, Tag)`、`GetAttributeValue(Actor, Attribute)` |

**协作要点**：它解决的两个具体痛点是：① 引擎的 `UAbilitySystemComponent::GetSetOnActor<T>()` 是 C++ 模板函数，`UFUNCTION` 不支持模板，所以想在蓝图里拿属性集读血条必须包一层；② 战斗代码里"取 ASC → 判空 → 查标签"三件套重复太多次。`IsAlive` 的实现顺序值得注意：**先 `Cast<IRPG_AbilitySystemInterface>` 走接口，取不到才退回 `State.Dead` 标签判定** —— 这样没实现接口的 Actor（可破坏物、召唤物）也能有一个合理的默认结果，同时给实现者留了"重写更严格判定"的空间。文件里多处 `const_cast<AActor*>` 的原因是引擎那两个函数的参数不是 const 指针，而本库只查询、绝不修改 Actor，所以对外保持 const 签名让 C++ 调用方也能传 const 指针。**这个库目前是"预留工具层"的定位**，如果迟迟没有蓝图调用点，可以考虑是否还需要维护它。

---

### `ARPG_EffectVolume` —— 场景里会碰撞的效果触发器：药水 / 治疗泉 / 毒池 / 陷阱

**文件**：`Source/RPG/World/RPG_EffectVolume.h` / `.cpp`　**继承**：`AActor`（`UCLASS(Blueprintable)`）

| | |
|---|---|
| **它依赖谁** | `USphereComponent`（根组件、重叠检测）、`UStaticMeshComponent`（外观，挂在碰撞球下面）、`UAbilitySystemBlueprintLibrary::SendGameplayEventToActor`（唯一的出口动作）、`ARPG_BaseCharacter`（`Cast` 出触发者、`IsAlive()`、`GetAbilitySystemComponent()`）、`URPG_GameplayAbilityBase::RespondsToGameplayEvent()`（发送前的诊断查询）、`FScopedAbilityListLock` + `ASC->GetActivatableAbilities()`（遍历能力列表必须持锁）、`TimerManager`（周期触发与重生）、`LogRPG_Combat` / `LogRPG_Ability`。**注意它不直接依赖 `URPG_AttributeSet` 或任何 GE 施加 API** |
| **谁依赖它** | 资产侧：`BP_Pickup_HealthPotion`、`BP_Pickup_AttackUpPotion_`、`BP_Pickup_DefenseDown`（蓝图子类，在 Class Defaults 里配 `TriggerEvent` / `EffectClass` / `Magnitude`），布置在 `L_RPG_TestArena.umap`（`Content/_My/BP/` 下另有两个同名前缀的小文件未检索到对本类的引用，疑似占位，未核实）。**响应侧**：`URPG_GA_Heal`（构造函数里 `TriggerData.TriggerTag = RPGTags::Event_Item_Heal`）、`URPG_GA_ApplyBuff`（一条 Trigger 配 `Event.Item.Buff`、另一条配 `Event.Item.Debuff`）—— 它们通过 GA 的 `AbilityTriggers` 接住事件 |
| **参与的功能** | 拾取药水（进入触发 + 用完消失）/ 治疗泉（范围内周期触发）/ 毒池与熔岩（周期减益）/ 一次性范围陷阱 / 定时重生 / "用掉了"的联机同步 |
| **关键成员** | `TriggerEvent`（`FGameplayTag`，配 `Event.Item.Heal/Buff/Debuff` 之一）、`EffectClass`（`TSubclassOf<UGameplayEffect>`，**这是"新增一种效果不用改代码"的地方**）、`Magnitude`（0 表示用 GA 的默认值）；两个枚举 `ERPG_EffectTriggerMode`（`OnEnter` / `WhileInside`）与 `ERPG_EffectTargetMode`（`TriggeringActor` / `AllOverlapping`）；`RepeatInterval`、`bConsumeOnTrigger`、`RespawnDelay`、`bAffectDead`；`bConsumed`（`ReplicatedUsing = OnRep_bConsumed`，`DOREPLIFETIME_CONDITION(..., COND_None)`）、`RepeatTimerHandle` / `RespawnTimerHandle`；`TriggerSphere` 是根组件，`Mesh` 挂在它下面 |

**协作要点**：本类最重要的一条约定是**它不直接施加 GameplayEffect，而是发一个 GameplayEvent 让 GA 去施加**（`ASC 初始化 → 触发 GA → GE 上 Buff/标签 → GC 特效` 是项目定死的链路）。直接 `ApplyGameplayEffectToTarget` 短期能跑，但会一次性丢掉消耗（要不要花耐力/有冷却）、动画（喝药有抬手动作）、打断与阻挡（死了或眩晕中不能捡）、能力层标签（`State.UsingItem` 之类）这四样东西；走事件链的额外收益是**新增一种效果不需要改 C++** —— 做一个新 GE 资产、在拾取物上指过去就完事。事件载荷里带两样参数：`EventMagnitude` 传数值、`EffectClass` 塞进 `OptionalObject` 传给 GA（GA 优先用它、拿不到才回落到自己配的默认 GE）；`Instigator` 指向**触发器本身**而不是某个角色，语义是"效果来自这口泉水"，为将来区分"被陷阱打的"和"被敌人打的"留口。把 `UClass` 塞进 `OptionalObject`（一个 `UObject*`）是本类唯一不太优雅的地方，作者在注释里写清了取舍：让 GA 反过来 `Cast<ARPG_EffectVolume>` 读配置会使 AbilitySystem 层依赖 World 层，而为单个字段建 Payload UObject 又太重；**如果将来效果源种类变多（法术、陷阱、光环各一套），就该抽接口了** —— 这是明确的演进信号。联机方面有三处必须守住：① `HasAuthority()` 守卫必须写在 `OnSphereBeginOverlap` 最前面（重叠事件在服务器和每个客户端都会触发，靠"客户端不会走到这里"来省会导致每台机器各施加一次，表现是治疗量翻倍且只在联机时出现）；② `bConsumed` 用 `COND_None` 复制而不是 `COND_OwnerOnly`，否则只有拾取者自己看得见它消失；③ 服务器上设 `bConsumed` **不会**触发 `OnRep`（OnRep 只在客户端收到复制时调），所以 `ConsumeVolume` 里要自己刷一次表现，否则 listen server 主机会看到已被捡走的药水还杵在地上。最后，`ApplyEffectTo` 里那段"真的有人会响应这个事件吗"的诊断值得单独看：GAS 对"事件发出去没有人接"是**完全沉默**的，所以这里主动遍历 `ActivatableAbilities`（必须持 `FScopedAbilityListLock`）调 `RespondsToGameplayEvent` 查一遍，没人接就打一条带排查步骤的 Warning —— 这也解释了为什么 `URPG_GameplayAbilityBase` 要提供那个函数：`UGameplayAbility::AbilityTriggers` 是 `protected` 的，只有派生类的成员函数读得到。

---

## 二、角色层与动画层：Character / Animation

本章覆盖 `Source/RPG/Character/`（3 个类）与 `Source/RPG/Animation/`（1 个基类 + 1 个类型头 + 4 个 AnimNotify）。
角度是**类之间怎么咬合**：谁被谁持有、谁在什么时机调谁、为什么这件事归它做。
逐函数的说明见 `Docs/CODE_REFERENCE.md`。

一句话概括这一层的形状：

```
                    ARPG_PlayerController            ARPG_AIController
                      （绑定输入）                      （决策 + 感知）
                            │                                │
                            ▼                                ▼
   GA_*（能力层）──────►  ARPG_BaseCharacter  ◄──── URPG_AnimInstanceBase
        │               （接口唯一实现 + 组件宿主）         ▲
        │                        ▲                        │ 每帧读标签/速度
        │  GameplayEvent         │ EnterRagdoll 等         │
        ▼                        │                        │
   AnimNotify ──────────►  蒙太奇（动画帧）──────────► ABP_RPG_Base
   （只广播事实）                                        （只连线）
```

---

### `ARPG_BaseCharacter` —— 玩家与敌人的共同基类，也是整个 GAS 接入的唯一收口点

**文件**：`Source/RPG/Character/RPG_BaseCharacter.h` / `.cpp`　**继承**：`ACharacter`（+ `IAbilitySystemInterface`、`IRPG_AbilitySystemInterface`）

| | |
|---|---|
| **它依赖谁** | **`URPG_CombatComponent`**（`Combat/RPG_CombatComponent.h`）：构造函数里 `CreateDefaultSubobject` 建一份，持有输入缓存 / 连段索引 / 当前攻击模组；`ResetForRespawn()` 调它的 `ClearInputBuffer()` 与 `ResetCombo()`。**`URPG_OverheadHealthBarComponent`**（`UI/`）：同样是默认子对象，挂在 Root 的 (0,0,110)，`CastShadow=false`。**`UAbilitySystemComponent` + `URPG_AttributeSet`**：`GetRPGAttributeSet()` 内部 `ASC->GetSet<URPG_AttributeSet>()`（带 `const_cast`）；`IsAlive()` 查 `State_Dead`；`SetCombatMovement()` 用 `AddLooseGameplayTags/RemoveLooseGameplayTags` 挂 `State.Sprinting`；`ResetForRespawn()` 摘 `State.Dead`、`RemoveActiveEffects(FGameplayEffectQuery())`、重放 `InitAttributesEffect`。**`URPG_AbilitySystemComponent`**：`ResetForRespawn()` 里 `Cast` 后调 `ReactivatePassiveAbilities()`，把被 `CancelAllAbilities` 停掉的常驻被动重新拉起来。**`ARPG_HUD`**（`UI/RPG_HUD.h`）：`Multicast_ShowDamageNumber` 遍历 PlayerController 找到本地那个后调 `HUD->ShowDamageNumber()`。**`RPGTags`**（`Core/RPG_GameplayTags.h`）：`State_Dead`、`State_Sprinting`。**引擎组件**：`USpringArmComponent` / `UCameraComponent` / `UCharacterMovementComponent` / `UCapsuleComponent` / `USkeletalMeshComponent`。**`UAnimMontage`**：只作为受击蒙太奇池与死亡蒙太奇的配置项被持有，从不自己播。 |
| **谁依赖它** | **`ARPG_Player` / `ARPG_Enemy`**：唯一两个子类。**`URPG_GameplayAbilityBase::GetRPGCharacter()`**：所有 GA 取角色的统一入口（`Cast<ARPG_BaseCharacter>(GetAvatarActorFromActorInfo())`）。**`URPG_GA_Death`**：`OnDeathStarted()` / `GetDeathMontage()` / `GetDeathMontagePlayRate()` / `EnterRagdoll()` / `StartRespawnCountdown()`。**`URPG_GA_HitReact`**：`PickHitReactMontage()` / `GetHitReactPlayRate()`。**`URPG_GA_Sprint`**：`StartSprint()` / `StopSprint()`。**`ARPG_PlayerController`**：`Move()` / `Look()` / `ToggleCrouch()` / `IsAlive()`（两道门）/ `GetCombatComponent()` / `GetAbilitySystemComponent()` / `GetRPGAttributeSet()`。**`ARPG_AIController`**：`IsAlive()` 当感知回调的前置闸门（死人不再响应感知）、`SetCombatMovement()`。**`URPG_BTTask_Attack`**：`GetAbilitySystemComponent()` + `GetCombatComponent()`，故意走基类以便对玩家和敌人一视同仁。**`URPG_BTDecorator_CanAttack`**：对**自己和目标**各查一次 `IsAlive()`。**`URPG_BTService_CombatUpdate`**：查目标 `IsAlive()` 决定是否脱战。**`URPG_AnimInstanceBase`**：`TryGetPawnOwner()` 转成它，之后读 `GetCharacterMovement()` / `bIsCrouched` / `GetActorRotation()` / `GetAbilitySystemComponent()` / `GetCombatComponent()`。**`URPG_HUDWidget`**：`GetLocalCharacter()` 拿它，再取 ASC / CombatComponent / `GetRespawnDelay()`（死亡面板倒计时）。**`URPG_OverheadHealthBarWidget`**：`Cast<ARPG_BaseCharacter>(Owner)` 反查主人。**`URPG_AttributeSet::PostGameplayEffectExecute`**：对受击者调 `Multicast_ShowDamageNumber()`。**`ARPG_EffectVolume`**：`ApplyEffectTo(ARPG_BaseCharacter*)`、`CanAffect()`、`GetOverlappingActors(..., ARPG_BaseCharacter::StaticClass())`。 |
| **参与的功能** | 输入处理的**落地端**（Move / Look / Crouch 的实际实现，绑定在 PlayerController）／ 冲刺速度切换（GA_Sprint 调用）／ 战斗移动速度（AI 调用）／ 受击表现资产（蒙太奇池 + 速率）／ 死亡与布娃娃 / 重生 / 头顶血条可见性 / 伤害飘字广播 / GAS 接口层（ASC + 属性集 + 存活判定） |
| **关键成员** | 组件：`CameraBoom`、`FollowCamera`、`CombatComponent`、`OverheadHealthBar`。能力配置：`StartupAbilities`（`TMap<FGameplayTag, TSubclassOf<UGameplayAbility>>`，输入标签→能力）、`StartupPassiveAbilities`（无输入触发的，含常驻被动与事件驱动两类）、`InitAttributesEffect`（初始属性 GE）。表现配置：`HitReactMontages`（池）、`HitReactPlayRate`、`DeathMontage`、`DeathMontagePlayRate`。死亡/重生：`RespawnDelay`（0 = 不重生）、`bRagdollEnabled`（`ReplicatedUsing=OnRep_RagdollEnabled`）、`RespawnTimerHandle`。运行时缓存（都在 BeginPlay 记）：`CachedMeshRelativeTransform`、`CachedMeshCollisionProfile`、`CachedCapsuleCollisionEnabled`、`CachedSpawnTransform`。私有状态：`bInCombatMovement`。移动参数：`WalkSpeed` / `SprintSpeed` / `CrouchSpeed` / `CombatMoveSpeed` / `RotationRateYaw`。 |

**协作要点**：
这个类的核心设计取舍是"**接口实现留在基类、ASC 的来源下放给子类**"。`IAbilitySystemInterface` 与 `IRPG_AbilitySystemInterface` 的全部实现都在这儿（`GetAbilitySystemComponent()` 只是转发给 `GetASCInternal()`，`GetRPGAttributeSet()` / `IsAlive()` 建立在它之上），子类只需实现一个 `GetASCInternal()`。好处是"玩家 ASC 在 PlayerState、敌人 ASC 在自己身上"这个差异被完全封死在子类里 —— 上层（GA、AI、HUD、`ARPG_EffectVolume`）一律调 `GetAbilitySystemComponent()`，感知不到差异。

`CombatComponent` 被刻意做成**不依赖任何 GAS 类**（只存输入缓存 / 连段索引 / 攻击模组），所以它挂在基类而不是两个子类各建一份；"当前是否在攻击"这类**状态**则一律走 GameplayTag，避免两份真相。

`IsLocallyControlledPlayer()` 是一个不能省的封装：它比 `IsLocallyControlled()` 多要求"控制器必须是 `APlayerController`"，因为 `AController::IsLocalController()` 在 Standalone 下无脑返回 true、且对权威的 AIController 也返回 true —— 直接用会把单机下所有敌人都判成"本地玩家自己"，头顶血条和伤害飘字一起被吞掉，而且只在单机/主机上复现。`RefreshOverheadWidgetVisibility()` 在 `BeginPlay` / `PossessedBy` / `OnRep_Controller` 三处被调，缺一不可（BeginPlay 早于 Possess；客户端的 Controller 要靠复制才到）。同理 `Multicast_ShowDamageNumber()` 挑本地 PC 时用 `PC->IsLocalController()` 逐个过滤，而不用 `GetFirstPlayerController()`（那个没有任何本地性过滤，主机上只是"碰巧"对）。

布娃娃这块，**复制的是 `bRagdollEnabled` 这个开关，不是物理状态**：Chaos 在各端各自模拟，服务器必须手动调一次 `OnRep_RagdollEnabled()`（OnRep 不会在服务器自动触发），客户端靠复制触发的 OnRep 跑同一套 API；`EnterRagdoll` / `ExitRagdoll` 各有一道幂等守卫。`RefreshMaxWalkSpeed()` 是"改速度"的唯一入口，优先级固定为 **蹲伏 > 战斗 > 常态**（冲刺不参与，由 `StartSprint` 直接写、`StopSprint` 回到这个入口）。

---

### `ARPG_Player` —— 玩家角色：不创建 ASC，只负责把它接到自己身上

**文件**：`Source/RPG/Character/RPG_Player.h` / `.cpp`　**继承**：`ARPG_BaseCharacter`

| | |
|---|---|
| **它依赖谁** | **`ARPG_PlayerState`**（`Core/RPG_PlayerState.h`）：`GetASCInternal()` 里 `Cast` 它再取 `GetAbilitySystemComponent()`（ASC 挂在 PlayerState 上）。**`URPG_AbilitySystemComponent`**：`InitAbilityActorInfo(PS, this)` 建立 Owner=PlayerState / Avatar=self 的关联；`RegisterInputAbilityMappings(StartupAbilities)` 登记映射表；`GrantInputAbilities()` 授予；`GivePassiveAbility()` 逐个授予被动。**`AGameModeBase::FindPlayerStart(GetController())`**：`GetRespawnTransform()` 里走引擎的选点逻辑。**`AController`**：`OnRespawned()` 里 `SetControlRotation()` + `ClientSetLocation()`。**`APlayerState`**：`InitializeAbilitySystem()` 的判空对象。 |
| **谁依赖它** | **`ARPG_GameModeBase`**（`Core/RPG_GameModeBase.cpp:26`）：`DefaultPawnClass = ARPG_Player::StaticClass()` —— 这是全工程**唯一**一处用 C++ 点名 `ARPG_Player` 的地方。其余所有代码（GA / AI / UI / 控制器）都只认 `ARPG_BaseCharacter` 或接口。**`RPG_AbilitySystemLibrary.h`**：注释里把它当作"ASC 在 PlayerState 上"的示例。 |
| **参与的功能** | GAS 初始化（ActorInfo 关联 / 属性 / 能力授予）／ 输入能力的映射表登记（两端）／ 重生点解析（PlayerStart）／ 复活后的位置与视角同步 |
| **关键成员** | `bAbilitySystemInitialized`（幂等标志，保证授予能力与应用初始属性只做一次）。类本身**没有新增任何组件或 UPROPERTY** —— 构造函数是空的，且有意留了注释说明"不在这里创建 ASC 不是忘了写"。 |

**协作要点**：
`InitializeAbilitySystem()` 被 `PossessedBy` / `OnRep_PlayerState` / `BeginPlay` **三处**调用，且写成幂等的 —— 因为这三个时机的先后顺序在单机、联机、不同初始化路径下都不确定，与其去追引擎内部的调用顺序，不如写成"谁来都行、只生效一次"。

函数内部有一条**必须两端都执行**的步骤：`RegisterInputAbilityMappings(StartupAbilities)`。它是纯配置表而不是运行时状态，客户端也要有 —— 因为按 `Input.Attack.Light` 激活能力走的是 `TryActivateAbilityByInputTag`，那张表就是翻译器；少了它，客户端按键会静默地"没有绑定任何能力"，而单机测试永远发现不了。紧随其后的 `HasAuthority()` 闸门只挡住"授予能力"和"应用初始属性"两件事（状态由服务器产生、客户端消费复制结果）。如果 ASC 不是 `URPG_AbilitySystemComponent`，函数会**提前 return 而不置 `bAbilitySystemInitialized`**，否则后面所有初始化时机都会因为那个标志变成空操作，只留下一行 Error 当线索。

重生路径上它和敌人分道扬镳：`GetRespawnTransform()` 覆写成找 PlayerStart（敌人用基类的"出生点"），`OnRespawned()` 额外推一次 `ClientSetLocation()` —— 因为本地控制的角色在客户端是 AutonomousProxy，位置复制条件是 `COND_SimulatedOnly` 根本不会发给它，而 `ControlRotation` 压根不是复制属性，服务器算完必须显式推给客户端。

---

### `ARPG_Enemy` —— 敌人角色：自持 ASC，并把"身体死亡"和"大脑停止"接起来

**文件**：`Source/RPG/Character/RPG_Enemy.h` / `.cpp`　**继承**：`ARPG_BaseCharacter`

| | |
|---|---|
| **它依赖谁** | **`URPG_AbilitySystemComponent` + `URPG_AttributeSet`**：构造函数里各 `CreateDefaultSubobject` 一份（`AttributeSet` 的登记推迟到 `InitializeAbilitySystem()`，因为构造函数会对 CDO 执行多次，而 `AddSpawnedAttribute` 会改内部数组）。**`ARPG_AIController`**（`AI/RPG_AIController.h`）：构造函数里设 `AIControllerClass = ARPG_AIController::StaticClass()` 与 `AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned`；`OnDeathStarted()` / `OnRespawned()` 里 `Cast` 它并调 `StopAI()` / `RestartAI()`。**`URPG_AttackModuleData` 间接**：AI 用的能力通过基类的 `StartupAbilities` 走同一套映射，自己不碰。 |
| **谁依赖它** | **`ARPG_AIController::InitializeBlackboardValues()`**：`Cast<ARPG_Enemy>(GetPawn())` 后读 `AttackRange` 写进黑板（让行为树节点不必知道具体角色类）。**`URPG_BTTask_Patrol`**：`Cast<ARPG_Enemy>(AIController->GetPawn())` 后读 `PatrolPoints`。**`RPG_OverheadHealthBarWidget.cpp:258`**：注释里引用它的 `InitializeAbilitySystem()`（属性集是那时才登记的）。 |
| **参与的功能** | 自持 ASC 的 GAS 初始化／AI 接管（控制器类 + 自动占有）／死亡时停 AI、重生时重启 AI／巡逻点与攻击距离的数据源 |
| **关键成员** | `AbilitySystemComponent`（`VisibleAnywhere`）、`AttributeSet`、`PatrolPoints`（`EditInstanceOnly`，每只敌人可不同、不必做蓝图子类）、`AttackRange`、`LoseTargetDistance`、`bAbilitySystemInitialized`。另有 `GetRPGAbilitySystemComponent()`（`BlueprintPure`，**C++ 侧无调用者**，供蓝图/AI 节点使用）。 |

**协作要点**：
"ASC 放哪"在这里和玩家**故意不对称**：敌人没有"重生后保留冷却与 Buff"的需求，所以 ASC 与属性集都放自己身上，少一层间接寻址、少一个要维护生命周期的外部对象。这个差异被基类的接口完全抹平，调用方感知不到。

`OnDeathStarted()` / `OnRespawned()` 是基类留的**空实现钩子**，由 `URPG_GA_Death` 在固定时机调用，只有敌人重写。这里做的是行为树做不了的事：行为树没有"我死了"的概念，不主动停它就会一直写黑板、发起寻路、尝试激活攻击能力，而那时移动模式已被布娃娃关掉，寻路必然失败，任务会卡在一个永不结束的 Latent Task 上。两个钩子严格配对，只停不重启的后果是"敌人复活后站着不动，且不报错"。

`InitializeAbilitySystem()` 与玩家那份是**同构**的：同样两端登记映射表、同样只在服务器授予能力与应用属性、同样"先属性后能力"。敌人其实用不上那张映射表（AI 只在服务器跑），仍然两端登记是为了让初始化流程保持唯一形状 —— 不同构的代价是以后每次改初始化都要想两遍。`BTTask_Attack` 那边也特意用基类的 `GetAbilitySystemComponent()` 而不是这个类的 `GetRPGAbilitySystemComponent()`，就是为了让这段代码对玩家和敌人一视同仁。

---

### `URPG_AnimInstanceBase` —— 动画的数据来源层：把"真相源"翻译成动画能懂的布尔量

**文件**：`Source/RPG/Animation/RPG_AnimInstanceBase.h` / `.cpp`　**继承**：`UAnimInstance`

| | |
|---|---|
| **它依赖谁** | **`ARPG_BaseCharacter`**（`Character/RPG_BaseCharacter.h`）：`NativeInitializeAnimation` 与每帧 `CacheOwnerCharacter()` 用 `TryGetPawnOwner()` 转成它，以 `TWeakObjectPtr<ARPG_BaseCharacter> OwnerCharacter` 持有。**`UCharacterMovementComponent`**：读 `Velocity`、`IsFalling()`、`MaxWalkSpeed`、`MaxWalkSpeedCrouched`。**`UAbilitySystemComponent`**：`HasMatchingGameplayTag()` 读 7 个状态标签。**`URPG_CombatComponent`**：读 `GetComboIndex()`。**`URPG_AttackModuleData`**（`Combat/`）：读 `ModuleType`。**`RPGTags`**：`State_Attacking` / `State_Dodging` / `State_Sprinting` / `State_Attack_Charging(_Lv1/Lv2/Lv3)` / `State_Invulnerable` / `State_Dead`。**`ERPG_MovementState`**（`Animation/RPG_AnimationTypes.h`）与 **`ERPG_AttackModuleType`**（`Combat/RPG_CombatTypes.h`）。**引擎动画设施**：`UAnimMontage`、`FAnimMontageInstance`（取 `GetPlayRate()`）、`UAnimInstance::OnMontageStarted` / `OnMontageEnded` 两个动态多播委托。 |
| **谁依赖它** | **`ABP_RPG_Base`（蓝图，Anim Class 在角色蓝图上指定）** 及其子类 `ABP_RPG_Player` / `ABP_RPG_Enemy`：AnimGraph 只读它暴露的 `BlueprintReadOnly` 变量连线，**不在蓝图里做任何计算**。**C++ 侧没有任何类引用它** —— 它是动画层的终点，只被蓝图消费。**文档/注释层面的引用**：`RPG_BaseCharacter.cpp:422`、`Core/RPG_GameplayTags.h:153`、`UI/RPG_HUDWidget.h:51` 都以它为例说明"状态一律从标签读"这条约定。 |
| **参与的功能** | 移动状态机（走/跑/蹲/腾空的切换依据 + 混合空间输入）／战斗表现的标签翻译（攻击、闪避、冲刺、蓄力段位、无敌、死亡）／连段索引与攻击模组供动画选资产／蒙太奇生命周期诊断（"出招顿挫"的量化排查） |
| **关键成员** | 移动类：`Speed`（水平速度，已剔除垂直分量）、`MaxSpeed`、`SpeedRatio`（**混合空间横轴用这个**）、`Direction`（0 前 / +90 右 / -90 左 / ±180 后）、`VerticalVelocity`、`bIsInAir`、`bIsCrouching`、`MovementState`。战斗类：`bIsAttacking`、`bIsDodging`、`bIsSprinting`、`bIsCharging`、`ChargeLevel`、`bIsInvulnerable`、`bIsDead`、`AttackModuleType`、`ComboIndex`。阈值：`IdleSpeedThreshold`。诊断：`MontagePlayRecords`（`TMap<TWeakObjectPtr<UAnimMontage>, TArray<FMontagePlayRecord>>`，**值是数组不是单条**）、`MontageCutShortWarnRatio`、内部结构 `FMontagePlayRecord{StartTime, Length, PlayRate}`。 |

**协作要点**：
这个类站在 `CharacterMovement / GameplayTag`（真相源）与 `ABP_RPG_Base`（连线）之间，唯一的职责是**每帧把状态翻译成布尔量**。两条硬约定决定了它的写法：

① **状态一律从 GameplayTag 读，不让 GA 推变量**。GA 手写 `bIsAttacking = true/false` 有三个必踩的坑：被打断时走的是 `EndAbility` 的另一条分支容易漏复位、联机下预测与权威不一致、以及和标签形成两份真相。读标签则天然免疫 —— 标签的生命周期由 GAS 托管，能力结束、被打断、死亡清空能力都会自动摘干净。代价是每帧几次哈希查找，可忽略。

② **每帧重新取 ASC，不做缓存**。玩家的 ASC 挂在 PlayerState 上，而 PlayerState 会随重生、关卡切换、联机重连被整个替换；缓存 WeakObjectPtr 就必须自己处理失效时机，漏一个就是"复活之后动画再也不对了"。拿不到 ASC 时**不能提前 return**，必须把成员逐个复位 —— 否则角色池复用（重生）时会带着上一个角色的攻击姿态。

每帧的调用顺序也有讲究：**先 `UpdateCombatState()` 后 `UpdateLocomotion()`**，因为 `MovementState` 的冲刺分支要读 `bIsSprinting`，反过来会让冲刺状态慢一帧。`MovementState` 的判定是优先级链：腾空 > 蹲伏 > 冲刺 > 常态；冲刺分支要**标签与速度同时成立**（`bIsSprinting && Speed > IdleSpeedThreshold`）—— 只看标签会让站着不动的敌人在 Sprinting 状态机里播到混合空间最慢的采样点，表现是**原地踏步**。

`MontagePlayRecords` 是给"出招动画有顿挫感"这句主观描述做的量化工具。它挂在 AnimInstance 而不是 GA，因为 GA 只知道"我请求播放了"，蒙太奇被谁停掉（自己结束 / 被别的蒙太奇顶掉 / 能力结束 / 预测被拒）它不一定知情，而 AnimInstance 是所有路径的必经之地；用引擎的 `OnMontageStarted` / `OnMontageEnded` 多播委托也就自动覆盖了全部打断来源。两个细节：记录必须**按蒙太奇资产存一个数组**（连段切段是"先停上一段、再播下一段"，两段在同一帧重叠，只存"当前那个"会把数字算乱），`Ended` 时取**最早一条**（FIFO）配对；判据必须把 `PlayRate` 换算成墙钟秒（角色上配了 `HitReactPlayRate` / `DeathMontagePlayRate`，不换算会把每次正常播完都报成"被提前结束"），而且不能只看 `bInterrupted`（`PlayMontageOrSkip` 用 `bStopWhenAbilityEnds=true`，播完再被顺手停掉也报 `true`）。委托用 `AddUniqueDynamic` 绑定、`NativeUninitializeAnimation()` 里 `RemoveDynamic` 成对解绑 —— `InitializeAnimation()` 第一句就是 `UninitializeAnimation()`，同一个实例可以被重复初始化，而 `AddDynamic` 不查重。

---

### `ERPG_MovementState`（`RPG_AnimationTypes.h`）—— 动画层唯一的类型定义：回答"该走哪条子状态机"

**文件**：`Source/RPG/Animation/RPG_AnimationTypes.h`（**只有头文件，无 .cpp**）　**继承**：不适用（`UENUM(BlueprintType)` 纯类型定义，`uint8` 底层类型）

| | |
|---|---|
| **它依赖谁** | **什么都不依赖**。整个头文件只 include `CoreMinimal.h` 和生成的头，刻意**不依赖任何 GAS 类** —— 和 `Combat/RPG_CombatTypes.h` 同一条约定：动画与战斗类型是表现层与数据层的公共词汇，让它们依赖 ASC 会让"单独测试状态计算"变得不可能。 |
| **谁依赖它** | **`URPG_AnimInstanceBase`**：`MovementState` 成员的类型，`UpdateLocomotion()` 里逐分支赋值，`GetAnimationDebugString()` 里经 `StaticEnum<>()` 转字符串。**`ABP_RPG_Base`（蓝图）**：Main 状态机的切换依据，四个值对应四条子状态机（走 / 冲刺 / 空中 / 蹲伏）。C++ 侧只有这一个消费者。 |
| **参与的功能** | 移动状态机的分支选择（不是"该播哪个动画"，而是"该进哪条状态机"） |
| **关键成员** | 四个枚举值：`Grounded`（地面常态，走/慢跑/站定都在这里）、`Sprinting`、`InAir`、`Crouching`。 |

**协作要点**：
这个枚举刻意**不按 Idle / Walk / Run / Sprint 划分**，因为那种划分会强迫 Main 状态机为"走→跑→冲刺"再画一圈转移线，和子状态机内部重复；而且每个组合（蹲走→蹲跑、空中走→空中跑……）都要单独连线，数量爆炸。它只区分**结构性差异**（在地面 / 腾空 / 蹲着 / 冲刺），速度的连续变化交给子状态机里的 BlendSpace 用 `SpeedRatio` 混合 —— 这也是 `URPG_AnimInstanceBase` 把 `SpeedRatio` 标为"混合空间横轴请用这个"的原因：本工程的 `MaxSpeed` 会变（走 300 / 冲刺 850 / 蹲 180），用固定阈值的 `Speed` 轴要为每种姿态各做一个混合空间。

判定的优先级顺序（腾空 > 蹲伏 > 冲刺 > 常态）写在 `UpdateLocomotion()` 里而不是枚举本身，语义是"腾空优先于一切"：从空中落下时就算还按着冲刺键也该播下落动画，蹲着从平台边缘掉下去同理。

---

### `URPG_AnimNotify_AttackEnd` —— 攻击段结束的单点通知，把"这一帧算打完"的决定权交给动画

**文件**：`Source/RPG/Animation/Notifies/RPG_AnimNotify_AttackEnd.h` / `.cpp`　**继承**：`UAnimNotify`

| | |
|---|---|
| **它依赖谁** | **`UAbilitySystemBlueprintLibrary::SendGameplayEventToActor()`**（`AbilitySystemBlueprintLibrary.h`）—— 唯一的通信手段。**`RPGTags::Event_Combat_AttackEnd`**（`Core/RPG_GameplayTags.h`）。**`USkeletalMeshComponent::GetOwner()`** + **`AActor`**：取"发事件的是谁"。**`LogRPG_Animation`**。装出来的 `FGameplayEventData` 只填三个字段：`EventTag`、`Instigator = Owner`、`Target = Owner`（**不带来源蒙太奇**，因为单点 Notify 不需要迟到事件过滤）。 |
| **谁依赖它** | **运行时**：`URPG_GA_LightAttack::OnAttackEndEvent()`（→ `FinishCombo(false)`，连段收尾）、`URPG_GA_HeavyAttack::OnAttackEndEvent()`（→ `FinishHeavyAttack(false)`），两者都通过 `UAbilityTask_WaitGameplayEvent` 监听这个标签。**编辑器校验**：`URPG_AttackModuleData::IsDataValid()` 在 `WITH_EDITOR` 下 `Cast<URPG_AnimNotify_AttackEnd>(Event.Notify)` 扫描蒙太奇，位置 < 70% 就标黄（"太靠前 → GA 会在这一帧结束能力并停掉动画，表现为连段中途断掉"）。**未核实**：具体哪几个蒙太奇资产挂了它，需要逐个打开资产看时间轴。 |
| **参与的功能** | 轻击连段收尾 / 重击收尾 / 蒙太奇资产的编辑器体检（连段能不能打全） |
| **关键成员** | 无 UPROPERTY。只有 `Notify()` 重写和 `GetNotifyName_Implementation()`（返回"攻击结束"，用于蒙太奇时间轴显示）。 |

**协作要点**：
用 Notify 而不是等 `PlayMontageAndWait` 的 `OnCompleted`，有三个理由：蒙太奇末尾常有"保持姿势"的尾巴而玩家在那段时间已经可以操作了，等它播完才结束能力会让操作迟滞；Notify 能把"哪一帧算结束"精确到帧，把尾巴留给过渡；有些段需要被衔接窗口提前接走，有显式结束点更好控制。GA 侧同时保留了 `OnMontageCompleted` 作为兜底（没配这个 Notify 时退化）。

它和 `ComboWindow` / `AttackWindow` 的一个关键差别是**不带来源蒙太奇**：它是单点 Notify，引擎不会在蒙太奇被停掉时补发它，所以没有"迟到事件"问题，GA 侧也就不需要对它做来源过滤。

链路是：动画帧 → `Notifies` → `SendGameplayEventToActor` → `Event.Combat.AttackEnd` → `UAbilityTask_WaitGameplayEvent` 回调 → GA 的 `OnAttackEndEvent` → `FinishCombo` / `FinishHeavyAttack`。Notifies 层在这里只做一件事：**广播事实，不做任何逻辑**。项目里还有一条同向的旁路 —— `RPG_BTTask_Attack` 判断"AI 的攻击结束了没"用的是 `State.Attacking` 标签而不是这个事件，因为标签覆盖所有结束路径（Notify / 蒙太奇自然播完 / 能力被取消 / 压根没激活成功），而事件只覆盖第一条。

---

### `URPG_AnimNotifyState_AttackWindow` —— 伤害判定窗口：动画决定"哪一刀真的能打到人"

**文件**：`Source/RPG/Animation/Notifies/RPG_AnimNotifyState_AttackWindow.h` / `.cpp`　**继承**：`UAnimNotifyState`

| | |
|---|---|
| **它依赖谁** | **`URPG_AttackWindowPayload`**（`Combat/RPG_AttackWindowPayload.h`）：`NotifyBegin` 里 `NewObject<URPG_AttackWindowPayload>(MeshComp)` 打包检测参数（Outer 故意传 `MeshComp` 而非 `GetTransientPackage()`，让载荷的生命周期跟着角色走）。**`ERPG_TraceSource`**（`Combat/RPG_CombatTypes.h`）：`TraceSource` 属性的类型。**`UAbilitySystemBlueprintLibrary`** + **`RPGTags::Event_Combat_AttackWindow_Open` / `_Close`**。**`USkeletalMeshComponent::GetOwner()`**、**`AActor`**、**`LogRPG_Animation`**。 |
| **谁依赖它** | **运行时**：`URPG_GA_LightAttack`（`OnAttackWindowOpen` / `OnAttackWindowClose`）与 `URPG_GA_HeavyAttack`（同名回调），两者都用 `UAbilityTask_WaitGameplayEvent` 监听；收到 Open 就建 `URPG_AbilityTask_WeaponTrace`（`CreateWeaponTraceTask`），Close 就 `EndTask()`。**编辑器校验**：`URPG_AttackModuleData::IsDataValid()` 用 `Cast<URPG_AnimNotifyState_AttackWindow>(Event.NotifyStateClass)` 扫描，缺了直接报"这一刀不会造成任何伤害"，并把位置换算成百分比写进校验信息。**反向约束**：重击的"蓄力起手 / 蓄力循环"蒙太奇用 `ValidateHoldMontage()` 检查 —— 这类蒙太奇上有判定窗口会被判为"每次摆姿势都白送一次伤害"。 |
| **参与的功能** | 轻击连段与重击的伤害判定窗口 / 武器轨迹检测的启停 / 蒙太奇资产的编辑器体检 |
| **关键成员** | `AttackTag`（`FGameplayTag`，日志区分第几段）、`bOverrideTrace`（是否用下面的参数覆盖攻击模组里的检测配置）、`TraceSource`、`TraceRadius`、`SocketStart`、`SocketEnd`（后四个都带 `EditCondition = "bOverrideTrace"`）。 |

**协作要点**：
`NotifyBegin` / `NotifyEnd` 各广播一个事件，参数走 `FGameplayEventData` 的两个通用槽：`OptionalObject` 放 `URPG_AttackWindowPayload`（检测参数），`OptionalObject2` 放**来源蒙太奇**。后者不是可选项 —— 引擎在蒙太奇被停掉时会为身上还活着的 NotifyState 补发 `NotifyEnd`（`UAnimInstance::TriggerMontageEndedEvent`），连段接下一段正是"停掉上一段的蒙太奇"，于是上一段的"判定窗口关闭"会在下一段刚播起来时才到达；GA 侧靠 `IsEventFromCurrentSegment(Payload)` 比 `OptionalObject2` 与 `CurrentSegmentMontage` 来忽略它，否则会把下一段刚开的轨迹检测任务直接掐掉，症状是"连招里后面几段打不到人"。

**为什么不在 Notify 里直接做伤害判定**是这一层最硬的边界：判定写进 Notify 就意味着数值和动画焊死（改数值要动动画资产）、无法脱离动画验证数值链路、而且联机下 Notify 的播放时机在各端可能有细微差异，把权威逻辑放这儿会引入难复现的 bug。所以 Notify 只负责"广播一个事实"，怎么响应是 GA 的事。GA 侧拿到载荷后也不是照单全收：`bOverrideTrace` 为假时仍然用 `URPG_AttackModuleData` 上的配置 —— **检测参数（半径、Socket）配在 Notify 上是因为它们本质由动画决定，而伤害倍率属于数值设计、留在 AttackModuleData 里**（改个倍率不该需要打开动画资产）。

一个已知局限写在 GA 侧：`IsEventFromCurrentSegment()` 比的是**蒙太奇资产**而不是播放实例，如果连续两段共用同一个蒙太奇资产就认不出迟到事件；本工程每段用各自独立的资产（`AM_Light_01~05`），踩不到这个边界。

---

### `URPG_AnimNotifyState_ComboWindow` —— 连段衔接窗口：连招手感的调参旋钮

**文件**：`Source/RPG/Animation/Notifies/RPG_AnimNotifyState_ComboWindow.h` / `.cpp`　**继承**：`UAnimNotifyState`

| | |
|---|---|
| **它依赖谁** | **`UAbilitySystemBlueprintLibrary::SendGameplayEventToActor()`** + **`RPGTags::Event_Combat_ComboWindow_Open` / `_Close`**。**`USkeletalMeshComponent::GetOwner()`**、**`AActor`**、**`LogRPG_Animation`**。事件里 `Instigator = Owner`、`OptionalObject2 = Animation`（来源蒙太奇），**不设 `Target`**。 |
| **谁依赖它** | **运行时**：`URPG_GA_LightAttack` —— `OnComboWindowOpen()`（置 `bComboWindowOpen` 并立刻 `TryStartNextSegment()`，不等窗口关闭）、`OnComboWindowClose()`（同样先 `TryStartNextSegment()`，没有可续的段就 `GetCombatComponent()->ResetCombo()`）。**编辑器校验**：`URPG_AttackModuleData::IsDataValid()` —— 用 `Cast<URPG_AnimNotifyState_ComboWindow>(Event.NotifyStateClass)` 扫描，非最后一段必须有（否则"这一招接不下去"）、最后一段不该有（否则"可以无限连下去"），重击释放与切手技按"不该有"处理，蓄力起手/循环按"一个通知都不该有"处理。 |
| **参与的功能** | 轻击连段衔接（决定能不能接下一段、什么时候接）／连段索引的重置时机／蒙太奇资产的编辑器体检 |
| **关键成员** | 无 UPROPERTY，纯事件通知。可调参数都在**蒙太奇时间轴上**（窗口的起止帧），不在代码里 —— 这正是设计意图。 |

**协作要点**：
它和输入缓存是一对分工：**输入缓存解决"玩家按早了"**（按键先记下来，等窗口开），**衔接窗口解决"什么时候能接"**（定义规则）。只有窗口没有缓存 → 按早了白按，不跟手；只有缓存没有窗口 → 可以无限快速连打，失去节奏。窗口太窄玩家必须卡精确时机（"连不上"），太宽则乱按也能连（失去节奏感），通常开在后摇的前 1/3 到 1/2、长约 0.3~0.5 秒，最后一段不开。这些数值只能在动画里逐帧调，所以 Notify 上没有任何可配属性。

"窗口关闭"同时是**连段重置的触发点**：到那时缓存里还没有输入就说明玩家停手了，GA 把 `bComboWindowOpen` 置回 false 并调 `CombatComponent->ResetCombo()` 把连段索引归零 —— 否则玩家停手后下一击会直接从第 N 段开始，这是连段系统最经典的 bug。（`RPGTags::State_Attack_ComboWindow` 这个标签已在 `Core/RPG_GameplayTags` 里声明，但 C++ 侧目前**没有**任何地方读写它 —— GA 用的是自己的 `bComboWindowOpen` 成员。）

它的 `OptionalObject2` 比判定窗口那条更要紧：连段推进的做法就是"停掉上一段的蒙太奇"，引擎会为上一段还活着的 NotifyState 补发 `NotifyEnd`，这条迟到的"衔接窗口关闭"会在接上下一段后一帧到达；GA 不辨来源的话会拿它当成"当前这一段的窗口关闭"、顺手把缓存里的下一次按键吃掉，**凭空多跳一段** —— 表现是"连招打不全 / 跳段"，而且只在快速连打时出现。用 `OptionalObject2` 而不是 `OptionalObject`，是因为后者留给了判定窗口传载荷。

---

### `URPG_AnimNotifyState_Invulnerability` —— 无敌帧窗口：闪避收益与手感的边界

**文件**：`Source/RPG/Animation/Notifies/RPG_AnimNotifyState_Invulnerability.h` / `.cpp`　**继承**：`UAnimNotifyState`

| | |
|---|---|
| **它依赖谁** | **`UAbilitySystemBlueprintLibrary::SendGameplayEventToActor()`** + **`RPGTags::Event_Character_Invulnerability_Begin` / `_End`**（注意命名空间是 `Event.Character.*` 而不是 `Event.Combat.*` —— 它不是战斗判定的一环，而是角色状态）。**`USkeletalMeshComponent::GetOwner()`**、**`AActor`**、**`LogRPG_Animation`**。 |
| **谁依赖它** | **`URPG_GA_Dodge`**：`BindGameplayEventListeners()` 里为这两个标签各建一个 `UAbilityTask_WaitGameplayEvent`，`OnInvulnerabilityBegin` → `ApplyInvulnerability()`（`ApplyGameplayEffectSpecToSelf` 施加 `InvulnerabilityEffectClass`，通常是 `GE_Invulnerable`，并保存句柄），`OnInvulnerabilityEnd` → `RemoveInvulnerability()`（按句柄 `RemoveActiveGameplayEffect`）。**下游消费者**：`URPG_AnimInstanceBase::UpdateCombatState()` 读 `State_Invulnerable` 标签填 `bIsInvulnerable`。**未核实**：这个 NotifyState 目前只挂在闪避蒙太奇上，还是有别的蒙太奇也用了。 |
| **参与的功能** | 闪避无敌帧（翻滚的无敌窗口）／闪避蒙太奇的编辑器配置 |
| **关键成员** | 无 UPROPERTY，纯事件通知。**注意它自己不挂任何标签** —— 真正上无敌状态的是 GA 侧的 `GE_Invulnerable`。 |

**协作要点**：
无敌帧的起止点直接决定闪避的收益和手感：开太晚玩家"明明翻出去了还是被打到"、关太早翻滚后半段暴露、关太晚可以无限翻滚规避一切。这三条边界和动画的视觉表现强相关（脚离地的那一帧、落地的前一帧），所以必须在动画里逐帧调，代码里不给固定秒数 —— 这就是它存在而不是在 GA 里写 `SetTimer` 的理由（GA 里那个定时器只作为"没配蒙太奇"时的模拟兜底）。

**为什么走 GE 而不是 `AddLooseGameplayTag`**：GE 在 GameplayDebugger 里能看到"无敌是哪个 GE 给的、还剩多久"；能力被打断时 GE 会随能力一起清理，不会残留一个永久无敌标签（那会表现为"敌人打不动我"且不报错，极难查）；将来要做"无敌期间免疫特定类型伤害"，GE 的标签要求能直接支持。GA 侧因此把 `RemoveInvulnerability()` 放在 `EndAbility()` 里统一收尾，覆盖所有结束路径。

和另外两个 NotifyState 的差别是**它不需要迟到事件过滤**：无敌状态由 GE 自己带着持续时间，重复 `Apply` 有 `InvulnerabilityHandle.IsValid()` 这道幂等守卫挡住，`Remove` 按句柄精确移除，所以即便蒙太奇被停掉时引擎补发了 `NotifyEnd`，最坏结果也只是提前结束无敌 —— 不会像判定窗口那样把别的东西掐掉。

---

### 这一层的三条横向规律

1. **ASC 的差异被封在一个虚函数里。** `IAbilitySystemInterface` 与 `IRPG_AbilitySystemInterface` 的实现只有 `ARPG_BaseCharacter` 一份，子类只实现 `GetASCInternal()`。上层（GA / AI / HUD / 效果触发器 / 动画）一律调接口，玩家 ASC 在 PlayerState、敌人 ASC 在自己身上这件事对它们不可见。

2. **状态的唯一真相源是 GameplayTag，不是成员变量。** `URPG_AnimInstanceBase` 每帧现查标签、`RPG_BTTask_Attack` 靠 `State.Attacking` 消失判断攻击结束、`ARPG_EffectVolume` 靠 `IsAlive()` 判活、头顶血条与 HUD 死亡面板靠 `State.Dead` —— 全都是为了让"能力被打断 / 被取消 / 死亡清空能力"这些路径自动保持一致，不必每处手写复位。

3. **动画层只广播事实，不做逻辑。** 四个 Notify 加起来只做两件事：拼 `FGameplayEventData`、`SendGameplayEventToActor`。判定、伤害、无敌、连段推进全部在 GA 侧；`ABP_RPG_Base` 的 AnimGraph 也只连线不计算。这条边界的收益是数值链路可以脱离动画资产独立验证，动画出问题时排查范围不会扩散到数值层。

---

## 三、GAS 层：AbilitySystem

> 本章讲"类之间怎么咬合"，不讲"函数干什么"（逐函数参考见 `Docs/CODE_REFERENCE.md`）。
>
> 本层共 15 个类：1 个 ASC + 1 个 AttributeSet + 1 个 GA 基类 + 10 个具体 GA
> + 1 个 Execution Calculation + 1 个 AbilityTask。
>
> **进出的两条主链路**（本章所有协作关系都挂在这两条线上）：
>
> ```
> 输入链路   Input.* 标签
>              → URPG_AbilitySystemComponent::InputTagToAbilityClass / InputTagToSpecHandle
>                → TryActivateAbility(SpecHandle) → 某个 GA
>
> 伤害链路   GA::ApplyDamageToTarget
>              → GE_Damage（内含 URPG_DamageExecution）
>                → 写元属性 IncomingDamage
>                  → URPG_AttributeSet::PostGameplayEffectExecute（全项目唯一扣血入口）
>                    → 广播 Event.Combat.Hit / Event.Combat.Death
> ```

---

### `URPG_AbilitySystemComponent` —— 输入层与能力层之间的翻译官，敌我共用

**文件**：`Source/RPG/AbilitySystem/RPG_AbilitySystemComponent.h/.cpp`　**继承**：`UAbilitySystemComponent`

| | |
|---|---|
| **它依赖谁** | `UGameplayAbility` / `FGameplayAbilitySpecHandle`（授予与激活的统一句柄）；`URPG_GameplayAbilityBase`（**唯一**的 GA 反向依赖点：读 CDO 上的 `ShouldActivateOnGranted()`、对实例调 `OnInputReleased()`）；`Core/RPG_LogChannels.h` 的 `LogRPG_Ability`；引擎侧的 `FScopedAbilityListLock`、`FindAbilitySpecFromClass`、`ActivatableAbilities.Items` |
| **谁依赖它** | `ARPG_PlayerState`（**持有**玩家的 ASC，暴露 `GetRPGAbilitySystemComponent()`）；`ARPG_Enemy`（**持有**敌人的 ASC）；`ARPG_Player::InitializeAbilitySystem`（`RegisterInputAbilityMappings` + `GrantInputAbilities` + `GivePassiveAbility`）；`ARPG_Enemy::InitializeAbilitySystem`（同上，完全同构）；`ARPG_PlayerController`（`TryActivateAbilityByInputTag` / `NotifyInputReleased` / `HasAbilityForInputTag`）；`URPG_BTTask_Attack`（AI 用 `TryActivateAbilityByInputTag`）；`ARPG_BaseCharacter`（重生时 `ReactivatePassiveAbilities`） |
| **参与的功能** | 输入→能力两级解耦 / 客户端能力索引重建 / 死亡后被动能力重启 / 激活失败的日志诊断 |
| **关键配置** | 构造：`SetIsReplicated(true)`、`SetReplicationMode(Mixed)`；`BeginPlay` 挂 `AbilityFailedCallbacks`。关键成员：`InputTagToAbilityClass`（配置表，**两端都登记**）、`InputTagToSpecHandle`（运行期索引，客户端由 `OnRep_ActivateAbilities` 重建）、`WarnedUnmappedTags` 与 `WarnedUnresolvedTags`（**刻意分成两个**去重集合） |

**协作要点**：它存在的理由是"映射表两端都要有，而授予只能服务器做"，所以 `RegisterInputAbilityMappings()`（纯配置，两端）与 `GrantInputAbilities()`（授予，仅服务器）被拆成两步 —— 合成一步时客户端映射表永远是空的，按任何键都失败且只在联机时出现。它对 GA 的依赖只有两处、都是通过基类：授予时读 CDO 的 `ShouldActivateOnGranted`（被动能力靠它自动拉起），松手时对实例调 `OnInputReleased()`（按住型能力靠它收招）—— **没有任何 `Cast` 到具体 GA 的代码**，这是"加新技能不用改 ASC"的前提。客户端侧的索引重建在 `OnRep_ActivateAbilities` 里按类反查（`FindAbilitySpecFromClass` 只返回第一个匹配，所以 `RegisterInputAbilityMappings` 里配了一道"两个标签映射到同一个 GA 类"的警告）。`TryActivateAbilityByInputTag` 的缓存未命中被拆成"配置里没有"和"能力还没复制到"两种情况，分别用两个去重集合报不同措辞的警告 —— 合成一条会把排查方向带反。

---

### `URPG_AttributeSet` —— 敌我共用的属性集，全项目唯一的扣血入口

**文件**：`Source/RPG/AbilitySystem/RPG_AttributeSet.h/.cpp`　**继承**：`UAttributeSet`

| | |
|---|---|
| **它依赖谁** | `AbilitySystemBlueprintLibrary`（`SendGameplayEventToActor` 广播 `Event.Combat.Hit` / `Event.Combat.Death`）；`ARPG_BaseCharacter`（`Cast` 后调 `Multicast_ShowDamageNumber`，并读 `GetSimpleCollisionHalfHeight` 做飘字高度兜底）；`Core/RPG_GameplayTags.h`（`State_Invulnerable` / `Event_Combat_Hit` / `Event_Combat_Death`）；`LogRPG_Combat` / `LogRPG_Ability`；`Net/UnrealNetwork.h` |
| **谁依赖它** | `ARPG_PlayerState`（持有玩家那份）、`ARPG_Enemy`（持有敌人那份，`AddSpawnedAttribute`）；`URPG_DamageExecution`（三处 `DECLARE_ATTRIBUTE_CAPTUREDEF` + 读静态常量 `DefenseConstant` / `MinDamage`）；`URPG_GameplayAbilityBase::GetRPGAttributeSet` / `HasEnoughStamina`；`URPG_GA_Dodge` / `URPG_GA_Jump` / `URPG_GA_Sprint` 的 `CanActivateAbility` / Tick 里读 `GetStamina()`；`ARPG_BaseCharacter::GetRPGAttributeSet`；`URPG_AbilitySystemLibrary::GetRPGAttributeSet` / `GetAttributeValue`；`URPG_HUDWidget`（订阅 8 个属性的 `GetGameplayAttributeValueChangeDelegate`）；`URPG_OverheadHealthBarWidget`（订阅 `GetHealthAttribute` / `GetMaxHealthAttribute`） |
| **参与的功能** | 伤害落地（扣血 + 无敌兜底 + 死亡判定）/ 伤害飘字广播 / 属性钳制 / 属性网络复制 |
| **关键配置** | 8 个复制属性 `DOREPLIFETIME_CONDITION_NOTIFY(..., COND_None, REPNOTIFY_Always)`；`IncomingDamage` **有意不注册复制**；`static constexpr float DefenseConstant = 100.f`、`MinDamage = 1.f`；构造函数给保底默认值（100/10/100/100），真实数值由 `GE_InitAttributes` 覆盖 |

**协作要点**：它是伤害链路的下游终点，也是**唯一**写 `Health` 的地方 —— `URPG_DamageExecution` 只把结果写进元属性 `IncomingDamage`，扣血、无敌检查、死亡判定全部集中在这里，加"格挡 / 吸血 / 顿帧"都不用回头改 Execution。角色实体一律取 `Data.Target.GetAvatarActor()` 而不是 `OwningActor`：玩家的 `OwnerActor` 是 PlayerState，`Cast<ARPG_BaseCharacter>(OwningActor)` 会静默失败（项目实际踩过，症状是打玩家不冒伤害数字）。事件广播与飘字被 `OwningActor->HasAuthority()` 严格限制在服务器 —— 属性修改是可预测可回滚的，**委托广播一旦发出去收不回来**，不挡会导致客户端预测出一次"假死/假受击"表现。死亡判定与受击广播是 `else if` 二选一，且 `OldHealth <= 0` 时提前返回 —— 这道早退**就是**"从活到死只触发一次"的成因（尸体挨打会走死亡分支而非受击分支）。`ClampAttribute` 被 `PreAttributeChange` 与 `PreAttributeBaseChange` 共用，保证"直接赋值"和"GE 修改"两条路径结果绝对一致。

---

### `URPG_GameplayAbilityBase` —— 所有能力的基类，把"角色/战斗组件/属性集/伤害/耐力"收口成一组封装

**文件**：`Source/RPG/AbilitySystem/Abilities/RPG_GameplayAbilityBase.h/.cpp`　**继承**：`UGameplayAbility`（`UCLASS(Abstract)`）

| | |
|---|---|
| **它依赖谁** | `ARPG_BaseCharacter`（`GetAvatarActorFromActorInfo` 转 RPG 角色）；`URPG_CombatComponent`（连段索引 + 输入缓存）；`URPG_AttributeSet`（`GetSet<URPG_AttributeSet>`）；`URPG_AttackModuleData`（攻击模组）；`UAbilityTask_PlayMontageAndWait` + `UAnimMontage`（`PlayMontageOrSkip`）；`UAbilitySystemBlueprintLibrary`（按 Actor 取 ASC）；`RPGTags`（`State_Dead` / `Data_Damage_Multiplier` / `Data_Stamina_Cost`）；`Engine/OverlapResult.h`（`PerformSimulatedMeleeHit` 的球形检测） |
| **谁依赖它** | 10 个具体 GA **全部**继承它并调用它的封装（`GetCombatComponent` / `GetAttackModule` / `PlayMontageOrSkip` / `ApplyDamageToTarget` / `ConsumeStamina` / `HasEnoughStamina` / `PerformSimulatedMeleeHit` / 三个载荷工具函数）；`URPG_AbilitySystemComponent`（`Cast` 后调 `ShouldActivateOnGranted()` / `OnInputReleased()`）；`ARPG_EffectVolume`（`Cast` 后调 `RespondsToGameplayEvent()` 做"有没有人接这个事件"的诊断） |
| **参与的功能** | 轻击连段 / 蓄力重击 / 闪避无敌帧 / 受击死亡 / 治疗拾取 / 全部 10 个能力 |
| **关键配置** | `InstancingPolicy = InstancedPerActor`（**显式覆盖** UE 5.8 的默认 `InstancedPerExecution`）；`NetExecutionPolicy = LocalPredicted`；`NetSecurityPolicy = ClientOrServer`；`ActivationBlockedTags += State.Dead`；`UPROPERTY`：`DamageEffectClass` / `StaminaCostEffectClass` / `StaminaRegenDelayEffectClass` / `bActivateOnGranted`；**无 `AbilityTriggers`**（事件触发由子类各自声明） |

**协作要点**：这一层最关键的作用是让 `CanActivateAbility` 的 CDO 陷阱只在一个地方被处理 —— 便捷查询（`GetRPGAttributeSet` 等）全部走 `CurrentActorInfo`，因此**不能**在 `CanActivateAbility` 里用；需要属性集的两个子类（`GA_Dodge` / `GA_Jump`）改为直接读参数里的 `ActorInfo->AbilitySystemComponent`。`ApplyDamageToTarget` 是 10 个能力唯一的伤害出口：它建 `GE_Damage` 的 Spec、用 `SetByCaller(Data.Damage.Multiplier)` 传倍率（**一个 GE 资产服务所有攻击段**）、并把 `HitResult` 塞进 `EffectContext`（下游 `URPG_AttributeSet` 取它当飘字位置，GameplayCue 取它当命中点）。耐力侧的 `ConsumeStamina` 同时做两件事：扣 `GE_StaminaCost`（传**负数**）与刷新 `GE_StaminaRegenDelay` 的阻断时长 —— 后者让"停手 3 秒才恢复"完全由标签 + GE Duration 表达，链路上没有任何一行计时代码。三个载荷工具函数（`ResolveEffectClassFromEvent` / `ResolveMagnitudeFromEvent` / `RespondsToGameplayEvent`）是把"`FGameplayEventData::OptionalObject` 里装 `UClass`"这个不优雅但轻量的约定**收敛到一处**，`RespondsToGameplayEvent` 之所以放基类而不是让触发器自己读，是因为 `AbilityTriggers` 是 protected。

---

### `URPG_GA_LightAttack` —— 轻击，一个能力内部跑完 5 段连段

**文件**：`Source/RPG/AbilitySystem/Abilities/RPG_GA_LightAttack.h/.cpp`　**继承**：`URPG_GameplayAbilityBase`

| | |
|---|---|
| **它依赖谁** | `URPG_CombatComponent`（`GetComboIndex` / `SetComboIndex` / `ResetCombo` / `ConsumeInputTag` / `PushInputTag`）；`URPG_AttackModuleData`（`GetLightSegment` / `GetLightSegmentCount` / `TraceSource` / `TraceRadius` / `LeftHandSocket` / `RightHandSocket` / `BladeStartSocket` / `BladeEndSocket`）；`URPG_AttackWindowPayload`（Notify 上的检测参数覆盖）；`URPG_AbilityTask_WeaponTrace`（`CreateWeaponTraceTask` + `OnHit`）；`UAbilityTask_WaitGameplayEvent`（5 个事件监听）；`UAbilityTask_PlayMontageAndWait`；`UAbilitySystemComponent::StopMontageIfCurrent`；`FTimerManager`（模拟时序）；`RPGTags`：`Event_Combat_AttackWindow_Open/Close`、`Event_Combat_ComboWindow_Open/Close`、`Event_Combat_AttackEnd`、`Input_Attack_Light`、`Ability_Attack_Light`、`Ability_Attack_Heavy`、`State_Attacking` |
| **谁依赖它** | `URPG_AbilitySystemComponent`（按 `Input.Attack.Light` 授予并激活 —— 映射配在角色/敌人的 `StartupAbilities` 里，C++ 未硬编码）；`ARPG_PlayerController`（按键时把 `Input.Attack.Light` 推入缓存，连段衔接靠它）；`URPG_BTTask_Attack`（AI 以同一个输入标签走同一条链路，**必须先推缓存**否则永远只能打第 1 段）；`URPG_GA_HeavyAttack`（读 `Ability.Attack.Light` 标签决定走切手技分支，并显式 `CancelAbilities` 取消它）；`URPG_GA_HitReact`（`CancelAbilitiesWithTag` 里的 `Ability.Attack` 父标签覆盖它）；`URPG_AnimNotifyState_AttackWindow` / `_ComboWindow` / `URPG_AnimNotify_AttackEnd`（广播它监听的那 5 个事件） |
| **参与的功能** | 轻击 5 段连段 / 武器轨迹伤害 / 耐力消耗 / 无蒙太奇时的模拟时序验证 |
| **关键配置** | `SetAssetTags(Ability.Attack.Light)`；`ActivationOwnedTags = { Ability.Attack.Light, State.Attacking }`；`CancelAbilitiesWithTag += Ability.Attack.Heavy`；`InstancingPolicy` / `NetExecutionPolicy` 继承基类的 `InstancedPerActor` / `LocalPredicted`；**无 `AbilityTriggers`**。`UPROPERTY`：`SimulatedSegmentDuration = 0.8` / `SimulatedComboWindowDuration = 0.5`；成员：`CurrentSegmentIndex` / `CurrentDamageMultiplier` / `bComboWindowOpen` / `CurrentSegmentMontageTask` / `CurrentSegmentMontage` / `TraceTask` |

**协作要点**：连段状态（第几段）存在 GA 的**成员变量**里，这正是基类把 `InstancingPolicy` 钉死在 `InstancedPerActor` 的原因 —— `InstancedPerExecution` 下每次激活都是新实例，连段会永远从第 1 段重来。输入不是"激活"驱动的而是"缓存"驱动的：`ActivateAbility` 先 `ConsumeInputTag` 吃掉触发本次激活的那条（不消耗会残留导致第一次开窗就多打一段），之后每段靠 `OnComboWindowOpen` / `OnComboWindowClose` 从 `URPG_CombatComponent` 取缓存续段；取到的若是别的意图（闪避）会 `PushInputTag` 放回去。**它最微妙的一处是 `IsEventFromCurrentSegment()`**：引擎会在蒙太奇被停掉时给所有活着的 NotifyState 补发 NotifyEnd（`UAnimInstance::TriggerMontageEndedEvent`），而"接下一段"恰恰就是停上一段，于是上一段的"衔接窗口关闭"会晚一帧到达并**吃掉缓存里的下一次按键**（症状：快速连打跳段）—— 靠 Notify 带上的来源蒙太奇（`OptionalObject2`）过滤。配套的第二处是 `StopCurrentSegmentMontage()` 的三件事（摘回调 + 停蒙太奇 + 清引用）：只 `EndTask()` 拦不住回调，因为 `ShouldBroadcastAbilityTaskDelegates()` 判的是**能力**是否激活（`AbilityTask.cpp:199`），而连段期间能力一直激活 —— 漏掉就会"连招永远打不全"。`EndAbility` 里**也要** `ResetCombo`：被切手技 `CancelAbilities` 取消时不走 `FinishCombo`，不补这一句会"打着打着突然从第 3 段起手"。

---

### `URPG_GA_HeavyAttack` —— 重击，同一个输入下的"3 段蓄力"与"切手技"双形态

**文件**：`Source/RPG/AbilitySystem/Abilities/RPG_GA_HeavyAttack.h/.cpp`　**继承**：`URPG_GameplayAbilityBase`

| | |
|---|---|
| **它依赖谁** | 与轻击同源的 `URPG_CombatComponent`（`ConsumeInputTag`）与 `URPG_AttackModuleData`（`GetHeavyLevel` / `GetChargeLevelForTime` / `HeavyAttack.ChargeStartMontage` / `ChargeLoopMontage` / `ChargeStaminaDrainPerSecond` / `ComboTransitionMontage` / `ComboTransitionMultiplier` / `ComboTransitionStaminaCost`）；`URPG_AttackWindowPayload`；`URPG_AbilityTask_WeaponTrace`；`UAbilityTask_WaitGameplayEvent`（3 个事件）；`UAbilitySystemComponent`（`HasMatchingGameplayTag` / `CancelAbilities` / `AddLooseGameplayTags` / `RemoveLooseGameplayTags` / `StopMontageIfCurrent`）；`FTimerManager`；`RPGTags`：`Ability_Attack_Light` / `Ability_Attack_Heavy` / `State_Attacking` / `State_Attack_Charging` + `_Lv1/2/3` / `State_Attack_Transition` |
| **谁依赖它** | `URPG_AbilitySystemComponent`（按 `Input.Attack.Heavy` 授予激活，映射在 `StartupAbilities` 配置）；`ARPG_PlayerController`（松手时 `NotifyInputReleased` → `OnInputReleased` → `ReleaseCharge`）；`URPG_GA_LightAttack`（`CancelAbilitiesWithTag += Ability.Attack.Heavy`，实现"轻击打断蓄力"）；`URPG_HUDWidget`（读 `State.Attack.Transition` / `State.Attack.Charging` 显示当前招式）；`URPG_AnimInstanceBase` 侧的动画蓝图（读 `State.Attack.Charging.Lv1/2/3` 切蓄力姿势） |
| **参与的功能** | 蓄力重击（3 段）/ 切手技 / 蓄力期间的持续耐力消耗与耗尽强制释放 / 切手蒙太奇与释放蒙太奇的伤害窗口 |
| **关键配置** | `SetAssetTags(Ability.Attack.Heavy)`；`ActivationOwnedTags = { Ability.Attack.Heavy, State.Attacking }`；**故意不在构造函数里设 `CancelAbilitiesWithTag`**；`InstancingPolicy` / `NetExecutionPolicy` 继承基类；**无 `AbilityTriggers`**。`UPROPERTY`：`ChargeTickInterval = 0.1`、`MaxChargeTime = 3.0`；成员：`bTransitionBranch` / `bReleasing` / `CurrentChargeLevel` / `ChargeElapsed` / `ActiveChargeTags` |

**协作要点**：分支判据是**激活瞬间** `ASC->HasMatchingGameplayTag(Ability.Attack.Light)`，而这一句必须在 `ActivateAbility` 的最前面 —— 因为声明式的 `CancelAbilitiesWithTag` 会在 `PreActivate()`（`GameplayAbility.cpp:999/1022`）里先跑，等到 `ActivateAbility()` 时轻击的 `ActivationOwnedTags` 已经被摘掉，切手技**永远不会被判定出来**且不报错。所以取消动作改成在读完标签后显式调 `ASC->CancelAbilities(&CancelTags, nullptr, this)`（传 `this` 排除自己；匹配用的是轻击的 **AssetTags**，不是 `ActivationOwnedTags` —— 传错会静默地什么都不取消）。蓄力状态全部以 **loose tag** 表达而不是 bool 成员：`State.Attack.Charging` 是父标签（覆盖还没到第 1 段门槛的起手期），Lv1/2/3 由 `UpdateChargeTags()` 同步，`ActiveChargeTags` 记下"实际挂了哪些"以保证增删成对；复制状态显式写成 `CountToOwner`（默认的 `None` 不复制，会"自己看得到蓄力、别人看你站着"）。它是轻击那套"多段蒙太奇残留"问题的翻版：`DetachCurrentStageMontageTask()` 必须把 `OnCompleted` 上**两个**回调都摘掉（起手段绑的是 `OnChargeStartMontageCompleted`，释放段绑 `OnMontageCompleted`），且 `bReleasing` 用来区分"正常的蓄力→释放切换"与"真被打断" —— 少了它，释放时会误判成被打断、招式刚出手就结束。

---

### `URPG_GA_Dodge` —— 闪避：位移 + 无敌帧 + 耐力代价

**文件**：`Source/RPG/AbilitySystem/Abilities/RPG_GA_Dodge.h/.cpp`　**继承**：`URPG_GameplayAbilityBase`

| | |
|---|---|
| **它依赖谁** | `URPG_AttributeSet`（`CanActivateAbility` 里读 `GetStamina()` —— **走参数里的 `ActorInfo`**，不用基类便捷函数）；`ACharacter`（`LaunchCharacter` 施加冲量）；`UAbilityTask_WaitGameplayEvent`（监听 `Event_Character_Invulnerability_Begin` / `_End`）；`UAbilityTask_PlayMontageAndWait`；`UAbilitySystemComponent`（`MakeOutgoingSpec` / `ApplyGameplayEffectSpecToSelf` / `RemoveActiveGameplayEffect`）；`FTimerManager`；`RPGTags`：`Ability_Dodge` / `State_Dodging` / `Event_Character_Invulnerability_Begin` / `Event_Character_Invulnerability_End` |
| **谁依赖它** | `URPG_AbilitySystemComponent`（按 `Input.Dodge` 授予激活，映射在 `StartupAbilities` 配置）；`URPG_AnimNotifyState_Invulnerability`（广播它监听的两个无敌帧事件）；`URPG_GA_HitReact`（`CancelAbilitiesWithTag += Ability.Dodge`）；`URPG_HUDWidget` 与 `URPG_AnimInstanceBase`（读 `State.Dodging`）；**间接**：`URPG_AttributeSet::PostGameplayEffectExecute` 与 `GE_Damage` 的 `TargetTagRequirements` 都检查它授予的 `State.Invulnerable` |
| **参与的功能** | 闪避无敌帧 / 翻滚位移 / 耐力消耗 / 无蒙太奇时的模拟无敌时序 |
| **关键配置** | `SetAssetTags(Ability.Dodge)`；`ActivationOwnedTags = { State.Dodging }`；`InstancingPolicy` / `NetExecutionPolicy` 继承基类的 `InstancedPerActor` / `LocalPredicted`；**无 `AbilityTriggers`**（无敌帧事件由 `WaitGameplayEvent` Task 监听，不是 `AbilityTriggers`）。`UPROPERTY`：`DodgeMontage` / `StaminaCost = 20` / `DodgeImpulse = 1200` / `InvulnerabilityEffectClass` / `SimulatedDodgeDuration = 0.7` / `SimulatedInvulnerabilityRatio = 0.6`；成员：`InvulnerabilityHandle`（`FActiveGameplayEffectHandle`） |

**协作要点**：无敌的**起止时机由蒙太奇上的 AnimNotifyState 精确定义**（脚离地那帧开、落地前那帧关），代码只负责收事件 → 挂/摘 GE，不用固定秒数 —— 这是"无敌帧必须和视觉对齐"这条要求的直接体现。无敌是通过 `InvulnerabilityEffectClass` 的 `TargetTags` 授予 `State.Invulnerable` 实现的，因此下游有**两道**消费者：第一道在 `GE_Damage` 资产上（GE 层面直接拒绝应用，连 Execution 都不跑），第二道是 `URPG_AttributeSet::PostGameplayEffectExecute` 的兜底检查（防陷阱 / DOT / 脚本直接写 `IncomingDamage` 打穿）。清理被刻意放在 `EndAbility` 而不是 `FinishDodge`：被 `CancelAbilitiesWithTag` 强行取消时不会走 `FinishDodge`，只在里面删 GE 会留下**永久无敌**且不报错。`RemoveActiveGameplayEffect(Handle)` 用句柄而不是"移除该类所有 GE"，是为了将来别的来源也给无敌时不误伤。选 `LaunchCharacter` 而非 RootMotion 的理由是闪避距离属于"要反复调的手感参数"。

---

### `URPG_GA_HitReact` —— 受击反应：整条链路没有一个直接调用点

**文件**：`Source/RPG/AbilitySystem/Abilities/RPG_GA_HitReact.h/.cpp`　**继承**：`URPG_GameplayAbilityBase`

| | |
|---|---|
| **它依赖谁** | `ARPG_BaseCharacter`（`PickHitReactMontage()` 挑蒙太奇、`GetHitReactPlayRate()` 读播放速率）；`UAbilityTask_PlayMontageAndWait`；`FTimerManager`（无蒙太奇时的模拟硬直）；`RPGTags`：`Event_Combat_Hit` / `State_Hit` / `Ability_Attack` / `Ability_Dodge` |
| **谁依赖它** | `URPG_AttributeSet::PostGameplayEffectExecute`（广播 `Event.Combat.Hit`，**只管广播，不知道本类存在**）；`UAbilitySystemComponent`（授予时把 `AbilityTriggers` 登记进事件表，是唯一的实际连接点）；`URPG_GA_Death`（`CancelAllAbilities` 会把正在播的受击蒙太奇扫掉）；动画蓝图 / AI（读 `State.Hit`） |
| **参与的功能** | 受击硬直 / 打断玩家攻击与闪避 / 无蒙太奇时的模拟硬直 |
| **关键配置** | `InstancingPolicy = InstancedPerActor`（显式写）；`NetExecutionPolicy = ServerOnly`；**`bRetriggerInstancedAbility = true`**（连挨两下要刷新硬直而不是第二下没反应）；`CancelAbilitiesWithTag = { Ability.Attack（父标签，一次覆盖轻击+重击）, Ability.Dodge }`；`ActivationOwnedTags = { State.Hit }`；**`AbilityTriggers = { Event.Combat.Hit @ GameplayEvent }`**；`UPROPERTY`：`SimulatedHitDuration = 0.4` |

**协作要点**：触发完全是声明式的 —— 属性集 `SendGameplayEventToActor`，GAS 在**授予时**就把"能接事件的能力"登记进一张表（`AbilitySystemComponent_Abilities.cpp:578`），`HandleGameplayEvent` 只遍历这张表；**没有 `AbilityTriggers` 的能力事件系统根本看不见它**，会被正常授予、日志一切正常、但永远等不到触发（这段构造函数代码一旦漏掉，整个功能就是死代码）。设 `ServerOnly` 的依据是事件本身只在服务器广播（属性集里的权威判断），顺带堵死"客户端伪造受击让敌人硬直"。`bRetriggerInstancedAbility = true` 让引擎先 `EndAbility` 旧实例再重新激活，所以 `EndAbility` 必须把旧任务清理干净 —— 且必须**先 `RemoveDynamic` 再 `EndTask`**，因为 `ShouldBroadcastAbilityTaskDelegates()` 判的是能力激活状态而不是任务状态，结束掉的任务照样会把 `OnCompleted` 送过来（这个坑项目在轻击连段上先踩过一次）。它只挂自己的 `State.Hit`、**不碰别人的动画** —— 被打断的攻击由它自己的 `EndAbility` 收尾，这条边界是"互相清理一定会漏"的直接对策。

---

### `URPG_GA_Death` —— 死亡流程的编排者，五个步骤顺序不能乱

**文件**：`Source/RPG/AbilitySystem/Abilities/RPG_GA_Death.h/.cpp`　**继承**：`URPG_GameplayAbilityBase`

| | |
|---|---|
| **它依赖谁** | `UAbilitySystemComponent`（`AddLooseGameplayTags(State.Dead, CountToOwner)` / `CancelAllAbilities(this)`）；`ARPG_BaseCharacter`（`OnDeathStarted()` / `GetDeathMontage()` / `GetDeathMontagePlayRate()` / `EnterRagdoll()` / `StartRespawnCountdown()`）；`UAbilityTask_PlayMontageAndWait`；`RPGTags`：`Event_Combat_Death` / `State_Dead` |
| **谁依赖它** | `URPG_AttributeSet::PostGameplayEffectExecute`（血量归零时广播 `Event.Combat.Death`）；`UAbilitySystemComponent`（事件触发登记 + 基类的 `ActivationBlockedTags += State.Dead` 顺带实现"死亡只能发生一次"）；`ARPG_BaseCharacter::Respawn`（复活时摘掉它挂的 `State.Dead` loose tag、`RemoveActiveEffects`、`ReactivatePassiveAbilities`）；`URPG_HUDWidget`（读 `State.Dead` 显示死亡面板）；`URPG_AnimInstanceBase`（读 `State.Dead`） |
| **参与的功能** | 受击死亡 / 布娃娃 / 重生倒计时 / AI 停止与输入停止 |
| **关键配置** | `InstancingPolicy = InstancedPerActor`；`NetExecutionPolicy = ServerOnly`；`bRetriggerInstancedAbility = false`；**`AbilityTriggers = { Event.Combat.Death @ GameplayEvent }`**；**不加任何 `ActivationOwnedTags`**（`State.Dead` 是 loose tag）；**刻意不动 `ActivationBlockedTags`**（基类的 `State.Dead` 正是防重入所需）；授予途径是角色的 `StartupPassiveAbilities`，但 `bActivateOnGranted` 保持 `false` —— 它是"待命"而不是"被动常驻" |

**协作要点**：① 挂 `State.Dead` 必须排在 ④ 播蒙太奇之前，因为从"血量归零"到"蒙太奇播完"之间有 1~2 秒，这段时间角色**还是活的** —— 不先挂标签，玩家会看到一具尸体在打拳；挂上之后基类的一条 `ActivationBlockedTags` 就挡掉了所有新激活，不需要在每个 GA 里写"死了不能放"。② `CancelAllAbilities(this)` 的目的不是"停动作"而是**清理它们挂着的 `ActivationOwnedTags`**（死在攻击中的角色带着 `State.Attacking` 会让动画蓝图一直摆战斗姿势），传 `this` 是为了不把自己取消掉。而这一步产生了下游依赖：`GA_StaminaRegen` 也被一起停掉，所以 `ARPG_BaseCharacter` 的复活流程必须调 `URPG_AbilitySystemComponent::ReactivatePassiveAbilities()`，否则症状是"复活后耐力永远不再恢复"且不报任何错。`State.Dead` 用 loose tag 而非 GE，是因为复活流程里有 `RemoveActiveEffects` —— 用 GE 会被顺手清掉，复活逻辑就得反过来"靠清掉了什么"推断状态。

---

### `URPG_GA_Jump` —— 跳跃：一次性耐力消耗 + 可变高度

**文件**：`Source/RPG/AbilitySystem/Abilities/RPG_GA_Jump.h/.cpp`　**继承**：`URPG_GameplayAbilityBase`

| | |
|---|---|
| **它依赖谁** | `URPG_AttributeSet`（`CanActivateAbility` 里读 `GetStamina()`，同样走参数里的 `ActorInfo`）；`ACharacter`（`CanJump()` / `Jump()` / `StopJumping()`）；`FTimerManager`（`MaxJumpHoldTime` 超时兜底）；`RPGTags`：`Ability_Jump` |
| **谁依赖它** | `URPG_AbilitySystemComponent`（按 `Input.Jump` 授予激活，映射在 `StartupAbilities` 配置）；`ARPG_PlayerController`（松手 → `NotifyInputReleased` → `OnInputReleased` → `StopJumping`）；`URPG_GA_HitReact`（`Ability.Attack` 父标签不覆盖它，所以受击不会打断跳跃 —— 当前设计如此） |
| **参与的功能** | 跳跃 / 可变高度（松手停止上升）/ 跳跃耐力消耗 |
| **关键配置** | `SetAssetTags(Ability.Jump)`；`InstancingPolicy` / `NetExecutionPolicy` 继承基类的 `InstancedPerActor` / `LocalPredicted`；**无 `AbilityTriggers`**；**无 `ActivationOwnedTags`**；`UPROPERTY`：`JumpStaminaCost = 10`、`MaxJumpHoldTime = 1.5` |

**协作要点**：做成 GA 而不是直接 `ACharacter::Jump()`，是因为跳跃要消耗耐力并与闪避、攻击**竞争同一份资源**，将来还要做"空中只能跳一次""受击禁止跳跃"这类规则 —— 有 GA 才有地方挂这些条件。激活后**刻意不立即结束**：可变高度跳跃需要"松手时调 `StopJumping()`"，这要求能力在滞空期间保持激活，代价用一个 `MaxJumpHoldTime` 超时兜底（防一直不松手导致能力永久激活）。耐力的前置检查放在 `CanActivateAbility` 而不是激活后再扣，避免"激活了但扣成负数"；同时它也是"必须用参数里的 `ActorInfo`"这条规则的第二个实践点（另一个是 `GA_Dodge`）。

---

### `URPG_GA_Sprint` —— 奔跑：按住型，持续消耗耐力

**文件**：`Source/RPG/AbilitySystem/Abilities/RPG_GA_Sprint.h/.cpp`　**继承**：`URPG_GameplayAbilityBase`

| | |
|---|---|
| **它依赖谁** | `URPG_AttributeSet`（`GetStamina()` 判"见底就不进入奔跑"与"耗尽自动中断"）；`ARPG_BaseCharacter`（`StartSprint()` / `StopSprint()` 切移动速度）；`ConsumeStamina`（**间接**依赖 `GE_StaminaCost` 与 `GE_StaminaRegenDelay`）；`FTimerManager`；`RPGTags`：`Ability_Sprint` / `State_Sprinting` |
| **谁依赖它** | `URPG_AbilitySystemComponent`（按 `Input.Sprint` 授予激活，映射在 `StartupAbilities` 配置）；`ARPG_PlayerController`（松手 → `NotifyInputReleased` → `OnInputReleased` → `FinishSprint`）；`URPG_AnimInstanceBase` / 动画蓝图（读 `State.Sprinting` 选移动状态机）；`URPG_GA_Death`（`CancelAllAbilities` 会停掉它） |
| **参与的功能** | 奔跑 / 持续耐力消耗 / 耐力耗尽自动中断 / 移动速度切换 |
| **关键配置** | `SetAssetTags(Ability.Sprint)`；`ActivationOwnedTags = { State.Sprinting }`；`InstancingPolicy` / `NetExecutionPolicy` 继承基类的 `InstancedPerActor` / `LocalPredicted`；**无 `AbilityTriggers`**；`UPROPERTY`：`StaminaDrainPerSecond = 5`、`DrainTickInterval = 0.1` |

**协作要点**：持续消耗用**定时器 + 复用 `ConsumeStamina`** 而不是挂周期 GE，理由是 `ConsumeStamina` 已经处理了"刷新恢复阻断"—— 走它才能保证奔跑期间耐力不会偷偷恢复；换成周期 GE 就得多维护一个容易配错的资产。速度恢复放在 `EndAbility` 而不是 `FinishSprint`：GA 被外部取消（死亡、受击）时不走 `FinishSprint`，只在那里恢复会出现"被打断后永久加速"。耐力耗尽的分支放在 `TickStaminaDrain` 里，是本项目少数几个"能力主动读属性变化并据此决策"的地方。

---

### `URPG_GA_Heal` —— 治疗：一次实现，服务所有强度的治疗

**文件**：`Source/RPG/AbilitySystem/Abilities/RPG_GA_Heal.h/.cpp`　**继承**：`URPG_GameplayAbilityBase`

| | |
|---|---|
| **它依赖谁** | `URPG_GameplayAbilityBase::ResolveEffectClassFromEvent` / `ResolveMagnitudeFromEvent`（从事件载荷取 GE 与治疗量）；`UAbilitySystemComponent`（`MakeOutgoingSpec` / `ApplyGameplayEffectSpecToSelf`）；`RPGTags`：`Event_Item_Heal` / `Ability_Heal` / `Data_Heal_Amount`；**间接**依赖 `URPG_AttributeSet` 的 `PreAttributeChange`（Health 被夹在 `[0, MaxHealth]`，所以治疗不会溢出成超血） |
| **谁依赖它** | `ARPG_EffectVolume`（药水 / 治疗泉 / 拾取物：先调 `RespondsToGameplayEvent(Event.Item.Heal)` 确认有人接，再 `SendGameplayEventToActor`）；`UAbilitySystemComponent`（事件触发登记）；角色的 `StartupPassiveAbilities`（**它没有输入标签，只能走被动能力入口授予**） |
| **参与的功能** | 治疗拾取 / 治疗泉 / 任意强度的一次性回血 |
| **关键配置** | `SetAssetTags(Ability.Heal)`；`NetExecutionPolicy = ServerOnly`（用 `LocalPredicted` 会两端各算一次、治疗量翻倍）；`InstancingPolicy` 继承基类的 `InstancedPerActor`；**`AbilityTriggers = { Event.Item.Heal @ GameplayEvent }`**；`UPROPERTY`：`HealAmount = 30`、`HealEffectClass`；`UFUNCTION(BlueprintCallable) SetHealAmount(float)`（运行时覆盖，供法术系统激活前调用） |

**协作要点**：参数解析是"**事件载荷优先、能力自身配置兜底**"：载荷里带了合法 GE 与 `EventMagnitude > 0` 就用它，否则回落到 `HealEffectClass` / `HealAmount` —— 于是同一个 GA 能服务小药、大药、治疗泉，不需要为每个强度做一个 GA 蓝图，而手动激活的老用法完全不受影响。治疗量走 `SetByCaller(Data.Heal.Amount)`，与伤害倍率是同一套"一个 GE 服务所有强度"的思路。⚠️ 触发源必须是 `GameplayEvent`，发送方必须走 `SendGameplayEventToActor` —— 直接 `ApplyGameplayEffectToTarget` 走不到这里（那也绕开了 GA 层，消耗 / 动画 / 打断 / 标签就全没地方放了）。选 `ServerOnly` 而不是 `LocalPredicted` 的理由与 `GA_ApplyBuff` 相同：数值逻辑两端各算一次就会翻倍。

---

### `URPG_GA_ApplyBuff` —— 通用施加者：一个类服务所有 Buff / Debuff

**文件**：`Source/RPG/AbilitySystem/Abilities/RPG_GA_ApplyBuff.h/.cpp`　**继承**：`URPG_GameplayAbilityBase`

| | |
|---|---|
| **它依赖谁** | `URPG_GameplayAbilityBase::ResolveEffectClassFromEvent`（从载荷取 GE）；`UAbilitySystemComponent`（`MakeOutgoingSpec` / `ApplyGameplayEffectSpecToSelf`）；`RPGTags`：`Event_Item_Buff` / `Event_Item_Debuff` / `Ability_ApplyEffect`；具体效果本身完全由 **GE 资产**决定（`GE_Buff_AttackUp` / `GE_Buff_DefenseUp` / `GE_Debuff_DefenseDown` 等，其 `GrantedTags` 形如 `State.Buff.AttackUp` / `State.Debuff.DefenseDown`） |
| **谁依赖它** | `ARPG_EffectVolume`（卷轴 / 毒瓶 / 陷阱：`RespondsToGameplayEvent` 后 `SendGameplayEventToActor`）；`UAbilitySystemComponent`（事件触发登记）；角色的 `StartupPassiveAbilities`（无输入标签，走被动入口授予） |
| **参与的功能** | 增益拾取 / 减益陷阱 / 加攻、加防、减防等一切由 GE 描述的效果 |
| **关键配置** | `SetAssetTags(Ability.ApplyEffect)`（**通用标签**，不按效果种类各建一个）；`NetExecutionPolicy = ServerOnly`；`InstancingPolicy` 继承基类的 `InstancedPerActor`；**`AbilityTriggers = { Event.Item.Buff @ GameplayEvent, Event.Item.Debuff @ GameplayEvent }`**；`UPROPERTY`：`BuffEffectClass`；`UFUNCTION(BlueprintCallable) SetBuffEffect(TSubclassOf<UGameplayEffect>)` |

**协作要点**：新增一种 Buff **不需要改 C++、也不需要新建 GA 蓝图** —— 做一个新 GE 资产、在拾取物上指过去就完事，因为要施加的 GE 是从事件载荷里取的。代价写在头文件里：它**在角色身上只能授予一次**（同时响应增益和减益两个事件），授予两份会让一次事件激活两次、效果翻倍。不像 `GA_Heal` 那样用 `SetByCaller` 传数值，是因为"加攻 20% 持续 15 秒"和"加防 30% 持续 10 秒"之间没有可参数化的共性 —— 硬抽象只会做出一堆含义不明的参数，所以采用"一个效果一个 GE"。减益不单独建 GA 类，因为机制完全一样（负修饰符 + `State.Debuff.*` 的 `GrantedTags`），真正需要区分"增益 / 减益"的地方（UI 图标、驱散只清减益）查的是 **GE 的标签**而不是 GA 的类。

---

### `URPG_GA_StaminaRegen` —— 耐力恢复：这个类里没有一行恢复逻辑

**文件**：`Source/RPG/AbilitySystem/Abilities/RPG_GA_StaminaRegen.h/.cpp`　**继承**：`URPG_GameplayAbilityBase`

| | |
|---|---|
| **它依赖谁** | `UAbilitySystemComponent`（把 `RegenEffectClass` 挂成常驻 GE）；`URPG_GameplayAbilityBase::bActivateOnGranted`（被 ASC 识别为被动能力的**唯一**依据）；`RPGTags`：`Ability_StaminaRegen`；**真正决定行为的是 GE 资产**：`GE_StaminaRegen`（Infinite + Period 0.25 + `OngoingTagRequirements` 的 Ignore Tags = `State.Stamina.Blocked`）与 `GE_StaminaRegenDelay`（Duration 3s，授予那个阻断标签，由 `ConsumeStamina` 每次刷新） |
| **谁依赖它** | `URPG_AbilitySystemComponent`（`GivePassiveAbility` / `RegisterInputAbility` 在授予时读 CDO 的 `ShouldActivateOnGranted()` 并立即 `TryActivateAbility`；重生时 `ReactivatePassiveAbilities` 按同一个标记重新拉起）；`URPG_GA_Death`（`CancelAllAbilities` 会停掉它 —— 这就是重生必须重启被动能力的原因）；角色的 `StartupPassiveAbilities` 配置；`GA_LightAttack` / `GA_HeavyAttack` / `GA_Dodge` / `GA_Jump` / `GA_Sprint`（它们的 `ConsumeStamina` 会刷新阻断标签，**间接**决定恢复何时生效） |
| **参与的功能** | 耐力自动恢复 / "停手 3 秒后缓慢恢复" / 死亡后重生时的被动能力重启 |
| **关键配置** | `bActivateOnGranted = true`（本类唯一显式配置的开关）；`NetExecutionPolicy = ServerOnly`；`NetSecurityPolicy = ServerOnly`；`SetAssetTags(Ability.StaminaRegen)`；`ActivationOwnedTags += Ability.StaminaRegen`；`InstancingPolicy` 继承基类的 `InstancedPerActor`；**无 `AbilityTriggers`**；`UPROPERTY`：`RegenEffectClass` |

**协作要点**：激活时只做一件事 —— 把 Infinite 的 `GE_StaminaRegen` 挂上去，然后**刻意不调 `EndAbility`**（能力保持激活，GE 才会一直挂着）。"什么时候恢复"完全由 GE 的 `OngoingTagRequirements` 决定：`State.Stamina.Blocked` 存在时整个 GE 被抑制，而这个标签由 `GE_StaminaRegenDelay` 授予、由每次 `ConsumeStamina` 重新挂一次来刷新时长。于是"停手 3 秒后恢复"变成了纯标签 + Duration 的推导：**没有 Tick、没有计时器、没有一行判断代码**，改恢复延迟就是改 GE 的 Duration，标签会复制所以两端表现天然一致，调试时用 GameplayDebugger 一眼能看到"被 `State.Stamina.Blocked` 挡住了"。它与 `URPG_AbilitySystemComponent::ReactivatePassiveAbilities` 是一对：`GA_Death` 的 `CancelAllAbilities` 会把它停掉，复活时不重启就会出现"复活后耐力永远不再恢复"且不报任何错。

---

### `URPG_DamageExecution` —— 伤害公式：只负责算，不负责扣血

**文件**：`Source/RPG/AbilitySystem/Effects/RPG_DamageExecution.h/.cpp`　**继承**：`UGameplayEffectExecutionCalculation`

| | |
|---|---|
| **它依赖谁** | `URPG_AttributeSet`（三处 `DECLARE_ATTRIBUTE_CAPTUREDEF`：`Attack` 从 Source、`Defense` 从 Target、`IncomingDamage` 从 Target；并直接读静态常量 `URPG_AttributeSet::DefenseConstant` 与 `MinDamage`）；`RPGTags::Data_Damage_Multiplier`（`GetSetByCallerMagnitude`，找不到时默认 1.0 而不是 0）；`UGameplayEffectCustomExecutionParameters` / `FGameplayEffectCustomExecutionOutput`（引擎） |
| **谁依赖它** | `GE_Damage` 资产（Execution 是**挂在 GE 上**配置的，不是被代码调用的）；`URPG_GameplayAbilityBase::ApplyDamageToTarget`（建 `GE_Damage` 的 Spec 并用 `SetByCaller(Data.Damage.Multiplier)` 填倍率 —— 它就是"一个 GE 服务所有攻击段"的另一半）；下游是 `URPG_AttributeSet::PostGameplayEffectExecute` |
| **参与的功能** | 伤害计算 / 攻防与 Buff/Debuff 的综合 / 10 个能力的全部伤害来源 |
| **关键配置** | `RelevantAttributesToCapture`：`Attack`（Source，**Snapshot = true**）、`Defense`（Target，**Snapshot = true**）、`IncomingDamage`（Target，Snapshot 无意义，只写不读）；公式：`BaseDamage = Attack × Multiplier`，`Mitigation = Defense / (Defense + 100)`，`FinalDamage = max(BaseDamage × (1 - Mitigation), 1.0)`；输出：`FGameplayModifierEvaluatedData(IncomingDamage, Additive, FinalDamage)` |

**协作要点**：之所以必须用 Execution 而不是普通 Modifier，是因为伤害要**同时读双方的属性**做非线性运算（跨 Actor 的 `Defense/(Defense+K)` 曲线），而普通 Modifier 只能做单属性的线性加减乘除。最容易被误解的一点是：**捕获到的 `Attack` / `Defense` 已经是被所有 GE 修改过的当前值** —— 加攻 Buff、减防 Debuff 在属性求值阶段就处理完了，"综合 buff/debuff 得出最终伤害"在代码上只体现为两行读取。两个属性都用 `Snapshot = true`（出手瞬间锁定），否则投射物飞行途中目标吃了减防 Debuff、伤害会莫名变高，玩家无法理解。它**只写元属性 `IncomingDamage`**，绝不直接改 `Health` —— 扣血、无敌判定、伤害飘字、死亡广播全部集中在 `URPG_AttributeSet::PostGameplayEffectExecute`，加新机制（格挡、吸血、顿帧）不用回头改这个文件。

---

### `URPG_AbilityTask_WeaponTrace` —— 武器轨迹检测：用 Sweep 防隧穿，用 TSet 防重复命中

**文件**：`Source/RPG/AbilitySystem/Tasks/RPG_AbilityTask_WeaponTrace.h/.cpp`　**继承**：`UAbilityTask`

| | |
|---|---|
| **它依赖谁** | `ERPG_TraceSource`（`Combat/RPG_CombatTypes.h` 的枚举，决定用哪套 Socket）；`ACharacter` + `USkeletalMeshComponent`（`GetSocketLocation` / `DoesSocketExist` 取采样线段）；`UWorld::SweepMultiByChannel`（`ECC_Pawn` 通道）；`LogRPG_Combat`；`FHitResult` 数组（广播内容） |
| **谁依赖它** | `URPG_GA_LightAttack`（`OnAttackWindowOpen` 里 `CreateWeaponTraceTask` + `OnHit` 绑到 `OnWeaponTraceHit` → `ApplyDamageToTarget`）；`URPG_GA_HeavyAttack`（同一套用法，蓄力释放与切手技共用）；两者的 `OnAttackWindowClose` / `EndAbility` / `FinishCombo` / `FinishHeavyAttack` 都会 `EndTask()` 它；**间接下游**：`URPG_GameplayAbilityBase::ApplyDamageToTarget` 把 `HitResult` 塞进 `EffectContext`，于是 `URPG_AttributeSet` 能取到 `ImpactPoint` 做飘字位置、GameplayCue 能取到命中点播特效 |
| **参与的功能** | 武器轨迹伤害判定 / 命中去重 / 命中点传递 |
| **关键配置** | `bTickingTask = true`（构造里设；轨迹检测的本质就是"每帧采样连成线"）；`UPROPERTY(BlueprintAssignable) FRPGWeaponTraceHitDelegate OnHit`（`TArray<FHitResult>` 参数，一刀扫到多人时一次广播带多个）；`CreateWeaponTraceTask(OwningAbility, TraceSource, TraceRadius, SocketStart, SocketEnd)`（`TraceRadius` 下限夹到 1）；成员：`PreviousStart` / `PreviousEnd` / `bHasPreviousSample` / `HitActorsThisSwing` |

**协作要点**：必须用 Sweep 而不是 Overlap —— 30fps 下一帧内刀尖能移动 40cm 以上，而敌人胶囊半径可能不到 40cm，两个离散位置都检测不到就是"隧穿"，连续扫掠才能抓到。检测体是把线段包成胶囊、**高度方向对齐线段方向**（否则横扫时判定严重失真）；手没动的那一帧给一个 0.01cm 的极小位移让 Sweep 退化成重叠检测，这样代码只有一条路径。去重用 `TSet<TWeakObjectPtr<AActor>> HitActorsThisSwing`（检测窗口通常持续 3~5 帧，不去重一刀会打出 3~5 倍伤害），Task 销毁时清空、下次挥砍重新开始。它在**客户端和服务器各跑一份**（因为 GA 是 `LocalPredicted`），客户端的检测只用于即时反馈、服务器的才产生真实伤害 —— 所以这里不需要"只在服务器跑"的限制。采样的 Socket 名来自 `URPG_AttackModuleData`（可由 Notify 上的 `URPG_AttackWindowPayload` 覆盖），拿不到采样点时只在第一次打一条 Warning 而不是每帧刷屏。

---

### 附：本层的三条隐含约定

1. **`CanActivateAbility` 里只能读参数，不能读实例状态。** 该函数可能在 CDO 上执行（`CurrentActorInfo` 为空），所以基类的所有便捷查询在这里全部失效 —— `URPG_GA_Dodge` / `URPG_GA_Jump` 都直接读 `ActorInfo->AbilitySystemComponent`。
2. **`EndTask()` 拦不住 AbilityTask 的回调，必须 `RemoveDynamic` 先。** `ShouldBroadcastAbilityTaskDelegates()` 判的是**能力**是否激活（`AbilityTask.cpp:199`），能力还在激活则结束掉的任务照样广播 —— `GA_LightAttack` / `GA_HeavyAttack` / `GA_HitReact` / `GA_Death` 四个类都在处理这件事。
3. **能力之间通过标签协作，不通过互相调用。** `Ability.Attack.Light`（切手技判据 + 取消目标）、`Ability.Attack` 父标签（受击打断）、`State.Attack.Charging.*`（动画姿势）、`State.Attack.Transition`（HUD 招式名）、`State.Invulnerable`（伤害拦截）—— 没有任何一处 `Cast` 到别的 GA。

---

## 四、战斗规则与敌人 AI：Combat / AI

本章覆盖 `Source/RPG/Combat/`（6 个文件）与 `Source/RPG/AI/`（10 个文件）的全部类型。

- **`Combat/`** 是战斗规则的**状态与数据层**：输入意图的容器、连段进度、招式表资产、动画事件载荷。它**刻意不依赖任何 GAS 类型**（只 include `GameplayTagContainer` / `Engine/DataAsset`），所以这一层的类型可以脱离 ASC 单独存在。
- **`AI/`** 是**决策层**：感知 → 写黑板 → 行为树节点决策。它不实现任何战斗逻辑，而是**复用玩家的那一条输入链路**。

两层的咬合点只有一个：`Input.*` 标签。玩家按左键、AI 执行 `RPG 攻击` 节点，最终都落到 `URPG_CombatComponent::PushInputTag()` + `ASC->TryActivateAbilityByInputTag()` 这两步。**没有第二条路径** —— 这是本章所有协作关系的总纲。

---

### Combat

### `URPG_CombatComponent` —— 敌我共用的战斗状态容器

**文件**：`Source/RPG/Combat/RPG_CombatComponent.h` / `.cpp`　**继承**：`UActorComponent`

| | |
|---|---|
| **它依赖谁** | `URPG_InputBuffer`（`BeginPlay` 里 `NewObject` 创建并持有，把所有 Push/Consume/Num/Clear/ToDebugString 转发给它）；`URPG_AttackModuleData`（`DefaultModule` 是蓝图配置，`CurrentModule` 是运行时生效的那份）；`ERPG_InputBufferMode`、`FRPG_BufferedInput`（来自 `RPG_CombatTypes.h`）；`LogRPG_Combat`。 |
| **谁依赖它** | `ARPG_BaseCharacter`（`CreateDefaultSubobject` 建档、`GetCombatComponent()` 暴露；`ResetForRespawn()` 里调 `ClearInputBuffer()` + `ResetCombo()`）；`ARPG_PlayerController`（按键时 `PushInputTag`，`Server_PushInputTag_Implementation` 也走同一个入口）；`URPG_BTTask_Attack`（AI 唯一的推输入点）；`URPG_GameplayAbilityBase::GetCombatComponent()/GetAttackModule()`（给所有 GA 的统一取用口）；`URPG_GA_LightAttack`（连段读改写 + 消耗/回退缓存）；`URPG_GA_HeavyAttack`（`ConsumeInputTag` 消耗触发本次激活的输入）；`URPG_AnimInstanceBase`（读 `ComboIndex` 与 `ModuleType` 喂动画蓝图）；`URPG_HUDWidget`（读 `ComboIndex` 显示"第 N 段"、读模组算蓄力条满格时间）。 |
| **参与的功能** | 输入缓存 / 轻击连段 / 蓄力（提供招式表查询）/ AI 攻击 / AI 巡逻追击（间接，见 `URPG_BTTask_Attack`）/ 死亡重生复位 |
| **关键成员** | `DefaultModule`（`EditDefaultsOnly`，缺了会在 `BeginPlay` 打 Warning）、`BufferMode`（默认 `Stack`）、`InputLifeTime`（默认 1.0，钳制 0.05~2.0）、`InputBuffer`（运行时 `NewObject`，**不复制**）、`CurrentModule`、`ComboIndex`（0 = 不在连段，1-based = 第 N 段） |

**协作要点**：组件里的 `InputBuffer` 是在 `BeginPlay` 而不是构造函数里 `NewObject` 出来的 —— 构造函数会对 CDO 执行多次，运行时对象不该挂在 CDO 上；同时组件从没调过 `SetIsReplicated`，所以**这份缓存只存在于"按键的那台机器"上**，这正是 `ARPG_PlayerController::Server_PushInputTag` 存在的唯一理由（服务器那份 GA 的衔接窗口打开时缓存是空的，会导致服务器提前 `EndAbility` 并把客户端动画硬切）。

`SetAttackModule(nullptr)` 的语义是"回落到 `DefaultModule` 而不是变成没有招式表"，并顺手 `ResetCombo()` —— 否则从徒手第 3 段换武器会直接从武器第 4 段开始，取到越界配置。

组件刻意**不保存"是否正在攻击"**这类状态：那是 `State.Attacking` 标签的事。原因是标签会被 GAS 在能力结束时自动清理、还会复制，而组件成员不会 —— 两份真相必然在某个时序上打架。组件只保存"打到第几段"这种标签表达不了的**进度**。

`GetCombatDebugString()` 在 C++ 侧没有调用方（仅 `BlueprintPure` 暴露，蓝图是否调用未核实）。

---

### `URPG_InputBuffer` —— 输入意图的容器（栈/队列 + 过期作废）

**文件**：`Source/RPG/Combat/RPG_InputBuffer.h` / `.cpp`　**继承**：`UObject`（非 Actor、非组件）

| | |
|---|---|
| **它依赖谁** | `FRPG_BufferedInput`、`ERPG_InputBufferMode`（`RPG_CombatTypes.h`）；`FGameplayTag`；`LogRPG_Combat`。**不依赖 World、不依赖 Actor** —— 时间由调用方以 `Now` 参数传入，所以容器本身可以在没有关卡的情况下跑单测。 |
| **谁依赖它** | 只有 `URPG_CombatComponent` 一个持有者与唯一调用方（`NewObject` + `SetMode`/`Push`/`Consume`/`Num`/`Clear`/`ToDebugString`）。 |
| **参与的功能** | 轻击连段（衔接窗口取下一段输入）/ 蓄力（重击释放前消耗触发输入）/ AI 攻击（AI 推输入的唯一落点）/ 联机连段同步 |
| **关键成员** | `MaxEntries = 4`（`static constexpr` 容量上限）、`Entries`（`TArray<FRPG_BufferedInput>`，末尾是最新）、`Mode`（`Stack` 默认 / `Queue`） |

**协作要点**：容器只在 **Push 和 Consume 两个时刻**被访问，所以过期清理是**惰性**的（在这两处顺带 `PruneExpired`），没有 Timer 也没有 Tick —— 清理时机与使用时机天然重合，零额外开销。

容量满时**丢最旧的**而不是拒绝新的：玩家最新按下的才代表当前意图，拒绝新输入会让"连按三次轻击"的第三次被吞掉。取出位置由 `Mode` 决定：`Stack` 取末尾（尊重最新意图）、`Queue` 取开头（按输入顺序执行）；单键连打时两者等价，差异只在混合输入时体现。

它是**本机运行时对象、从不复制**，所以服务器上没有"玩家按了什么键"这件事 —— 这是 `Server_PushInputTag` 这条 RPC 的根因，也是整套联机连段方案的地基。

---

### `URPG_AttackModuleData` —— 一套武器的完整招式表（数据资产）

**文件**：`Source/RPG/Combat/RPG_AttackModuleData.h` / `.cpp`　**继承**：`UPrimaryDataAsset`

| | |
|---|---|
| **它依赖谁** | `FRPG_AttackSegment` / `FRPG_HeavyAttackLevel` / `FRPG_HeavyAttackSet`（`RPG_AttackTypes.h`）；`ERPG_AttackModuleType`、`ERPG_TraceSource`（`RPG_CombatTypes.h`）；`UAnimMontage`；**仅 `WITH_EDITOR` 下**额外 include 四个 Notify 类（`RPG_AnimNotify_AttackEnd`、`RPG_AnimNotifyState_AttackWindow`、`RPG_AnimNotifyState_ComboWindow`，以及 `RPG_AnimNotifyState_Invulnerability` —— 最后一个在 `ScanMontageNotifies()` 里并未被使用，属未使用的包含）用于 `IsDataValid`。 |
| **谁依赖它** | `URPG_CombatComponent`（`DefaultModule` 持有它）；`URPG_GameplayAbilityBase::GetAttackModule()`（GA 侧的取用口）；`URPG_GA_LightAttack`（`GetLightSegment`/`GetLightSegmentCount` + 段的倍率、耐力、蒙太奇）；`URPG_GA_HeavyAttack`（`GetChargeLevelForTime` 判段位、`GetHeavyLevel` 取释放配置、`GetHeavyLevelCount`、`ChargeLevelTag`）；`URPG_HUDWidget`（用最后一级的 `RequiredChargeTime` 当蓄力条分母）；`URPG_AnimInstanceBase`（读 `ModuleType` 切动画姿态）。 |
| **参与的功能** | 三套模组（徒手/近战/远程）共用同一批 GA 的数据驱动 / 蓄力段位判定 / 切手技 / 编辑器资产体检 |
| **关键成员** | `ModuleType`、`ModuleTag`、`TraceSource` + `TraceRadius` + 三组 Socket（`LeftHandSocket`/`RightHandSocket`、`BladeStartSocket`/`BladeEndSocket`、`MuzzleSocket`/`ProjectileClass`）、`LightAttacks`（5 段）、`HeavyAttack`（`FRPG_HeavyAttackSet`）、`ComboTransition*`（切手技三件套）、`IsDataValid()` |

**协作要点**：这个资产的职责边界由两条规则划清 ——**伤害倍率、耐力消耗这类"数值设计"必须集中在这里**（改倍率不该需要打开动画资产）；而**检测半径、Socket 名这类"由动画决定"的参数允许被 AnimNotify 覆盖**（`URPG_AttackWindowPayload::bOverrideTrace`）。所以 GA 取检测参数的顺序是"先读模组默认值 → 若 Notify 声明覆盖则换成 Payload 的值 → 若检测源是刀锋则强制换成武器 Socket"。

每个蒙太奇字段都允许留空，GA 侧会走"模拟时序"分支：**没有动画时连段推进、伤害结算、耐力消耗全部照常执行**，动画是表现层而不是数值逻辑的硬依赖。

`IsDataValid()` 是这个类最有特色的部分：它按**两套相反的规则**体检蒙太奇。攻击类蒙太奇（轻击各段、重击释放、切手技）要求"有判定窗口、非终结段要有衔接窗口、`RPG 攻击结束` 必须在 70% 之后"；而蓄力起手/循环这类**保持姿势**蒙太奇则要求**一个通知都不能有** —— 起手动画上误加 `RPG 攻击结束` 会让蓄力机制直接失效，误加判定窗口会每次摆姿势白送一次伤害。写错规则比不检查更糟，所以这两条路径是分开的两个函数。

---

### `RPG_AttackTypes.h`（`FRPG_AttackSegment` / `FRPG_HeavyAttackLevel` / `FRPG_HeavyAttackSet`）—— 招式表的三个纯数据结构

**文件**：`Source/RPG/Combat/RPG_AttackTypes.h`　**继承**：无（均为 `USTRUCT(BlueprintType)`，只含数据与注释）

| | |
|---|---|
| **它依赖谁** | 只有 `GameplayTagContainer.h` 和 `UAnimMontage` 前置声明。不依赖任何运行时类。 |
| **谁依赖它** | `URPG_AttackModuleData` 是唯一的使用者（`TArray<FRPG_AttackSegment>`、`FRPG_HeavyAttackSet`、`TArray<FRPG_HeavyAttackLevel>`），并通过它的 `GetLightSegment()` / `GetHeavyLevel()` 把裸指针借给 `URPG_GA_LightAttack`、`URPG_GA_HeavyAttack`、`URPG_HUDWidget` 读取。 |
| **参与的功能** | 轻击连段（每段的蒙太奇/倍率/耐力/标签）/ 蓄力（每级门槛时间/释放蒙太奇/倍率/耐力/标签）/ 切手技（经由 `FRPG_HeavyAttackSet` 之外的字段，不含在本文件） |
| **关键成员** | `FRPG_AttackSegment`：`Montage`（可空）、`DamageMultiplier`、`StaminaCost`、`AttackTag`；`FRPG_HeavyAttackLevel`：`RequiredChargeTime`、`ReleaseMontage`、`DamageMultiplier`、`StaminaCost`、`ChargeLevelTag`；`FRPG_HeavyAttackSet`：`ChargeStartMontage`、`ChargeLoopMontage`、`Levels`、`ChargeStaminaDrainPerSecond`、`ChargeMoveSpeedScale` |

**协作要点**：这三个结构是"数据"与"逻辑"之间的契约面。约定俗成的默认曲线写在了注释里：轻击 5 段倍率 1.0/1.15/1.4/1.6/2.0（越往后越强，鼓励打完整套），蓄力 3 级 3.0/4.5/6.5。

注意 `ChargeLevelTag` 只是**扩展位**：`URPG_GA_HeavyAttack::UpdateChargeTags()` 同时挂固定标签（`State.Attack.Charging.Lv1/2/3`，动画蓝图只认这三个）和这个自定义标签，两者缺一不可 —— 只挂自定义标签则 DA 配错动画就瞎，只挂固定标签则该字段成摆设。

---

### `RPG_CombatTypes.h`（`ERPG_AttackModuleType` / `ERPG_TraceSource` / `ERPG_InputBufferMode` / `FRPG_BufferedInput`）—— 战斗层公共类型

**文件**：`Source/RPG/Combat/RPG_CombatTypes.h`　**继承**：无（三个 `UENUM` + 一个 `USTRUCT`）

| | |
|---|---|
| **它依赖谁** | 只有 `GameplayTagContainer.h`。文件头注释明确写着"这个文件不依赖任何 GAS 类"，目的是让 `Combat/` 能独立编译、独立测试。 |
| **谁依赖它** | `URPG_CombatComponent`（`BufferMode` + `FRPG_BufferedInput`）、`URPG_InputBuffer`（`Mode` + `Entries`）、`URPG_AttackWindowPayload`（`ERPG_TraceSource`）、`URPG_AttackModuleData`（`ModuleType` + `TraceSource`）、`URPG_AbilityTask_WeaponTrace`（`ERPG_TraceSource`）、`URPG_AnimInstanceBase`（`ERPG_AttackModuleType`）、`URPG_AnimNotifyState_AttackWindow`（`ERPG_TraceSource`，写在 Notify 上覆盖模组）。 |
| **参与的功能** | 三套攻击模组的类型区分 / 伤害检测源选择 / 输入缓存策略 / 缓存条目结构 |
| **关键成员** | `ERPG_AttackModuleType`：`Unarmed`/`Melee`/`Ranged`（三套模组共用同一批 GA，靠它分流）；`ERPG_TraceSource`：`Hands`/`WeaponBlade`/`Projectile`（决定 WeaponTrace 采样什么）；`ERPG_InputBufferMode`：`Stack`/`Queue`；`FRPG_BufferedInput`：`InputTag` + `Timestamp` + `LifeTime` + `IsExpired(Now)` |

**协作要点**：这个头文件是整个 `Combat/` 目录"零 GAS 依赖"这条架构约束的落点 —— 有了它，`Combat/` 里的类型（包括输入缓存）不会被 `AbilitySystemComponent` 之类的重型头文件污染。

`FRPG_BufferedInput::IsExpired()` 是唯一带行为的成员：`(Now - Timestamp) > LifeTime`。把"过期"定义在数据结构上、把"什么时候清理"留给容器，是这套设计里职责切割得最干净的一处。

---

### `URPG_AttackWindowPayload` —— 动画事件载荷（AnimNotify → GameplayEvent）

**文件**：`Source/RPG/Combat/RPG_AttackWindowPayload.h`　**继承**：`UObject`

| | |
|---|---|
| **它依赖谁** | `ERPG_TraceSource`（`RPG_CombatTypes.h`）、`FGameplayTag`。没有别的依赖。 |
| **谁依赖它** | 生产方：`URPG_AnimNotifyState_AttackWindow::NotifyBegin()`（`NewObject<URPG_AttackWindowPayload>(MeshComp)` 后填字段，塞进 `FGameplayEventData::OptionalObject`，再 `SendGameplayEventToActor`）；消费方：`URPG_GA_LightAttack::OnAttackWindowOpen()` 与 `URPG_GA_HeavyAttack::OnAttackWindowOpen()`（`Cast` 出来后按 `bOverrideTrace` 决定是否覆盖模组的检测参数）。`URPG_GameplayAbilityBase.h` 的注释里把它当作"自定义载荷该怎么传"的范本引用。 |
| **参与的功能** | 动画事件桥接（动画驱动的伤害判定窗口）/ 轨迹检测参数覆盖 / 轻重击两条 GA 的判定窗口共用 |
| **关键成员** | `AttackTag`（日志与事件过滤）、`bOverrideTrace`、`TraceSource`、`TraceRadius`、`SocketStart`、`SocketEnd` |

**协作要点**：用 `UObject` 包装是因为 `FGameplayEventData` 是引擎定义的固定结构，只提供 `OptionalObject` / `OptionalObject2` 两个通用槽位，想传结构化数据只能包一层。

它**只带检测参数、不带伤害倍率**，这条边界是有意的：半径和 Socket 本质上由动画决定（这一刀挥出去手在哪、扫过多大范围）；而倍率属于数值设计，必须留在 `URPG_AttackModuleData` 里管理。

`NewObject` 的 Outer 传的是 `MeshComp` 而不是 `GetTransientPackage()`，让载荷的生命周期跟着角色走，角色销毁时一起回收。

---

### AI

### `ARPG_AIController` —— 敌人的大脑（感知 → 黑板 → 行为树）

**文件**：`Source/RPG/AI/RPG_AIController.h` / `.cpp`　**继承**：`AAIController`

| | |
|---|---|
| **它依赖谁** | `UAIPerceptionComponent` + `UAISenseConfig_Sight` / `UAISenseConfig_Hearing`（构造函数里自建，因为 `AAIController` 默认**只建 PathFollowingComponent**）；`UBehaviorTree`；`UBlackboardComponent`；`RPGBlackboardKeys`（所有键名）；`ARPG_BaseCharacter`（`SetCombatMovement()` 切战斗移速、`IsAlive()` 过滤死尸回调）；`ARPG_Enemy`（读 `AttackRange` 写进黑板）；`UBrainComponent`（`StopLogic` / `RestartLogic`）；`LogRPG_AI`。 |
| **谁依赖它** | `ARPG_Enemy`（构造函数里 `AIControllerClass = ARPG_AIController::StaticClass()` + `AutoPossessAI`；`OnDeathStarted()` 调 `StopAI()`、`OnRespawned()` 调 `RestartAI()`）；`URPG_BTService_CombatUpdate`（脱战时 `Cast` 成它并调 `SetCombatState(false)`，找不到才退回直接写黑板）。 |
| **参与的功能** | AI 感知与索敌 / 战斗状态切换（黑板 + 移速同步）/ 行为树启停 / 死亡与重生时的 AI 生命周期 |
| **关键成员** | `SightConfig`/`HearingConfig`、`BehaviorTreeAsset`（留空则打 Error 并站住不动）、`SightRadius`(1500)/`LoseSightRadius`(2000)/`PeripheralVisionAngleDegrees`(45)/`SightMaxAge`(5)/`AutoSuccessRangeFromLastSeenLocation`(900)、`HearingRange`(1200)、`LoseTargetAfterSeconds`(6) |

**协作要点**：构造函数里把 `DetectionByAffiliation` 的三个开关（`bDetectEnemies` / `bDetectNeutrals` / `bDetectFriendlies`）**全部显式打开** —— 引擎默认三个都是 false，压成位掩码后是 0，导致任何阵营都感知不到，症状是"敌人站着不动完全不看你"且**不报任何错**。更绕的是默认的团队态度求解器把两个 `NoTeam(255)` 判成 Friendly 而非 Neutral，所以只开 `bDetectNeutrals` 也没用。

`SetCombatState()` 是**唯一允许改战斗状态的入口**：它同时写黑板的 `bInCombat`（行为树根选择器靠它选分支）和角色的 `MaxWalkSpeed`（`ARPG_BaseCharacter::SetCombatMovement()`）。收成一个入口，两处就不可能不同步；而且它刻意**不提前 return**，每次调用都当成一次校正。

`OnTargetPerceptionUpdated` 里只对"有 GAS 的角色"起反应（石头、门不该引起战斗），并且**丢视野时只清 `bTargetVisible`、不清 `TargetActor`** —— 丢视野是一瞬间的事，脱战是"连续 N 秒看不见"的持续判断，这个计时交给服务去做。感知回调自己也**不是每帧调用**的，只在感知状态变化时触发，所以"立刻进入战斗"这一步放在这里最合适（比 BTService 的 0.2 秒周期还早）。

`StopAI()` 与 `RestartAI()` 必须成对使用：行为树没有"我死了"的概念，不停的话会一直写黑板、发起寻路（此时移动模式已被布娃娃关掉，每帧失败），并且卡在永不结束的 Latent Task 里；只停不重启则敌人复活后站在原地一动不动且不报错。调用方是 `ARPG_Enemy::OnDeathStarted/OnRespawned`，而这两者的上游是 `URPG_GA_Death`（`Character->OnDeathStarted()`）。

---

### `RPG_BlackboardKeys`（namespace）—— 黑板键名的唯一来源

**文件**：`Source/RPG/AI/RPG_BlackboardKeys.h` / `.cpp`　**继承**：无（`namespace` + 8 个 `extern const FName`）

| | |
|---|---|
| **它依赖谁** | 只有 `CoreMinimal.h`。定义放在 `.cpp` 里而不是头文件 inline，是为了配合 UE 的 FName 名字池初始化顺序（引擎自己定义 `FBlackboard::KeySelf` 也是这个写法）。 |
| **谁依赖它** | `ARPG_AIController`（写 `HomeLocation`/`AttackRange`/`bInCombat`/`bTargetVisible`/`PatrolIndex`，读写 `TargetActor`/`LastKnownLocation`）；`URPG_BTService_CombatUpdate`（`TargetActor`/`bTargetVisible`/`bInCombat`/`LastKnownLocation`）；`URPG_BTDecorator_CanAttack`（`TargetActor`/`AttackRange`）；`URPG_BTTask_MoveToTarget`（`TargetActor`/`bTargetVisible`/`LastKnownLocation`）；`URPG_BTTask_Patrol`（`HomeLocation`/`PatrolIndex`/`PatrolLocation`）。 |
| **参与的功能** | AI 巡逻 / 追击 / 攻击决策 / 脱战计时 —— 全部黑板读写的公共词汇表 |
| **关键成员** | `TargetActor`(Object)、`LastKnownLocation`(Vector)、`bTargetVisible`(Bool)、`HomeLocation`(Vector)、`PatrolLocation`(Vector)、`PatrolIndex`(Int)、`AttackRange`(Float)、`bInCombat`(Bool) |

**协作要点**：存在的理由和 GameplayTag 用 C++ 声明而不是纯 ini 一样 —— 黑板读写全靠 `FName`，而 **FName 拼错编译期不报错**，会安安静静返回 nullptr，表现像"AI 没发现玩家"，于是你会去查感知配置、碰撞通道、阵营设置，就是想不到是键名拼错。

在 `BB_RPG_Enemy` 资产里建键时名字必须与这里**逐字一致**（区分大小写），类型也必须对得上 —— 键存在但类型不对时 `SetValueAsXxx` 会静默失败。

`PatrolLocation` 在 C++ 侧是**只写不读**的（只有 `URPG_BTTask_Patrol` 写它，没有任何 C++ 读者），实际用途是运行时在编辑器里看 AI 当前要去哪个点。

---

### `URPG_BTDecorator_CanAttack` —— "够得着 + 双方活着"的攻击条件

**文件**：`Source/RPG/AI/Decorators/RPG_BTDecorator_CanAttack.h` / `.cpp`　**继承**：`UBTDecorator`

| | |
|---|---|
| **它依赖谁** | `AAIController`（`OwnerComp.GetAIOwner()` → `GetPawn()`）；`UBlackboardComponent`（读 `TargetActor`、`AttackRange`）；`ARPG_BaseCharacter::IsAlive()`（双方存活判定）；`APawn::GetSimpleCollisionRadius()`（表面距离）。 |
| **谁依赖它** | 只有行为树资产 `BT_RPG_Enemy`（挂在「攻击」Sequence 上）。没有 C++ 调用方。 |
| **参与的功能** | AI 攻击决策（决定行为树是走「攻击」分支还是落到「追击」分支） |
| **关键成员** | `RangeTolerance = 1.15`（允许在范围外 15% 就出手，留攻击前摇的提前量）；构造函数里设的 `FlowAbortMode = EBTFlowAbortMode::LowerPriority` |

**协作要点**：距离**当场算，不读黑板** —— 距离每帧都在变，如果由服务每 0.2 秒写一次黑板，装饰器读到的就是最多 0.2 秒前的旧值，玩家快速冲刺时会出现"明明跑到面前了 AI 还在往前追"。判断标准是"变化频率"：`AttackRange` 这种很少变的值才适合放黑板，每帧变的当场算更准也更便宜。

用**表面距离**（中心距减两个胶囊半径）而不是原点距离：原点在脚底，直接用原点距离会让两个贴身站立的角色"看起来相距很远"，手感上就是"明明挨着却打不到"。

构造函数里把 `FlowAbortMode` 默认设成 `LowerPriority` 是这个类存在感最强的一处设计：Selector 从左往右选，敌人正在追击时 Selector 会"卡"在追击分支上，**不会主动回头重新检查攻击分支**，表现为"追到玩家面前了还一路贴着走"。因为这是我们自己的装饰器，可以把默认值定在代码里，而引擎自带的 Blackboard 装饰器只能在 Details 面板手动设。

---

### `URPG_BTService_CombatUpdate` —— 战斗状态的定期维护与脱战计时

**文件**：`Source/RPG/AI/Services/RPG_BTService_CombatUpdate.h` / `.cpp`　**继承**：`UBTService`

| | |
|---|---|
| **它依赖谁** | `UBlackboardComponent`（`TargetActor`/`bTargetVisible`/`bInCombat`/`LastKnownLocation`）；`ARPG_AIController::SetCombatState()`（脱战必须走统一入口）；`ARPG_BaseCharacter::IsAlive()`（目标死亡判定）；`LogRPG_AI`。 |
| **谁依赖它** | 只有行为树资产 `BT_RPG_Enemy`（挂在「战斗」Sequence 上，是该分支内所有决策的"事实来源"）。 |
| **参与的功能** | AI 脱战判定 / "最后已知位置"记忆的持续刷新 / 目标死亡后的立即脱战 / `bInCombat` 与「有没有目标」的一致性兜底 |
| **关键成员** | `LoseTargetAfterSeconds = 6`（连续不可见多久算脱战）、`TimeSinceLastSeen`（逐实例状态）、`Interval = 0.2` + `RandomDeviation = 0.05`、`bCreateNodeInstance = true` |

**协作要点**：它解决三件事。① **持续刷新记忆** —— 只在"刚看到"那一刻记一次是不够的，玩家一直在动，追到最后已知位置时会停在玩家几秒前待过的地方。② **丢视野 ≠ 脱战** —— 计时到阈值才清目标；`TargetActor` 清掉但 `LastKnownLocation` **刻意保留**（下次进战斗一开始就丢视野时至少还有个搜索目标）。③ **目标死了要立刻脱战** —— 死亡不会触发感知丢失（尸体还在视野里），不判的话 AI 会对着尸体继续挥拳。

服务主循环还带一个兜底：`TargetActor` 为空但 `bInCombat` 仍为 true 时，主动调 `SetCombatState(false)` 校正 —— 少了它，一旦某条路径漏清 `bInCombat`，AI 会卡在"战斗分支里但没有目标"的状态，两个分支都不走，站着发呆。

`bCreateNodeInstance = true` 是必需的：`TimeSinceLastSeen` 是逐实例状态，共享节点对象会让多只敌人的计时互相覆盖。用 `Interval` 而不是 Tick 是刻意的 —— 脱战计时不需要每帧精度，0.2 秒足够，而十几只敌人每帧同时做感知查询会明显掉帧，`RandomDeviation` 进一步把它们的执行时机错开。

---

### `URPG_BTTask_Attack` —— 发动一次攻击并等它播完（Latent Task）

**文件**：`Source/RPG/AI/Tasks/RPG_BTTask_Attack.h` / `.cpp`　**继承**：`UBTTaskNode`

| | |
|---|---|
| **它依赖谁** | `AAIController`（取 Pawn）；`ARPG_BaseCharacter::GetAbilitySystemComponent()`（走基类的 `IAbilitySystemInterface`，而不是敌人专有的 `GetRPGAbilitySystemComponent()` —— 这样节点对玩家和敌人一视同仁）；`URPG_AbilitySystemComponent::TryActivateAbilityByInputTag()`；`URPG_CombatComponent::PushInputTag()`；`URPG_AbilitySystemComponent::HasMatchingGameplayTag(RPGTags::State_Attacking)`；`UBehaviorTreeComponent`（`FinishLatentTask`）；`LogRPG_AI`。 |
| **谁依赖它** | 只有行为树资产 `BT_RPG_Enemy`（「攻击」Sequence 里的主节点）。 |
| **参与的功能** | AI 攻击发起 / AI 连段（详情见下）/ 与玩家共用输入链路 |
| **关键成员** | `InputTag`（默认 `Input.Attack.Light`，`meta = (Categories = "Input")`）、`AttackTimeoutSeconds = 3`、`ElapsedSeconds`、`CachedOwnerComp`（`TWeakObjectPtr`）、构造函数里的 `bNotifyTick = true` + `bCreateNodeInstance = true` + `bIgnoreRestartSelf = false` |

**协作要点**：这个节点做的事情和 `ARPG_PlayerController::OnAbilityInputPressed` **逐步一致** —— ① `Combat->PushInputTag(InputTag)`；② `ASC->TryActivateAbilityByInputTag(InputTag)`。之所以要先推缓存再激活（看着多余），是因为 `GA_LightAttack` 的连段靠"衔接窗口打开时从缓存里取下一段"来推进，不推缓存 AI 永远只能打第 1 段；而之所以不直接 `TryActivateAbilityByTag(Ability.Attack.Light)`，是为了避免出现"玩家能放、AI 放不出来"这类两条路径不一致的问题。

`ExecuteTask` 返回 **`InProgress`** 而不是 `Succeeded`（Latent Task 模式）：攻击动画要播 0.5~1 秒，立刻返回成功会让行为树在挥拳的同时开始走向玩家，看起来像"滑步出拳"。

判断"攻击结束"用的是**每帧轮询 `State.Attacking` 标签消失**，而不是等 `Event.Combat.AttackEnd` 事件：事件只覆盖"蒙太奇播到 AttackEnd 通知"这一条路径，而攻击被取消、耐力不足激活失败、蒙太奇没配走模拟时序这些情况下事件不会来，AI 会一直等下去；标签则覆盖**所有**结束路径（能力一结束 GAS 自动摘标签）。这正是本项目"标签是唯一真相源"原则的直接收益。超时（默认 3 秒）是最后兜底，防止标签因 bug 没被摘掉时 AI 永远卡住。

`AbortTask` **故意不取消攻击能力**：被打断通常是"条件不再满足"，但招式已经挥出去了，中途硬停会看到动作卡在半空，而且判定窗口可能已经开过 —— 让这一招自然播完（通常不到 1 秒），行为树下一轮自然会重新决策。

`bCreateNodeInstance = true` 同样是因为有逐实例状态（`ElapsedSeconds`）。

---

### `URPG_BTTask_MoveBase` —— 巡逻与追击共用的移动基类（Abstract）

**文件**：`Source/RPG/AI/Tasks/RPG_BTTask_MoveBase.h` / `.cpp`　**继承**：`UBTTaskNode`（`UCLASS(Abstract)`）

| | |
|---|---|
| **它依赖谁** | `AAIController`（`MoveTo(FAIMoveRequest)`、`ReceiveMoveCompleted` 动态委托、`StopMovement()`）；`FAIMoveRequest` 与 `EPathFollowingResult` / `FPathFollowingRequestResult`（`Navigation/PathFollowingComponent.h`）；`UBehaviorTreeComponent`（`FinishLatentTask`）；`LogRPG_AI`。 |
| **谁依赖它** | `URPG_BTTask_MoveToTarget` 与 `URPG_BTTask_Patrol`（唯一实现 `PrepareMove()` 的两个子类）；`URPG_BTTask_MoveToTarget::TickTask()` 还会复用基类的 `StartMove()`。 |
| **参与的功能** | AI 巡逻 / AI 追击 / 移动的中断、失败、超时统一处理 |
| **关键成员** | `AcceptanceRadius = 60`、`MoveTimeoutSeconds = 10`、`CurrentRequestID`（过滤非本次请求的完成回调）、`ElapsedSeconds`、`CachedOwnerComp`（回调时机拿不到 `OwnerComp`，且节点 Outer 是行为树资产不是运行组件，所以必须自存）；纯虚 `PrepareMove(OwnerComp, OutDestination, OutGoalActor)` |

**协作要点**：抽基类的理由是"巡逻和追击的差别只有'去哪'这一件事"，其余（下请求、绑回调、处理到达/失败/超时/中断）完全一样 —— 让两者共用同一份中断处理，能保证最容易写漏的部分**绝对一致**。

移动必须用 Latent Task：`MoveToLocation` 只是下了一个异步请求，函数返回时角色才刚抬脚，`return Succeeded` 会让行为树立刻执行下一个节点（典型症状是"一边走一边挥拳"）。

`StartMove()` 里有两个关键细节：① 每次先 `RemoveDynamic` 再 `AddDynamic` —— `StartMove` 在一轮任务里可能被调用多次（`MoveToTarget` 中途变卦要换目标），动态多播重复绑定会让一次移动完成触发 N 次回调，表现是"AI 走着走着突然跳一大步"。② 构造请求时优先用 `FAIMoveRequest(GoalActor)` 而不是位置 —— 传 Actor 才会让引擎调 `SetGoalActorObservation()` **持续观察目标位置并自动重算路径**，传坐标只是"走到你刚才站的地方"（一顿一顿地追）。

`AbortTask` 里**先解绑再 `StopMovement()`** 的顺序是刻意的：`StopMovement` 可能触发一次移动完成回调（结果是 Aborted），此时任务已经被打断，再跑一遍 `OnMoveCompleted` 会去 `FinishLatentTask` 一个已经结束的任务。

`bCreateNodeInstance = true`：本类在成员里存了请求 ID 和已等待时长，共享节点对象会让两只敌人互相覆盖 —— 这个 bug 只在**同屏出现第二只敌人**时才复现。用每实例节点对象而不是引擎惯用的 `NodeMemory` 方案，代价是每实例多一个 `UObject`，换来不用手算内存大小和 Placement New。

---

### `URPG_BTTask_MoveToTarget` —— 追击：跟随目标 / 去最后已知位置搜索

**文件**：`Source/RPG/AI/Tasks/RPG_BTTask_MoveToTarget.h` / `.cpp`　**继承**：`URPG_BTTask_MoveBase`

| | |
|---|---|
| **它依赖谁** | `UBlackboardComponent`（`TargetActor`、`bTargetVisible`、`LastKnownLocation`）；基类的 `StartMove()`；`LogRPG_AI`。 |
| **谁依赖它** | 只有行为树资产 `BT_RPG_Enemy`（「追击」Sequence）。 |
| **参与的功能** | AI 追击 / 视野丢失后的"有记忆的搜索" / 追击途中的走法切换 |
| **关键成员** | `bChaseVisibleTargetOnly = false`（不勾 = 看不见时去最后已知位置找）、`bLastMoveFollowedActor`（记录上次用的是"跟随 Actor"还是"去固定点"）、`IsTargetVisible()` |

**协作要点**：`PrepareMove` 分三条路：看得见 → `OutGoalActor = Target`（跟随活体，引擎持续观察并重算路径）；看不见且 `bChaseVisibleTargetOnly` → 返回 false 交回行为树；看不见 → 去 `LastKnownLocation`，但**先判 `IsNearlyZero()`** —— 从没看到过目标时它是 `(0,0,0)`，直接走过去会让 AI 冲向世界原点。

`TickTask` 里做了一件别人不会做的事：发现"看得见/看不见"翻转时**就地重发移动请求**（`StartMove`），而不是 `return Succeeded` 让行为树绕一圈 —— 因为追击分支后面跟着 `Wait` 节点，绕一圈意味着原地站 0.2 秒再出发，那正是要消掉的"一顿"。这条也解释了 `StartMove` 为什么要能被反复调用（见 `URPG_BTTask_MoveBase` 的重复绑定处理）。

"去最后已知位置找一圈"的耐心（找多久才放弃）由 `URPG_BTService_CombatUpdate` 的脱战计时控制 —— 两个类的职责在这条线上正好接住：任务负责"去哪找"，服务负责"找多久就放弃"。

---

### `URPG_BTTask_Patrol` —— 巡逻：环形走遍敌人身上的巡逻点

**文件**：`Source/RPG/AI/Tasks/RPG_BTTask_Patrol.h` / `.cpp`　**继承**：`URPG_BTTask_MoveBase`

| | |
|---|---|
| **它依赖谁** | `ARPG_Enemy::PatrolPoints`（`EditInstanceOnly` 的 `TArray<TObjectPtr<AActor>>`，在关卡里按实例拖 TargetPoint）；`UBlackboardComponent`（读 `HomeLocation`、`PatrolIndex`，写 `PatrolIndex`、`PatrolLocation`）；`LogRPG_AI`。 |
| **谁依赖它** | 只有行为树资产 `BT_RPG_Enemy`（「巡逻」Sequence，且该分支**故意不加任何 Decorator**，作为永远可用的兜底）。 |
| **参与的功能** | AI 巡逻 / 脱战回位 / 行为树"永远有一个能执行的分支"的兜底 |
| **关键成员** | `bFallbackToHomeLocation = true`（一个巡逻点都没配时回出生点，而不是让任务失败导致 AI 发呆） |

**协作要点**：巡逻索引 `PatrolIndex` **存在黑板上而不是节点成员里**，就是为了运行时能在编辑器里实时看到 AI 走到第几个点了；环形推进用 `(CurrentIndex + 1) % Count`，走完一圈从头再来。

`PrepareMove` 里对"数组里留了个 None"这种配置错误做了防护（跳过失败并打 Warning）；`OutDestination` 用的是巡逻点 Actor 的 `GetActorLocation()`，同时顺手写进黑板的 `PatrolLocation` 供调试观察 —— 注意 `HomeLocation` 的分支**不写** `PatrolLocation`，因为那会把"巡逻点位置"这个语义误导成出生点。

`bFallbackToHomeLocation` 的默认值体现了行为树的一条实用原则：宁可"回出生点待着"，也不要让任务失败导致两个分支都不成立 —— 那种情况下 AI 会完全站着不动，而且不报错。

---

### 附：跨章节的接口速查

| 咬合点 | 上游 → 下游 | 载体 |
|---|---|---|
| 玩家输入 | `ARPG_PlayerController::OnAbilityInputPressed` → `URPG_CombatComponent::PushInputTag` → `ASC::TryActivateAbilityByInputTag` | `Input.*` 标签（`Input.Attack.Light` 等） |
| AI 输入 | `URPG_BTTask_Attack::ExecuteTask` → **同一对调用** | 同上（BT 节点上配的 `InputTag`） |
| 联机连段 | `ARPG_PlayerController::Server_PushInputTag` → 服务器那份 `URPG_CombatComponent::PushInputTag` | `Server, Reliable` RPC |
| 连段进度 | `URPG_GA_LightAttack` ↔ `URPG_CombatComponent::ComboIndex` | `GetComboIndex`/`SetComboIndex`/`ResetCombo` |
| 招式表查询 | GA / 动画蓝图 / HUD → `URPG_CombatComponent::GetAttackModule()` → `URPG_AttackModuleData` | DataAsset |
| 动画 → 逻辑 | `URPG_AnimNotifyState_AttackWindow` → `SendGameplayEventToActor` → `URPG_GA_LightAttack/HeavyAttack::OnAttackWindowOpen` | `URPG_AttackWindowPayload`（`OptionalObject`）|
| 感知 → 决策 | `ARPG_AIController::OnTargetPerceptionUpdated` → `RPGBlackboardKeys` 各键 → 行为树节点 | `BB_RPG_Enemy` |
| 脱战 | `URPG_BTService_CombatUpdate` → `ARPG_AIController::SetCombatState(false)` → 黑板 `bInCombat` + `ARPG_BaseCharacter::SetCombatMovement` | 统一入口函数 |
| AI 生命周期 | `URPG_GA_Death` → `ARPG_Enemy::OnDeathStarted` → `ARPG_AIController::StopAI`（重生侧对应 `RestartAI`） | 虚函数钩子 |

---

## 五、表现层：UI

> 本层的共同前提：**C++ 只做"把状态变成数字和可见性"，外观全部留给 WBP**。
> 六组类里没有一处硬编码颜色、字体、动画时长之外的视觉资源；所有控件引用都走
> `BindWidget` / `BindWidgetOptional`，名字写错在 WBP 编译期就会报出来。
>
> 数据来源分两条路，这条分界线贯穿本层的每一个类：
>
> | 数据类型 | 例子 | 手段 | 用在哪 |
> |---|---|---|---|
> | 连续量 | Health / Mana / Stamina / Attack / Defense | `GetGameplayAttributeValueChangeDelegate`（服务器 GE 改值时 + 客户端 `OnRep` 写入时各触发一次） | 主 HUD 三条属性条、攻防文本、头顶血条 |
> | 离散状态 | 是否在蓄力 / 招式名 / 是否闪避 / 是否死亡 | 每帧 `HasMatchingGameplayTag` | 蓄力条、招式名、闪避图标、死亡面板 |
>
> 底下这条约定和 `URPG_AnimInstanceBase::UpdateCombatState()` 是同一套 ——
> 项目里"C++ 读 GAS 状态再暴露给表现层"已经是一个惯例，不是本层自创。

---

### `ARPG_HUD` —— 每个本地玩家一份的 HUD 宿主，同时是伤害飘字的落点

**文件**：`Source/RPG/UI/RPG_HUD.h` / `Source/RPG/UI/RPG_HUD.cpp`　**继承**：`AHUD`

| | |
|---|---|
| **它依赖谁** | `URPG_HUDWidget`：`HUDWidgetClass` 是 `TSubclassOf`，`CreateWidget<URPG_HUDWidget>(GetOwningPlayerController(), ...)` 造出主 HUD 并 `AddToViewport(0)`。`URPG_DamageNumberWidget`：`DamageNumberWidgetClass` 同样是 `TSubclassOf`，每次飘字 `CreateWidget` 一个并 `AddToViewport(10)`。`APlayerController`（`PlayerOwner`）：世界坐标→屏幕坐标全靠它的 `ProjectWorldLocationToScreen`。`LogRPG_Combat`：配置缺失/创建失败的日志 |
| **谁依赖它** | `ARPG_BaseCharacter::Multicast_ShowDamageNumber_Implementation`：遍历本地 PC，`PC->GetHUD<ARPG_HUD>()` 拿到它再调 `ShowDamageNumber(Amount, Location)` —— 这是飘字进来**唯一**的入口。`ARPG_GameModeBase` 构造函数：`HUDClass = ARPG_HUD::StaticClass()`（真正生效的是蓝图子类 `BP_RPG_HUD`）。`BP_RPG_HUD` 的 Class Defaults：注入 `WBP_RPG_HUD` / `WBP_DamageNumber` 两个类 |
| **参与的功能** | 伤害飘字（宿主 + 投影 + 散开 + 上限 + 清理）、主 HUD 的创建与销毁、飘字"跟随世界坐标"的可选模式 |
| **关键成员** | `HUDWidgetClass` / `DamageNumberWidgetClass`（`EditDefaultsOnly`，蓝图注入点）；`bDamageNumbersTrackWorld`（默认 false）；`MaxDamageNumbers = 32`（超出丢最旧的）；`DamageNumberScatterRadius = 22.f`；`ActiveDamageNumbers` 与一一对应的 `ActiveDamageNumberLocations`；`PruneDamageNumbers()`；`ProjectToScreen()`；`HUDWidget` + `GetHUDWidget()`（`BlueprintPure`，**源码里没有任何 C++ 调用者**，是给蓝图用的访问器） |

**协作要点**：它为什么是 `AHUD` 而不是"在 PlayerController 里 CreateWidget"——`AHUD` 天生是"每个本地玩家一份"的语义，引擎负责创建时机；而 PlayerController 已经把"输入翻译成能力激活"当成正事，再塞 UI 生命周期进去会让那个类变成抽屉。更关键的是**飘字必须在这一层落地**：`Multicast_ShowDamageNumber` 只广播了"谁挨了多少、在世界哪个点"，世界坐标→屏幕坐标要用相机信息，而相机是每个客户端自己的 —— 所以每一端都得有"自己那个"HUD 来完成这次投影。`BeginPlay` 里有一道 `PC->IsLocalController()` 早退（注释里自认是冗余的，引擎保证非本地玩家不会生成 HUD），`EndPlay` 里显式 `RemoveFromParent` 掉 HUD 并 `Reset()` 两个数组，防关卡切换时留下悬垂。

**WBP / 蓝图侧要配合做什么**：`BP_RPG_HUD`（父类 `ARPG_HUD`，是 **Actor 不是控件**）的 Class Defaults 里填 `HUD Widget Class = WBP_RPG_HUD`、`Damage Number Widget Class = WBP_DamageNumber`；再把 `BP_RPG_GameModeBase` 的 `HUD Class` 指到 `BP_RPG_HUD`。两个类都不填的表现是"进游戏什么都没有"，前者会打 Warning、后者静默（飘字缺席不影响玩法验证）。`BP_RPG_HUD` 本身没有可接的动画 —— 它是 Actor，动画都在 WBP 里。

---

### `URPG_HUDWidget` —— 主 HUD：属性走委托、状态走轮询的那一半

**文件**：`Source/RPG/UI/RPG_HUDWidget.h` / `Source/RPG/UI/RPG_HUDWidget.cpp`　**继承**：`UUserWidget`（`UCLASS(Abstract)`）

| | |
|---|---|
| **它依赖谁** | `ARPG_BaseCharacter`：`GetOwningPlayerPawn()` → Cast 得到本地角色，再拿 `GetAbilitySystemComponent()` / `GetCombatComponent()` / `GetRespawnDelay()`。`UAbilitySystemComponent`：8 个属性的 `GetGameplayAttributeValueChangeDelegate(...).AddUObject(...)` + `HasMatchingGameplayTag` + `GetSet<URPG_AttributeSet>()`。`URPG_AttributeSet`：`GetHealth/GetMaxHealth/GetMana/GetMaxMana/GetStamina/GetMaxStamina/GetAttack/GetDefense` 八个 getter 与同名 `GetXxxAttribute()` 静态属性。`URPG_CombatComponent`：`GetComboIndex()`（轻击段位）、`GetAttackModule()`。`URPG_AttackModuleData` + `FRPG_HeavyAttackLevel`：`GetHeavyLevelCount()` / `GetHeavyLevel(N)` / `RequiredChargeTime`（蓄力条分母）。`RPGTags`：`State_Dodging` / `State_Attack_Charging` / `State_Attack_Transition` / `State_Attacking` / `State_Dead`。`URPG_AttributeBarWidget`：`SetAttributeValues()`。UMG：`UProgressBar` / `UImage` / `UTextBlock` / `UWidget` |
| **谁依赖它** | `ARPG_HUD::BeginPlay` 造它并 `AddToViewport(0)`；`WBP_RPG_HUD` 提供 11 个同名控件；`URPG_AttributeBarWidget` 的三个实例被它驱动 |
| **参与的功能** | 属性条（血/蓝/耐力）、攻防数值、蓄力条、招式名、闪避图标、死亡面板 + 重生倒计时 |
| **关键成员** | `BindWidgetOptional` 控件：`HealthBar` / `ManaBar` / `StaminaBar`（类型是 `URPG_AttributeBarWidget`，不是裸进度条）、`AttackText` / `DefenseText` / `DodgeIcon` / `SkillPanel` / `ChargeBar` / `MoveNameText` / `DeathPanel` / `RespawnCountdownText`。状态：`BoundASC`（`TWeakObjectPtr`）、`AttributeDelegateHandles` + `BoundAttributes`（成对保存，解绑要用 handle）、`ChargeDisplayTime` + `bWasCharging`、`bWasDead` + `RespawnCountdown`、`LastMoveName`（避免每帧写 `TextBlock` 触发排版）。文案：`LightAttackFormat` / `HeavyAttackText` / `TransitionAttackText` / `RespawnCountdownFormat` / `AttackValueFormat` / `DefenseValueFormat` |

**协作要点**：它把"两类数据源"的分工固定在这个类里。属性侧一次绑 8 个属性、共用一个 `HandleAttributeChanged` 回调再按 `Data.Attribute` 分派；**同时监听 `MaxHealth` 等上限属性**，因为"满血的定义"变了但当前值没变时血条也该动。绑完立刻 `RefreshAllAttributes()` 整表刷一次，否则满血玩家开局看到的是三条空条。`UnbindFromASC()` 用保存下来的 handle 逐个 `Remove` 而**不是** `RemoveAll` —— 后者会把头顶血条等别的 UI 注册在同一属性上的回调一起摘掉。状态侧靠 `NativeTick` 轮询：`State_Attack_Charging` 当节拍器做边沿检测，**UI 自己用 `InDeltaTime` 累计**蓄力时间（不读 `URPG_GA_HeavyAttack` 的私有 `ChargeElapsed`，它每 0.1 秒才更新一次，直接画进度条会看到台阶）；蓄力条的分母取自数据资产的**最后一段** `RequiredChargeTime`，而不是 GA 上的"强制释放保护上限"。死亡倒计时也是客户端自己数（`State.Dead` 是复制的、`RespawnDelay` 是配置常量），`RespawnDelay == 0` 时只显示"你死了"不显示倒计时。招式名的判定顺序被刻意固定为**切手 > 蓄力 > 轻击**（三者共用输入与 GA，顺序反了会出现"放的是切手技、UI 显示轻击"）。

> ⚠️ **一处文档与实现不一致（按代码为准）**：`ChargeDisplayTime` 的注释写"段位仍然读权威的 Lv1/Lv2/Lv3 标签"，但整个 `Source/RPG/UI/` 里**没有任何对 `State.Attack.Charging.LvN` 的引用**（`RPG_GameplayTags.h:128-130` 定义了这三个标签）。实际实现是：轻击段位读 `URPG_CombatComponent::GetComboIndex()`，蓄力中只显示固定文案 `HeavyAttackText`，不显示蓄力段位。

**WBP 侧要配合做什么**：`WBP_RPG_HUD`（父类 `URPG_HUDWidget`，根节点 `Canvas Panel`）里这 11 个控件名就是契约 —— 因为全标了 `BindWidgetOptional`，**少做哪个就少哪个功能，不会编译失败**（这也是它比 `URPG_AttributeBarWidget` 宽容的地方）。两种可见性写法有区别：`DodgeIcon` / `SkillPanel` / `DeathPanel` / `RespawnCountdownText` 都是在 `HitTestInvisible` 和 `Hidden` 之间切，不是 `Collapsed`。**`Tick Frequency` 不能改成 `Never`** —— 右侧招式区、闪避图标、死亡面板全靠 `NativeTick`，关掉之后表现是"进游戏显示一次，之后永远不动"，且一条日志都不打。文案格式全在 Class Defaults 里改，不用重编译。

---

### `URPG_AttributeBarWidget` —— 一个"数值→比例+文本"的换算器，血/蓝/耐力三处共用

**文件**：`Source/RPG/UI/RPG_AttributeBarWidget.h` / `Source/RPG/UI/RPG_AttributeBarWidget.cpp`　**继承**：`UUserWidget`（`UCLASS(Abstract)`）

| | |
|---|---|
| **它依赖谁** | 只有 UMG：`UProgressBar`（`SetPercent`）、`UTextBlock`（`SetText`）、`UImage`；外加 `FText` 格式化。**它不认识 GAS** —— 没有一处 include `AbilitySystemComponent.h` / `RPG_AttributeSet.h` |
| **谁依赖它** | `URPG_HUDWidget::RefreshAttribute()`：属性委托触发时调 `SetAttributeValues(Health, MaxHealth)` 等三处；`WBP_RPG_HUD` 里三个实例（`HealthBar` / `ManaBar` / `StaminaBar`）；WBP 子类 `WBP_HealthBar` / `WBP_ManaBar` / `WBP_StaminaBar` 继承它 |
| **参与的功能** | 属性条（血/蓝/耐力三条共用同一份逻辑），以及 WBP 侧扩展出来的数值滚动、掉血闪红、低血脉动 |
| **关键成员** | `Bar`（`BindWidget` **必填**，`UProgressBar`）；`ValueText` / `Icon`（`BindWidgetOptional`）；`ValueFormat = "{0} / {1}"`；`CachedCurrent` / `CachedMax` / `CachedPercent`（对外读数，`GetPercent()` / `GetCurrentValue()` / `GetMaxValue()` 三个 `BlueprintPure`）；`BP_OnAttributeValuesChanged(Current, Max, bIncreased)`（`BlueprintImplementableEvent`） |

**协作要点**：它是本层唯一一个"完全无状态依赖"的类 —— 不知道自己显示的是血还是耐力，也不知道值从哪来，那由 `URPG_HUDWidget` 去接 ASC。抽出来的理由是三条属性条的换算完全一样、只有颜色和图标不同，各写三遍的话将来加"数值平滑过渡"必然漏一处。两个刻意的写法：`Max <= KINDA_SMALL_NUMBER` 时按"满"处理（属性集初始化前的窗口期里除以 0 会得到 NaN，进度条会变成一条诡异白线或干脆不显示，看起来像"UI 没接上"）；`SetAttributeValues` 里**先存旧值再覆盖缓存**再算 `bIncreased`，顺序反了蓝图那边的"掉血闪红"永远不触发且不报错。

**WBP 侧要配合做什么**：`WBP_AttributeBar` 里 `Bar`（Progress Bar）必填，`ValueText`（Text）、`Icon`（Image）可选。可接的事件是 **`On Attribute Values Changed(Current, Max, bIncreased)`** —— 项目规范给的典型用法是并在下面叠一个 `LagBar`，在这个事件里播 `LagCatchUp` 动画补到当前值，做出"残影血条"；也可以在这里换低血填充色或闪红。`SetAttributeValues` 被声明为 `BlueprintCallable` 但**不该由 WBP 主动调**，它是被 HUD 推着走的。

---

### `URPG_OverheadHealthBarComponent` —— 只比引擎组件多做一件事：把"我是谁的血条"递进去

**文件**：`Source/RPG/UI/RPG_OverheadHealthBarComponent.h` / `Source/RPG/UI/RPG_OverheadHealthBarComponent.cpp`　**继承**：`UWidgetComponent`

| | |
|---|---|
| **它依赖谁** | `URPG_OverheadHealthBarWidget`：`InitWidget()` 里 `Cast<URPG_OverheadHealthBarWidget>(GetUserWidgetObject())` 后调 `SetOwningActor(GetOwner())` |
| **谁依赖它** | `ARPG_BaseCharacter` 构造函数：`CreateDefaultSubobject<URPG_OverheadHealthBarComponent>("OverheadHealthBar")` + `SetupAttachment(RootComponent)` + `SetRelativeLocation(0,0,110)` + `CastShadow = false`；`ARPG_BaseCharacter::RefreshOverheadWidgetVisibility()` 通过 `GetOverheadHealthBar()` 改它的**组件级**可见性；`BP_RPG_Player` / `BP_RPG_Enemy` 在 Details 里给它配 `Widget Class` |
| **参与的功能** | 头顶血条（承载与"主人身份"的注入；不参与飘字、不参与主 HUD） |
| **关键成员** | 构造函数里四个设定：`SetWidgetSpace(EWidgetSpace::Screen)`（恒定屏幕尺寸，不给透视缩放）、`SetDrawSize(FVector2D(140,20))`、`SetTickWhenOffscreen(false)`（血条变化时自己会重绘，不需要每帧重建 Slate 布局）、`SetVisibility(false)`（构造期先关，避免生成那一帧闪一下）；唯一的行为重写是 `InitWidget()` |

**协作要点**：这个类存在的唯一理由是**传递链路的两条路都是断的** —— `UWidgetComponent` 造 Widget 走的是 `CreateWidget(World, WidgetClass)`，那个重载把 PlayerContext 设成 `GameInstance.GetFirstGamePlayer()`（Widget 本身还被 outer 到 GameInstance 上），于是 `GetOwningPlayerPawn()` 返回的是**本地玩家**、`GetTypedOuter<UWidgetComponent>()` 也找不到组件。后果是所有敌人的血条都显示玩家自己的血量和名字，而且完全不报错。所以只能在"Widget 刚被造出来"的那一刻显式传 —— `InitWidget()` 正是那个时刻，并且它是 `virtual`。调用时机是 `UWidgetComponent::BeginPlay()` → `InitWidget()`，早于角色的 `ReceiveBeginPlay`，所以角色自己的 `BeginPlay` 里拿 `GetUserWidgetObject()` 时它一定就绪。`Cast` 失败（没配 Widget Class，或类型不对）会打一条 Warning —— 那是个纯静默失败，肉眼排查会绕很远。

**WBP / 蓝图侧要配合做什么**：`BP_RPG_Player` 和 `BP_RPG_Enemy` 上选中 `OverheadHealthBar` 组件，把 `Widget Class` 设成 `WBP_OverheadHealthBar`（`Space` / `Draw Size` / `Relative Location` C++ 已设，确认即可）。**不要把这个组件换成引擎自带的 `Widget Component`** —— 换掉就等于放弃 `SetOwningActor`，血条会静默显示成玩家自己的数据。

---

### `URPG_OverheadHealthBarWidget` —— 每个角色一条、自己订阅自己主人的、挨打才亮 5 秒的血条

**文件**：`Source/RPG/UI/RPG_OverheadHealthBarWidget.h` / `Source/RPG/UI/RPG_OverheadHealthBarWidget.cpp`　**继承**：`UUserWidget`（`UCLASS(Abstract)`）

| | |
|---|---|
| **它依赖谁** | `AActor`（`OwningActor`，`TWeakObjectPtr`）；`ARPG_BaseCharacter`：Cast 后 `GetAbilitySystemComponent()`（基类把"玩家去 PlayerState 找、敌人从自己身上拿"抹平了）；`UAbilitySystemComponent`：`GetGameplayAttributeValueChangeDelegate(Health/MaxHealth).AddUObject(...)`、`GetSet<URPG_AttributeSet>()`；`URPG_AttributeSet`：`GetHealth()` / `GetMaxHealth()`；`UProgressBar` / `UTextBlock`；`UWorld::GetTimerManager()`；`LogRPG_Combat` |
| **谁依赖它** | `URPG_OverheadHealthBarComponent::InitWidget()`：`SetOwningActor(GetOwner())` —— 它唯一的数据注入点；`BP_RPG_Player` / `BP_RPG_Enemy` 的 `OverheadHealthBar` 组件 `Widget Class` 指向它的 WBP 子类 |
| **参与的功能** | 头顶血条：挨打亮起 → 5 秒后收回、残血阈值通知、角色名显示、联机下读复制过来的血量 |
| **关键成员** | `OwningActor` / `BoundASC`（双 `WeakObjectPtr`）；`HealthChangedHandle` + `MaxHealthChangedHandle`（两个属性**共用** `HandleHealthChanged` 回调）；`LastSeenHealth`（掉血检测的基线，初始 `-1`）；`HideTimerHandle` / `BindRetryHandle`（两个 `FTimerHandle`）；`RevealDuration = 5.f`、`LowHealthThreshold = 0.3f`、`bWasLowHealth`；`BindRetryInterval = 0.2f`、`AttributeSetWarnAfterAttempts = 25`；`BindWidget` 的 `HealthBar`（必填）与 `BindWidgetOptional` 的 `NameText`；事件 `BP_OnRevealChanged(bool)` 和 `BP_OnLowHealth(bool)` |

**协作要点**：**触发源用"血量下降"而不是 `Event.Combat.Hit`** —— 后者只在服务器广播（`RPG_AttributeSet` 里有权威判断），客户端血条根本收不到，表现是"主机看得见、客户端看不见"；而"血量下降"在两端都会触发（服务器 GE 改值 / 客户端 `OnRep` 写值，走同一个委托），顺带还覆盖了没有单次命中事件的持续伤害。掉血比较用**自己缓存的 `LastSeenHealth`** 而不是 `FOnAttributeChangeData::OldValue`（后者在客户端复制路径上不一定被填，而且这个函数还有"刚接上时主动读一次"的第二个入口）。计时走 `TimerManager` 而不是 `NativeTick`：控件常规是 `Collapsed` 的，隐藏的 Widget 还 tick 不 tick 属于 Slate 实现细节，把"5 秒后收回"押在那上面等于埋雷；同一个 handle 再 `SetTimer` 会替换上一个，正好实现"连续挨打刷新计时而不是叠加"。绑定要重试：组件 `BeginPlay` 早于角色 GAS 初始化，Widget 造出来时 ASC 和属性集都还没有 —— `TryBindToOwnerASC()` 里**"还没就位"那段必须排在"主人没变就直接返回"前面**（`ASC == BoundASC.Get()` 在初始状态下是 `null == null`，判定成立就会认定"已接上"并 return，重试逻辑变死代码；而且症状是"看 AI 有血条、看玩家没有"）。两条容易写错的纪律：`RefreshBarValues()` 里**不要**顺手写 `BoundASC`（写了会让订阅判定以为已接上，从此永不绑委托，血条卡住不动）；`NativeConstruct()` 是**会跑第二遍**的（组件不可见时 Slate 对象析构、恢复可见时重建），所以必须清掉 `LastSeenHealth` 基线，否则组件隐藏期间掉的血会让血条凭空亮一下。`CanBeRevealed()` 用"血量 > 0"而不是查 `State.Dead`，避免对尸体显示空血条。

> ⚠️ **一处文档与实现不一致（按代码为准）**：`ResolveOwnerASC()` 上方的注释写"所以 NativeTick 里会一直重试到拿到为止"，但本类**没有 `NativeTick` 重写**，实际重试走的是 `BindRetryHandle` 循环定时器（头文件另一处 `TryBindToOwnerASC` 的注释才是对的）。

**WBP 侧要配合做什么**：`WBP_OverheadHealthBar`（父类 `URPG_OverheadHealthBarWidget`）里 `HealthBar`（Progress Bar）**必填**、`NameText`（Text）可选。两个事件可以接动画：`On Reveal Changed(bool bRevealed)` 在亮起/收回时各调一次（已做边沿检测，亮着再挨打只重置计时、不重发），接 `Fade In`/`Fade Out` 就是淡入淡出、不接就是硬切；`On Low Health Changed(bool bIsLow)` 只在跨越 `LowHealthThreshold` 那一次调，用来做残血闪烁。⚠️ 动画里**用 `Opacity` 而不是 `Set Visibility → Visible`** —— 后者会在动画播完后把控件锁在可见状态，`HideAfterDamage()` 再设 `Collapsed` 也会被动画覆盖，表现是"血条淡出之后再也不亮"。

---

### `URPG_DamageNumberWidget` —— 一个数字：自己飘、自己淡、自己送走自己

**文件**：`Source/RPG/UI/RPG_DamageNumberWidget.h` / `Source/RPG/UI/RPG_DamageNumberWidget.cpp`　**继承**：`UUserWidget`（`UCLASS(Abstract)`）

| | |
|---|---|
| **它依赖谁** | 只有 `UTextBlock`（`SetText`）+ 自身 `NativeTick` + `UUserWidget` 自带的视口 API（`SetPositionInViewport` / `SetAlignmentInViewport` / `SetRenderOpacity` / `SetRenderTranslation` / `RemoveFromParent`）。**没有 include 任何游戏代码** —— 唯一的业务输入是 `InitializeDamageNumber` 传进来的一个 `float` |
| **谁依赖它** | `ARPG_HUD::ShowDamageNumber()`：`CreateWidget` → `AddToViewport(10)` → `InitializeDamageNumber(Amount, ScreenPosition, 0.f)`，并把它加进 `ActiveDamageNumbers`；`ARPG_HUD::Tick()` 在 `bDamageNumbersTrackWorld` 开启时调 `UpdateScreenPosition()`；`ARPG_HUD::PruneDamageNumbers()` 靠 `IsInViewport()` 判断它还活着；`ARPG_HUD::EndPlay()` 直接 `Reset()` 两个数组 |
| **参与的功能** | 伤害飘字（由 `RPG_AttributeSet::PostGameplayEffectExecute` → `ARPG_BaseCharacter::Multicast_ShowDamageNumber` → `ARPG_HUD::ShowDamageNumber` 这条链驱动） |
| **关键成员** | `AmountText`（`BindWidget` **必填**）；运动参数 `RiseDistance = 70.f` / `RiseEasingExponent = 2.f` / `FadeOutStartRatio = 0.45f` / `DefaultLifetime = 0.9f`；`OriginPosition` / `ElapsedTime` / `TotalLifetime`；`InitializeDamageNumber()`、`UpdateScreenPosition()`、`GetElapsedRatio()`（`BlueprintPure`）；事件 `BP_OnDamageNumberInitialized(float Amount)` |

**协作要点**：它**不是 `WidgetComponent`**。头顶血条是每角色一个、长期存在，所以挂在组件上；飘字恰好相反 —— 短命、数量多、每帧都在动，给每次命中生成一个场景组件的开销在群怪混战时很难看。所以走纯屏幕空间：HUD 投影一次 → 控件摆在那个点上 → 自己 `Tick` 上飘+淡出 → 到点 `RemoveFromParent`。代价是数字**不跟着角色走**（只在生成瞬间算一次位置），对不到 1 秒的飘字看不出来，`ARPG_HUD::bDamageNumbersTrackWorld` 留了口子。两个容易被忽略的正确性细节：`SetPositionInViewport(ScreenPosition, /*bRemoveDPIScale=*/true)` —— 我们算的是像素而 UMG 位置是 Slate 单位，不除 DPI 的话在 125%/150% 缩放的显示器上飘字会偏出去一大截，而且只在别人机器上复现；上飘用 `SetRenderTranslation` 而不是改 Canvas 槽位，前者不触发布局重算，几十个飘字同时飘也不掉帧。**它自己管自己的生命周期**（`NativeTick` 到点 `RemoveFromParent`），HUD 只在旁边观察 —— 这直接决定了 `ARPG_HUD::PruneDamageNumbers()` 的判活条件必须是 `IsInViewport()`：`RemoveFromParent` 不会把对象标记成待回收，而 `UPROPERTY` 数组持强引用，`!IsValid()` 永远为 false，清理循环一个都删不掉。

**WBP 侧要配合做什么**：`WBP_DamageNumber`（父类 `URPG_DamageNumberWidget`，根节点保持默认 `Canvas Panel`）里只需一个 `AmountText`（Text，**必填**），字体大小/描边在 WBP 里调；位置由 C++ 用 `SetPositionInViewport` 摆，WBP 不用管布局。可接的事件是 **`On Damage Number Initialized(float Amount)`** —— 按数值大小换字号/颜色、播"弹出+回弹"动画、给大伤害换音效都在这里；淡出与位移已经由 C++ 驱动（避免"动画时长"和"生命周期"分散在两个地方各改各的），WBP 里再叠动画是纯增量。

> 注：`RPG_DamageNumberWidget.h` 里 `InitializeDamageNumber` 的注释写"由 `URPG_HUDWidget` 在创建之后立刻调用"，实际调用者是 `ARPG_HUD`（`RPG_HUD.cpp:188`）—— 注释笔误，按代码为准。

---

## 附录：本次核实出的"类在那儿、但没人用 / 注释不符"

> 写这份文档时逐条核对了 include 与调用点（全库 `grep` + 源码级确认）。
> 以下是**核实出来的**问题，供后续清理参考。
> **不影响编译，也不一定都是 bug**（有些是刻意预留的扩展点）。

### A. 声明了但没有 C++ 调用方

| 类 / 成员 | 情况 | 判断 |
|---|---|---|
| `URPG_AbilitySystemLibrary` 的 4 个函数 | `Source/` 下无 C++ 调用点 | **预留工具层** —— 引擎的 `GetSetOnActor<T>()` 是模板函数、蓝图用不了，所以包了这一层。蓝图侧的引用未能完全核实（`.uasset` 是二进制） |
| `URPG_InputConfig` 的 3 个查询函数 | 同上 | 同上 |
| `ARPG_Enemy::GetRPGAbilitySystemComponent()` | 无 C++ 调用者 | 可能只给蓝图用 |
| `URPG_CombatComponent::GetCombatDebugString()` | 无 C++ 调用者 | 调试辅助，可能只给控制台命令 |
| `RPGTags::State_Attack_ComboWindow` | 无 C++ 读写 | 可能是给动画资产用的 |

### B. 冗余 include

| 位置 | 情况 |
|---|---|
| `RPG_AttackModuleData.cpp` | include 了 `RPG_AnimNotifyState_Invulnerability.h`，但 `ScanMontageNotifies()` 里没用到 |

### C. 注释与实现不符（**已在本次修正**）

| 位置 | 原文说 | 实际是 |
|---|---|---|
| `RPG_OverheadHealthBarWidget.h` | 订阅失败"NativeTick 里会一直重试" | 走 `TryBindToOwnerASC()` 的**定时器**（代码早已改掉，注释没跟上） |
| `RPG_DamageNumberWidget.h` | `InitializeDamageNumber` 由 `URPG_HUDWidget` 调用 | 由 `ARPG_HUD::ShowDamageNumber()` 调用 |
| `RPG_HUDWidget.h` | 蓄力段位"读权威的 Lv1 / Lv2 / Lv3 标签" | 实际从 `URPG_CombatComponent::GetComboIndex()` 读连段索引 —— 段数没有上限，靠标签表达不了（`.cpp` 里本来就有这条注释，头文件那句是旧设计残留） |

> 这三条是**同一个毛病**：改了代码，只更新了 `.cpp` 里那处注释，
> 漏了头文件里描述同一件事的那句。
> **同一条知识写在两个地方，就一定会有一处过期。**
>
> 这也是这份文档存在的理由之一 —— 把"谁跟谁有关系"集中写一遍，
> 就不必在每个文件里各写一份会过期的描述。

---

*本文档由 5 个独立读者分头通读源码写成 · 覆盖 52 个类 / 类型 · 最后更新：阶段 9-①（`c143056`）之后*
