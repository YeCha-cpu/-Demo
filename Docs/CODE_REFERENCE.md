# RPG 代码参考手册

> **逐文件、逐函数、逐枚举的完整参考。**
> 读完之后，你不需要再打开头文件就能知道每个函数在干什么、为什么这么写、被谁调用。

## 这份文档怎么用

| 你想 | 去哪 |
|---|---|
| 先搞懂"这是个什么项目" | [`../README.md`](../README.md) |
| 搞懂"这些零件怎么咬合成一条链" | [`REVIEW_PHASES_0-4.md`](./REVIEW_PHASES_0-4.md) |
| 查"这个函数到底干了什么" | **本文档** |
| 查"为什么当初这么决定 / 踩过什么坑" | [`REVIEW_GUIDE.md`](./REVIEW_GUIDE.md) |
| 查"现在是什么结构" | [`ARCHITECTURE.md`](./ARCHITECTURE.md) |

## 阅读须知

本文档由 6 个独立读者分头**通读源码**后写成，不是从注释或记忆转述的。
每一条都来自实际代码。

有两点需要提前知道：

1. **每章末尾有一份"代码与注释不一致 / 未使用项"清单。**
   那是写这份文档时顺带查出来的、**注释说了但代码没做（或反过来）**的地方。
   它们不影响编译，也不一定都是 bug（有些是预留），但看到注释时不要产生错误预期。
   汇总见下方 [附录](#附录已知的代码与注释不一致项)。

2. **本文档描述的是"代码现在是什么样"，不是"应该是什么样"。**
   如果是设计意图与实现不符，会如实写出两边。

---

## 目录

| 章 | 内容 |
|---|---|
| 一 | **Core / Input / Interfaces** —— 框架层：日志类别、GameplayTag 体系、PlayerState / PlayerController / GameMode、输入配置、接口与静态函数库 |
| 二 | **Character / Animation** —— 角色层三件套（基类 / 玩家 / 敌人）、动画实例基类、动画公共类型、4 个 AnimNotify 桥接类 |
| 三 | **GAS 核心** —— ASC 扩展、属性集、GA 基类、自定义 ExecutionCalculation、自定义 AbilityTask（武器轨迹检测） |
| 四 | **具体能力（GA）** —— 10 个能力：5 段轻击、3 段蓄力重击 + 切手技、闪避、受击、死亡、跳跃、奔跑、治疗、增益、耐力恢复 |
| 五 | **战斗规则与敌人 AI** —— 输入缓存、战斗状态组件、攻击模组 DataAsset、行为树装饰器 / 服务 / 任务、AIController |
| 六 | **战斗 HUD 与表现层** —— HUD、主界面、属性条、头顶血条（组件 + 控件）、伤害飘字 |
| 附 | **已知的代码与注释不一致项** —— 全库 grep 核实出来的 14 条 |

> 中文标题在 GitHub 上的锚点规则不稳定，所以这里不放跳转链接 ——
> 用编辑器的目录视图或 `Ctrl+F` 搜"# 一、"这类标题更快。

---

# 一、Core / Input / Interfaces

本章覆盖 8 个模块、15 个文件：

| 模块 | 文件 |
| --- | --- |
| Core | `RPG_LogChannels.h/.cpp`、`RPG_GameplayTags.h/.cpp`、`RPG_GameModeBase.h/.cpp`、`RPG_PlayerState.h/.cpp`、`RPG_PlayerController.h/.cpp` |
| Input | `RPG_InputConfig.h/.cpp` |
| Interfaces | `RPG_AbilitySystemInterface.h`（无 .cpp） |
| AbilitySystem | `RPG_AbilitySystemLibrary.h/.cpp` |

**读本章前需要知道的四条项目约定**（后续各节都依赖它们）：

1. **玩家 ASC 挂在 `ARPG_PlayerState` 上，敌人 ASC 挂在自己身上。** 这个差异由 `ARPG_BaseCharacter::GetAbilitySystemComponent()`（内部转发到纯虚的 `GetASCInternal()`）抹平。
2. **所有玩家输入走 `ARPG_PlayerController` + 增强输入，AI 输入走完全相同的入口**：都是 `URPG_CombatComponent::PushInputTag()` 推缓存 + `URPG_AbilitySystemComponent::TryActivateAbilityByInputTag()` 激活（AI 侧见 `Source/RPG/AI/Tasks/RPG_BTTask_Attack.cpp:89-93`）。
3. **联机范围 L1（基础复制）+ L2（权威与预测），Listen Server 为主，Standalone 自动降级**（各处 `IsLocalController()` 判断在单机恒为 true）。
4. **GameplayTag 用 `UE_DEFINE_GAMEPLAY_TAG_COMMENT` 在 C++ 里声明**，不写 `DefaultGameplayTags.ini`。

---

### `Source/RPG/Core/RPG_LogChannels.h` / `Source/RPG/Core/RPG_LogChannels.cpp`

**一句话职责**：把项目日志按功能域拆成 5 个独立类别，使 GAS 调试时可以单独把某一类开到 `VeryVerbose`，而不是在上百行混杂日志里翻找。

**类/结构体/命名空间**：无。整个头文件只有 5 条 `DECLARE_LOG_CATEGORY_EXTERN`，实现文件只有 5 条对应的 `DEFINE_LOG_CATEGORY`。

**.h 中的宏**（每条的参数都是 `(类别名, 默认运行时级别, 编译期最高级别)`，五条统一为 `(X, Log, All)`）：

| 类别 | 语义域（头文件注释原文） |
| --- | --- |
| `LogRPG` | 通用 / 未分类 |
| `LogRPG_Ability` | GAS 层：ASC 初始化、能力激活/结束、GE 施加、标签变化 |
| `LogRPG_Combat` | 战斗层：输入缓存、连段推进、检测框命中、伤害结算 |
| `LogRPG_AI` | AI 层：感知更新、行为树节点执行 |
| `LogRPG_Animation` | 动画层：AnimNotify 广播、状态机变量更新 |

**关键实现**

- `.cpp` 里 5 行 `DEFINE_LOG_CATEGORY(LogRPG)`、`(LogRPG_Ability)`、`(LogRPG_Combat)`、`(LogRPG_AI)`、`(LogRPG_Animation)`，与头文件声明一一对应，再无其他代码。
- 默认运行时级别是 `Log`，编译期上限是 `All`（意味着允许运行时用控制台命令临时调到 `Verbose`/`VeryVerbose`）。

**为什么这么写**（头文件长注释，逐条转述）

- **为什么不用单一 `LogRPG`**：GAS 调试时日志量极大（一次能力激活可能刷十几条），混在一个类别里想看战斗判定就得在几百行里翻。拆开后可以单独开：
  ```
  Log LogRPG_Combat VeryVerbose      // 只看战斗判定
  Log LogRPG_Ability VeryVerbose     // 只看能力激活/结束
  Log LogRPG_Ability Log             // 关回去
  Log LogRPG_Combat Off              // 完全静音
  ```
  也可以写进 `Config/DefaultEngine.ini` 让它启动就生效：
  ```
  [Core.Log]
  LogRPG_Combat=VeryVerbose
  ```
- **默认级别为什么是 `Log` 而不是 `Verbose`**：交付版本不希望刷屏；开发期用控制台命令临时打开即可。若某类需要长期开 `Verbose`（注释举的例子是 Ability），把声明里的 `Log` 改成 `Verbose` 即可。

**被谁调用 / 调用谁**：本文件不调用任何东西，只被调用。当前 `Source/RPG` 下的引用点计数（排除本文件自身，按标识符精确匹配）：

| 类别 | 引用点数量 |
| --- | --- |
| `LogRPG` | 20 |
| `LogRPG_Ability` | 58 |
| `LogRPG_Combat` | 73 |
| `LogRPG_AI` | 22 |
| `LogRPG_Animation` | 12 |

本章范围内的直接使用者：`ARPG_GameModeBase` 构造函数（`LogRPG`/Verbose）、`ARPG_PlayerState`（include 了头文件）、`ARPG_PlayerController`（`LogRPG` 与 `LogRPG_Ability`，含 Error 级配置报错与 `VeryVerbose` 级服务器丢弃日志）。

---

### `Source/RPG/Core/RPG_GameplayTags.h` / `Source/RPG/Core/RPG_GameplayTags.cpp`

**一句话职责**：项目全部原生 GameplayTag 的唯一集中声明处（.h 声明、.cpp 定义并自动注册），共 **76 个**标签。

**类/结构体/命名空间**：`namespace RPGTags` —— 不含类，只有 76 个 `FNativeGameplayTag` 变量（.h 中 `extern`、.cpp 中定义）。

**宏用法（两个文件的配合）**

- `.h`：`RPG_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Input_Attack_Light);` —— 声明一个带模块导出宏的 `FNativeGameplayTag` 外部变量，变量名即 C++ 侧的引用名。
- `.cpp`：`UE_DEFINE_GAMEPLAY_TAG_COMMENT(Input_Attack_Light, "Input.Attack.Light", "轻击输入（鼠标左键）");` —— 定义该变量，第三个参数是标签注释（编辑器/调试时可见）。
- 注册时机：`.cpp` 顶部的注释说明，这些变量在**模块加载时通过 `FNativeGameplayTag` 的构造函数自动向 GameplayTag 表注册**，所以不需要在 `Config/DefaultGameplayTags.ini` 里重复声明。
- 调试：编辑器控制台 `GameplayTags.List`（列出全部已注册标签）与 `GameplayTags.List Input`（按前缀过滤），会显示每个标签及其来源模块 `RPG`。

**为什么用 C++ 而不是 ini**（头注释，转述）

- 纯 ini 方案在 C++ 里只能 `FGameplayTag::RequestGameplayTag(FName("Input.Attack.Light"))`，字符串拼错编译期不报错、运行时才崩，而且崩在很远的地方、很难查。
- `UE_DEFINE_GAMEPLAY_TAG_COMMENT` 声明的标签：是真正的 C++ 变量（拼错名字编译不过）、IDE 能跳转和全局查找、重构重命名安全、模块加载时自动注册。
- 代价是改标签要重新编译，所以工程约定：**影响代码逻辑的"骨架标签"用 C++ 声明（本文件）；纯内容配置、策划频繁调整的标签后续可另建 ini。**

**命名规范**（头注释）：`<域>.<类别>.<子类别>.<具体>`，不超过 5 段，用单数名词。

**Input 和 Ability 为什么分成两套**（头注释）：两级解耦 —— `按键 → Input.Attack.Light →（映射表）→ Ability.Attack.Light`，中间映射表配在 `URPG_InputConfig` 资产里。好处：改按键只改 InputMappingContext 不碰代码；同一技能可被两个键触发（映射表配两条）；做按键重绑定只动输入层。若按键直接绑能力，这两个域就焊死了。

#### 标签逐条清单（76 条）

**Input.\* —— 玩家输入意图（9 条）**（由 `RPG_PlayerController` 产生，推入 `CombatComponent` 的输入缓存容器）

| C++ 变量 | 标签字符串 | 用途 |
| --- | --- | --- |
| `Input_Attack_Light` | `Input.Attack.Light` | 轻击输入（鼠标左键） |
| `Input_Attack_Heavy` | `Input.Attack.Heavy` | 重击输入（鼠标右键） |
| `Input_Dodge` | `Input.Dodge` | 闪避输入 |
| `Input_Sprint` | `Input.Sprint` | 奔跑输入（按住） |
| `Input_Jump` | `Input.Jump` | 跳跃输入 |
| `Input_Crouch` | `Input.Crouch` | 蹲伏输入（注：PC 的蹲伏实际走 `CrouchAction → ToggleCrouch()`，不经 GAS） |
| `Input_Spell_1` | `Input.Spell.1` | 法术 1 |
| `Input_Spell_2` | `Input.Spell.2` | 法术 2 |
| `Input_Spell_3` | `Input.Spell.3` | 法术 3 |

**Ability.\* —— 能力标识（18 条）**（用 `ASC->TryActivateAbilitiesByTag()` 触发；也用作 GA 的 `AssetTags`）

| C++ 变量 | 标签字符串 | 用途 |
| --- | --- | --- |
| `Ability_Attack` | `Ability.Attack` | 攻击类能力的**父标签**，本身不挂在任何能力上；只给 `CancelAbilities()` 当"取消组"用（匹配是层级式的，传父标签一次命中 `Ability.Attack.Light` 与 `Ability.Attack.Heavy`）。好处是加新攻击类型（如 `Ability.Attack.Special`）时，"受击要打断攻击"那处代码不用回来改 |
| `Ability_Attack_Light` | `Ability.Attack.Light` | 轻击能力（5 段连段） |
| `Ability_Attack_Light_01` ~ `_05` | `Ability.Attack.Light.01` ~ `.05` | 轻击 5 段的逐段能力 |
| `Ability_Attack_Heavy` | `Ability.Attack.Heavy` | 重击能力（3 段蓄力 + 切手技） |
| `Ability_Dodge` | `Ability.Dodge` | 闪避能力（含无敌帧） |
| `Ability_Sprint` | `Ability.Sprint` | 奔跑能力（持续消耗耐力） |
| `Ability_Jump` | `Ability.Jump` | 跳跃能力（一次性消耗耐力） |
| `Ability_Heal` | `Ability.Heal` | 治疗能力 |
| `Ability_Buff_AttackUp` | `Ability.Buff.AttackUp` | 加攻 Buff 能力 |
| `Ability_StaminaRegen` | `Ability.StaminaRegen` | 被动能力：常驻激活，负责耐力恢复（恢复开关由 `State.Stamina.Blocked` 控制） |
| `Ability_Death` | `Ability.Death` | 被动能力：死亡表现 |
| `Ability_Spell_1/2/3` | `Ability.Spell.1/2/3` | 法术 1/2/3 能力 |

**State.\* —— 持续状态标签（18 条）**（由 GE 的 `GrantedTags` 授予；任何系统都可查询：动画、AI、伤害计算）

| C++ 变量 | 标签字符串 | 用途 |
| --- | --- | --- |
| `State_Attacking` | `State.Attacking` | 攻击中（总括标签，动画蓝图用它判断是否走战斗分支） |
| `State_Attack_Windup` | `State.Attack.Windup` | 攻击前摇 |
| `State_Attack_Active` | `State.Attack.Active` | 伤害判定窗口开启中 —— WeaponTrace 只在此标签存在时采样检测 |
| `State_Attack_Recovery` | `State.Attack.Recovery` | 攻击后摇 |
| `State_Attack_ComboWindow` | `State.Attack.ComboWindow` | 连段衔接窗口开启中 —— 缓存容器只在此标签存在时被消耗 |
| `State_Attack_Charging` | `State.Attack.Charging` | 蓄力中 |
| `State_Attack_Charging_Lv1/2/3` | `State.Attack.Charging.Lv1/2/3` | 蓄力一段/二段/三段 |
| `State_Attack_Transition` | `State.Attack.Transition` | 切手技进行中（轻击连段中按右键切入的那一招）。头注释给了单独建标签的理由：切手技和蓄力重击**共用同一个 GA、同一个输入**，走哪条分支只由"激活瞬间在不在轻击连段里"决定，是 GA 的私有状态；没有标签的话 HUD 只能 Cast 到 GA 读私有成员（GA 实例随能力结束被回收）或靠蒙太奇名字猜（改名就坏）。挂成标签后"当前招式"对所有系统都是可查的公共状态 |
| `State_Dodging` | `State.Dodging` | 闪避中 |
| `State_Invulnerable` | `State.Invulnerable` | ★ 无敌帧。伤害 GE 检查此标签决定是否生效 |
| `State_Sprinting` | `State.Sprinting` | 以高于行走的速度移动。玩家的冲刺（GA_Sprint）和 AI 追击时切到的战斗速度都挂它 —— 它们对动画的要求一样（"别再播走路了"）。动画蓝图靠它决定用哪条移动状态机（见 `URPG_AnimInstanceBase::UpdateLocomotion` 的 `MovementState`） |
| `State_Blocking` | `State.Blocking` | 格挡（下期内容，先占位，避免将来改标签名） |
| `State_Hit` | `State.Hit` | 受击中 |
| `State_Dead` | `State.Dead` | 已死亡（`IsAlive()` 系列的判据） |
| `State_Stamina_Blocked` | `State.Stamina.Blocked` | ★ 耐力恢复阻断。存在时 `GE_StaminaRegen` 被抑制（走 `OngoingTagRequirements`） |
| `State_Combat_InCombat` | `State.Combat.InCombat` | 战斗中 |

**Event.\* —— 瞬时事件（14 条）**（用 `UAbilitySystemBlueprintLibrary::SendGameplayEventToActor` 广播、用 `UAbilityTask_WaitGameplayEvent` 监听。头注释强调：**这是动画 ↔ GAS 之间唯一的通信通道**：`AnimNotify → SendGameplayEventToActor → GA 的 WaitGameplayEvent 回调`）

| C++ 变量 | 标签字符串 | 用途 |
| --- | --- | --- |
| `Event_Combat_AttackWindow_Open` | `Event.Combat.AttackWindow.Open` | 伤害判定窗口开启。Payload 携带单次攻击信息（倍率、检测源、半径） |
| `Event_Combat_AttackWindow_Close` | `Event.Combat.AttackWindow.Close` | 伤害判定窗口关闭 |
| `Event_Combat_ComboWindow_Open` | `Event.Combat.ComboWindow.Open` | 连段衔接窗口开启。GA 收到后检查缓存容器，决定是否续段 |
| `Event_Combat_ComboWindow_Close` | `Event.Combat.ComboWindow.Close` | 连段衔接窗口关闭 |
| `Event_Combat_AttackEnd` | `Event.Combat.AttackEnd` | 攻击段结束。GA 收到后 `EndAbility`；`BTTask_RPG_Attack` 也监听它来结束潜在任务 |
| `Event_Combat_Hit` | `Event.Combat.Hit` | 命中发生 |
| `Event_Combat_Death` | `Event.Combat.Death` | 死亡 |
| `Event_Combat_AttackRequest` | `Event.Combat.AttackRequest` | AI 请求攻击。用事件而不是直接激活能力，是为了能携带"用哪个模组/第几段"参数 |
| `Event_Combat_ChargeStart` | `Event.Combat.ChargeStart` | 蓄力：开始 |
| `Event_Combat_ChargeLevelUp` | `Event.Combat.ChargeLevelUp` | 蓄力：升段 |
| `Event_Combat_ChargeRelease` | `Event.Combat.ChargeRelease` | 蓄力：松手释放 |
| `Event_Character_Invulnerability_Begin` | `Event.Character.Invulnerability.Begin` | 无敌帧开始。由 AnimNotifyState 广播，GA 据此上无敌 GE |
| `Event_Character_Invulnerability_End` | `Event.Character.Invulnerability.End` | 无敌帧结束 |
| `Event_Character_StaminaCost` | `Event.Character.StaminaCost` | 动画驱动的耐力消耗（攻击段在某一帧扣耐力） |

**Data.\* —— SetByCaller 传参键（5 条）**（用法：`Spec->SetSetByCallerMagnitude(RPGTags::Data_Damage_Multiplier, 1.15f)`）

| C++ 变量 | 标签字符串 | 用途 |
| --- | --- | --- |
| `Data_Damage_Multiplier` | `Data.Damage.Multiplier` | 本次攻击的伤害倍率（注释给出取值：轻击 1.0/1.15/1.4/1.6/2.0，重击 3.0/4.5/6.5） |
| `Data_Damage_Base` | `Data.Damage.Base` | 固定基础伤害（不依赖攻击力时使用，如陷阱） |
| `Data_Stamina_Cost` | `Data.Stamina.Cost` | 单次耐力消耗量 |
| `Data_Stamina_Rate` | `Data.Stamina.Rate` | 持续耐力消耗速率（每秒） |
| `Data_Heal_Amount` | `Data.Heal.Amount` | 治疗量 |

**Cooldown.\* —— 冷却标识（3 条）**（用作 `GE_Cooldown` 的 `GrantedTags`，查询用 `ASC->GetCooldownRemainingForTag`）

| C++ 变量 | 标签字符串 | 用途 |
| --- | --- | --- |
| `Cooldown_Attack_Light` | `Cooldown.Attack.Light` | 轻击冷却 |
| `Cooldown_Attack_Heavy` | `Cooldown.Attack.Heavy` | 重击冷却 |
| `Cooldown_Dodge` | `Cooldown.Dodge` | 闪避冷却 |

**Attack.Module.\* —— 攻击模组类型（3 条）**（挂在 `AttackModuleData` 资产上；GA 据它决定用哪套蒙太奇与检测源）

| C++ 变量 | 标签字符串 | 用途 |
| --- | --- | --- |
| `Attack_Module_Unarmed` | `Attack.Module.Unarmed` | 徒手模组（双手检测） |
| `Attack_Module_Melee` | `Attack.Module.Melee` | 近战武器模组（刀锋检测） |
| `Attack_Module_Ranged` | `Attack.Module.Ranged` | 远程武器模组（发射物） |

**GameplayCue.\* —— 特效音效（6 条）**（由 GE 的 `GameplayCues` 字段触发，或手动 `ExecuteGameplayCue`；头注释强调**命名必须以 `GameplayCue.` 开头**，否则 GC 系统不会识别）

| C++ 变量 | 标签字符串 | 用途 |
| --- | --- | --- |
| `GameplayCue_Combat_Hit` | `GameplayCue.Combat.Hit` | 普通命中特效音效 |
| `GameplayCue_Combat_HeavyHit` | `GameplayCue.Combat.HeavyHit` | 重击命中特效音效 |
| `GameplayCue_Combat_Death` | `GameplayCue.Combat.Death` | 死亡特效音效 |
| `GameplayCue_Character_Dodge` | `GameplayCue.Character.Dodge` | 闪避特效音效 |
| `GameplayCue_Character_Heal` | `GameplayCue.Character.Heal` | 治疗特效音效 |
| `GameplayCue_Character_Buff` | `GameplayCue.Character.Buff` | Buff 特效音效 |

**被谁调用 / 调用谁**：本文件不调用任何东西。当前 `Source/RPG` 下 `RPGTags::` 形式的引用共 **89 处**，分布在 **24 个文件**（`RPG_BaseCharacter.cpp`、`RPG_PlayerState.cpp`、全部 `RPG_GA_*.cpp`、`RPG_DamageExecution.cpp`、`RPG_AttributeSet.cpp`、`RPG_AbilitySystemLibrary.cpp`、`RPG_AnimInstanceBase.cpp`、4 个 AnimNotify/NotifyState、`RPG_BTTask_Attack.cpp`、`RPG_HUDWidget.cpp`）。其中被 C++ 引用的标签共 **33 个**（`Ability_Attack*`、`Ability_Dodge/Heal/Jump/Sprint/StaminaRegen/Buff_AttackUp`、`Data_Damage_Multiplier/Heal_Amount/Stamina_Cost`、`Event_Character_Invulnerability_*`、`Event_Combat_*`、`Input_Attack_Light`、`State_Attack_Charging*`、`State_Attack_Transition`、`State_Attacking`、`State_Dead`、`State_Dodging`、`State_Hit`、`State_Invulnerable`、`State_Sprinting`）；其余标签由资产（GA/GE/蒙太奇通知/DataAsset）按标签名引用（未逐一核实）。

---

### `Source/RPG/Core/RPG_GameModeBase.h` / `Source/RPG/Core/RPG_GameModeBase.cpp`

**一句话职责**：项目默认 GameMode，唯一的动作是在构造函数里把各类默认类型指向本项目的 C++ 类。

**类/结构体/命名空间**：`ARPG_GameModeBase` —— 继承 `AGameModeBase`，无接口实现，无其他成员函数。

#### `ARPG_GameModeBase::ARPG_GameModeBase()`

- **干什么**：指定 4 个默认类，并打一条 Verbose 日志。
- **关键实现**（逐行）：
  1. `DefaultPawnClass = ARPG_Player::StaticClass();`
  2. `PlayerStateClass = ARPG_PlayerState::StaticClass();`
  3. `PlayerControllerClass = ARPG_PlayerController::StaticClass();`
  4. `HUDClass = ARPG_HUD::StaticClass();`
  5. `UE_LOG(LogRPG, Verbose, TEXT("ARPG_GameModeBase 构造完成，默认类已指定"));`
  - 本文件 include 了 `Character/RPG_Player.h`、`Core/RPG_LogChannels.h`、`Core/RPG_PlayerController.h`、`Core/RPG_PlayerState.h`、`UI/RPG_HUD.h`。
- **为什么这么写**（注释原文要点）：
  - 这里给的是 **C++ 层面的默认值，保证"即使不做任何蓝图子类也能跑起来"**；实际项目几乎总会在蓝图子类（`BP_RPG_GameModeBase`）里覆盖 `DefaultPawnClass` 指向 `BP_RPG_Player`，因为角色上要配 `InputConfig`、`StartupAbilities`、`InitAttributesEffect` 这些资产引用，只有在编辑器里才能可视化指定。
  - **`PlayerStateClass` 必须是 `ARPG_PlayerState`**，否则玩家的 ASC 无处安放，`GetASCInternal()` 会一直返回 `nullptr`，症状是"技能全失效但不报任何错" —— 注释称之为"本项目里最隐蔽的一个配置陷阱"。
  - **HUD 部分**：引擎会为每个玩家创建一个 HUD 实例；listen server 主机上远程玩家的 PlayerController 也有自己的 `AHUD`，但 `ARPG_HUD::BeginPlay` 里用 `IsLocalController()` 把它挡掉了（已核实：`Source/RPG/UI/RPG_HUD.cpp:37-43` 确实有 `if (!PC->IsLocalController()) return;`），所以那台机器上只会多出几个空壳，不会重复创建 UI。注释同时警告：`HUDClass` 这里只是 C++ 默认值，**真正要用的子类必须在蓝图里指定**（`WBP_RPG_HUD` 是蓝图资产，C++ 引用不到），见 `Docs/PHASE7_UI_SETUP.md` 第 6 步。
- **被谁调用 / 调用谁**：构造函数由引擎在生成 GameMode 时调用；本文件调用 `ARPG_Player::StaticClass()`、`ARPG_PlayerState::StaticClass()`、`ARPG_PlayerController::StaticClass()`、`ARPG_HUD::StaticClass()` 与 `UE_LOG(LogRPG, ...)`。

---

### `Source/RPG/Core/RPG_PlayerState.h` / `Source/RPG/Core/RPG_PlayerState.cpp`

**一句话职责**：玩家状态类，同时是**玩家 ASC 的宿主**（创建并持有 ASC 与属性集）。

**类/结构体/命名空间**：`ARPG_PlayerState` —— 继承 `APlayerState`，并实现两个接口：`IAbilitySystemInterface`（引擎接口，GAS 内部机制依赖）与 `IRPG_AbilitySystemInterface`（项目接口，补充属性集/存活查询）。前置声明了 `URPG_AbilitySystemComponent`、`URPG_AttributeSet`。

**成员变量**

| 成员 | 声明 | 用途 |
| --- | --- | --- |
| `AbilitySystemComponent` | `UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RPG\|Abilities") TObjectPtr<URPG_AbilitySystemComponent>`，protected | 玩家的 ASC。注释明确：它挂在 PlayerState 上，所以 `ActorInfo` 的 Owner = PlayerState、Avatar = 玩家角色，这个关联由 `ARPG_Player::InitializeAbilitySystem()` 建立 |
| `AttributeSet` | `UPROPERTY() TObjectPtr<URPG_AttributeSet>`，protected | 玩家属性集。注释：敌我用的是同一个类 |

#### `ARPG_PlayerState::ARPG_PlayerState()`

- **干什么**：创建 ASC 与属性集两个子对象、把 PlayerState 的复制频率提到 30 Hz、把属性集登记进 ASC。
- **关键实现**（4 步）：
  1. `AbilitySystemComponent = CreateDefaultSubobject<URPG_AbilitySystemComponent>(TEXT("AbilitySystemComponent"));`
  2. `AttributeSet = CreateDefaultSubobject<URPG_AttributeSet>(TEXT("AttributeSet"));`
  3. `SetNetUpdateFrequency(30.f);` —— 注释说明：`APlayerState` 的构造函数里硬编码了 `SetNetUpdateFrequency(1)`（注释标注 `PlayerState.cpp:28`），对"分数、名字"这类数据合理；但玩家的 ASC 挂在这个 PlayerState 上，而 ASC 的 `RepAnimMontageInfo`（蒙太奇同步）跟着 **PlayerState 的通道**走，于是"玩家的蒙太奇同步 ≈ 每秒 1 次，敌人的 ≈ 每秒 100 次（ASC 在角色身上，默认频率）"，表现是"看另一个玩家出招像幻灯片，看敌人却还行"。注意这是**整个 PlayerState** 的复制频率（属性、标签、GE 全都跟着提速），所以别设太高：30 已经和引擎 `NetServerMaxTickRate`（默认 30）持平，再高也发不出去。
  4. `AbilitySystemComponent->AddSpawnedAttribute(AttributeSet);` —— 注释标"⚠️ 这一行不能省"：`AddSpawnedAttribute` 把属性集登记进 ASC 的属性表，之后 `ASC->GetSet<URPG_AttributeSet>()` 才能找到它；漏掉不会报错，症状是"伤害没反应、血条不动"，非常难查。
- **为什么这么写**：构造函数里没有写复制相关设置，注释解释了原因 —— 分两处、这里都不需要重复写：ASC 的 `SetIsReplicated`/`SetReplicationMode` 在 `URPG_AbilitySystemComponent` 构造函数（敌我共用，改一处即可）；属性集的复制声明（`ReplicatedUsing` / `GetLifetimeReplicatedProps`）在 `URPG_AttributeSet`。另外 PlayerState 默认不 Tick，注释说明 ASC 和属性集都不需要每帧更新，**属性变化是通过 GE 的委托驱动的，不是轮询**。
- **被谁调用 / 调用谁**：由引擎（`PlayerStateClass`）实例化；调用 `CreateDefaultSubobject`、`SetNetUpdateFrequency`、`AbilitySystemComponent->AddSpawnedAttribute`。

#### `UAbilitySystemComponent* ARPG_PlayerState::GetAbilitySystemComponent() const`

- **干什么**：实现引擎 `IAbilitySystemInterface`，返回 `AbilitySystemComponent`。
- **关键实现**：单行 `return AbilitySystemComponent;`，无判空。
- **为什么这么写**：注释未说明。
- **被谁调用 / 调用谁**：`ARPG_Player::GetASCInternal()`（`Source/RPG/Character/RPG_Player.cpp:115-125`：`Cast<ARPG_PlayerState>(GetPlayerState())` 后调它）——这是玩家侧 ASC 的唯一解析路径。

#### `URPG_AttributeSet* ARPG_PlayerState::GetRPGAttributeSet() const`

- **干什么**：实现项目接口，返回属性集。
- **关键实现**：直接 `return AttributeSet;`（成员直取）。
- **为什么这么写**：注释给了理由 —— 直接用成员而不是 `ASC->GetSet<>()`：这里明确知道属性集就是自己创建的那个，少一次查表，也避免 ASC 尚未注册完成时返回 `nullptr`。
- **被谁调用 / 调用谁**：接口调用方（`URPG_AbilitySystemLibrary::IsAlive` 等走接口的代码路径）；未在 `Source/` 下检索到对 `ARPG_PlayerState::GetRPGAttributeSet` 的直接调用点（调用方走的是 `IRPG_AbilitySystemInterface` 基类指针）。

#### `bool ARPG_PlayerState::IsAlive() const`

- **干什么**：以 `State.Dead` 标签为判据返回存活状态。
- **关键实现**：
  - `if (!AbilitySystemComponent) return true;` 注释：初始化未完成，按存活处理（注释指向 `RPG_BaseCharacter::IsAlive` 的说明 —— 那边写得更细：如果这里返回 false，刚生成的角色会在第一帧被 AI 当成尸体忽略掉）。
  - `return !AbilitySystemComponent->HasMatchingGameplayTag(RPGTags::State_Dead);`
- **为什么这么写**：注释指向 `RPG_BaseCharacter::IsAlive` 的同款说明（判据是标签而非血量数值）。
- **被谁调用 / 调用谁**：调用 `UAbilitySystemComponent::HasMatchingGameplayTag` 与 `RPGTags::State_Dead`；接口调用方同上。

#### `URPG_AbilitySystemComponent* ARPG_PlayerState::GetRPGAbilitySystemComponent() const`（inline，头文件内联）

- **干什么**：`UFUNCTION(BlueprintPure, Category = "RPG|AbilitySystem")` 的便捷 getter，返回确切的 ASC 类型，省去每次 `Cast`。
- **关键实现**：`return AbilitySystemComponent;`。
- **为什么这么写**：头文件注释写的是"供 UI / 调试使用：拿到项目扩展的 ASC 类型，省去每次 Cast"。
- **被谁调用 / 调用谁**：`Source/` 下未检索到 C++ 调用点；`BlueprintPure` 说明设计意图是给蓝图（UI）用。

**头文件顶部长注释：为什么玩家 ASC 放 PlayerState 而不是角色身上**（转述，三条理由）

1. **死亡与重生**：角色死亡时 Character 会被销毁，ASC 挂它身上的话技能冷却、Buff 剩余时间、属性数值会一并消失；挂 PlayerState 上重生后状态自然延续。"死亡惩罚是清空 Buff 还是保留"是设计决策，放 PlayerState 让你**有得选**，放 Character 上根本没得选。
2. **Owner 与 Avatar 的语义分离**：`OwnerActor` = 能力属于谁（逻辑归属，用于 GE 来源判定、团队判定）；`AvatarActor` = 通过什么身体表现（动画、GameplayCue 定位）。玩家死后"魂还在、身体没了"—— Owner 仍有效、Avatar 为空，放 PlayerState 上天然表达这个关系。
3. **复制正确**：PlayerState 是网络复制中天然跟随玩家的 Actor，ASC 挂它下面，重生时属性/冷却/Buff 会自动同步给客户端；反过来放 Character 上会导致"角色重生后客户端拿不到正确 ASC 引用"这类经典 bug。

**敌人为什么反过来放自己身上**（同段注释）：敌人的 ASC 生命周期和它自己完全一致 —— 死了一起销毁、没有重生需求、没有跨 Actor 的状态延续问题，放自己身上最简单，也少一层间接寻址。

---

### `Source/RPG/Core/RPG_PlayerController.h` / `Source/RPG/Core/RPG_PlayerController.cpp`

**一句话职责**：**所有玩家输入的入口** —— 绑定增强输入、把输入翻译成"输入标签"、路由到 ASC 或角色的移动接口。

**类/结构体/命名空间**：`ARPG_PlayerController` —— 继承 `APlayerController`，无接口。头文件前置声明 `URPG_InputConfig`、`URPG_AbilitySystemComponent`、`ARPG_BaseCharacter`。

**职责边界**（头注释）：负责绑定增强输入、把输入翻译成"输入标签"、路由到 ASC；不负责具体动作怎么执行（→ 角色）、能力内部逻辑（→ GA）。

**两类输入的走法**（头注释）：

| 类别 | 链路 | 理由 |
| --- | --- | --- |
| 移动/视角/蹲伏 | 按键 → PC 回调 → `Character->Move()/Look()/ToggleCrouch()` | 没有冷却、没有消耗、不能被技能打断，走 GAS 是纯负担 |
| 能力类（攻击/闪避/跳跃/法术） | 按键 → PC 回调 → `ASC->TryActivateAbilityByInputTag(Tag)` → GA | 需要冷却、耐力消耗、前摇后摇、被状态标签阻断 |

**一个回调处理所有能力输入**（头注释）：增强输入的 `BindAction` 支持附加参数（`VarTypes...`），所以 `for (每条映射) EIC->BindAction(Mapping.InputAction, Started, this, &OnAbilityInputPressed, Mapping.InputTag);` —— 十几种能力输入共用同一个回调，靠绑定时传入的标签区分；新增技能时只改配置资产，本文件一行都不用动。

**成员变量**

| 成员 | 声明 | 用途 |
| --- | --- | --- |
| `InputConfig` | `UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG\|Input") TObjectPtr<URPG_InputConfig>`，protected | 输入配置资产，在蓝图子类 `BP_RPG_PlayerController` 里指定；换成另一套配置就能得到完全不同的操作方案，不需要改代码 |

#### `ARPG_PlayerController::ARPG_PlayerController()`

- **干什么**：**函数体为空**，只有一大段注释。
- **关键实现**：无代码。
- **为什么这么写**：注释是一份"不要踩这个坑"的警告 —— **绝对不要在这里写 `PrimaryActorTick.bCanEverTick = false;`**。曾经的理由是"增强输入是事件驱动的，不需要每帧处理"，但这个判断是**错的**：增强输入的 `Triggered`/`Started`/`Completed` 事件恰恰是在每帧的输入处理里评估 IMC 的 Trigger 才产生的，链路是
  ```
  APlayerController::TickActor()     ← 由 PrimaryActorTick 驱动
    └─ TickPlayerInput()
         └─ UPlayerInput::Tick()      ← 在这里评估所有 IMC 的 Trigger
              └─ 产生 Triggered / Started / Completed 事件
  ```
  把 `bCanEverTick` 设为 false 等于掐断整条输入管线，症状是**所有按键都没反应且不报任何错**，极难查。`AController` 构造函数里把它设成 true（注释标注 `Controller.cpp:62`）正是为了让这条链路能跑；子类保持默认即可。注释还补了一句：将来做"按住攻击键蓄力"这类需要累积时间的逻辑时用 GA 里的 AbilityTask 处理，即使那样 PlayerController 的 Tick 也不能关。
- **被谁调用 / 调用谁**：引擎实例化。

#### `void ARPG_PlayerController::BeginPlay()`

- **干什么**：注册输入映射上下文（IMC），并对配置缺失/配置为空做分级诊断。
- **关键实现**（分支顺序）：
  1. `Super::BeginPlay();`
  2. `if (!IsLocalController())` → `UE_LOG(LogRPG, Verbose, "[%s] 不是本机控制器（远程玩家），跳过输入初始化")` 后 `return;`。注释：联机下服务器上会存在代表远程玩家的 PC，它们没有 LocalPlayer（`GetLocalPlayer()` 返回 nullptr），在上面绑定输入、添加 IMC 没有意义；单机（Standalone）下 `IsLocalController()` 恒为 true，这个判断等于不存在，**不会**影响单人 PIE 迭代速度 —— 这是"保留单机自动降级"的体现。
  3. `if (!InputConfig)` → `UE_LOG(LogRPG, Error, "[%s] 没有配置 InputConfig！输入将完全失效。请在 BP_RPG_PlayerController 的 Details 面板里指定 DA_RPG_InputConfig")` 后 `return;`。
  4. `if (!InputConfig->DefaultMappingContext)` → `UE_LOG(LogRPG, Error, "...InputConfig 里没有指定 DefaultMappingContext")` 后 `return;`。
  5. `if (const ULocalPlayer* LP = GetLocalPlayer())` → `LP->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>()` → `Subsystem->AddMappingContext(InputConfig->DefaultMappingContext, InputConfig->MappingContextPriority);`，成功则 `UE_LOG(LogRPG, Log, "已添加输入映射上下文：%s（优先级 %d）")`。注释说明分工：**MappingContext 才是真正决定"哪个键触发哪个 InputAction"的地方**，InputConfig 只负责"哪个 InputAction 对应哪个输入标签（起映射作用）"。
  6. **诊断**：`const int32 MappingCount = InputConfig->DefaultMappingContext->GetMappings().Num();`；为 0 → `UE_LOG(LogRPG, Error, "IMC「%s」里一条按键映射都没有！请打开这个 IMC 资产，在 Mappings 数组里添加 W/A/S/D 等映射")`；否则 `UE_LOG(LogRPG, Log, "[%s] IMC「%s」中共有 %d 条按键映射")`。注释给出理由：**"IMC 添加成功"不等于"IMC 里有内容"**，一个空的 IMC 照样能添加成功但按什么键都不会有反应，少了这个检查两种情况日志长得一模一样。
- **为什么这么写**：见上各步注释。
- **被谁调用 / 调用谁**：引擎在 Actor 初始化阶段调用；调用 `Super::BeginPlay()`、`IsLocalController()`、`GetLocalPlayer()`、`UEnhancedInputLocalPlayerSubsystem::AddMappingContext()`、`UInputMappingContext::GetMappings()`。

#### `void ARPG_PlayerController::SetupInputComponent()`

- **干什么**：绑定全部输入：移动三件套 + 遍历 `InputConfig->AbilityInputMappings` 批量绑定能力输入。
- **关键实现**（分支顺序）：
  1. `Super::SetupInputComponent();`
  2. `UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(InputComponent);` 失败 → `UE_LOG(LogRPG, Error, "InputComponent 不是 UEnhancedInputComponent。请检查 Config/DefaultInput.ini 里的 DefaultInputComponentClass 是否指向 EnhancedInputComponent")` 后 `return;`
  3. `if (!InputConfig)` → `Error` 日志后 `return;`
  4. **Move**：`EIC->BindAction(InputConfig->MoveAction, ETriggerEvent::Triggered, this, &ARPG_PlayerController::OnMove);` + Log 打印绑定目标；随后校验 `InputConfig->MoveAction->ValueType != EInputActionValueType::Axis2D` 时打 Error（注释：配成 Digital 或 Axis1D 时 `Value.Get<FVector2D>()` 会**静默返回零向量** —— 按了键、代码也收到事件，但移动向量是 (0,0)，表现为"按键没反应"）。`MoveAction` 为空 → Error（"DA_RPG_InputConfig 的 Locomotion 分类下 Move Action 是空的 —— WASD 不会有任何反应"）。
  5. **Look**：同上，`Triggered` → `OnLook`，同样校验 `Axis2D`；为空 → Error（"鼠标转不了视角"）。
  6. **Crouch**：`ETriggerEvent::Started` → `OnCrouch`。注释：蹲伏用 `Started`（按下瞬间切换一次）而不是 `Triggered` —— `Triggered` 每帧都会触发，会导致蹲下立刻又站起来、反复横跳。为空 → `Warning`（不是 Error）。
  7. **能力输入循环**：`for (const FRPG_InputActionMapping& Mapping : InputConfig->AbilityInputMappings)`，先 `if (!Mapping.InputAction || !Mapping.InputTag.IsValid()) continue;`（注释：编辑资产时必然有"填了一半"的状态，这里静默跳过），然后 **两次绑定**：
     - `EIC->BindAction(Mapping.InputAction, ETriggerEvent::Started, this, &ARPG_PlayerController::OnAbilityInputPressed, Mapping.InputTag);`
     - `EIC->BindAction(Mapping.InputAction, ETriggerEvent::Completed, this, &ARPG_PlayerController::OnAbilityInputReleased, Mapping.InputTag);`（注释：`Completed` 用于"按住型"能力（重击蓄力松开释放），对瞬发技能是空操作、不会有害）
     - `++BoundCount;`
  8. `UE_LOG(LogRPG, Log, "[%s] 输入绑定完成：%d 个能力输入", ...)`。
  - 注释块还交代了**历史**：阶段 1 时 Jump/Sprint 还没有对应 GA，曾在这里做过"过渡期原生绑定"；阶段 3 已把 `GA_Jump`/`GA_Sprint` 做出来，那段临时代码已删除，现在它们和其他能力一样走能力循环；前提是它们要出现在 `DA_RPG_InputConfig` 的 Ability Input Mappings 里（`IA_RPG_Jump → Input.Jump`、`IA_RPG_Sprint → Input.Sprint`）。
- **为什么这么写**：每个分支都打印结果，注释解释："之前这里是静默的，导致 `InputConfig 里没填 Move Action` 和 `IMC 里没配按键` 这两种完全不同的故障，在日志上完全分辨不出来。" 失败属于"配置错误"而非"运行异常"，所以用 Error 级别并把修复方法直接写进日志 —— 这类问题靠猜很费时间。
- **被谁调用 / 调用谁**：引擎（`APlayerController::InitInputSystem` 等）调用；调用 `UEnhancedInputComponent::BindAction`、`UInputAction::GetName`、`UE_LOG`。注意本函数**没有** `IsLocalController()` 判断（该判断只在 `BeginPlay` 里），远程 PC 在服务器上也会走到这里。

#### `void ARPG_PlayerController::OnMove(const FInputActionValue& Value)`

- **干什么**：把移动输入转给角色。
- **关键实现**：`if (ARPG_BaseCharacter* RPGChar = GetRPGCharacter()) RPGChar->Move(Value);` —— 只有一次判空，无其他逻辑。
- **为什么这么写**：注释未说明（分工理由在头文件里：移动不经 GAS）。
- **被谁调用 / 调用谁**：`SetupInputComponent` 绑定；调用 `GetRPGCharacter()` 与 `ARPG_BaseCharacter::Move()`（后者内部自己处理 `IsAlive()` 门禁、控制器 Yaw 参考系、`AddMovementInput`）。

#### `void ARPG_PlayerController::OnLook(const FInputActionValue& Value)`

- **干什么**：把视角输入转给角色。
- **关键实现**：`if (ARPG_BaseCharacter* RPGChar = GetRPGCharacter()) RPGChar->Look(Value);`
- **为什么这么写**：注释未说明。
- **被谁调用 / 调用谁**：`SetupInputComponent` 绑定；调用 `ARPG_BaseCharacter::Look()`。

#### `void ARPG_PlayerController::OnCrouch()`

- **干什么**：切换蹲伏。
- **关键实现**：`if (ARPG_BaseCharacter* RPGChar = GetRPGCharacter()) RPGChar->ToggleCrouch();`
- **为什么这么写**：注释未说明（为何用 `Started` 触发的理由写在绑定处）。
- **被谁调用 / 调用谁**：`SetupInputComponent` 绑定（`Started`）；调用 `ARPG_BaseCharacter::ToggleCrouch()`。

#### `void ARPG_PlayerController::OnAbilityInputPressed(FGameplayTag InputTag)`

- **干什么**：能力输入的**统一入口**：先把意图推入输入缓存、必要时同步到服务器，再尝试激活能力。
- **关键实现**（逐步）：
  1. `UE_LOG(LogRPG_Ability, Log, "[%s] 收到能力输入：%s", *GetName(), *InputTag.ToString());` —— 注释说明这条日志是排查"按键没反应"的**第一个检查点**：能看到它说明按键、IMC、InputConfig 三层映射都是通的，问题在 GAS 侧；看不到说明三层里有一层断了（绝大多数情况是 IMC 里没配这个按键）。
  2. `URPG_AbilitySystemComponent* ASC = GetRPGAbilitySystemComponent();` 为空 → `UE_LOG(LogRPG_Ability, Warning, "拿不到 ASC，输入 %s 无法处理 —— 请检查 GameMode 的 PlayerStateClass 是否指向 RPG_PlayerState 的蓝图子类")` 后 `return;`（注释：拿不到 ASC 几乎总是因为 `PlayerStateClass` 没指向 `RPG_PlayerState` 的蓝图子类）。
  3. `if (ARPG_BaseCharacter* RPGChar = GetRPGCharacter())` 内：
     - **死亡门禁**：`if (!RPGChar->IsAlive()) return;` —— 注释给出完整理由：不做这道判断的话，死亡期间（3 秒重生倒计时里）每按一次键都会 ① 往缓存里塞一条永远没人取的输入 ② 让 GA 走一遍 `CanActivateAbility` → 被 `State.Dead` 阻断 → 基类打一条 Warning，表现是**死亡期间按键刷屏告警**。判断放在控制器而不是各个 GA 里的理由："控制器是玩家意图的入口，'死人没有意图'这件事应该在最靠前的地方被挡掉"；GA 侧的 `ActivationBlockedTags` 仍然保留 —— 那是给 AI 和其他非输入路径兜底的，两道防线管的不是同一件事。
     - `if (URPG_CombatComponent* Combat = RPGChar->GetCombatComponent())` 内：
       - `Combat->PushInputTag(InputTag);` —— 注释：这一步是连段衔接的前提，玩家在攻击动画期间按下的"下一段"不会被立即执行，而是留在缓存里等衔接窗口打开时由 GA 取走；如果只调 `TryActivateAbilityByInputTag` 而不推缓存，攻击进行中的按键会激活失败然后**被彻底丢弃**，结果就是"连按没有衔接，只能等上一段完全播完再按"。
       - **服务器同步**：`if (IsLocalController() && !HasAuthority()) { Server_PushInputTag(InputTag); }` —— 注释：服务器的输入缓存不会自己填上，它只在**按键那台机器**上被写；而连段推进（`TryStartNextSegment`）读的正是这个缓存，所以服务器那份 GA 永远连不上第二段、打完第 1 段就 `EndAbility`，再用 `ClientEndAbility` 把客户端正在播的动画**硬切**掉。只在"本地控制但又不是服务器"时发：主机自己已经在服务器上不用发，远端玩家的 PC 在这台机器上不会收到输入事件 —— 这个条件正好覆盖"纯客户端"这一种情况，不会重复推。
  4. **激活**：`ASC->TryActivateAbilityByInputTag(InputTag);` —— 注释：当前没有攻击在进行时这次激活会成功，GA 内部会消耗掉刚推入的那条输入；正在攻击中则激活失败（无害），输入留在缓存里等衔接窗口来取。这里**不做**任何"能不能放"的前置判断 —— 那是 GA 的 `CanActivateAbility` 和 GE 的标签阻断该管的事，控制器只负责把意图送达。
- **为什么这么写**：见上（每条注释都已在对应步骤转述）。
- **被谁调用 / 调用谁**：`SetupInputComponent` 里按 `AbilityInputMappings` 逐条绑定（`Started`）；调用 `GetRPGCharacter()`、`ARPG_BaseCharacter::IsAlive()`、`ARPG_BaseCharacter::GetCombatComponent()`、`URPG_CombatComponent::PushInputTag()`、`Server_PushInputTag()`、`URPG_AbilitySystemComponent::TryActivateAbilityByInputTag()`。

#### `void ARPG_PlayerController::Server_PushInputTag_Implementation(FGameplayTag InputTag)`

（声明：`UFUNCTION(Server, Reliable) void Server_PushInputTag(FGameplayTag InputTag);`）

- **干什么**：服务器端把客户端的输入意图推进**这台机器上那份**缓存，好让服务器侧的连段推进和客户端走同样的逻辑。
- **关键实现**（两道门 + 一次推送）：
  - **门 ①：标签白名单** —— `if (!GetRPGAbilitySystemComponent() || !GetRPGAbilitySystemComponent()->HasAbilityForInputTag(InputTag))` → `UE_LOG(LogRPG_Ability, VeryVerbose, "[%s] 服务器丢弃未登记的输入标签：%s")` 后 `return;`。注释：服务器会照单全收任意 `GameplayTag`，不在映射表里的标签推进去也没用（连段的消费只认自己那几个），但会**白占一个缓存槽**（注释注明缓存容量 4，满了挤掉最旧的），把合法的连段输入挤出去。
  - **门 ②：存活判断** —— `if (ARPG_BaseCharacter* RPGChar = GetRPGCharacter())` 内先 `if (!RPGChar->IsAlive()) return;`。注释：客户端在 `OnAbilityInputPressed` 里已经判过 `IsAlive`，但那是**在客户端**，服务器不能依赖客户端的判断；不判的话改过的客户端可以在死亡期间往服务器缓存里塞条目（危害有限，重生时 `ClearInputBuffer` 会兜住，但"死人没有意图"这条规则在服务器侧不该是缺失的）。
  - **推送**：`Combat->PushInputTag(InputTag);` 然后 `UE_LOG(LogRPG_Ability, VeryVerbose, "[%s] 服务器收到客户端输入：%s（缓存现有 %d 条）", ..., Combat->GetBufferedInputCount());` —— 注释：走的是和本地按键完全相同的入口 `PushInputTag`，和项目"AI 和玩家共用一条输入链路"的约定一致，服务器不需要知道这条输入是玩家按的还是 RPC 送来的。
- **为什么这么写**：
  - 头文件里的长注释解释了**为什么必须有这个 RPC**：`URPG_CombatComponent::InputBuffer` 是**本机运行时对象**，从来不复制（`NewObject` 建的，组件也没调 `SetIsReplicated`），也就是说只有按键那台机器的缓存里有东西；而连段推进靠的正是这个缓存（`TryStartNextSegment()` → `Combat->ConsumeInputTag(...)`），于是服务器上那份轻击 GA 永远连不上第二段 —— 衔接窗口开的时候缓存是空的；第 1 段一结束服务器就 `EndAbility`，再复制给客户端：`ClientEndAbility → EndAbility → StopCurrentSegmentMontage() → Montage_Stop(0.f)`（**零混合时间的硬切**）。表现是"客户端出招一顿一顿的、连段打不全"，而**主机完全正常** —— 因为引擎只在 `!IsLocallyControlled()` 时才发 `ClientEndAbility`，主机自己控制的 Pawn 收不到这条 RPC。修法就是让服务器的缓存和客户端保持一致。
  - **为什么用 `Reliable` 而不是 `Unreliable`**：丢一条输入就是"这一段被吞了"，表现是连段断掉、玩家感觉按键失灵；输入事件频率很低（人手按键），Reliable 的代价可以忽略。
  - **服务器侧两道门的定性**（实现文件注释）：先澄清它**不是**什么 —— 它不需要额外的"权威判断"，`UNetDriver::ShouldCallRemoteFunction` 保证只有 Owner 能对自己的 Actor 发 Server RPC，加上 `APlayerController` 的 `bOnlyRelevantToOwner`，别的客户端根本看不到你也调不到你；两道门防的是"**改过的客户端**"，不是"别的玩家"。注释还特别强调："也别把这里说成安全防线。真正的安全来自'服务器有权重算一切'（命中判定、伤害数值、状态变更全在服务器算）。这里做的只是'别让明显的垃圾进缓存'。"
  - 实现里**不调** `TryActivateAbilityByInputTag` 的理由（注释）：服务器侧的能力激活由 `ServerTryActivateAbility`（GA 的 `LocalPredicted` 机制）自己负责，那条路已经通了；这里重复激活会让服务器上出现两份激活。这个 RPC 只解决"缓存里有没有货"这一件事。
- **被谁调用 / 调用谁**：由客户端侧 `OnAbilityInputPressed` 在 `IsLocalController() && !HasAuthority()` 时调用；内部调用 `GetRPGAbilitySystemComponent()`、`URPG_AbilitySystemComponent::HasAbilityForInputTag()`、`GetRPGCharacter()`、`ARPG_BaseCharacter::IsAlive()`、`GetCombatComponent()`、`URPG_CombatComponent::PushInputTag()`、`URPG_CombatComponent::GetBufferedInputCount()`。

#### `void ARPG_PlayerController::OnAbilityInputReleased(FGameplayTag InputTag)`

- **干什么**：把"松手"事件转给 ASC，由 ASC 找到对应能力实例并调用其 `OnInputReleased()`。
- **关键实现**：先 `UE_LOG(LogRPG_Ability, Verbose, "[%s] 输入释放：%s")`，再 `if (URPG_AbilitySystemComponent* ASC = GetRPGAbilitySystemComponent()) ASC->NotifyInputReleased(InputTag);` —— 没有别的分支。
- **为什么这么写**：注释说明 —— 交给 ASC 去找"这个输入标签对应的、当前正在激活的能力"，然后调用它的 `OnInputReleased()`；瞬发能力对这个调用无感（基类默认空实现），按住型能力（重击蓄力）靠它知道玩家松手了。（ASC 侧实现见 `RPG_AbilitySystemComponent.cpp:538-568`：查 `InputTagToSpecHandle` → `FindAbilitySpecFromHandle` → `GetPrimaryInstance()` → `Cast<URPG_GameplayAbilityBase>` 后调 `OnInputReleased()`；注释明确"我们所有 GA 都是 InstancedPerActor"，NonInstanced 拿不到实例会直接返回。）
- **被谁调用 / 调用谁**：`SetupInputComponent` 按映射绑定（`Completed`）；调用 `URPG_AbilitySystemComponent::NotifyInputReleased()`。

#### `ARPG_BaseCharacter* ARPG_PlayerController::GetRPGCharacter() const`（private）

- **干什么**：取当前控制的 RPG 角色，失败返回 `nullptr`。
- **关键实现**：`return Cast<ARPG_BaseCharacter>(GetCharacter());`
- **为什么这么写**：注释说明两点 —— ① `AController` 已经维护了一个 Character 成员，用 `GetCharacter()` 访问，它返回"当前控制的 `ACharacter`"，控制的不是角色时返回 `nullptr`，我们只是把它转成本项目类型，不需要自己再存一份；② ⚠️ 正因为 `AController` 有这个成员，在 Controller 的成员函数里**不要**用 `Character` 当局部变量名 —— 会遮蔽它，而且 UE 把 C4458（变量遮蔽类成员）当作编译错误而不是警告。
- **被谁调用 / 调用谁**：`OnMove`、`OnLook`、`OnCrouch`、`OnAbilityInputPressed`、`Server_PushInputTag_Implementation`、`GetRPGAbilitySystemComponent`、`RPGPrintAttributes`。

#### `URPG_AbilitySystemComponent* ARPG_PlayerController::GetRPGAbilitySystemComponent() const`（private）

- **干什么**：取玩家的项目扩展 ASC，失败返回 `nullptr`。
- **关键实现**：先 `const ARPG_BaseCharacter* RPGChar = GetRPGCharacter();` 判空返回 `nullptr`；再 `return Cast<URPG_AbilitySystemComponent>(RPGChar->GetAbilitySystemComponent());`
- **为什么这么写**：注释 —— 角色的 ASC 可能在它自己身上（敌人），也可能在 PlayerState 上（玩家），**走接口拿，不要在这里假设** —— 这正是接口存在的意义。
- **被谁调用 / 调用谁**：`OnAbilityInputPressed`、`Server_PushInputTag_Implementation`、`OnAbilityInputReleased`、`RPGPrintTags`；调用 `ARPG_BaseCharacter::GetAbilitySystemComponent()`。

#### `void ARPG_PlayerController::DebugPrint(const FString& Message, const FColor& Color = FColor::White) const`（private）

- **干什么**：双通道输出调试信息（屏幕 + Output Log）。
- **关键实现**：`if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 5.f, Color, Message);` 然后 `UE_LOG(LogRPG, Log, TEXT("%s"), *Message);` —— 屏幕消息 key 用 `-1`（不覆盖、自动分配），显示时长 5 秒。
- **为什么这么写**：注释：屏幕上一份（PIE 时不用切窗口），Output Log 一份（能回溯）。
- **被谁调用 / 调用谁**：只被 `RPGPrintAttributes`、`RPGPrintTags` 调用；调用 `GEngine->AddOnScreenDebugMessage` 与 `UE_LOG`。

#### `void ARPG_PlayerController::RPGPrintAttributes()`（`UFUNCTION(Exec)`）

- **干什么**：控制台命令 `RPGPrintAttributes`，打印玩家全部属性。
- **关键实现**：`GetRPGCharacter()` → `RPGChar->GetRPGAttributeSet()`；取不到属性集就 `DebugPrint("⚠ 拿不到属性集 —— ASC 可能尚未初始化，或角色的 GetASCInternal() 没有正确实现", FColor::Red)` 并返回；否则依次 `DebugPrint` 标题（Cyan）与六行：`生命 %6.1f / %.1f`（`GetHealth()`/`GetMaxHealth()`）、`攻击 %6.1f`（`GetAttack()`）、`防御 %6.1f`（`GetDefense()`）、`法力 %6.1f / %.1f`（`GetMana()`/`GetMaxMana()`）、`耐力 %6.1f / %.1f`（`GetStamina()`/`GetMaxStamina()`），最后再打一行分隔线。
- 说明：`Attributes` 是 `const URPG_AttributeSet*`，这些 getter 由 `ATTRIBUTE_ACCESSORS_BASIC` 宏生成（见 `RPG_AttributeSet.h:121-166`）。
- **为什么这么写**：头注释 —— 用控制台命令而不是绑定按键，因为它不需要任何输入资产就能用，也不占用输入映射。
- **被谁调用 / 调用谁**：引擎的 Exec 机制（游戏内控制台 `~` 输入命令）；调用 `DebugPrint`、`URPG_AttributeSet` 的各 getter。

#### `void ARPG_PlayerController::RPGPrintTags()`（`UFUNCTION(Exec)`）

- **干什么**：控制台命令 `RPGPrintTags`，打印玩家当前拥有的全部 GameplayTag。
- **关键实现**：`UAbilitySystemComponent* ASC = GetRPGAbilitySystemComponent();` 为空 → `DebugPrint("⚠ 拿不到 ASC", FColor::Red)` 返回；`FGameplayTagContainer OwnedTags; ASC->GetOwnedGameplayTags(OwnedTags);`；`OwnedTags.IsEmpty()` → `DebugPrint("当前没有任何 GameplayTag", FColor::Yellow)` 返回；否则打标题 `════ 当前标签（%d 个）════`（Cyan）后 `for (const FGameplayTag& Tag : OwnedTags) DebugPrint("  %s", *Tag.ToString());`
- **为什么这么写**：同 `RPGPrintAttributes`（头注释统一说明）。
- **被谁调用 / 调用谁**：控制台 Exec；调用 `GetRPGAbilitySystemComponent`、`UAbilitySystemComponent::GetOwnedGameplayTags`、`DebugPrint`。

#### ⚠️ 头文件中声明但无实现的函数

| 声明 | 位置 | 实际情况 |
| --- | --- | --- |
| `void OnSprintStarted();` | `RPG_PlayerController.h:87` | 全库检索只有这一行，**.cpp 无定义、无任何 `BindAction` 绑定点** —— 死声明 |
| `void OnSprintCompleted();` | `RPG_PlayerController.h:88` | 同上 |

`SetupInputComponent` 的注释交代了背景：阶段 1 曾为 Jump/Sprint 做过"过渡期原生绑定"，阶段 3 做出 `GA_Jump`/`GA_Sprint` 后那段代码已删除 —— 但**注释没有说明这两个残留声明是否是那次删除的遗留**（推断如此，未核实）；保留它们不会造成行为影响（无绑定即不会被调用），但属于可清理项。

---

### `Source/RPG/Input/RPG_InputConfig.h` / `Source/RPG/Input/RPG_InputConfig.cpp`

**一句话职责**：把"哪个按键/输入动作对应哪个输入标签"从代码里挪到数据资产，`PlayerController` 只按资产配置绑定。

**类/结构体/命名空间**：

- `FRPG_InputActionMapping` —— `USTRUCT(BlueprintType)`，一条"输入动作 → 输入标签"的映射。
- `URPG_InputConfig` —— 继承 `UPrimaryDataAsset`，`UCLASS(BlueprintType)`。

**为什么要有这个资产**（头注释）：换一套输入方案（键鼠/手柄/触屏）只需换一个 DataAsset；调试时可以运行时替换配置，不用重新编译；输入映射关系一目了然，不用在代码里翻 `BindAction` 调用。

**两类输入的分工**（头注释）：

- **Locomotion（移动/视角/蹲伏）** —— 直接驱动 `CharacterMovementComponent`，不经过 GAS。这类输入没有冷却、没有消耗、不需要被其他技能打断，走 GAS 是纯粹的负担。
- **Ability（攻击/闪避/跳跃/奔跑/法术）** —— 全部通过输入标签进入 GAS。因为跳跃和奔跑要消耗耐力，攻击要有冷却和前摇后摇，闪避要有无敌帧 —— 它们都是"能力"。

#### `FRPG_InputActionMapping` 的成员

| 成员 | 声明 | 用途 |
| --- | --- | --- |
| `InputAction` | `UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG\|Input") TObjectPtr<UInputAction> = nullptr` | 增强输入的输入动作资产 |
| `InputTag` | `UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG\|Input", meta = (Categories = "Input")) FGameplayTag` | 该动作对应的输入标签，必须在 `Input.*` 命名空间下（`meta` 限制了编辑器里的标签选择范围） |
| `Description` | `UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG\|Input") FString` | 用于日志与调试的可读说明 |

结构体头注释说明**映射方向**：是 `Action → Tag`，不是 `Tag → Action`。因为同一个动作可能在不同情境下代表不同意图（比如长按/短按），而每个动作资产是唯一的配置点，在它这里指定标签最自然。

#### `URPG_InputConfig` 的成员

| 成员 | 声明 | 用途 |
| --- | --- | --- |
| `DefaultMappingContext` | `EditDefaultsOnly, BlueprintReadOnly, "RPG\|Input"`，`TObjectPtr<UInputMappingContext> = nullptr` | 默认 IMC。`PlayerController` 在 `BeginPlay` 时把它加进 EnhancedInput 子系统 |
| `MappingContextPriority` | `int32 = 0` | 映射上下文优先级。数值高的会覆盖低的（注释举例：用于"战斗模式"覆盖"探索模式"这类场景） |
| `MoveAction` | `TObjectPtr<UInputAction>`，分类 `RPG\|Input\|Locomotion` | 直接驱动移动的输入（不走 GAS） |
| `LookAction` | 同上 | 视角输入 |
| `CrouchAction` | 同上 | 蹲伏输入 |
| `AbilityInputMappings` | `TArray<FRPG_InputActionMapping>`，分类 `RPG\|Input\|Abilities`，`meta = (TitleProperty = "Description")` | 攻击、闪避、跳跃、奔跑、法术等，全部通过输入标签进入 GAS；`TitleProperty` 让数组条目在编辑器里显示 `Description` 而不是默认字段 |

#### `bool URPG_InputConfig::FindInputTagForAction(const UInputAction* Action, FGameplayTag& OutTag) const`

- **干什么**：由 `InputAction` 反查输入标签，找不到返回 `false`。
- **关键实现**：`if (!Action) return false;` → `for (const FRPG_InputActionMapping& Mapping : AbilityInputMappings)` 内 `if (Mapping.InputAction == Action && Mapping.InputTag.IsValid()) { OutTag = Mapping.InputTag; return true; }` → 循环走完 `return false;`
- **为什么这么写**：注释 —— 用**指针相等**判断，而不是名字比较：`InputAction` 是资产，指针就是它的身份；名字比较会在有同名资产时出错，而且慢。同时要求 `InputTag.IsValid()` 才算命中（半配置的条目不算数）。
- **被谁调用 / 调用谁**：**全库检索未发现调用点**（`Source/` 下仅命中声明与定义本身；它也不是 `UFUNCTION`，蓝图不可调用）—— 当前是未被使用的公开 API。

#### `UInputAction* URPG_InputConfig::FindActionForInputTag(FGameplayTag InputTag) const`

- **干什么**：由输入标签反查 `InputAction`，找不到返回 `nullptr`。
- **关键实现**：`if (!InputTag.IsValid()) return nullptr;` → 循环内 `if (Mapping.InputTag == InputTag) return Mapping.InputAction;` → `return nullptr;`（注意：这里没有校验 `Mapping.InputAction` 是否为空，可能返回 `nullptr`，与"找不到"同义）
- **为什么这么写**：注释未说明。
- **被谁调用 / 调用谁**：**全库检索未发现调用点**（同上）。

#### `void URPG_InputConfig::GetAllAbilityActions(TArray<const UInputAction*>& OutActions) const`

- **干什么**：收集所有能力输入动作，供 `PlayerController` 批量绑定。
- **关键实现**：`OutActions.Reset();` → 遍历 `AbilityInputMappings`，`if (Mapping.InputAction && Mapping.InputTag.IsValid()) OutActions.Add(Mapping.InputAction);`
- **为什么这么写**：注释 —— 只收集"动作和标签都配好了"的条目；配了一半的条目是配置错误，但在这里**静默跳过而不是报错**，因为编辑资产的过程中必然会经过"填了一半"的状态，报错会刷屏。
- **被谁调用 / 调用谁**：注释说"供 `PlayerController` 批量绑定"，但**当前 `ARPG_PlayerController::SetupInputComponent` 是直接遍历 `InputConfig->AbilityInputMappings` 的**，没有调用本函数（全库检索未发现调用点）—— 属于与现有实现重叠的备用 API。

---

### `Source/RPG/Interfaces/RPG_AbilitySystemInterface.h`

**一句话职责**：在引擎 `IAbilitySystemInterface` 之外，补充游戏层需要的查询（属性集、是否存活），并以此**封装"ASC 挂在哪"这个差异**。

（本文件**没有对应的 .cpp** —— 接口全部是纯虚函数，`UINTERFACE` 由 UHT 生成反射代码，无需实现文件。）

**类/结构体/命名空间**：

- `URPG_AbilitySystemInterface` —— `UINTERFACE(MinimalAPI, BlueprintType)` 的反射壳，继承 `UInterface`，`GENERATED_BODY()`，空体。
- `IRPG_AbilitySystemInterface` —— 实际接口，`class RPG_API`，`GENERATED_BODY()`，含 2 个纯虚函数。

#### `virtual URPG_AttributeSet* GetRPGAttributeSet() const = 0;`

- **干什么**：获取 RPG 属性集。
- **关键实现**：纯虚，由实现者决定取法。头注释写明各自的责任：玩家角色 → 转发到 PlayerState 的 `AttributeSet`；敌人 → 返回自己的 `AttributeSet`。
- **为什么这么写**：头注释标题即"实现者负责处理'ASC 可能不在自己身上'的情况"；返回裸指针、**可能为 `nullptr`（ASC 尚未初始化时）**，调用方必须判空。
- **被谁调用 / 调用谁**：实现者 —— `ARPG_BaseCharacter`（`Source/RPG/Character/RPG_BaseCharacter.cpp:320-332`，内部 `GetASCInternal()` + `ASC->GetSet<URPG_AttributeSet>()`）与 `ARPG_PlayerState`（`RPG_PlayerState.cpp:58-63`）。

#### `virtual bool IsAlive() const = 0;`

- **干什么**：是否存活。
- **关键实现**：纯虚。头注释明确判据是 **`State.Dead` 标签是否存在，而不是 Health 数值** —— 因为标签是"状态"的唯一真相源，而 Health 可能被各种 Buff 短暂改写。
- **为什么这么写**：见上（注释给出的理由即为设计理由）。头注释还列出调用场景：AI 选目标、伤害前置检查、UI 刷新都用它。
- **被谁调用 / 调用谁**：实现者 —— `ARPG_BaseCharacter::IsAlive()`（ASC 为空时返回 `true`，否则 `!HasMatchingGameplayTag(State_Dead)`）、`ARPG_PlayerState::IsAlive()`（同款逻辑、直接查 `AbilitySystemComponent`）。调用方之一：`URPG_AbilitySystemLibrary::IsAlive()` 用 `Cast<IRPG_AbilitySystemInterface>` 优先走接口。

**头文件长注释：为什么还需要这个接口**（转述）

- `IAbilitySystemInterface`（引擎）：只有一个 `GetAbilitySystemComponent()`；GAS 内部机制依赖它（`UAbilitySystemGlobals::GetAbilitySystemComponentFromActor` 会 Cast 它），所以**必须实现**，不实现的话 GAS 根本找不到你的 ASC。
- `IRPG_AbilitySystemInterface`（本接口）：只补充游戏层需要的查询（属性集、是否存活等），**不重复提供 `GetAbilitySystemComponent()`** —— 那是引擎接口的职责。
- "ASC 挂在哪是一个必须被封装掉的差异"：玩家在 `RPG_PlayerState` 上、敌人挂自己身上；战斗代码（比如"给目标上一个减防 Debuff"）不该关心这个差异 —— 那是实现者的责任，接口就是这层封装。

---

### `Source/RPG/AbilitySystem/RPG_AbilitySystemLibrary.h` / `Source/RPG/AbilitySystem/RPG_AbilitySystemLibrary.cpp`

**一句话职责**：GAS 相关的静态查询工具库，给 C++ 和蓝图提供"不用关心 ASC 挂在哪"的统一入口。

**类/结构体/命名空间**：`URPG_AbilitySystemLibrary` —— 继承 `UBlueprintFunctionLibrary`，4 个 `static` 函数，全部 `UFUNCTION(BlueprintPure, Category = "RPG|AbilitySystem", meta = (DefaultToSelf = "Actor"))`（`DefaultToSelf` 让蓝图里调用时 `Actor` 引脚自动接 `self`）。

**为什么需要它**（头注释）：

- 引擎的库提供了 `GetAbilitySystemComponent(AActor*)` —— 本项目**直接用它的，不重复造**。本库只补两件引擎没有的事：
  1. `GetRPGAttributeSet()`：引擎有 `UAbilitySystemComponent::GetSetOnActor<T>()`，但那是 C++ 模板函数、**无法暴露给蓝图**（`UFUNCTION` 不支持模板）；想在蓝图里拿属性集读血条就需要这个包装。
  2. `IsAlive()` / `HasGameplayTag()`：游戏层语义的便捷查询，避免每个调用点都写一遍"取 ASC → 判空 → 查标签"三件套。
- **它解决的真正痛点**：战斗代码里最烦的是"我想给目标上 Debuff，但它的 ASC 可能在 PlayerState 上，也可能在自己身上"。如果每个调用点都写 `Cast<ARPG_Player>`/`Cast<ARPG_Enemy>` 分支，那就是灾难；这里收口一次，调用方永远不用关心 ASC 挂在哪。

**实现文件顶部的统一说明（关于 `const_cast`）**：本文件多处出现 `const_cast<AActor*>`，原因是引擎的 `UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(AActor*)` 与 `UAbilitySystemComponent::GetSetOnActor<T>(AActor*)` 的参数都不是 const 指针；代码只是查询、绝不修改 Actor，所以转换是安全的；对外用 const 参数是为了让 C++ 调用方传 const 指针也能编译。

#### `static URPG_AttributeSet* URPG_AbilitySystemLibrary::GetRPGAttributeSet(const AActor* Actor)`

- **干什么**：获取任意 Actor 的 RPG 属性集，取不到返回 `nullptr`。
- **关键实现**：`if (!Actor) return nullptr;` → `return const_cast<URPG_AttributeSet*>(UAbilitySystemComponent::GetSetOnActor<URPG_AttributeSet>(const_cast<AActor*>(Actor)));`（一次空判 + 两层 const_cast，无其他逻辑）
- **为什么这么写**：头注释 —— 内部转发给引擎的 `GetSetOnActor`，它会通过 `IAbilitySystemInterface` 找到 ASC，因此玩家（ASC 在 PlayerState）和敌人（ASC 在自己身上）都能正确处理 —— **前提是角色的接口实现写对了**。
- **被谁调用 / 调用谁**：`Source/` 下未检索到 C++ 调用点（`UFUNCTION(BlueprintPure)` 说明设计意图是给蓝图用；本次对 `Content/` 的文本检索（含 `AbilitySystemLibrary`、`GetRPGAttributeSet` 关键字）没有命中，仅供参考 —— 二进制资产检索未核实）。内部调用 `UAbilitySystemComponent::GetSetOnActor<URPG_AttributeSet>`。

#### `static bool URPG_AbilitySystemLibrary::IsAlive(const AActor* Actor)`

- **干什么**：判断角色是否存活（以 `State.Dead` 标签为判据，不是看血量数值）。
- **关键实现**（两条路径）：
  1. `if (!Actor) return false;`
  2. `if (const IRPG_AbilitySystemInterface* RPGInterface = Cast<IRPG_AbilitySystemInterface>(const_cast<AActor*>(Actor))) return RPGInterface->IsAlive();`
  3. 兜底：`return !HasGameplayTag(Actor, RPGTags::State_Dead);`
- **为什么这么写**：注释 —— 优先走 RPG 接口，因为**实现者可能重写了更严格的判定**（注释举的例子："无敌帧中"不算可攻击状态，但仍算存活）；没实现接口的 Actor（可破坏物、召唤物…）退回标签判定。
- **被谁调用 / 调用谁**：`Source/` 下未检索到 C++ 调用点（供蓝图）；内部调用 `Cast<IRPG_AbilitySystemInterface>`、`HasGameplayTag`（本类自身）、`RPGTags::State_Dead`。

#### `static bool URPG_AbilitySystemLibrary::HasGameplayTag(const AActor* Actor, FGameplayTag Tag)`

- **干什么**：Actor 是否拥有指定标签；内部处理 ASC 为空的情况。
- **关键实现**：`if (!Actor || !Tag.IsValid()) return false;` → `const UAbilitySystemComponent* ASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(const_cast<AActor*>(Actor));` → `return ASC && ASC->HasMatchingGameplayTag(Tag);`
- **为什么这么写**：注释未额外说明（保持"判空 + 短路"的安全语义：Actor 无效或标签无效都返回 false，ASC 为空返回 false）。
- **被谁调用 / 调用谁**：被本类 `IsAlive` 调用（兜底路径）；`Source/` 下无其他 C++ 调用点。调用引擎的 `UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent` 与 `UAbilitySystemComponent::HasMatchingGameplayTag`。

#### `static float URPG_AbilitySystemLibrary::GetAttributeValue(const AActor* Actor, FGameplayAttribute Attribute)`

- **干什么**：取 Actor 当前的某个属性值（原始 float）。
- **关键实现**：`if (!Attribute.IsValid()) return 0.f;` → `const UAbilitySystemComponent* ASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(const_cast<AActor*>(Actor));` → `return ASC ? ASC->GetNumericAttribute(Attribute) : 0.f;`
- **为什么这么写**：头注释 —— 蓝图里读属性要经过 `FGameplayAttributeData`，比较啰嗦，这里包一层。（注意本函数**不判 Actor 是否为空**，直接把它交给引擎函数；`GetAbilitySystemComponent(nullptr)` 返回 `nullptr`，最终走 `0.f` 分支 —— 结果安全，但这是实现细节推断，注释未说明。）
- **被谁调用 / 调用谁**：`Source/` 下未检索到 C++ 调用点（供蓝图）；调用 `UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent`、`UAbilitySystemComponent::GetNumericAttribute`。

---

## 本章交叉索引（谁依赖谁）

| 关系 | 具体 |
| --- | --- |
| `ARPG_GameModeBase` → 其余三者 | 构造函数指定 `DefaultPawnClass=ARPG_Player`、`PlayerStateClass=ARPG_PlayerState`、`PlayerControllerClass=ARPG_PlayerController`、`HUDClass=ARPG_HUD`。**`PlayerStateClass` 配错 → 玩家 ASC 永远拿不到，且不报错** |
| `ARPG_PlayerState` → ASC / AttributeSet | 创建 `URPG_AbilitySystemComponent` + `URPG_AttributeSet`，`AddSpawnedAttribute` 登记，`SetNetUpdateFrequency(30)` 保证蒙太奇同步 |
| `ARPG_Player` → `ARPG_PlayerState` | `ARPG_Player::GetASCInternal()` 取 PlayerState 的 ASC；`InitializeAbilitySystem()` 用 `InitAbilityActorInfo(PS, this)` 建立 Owner/Avatar 关联（Owner=PlayerState、Avatar=角色） |
| `ARPG_PlayerController` → `URPG_InputConfig` | `BeginPlay` 读 `DefaultMappingContext`/`MappingContextPriority`；`SetupInputComponent` 读 `MoveAction`/`LookAction`/`CrouchAction`/`AbilityInputMappings` |
| `ARPG_PlayerController` → ASC / CombatComponent | 能力输入：`Combat->PushInputTag()` → `ASC->TryActivateAbilityByInputTag()`；松手：`ASC->NotifyInputReleased()`；联机：`Server_PushInputTag` 在服务器重复 `PushInputTag` |
| `RPGTags` → 全项目 | 89 处 `RPGTags::` 引用、24 个文件；`State_Dead` 是 `IsAlive` 系列的判据，`Ability.Attack` 是受击打断的取消组父标签 |
| `IRPG_AbilitySystemInterface` ← 实现者 | `ARPG_BaseCharacter`（玩家/敌人共用）、`ARPG_PlayerState`；消费者 `URPG_AbilitySystemLibrary::IsAlive` |
| `URPG_AbilitySystemLibrary` | 全部为 `BlueprintPure` 静态函数，`Source/` 下无 C++ 调用点；是"给蓝图用 + 收口 ASC 位置差异"的预留工具层 |

---

# 二、Character / Animation

本章覆盖角色层（玩家 / 敌人的共同基类与两个子类）、动画实例基类、动画公共类型，以及 4 个 AnimNotify / AnimNotifyState 桥接类。

阅读本章前建议先建立这条主线：

```
ARPG_BaseCharacter（ACharacter + IAbilitySystemInterface + IRPG_AbilitySystemInterface）
├── ARPG_Player   →  ASC 在 ARPG_PlayerState 上（GetASCInternal 去 PlayerState 取）
└── ARPG_Enemy    →  ASC 在自己身上（GetASCInternal 返回自己的成员）

URPG_AnimInstanceBase（UAnimInstance）
  每帧：UpdateCombatState()（读 GameplayTag）→ UpdateLocomotion()（读 CharacterMovement）
  输出：一组 BlueprintReadOnly 的布尔/浮点，交给 ABP_RPG_Base 的 AnimGraph 连线

AnimNotify / AnimNotifyState（表现层 → 逻辑层的单向桥）
  Notify 只做一件事：SendGameplayEventToActor 广播一个 GameplayTag 事件，
  由 GA 侧用 UAbilityTask_WaitGameplayEvent 监听并响应。
```

---

### `Source/RPG/Character/RPG_BaseCharacter.h` / `RPG_BaseCharacter.cpp`

**一句话职责**：玩家与敌人的共同基类 —— 负责相机与弹簧臂、移动参数、两个接口的唯一默认实现、"角色能做的动作"（Move/Look/Sprint/Crouch）、布娃娃与重生流程、头顶血条可见性与伤害飘字广播。

**类/结构体**：

- `ARPG_BaseCharacter` —— 继承 `ACharacter` + `IAbilitySystemInterface` + `IRPG_AbilitySystemInterface`，声明为 `UCLASS(Abstract)`，是 `ARPG_Player` 与 `ARPG_Enemy` 的共同父类。

头文件开头的职责边界注释（原文转述）：

- **负责**：相机与弹簧臂、移动参数、接口实现、"角色能做的动作"（Move/Look/Sprint/Crouch）
- **不负责**：输入绑定 → `RPG_PlayerController`（玩家输入一律走 PC，这是项目硬性约定）；ASC 的创建 → `RPG_PlayerState`（玩家）/ `RPG_Enemy`（敌人）；能力的具体逻辑 → 各个 GA
- **为什么基类实现接口而 ASC 由子类提供**：接口要求"能拿到 ASC"，但 ASC 在哪对玩家和敌人不一样（玩家 → PlayerState 上，敌人 → 自己身上，可能还会有更多角色类型）。与其让两个子类各写一遍接口实现，不如基类实现接口、把"取 ASC"抽成虚函数 `GetASCInternal()`，子类只实现这一个函数。收益：接口实现只有一份，行为绝对一致；新增角色类型（比如召唤物）只需实现 `GetASCInternal()`；`IsAlive` / `GetRPGAttributeSet` 这类派生查询自动可用。

#### `ARPG_BaseCharacter::ARPG_BaseCharacter()`

- **干什么**：建全部默认子对象（弹簧臂、相机、战斗组件、头顶血条）并设置朝向策略与移动参数。
- **关键实现**：
  - **相机**：`CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"))`，`SetupAttachment(RootComponent)`，`TargetArmLength = CameraBoomLength`（默认 400），`SetRelativeRotation(CameraBoomOffset)`（默认 `FRotator(-10.f, 0.f, 0.f)`），`bUsePawnControlRotation = true`，`bEnableCameraLag = true`，`CameraLagSpeed = CameraLagSpeed`。
  - **相机**：`FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"))`，`SetupAttachment(CameraBoom, USpringArmComponent::SocketName)`，`bUsePawnControlRotation = false`。
  - **朝向策略**：`bUseControllerRotationPitch / Yaw / Roll` 全部置 `false`。
  - **移动**：取 `GetCharacterMovement()` 后设 `bOrientRotationToMovement = true`、`RotationRate = FRotator(0.f, RotationRateYaw, 0.f)`（默认 540）、`MaxWalkSpeed = WalkSpeed`（300）、`MaxWalkSpeedCrouched = CrouchSpeed`（180）、`JumpZVelocity = 600.f`、`AirControl = 0.35f`；并设 `Movement->GetNavAgentPropertiesRef().bCanCrouch = true`。
  - **战斗组件**：`CombatComponent = CreateDefaultSubobject<URPG_CombatComponent>(TEXT("CombatComponent"))`。
  - **头顶血条**：`OverheadHealthBar = CreateDefaultSubobject<URPG_OverheadHealthBarComponent>(TEXT("OverheadHealthBar"))`，`SetupAttachment(RootComponent)`，`SetRelativeLocation(FVector(0.f, 0.f, 110.f))`，`CastShadow = false`。
  - 构造函数**不设置**组件的碰撞 / 绘制尺寸 / 可见性 —— 注释说明那些是"这个组件是什么"的属性，在组件自己的构造函数里设，不该散落在每个使用者的构造函数里。
- **为什么这么写**（注释原文要点）：
  - `bUsePawnControlRotation = true` 是"鼠标控制视角"的关键；关掉它相机就只会跟着角色身体转，变成固定追尾视角。
  - `FollowCamera->bUsePawnControlRotation = false`：相机已经挂在会旋转的弹簧臂末端了，再转一次会变成双重旋转。
  - 身体不跟随控制器旋转：否则鼠标一动人物原地转圈，而是由"移动方向"驱动 —— 这才是第三人称动作游戏的手感。
  - `bCanCrouch = true`：没有这一句，`Crouch()` 会静默失败（角色蹲不下去，也不报错，很难查）。
  - 战斗组件放基类：敌我共用，且不依赖 ASC，所以不在两个子类里各建一份。
  - 用 `URPG_OverheadHealthBarComponent` 而不是引擎的 `UWidgetComponent`：它多做一件关键的事 —— 把"我是谁的血条"显式告诉 Widget。少了那一步，敌人的血条会去问 `GetOwningPlayerPawn()`，拿到的却是**本地玩家**，于是每条血条都显示玩家自己的血量（详见 `RPG_OverheadHealthBarComponent.h`）。
  - 血条位置 110：90 是默认半高（胶囊总高 180），+20 留出一点缝隙。
  - 血条不投影：地面上跟着角色跑的方块阴影很出戏。
- **被谁调用 / 调用谁**：引擎在 CDO 与实例构造时调用；只调用 `CreateDefaultSubobject` 与组件/移动组件的属性赋值。

#### `void ARPG_BaseCharacter::BeginPlay()`

- **干什么**：把蓝图里配置的 `UPROPERTY` 值同步到组件上；缓存布娃娃复位基准值与出生变换；刷新最大速度与头顶血条可见性。
- **关键实现**（按顺序）：
  1. `Super::BeginPlay()`。
  2. 若 `CameraBoom` 有效：`TargetArmLength = CameraBoomLength`；`SetRelativeRotation(CameraBoomOffset)`；`bEnableCameraLag = CameraLagSpeed > 0.f`；`CameraLagSpeed = FMath::Max(CameraLagSpeed, 1.f)`。
  3. 若 `GetCharacterMovement()` 有效：`RotationRate = FRotator(0.f, RotationRateYaw, 0.f)`；`MaxWalkSpeedCrouched = CrouchSpeed`。
  4. 若 `GetMesh()` 有效：`CachedMeshRelativeTransform = MeshComp->GetRelativeTransform()`；`CachedMeshCollisionProfile = MeshComp->GetCollisionProfileName()`。
  5. 若 `GetCapsuleComponent()` 有效：`CachedCapsuleCollisionEnabled = Capsule->GetCollisionEnabled()`。
  6. `CachedSpawnTransform = GetActorTransform()`。
  7. `RefreshMaxWalkSpeed()`。
  8. `RefreshOverheadWidgetVisibility()`。
- **为什么这么写**：
  - 为什么构造函数里设了还要再设一遍（注释原文要点）：构造函数只在 CDO（类默认对象）创建时执行一次。在蓝图子类里把 `CameraBoomLength` 从 400 改成 600 **不会**重新执行构造函数 —— 那时组件上的值仍然是 400。必须在运行时同步一次，蓝图配置才真的生效。注释称这是 UE 里非常经典的一个坑，凡是"UPROPERTY 的值影响组件属性"都要这么处理。
  - 缓存网格变换与碰撞预设放在 BeginPlay 而不是构造函数：蓝图可以覆盖网格的相对位置，构造函数拿到的只是 CDO 的默认值（见头文件对 `CachedMeshRelativeTransform` 的说明）。
  - 走统一的 `RefreshMaxWalkSpeed()` 入口而不是直接写 `WalkSpeed`：注释说"万一将来加了'出生即在战斗中'之类的配置，这里不用再改一次"。
  - 头顶血条先按"现在已知的控制关系"判一次；联机时这次判断可能还不准（Controller 还没复制过来），会在 `OnRep_Controller` 里再刷一次。
- **被谁调用 / 调用谁**：引擎派发；调用 `RefreshMaxWalkSpeed()`、`RefreshOverheadWidgetVisibility()`。

#### `void ARPG_BaseCharacter::PossessedBy(AController* NewController)`

- **干什么**：`Super::PossessedBy(NewController)` 后调一次 `RefreshOverheadWidgetVisibility()`。
- **关键实现**：函数体只有 Super 调用 + 一句 `RefreshOverheadWidgetVisibility()`。
- **为什么这么写**（注释原文要点）：这个函数是**必需**的，不能只靠 BeginPlay。引擎的生成顺序是：GameMode 先 `SpawnActor`（→ `PostActorConstruction` → `DispatchBeginPlay`），然后才调 `FinishRestartPlayer` → `Possess`。也就是说 **BeginPlay 跑在 Possess 之前** —— 那一刻 Controller 还是空的，判据必然返回 false，结果就是"本地玩家自己的头顶血条被显示出来"。
- **被谁调用 / 调用谁**：引擎（`APawn::PossessedBy` 流程）；调用 `RefreshOverheadWidgetVisibility()`。

#### `void ARPG_BaseCharacter::OnRep_Controller()`

- **干什么**：`Super::OnRep_Controller()` 后调一次 `RefreshOverheadWidgetVisibility()`。
- **关键实现**：函数体只有 Super 调用 + 一句 `RefreshOverheadWidgetVisibility()`。
- **为什么这么写**（注释原文）：客户端侧：Controller 是通过复制才到达的，在那之前判据一直返回 false。少了这一处，客户端上每个玩家的头顶血条都会显示出来 —— 包括自己那个。
- **被谁调用 / 调用谁**：引擎（`APawn` 的 `Controller` 复制回调）；调用 `RefreshOverheadWidgetVisibility()`。
- 头文件注明 `RefreshOverheadWidgetVisibility` 的三处调用（BeginPlay / PossessedBy / OnRep_Controller）"缺一不可"。

#### `bool ARPG_BaseCharacter::IsLocallyControlledPlayer() const`

- **干什么**：判断这个角色是不是"我这台机器上那个玩家"所控制的。
- **关键实现**（两个分支）：
  1. `const AController* Ctrl = GetController();` → 若 `!Ctrl || !Ctrl->IsA<APlayerController>()` 直接 `return false`。
  2. `return Ctrl->IsLocalController();`
- **为什么这么写**（头文件长注释，含引擎引用，原文要点）：
  - `APawn::IsLocallyControlled()` 就是 `Controller->IsLocalController()`，而 `AController::IsLocalController()`（`Controller.cpp:90-113`）有两条：
    - 第 94-98 行：`NetMode == NM_Standalone` → **无脑 return true**
    - 第 106-110 行：本地角色是权威、且远端角色不是 AutonomousProxy → return true（注释写的就是 "Local authority in control"）
  - 服务器上的 `AIController` 两条都沾（它是权威，也不是谁的 AutonomousProxy），所以**敌人身上 `IsLocallyControlled()` 恒为 true**。
  - 拿它当"是不是本地玩家"的判据，后果是单机下所有敌人都被当成玩家自己，头顶血条全被藏起来 —— 而且因为客户端上复制来的 AIController 不是权威、判定反而是对的，所以这个 bug **只在单机/主机上出现**。
  - 正确的定义要额外加一条"必须是 PlayerController"。
  - ② 的补充说明：listen server 主机上有多个 PlayerController，只有一个是自己的；远端玩家的 PlayerController 在主机上也会存在，但它不是本地的。
- **被谁调用 / 调用谁**：`RefreshOverheadWidgetVisibility()`、`Multicast_ShowDamageNumber_Implementation()`。标记为 `UFUNCTION(BlueprintPure)`。

#### `void ARPG_BaseCharacter::RefreshOverheadWidgetVisibility()`

- **干什么**：根据"我是不是本地玩家自己"决定头顶血条显示不显示，并打日志。
- **关键实现**：
  1. `if (!OverheadHealthBar) return;`
  2. `const bool bAmLocalPlayer = IsLocallyControlledPlayer();`
  3. `OverheadHealthBar->SetVisibility(!bAmLocalPlayer);` —— 即本地玩家**隐藏**，其他所有人**显示**。
  4. `UE_LOG(LogRPG_Combat, Log, TEXT("[%s] 头顶血条：%s（控制器 %s）"), ...)`，第二个占位符是 `"隐藏（我是本地玩家自己）"` 或 `"显示"`，第三个是 `*GetNameSafe(GetController())`。
  5. 若 `!bAmLocalPlayer && !OverheadHealthBar->GetWidgetClass()` → `UE_LOG(LogRPG_Combat, Warning, ...)`，提示在角色蓝图的 OverheadHealthBar 组件上指定 `WBP_OverheadHealthBar`。
- **为什么这么写**（注释原文要点）：
  - 需求是"除本地玩家外的其他玩家、AI 要有头顶血条"。判据必须是"**被本地 PlayerController 占有**"，不能直接用 `IsLocallyControlled()` —— 那个函数在敌人身上恒为 true。原因在 `AController::IsLocalController()`（`Controller.cpp:90-113`）：第 94-98 行 `NM_Standalone` 下无脑 return true；第 106-110 行 `GetLocalRole() == ROLE_Authority` 且 `RemoteRole` 不是 `AutonomousProxy` 时 return true。服务器上的 AIController 正好满足第二条，Standalone 下连第一条都直接命中。
  - ⚠️ 上面这段只适用于 **AI** —— `AAIController` 没有重写 `IsLocalController()`，走的就是基类实现。**玩家**的控制器走的是另一套：`APlayerController` 重写了它（`PlayerController.cpp:304-352`），规则完全不同（`NM_Client || NM_Standalone` 下无条件 true、`bIsLocalPlayerController` 快速通道等）。本函数对玩家的结论之所以成立，靠的是别的前提：服务器上远端玩家的 PC，`bIsLocalPlayerController` 只在 `Role == ROLE_SimulatedProxy` 时才置位（`GameModeBase.cpp:760-764`）；客户端**根本收不到**别人的 PC —— `AController` 构造函数里 `bOnlyRelevantToOwner = true`（`Controller.cpp:67`）+ `AActor::IsNetRelevantFor`（`Actor.cpp:398-401`）。所以"客户端上只有自己一个 PC"这个前提一旦被打破（旁观者流程、L3 联机），这里会**静默**把别人挨打的表现一起吞掉。
  - 打日志的理由：`"敌人头顶没血条"`有好几种成因 —— WBP 没建、组件上没配 Widget Class、可见性判反了 —— 而它们的现象**一模一样**。这条日志能把最后一种直接排除掉，省掉一轮"改代码加日志再编译"的往返。频率是每个角色每次控制关系变化一条，不会刷屏。
  - 报 Widget Class 警告的理由：Widget Class 没配是个静默失败：组件在那儿、可见性也对，就是什么都不显示。值得在生成时报一次 —— 这类问题靠肉眼排查会绕很远。
- **被谁调用 / 调用谁**：`BeginPlay()`、`PossessedBy()`、`OnRep_Controller()`；调用 `IsLocallyControlledPlayer()`、`URPG_OverheadHealthBarComponent::SetVisibility/GetWidgetClass()`。

#### `void ARPG_BaseCharacter::Multicast_ShowDamageNumber_Implementation(float Amount, FVector_NetQuantize Location)`

- **声明**：`UFUNCTION(NetMulticast, Unreliable)`，`void Multicast_ShowDamageNumber(float Amount, FVector_NetQuantize Location);` —— 头文件注释说明它让**所有客户端**在这个角色的某个位置冒一个伤害数字。
- **干什么**：让本机 HUD 在指定世界位置画一个伤害数字；本地玩家自己被打时跳过。
- **关键实现**：
  1. `if (IsLocallyControlledPlayer()) return;` —— 受击者自己那台机器不显示。
  2. `UWorld* World = GetWorld(); if (!World) return;`
  3. 遍历 `for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)`：取 `APlayerController* PC = It->Get();`，`if (!PC || !PC->IsLocalController()) continue;`，然后 `if (ARPG_HUD* HUD = PC->GetHUD<ARPG_HUD>()) HUD->ShowDamageNumber(Amount, Location);`，最后 `break;`（注释：一个客户端只有一个本地 PC，找到就够了）。
- **为什么这么写**（注释原文要点）：
  - 【为什么不能用现成的 `Event.Combat.Hit`】那个 GameplayEvent 只在**服务器**广播（见 `RPG_AttributeSet` 里的权威判断），而飘字是每个客户端都要各自画的东西 —— 服务器上画了没人看得见。NetMulticast 就是干这个的：服务器调一次，所有客户端各自执行一份。
  - 用 `Unreliable` 而不是 `Reliable`：飘字是纯表现，丢一个数字不影响任何逻辑，而 Reliable RPC 的确认与重发机制在挨打密集时会白白吃掉带宽。这是"表现类 RPC 一律 Unreliable"这条通用规则的实例。
  - `FVector_NetQuantize` 而不是 `FVector`：飘字位置精确到厘米就够了，量化后每个坐标只占几个字节。参数 `Amount` 只用于显示，不做任何计算。
  - **为什么判据放在这里就够**：这个 Multicast 是在**受击者**身上发起的（见 `RPG_AttributeSet::PostGameplayEffectExecute`），所以受击者自己那台机器 → `IsLocallyControlledPlayer() == true` → 跳过；其他客户端 / 主机 → false → 照常显示（别人照样看得见你被打掉多少）；AI 受击者 → 恒为 false（没有 PlayerController 控制它）→ 不受影响。
  - ⚠️ 用 `IsLocallyControlledPlayer()` 而不是 `IsLocallyControlled()`：后者对服务器上的 AI 也返回 true（`Controller.cpp:94-110`），会把"敌人挨打"在你的屏幕上一起吞掉。
  - 顺带一提：本地玩家的伤害本来就有 HUD 那三条属性条在显示，屏幕上再叠一个跟着自己角色飘的数字，位置又正好在屏幕中心，纯属干扰。
  - 职责划分：角色不该知道"飘字 widget 长什么样、放在哪、用什么动画"，那是表现层的事。这里只做"广播事实"，由 HUD 决定怎么画。
  - ⚠️ 必须挑**本地控制**的那个 PlayerController，不能用 `GetFirstPlayerController()` —— 那个函数返回的是 `PlayerControllerList[0]`，**没有任何 `IsLocalController` 过滤**（`World.cpp:6515-6533`）。listen server 主机上列表里的第一个恰好是主机自己，看起来能用；但那是"碰巧"而不是"保证"，换个创建顺序就会静默失效。挑本地那个才能保证"这一端的屏幕上冒一次数字"。
- **被谁调用 / 调用谁**：被 `RPG_AttributeSet::PostGameplayEffectExecute` 发起（注释指出）；调用 `ARPG_HUD::ShowDamageNumber(Amount, Location)`。

#### `UAbilitySystemComponent* ARPG_BaseCharacter::GetAbilitySystemComponent() const`

- **干什么**：`IAbilitySystemInterface` 的引擎接口实现。
- **关键实现**：一行 `return GetASCInternal();` —— 接口本身不含任何逻辑，全部差异由虚函数承担。
- **为什么这么写**（头文件）：注释写"引擎接口：GAS 内部机制依赖它，必须实现"。
- **被谁调用 / 调用谁**：GAS 内部机制、`URPG_AnimInstanceBase::UpdateCombatState()`；调用 `GetASCInternal()`。

#### `URPG_AttributeSet* ARPG_BaseCharacter::GetRPGAttributeSet() const`

- **干什么**：返回本角色的属性集（`IRPG_AbilitySystemInterface` 实现）。
- **关键实现**：`const UAbilitySystemComponent* ASC = GetASCInternal();` 为空返回 `nullptr`；否则 `return const_cast<URPG_AttributeSet*>(ASC->GetSet<URPG_AttributeSet>());`
- **为什么这么写**（注释原文）：`GetSet` 返回 const 指针 —— 因为它只是"找到并返回"，不承诺可写。但属性集本身是需要被 GE 修改的（GE 内部持有非 const 指针），所以这里去掉 const 限定是安全且必要的。
- **被谁调用 / 调用谁**：`IRPG_AbilitySystemInterface` 的调用方；调用 `GetASCInternal()`、`UAbilitySystemComponent::GetSet<URPG_AttributeSet>()`。

#### `bool ARPG_BaseCharacter::IsAlive() const`

- **干什么**：判断角色是否存活（`IRPG_AbilitySystemInterface` 实现）。
- **关键实现**：
  1. `const UAbilitySystemComponent* ASC = GetASCInternal();`
  2. `if (!ASC) return true;` —— ASC 尚未初始化完时按"存活"处理。
  3. `return !ASC->HasMatchingGameplayTag(RPGTags::State_Dead);`
- **为什么这么写**（注释原文）：ASC 尚未初始化完（BeginPlay 之前会被调用到）—— 按"存活"处理。如果这里返回 false，刚生成的角色会在第一帧被 AI 当成尸体忽略掉。
- **被谁调用 / 调用谁**：AI/GA 与 `Move()`；调用 `GetASCInternal()`、`HasMatchingGameplayTag`。

#### `void ARPG_BaseCharacter::Move(const FInputActionValue& Value)`

- **干什么**：按输入向量移动角色。
- **关键实现**：
  1. **★ `if (!IsAlive()) return;`** —— 死了就不接受移动输入。
  2. `const FVector2D MovementVector = Value.Get<FVector2D>();`
  3. `if (!Controller || MovementVector.IsNearlyZero()) return;`
  4. 用控制器的 Yaw 建旋转：`const FRotator YawRotation(0.f, Controller->GetControlRotation().Yaw, 0.f);`，取出 `ForwardDirection`（`EAxis::X`）与 `RightDirection`（`EAxis::Y`）。
  5. **交叉赋值**：`AddMovementInput(ForwardDirection, MovementVector.Y); AddMovementInput(RightDirection, MovementVector.X);`（前后用 Y、左右用 X）。
- **为什么这么写**（注释原文要点）：
  - 死亡判断放在这里的原因：这是**唯一**一处挡住"死亡期间还能跑"的地方，因为移动不是能力，`ActivationBlockedTags` 拦不住它；从血归零到 `GA_Death` 把移动模式关掉之间有一段蒙太奇时间，没有这道判断，玩家会在弥留之际满场跑。
  - 放在 `Move()` 而不是用 `Controller->SetIgnoreMoveInput()`：后者不复制，服务器上设了客户端不知道，联机下等于没设。而 `State.Dead` 标签是复制的，两端行为天然一致。
  - 用**控制器的** Yaw 而不是角色自身的 Yaw 作为参考系：这样"按 W"永远是"朝屏幕前方走"，而不是"朝角色面朝方向走"—— 后者在角色背对镜头时会变成往镜头方向跑，手感很差。
  - 轴约定：增强输入的 2D 轴是 X = 左右、Y = 前后，注释特意提醒"这里和第二行是交叉的（前后用 Y、左右用 X），别写反"。
- **被谁调用 / 调用谁**：`ARPG_PlayerController` 绑定输入后调用（头文件注释：这些是"角色会做什么"而不是"哪个键触发"，后者才是 PC 的职责）；调用 `IsAlive()`、`AddMovementInput()`。

#### `void ARPG_BaseCharacter::Look(const FInputActionValue& Value)`

- **干什么**：把视角输入转给控制器。
- **关键实现**：`const FVector2D LookAxisVector = Value.Get<FVector2D>();` → `if (!Controller) return;` → `AddControllerYawInput(LookAxisVector.X);` → `AddControllerPitchInput(LookAxisVector.Y);`（X = 水平旋转，Y = 俯仰）。
- **为什么这么写**：注释未说明。
- **被谁调用 / 调用谁**：`ARPG_PlayerController`；调用 `AddControllerYawInput` / `AddControllerPitchInput`。

#### `void ARPG_BaseCharacter::StartSprint()`

- **干什么**：切到冲刺速度。
- **关键实现**：`if (UCharacterMovementComponent* Movement = GetCharacterMovement()) Movement->MaxWalkSpeed = SprintSpeed;`（默认 850）。
- **为什么这么写**：头文件注明"阶段 3 起由 GA_Sprint 调用它并负责耐力消耗，输入不再直接调这里"。
- **被谁调用 / 调用谁**：`GA_Sprint`。

#### `void ARPG_BaseCharacter::StopSprint()`

- **干什么**：停止奔跑，速度回落到当前姿态对应的值。
- **关键实现**：只调 `RefreshMaxWalkSpeed();`，不直接写 `WalkSpeed`。
- **为什么这么写**（注释原文）：回落到"当前状态对应"的速度，而不是无条件回 `WalkSpeed` —— 否则蹲着跑完松开按键，角色会突然站起来以行走速度移动。交给统一的入口算，战斗/蹲伏/常态三种状态的优先级只有一份定义。
- **被谁调用 / 调用谁**：`GA_Sprint`；调用 `RefreshMaxWalkSpeed()`。

#### `void ARPG_BaseCharacter::SetCombatMovement(bool bInCombat)`（`UFUNCTION(BlueprintCallable, Category = "RPG|Movement")`）

- **干什么**：切换"战斗移动"状态，并按需挂/摘 `State.Sprinting` 标签（让动画切到跑步状态机）。
- **关键实现**：
  1. **幂等**：`if (bInCombat == bInCombatMovement) return;`
  2. `bInCombatMovement = bInCombat;`
  3. `RefreshMaxWalkSpeed();`
  4. 标签同步：`if (UAbilitySystemComponent* ASC = GetASCInternal())` → `const FGameplayTagContainer SprintTag(RPGTags::State_Sprinting);` → `const EGameplayTagReplicationState RepState = EGameplayTagReplicationState::CountToOwner;` → `bInCombat ? ASC->AddLooseGameplayTags(SprintTag, 1, RepState) : ASC->RemoveLooseGameplayTags(SprintTag, 1, RepState);`
  5. `UE_LOG(LogRPG_Combat, Verbose, TEXT("[%s] %s战斗移动（MaxWalkSpeed = %.0f）"), ...)`。
- **为什么这么写**（注释原文要点）：
  - **幂等的理由**（头文件）：感知回调在"进入/离开视野"时各调一次，但服务超时、行为树中断等路径也可能重复调，加个判断省掉无谓的属性写入（写 `MaxWalkSpeed` 会触发寻路参数更新）。
  - **为什么要顺带挂标签**：动画蓝图靠 **`State.Sprinting` 标签**决定用哪条移动状态机（见 `URPG_AnimInstanceBase::UpdateLocomotion`），而那个标签原本只有 `GA_Sprint` 会挂。敌人没有 `GA_Sprint`，所以它的 `MovementState` 一直停在 `Grounded` —— 走的是"走路"那条状态机。表现就是"人明明变快了，看着还在走"。补上标签后 `MovementState` 变成 `Sprinting`，动画切换到跑步状态机。标签的语义在这里要放宽理解：它表示"**以高于行走的速度移动**"，玩家的冲刺和 AI 的追击都算 —— 它们对动画的要求是一样的。
  - **为什么用 `CountToOwner` 而不是默认的"不复制"**：敌人的动画在**每个客户端**上都要各自求值，标签必须复制过去，否则会出现"服务器上在跑、客户端上在走"。`CountToOwner` 的语义是"标签复制给所有人，只有计数只发给拥有者"（`EGameplayTagReplicationState`，`GameplayEffectTypes.h:1050-1057`），对 `HasMatchingGameplayTag` 这类布尔查询完全够用。
  - **⚠️ 为什么这里不需要处理"客户端也要知道"**：`UCharacterMovementComponent::MaxWalkSpeed` 在引擎里**不是复制属性**（`CharacterMovementComponent.h:274`，UPROPERTY 上没有 replicated 标记），所以这个改动只影响服务器，客户端的 `MaxWalkSpeed` 会保持原值。对本项目不是问题，原因有两层：① **移动本身是复制的**，敌人的位置由 CharacterMovement 的复制通道同步给客户端，客户端看到的就是正确的移动速度，不需要知道 `MaxWalkSpeed` 是多少；② **AI 的移动不做客户端预测**，客户端的 `MaxWalkSpeed` 只被动画蓝图用来算 `SpeedRatio`，而 `SpeedRatio` 在算出来之后会被 Clamp 到 0~1 —— 即使分母是旧的 300，追人时的比值也只是被封顶到 1.0，表现上仍然是"全速跑"，方向是对的，只是失去了中间的过渡区间。如果将来要做"敌人被减速 50%"这类会让 `SpeedRatio` 变得不准的效果，就得把 `bInCombatMovement` / 速度倍率做成 `ReplicatedUsing` 的属性，在 OnRep 里重新算一遍。目前不需要。
- **被谁调用 / 调用谁**：AI 在感知到目标时调用 —— 见 `ARPG_AIController::SetCombatState()`；调用 `RefreshMaxWalkSpeed()`、`AddLooseGameplayTags` / `RemoveLooseGameplayTags`。

#### `void ARPG_BaseCharacter::RefreshMaxWalkSpeed()`（protected）

- **干什么**：按当前姿态重算 `MaxWalkSpeed`，是这一属性的唯一写入口。
- **关键实现**：
  1. `UCharacterMovementComponent* Movement = GetCharacterMovement(); if (!Movement) return;`
  2. `if (bIsCrouched)` → `Movement->MaxWalkSpeed = CrouchSpeed;`
  3. `else if (bInCombatMovement)` → `Movement->MaxWalkSpeed = CombatMoveSpeed;`
  4. `else` → `Movement->MaxWalkSpeed = WalkSpeed;`
- **为什么这么写**（注释原文要点）：
  - 抽出来的理由（头文件）：有**三个**地方会改这个值（启动同步 / 蹲伏 / 战斗切换），各写各的迟早会出现"蹲着脱战之后站起来用走路速度跑"这类组合 bug —— 优先级规则只有一份，才不会各处不一致。优先级：**蹲伏 > 战斗 > 常态**。
  - 为什么蹲伏排最前：蹲着移动时 UE 用的是另一个字段（`MaxWalkSpeedCrouched`），但我们的蹲伏和战斗是有可能叠加的（敌人蹲着巡逻时发现玩家）。这时"蹲着"是更强的约束。
  - 冲刺不参与这个优先级：它由 `StartSprint()` 直接写 `SprintSpeed`，只在能力激活期间生效，结束时调 `StopSprint()` 回到这里重新算。
- **被谁调用 / 调用谁**：`BeginPlay()`、`StopSprint()`、`SetCombatMovement()`。

#### `void ARPG_BaseCharacter::ToggleCrouch()`

- **干什么**：蹲伏/起立切换。
- **关键实现**：两行 —— `if (bIsCrouched) UnCrouch(); else Crouch();`
- **为什么这么写**：注释未说明。
- **被谁调用 / 调用谁**：输入（`ARPG_PlayerController`）；调用 `ACharacter::Crouch()` / `UnCrouch()`。

#### `UAnimMontage* ARPG_BaseCharacter::PickHitReactMontage() const`

- **干什么**：从受击蒙太奇池里随机挑一个（**只挑不播**）。
- **关键实现**：
  1. `if (HitReactMontages.Num() == 0) return nullptr;`
  2. 用 `TArray<UAnimMontage*, TInlineAllocator<8>> ValidMontages;` + `Reserve(HitReactMontages.Num())`，`for (const TObjectPtr<UAnimMontage>& Montage : HitReactMontages)` 把非空的加进去。
  3. `if (ValidMontages.Num() == 0) return nullptr;`
  4. `return ValidMontages[FMath::RandRange(0, ValidMontages.Num() - 1)];`
- **为什么这么写**（注释原文要点）：
  - 逐个判空而不是直接随机取值：数组里留了 None 槽（美术删了蒙太奇、或配置时手滑加了一行）不该变成运行时崩溃。
  - 池子（而不是单个）是为了避免连续挨打时反复播同一个动作 —— 打击感的一半来自"每次反馈略有不同"。只配一个也能跑，随机范围就是它自己。
  - ⚠️ 随机只在**服务器**上发生一次，客户端不参与挑选 —— 蒙太奇信息（`RepAnimMontageInfo`）是复制属性，服务器播了哪个，客户端就跟着播哪个。两端各自随机的话，会随机到不同的蒙太奇。
  - 头文件补充：播放交给 GA 的 AbilityTask —— 那是唯一能正确跟踪"蒙太奇什么时候结束"的地方，而受击/死亡的表现时序完全依赖这个信号。角色这边只负责回答"我该播哪个"。没配、或配的槽位全是 None 时返回 nullptr。
- **被谁调用 / 调用谁**：`GA_HitReact` 一侧（头文件说明由 GA 的 AbilityTask 播放）。

#### `void ARPG_BaseCharacter::EnterRagdoll()`

- **干什么**：进入布娃娃状态（角色失去控制，网格交给物理引擎）。
- **关键实现**：`if (bRagdollEnabled) return;` → `bRagdollEnabled = true;` → 手动调一次 `OnRep_RagdollEnabled();`
- **为什么这么写**：
  - ⚠️ 注释原文：OnRep 在**服务器上不会被自动调用** —— 它只在属性复制到客户端时触发。所以这里手动调一次，让服务器本体的表现和客户端一致。少了这一句，表现是"客户端上敌人倒了，服务器（也就是 PIE 里的主机自己）看着还站着"。
  - 头文件：由 `GA_Death` 在死亡蒙太奇播完后调用。**不要直接调它来表示"死亡"** —— 死亡是一个过程（蒙太奇 → 倒地），布娃娃只是最后一步。
- **被谁调用 / 调用谁**：`GA_Death`；调用 `OnRep_RagdollEnabled()`。

#### `void ARPG_BaseCharacter::ExitRagdoll()`

- **干什么**：退出布娃娃，把网格挂回胶囊体并恢复移动。
- **关键实现**：`if (!bRagdollEnabled) return;` → `bRagdollEnabled = false;` → `OnRep_RagdollEnabled();`
- **为什么这么写**（注释原文）：幂等守卫是**必须**的，不是优化：属性初次复制到客户端时，引擎会为所有复制属性调用一次 OnRep，包括值等于默认值的那些。也就是说活着的角色也会收到一次 `OnRep_RagdollEnabled(false)`，没有这个守卫就会白白跑一遍"站起来"的流程（把移动模式重设成 Walking、把网格变换拨回去……在跳跃中触发时尤其明显）。
- **⚠️ 与头文件的表述不一致（如实指出，不做判断）**：头文件对同一件事的说明相反 —— 它说 OnRep 的触发条件比想象中严格，**值真的变了才会调**（`RepLayout.cpp:3372-3392`，值相同会打一条 "Skipping RepNotify" 的 Verbose 日志），所以活着的角色初次复制到客户端时**不会**收到 `OnRep_RagdollEnabled(false)`（本地初值就是 false，没变化）；而"客户端开始观察一个**已经倒地**的角色"这种情况会收到 `OnRep(true)`，那正是我们要的 —— 新进来的客户端也能看到尸体躺在地上。头文件据此说 `ExitRagdoll` 里那道幂等守卫"因此不是'必须的'，留着是因为它便宜，而且能挡住将来可能出现的其他调用路径"。两处注释（`.h` 第 514-523 行 vs `.cpp` 第 574-578 行）对"引擎是否会为默认值补发 OnRep"给出了矛盾的说法，本章如实并列，不代为裁定。
- **被谁调用 / 调用谁**：`ResetForRespawn()`、`GA_Death`（推测）；调用 `OnRep_RagdollEnabled()`。

#### `void ARPG_BaseCharacter::OnRep_RagdollEnabled()`（`UFUNCTION()`，`ReplicatedUsing` 目标）

- **干什么**：布娃娃开关的复制回调 —— 各端收到开关后，各自跑同一套引擎 API 让网格进入/退出物理模拟。
- **关键实现**：
  - 先取 `USkeletalMeshComponent* MeshComp = GetMesh();`，为空 return。
  - **倒下分支（`bRagdollEnabled == true`）**，按 ①②③ 顺序：
    1. **先停移动**：`Movement->StopMovementImmediately(); Movement->DisableMovement();`
    2. **胶囊体让位**：`Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);`
    3. **交给物理**：`MeshComp->SetCollisionProfileName(TEXT("Ragdoll"));` → `SetAllBodiesSimulatePhysics(true)` → `SetSimulatePhysics(true)` → `WakeAllRigidBodies()`。
    4. `MeshComp->bBlendPhysics = true;`
  - **站起来分支（`bRagdollEnabled == false`）**：
    1. 先清速度：`SetAllPhysicsLinearVelocity(FVector::ZeroVector)`、`SetAllPhysicsAngularVelocityInDegrees(FVector::ZeroVector)`。
    2. `bBlendPhysics = false;` → `SetAllBodiesSimulatePhysics(false)` → `SetSimulatePhysics(false)`。
    3. 还原网格变换：`if (CachedMeshRelativeTransform.IsSet()) MeshComp->SetRelativeTransform(CachedMeshRelativeTransform.GetValue());`
    4. 还原碰撞预设：`SetCollisionProfileName(CachedMeshCollisionProfile.IsNone() ? FName(TEXT("CharacterMesh")) : CachedMeshCollisionProfile);`
    5. 还原胶囊体碰撞：`Capsule->SetCollisionEnabled(CachedCapsuleCollisionEnabled);`
    6. 恢复移动模式：`Movement->SetMovementMode(MOVE_Walking);`
- **为什么这么写**（注释原文要点）：
  - ① 不停移动的话，CharacterMovement 下一帧还会按输入方向推胶囊体，而网格已经在自己模拟物理了 —— 表现是"尸体在地上滑行"。
  - ② 还开着碰撞的话，物理网格会和"套着自己的那个胶囊体"顶在一起：尸体要么悬在半空，要么原地高频抖动。
  - ③ "Ragdoll" 是引擎自带的碰撞预设（PhysicsBody + 忽略 Pawn 通道），直接用它比自己拼一串响应设置更不容易错。
  - ④ `bBlendPhysics` 让"动画姿势"平滑过渡到"物理模拟"。注释注明：`SetSimulatePhysics(true)` 内部已经置过这个标志了（`SkeletalMeshComponentPhysics.cpp:362-372`），这里显式写一遍是为了让"为什么要混合"这件事在代码里可见。
  - 先清速度再关模拟：不清的话，各骨骼会保留倒地时的线速度/角速度 —— 下一次再进布娃娃时这些陈旧速度会被直接拿来用，表现是"第二次死亡时尸体猛地弹一下"。注释特意提醒参数是 `FVector`（UE 5.8 里没有接受 float 的那版重载了）。
  - 必须显式拨回网格变换：物理一关，网格会停在"最后被模拟到的那个姿势"上，否则胶囊体在原地、网格却躺在三米外 —— 而且它**不会自己恢复**。
  - 碰撞预设要还原成**出生时那个**而不是硬写 `"CharacterMesh"`：蓝图里可能配了自定义的碰撞预设，硬写会把它悄悄改掉。拿不到缓存时（BeginPlay 之前就被调用）才回落。
  - `MOVE_Walking` 而不是 `MOVE_None`：复活后本来就该是站着的。至于"脚下有没有地、要不要转成 Falling"，CharacterMovement 下一帧自己会纠正。
  - 头文件对复制策略的说明：⚠️ 复制的是这个**开关**，不是物理状态本身。骨骼的位置/速度/碰撞由 Chaos 在各端**各自**模拟，引擎压根不复制它们。所以做法只能是：服务器改开关 → 各端收到 OnRep → 各端跑同一套 API。视觉上会有细微差异（两台机器算出来的倒地姿势不可能逐帧一致），要精确同步得用 Network Physics 那一套 —— 那属于 L3，本项目不做。
- **被谁调用 / 调用谁**：`EnterRagdoll()` / `ExitRagdoll()`（服务器侧手动调用）、引擎复制回调（客户端侧，由 `GetLifetimeReplicatedProps` 里的 `ReplicatedUsing` 指定）。

#### `void ARPG_BaseCharacter::StartRespawnCountdown()`

- **干什么**：启动重生倒计时（仅服务器）。
- **关键实现**：
  1. `if (!HasAuthority()) return;`
  2. `if (RespawnDelay <= 0.f)` → 打 Log（`"RespawnDelay 为 0，不重生（尸体保留）"`）并 return。
  3. `GetWorldTimerManager().SetTimer(RespawnTimerHandle, this, &ARPG_BaseCharacter::PerformRespawn, RespawnDelay, /*bLoop=*/false);`
  4. 打 Log（`"[%s] %.1f 秒后重生"`）。
- **为什么这么写**：
  - 计时只在服务器跑的理由（注释原文）：客户端上这个 TimerHandle 永远是空的，它看到的"复活"是服务器传送 + 属性复制过来的结果。
  - `RespawnDelay <= 0` 表示"不重生"（头文件：敌人默认就是这样 —— 死了就躺着，免得玩家刚打赢又冒出来一个）。
  - 头文件：由 `GA_Death` 在布娃娃之后调用（**仅服务器**）。
- **被谁调用 / 调用谁**：`GA_Death`；调用 `GetWorldTimerManager().SetTimer` 注册 `PerformRespawn`。

#### `FTransform ARPG_BaseCharacter::GetRespawnTransform() const`（virtual protected）

- **干什么**：回答"重生时把角色放回哪个位置"。
- **关键实现**：一行 `return CachedSpawnTransform;`（BeginPlay 时记下的出生变换）。
- **为什么这么写**（注释原文）：基类默认回出生点。敌人用这个 —— 它在关卡里摆在哪就在哪复活。玩家会覆写成"找 PlayerStart"（见 `ARPG_Player`）。
- **被谁调用 / 调用谁**：`PerformRespawn()`；被 `ARPG_Player::GetRespawnTransform()` 覆写。

#### `void ARPG_BaseCharacter::PerformRespawn()`（virtual protected）

- **干什么**：倒计时到点后真正执行重生：复位状态 → 传送回出生点 → 通知子类。
- **关键实现**：
  1. `if (!HasAuthority()) return;`
  2. ① `ResetForRespawn();`
  3. ② `const FTransform RespawnTransform = GetRespawnTransform(); SetActorTransform(RespawnTransform, /*bSweep=*/false, nullptr, ETeleportType::TeleportPhysics);`
  4. ③ `OnRespawned();`
  5. Log：`"[%s] 已重生到 %s"`（用 `RespawnTransform.GetLocation().ToCompactString()`）。
- **为什么这么写**（注释原文要点）：
  - ⚠️ 顺序不能反（头文件）：必须先 `ResetForRespawn()` 再传送 —— 反过来的话，传送触发的物理/碰撞更新会在"角色还带着死亡状态"时跑一遍，布娃娃的网格会和刚落地的胶囊体打架，表现是重生瞬间人被弹飞。
  - 用 `TeleportPhysics` 而不是默认的 `TeleportNone`：布娃娃刚关掉、物理场景里还留着网格的旧位置，不通知物理线程的话它会把角色拽回去。
  - `OnRespawned()` 的作用（注释）：通知子类（敌人在这里重启 AI，玩家在那里同步视角）。
- **被谁调用 / 调用谁**：`StartRespawnCountdown()` 注册的定时器；调用 `ResetForRespawn()`、`GetRespawnTransform()`、`SetActorTransform()`、`OnRespawned()`。

#### `void ARPG_BaseCharacter::ResetForRespawn()`

- **干什么**：复活前的状态复位：退出布娃娃、摘掉死亡标记、清掉 GE、属性回满、重启被动能力、清空输入缓存。
- **关键实现**（按 ①②③③.5④ 编号）：
  1. ① `if (bRagdollEnabled) ExitRagdoll();`
  2. ② `if (UAbilitySystemComponent* ASC = GetASCInternal())`：
     - `ASC->RemoveLooseGameplayTags(FGameplayTagContainer(RPGTags::State_Dead), 1, EGameplayTagReplicationState::CountToOwner);`
     - `ASC->RemoveActiveEffects(FGameplayEffectQuery());`
     - ③ 属性回满：`if (InitAttributesEffect)` → `FGameplayEffectContextHandle Context = ASC->MakeEffectContext(); Context.AddSourceObject(this); ASC->ApplyGameplayEffectToSelf(InitAttributesEffect->GetDefaultObject<UGameplayEffect>(), 1.f, Context);`；否则 Warning（`"[%s] 没配 InitAttributesEffect，重生后属性不会恢复"`）。
     - ③.5 `if (URPG_AbilitySystemComponent* RPGASC = Cast<URPG_AbilitySystemComponent>(ASC)) RPGASC->ReactivatePassiveAbilities();`
  3. ④ `if (CombatComponent)` → `CombatComponent->ClearInputBuffer(); CombatComponent->ResetCombo();`
- **为什么这么写**（注释原文要点）：
  - 死亡标记必须单独摘：它是 loose tag 不是 GE —— `RemoveActiveEffects` 清不掉它。不摘的话新角色一出生就带着 `State.Dead`，所有能力被 `ActivationBlockedTags` 挡死，表现是"复活了但一个键都按不动"，且不会有任何报错。
  - 清 GE 的理由：把还挂着的 GE 全清掉（流血、Buff、冷却、无敌帧……），留着它们会出现"复活后还带着上辈子的减速 debuff"这种超自然现象。
  - 属性回满为什么走 GE 而不是 `SetHealth(GetMaxHealth())`：重新应用一次初始化 GE，才能保证"属性怎么初始化"这件事只有一份定义 —— 哪天初始 GE 里加了护盾、加了初始耐力，重生逻辑不用跟着改。
  - ③.5 的理由：死亡时 `CancelAllAbilities()` 把 `GA_StaminaRegen` 也停掉了，不重启的话"复活后耐力永远不恢复"，而且不报错。详见 `URPG_AbilitySystemComponent::ReactivatePassiveAbilities`。
  - ④ 的理由：不清的话，死亡瞬间按下的那一堆输入会被带进新一条命：复活后角色自己就动起来了 —— 玩家会觉得"角色不受控制"。
  - 头文件：**不含传送** —— 传送到哪由重生逻辑决定（玩家去 PlayerStart、敌人回出生点），这里只负责"把这个人恢复成能动的状态"。
- **被谁调用 / 调用谁**：`PerformRespawn()`；调用 `ExitRagdoll()`、`GetASCInternal()`、`RemoveLooseGameplayTags`、`RemoveActiveEffects`、`ApplyGameplayEffectToSelf`、`ReactivatePassiveAbilities()`、`ClearInputBuffer()`、`ResetCombo()`。

#### `virtual void ARPG_BaseCharacter::OnDeathStarted() {}` / `virtual void ARPG_BaseCharacter::OnRespawned() {}`

- **干什么**：死亡 / 重生的子类扩展点，基类实现为空。
- **关键实现**：空函数体，调用 `Super` 与否由子类决定（`ARPG_Enemy` 两个都调了 `Super`）。
- **为什么这么写**（头文件注释原文要点）：
  - 这两个函数由 `GA_Death` 在流程中的固定时机调用，**不是**给自己写的 —— 基类实现是空的，只有需要额外处理的子类才重写。
  - 它们放在 public 而不是 protected，是因为调用方是另一个类（`GA_Death`）。语义上它们是"通知"，和 `EnterRagdoll()` 一样属于角色的公开能力。
  - `OnDeathStarted`：死亡开始（在挂上 `State.Dead`、取消完其它能力之后调用）。敌人重写它来让 AI 停止思考。**只有服务器会走到这里**（`GA_Death` 是 ServerOnly）。
  - `OnRespawned`：重生完成（复位 + 传送都做完之后调用）。敌人重写它来重启 AI。
- **被谁调用 / 调用谁**：`GA_Death`（`OnDeathStarted`）、`PerformRespawn()`（`OnRespawned`）；被 `ARPG_Player`、`ARPG_Enemy` 覆写。

#### `void ARPG_BaseCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const`

- **干什么**：声明复制属性。
- **关键实现**：`Super::GetLifetimeReplicatedProps(OutLifetimeProps);` 后一行：`DOREPLIFETIME_CONDITION(ARPG_BaseCharacter, bRagdollEnabled, COND_None);`
- **为什么这么写**（注释原文）：只复制"倒没倒"这个开关，物理本身各端各自模拟 —— 理由见头文件。`COND_None`：要复制给所有客户端，包括"看着这个敌人"的那几个。
- **被谁调用 / 调用谁**：引擎网络复制系统。

#### `virtual UAbilitySystemComponent* ARPG_BaseCharacter::GetASCInternal() const`（protected）

- **干什么**：返回本角色的 ASC 的虚函数，是子类**必须**实现的 GAS 接入点。
- **关键实现**：`PURE_VIRTUAL(ARPG_BaseCharacter::GetASCInternal, return nullptr;);` —— 纯虚，但带一个返回 nullptr 的兜底实现。
- **为什么这么写**（头文件）：玩家 `return PlayerState->GetAbilitySystemComponent()`；敌人 `return 自己的 AbilitySystemComponent`。允许返回 nullptr（ASC 尚未初始化时）。
- **被谁调用 / 调用谁**：`GetAbilitySystemComponent()`、`GetRPGAttributeSet()`、`IsAlive()`、`SetCombatMovement()`、`ResetForRespawn()`；由 `ARPG_Player` / `ARPG_Enemy` 实现。

#### 内联访问器（全部 `UFUNCTION(BlueprintPure)`）

| 签名 | 返回 | 说明 |
| --- | --- | --- |
| `bool IsInCombatMovement() const` | `bInCombatMovement` | 是否处于战斗移动状态 |
| `float GetWalkSpeed() const` | `WalkSpeed` | 角色当前的基础移动速度（行走） |
| `URPG_CombatComponent* GetCombatComponent() const` | `CombatComponent` | 战斗组件：输入缓存、连段索引、当前攻击模组。GA 通过它读取"当前该打第几段" |
| `bool IsRagdoll() const` | `bRagdollEnabled` | 是否处于布娃娃状态 |
| `UAnimMontage* GetDeathMontage() const` | `DeathMontage` | 死亡蒙太奇（没配返回 nullptr） |
| `float GetHitReactPlayRate() const` | `HitReactPlayRate` | 受击蒙太奇播放速率 |
| `float GetDeathMontagePlayRate() const` | `DeathMontagePlayRate` | 死亡蒙太奇播放速率 |
| `URPG_OverheadHealthBarComponent* GetOverheadHealthBar() const` | `OverheadHealthBar` | 头顶血条组件。本地玩家自己看不到它 |
| `float GetRespawnDelay() const` | `RespawnDelay` | 死后多久重生（秒）。注释：公开出来是给 HUD 做倒计时用的 —— 死亡状态（`State.Dead`）是复制的，重生时长是个配置常量，两者客户端都能拿到，所以**客户端可以自己数这个秒数**，不需要服务器再复制一个"还剩几秒" |

#### 成员变量

| 变量 | 标记 | 类型 / 默认值 | 说明（注释原文要点） |
| --- | --- | --- | --- |
| `CameraBoom` | `VisibleAnywhere, BlueprintReadOnly` | `TObjectPtr<USpringArmComponent>` | 弹簧臂。`bUsePawnControlRotation = true`，所以它跟随控制器旋转（鼠标控制视角） |
| `FollowCamera` | `VisibleAnywhere, BlueprintReadOnly` | `TObjectPtr<UCameraComponent>` | 跟随相机。挂在弹簧臂末端，自己不旋转，由弹簧臂带动 |
| `CombatComponent` | `VisibleAnywhere, BlueprintReadOnly` | `TObjectPtr<URPG_CombatComponent>` | 敌我共用。**不依赖任何 GAS 类** —— 只持有【输入缓存】【连段索引】【当前攻击模组】。好处：战斗逻辑可以脱离 GAS 单独测试；将来加召唤物、可破坏物之类没有 ASC 的 Actor 也能直接复用 |
| `OverheadHealthBar` | `VisibleAnywhere, BlueprintReadOnly` | `TObjectPtr<URPG_OverheadHealthBarComponent>` | 世界空间里"贴"在角色头顶，但用 **Screen** 空间渲染。为什么是 Screen 而不是 World：`EWidgetSpace::Screen` 的 WidgetComponent 会始终正对相机，且**大小不随距离变化** —— 这正是血条/名牌想要的行为（远处的小怪和贴脸的大怪血条一样清楚）；World 空间会让血条跟着透视缩放，离远了就糊成一团。为什么放基类而不是敌人独占：需求是"**除本地玩家外**的其他玩家、AI 都要有"，判定条件是"谁在控制我"不是"我是玩家还是敌人" —— 玩家的血条在**队友视角**里也是要显示的。Widget Class 在角色蓝图里设（见 `PHASE7_UI_SETUP.md`） |
| `CameraBoomLength` | `EditDefaultsOnly, BlueprintReadOnly` | `float = 400.f` | 弹簧臂长度 |
| `CameraBoomOffset` | `EditDefaultsOnly, BlueprintReadOnly` | `FRotator = FRotator(-10.f, 0.f, 0.f)` | 弹簧臂相对旋转 |
| `CameraLagSpeed` | `EditDefaultsOnly, BlueprintReadOnly` | `float = 15.f` | 相机滞后速度。越大越"跟手"，越小越有重量感。设 0 关闭滞后 |
| `WalkSpeed` | `EditDefaultsOnly, BlueprintReadOnly` | `float = 300.f` | 行走速度 |
| `SprintSpeed` | `EditDefaultsOnly, BlueprintReadOnly` | `float = 850.f` | 冲刺速度 |
| `CrouchSpeed` | `EditDefaultsOnly, BlueprintReadOnly` | `float = 180.f` | 蹲伏速度 |
| `CombatMoveSpeed` | `EditDefaultsOnly, BlueprintReadOnly` | `float = 600.f` | 战斗移动速度。注释：比巡逻慢走快，但比玩家冲刺（850）慢 —— 敌人的定位是"跟得上你，但你还能甩掉它"，跑得和玩家一样快会让追逐失去张力。⚠️ 改这个值只影响**服务器**（`MaxWalkSpeed` 不是复制属性，详见 `SetCombatMovement()`） |
| `RotationRateYaw` | `EditDefaultsOnly, BlueprintReadOnly` | `float = 540.f` | 角色转向速率（度/秒）。越大转身越快，越小越"重" |
| `StartupAbilities` | `EditDefaultsOnly, BlueprintReadOnly` | `TMap<FGameplayTag, TSubclassOf<UGameplayAbility>>` | 输入标签 → 起始能力。例：`Input.Attack.Light → GA_LightAttack`；敌人在自己的蓝图里配成 AI 用的能力（可以复用同一个 GA 类） |
| `StartupPassiveAbilities` | `EditDefaultsOnly, BlueprintReadOnly` | `TArray<TSubclassOf<UGameplayAbility>>` | 起始能力：**没有输入触发**的那些。注释列出两类：① 常驻被动（`bActivateOnGranted = true`），典型成员 `GA_StaminaRegen`；② 事件驱动（`bActivateOnGranted = false`），典型成员 `GA_Death`、`GA_HitReact`，授予后只是**待命**，靠自己在构造函数里声明的 `AbilityTriggers` 等 GameplayEvent。为什么不像 `StartupAbilities` 那样按"触发方式"分成两张表：因为"怎么触发"是**能力自己的属性**（`AbilityTriggers` / `bActivateOnGranted`），不是角色的属性；角色只负责回答"我会哪些能力"，分成两张表反而多一层需要维护的对应关系 |
| `InitAttributesEffect` | `EditDefaultsOnly, BlueprintReadOnly` | `TSubclassOf<UGameplayEffect>` | 初始属性集 GE。角色初始化时应用一次。放在 GE 而不是 C++ 构造函数里，是为了让数值可以被策划直接调整而不必重新编译 |
| `HitReactMontages` | `EditDefaultsOnly, BlueprintReadOnly` | `TArray<TObjectPtr<UAnimMontage>>` | 受击蒙太奇池。每次受击随机抽一个。池子（而不是单个）是为了避免连续挨打时反复播同一个动作 —— "打击感的一半来自'每次反馈略有不同'"。只配一个也能跑，随机范围就是它自己 |
| `HitReactPlayRate` | `EditDefaultsOnly, BlueprintReadOnly` | `float = 1.f` | 受击蒙太奇播放速率。小于 1 会放慢，配合略长的蒙太奇能做出"被打得踉跄"的沉重感。玩家 1.0 保持响应性，敌人 0.9 显得笨重 |
| `DeathMontage` | `EditDefaultsOnly, BlueprintReadOnly` | `TObjectPtr<UAnimMontage>` | 死亡蒙太奇。播完（或被打断）后进入布娃娃。留空也可以：`GA_Death` 会跳过等待直接倒地，表现上就是"人直接瘫下去"，测试布娃娃链路时反而更快 |
| `DeathMontagePlayRate` | `EditDefaultsOnly, BlueprintReadOnly` | `float = 1.f` | 死亡蒙太奇播放速率。一般大于 1（快放），因为死亡蒙太奇末尾通常已经躺到地上，再慢放会显得拖沓 |
| `RespawnDelay` | `EditDefaultsOnly, BlueprintReadOnly, meta = (ClampMin = "0.0")` | `float = 0.f` | 死后多久重生（秒）。0 = **不重生**，尸体原地留着（敌人默认）。玩家建议 3~5 秒 |
| `bInCombatMovement` | private，无 UPROPERTY | `bool = false` | 当前是否处于"战斗移动"状态 |
| `bRagdollEnabled` | **`UPROPERTY(ReplicatedUsing = OnRep_RagdollEnabled)`** | `bool = false` | 布娃娃开关。见上文 `OnRep_RagdollEnabled` 的两段长注释（复制的是开关不是物理状态；OnRep 触发条件的说明在头文件与 .cpp 之间不一致） |
| `CachedMeshRelativeTransform` | private，无 UPROPERTY | `TOptional<FTransform>` | 出生时的网格相对变换（相对胶囊体）。布娃娃结束时必须把网格拨回这个值 —— 物理仿真停下后，网格会保持着"最后被模拟到的世界姿势"和它的相对变换，不还原的话胶囊体站在原地、网格躺在几米外。在 BeginPlay 里记而不是构造函数里：蓝图可以覆盖网格的相对位置，构造函数拿到的只是 CDO 的默认值 |
| `CachedMeshCollisionProfile` | private，无 UPROPERTY | `FName` | 出生时的碰撞设置。布娃娃会临时改掉它们（网格换 "Ragdoll" 预设、胶囊体整个关掉），退出时必须还原成**原来那个**，而不是还原成"引擎默认值" —— 蓝图里可能给网格换了自定义的碰撞预设，硬写 `"CharacterMesh"` 会把它悄悄改掉，而且要过很久才会有人发现 |
| `CachedCapsuleCollisionEnabled` | private，无 UPROPERTY | `ECollisionEnabled::Type = ECollisionEnabled::QueryAndPhysics` | 同上，胶囊体的碰撞开关 |
| `CachedSpawnTransform` | private，无 UPROPERTY | `FTransform` | 出生位置。敌人（和不走 PlayerStart 的情况）重生时回到这里 |
| `RespawnTimerHandle` | private，无 UPROPERTY | `FTimerHandle` | 重生倒计时句柄 |

> 头文件对"受击 / 死亡表现"这一组配置的总说明：资产配置放这里（而不是放 GA 里），因为这是"这个角色长什么样"的属性 —— 同一个 `GA_HitReact` 挂在玩家和敌人身上，播出来的蒙太奇应该各是各的。

---

### `Source/RPG/Character/RPG_Player.h` / `RPG_Player.cpp`

**一句话职责**：玩家角色 —— 自己**不创建** ASC（由 `ARPG_PlayerState` 负责），本类唯一与 GAS 相关的职责是在正确的时机建立 "Owner = PlayerState / Avatar = 本角色" 的关联；此外覆写重生点为 PlayerStart，并在复活后显式把位置与视角推给客户端。

**类/结构体**：

- `ARPG_Player` —— 继承 `ARPG_BaseCharacter`（`UCLASS()`，非 Abstract）。

#### `ARPG_Player::ARPG_Player()`

- **干什么**：构造函数留空。
- **关键实现**：函数体为空。
- **为什么这么写**（注释原文）：玩家角色不创建 ASC —— 它在 `ARPG_PlayerState` 上。这里有意留空并写下注释，是为了让读到这个构造函数的人不会以为"忘了写"。
- **被谁调用 / 调用谁**：引擎。

#### `void ARPG_Player::BeginPlay()`

- **干什么**：`Super::BeginPlay()` 后补一次 GAS 初始化尝试。
- **关键实现**：`InitializeAbilitySystem();`
- **为什么这么写**（注释原文）：兜底尝试：若 `PossessedBy` 已经成功初始化过，这里是空操作。
- **被谁调用 / 调用谁**：引擎；调用 `InitializeAbilitySystem()`。

#### `void ARPG_Player::PossessedBy(AController* NewController)`

- **干什么**：`Super::PossessedBy(NewController)` 后调 `InitializeAbilitySystem()`。
- **关键实现**：函数体只有 Super + 一句 `InitializeAbilitySystem();`
- **为什么这么写**（注释原文）：单机 / 服务端的主要初始化路径：控制器占有 Pawn 时。注意这个时机 PlayerState 可能尚未就绪 —— `InitializeAbilitySystem` 内部会判空并推迟。
- **被谁调用 / 调用谁**：引擎（`APawn::PossessedBy`）；调用 `InitializeAbilitySystem()`。**注意**：`ARPG_BaseCharacter::PossessedBy` 里也有 `RefreshOverheadWidgetVisibility()`，本函数通过 Super 调用链覆盖它。

#### `void ARPG_Player::OnRep_PlayerState()`

- **干什么**：`Super::OnRep_PlayerState()` 后调 `InitializeAbilitySystem()`。
- **关键实现**：函数体只有 Super + 一句 `InitializeAbilitySystem();`
- **为什么这么写**（注释原文）：客户端路径：PlayerState 复制到达时重新建立关联。单机下这个函数不会被调用，但写上它几乎没有成本，而一旦将来接入联机，少了它就是"客户端技能全失效"级别的 bug。
- **被谁调用 / 调用谁**：引擎（`APawn` 的 `PlayerState` 复制回调）；调用 `InitializeAbilitySystem()`。

#### `void ARPG_Player::InitializeAbilitySystem()`（private）

- **干什么**：建立 GAS 的 ActorInfo 关联、登记输入映射、应用初始属性、授予能力。幂等。
- **关键实现**（按顺序）：
  1. `if (bAbilitySystemInitialized) return;`
  2. `APlayerState* PS = GetPlayerState(); if (!PS)` → `UE_LOG(LogRPG_Ability, Verbose, TEXT("[%s] PlayerState 尚未就绪，GAS 初始化推迟"))` 并 return。
  3. `UAbilitySystemComponent* ASC = GetASCInternal(); if (!ASC)` → `UE_LOG(..., Verbose, TEXT("[%s] 拿不到 ASC，GAS 初始化推迟"))` 并 return。
  4. `ASC->InitAbilityActorInfo(PS, this);`（Owner = PlayerState，Avatar = this）。
  5. `UE_LOG(LogRPG_Ability, Log, TEXT("[%s] GAS 初始化完成（Owner=%s，运行在%s）"), ...)`，第三项是 `HasAuthority() ? TEXT("服务器") : TEXT("客户端")`。
  6. **登记输入映射（两端都做）**：`if (URPG_AbilitySystemComponent* RPGASC = Cast<URPG_AbilitySystemComponent>(ASC)) RPGASC->RegisterInputAbilityMappings(StartupAbilities);` 否则打 `Error` 并 **`return`**（不置 `bAbilitySystemInitialized`）。
  7. **`if (!HasAuthority()) { bAbilitySystemInitialized = true; return; }`**
  8. **应用初始属性**（先属性后能力）：`if (InitAttributesEffect)` → `FGameplayEffectContextHandle Context = ASC->MakeEffectContext(); Context.AddSourceObject(this); const FGameplayEffectSpecHandle SpecHandle = ASC->MakeOutgoingSpec(InitAttributesEffect, 1.f, Context);`（此处有一行被注释掉的 `SpecHandle.Data.Get()->SetSetByCallerMagnitude("InitAttributesEffectTag", 1.f);`）→ `if (SpecHandle.IsValid())` → `ASC->ApplyGameplayEffectSpecToSelf(*SpecHandle.Data.Get())` + Log；否则 `Error` Log。`else` 分支打 `Warning`（`"未配置 InitAttributesEffect，将使用属性集的构造函数默认值"`）。
  9. **授予起始能力**：`if (URPG_AbilitySystemComponent* RPGASC = Cast<URPG_AbilitySystemComponent>(ASC))` → `RPGASC->GrantInputAbilities();` → `for (const TSubclassOf<UGameplayAbility>& PassiveClass : StartupPassiveAbilities) RPGASC->GivePassiveAbility(PassiveClass);`
  10. `bAbilitySystemInitialized = true;`
- **为什么这么写**（注释原文要点）：
  - **为什么幂等而不是只调用一次**（头文件）：因为它的三个调用时机（`PossessedBy` / `OnRep_PlayerState` / `BeginPlay`）**谁先谁后是不确定的** —— 单机下 `PossessedBy` 通常最早，但那时 PlayerState 可能还是空的；联机下 PlayerState 的复制到达时间决定 `OnRep_PlayerState` 的时机；`BeginPlay` 在某些初始化路径下会早于 `PossessedBy`。与其去研究引擎内部的调用顺序（不同版本还会变），不如写成"谁来都行、只生效一次"，这是 UE 里处理初始化的通用做法。
  - PlayerState 未就绪用 `Verbose` 而不是 `Warning`：这是正常的时序竞争，不是错误 —— 避免启动时刷出误导性的警告。
  - `InitAbilityActorInfo(PS, this)` 的参数含义：Owner = PlayerState（能力归属）、Avatar = this（表现载体）。这两个参数决定了 GAS 内部从哪里取"拥有者标签"、GameplayCue 挂在哪、GE 的来源判定算谁 —— 传反了会导致一堆诡异问题（比如 Cue 播在 PlayerState 位置上）。
  - **★ 输入映射表两端都要登记**：这是一张纯配置表（来自角色上的 `StartupAbilities`），不是运行时状态。客户端也必须拿到它 —— 因为 PlayerController 按键时走的是 `TryActivateAbilityByInputTag`，那个函数要靠这张表把输入标签翻译成能力。⚠️ 注释明确记录了历史 bug：这一步原先被放在下面的 `HasAuthority()` 分支**里面**，于是客户端的映射表永远是空的：按任何键都会走到"这个输入标签没有绑定任何能力"，而且**只有在非服务器的机器上才会发生**，单机测试永远发现不了。注意这里只登记、不授予 —— 授予是下一段的事。
  - **为什么 Cast 失败必须 `return`**：⚠️ 必须 return，不能让它继续走到函数末尾把 `bAbilitySystemInitialized` 置上。不 return 的话，后面所有初始化时机都会因为那个标志变成空操作，而唯一的线索就只有上面那一行 `Error` —— 排查时看到的是"初始化只跑了一次、之后再没动静"，很难联想到是这里提前"成功"了。
  - **为什么客户端就此返回**：① 授予能力 —— `GiveAbility` 产生的 `FGameplayAbilitySpec` 会由 ASC 自动复制到客户端，客户端自己也调一次的话会和复制过来那份重复，产生两个同能力实例 —— 症状是"放一次技能触发两遍效果"，而且**只在联机时出现**，极难查。② 初始属性 —— GE 的修改结果会通过属性集复制同步过来，客户端不需要自己算一遍。这是 GAS 联机的基本原则：**状态由服务器产生，客户端只消费复制结果**。客户端唯一要自己做的事是"预测"，那由 GA 的 `NetExecutionPolicy` 负责。注意 `InitAbilityActorInfo` 是**两边都要做**的（已在上面执行）：客户端也需要知道自己的 Owner 是哪个 PlayerState、Avatar 是哪个角色，否则本地预测和 GameplayCue 的定位都会出错。
  - **顺序：先属性、后能力** —— 因为能力激活时往往会读属性（比如耐力够不够），属性没就位会读到 0。
  - 没配 `InitAttributesEffect` 不是致命错误 —— 属性集构造函数里有保底默认值；但要做数值调整就必须配，所以给个警告。
- **被谁调用 / 调用谁**：`BeginPlay()`、`PossessedBy()`、`OnRep_PlayerState()`；调用 `GetPlayerState()`、`GetASCInternal()`、`InitAbilityActorInfo`、`RegisterInputAbilityMappings`、`MakeEffectContext`、`MakeOutgoingSpec`、`ApplyGameplayEffectSpecToSelf`、`GrantInputAbilities`、`GivePassiveAbility`。

#### `FTransform ARPG_Player::GetRespawnTransform() const`

- **干什么**：覆写基类，去 GameMode 要 PlayerStart。
- **关键实现**：
  1. `if (UWorld* World = GetWorld())` → `if (AGameModeBase* GameMode = World->GetAuthGameMode<AGameModeBase>())` → `if (AActor* PlayerStart = GameMode->FindPlayerStart(GetController())) return PlayerStart->GetActorTransform();`
  2. 全部落空 → `UE_LOG(LogRPG_Combat, Warning, TEXT("[%s] 拿不到 PlayerStart，重生回出生位置"), *GetName());` 并 `return Super::GetRespawnTransform();`
- **为什么这么写**（注释原文要点）：
  - `GetAuthGameMode` 只在服务器返回非空 —— 而重生本来就只在服务器跑（`PerformRespawn` 有 `HasAuthority` 判断），所以这里不需要再判一次。
  - 为什么和敌人不一样（头文件）：玩家一开始可能是在半空中生成的（关卡开始播放入场动画），或者是在某个特定的临时位置；而"复活"应该回到关卡的正式入口 —— 也就是 PlayerStart。
  - 走 GameMode 而不是自己遍历 Actor 找 PlayerStart：`FindPlayerStart` 里包含"这个点是否已被占用"之类的引擎逻辑，多人时还能按玩家编号挑不同的 Start。自己实现必然漏掉这些。
  - 注释：没有 GameMode 的场合（比如直接拖一个 Player 进空关卡做单点测试）回落到基类的出生变换 —— 有兜底总比传送回世界原点强。
- **被谁调用 / 调用谁**：`ARPG_BaseCharacter::PerformRespawn()`；调用 `World->GetAuthGameMode<AGameModeBase>()`、`AGameModeBase::FindPlayerStart()`、`Super::GetRespawnTransform()`。

#### `void ARPG_Player::OnRespawned()`

- **干什么**：复活后把控制器的朝向与位置显式推给拥有这个角色的客户端。
- **关键实现**：
  1. `Super::OnRespawned();`
  2. `AController* Ctrl = GetController(); if (!Ctrl) return;`
  3. `Ctrl->SetControlRotation(GetActorRotation());`
  4. `Ctrl->ClientSetLocation(GetActorLocation(), GetActorRotation());`
- **为什么这么写**（长注释，含引擎引用，原文要点）：
  - ★ 位置和视角都必须**显式推给拥有这个角色的客户端**。本地控制的角色在客户端是 **AutonomousProxy**，它由客户端预测驱动，服务器只负责纠偏。这意味着下面两样东西**都不会自动同步过去**：
    - **① 位置** —— `AActor::ReplicatedMovement` 的复制条件是 **`COND_SimulatedOnly`**，根本不会发给 AutonomousProxy。走的是"客户端发 `ServerMove` → 服务器发现对不上 → `ClientAdjustPosition`"。服务器传送完，客户端要等下一次移动纠偏才知道。玩家站着不动时这个延迟尤其明显 —— 表现是"复活后还站在死亡地点，过一会儿才被拽回出生点"。
    - **② 视角** —— `AController::ControlRotation` **压根不是复制属性**（`Controller.cpp:826-835` 只注册了 PlayerState 和 Pawn）。服务器调 `SetControlRotation` 只改到自己那份，引擎给客户端送视角走的是 `ClientSetRotation` 这个 RPC（`Controller.cpp:469-476`）。
  - `ClientSetLocation` 一次解决两件事（它的实现就是 `ClientSetRotation` + `TeleportTo`，`Controller.cpp:455-462`）。引擎自己的 `RestartPlayerAtTransform` 走的是同一条思路 —— 它干脆换一个新 Pawn 靠占有复制来解决；我们复用 Pawn，所以要显式推一次。
  - 服务器侧的 `SetControlRotation` 仍然要调：主机自己的玩家就在这台机器上，他的视角是真需要改的。
  - 头文件补充：不做这件事的话，复活瞬间镜头还指着死亡时看的方向 —— 玩家会有一小段时间分不清自己在哪、面朝哪。
- **被谁调用 / 调用谁**：`ARPG_BaseCharacter::PerformRespawn()` 的 ③；调用 `AController::SetControlRotation`、`AController::ClientSetLocation`。

#### `UAbilitySystemComponent* ARPG_Player::GetASCInternal() const`

- **干什么**：去 PlayerState 上取 ASC。
- **关键实现**：`if (const ARPG_PlayerState* RPGPlayerState = Cast<ARPG_PlayerState>(GetPlayerState())) return RPGPlayerState->GetAbilitySystemComponent(); return nullptr;`
- **为什么这么写**（注释原文）：ASC 在 PlayerState 上。用 `Cast` 而不是 `UAbilitySystemBlueprintLibrary`，是为了拿到 `ARPG_PlayerState` 的具体类型，语义更明确、也少一次接口查找。
- **被谁调用 / 调用谁**：基类的 `GetAbilitySystemComponent()` / `GetRPGAttributeSet()` / `IsAlive()` / `SetCombatMovement()` / `ResetForRespawn()`；调用 `ARPG_PlayerState::GetAbilitySystemComponent()`。

#### 成员变量

| 变量 | 标记 | 类型 / 默认值 | 说明 |
| --- | --- | --- | --- |
| `bAbilitySystemInitialized` | private，无 UPROPERTY | `bool = false` | 是否已完成初始化，保证授予能力和应用初始属性只做一次（头文件注释） |

---

### `Source/RPG/Character/RPG_Enemy.h` / `RPG_Enemy.cpp`

**一句话职责**：敌人角色，**自持 ASC**（`URPG_AbilitySystemComponent` + `URPG_AttributeSet` 都是自己的子对象），并在死亡 / 重生时停 / 启 AI。

**类/结构体**：

- `ARPG_Enemy` —— 继承 `ARPG_BaseCharacter`（`UCLASS()`）。

头文件开头的两段长注释（原文要点）：

- **为什么敌人不像玩家那样把 ASC 放 PlayerState**：敌人没有"重生后要保留状态"的需求 —— 死了就是销毁，冷却和 Buff 一起消失正合适。放自己身上少一层间接寻址，也少一个需要维护生命周期的外部对象。这里的"不对称"（玩家放 PlayerState、敌人放自己）不是设计缺陷，而是**按各自生命周期特征做的最优解**。接口层的存在让调用方感知不到这个差异。
- **AI 与玩家走同一条能力触发路径**：敌人也使用 `StartupAbilities`（输入标签 → 能力类）这套映射，只是触发者从"玩家按键"变成了"AI 决策"。好处：同一个 GA 类玩家和敌人共用，行为绝对一致；不会出现"玩家能打出来、AI 打不出来"这类只在一边出现的 bug；调试时用同一套日志和 GameplayDebugger 视图。

#### `ARPG_Enemy::ARPG_Enemy()`

- **干什么**：创建自己的 ASC 与属性集子对象，并配置 AI 自动接管。
- **关键实现**：
  1. `AbilitySystemComponent = CreateDefaultSubobject<URPG_AbilitySystemComponent>(TEXT("AbilitySystemComponent"));`
  2. `AttributeSet = CreateDefaultSubobject<URPG_AttributeSet>(TEXT("AttributeSet"));`
  3. `AIControllerClass = ARPG_AIController::StaticClass();`
  4. `AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;`
- **为什么这么写**（注释原文要点）：
  - 属性集的登记放在 `InitializeAbilitySystem` 里做，而不是构造函数。原因是构造函数可能对 CDO 执行多次，而 `AddSpawnedAttribute` 会改动内部数组；放到初始化函数里能保证"每个实例只登记一次"。
  - AI 接管**必须两个都设**，少一个敌人都会站着不动：`AIControllerClass` —— 用哪个大脑；`AutoPossessAI` —— 什么时候自动接管。只设 `AIControllerClass` 的话，敌人会一直等一个永远不会来的 PlayerController；而"没被占有"在引擎看来是完全正常的状态，所以**不报任何错** —— 又一个静默失败。
- **被谁调用 / 调用谁**：引擎（CDO 与实例构造）。

#### `void ARPG_Enemy::BeginPlay()`

- **干什么**：`Super::BeginPlay()` 后调 `InitializeAbilitySystem()`。
- **关键实现**：Super + 一句 `InitializeAbilitySystem();`
- **为什么这么写**：注释未说明（但由头文件的幂等设计推断，见 `InitializeAbilitySystem`）。
- **被谁调用 / 调用谁**：引擎；调用 `InitializeAbilitySystem()`。

#### `void ARPG_Enemy::PossessedBy(AController* NewController)`

- **干什么**：`Super::PossessedBy(NewController)` 后调 `InitializeAbilitySystem()`。
- **关键实现**：Super + 一句 `InitializeAbilitySystem();`
- **为什么这么写**（注释原文）：敌人的 ASC 与"谁在控制它"无关，所以这里只是多一个保险的初始化时机。写在 `PossessedBy` 里的实际价值是：AI 逻辑一旦开始跑（`PossessedBy` 之后），能力一定已经就位了。
- **被谁调用 / 调用谁**：引擎（`APawn::PossessedBy`）；调用 `InitializeAbilitySystem()`。

#### `URPG_AbilitySystemComponent* ARPG_Enemy::GetRPGAbilitySystemComponent() const`（`UFUNCTION(BlueprintPure)`，内联）

- **干什么**：拿到敌人自己的 ASC（供 AI 任务/服务使用）。
- **关键实现**：`return AbilitySystemComponent;`
- **为什么这么写**：注释写"供 AI 任务/服务使用"。
- **被谁调用 / 调用谁**：AI 任务 / 服务（蓝图侧）。

#### `UAbilitySystemComponent* ARPG_Enemy::GetASCInternal() const`

- **干什么**：返回自己的 ASC，实现基类要求的 GAS 接入点。
- **关键实现**：一行 `return AbilitySystemComponent;`
- **为什么这么写**：注释未说明（头文件说明敌人是 Owner == Avatar == 自己）。
- **被谁调用 / 调用谁**：基类的 `GetAbilitySystemComponent()` / `GetRPGAttributeSet()` / `IsAlive()` / `SetCombatMovement()` / `ResetForRespawn()`。

#### `void ARPG_Enemy::OnDeathStarted()`

- **干什么**：死亡时停掉 AI。
- **关键实现**：`Super::OnDeathStarted();` → `if (ARPG_AIController* AIController = Cast<ARPG_AIController>(GetController())) AIController->StopAI();`
- **为什么这么写**（注释原文要点）：
  - 敌人的"大脑"和"身体"是两个对象，死亡时两边都要处理：身体 → `GA_Death` 已经在做了（挂 `State.Dead`、进布娃娃）；大脑 → 就是这里。只处理身体不处理大脑的话，行为树会对着尸体继续发指令。
  - 头文件：为什么这件事必须由敌人的代码来做，而不是让行为树自己发现 —— 行为树没有"我死了"这个概念 —— 不主动停，它会一直写黑板、发起寻路、尝试激活攻击能力。而那时移动模式已被布娃娃关掉，寻路每次都失败，行为树会卡在一个永不结束的 Latent Task 上。这两个函数由 `GA_Death` 通过基类的钩子调用，只在服务器发生。
- **被谁调用 / 调用谁**：`GA_Death` → `ARPG_BaseCharacter::OnDeathStarted()` 的虚派发；调用 `ARPG_AIController::StopAI()`。

#### `void ARPG_Enemy::OnRespawned()`

- **干什么**：重生时重启 AI。
- **关键实现**：`Super::OnRespawned();` → `if (ARPG_AIController* AIController = Cast<ARPG_AIController>(GetController())) AIController->RestartAI();`
- **为什么这么写**（注释原文要点）：
  - 和 `OnDeathStarted` 严格配对。少了这一步，敌人复活后会站着不动 —— 行为树在死亡时被停了，没人把它拉起来，而且不会有任何报错。
  - 基类的 `PerformRespawn` 会先 `ResetForRespawn()` 再传送，所以走到这里时 `State.Dead` 已经摘掉、属性已经回满 —— AI 重新开始跑的时候看到的是一个健康的敌人。
  - 头文件：只停不重启的话，敌人复活后会站着不动，且不报错。
- **被谁调用 / 调用谁**：`ARPG_BaseCharacter::PerformRespawn()` 的 ③；调用 `ARPG_AIController::RestartAI()`。

#### `void ARPG_Enemy::InitializeAbilitySystem()`

- **干什么**：登记属性集、建立 ActorInfo 关联、登记输入映射、应用初始属性、授予能力。幂等。
- **关键实现**（按顺序）：
  1. `if (bAbilitySystemInitialized) return;`
  2. `if (!AbilitySystemComponent || !AttributeSet)` → `UE_LOG(LogRPG_Ability, Error, TEXT("[%s] ASC 或属性集为空，无法初始化 GAS"))` 并 return。
  3. **登记属性集（先查重）**：`if (!AbilitySystemComponent->GetSet<URPG_AttributeSet>()) AbilitySystemComponent->AddSpawnedAttribute(AttributeSet);`
  4. `AbilitySystemComponent->InitAbilityActorInfo(this, this);`（Owner == Avatar == 自己）
  5. `UE_LOG(LogRPG_Ability, Log, TEXT("[%s] GAS 初始化完成（Owner 与 Avatar 均为自身，运行在%s）"), ...)`。
  6. `AbilitySystemComponent->RegisterInputAbilityMappings(StartupAbilities);`（两端都登记）
  7. `if (!HasAuthority()) { bAbilitySystemInitialized = true; return; }`
  8. **初始属性**：`if (InitAttributesEffect)` → `Context = MakeEffectContext(); Context.AddSourceObject(this); const FGameplayEffectSpecHandle SpecHandle = MakeOutgoingSpec(InitAttributesEffect, 1.f, Context);` → `if (SpecHandle.IsValid())` → `ApplyGameplayEffectSpecToSelf(*SpecHandle.Data.Get())` + Log。
  9. **起始能力**：`AbilitySystemComponent->GrantInputAbilities();`
  10. `for (const TSubclassOf<UGameplayAbility>& PassiveClass : StartupPassiveAbilities) AbilitySystemComponent->GivePassiveAbility(PassiveClass);`
  11. `bAbilitySystemInitialized = true;`
- **为什么这么写**（注释原文要点）：
  - 登记前先查一次的理由：避免重复登记（重复登记会让同一个属性集在 ASC 里出现两次，修改时只改到其中一个，症状是"数值有时对有时不对"）。
  - `InitAbilityActorInfo(this, this)`：敌人是 Owner == Avatar == 自己。对比玩家：Owner = PlayerState、Avatar = 角色 —— 这正是不对称之处。
  - **敌人其实用不上输入映射表** —— AI 只在服务器跑，客户端上的敌人是模拟代理。仍然两端都登记，是为了让"敌人的 GAS 初始化"和玩家完全同构：唯一的差异就是下面那道 `HasAuthority()` 闸门，而不是"这里少一段、那里多一段"。不同构的代价是以后每次改初始化流程都要想两遍。
  - 以下只在服务器执行：敌人由 AI 驱动，所有状态都在服务器产生、再复制给各客户端。客户端上的敌人实例只需要正确的 ActorInfo（供动画和 GameplayCue 定位），不需要自己授予能力或应用初始属性 —— 理由与玩家一致，详见 `RPG_Player.cpp`。
  - 先属性后能力的理由：注释写"理由同玩家"（即能力激活时会读属性）。
  - 起始能力：敌人用与玩家完全相同的映射机制，只是触发者会是 AI 而不是按键。映射表在上面已经登记过了，这里只负责授予。
  - 被动能力同样要授予 —— 敌人也需要耐力恢复。
- **与 `ARPG_Player::InitializeAbilitySystem()` 的实现差异**（逐条对照读出的）：敌人的 `InitAttributesEffect` 分支**没有 `else` Warning**，`SpecHandle` 无效时**没有 `Error` 日志**；`GrantInputAbilityMappings` 前**没有** `Cast` 失败的 `Error` + `return`（因为 `AbilitySystemComponent` 的静态类型就是 `URPG_AbilitySystemComponent*`，不需要 Cast）；属性集登记是本函数独有的一步。
- **被谁调用 / 调用谁**：`BeginPlay()`、`PossessedBy()`；调用 `GetSet<URPG_AttributeSet>()`、`AddSpawnedAttribute`、`InitAbilityActorInfo`、`RegisterInputAbilityMappings`、`MakeEffectContext`、`MakeOutgoingSpec`、`ApplyGameplayEffectSpecToSelf`、`GrantInputAbilities`、`GivePassiveAbility`。

#### 成员变量

| 变量 | 标记 | 类型 / 默认值 | 说明 |
| --- | --- | --- | --- |
| `AbilitySystemComponent` | `VisibleAnywhere, BlueprintReadOnly` | `TObjectPtr<URPG_AbilitySystemComponent>` | 敌人自持的 ASC（在构造函数里建） |
| `AttributeSet` | `UPROPERTY()`（无访问修饰） | `TObjectPtr<URPG_AttributeSet>` | 敌人自持的属性集；登记时机见 `InitializeAbilitySystem()` |
| `PatrolPoints` | `EditInstanceOnly, BlueprintReadOnly` | `TArray<TObjectPtr<AActor>>` | 巡逻点。注释：在关卡里摆几个 `TargetPoint`，然后在这个敌人的实例上拖进来。用 `EditInstanceOnly`：每个敌人可以有不同的巡逻路线，但不需要为每只敌人都做蓝图子类 |
| `AttackRange` | `EditDefaultsOnly, BlueprintReadOnly` | `float = 250.f` | 进入这个距离（厘米）内，AI 就认为可以发起攻击 |
| `LoseTargetDistance` | `EditDefaultsOnly, BlueprintReadOnly` | `float = 2500.f` | 脱战距离：目标跑出这个距离且丢失视野一段时间后，返回巡逻 |
| `bAbilitySystemInitialized` | private，无 UPROPERTY | `bool = false` | 保证初始化只跑一次 |

---

### `Source/RPG/Animation/RPG_AnimInstanceBase.h` / `RPG_AnimInstanceBase.cpp`

**一句话职责**：玩家与敌人动画蓝图的共同父类 —— 每帧把 `CharacterMovement` 与 `GameplayTag` 翻译成动画能懂的布尔量/浮点量，供 `ABP_RPG_Base` 的 AnimGraph 直接连线。

**类/结构体**：

- `URPG_AnimInstanceBase` —— 继承 `UAnimInstance`（`UCLASS()`）。
- `URPG_AnimInstanceBase::FMontagePlayRecord` —— 内嵌结构体，一条蒙太奇播放记录（见下文）。

头文件开头的架构说明（原文要点）：

```
CharacterMovement / GameplayTag          ← 真相源
            ↓  每帧读一次
URPG_AnimInstanceBase（本类）             ← 翻译成动画能懂的布尔量
            ↓
ABP_RPG_Base 的 AnimGraph                ← 状态机 + 混合空间
```

- 关键约定：**AnimGraph 里不写任何计算**。所有"当前速度多少""在不在攻击"的判断都在 C++ 里做完，蓝图只负责连线。原因：蓝图里的计算没法做代码审查、没法 diff、改错了查不出来；状态逻辑和表现连线混在一起时，"动画不对"到底是逻辑错了还是线接错了根本分不清。

- **为什么状态一律从 GameplayTag 读，而不是让 GA 推变量**（三个必踩的坑）：
  1. **被打断时不会复位**。GA 被 `CancelAbilitiesWithTag` 强行取消时走的是 `EndAbility` 的另一条分支，手写的复位语句很容易漏。症状是"角色卡在攻击姿势"，而且只在被特定招式打断时出现。
  2. **联机下两边不同步**。客户端预测激活、服务器拒绝，变量就永远错了。
  3. **两份真相**。标签已经是真相源了，再存一份 bool 就要维护一致性。
  - 读标签天然免疫这三点：标签的生命周期由 GAS 托管，能力结束、被打断、角色死亡清空能力，标签都会被自动摘干净。代价是每帧要做几次标签查询 —— 都是哈希表查找，量级可忽略。

#### `URPG_AnimInstanceBase::URPG_AnimInstanceBase()`

- **干什么**：构造函数有意留空。
- **关键实现**：函数体为空。
- **为什么这么写**（注释原文）：这里有意什么都不设。特别提醒：**不要**在这里写 `bUseMultiThreadedAnimationUpdate = false`。网上有些教程为了"避免多线程问题"把它关掉，那是用性能换省事。本类的 `NativeUpdateAnimation` 只在游戏线程写成员变量，AnimGraph 只在动画线程读，没有共享可变状态，天然是安全的。

#### `void URPG_AnimInstanceBase::NativeInitializeAnimation()`

- **干什么**：绑定蒙太奇生命周期委托；尝试缓存所属角色。
- **关键实现**：
  1. `Super::NativeInitializeAnimation();`
  2. `OnMontageStarted.AddUniqueDynamic(this, &URPG_AnimInstanceBase::HandleMontageStarted);`
  3. `OnMontageEnded.AddUniqueDynamic(this, &URPG_AnimInstanceBase::HandleMontageEnded);`
  4. `CacheOwnerCharacter();`
- **为什么这么写**（注释原文要点）：
  - 这两个委托是**动态多播**，所以用 `AddUniqueDynamic` + `UFUNCTION` 回调。它们覆盖了蒙太奇的主要结束路径（自然播完 / 被别的蒙太奇顶掉 / 能力结束），所以不需要在 GA 那边再埋点。
  - ⚠️ **必须用 `AddUniqueDynamic` 而不是 `AddDynamic`**：`AddDynamic` **不查重** —— 它展开成 `AddInternal`，实现是无条件 `InvocationList.Add(...)`（`ScriptDelegates.h`），只在 `DO_ENSURE` 构建里先 `ensure` 一下。而 `UAnimInstance::InitializeAnimation()` 第一句就是 `UninitializeAnimation()`，且**同一个实例可以被重复初始化**（`USkeletalMeshComponent::InitializeAnimScriptInstance()` 在"实例已存在 + `bForceReinit`"时直接对现有实例再调一次，`bForceReinit` 来自重新注册）。一旦重入：两个回调各绑两份 → 每段蒙太奇打两条日志、`ensure` 报红，而日志的"实播"数字会被第二条 `-1.00s` 污染，正好把诊断本身弄坏。
  - `CacheOwnerCharacter()` 的时机说明：这时角色可能还没 Possess 完 / 还没 BeginPlay，拿不到也不报错 —— `NativeUpdateAnimation` 每帧都会再试一次（注释原文）。
- **被谁调用 / 调用谁**：引擎（`UAnimInstance::InitializeAnimation()` 调用它，而后者第一句是 `UninitializeAnimation()`）；调用 `AddUniqueDynamic`、`CacheOwnerCharacter()`。

#### `void URPG_AnimInstanceBase::NativeUpdateAnimation(float DeltaSeconds)`

- **干什么**：每帧刷新所有动画驱动量。
- **关键实现**：
  1. `Super::NativeUpdateAnimation(DeltaSeconds);`
  2. `if (!OwnerCharacter.IsValid()) { CacheOwnerCharacter(); if (!OwnerCharacter.IsValid()) return; }`
  3. **★ 先 `UpdateCombatState();` 再 `UpdateLocomotion(DeltaSeconds);`**
- **为什么这么写**（注释原文要点）：
  - 兜底重取的理由：AnimInstance 的初始化时机和角色初始化时机**不保证有先后**：蓝图里换 Mesh、运行时换 SkeletalMesh、编辑器预览，都会让 `InitializeAnimation` 早于角色就绪。
  - ★ 顺序有讲究：先战斗、后移动。`UpdateLocomotion` 里判定 `MovementState` 时要读 `bIsSprinting`，而那个值在 `UpdateCombatState` 里才算出来。反过来的话，冲刺状态永远慢一帧 —— 表现为"起步瞬间播的是跑步动画"。
  - `DeltaSeconds` 目前用不到，但保留参数是为了将来做速度插值（比如"停下时 `Direction` 缓慢归零"这种平滑处理）。
- **被谁调用 / 调用谁**：引擎（动画更新流程）；调用 `CacheOwnerCharacter()`、`UpdateCombatState()`、`UpdateLocomotion()`。

#### `void URPG_AnimInstanceBase::CacheOwnerCharacter()`（private）

- **干什么**：缓存所属角色引用。
- **关键实现**：一行 `OwnerCharacter = Cast<ARPG_BaseCharacter>(TryGetPawnOwner());`
- **为什么这么写**（注释原文）：`TryGetPawnOwner` 在编辑器预览窗口（没有真实 Pawn）会返回 nullptr，所以这里判空是正常路径，不是错误。
- **被谁调用 / 调用谁**：`NativeInitializeAnimation()`、`NativeUpdateAnimation()`。

#### `void URPG_AnimInstanceBase::UpdateLocomotion(float /*DeltaSeconds*/)`（private）

- **干什么**：从 `CharacterMovement` 读位移数据，算出速度、方向、垂直速度与 `MovementState`。
- **关键实现**（按顺序）：
  1. `ARPG_BaseCharacter* Character = OwnerCharacter.Get(); if (!Character) return;`
  2. `const UCharacterMovementComponent* Movement = Character->GetCharacterMovement(); if (!Movement) return;`
  3. `const FVector Velocity = Movement->Velocity;`
  4. `Speed = Velocity.Size2D();` —— **水平**速度。
  5. `const float CurrentMaxSpeed = Character->bIsCrouched ? Movement->MaxWalkSpeedCrouched : Movement->MaxWalkSpeed;` → `MaxSpeed = CurrentMaxSpeed;`
  6. `SpeedRatio = (CurrentMaxSpeed > KINDA_SMALL_NUMBER) ? FMath::Clamp(Speed / CurrentMaxSpeed, 0.f, 1.f) : 0.f;`
  7. 方向：`const FVector Velocity2D(Velocity.X, Velocity.Y, 0.f);` → `if (!Velocity2D.IsNearlyZero())` → `const FVector LocalVelocity = FRotationMatrix(Character->GetActorRotation()).InverseTransformVector(Velocity2D);` → `Direction = FMath::RadiansToDegrees(FMath::Atan2(LocalVelocity.Y, LocalVelocity.X));`（**速度为 0 时保留上一帧的 `Direction`，不归零**）。
  8. `VerticalVelocity = Velocity.Z; bIsInAir = Movement->IsFalling(); bIsCrouching = Character->bIsCrouched;`
  9. `MovementState` 判定（顺序即优先级）：`if (bIsInAir)` → `InAir`；`else if (bIsCrouching)` → `Crouching`；`else if (bIsSprinting && Speed > IdleSpeedThreshold)` → `Sprinting`；`else` → `Grounded`。
- **为什么这么写**（注释原文要点）：
  - 用 `Size2D` 而不是 `Size`：跳跃上升时 `Velocity.Z` 有几百，混进去会让"边跳边前进"被判成超高速移动，混合空间直接跳到冲刺段。
  - **★ 归一化速度的分母**：蹲伏时 UE 用的是另一个字段（`MaxWalkSpeedCrouched`），直接拿 `MaxWalkSpeed` 会让"蹲着走"的比值停在 180/300 = 0.6，混合空间卡在半速位置，看着像"蹲着慢跑"。
  - **除零保护**：`MaxWalkSpeed` 理论上不会为 0，但蓝图里手滑改成 0 是常见操作，而 0 除法的结果是 NaN —— NaN 进混合空间会让角色整个消失（顶点全变 NaN），现象非常吓人且极难定位到"原来是速度设成 0 了"。
  - 方向角的约定与实现：把世界坐标的速度转成"相对角色朝向"的局部向量，再取角度。约定与引擎的 `UKismetAnimationLibrary::CalculateDirection` 完全一致：正前 0° / 正右 +90° / 正左 -90° / 正后 ±180°。保持一致很重要 —— 这样从任何教程里抄来的方向混合空间都能直接对得上，不用反着配一遍角度。自己算而不是直接调引擎函数，是为了不额外依赖 `AnimGraphRuntime` 模块；数学只有三行，不值得为它加一个模块依赖。
  - `InverseTransformVector` 只旋转不平移 —— 我们要的是方向不是位置，用 `InverseTransformPosition` 会把角色坐标也减进去，结果完全错。
  - 速度为 0 时不归零 `Direction`：归零的话，角色停下的一瞬间方向会"啪"地弹回正前方，而这时混合空间还在往 Idle 淡出 —— 表现为停步时腿抽一下。
  - `MovementState` 判定顺序 = 优先级：腾空优先于一切 —— 从空中落下时就算还按着冲刺键，也该播下落动画；蹲着从平台边缘掉下去同理。
  - **★ `bIsSprinting && Speed > IdleSpeedThreshold` 两个条件都要**（大段注释）：
    - 光看标签会出一个很显眼的问题：`State.Sprinting` 在"冲刺/追击期间"是**常驻**的，人站着不动它也在。这时 `MovementState` 会是 `Sprinting` → 动画蓝图用「跑步」那条状态机 → 那条状态机的混合空间拿 `SpeedRatio` 当横轴，而站着时 `SpeedRatio = 0` → 播的是混合空间**最慢的那个采样点**（通常是走路）→ 表现就是**原地踏步**。
    - 两个具体症状（同一个根因）：敌人攻击后站在 Wait 里 → 走两步；玩家站着按 Shift → 进入"行走"，松开才回 idle。
    - 为什么阈值用 `IdleSpeedThreshold`（而不是另设一个大的）：它表达的是"算不算停下来了"，和速度归零的判断用同一把尺子，免得出现"动画说没停、但 `SpeedRatio` 已经是 0"这种前后不一致。
    - 至于"会不会因为阈值抖动"——不会：进入跑步仍然由**标签**决定（离散事件，按 Shift 才跳变），速度条件只负责"停下来时退出"这一个方向。
- **被谁调用 / 调用谁**：`NativeUpdateAnimation()`；调用 `GetCharacterMovement()`、`FMath::Clamp`、`FMath::RadiansToDegrees` / `FMath::Atan2`。

#### `void URPG_AnimInstanceBase::UpdateCombatState()`（private）

- **干什么**：从 GameplayTag 与 `CombatComponent` 读全部战斗状态。
- **关键实现**（按顺序）：
  1. `ARPG_BaseCharacter* Character = OwnerCharacter.Get(); if (!Character) return;`
  2. **每帧重新取 ASC，不做缓存**：`UAbilitySystemComponent* ASC = Character->GetAbilitySystemComponent();`
  3. `if (!ASC)` → 把 7 个状态量**全部显式清零**（`bIsAttacking = false; bIsDodging = false; bIsSprinting = false; bIsCharging = false; bIsInvulnerable = false; bIsDead = false; ChargeLevel = 0;`）后 `return`。
  4. 六个标签查询：`bIsAttacking = ASC->HasMatchingGameplayTag(RPGTags::State_Attacking);`、`bIsDodging = ...State_Dodging`、`bIsSprinting = ...State_Sprinting`、`bIsCharging = ...State_Attack_Charging`、`bIsInvulnerable = ...State_Invulnerable`、`bIsDead = ...State_Dead`。
  5. `ChargeLevel`：从高往低查 —— `Lv3` → `3`；`else if Lv2` → `2`；`else if Lv1` → `1`；`else` → `0`。
  6. 模组与连段：`if (const URPG_CombatComponent* Combat = Character->GetCombatComponent())` → `ComboIndex = Combat->GetComboIndex();` → `if (const URPG_AttackModuleData* Module = Combat->GetAttackModule()) AttackModuleType = Module->ModuleType;`
- **为什么这么写**（注释原文要点）：
  - **每帧重新取 ASC 不缓存的理由**：玩家的 ASC 挂在 PlayerState 上，而 PlayerState 会随重生、关卡切换、联机重连被整个替换掉。缓存 `WeakObjectPtr` 的话必须自己处理失效时机 —— 漏一个就是"复活之后动画再也不对了"，而且只在特定流程下复现。代价只是一次 `Cast` + 一次虚函数调用，一帧一次完全可忽略。拿一点点性能换掉一整类时序 bug，是划算的。
  - `!ASC` 分支为什么必须逐个赋值而不是提前 return：注释原文 —— "注意不能提前 return 而不赋值：成员变量会保留上一次的值，角色池复用（比如重生）时就会带着上个角色的攻击姿态出现。"
  - `HasMatchingGameplayTag` 是**层级匹配**：容器里有某个子标签时，查它的父标签同样返回 true。所以查 `State.Attacking` 对 `State.Attack.Windup` / `Active` / `Recovery` 都成立；蓄力挂的是 `State.Attack.Charging.Lv2`，查 `State.Attack.Charging` 也是 true。这让我们能用"粗粒度"的查询做动画分支，不必逐个枚举子标签。
  - `ChargeLevel` 从高往低查的理由：高段位标签存在时低段位一定不存在（GA 每次升段会先摘掉上一段的标签），所以这里不需要考虑"同时挂着 Lv1 和 Lv3"的情况。
  - **攻击模组与连段索引为什么走 `CombatComponent` 而不是标签**：这两个**不是** GameplayTag，因为它们不是"状态"而是"配置"和"进度" —— 当前拿什么武器是配置，换武器时才变；打到第几段是进度，0~5 的连续量，标签表达不了。
- **被谁调用 / 调用谁**：`NativeUpdateAnimation()`（必须早于 `UpdateLocomotion`）；调用 `ARPG_BaseCharacter::GetAbilitySystemComponent()`、`HasMatchingGameplayTag`、`GetCombatComponent()`、`URPG_CombatComponent::GetComboIndex()`、`GetAttackModule()`、`URPG_AttackModuleData::ModuleType`。
- **实现细节**：`ComboIndex` 与 `AttackModuleType` 在 `CombatComponent` 为空时**不会被重置**（保留上一帧的值），这与 `!ASC` 分支的"全部清零"处理方式不同。

#### `void URPG_AnimInstanceBase::HandleMontageStarted(UAnimMontage* Montage)`（`UFUNCTION()`）

- **干什么**：为刚开播的蒙太奇登记一条播放记录。
- **关键实现**：
  1. `if (!Montage) return;`
  2. `float PlayRate = 1.f; if (const FAnimMontageInstance* Instance = GetActiveInstanceForMontage(Montage)) PlayRate = Instance->GetPlayRate();`
  3. `FMontagePlayRecord& Record = MontagePlayRecords.FindOrAdd(Montage).AddDefaulted_GetRef();` → `Record.StartTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;` → `Record.Length = Montage->GetPlayLength();` → `Record.PlayRate = PlayRate;`
  4. `UE_LOG(LogRPG_Animation, Verbose, TEXT("[%s] 蒙太奇开始：%s（全长 %.2fs）"), ...)`。
- **为什么这么写**（注释原文要点）：
  - 记 `PlayRate` 的原因：判据要用它把"蒙太奇秒"换算成"墙钟秒"。`OnMontageStarted` 是在实例建好之后才广播的，所以这里能取到实例。取不到就按 1.0 算（保守：只会让判据更严格，不会漏报）。
  - 为什么按蒙太奇登记独立记录：不能只存"当前那一个"，连段切段时两段会在同一帧重叠；同一资产也可能有多个实例（见头文件对 `MontagePlayRecords` 的说明）。
  - 用 Verbose 而不是 Log：一次攻击会打好几条，正常游戏时没必要刷屏。排查手感问题时 `Log LogRPG_Animation Verbose` 打开即可。
- **被谁调用 / 调用谁**：`UAnimInstance::OnMontageStarted` 动态多播委托（在 `NativeInitializeAnimation` 里用 `AddUniqueDynamic` 绑定）；调用 `GetActiveInstanceForMontage`、`FAnimMontageInstance::GetPlayRate`。

#### `void URPG_AnimInstanceBase::HandleMontageEnded(UAnimMontage* Montage, bool bInterrupted)`（`UFUNCTION()`）

- **干什么**：配对播放记录，算出"实播时长 vs 应播时长"，明显偏短时打 Warning（可选打印调用栈）。
- **关键实现**：
  1. `if (!Montage) return;`
  2. **取最早一条记录（FIFO）**：`if (TArray<FMontagePlayRecord>* Records = MontagePlayRecords.Find(Montage))` → `if (Records->Num() > 0) { Record = (*Records)[0]; Records->RemoveAt(0); bHasRecord = true; }` → `if (Records->Num() == 0) MontagePlayRecords.Remove(Montage);`
  3. `const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;`
  4. `const float PlayedFor = bHasRecord ? (Now - Record.StartTime) : -1.f;`
  5. `const float ExpectedWallClock = bHasRecord ? Record.Length / FMath::Max(Record.PlayRate, KINDA_SMALL_NUMBER) : 0.f;`
  6. 判据：`bPlayedMostOfIt = bHasRecord && ExpectedWallClock > KINDA_SMALL_NUMBER && PlayedFor >= ExpectedWallClock * MontageCutShortWarnRatio;` → `bCutShort = bHasRecord && !bPlayedMostOfIt;`
  7. `RateNote`：`(bHasRecord && !FMath::IsNearlyEqual(Record.PlayRate, 1.f))` 时生成 `"（全长 %.2fs @ Rate %.2f）"`，否则空串。
  8. `if (bCutShort)` → `UE_LOG(LogRPG_Animation, Warning, TEXT("[%s] 蒙太奇被提前结束：%s —— 实播 %.2fs / 应播 %.2fs（%.0f%%）%s，bInterrupted=%s"), ...)`；并且 `if (CVarRPGLogMontageInterruptStack.GetValueOnGameThread() != 0) FDebug::DumpStackTraceToLog(ELogVerbosity::Warning);`
  9. `else` → `UE_LOG(LogRPG_Animation, Verbose, TEXT("[%s] 蒙太奇结束：%s（实播 %.2fs / 应播 %.2fs%s，bInterrupted=%s）"), ...)`（`bHasRecord` 为 false 时打 `-1.f`）。
- **为什么这么写**（注释原文要点）：
  - 为什么取最早一条：同一份资产可能同时/连续有多个实例（受击蒙太奇池小的时候很容易连取两次同一个）。`Montage_Play` 的顺序是"先停同组旧的 → 建新实例 → 广播 Started"，所以旧实例的 `Ended` 一定**先**到，它要配的就是最早那条记录。取最新那条的话算出来会是"实播 0.02s" —— 正是这个诊断工具当初要消灭的东西。取不到说明我们没见到它的开始（比如它在本 AnimInstance 初始化之前就在播了）—— 这种情况下老实说"没记录到"，而不是拿别人的数字硬凑。
  - 为什么要把蒙太奇秒换成墙钟秒：否则配了播放速率就全是假警报。例：全长 1.77s、Rate = 2.0 → 正常播完就是 0.885s 墙钟，拿 1.77 当基准会把每一次正常播完都报成"被提前结束 50%"。
  - ★ 这条日志是给"出招动画有顿挫感"准备的："顿挫"没法直接查，但可以变成数字：**它实际播了多久 vs 它本该播多久**。正常播完时 `PlayedFor ≈ 全长`（差一点点是混合时间的正常误差）。如果明显短于全长，说明**有人在动画播完之前把它停掉了** —— 那就不是"网络卡"，而是要找"是谁停的"。
  - ⚠️ 注意区分两类"被停"：**设计如此** —— 轻击连段换段时，上一段就是被下一段顶掉的（`GA_LightAttack::StartSegment` 里的 `StopCurrentSegmentMontage`）；**可疑** —— 最后一段（`AM_Light_05`）没有下一段可接，它被打断就说明有别的东西在动手。所以这条日志的价值在"看第几段被打断、打断了多少"，不在"有没有被打断"。
  - `bInterrupted` 是引擎直接给的判据，不是我们算的 —— 它永远可信。
  - ⚠️ 但**不能只看 `bInterrupted`**：`PlayMontageOrSkip` 建任务时用的是 `bStopWhenAbilityEnds = true`，能力一结束它就会停掉蒙太奇 —— 哪怕这段动画**已经播完了**，引擎也会报 `bInterrupted=true`。第一版日志里那些"实播 1.77s / 全长 1.77s（100%）但 `bInterrupted=true`"就是这么来的：**假警报**，把真正的问题淹掉了。所以判据是"实际播的时长明显短于全长"，`bInterrupted` 只作为补充说明。
  - 打 `RateNote` 的理由：否则看到"实播 0.88s / 应播 0.88s"会不知道为什么和蒙太奇全长对不上。
  - `else` 分支包含两种情况，都是**正常**的：自然播完；播完了、随后被"能力结束"顺手停掉（引擎仍报 `bInterrupted=true`）。
  - 头文件补充：为什么挂在这里而不是 GA 里 —— GA 只知道"我请求播放了"，而蒙太奇被谁停掉（自己结束、被别的蒙太奇顶掉、能力结束、预测被服务器拒绝）它不一定知情。AnimInstance 是所有路径的必经之地。用引擎的动态多播委托而不是自己埋点，是因为它覆盖了**全部**打断来源 —— 包括引擎自己发起的（比如 `OnPredictiveMontageRejected`）。
- **被谁调用 / 调用谁**：`UAnimInstance::OnMontageEnded` 动态多播委托；调用 `FDebug::DumpStackTraceToLog`、`CVarRPGLogMontageInterruptStack`（文件内匿名命名空间）。

#### `void URPG_AnimInstanceBase::NativeUninitializeAnimation()`

- **干什么**：解绑委托 + 清空播放记录。
- **关键实现**：
  1. `if (OnMontageStarted.IsAlreadyBound(this, &URPG_AnimInstanceBase::HandleMontageStarted)) OnMontageStarted.RemoveDynamic(this, &URPG_AnimInstanceBase::HandleMontageStarted);`
  2. `OnMontageEnded` 同样处理。
  3. `MontagePlayRecords.Empty();`
  4. `Super::NativeUninitializeAnimation();`
- **为什么这么写**（注释原文要点）：
  - 解绑必须成对 —— 理由见头文件（`InitializeAnimation` 会重入，且 `AddDynamic` 不查重）。用 `RemoveDynamic` 而不是 `RemoveAll`：后者会把将来可能加的别的监听者一起误伤。
  - 记录也一起清掉：不清的话，反初始化时活着的那些蒙太奇（`UninitializeAnimation` 是直接删实例、**不广播 Ended** 的）会留下永远配不上对儿的陈旧条目。
  - 头文件补充：⚠️ 必须成对解绑。`UAnimInstance::InitializeAnimation()` 的第一句就是 `UninitializeAnimation()`，而**同一个实例可以被重复初始化** —— `USkeletalMeshComponent::InitializeAnimScriptInstance()` 在"实例已存在且类相同 + `bForceReinit`"时会直接对现有实例再调一次（`SkeletalMeshComponent.cpp`），`bForceReinit=true` 来自重新注册路径。`AddDynamic` **不查重**（`ScriptDelegates.h` 的 `AddInternal` 是无条件 `InvocationList.Add`，只有 `AddUniqueDynamic` 才查重），所以重入一次就会绑两份：每段蒙太奇打两条日志，`ensure` 在 Development 构建里还会报红。
- **被谁调用 / 调用谁**：`UAnimInstance::InitializeAnimation()`（第一句）以及动画系统反初始化；调用 `RemoveDynamic`、`Super::NativeUninitializeAnimation()`。

#### `ARPG_BaseCharacter* URPG_AnimInstanceBase::GetRPGCharacter() const`（`UFUNCTION(BlueprintPure)`，内联）

- **干什么**：返回当前动画对象所属的角色。
- **关键实现**：`return OwnerCharacter.Get();`
- **为什么这么写**：注释写"可能为 nullptr（编辑器预览窗口里就没有角色）"。
- **被谁调用 / 调用谁**：蓝图 AnimGraph。

#### `FString URPG_AnimInstanceBase::GetAnimationDebugString() const`（`UFUNCTION(BlueprintPure)`）

- **干什么**：把当前动画状态格式化成一行字符串，供日志排查用。
- **关键实现**：
  1. `static const UEnum* MoveStateEnum = StaticEnum<ERPG_MovementState>();`
  2. `const FString MoveStateName = MoveStateEnum ? MoveStateEnum->GetNameStringByValue(static_cast<int64>(MovementState)) : TEXT("?");`
  3. 返回 `FString::Printf` 的格式化结果，模板为：`"移动[%s] 速度 %.0f/%.0f (%.2f) 方向 %.0f° 垂直 %.0f | 攻击 %d 闪避 %d 冲刺 %d 蓄力 %d(段 %d) 无敌 %d 死亡 %d | 模组 %d 连段 %d"`，实参依次为 `MoveStateName`、`Speed`、`MaxSpeed`、`SpeedRatio`、`Direction`、`VerticalVelocity`、`bIsAttacking`、`bIsDodging`、`bIsSprinting`、`bIsCharging`、`ChargeLevel`、`bIsInvulnerable`、`bIsDead`、`static_cast<int32>(AttackModuleType)`、`ComboIndex`（六个 bool 均以 `? 1 : 0` 转成整数）。
- **为什么这么写**：注释未说明（头文件写"供日志排查用"）。
- **被谁调用 / 调用谁**：蓝图 / 日志代码。

#### 文件内匿名命名空间：`CVarRPGLogMontageInterruptStack`

```cpp
static TAutoConsoleVariable<int32> CVarRPGLogMontageInterruptStack(
    TEXT("RPG.LogMontageInterruptStack"),
    0,
    TEXT("1 = 蒙太奇被提前结束时打印调用栈。排查\"谁把它停了\"用，很吵。"),
    ECVF_Default);
```

- **为什么这么写**（注释原文）：默认 0（关）。排查"是谁把动画停掉的"时打开：`RPG.LogMontageInterruptStack 1`，然后复现一次，日志里会直接给出**停它的那段代码**。为什么默认关：调用栈很贵、而且很吵（连段时每段都会打一次）。只在需要回答"凶手是谁"的时候开。
- **被谁调用 / 调用谁**：`HandleMontageEnded()`（用 `GetValueOnGameThread() != 0` 判断）。

#### 内嵌结构体 `URPG_AnimInstanceBase::FMontagePlayRecord`

| 成员 | 类型 / 默认值 | 说明（注释原文） |
| --- | --- | --- |
| `StartTime` | `float = 0.f` | 开始播放的世界时间（秒） |
| `Length` | `float = 0.f` | 蒙太奇全长（**蒙太奇秒**，不是墙钟秒） |
| `PlayRate` | `float = 1.f` | 播放速率。⚠️ 必须记下来，否则判据在 `Rate != 1` 时全是假警报。`PlayedFor` 是**墙钟时间**，`Length` 是**蒙太奇秒**，两者只在 `Rate == 1` 时可以直接比。角色上配了 `HitReactPlayRate` / `DeathMontagePlayRate`（`RPG_GA_HitReact.cpp` / `RPG_GA_Death.cpp` 会把它传给 `PlayMontageOrSkip`）的时候，全长 1.77s 的死亡动画实际 0.88s 播完是**完全正常**的 —— 不记 Rate 的话会把每一次正常播完都报成"被提前结束 50%" |

- 头文件对该结构体所在容器的额外警示：**必须每个蒙太奇一条**，不能用一个成员变量存"当前正在播的那个"。第一版就是那么写的，结果连段时数字全是乱的 —— 因为连段切段是"先停上一段、再播下一段"，两段在**同一帧**里重叠：`StartSegment: StopCurrentSegmentMontage() → 上一段开始结束；PlayMontageOrSkip(下一段) → 记录被覆盖；上一段的 Ended 回调 → 拿到的却是下一段的开始时间`。于是日志里出现了"实播 0.02s""实播 8.84s""全长 0.00s"这些一看就不对的值。教训：**做诊断工具时，"被观测对象是并发存在的"这件事必须先在数据结构上体现出来。** 用一个变量记"当前"，就是在假设"同一时刻只有一个" —— 而这个假设在连段里不成立。

#### 成员变量

| 变量 | 标记 | 类型 / 默认值 | 说明（注释原文要点） |
| --- | --- | --- | --- |
| `Speed` | `BlueprintReadOnly, Transient` | `float = 0.f` | 水平速度（cm/s）。已剔除垂直分量 |
| `MaxSpeed` | `BlueprintReadOnly, Transient` | `float = 0.f` | 当前姿态下的最大速度。会随冲刺 / 蹲伏变化 |
| `SpeedRatio` | `BlueprintReadOnly, Transient` | `float = 0.f` | 归一化速度 = `Speed / MaxSpeed`，恒在 0~1。★ 混合空间横轴请用这个，不要用 `Speed`。因为本工程的 `MaxSpeed` 会变（走 300 / 冲刺 850 / 蹲 180），用固定阈值的 `Speed` 轴需要为每种姿态各做一个混合空间；用归一化轴则一个混合空间三种速度通用，而且以后改数值不用重做动画 |
| `Direction` | `BlueprintReadOnly, Transient` | `float = 0.f` | 移动方向（度）。0 = 正前方，+90 = 正右，-90 = 正左，±180 = 正后。方向轴的混合空间就是靠它做"八向移动"—— 角色朝前跑但玩家按了左，`Direction` 就是 -90，混合出侧身跑动画 |
| `VerticalVelocity` | `BlueprintReadOnly, Transient` | `float = 0.f` | 垂直速度（cm/s）。正数上升、负数下落，用于区分起跳 / 滞空 / 落地 |
| `bIsInAir` | `BlueprintReadOnly, Transient` | `bool = false` | 是否腾空 |
| `bIsCrouching` | `BlueprintReadOnly, Transient` | `bool = false` | 是否蹲伏 |
| `MovementState` | `BlueprintReadOnly, Transient` | `ERPG_MovementState = Grounded` | Main 状态机的切换依据。四个值分别对应四条子状态机 |
| `bIsAttacking` | `BlueprintReadOnly, Transient` | `bool = false` | 攻击中。轻击连段与重击（含蓄力）都会让它为真 |
| `bIsDodging` | `BlueprintReadOnly, Transient` | `bool = false` | 闪避中 |
| `bIsSprinting` | `BlueprintReadOnly, Transient` | `bool = false` | 冲刺中 |
| `bIsCharging` | `BlueprintReadOnly, Transient` | `bool = false` | 蓄力中 |
| `ChargeLevel` | `BlueprintReadOnly, Transient` | `int32 = 0` | 蓄力段位 0~3。0 = 没在蓄力，或还在起手阶段没到第一段门槛 |
| `bIsInvulnerable` | `BlueprintReadOnly, Transient` | `bool = false` | 无敌帧中。翻滚的无敌窗口、复活保护等都挂这个标签 |
| `bIsDead` | `BlueprintReadOnly, Transient` | `bool = false` | 已死亡。死亡动画分支用它，而不是查血量 —— 血量可能被治疗拉回来 |
| `AttackModuleType` | `BlueprintReadOnly, Transient` | `ERPG_AttackModuleType = Unarmed` | 当前攻击模组。决定用哪套攻击动画（徒手 / 近战 / 远程） |
| `ComboIndex` | `BlueprintReadOnly, Transient` | `int32 = 0` | 当前连段索引。0 = 不在连段中，1..5 = 第几段。可用于调试显示 |
| `IdleSpeedThreshold` | `EditDefaultsOnly, BlueprintReadOnly, meta = (ClampMin = "0.0")` | `float = 10.f` | 判定为"站定"的速度上限（cm/s）。别设成 0 —— 角色实际停下时速度是逐渐衰减的，永远差一点点才到 0，结果就是 Idle 永远进不去，脚底一直在滑 |
| `MontageCutShortWarnRatio` | `EditDefaultsOnly, BlueprintReadOnly, meta = (ClampMin = "0.0", ClampMax = "1.0")` | `float = 0.9f` | 只有实际播放时长短于这个比例才打 Warning。设 0.9：正常播完（算上 BlendOut 的少量偏差）不会报警，但被截掉 10% 以上就会。调成 1.0 会让正常的混合误差也刷屏，反而把真正的问题淹掉 |
| `MontagePlayRecords` | private，无 UPROPERTY | `TMap<TWeakObjectPtr<UAnimMontage>, TArray<FMontagePlayRecord>>` | 正在播放中的蒙太奇记录。⚠️ 值是**数组**，不是单条 —— 同一份蒙太奇资产可以同时/连续有多个实例。只按"资产"存一条是不够的：受击蒙太奇池里只有一两个资产时，很容易连续两次取到同一个（`ARPG_BaseCharacter::PickHitReactMontage()` 是随机取）。`Montage_Play` 的顺序是"先停同组旧蒙太奇 → 建新实例 → 广播 `OnMontageStarted`"，于是旧实例的 `OnMontageEnded` 会在 `FindOrAdd` **覆盖过记录之后**才到 —— 拿到的 `StartTime` 是新实例的，算出来就是"实播 0.02s"。也就是说：按资产存单条时，被吐槽的"实播 0.02s / 全长 0.00s"只是**换了个入口重新出现**。用数组 + `Ended` 时取**最早一条**（FIFO）才对得上：旧实例先结束、配对最早的记录。键是弱引用，蒙太奇被卸载后键会失效，但**条目不会自动清掉** —— 会残留的路径只有"`UninitializeAnimation` 直接删实例、不广播 `Ended`"，残留量 = 每次反初始化时活着的蒙太奇数，且同一资产再播时会追加，不构成增长风险（`NativeUninitializeAnimation` 里会整个清空） |
| `OwnerCharacter` | private，无 UPROPERTY | `TWeakObjectPtr<ARPG_BaseCharacter>` | 所属角色。弱引用 —— AnimInstance 生命周期可能比角色长（编辑器预览） |

---

### `Source/RPG/Animation/RPG_AnimationTypes.h`

**一句话职责**：动画层的公共类型定义（目前只有一个枚举）。刻意**不依赖任何 GAS 类** —— 动画是表现层，让它依赖 ASC 会让"想单独测试动画状态计算"变得不可能（头文件注释，并与 `Combat/RPG_CombatTypes.h` 保持同一约定）。

#### 枚举 `ERPG_MovementState : uint8`（`UENUM(BlueprintType)`）

移动状态 —— 动画蓝图 Main 状态机的切换依据。

| 值 | DisplayName | 说明（注释原文） |
| --- | --- | --- |
| `Grounded` | 地面常态 | 地面常态 —— 走、慢跑、站定都在这里，交给 `SM_Locomotion_Walk` 内部混合 |
| `Sprinting` | 冲刺 | 冲刺中（由 `State.Sprinting` 标签判定，不是靠速度阈值猜） |
| `InAir` | 腾空 | 腾空 —— 起跳 / 滞空 / 下落 |
| `Crouching` | 蹲伏 | 蹲伏 —— 蹲着走和蹲着不动都在这里 |

- **为什么是这四个，而不是 Idle / Walk / Run / Sprint**（头文件长注释原文要点）：这个枚举回答的问题是"该走哪条子状态机"，不是"该播哪个动画"。走 / 跑 / 急停之间的过渡属于**同一个子状态机内部**的事，由 BlendSpace 的速度轴去混合 —— 把它们拆成平级状态的话：Main 状态机要为"走→跑→冲刺"再画一圈转移线，和子状态机里的重复；每个组合（蹲走→蹲跑、空中走→空中跑…）都要单独连线，数量爆炸。所以这里只区分**结构性差异**（在地面 / 腾空 / 蹲着 / 冲刺），速度的连续变化交给混合空间。
- **被谁调用 / 调用谁**：作为 `URPG_AnimInstanceBase::MovementState` 的类型，由 `UpdateLocomotion()` 写入、AnimGraph 读取。

---

### `Source/RPG/Animation/Notifies/RPG_AnimNotify_AttackEnd.h` / `.cpp`

**一句话职责**：攻击段结束标记（`UAnimNotify`）—— 放在蒙太奇的**最后一帧**，向角色广播 `Event.Combat.AttackEnd`，GA 收到后调用 `EndAbility` 结束这次能力。

**类/结构体**：`URPG_AnimNotify_AttackEnd` —— 继承 `UAnimNotify`，`UCLASS(meta = (DisplayName = "RPG 攻击结束"))`。

头文件注释原文要点：

- **为什么用 Notify 而不是等蒙太奇自然播完**：`PlayMontageAndWait` 的 `OnCompleted` 确实会在动画播完时触发，但 —— 蒙太奇末尾可能有一段"保持姿势"的时间，那段时间玩家已经可以操作了，等它播完才结束能力会让操作感觉迟滞；用 Notify 可以精确控制"哪一帧算结束"，把尾巴那几帧留给过渡；有些段需要"提前结束"（比如被衔接窗口接走了），有显式的结束点更好控制。
- **联机下的额外用途**：阶段 5 的 `BTTask_RPG_Attack` 会监听这个事件来实现"AI 等技能播完再继续行为树" —— 行为树任务必须用潜在任务（Latent Task）模式，发完攻击请求就返回 `Succeeded` 会导致 AI 在攻击动画播放期间继续移动。

#### `void URPG_AnimNotify_AttackEnd::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)`

- **干什么**：广播攻击段结束事件。
- **关键实现**：
  1. `Super::Notify(MeshComp, Animation, EventReference);`
  2. `if (!MeshComp) return;`
  3. `AActor* Owner = MeshComp->GetOwner(); if (!Owner) return;`
  4. `FGameplayEventData EventData;` → `EventData.EventTag = RPGTags::Event_Combat_AttackEnd;` → `EventData.Instigator = Owner;` → `EventData.Target = Owner;`
  5. `UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(Owner, RPGTags::Event_Combat_AttackEnd, EventData);`
  6. `UE_LOG(LogRPG_Animation, Verbose, TEXT("[%s] 攻击段结束"), *Owner->GetName());`
- **为什么这么写**：注释未在本函数内说明（设计理由见头文件）。
- **被谁调用 / 调用谁**：引擎动画通知系统；调用 `SendGameplayEventToActor`（由 GA 侧的 `UAbilityTask_WaitGameplayEvent` 监听）。

#### `FString URPG_AnimNotify_AttackEnd::GetNotifyName_Implementation() const`

- **干什么**：返回蒙太奇编辑器时间轴上显示的名字。
- **关键实现**：`return TEXT("攻击结束");`
- **为什么这么写**：注释未说明。
- **被谁调用 / 调用谁**：编辑器（AnimNotify 列表显示）。

---

### `Source/RPG/Animation/Notifies/RPG_AnimNotifyState_AttackWindow.h` / `.cpp`

**一句话职责**：伤害判定窗口（`UAnimNotifyState`）—— 覆盖"这一刀真正能打到人"的那段时间，Begin / End 分别广播 `Event.Combat.AttackWindow.Open` / `Close`，并可携带覆盖轨迹检测参数的载荷。

**类/结构体**：`URPG_AnimNotifyState_AttackWindow` —— 继承 `UAnimNotifyState`，`UCLASS(meta = (DisplayName = "RPG 攻击判定窗口"))`。

头文件注释原文要点：

- 放在蒙太奇里覆盖"这一刀真正能打到人"的那段时间。窗口之外挥刀不产生伤害 —— 这直接决定了战斗的手感：窗口开得太早会"还没挥到就掉血"，太晚会"明明打中了没反应"。
- **它怎么和 GA 通信**：`NotifyBegin` → `SendGameplayEventToActor(Event.Combat.AttackWindow.Open)`；`NotifyEnd` → `SendGameplayEventToActor(Event.Combat.AttackWindow.Close)`。GA 侧用 `UAbilityTask_WaitGameplayEvent` 监听这两个事件，开启时启动武器轨迹检测，关闭时停止。
- **为什么不在 Notify 里直接做伤害判定** —— 这是本项目的一条硬性原则：**表现层不做逻辑**。① Notify 里做判定 → 数值和动画焊死，改数值要动动画资产；② 无法单元测试，也无法在没有动画的情况下验证数值链路；③ 联机下 Notify 的播放时机在客户端和服务器可能有细微差异，把权威逻辑放在这里会引入难以复现的 bug。所以 Notify 只负责"广播一个事实"，怎么响应是 GA 的事。

#### 属性（UPROPERTY）

| 属性 | 标记 | 类型 / 默认值 | 说明（注释原文） |
| --- | --- | --- | --- |
| `AttackTag` | `EditAnywhere, Category = "RPG\|Attack"` | `FGameplayTag` | 这一段攻击的标签，便于在日志里区分是第几段 |
| `bOverrideTrace` | `EditAnywhere, Category = "RPG\|Attack"` | `bool = false` | 是否用下面的参数覆盖攻击模组里的检测配置。一般不需要开 —— 除非这一段的攻击范围明显和其他段不同（比如某个大范围的横扫招式） |
| `TraceSource` | `EditAnywhere, Category = "RPG\|Attack", meta = (EditCondition = "bOverrideTrace")` | `ERPG_TraceSource = ERPG_TraceSource::Hands` | 轨迹来源 |
| `TraceRadius` | `EditAnywhere, Category = "RPG\|Attack", meta = (EditCondition = "bOverrideTrace", ClampMin = "1.0")` | `float = 30.f` | 检测半径 |
| `SocketStart` | `EditAnywhere, Category = "RPG\|Attack", meta = (EditCondition = "bOverrideTrace")` | `FName` | 起始插槽 |
| `SocketEnd` | `EditAnywhere, Category = "RPG\|Attack", meta = (EditCondition = "bOverrideTrace")` | `FName` | 结束插槽 |

#### `void URPG_AnimNotifyState_AttackWindow::NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration, const FAnimNotifyEventReference& EventReference)`

- **干什么**：打包检测参数成 Payload，广播窗口开启事件。
- **关键实现**：
  1. `Super::NotifyBegin(...);`
  2. `if (!MeshComp) return;` → `AActor* Owner = MeshComp->GetOwner(); if (!Owner) return;`
  3. **构造 Payload**：`URPG_AttackWindowPayload* Payload = NewObject<URPG_AttackWindowPayload>(MeshComp);`，逐个字段拷贝 `AttackTag`、`bOverrideTrace`、`TraceSource`、`TraceRadius`、`SocketStart`、`SocketEnd`。
  4. `FGameplayEventData EventData;` → `EventData.EventTag = RPGTags::Event_Combat_AttackWindow_Open;` → `Instigator = Owner;` → `Target = Owner;` → `OptionalObject = Payload;` → `OptionalObject2 = Animation;`
  5. `SendGameplayEventToActor(Owner, RPGTags::Event_Combat_AttackWindow_Open, EventData);`
  6. `UE_LOG(LogRPG_Animation, Verbose, TEXT("[%s] 攻击判定窗口开启（%s）"), ...)`，标签无效时打"未指定标签"。
- **为什么这么写**（注释原文要点）：
  - **Outer 传 `MeshComp` 而不是 `GetTransientPackage()`**：这样 Payload 的生命周期跟着角色走，角色销毁时一起回收，不会泄漏。
  - `OptionalObject2 = Animation`：来源蒙太奇，供 GA 识别"迟到事件"（理由同 `ComboWindow`）。
- **被谁调用 / 调用谁**：引擎动画通知系统（NotifyState 的 Begin）；调用 `NewObject`、`SendGameplayEventToActor`。

#### `void URPG_AnimNotifyState_AttackWindow::NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)`

- **干什么**：广播窗口关闭事件。
- **关键实现**：
  1. `Super::NotifyEnd(...);`
  2. `if (!MeshComp) return;` → `AActor* Owner = MeshComp->GetOwner(); if (!Owner) return;`
  3. `FGameplayEventData EventData;` → `EventData.EventTag = RPGTags::Event_Combat_AttackWindow_Close;` → `Instigator = Owner;` → **`EventData.OptionalObject2 = Animation;`**
  4. `SendGameplayEventToActor(Owner, RPGTags::Event_Combat_AttackWindow_Close, EventData);`
  5. `UE_LOG(LogRPG_Animation, Verbose, TEXT("[%s] 攻击判定窗口关闭"), *Owner->GetName());`
- **为什么这么写**（注释原文要点）：
  - 关闭事件不需要载荷 —— GA 收到就知道该停检测了。
  - **★ 来源蒙太奇**：蒙太奇被停掉时引擎会补发 `NotifyEnd`，上一段的"判定窗口关闭"会在下一段刚播起来时到达。GA 靠这个字段判断它是不是当前这一段发的，不是就忽略，否则会把下一段刚开起来的轨迹检测任务直接掐掉。
- **被谁调用 / 调用谁**：引擎动画通知系统；调用 `SendGameplayEventToActor`。
- **实现细节**：`NotifyEnd` 里没有设置 `Target`（与 `NotifyBegin` 不同）。

#### `FString URPG_AnimNotifyState_AttackWindow::GetNotifyName_Implementation() const`

- **干什么**：返回时间轴上显示的名字（带标签）。
- **关键实现**：`return AttackTag.IsValid() ? FString::Printf(TEXT("判定窗口 [%s]"), *AttackTag.ToString()) : TEXT("判定窗口");`
- **为什么这么写**（注释原文）：在蒙太奇编辑器的时间轴上直接显示标签名，不用点开 Details 就能看出是哪一段。
- **被谁调用 / 调用谁**：编辑器。

---

### `Source/RPG/Animation/Notifies/RPG_AnimNotifyState_ComboWindow.h` / `.cpp`

**一句话职责**：连段衔接窗口（`UAnimNotifyState`）—— 定义"什么时候能接下一段"，Begin / End 广播 `Event.Combat.ComboWindow.Open` / `Close`。

**类/结构体**：`URPG_AnimNotifyState_ComboWindow` —— 继承 `UAnimNotifyState`，`UCLASS(meta = (DisplayName = "RPG 连段衔接窗口"))`。无额外 `UPROPERTY`。

头文件注释原文要点：

- **它决定连招手感，是最需要反复调的一个 Notify**：窗口内按攻击键 → 接上下一段；窗口外按攻击键 → 要么被缓存起来等窗口开（如果还在生命周期内），要么直接作废。
- **调参规律**：窗口太窄 → 玩家必须卡在很精确的时机按，感觉"连不上"；窗口太宽 → 玩家乱按也能连，连招失去节奏感；通常开在后摇的前 1/3 到 1/2 处，长度约 0.3~0.5 秒；最后一段一般不开窗口（打了就是打了，不能无限连）。
- **它和输入缓存的分工**：输入缓存 —— 解决"玩家按早了"：按键记下来，等窗口开；衔接窗口 —— 解决"什么时候能接"：定义规则。两者缺一不可。只有窗口没有缓存 → 按早了白按，不跟手；只有缓存没有窗口 → 可以无限快速连打，失去节奏。

#### `void URPG_AnimNotifyState_ComboWindow::NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration, const FAnimNotifyEventReference& EventReference)`

- **干什么**：广播衔接窗口开启事件。
- **关键实现**：
  1. `Super::NotifyBegin(...);`
  2. `if (!MeshComp) return;` → `AActor* Owner = MeshComp->GetOwner(); if (!Owner) return;`
  3. `EventData.EventTag = RPGTags::Event_Combat_ComboWindow_Open;` → `Instigator = Owner;` → `OptionalObject2 = Animation;`（行内注释："来源蒙太奇，见下方说明"）
  4. `SendGameplayEventToActor(Owner, RPGTags::Event_Combat_ComboWindow_Open, EventData);`
  5. `UE_LOG(LogRPG_Animation, Verbose, TEXT("[%s] 连段衔接窗口开启"), *Owner->GetName());`
- **为什么这么写**：见 `NotifyEnd` 的长注释（来源蒙太奇的作用）。
- **被谁调用 / 调用谁**：引擎动画通知系统；调用 `SendGameplayEventToActor`。
- **实现细节**：`NotifyBegin` 里没有设置 `Target`（只设了 `EventTag` / `Instigator` / `OptionalObject2`）。

#### `void URPG_AnimNotifyState_ComboWindow::NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)`

- **干什么**：广播衔接窗口关闭事件，并带上来源蒙太奇。
- **关键实现**：
  1. `Super::NotifyEnd(...);`
  2. `if (!MeshComp) return;` → `AActor* Owner = MeshComp->GetOwner(); if (!Owner) return;`
  3. `EventData.EventTag = RPGTags::Event_Combat_ComboWindow_Close;` → `Instigator = Owner;` → **`EventData.OptionalObject2 = Animation;`**
  4. `SendGameplayEventToActor(Owner, RPGTags::Event_Combat_ComboWindow_Close, EventData);`
  5. `UE_LOG(LogRPG_Animation, Verbose, TEXT("[%s] 连段衔接窗口关闭"), *Owner->GetName());`
- **为什么这么写**（长注释原文要点）：
  - ★ 把来源蒙太奇带上 —— 这是"迟到事件"能被识别出来的唯一依据。
  - 引擎在**蒙太奇被停掉时，会主动给所有还活着的 NotifyState 补发 `NotifyEnd`**（`UAnimInstance::TriggerMontageEndedEvent`，`AnimInstance.cpp:2507`，注释就写着 "Send end notifications for anim notify state when we are stopped"）。
  - 而连段接下一段的做法正是"停掉上一段的蒙太奇"，所以：第 N 段衔接窗口打开 → 接上第 N+1 段 → 第 N 段的蒙太奇被停 → 引擎补发第 N 段的「衔接窗口关闭」→ 一帧后到达 GA → 如果 GA 不辨来源，会拿它当成"当前这一段的窗口关闭" → 顺手把缓存里的下一次按键吃掉，凭空多跳一段。
  - 表现是"连招打不全/跳段"，而且只在快速连打时出现。带上来源之后，GA 侧一比就知道这条是上一段的，直接忽略。
  - 用 `OptionalObject2` 而不是 `OptionalObject`：后者留给攻击判定窗口传载荷用。
  - 末尾注释：窗口关闭是**连段重置的触发点** —— 如果到这里缓存里还是没有输入，说明玩家停手了，连段应该归零。否则玩家停手后下一击会直接从第 N 段开始 —— 这是连段系统最经典的 bug。（本 Notify 自身只负责广播事件与打日志，实际的连段重置在 GA 侧实现 —— 该实现不在本章阅读范围内，未核实。）
- **被谁调用 / 调用谁**：引擎动画通知系统；调用 `SendGameplayEventToActor`。

#### `FString URPG_AnimNotifyState_ComboWindow::GetNotifyName_Implementation() const`

- **干什么**：返回时间轴上显示的名字。
- **关键实现**：`return TEXT("衔接窗口");`
- **为什么这么写**：注释未说明。
- **被谁调用 / 调用谁**：编辑器。

---

### `Source/RPG/Animation/Notifies/RPG_AnimNotifyState_Invulnerability.h` / `.cpp`

**一句话职责**：无敌帧窗口（`UAnimNotifyState`）—— 覆盖闪避 / 翻滚中"身体已经翻出去、但还没站稳"的那段时间，Begin / End 广播 `Event.Character.Invulnerability.Begin` / `End`。

**类/结构体**：`URPG_AnimNotifyState_Invulnerability` —— 继承 `UAnimNotifyState`，`UCLASS(meta = (DisplayName = "RPG 无敌帧"))`。无额外 `UPROPERTY`。

头文件注释原文要点：

- **无敌帧为什么不用固定时长，而要用 Notify 精确控制**：无敌帧的起止点直接决定闪避的收益和手感 —— 开太晚 → 玩家明明已经翻出去了还是被打到，感觉"判定不公"；关太早 → 翻滚后半段暴露，闪避的收益下降；关太晚 → 玩家可以无限翻滚规避伤害，战斗失去张力。这三条边界和动画的视觉表现强相关（脚离地的那一帧、落地的前一帧），所以必须在动画里逐帧调，而不是在代码里给个固定秒数。
- **为什么用 GE 而不是 `AddLooseGameplayTag`**：通知本身只广播事件，真正上标签的是 GA 侧的 `GE_Invulnerable`。用 GE 的好处：在 GameplayDebugger 里能看到"无敌是哪个 GE 给的、还剩多久"；能力被打断时 GE 会随能力一起清理，不会残留一个永久的无敌标签；将来要做"无敌期间免疫特定类型伤害"，GE 的标签要求能直接支持。

#### `void URPG_AnimNotifyState_Invulnerability::NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration, const FAnimNotifyEventReference& EventReference)`

- **干什么**：广播无敌帧开始事件。
- **关键实现**：
  1. `Super::NotifyBegin(...);`
  2. `if (!MeshComp) return;` → `AActor* Owner = MeshComp->GetOwner(); if (!Owner) return;`
  3. `EventData.EventTag = RPGTags::Event_Character_Invulnerability_Begin;` → `Instigator = Owner;` → `Target = Owner;`
  4. `SendGameplayEventToActor(Owner, RPGTags::Event_Character_Invulnerability_Begin, EventData);`
  5. `UE_LOG(LogRPG_Animation, Verbose, TEXT("[%s] 无敌帧开始"), *Owner->GetName());`
- **为什么这么写**：见头文件（用 GE 而非 loose tag 的三条理由）。
- **被谁调用 / 调用谁**：引擎动画通知系统；调用 `SendGameplayEventToActor`（GA 侧响应并施加 `GE_Invulnerable`）。

#### `void URPG_AnimNotifyState_Invulnerability::NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)`

- **干什么**：广播无敌帧结束事件。
- **关键实现**：
  1. `Super::NotifyEnd(...);`
  2. `if (!MeshComp) return;` → `AActor* Owner = MeshComp->GetOwner(); if (!Owner) return;`
  3. `EventData.EventTag = RPGTags::Event_Character_Invulnerability_End;` → `EventData.Instigator = Owner;`
  4. `SendGameplayEventToActor(Owner, RPGTags::Event_Character_Invulnerability_End, EventData);`
  5. `UE_LOG(LogRPG_Animation, Verbose, TEXT("[%s] 无敌帧结束"), *Owner->GetName());`
- **为什么这么写**：注释未说明。
- **被谁调用 / 调用谁**：引擎动画通知系统；调用 `SendGameplayEventToActor`。
- **实现细节**：与 `AttackWindow` / `ComboWindow` 不同，本类的 `NotifyEnd` **没有**设置 `Target`，也**没有**携带 `OptionalObject2` 来源蒙太奇（因此不参与"迟到事件"的来源判别，这一点在头文件与 .cpp 中也没有任何说明 —— 未核实是否为有意设计）。

#### `FString URPG_AnimNotifyState_Invulnerability::GetNotifyName_Implementation() const`

- **干什么**：返回时间轴上显示的名字。
- **关键实现**：`return TEXT("无敌帧");`
- **为什么这么写**：注释未说明。
- **被谁调用 / 调用谁**：编辑器。

---

## 本章小结：几处值得注意的设计要点

1. **ASC 归属的"不对称"是刻意的**：`ARPG_BaseCharacter` 用 `GetASCInternal()` 这一个纯虚函数把差异收敛到一处，接口实现只有一份；`ARPG_Player` 与 `ARPG_Enemy` 各实现一行返回语句。
2. **"是不是本地玩家"必须用 `IsLocallyControlledPlayer()`**，不能用 `IsLocallyControlled()` —— 后者对服务器上的 AI（以及 Standalone 下的所有角色）恒为 true。该判据同时服务于头顶血条可见性与伤害飘字屏蔽两处。
3. **布娃娃只复制开关**：`bRagdollEnabled` 是唯一与布娃娃相关的复制属性（`COND_None`），物理由各端各自模拟；服务器侧需要在 `EnterRagdoll` / `ExitRagdoll` 里手动调一次 `OnRep_RagdollEnabled()`，因为 OnRep 在服务器上不会被自动调用。
4. **动画层的真相源只有两个**：`CharacterMovement`（速度、方向、姿态）与 `GameplayTag`（战斗状态），`CombatComponent` 只贡献"配置"（攻击模组）和"进度"（连段索引）。AnimInstance **不缓存 ASC**，因为 PlayerState 会被替换。
5. **Notify 只广播事实，不做判定**：4 个 Notify 类的实现高度同构 —— 判空 → 组 `FGameplayEventData` → `SendGameplayEventToActor` → Verbose 日志。区分"迟到事件"的手段是 `EventData.OptionalObject2 = Animation`（`AttackWindow` 的 Close 与 `ComboWindow` 的 Close 都带；`Invulnerability` 两端都不带）。
6. **蒙太奇诊断是这套动画层里最"反直觉"的一段**：`MontagePlayRecords` 用 `TMap<资产, TArray<记录>>` 而不是单个"当前记录"，`HandleMontageEnded` 取**最早**一条而非最新一条 —— 两者都是被"连段切段时两段在同一帧重叠"这个真实 bug 逼出来的（见头文件里 `FMontagePlayRecord` 上方的大段复盘）。

---

# 三、GAS 核心（ASC / AttributeSet / GA 基类 / Execution / AbilityTask）

### `Source/RPG/AbilitySystem/RPG_AbilitySystemComponent.h`（实现：`Source/RPG/AbilitySystem/RPG_AbilitySystemComponent.cpp`）

**一句话职责**：项目扩展的 ASC，敌我共用；把"物理按键 → Input.* 标签 → Ability.* 能力"做成两级解耦，并补上 GAS 默认沉默的两处盲区（激活失败无日志、客户端索引缺失）。

**类/结构体**：`URPG_AbilitySystemComponent` —— 继承 `UAbilitySystemComponent`；`UCLASS(ClassGroup = (RPG), meta = (BlueprintSpawnableComponent))`。玩家挂 `ARPG_PlayerState`、敌人挂自己身上，本类不区分敌我，靠外部 `GetASCInternal()` 抹平差异。

**头文件注释里声明的前提（原样转述，都是本项目的设计依据）**

- **两级解耦**：物理按键 ──①(IMC 配置)──▶ `Input.*` 标签 ──②(本类的映射表)──▶ `Ability.*` 能力。① 在 InputMappingContext 资产里配，改键位不碰代码；② 在角色初始化时注册，改技能配置不碰输入代码。对比写法（按键事件里直接 `Cast` 到具体 GA 类激活）的三个后果：想改键位要动代码、一个技能被两个键触发要写两遍、按键重绑定无从下手。
- **为什么不用引擎自带的 InputID 机制**：UE 5.8 的 `FGameplayAbilitySpec` 只有 `int32 InputID`，**没有** `FGameplayTag InputTag`。措辞要点（注释特别强调）：不是"引擎早期有、后来移除了"——引擎**从来没提供过** InputTag 版本；网上教程里那种写法来自 Lyra 自己的 `FLyraAbilitySet_GameplayAbility`，那是示例项目的扩展，不是引擎接口。int32 编号方案的两个问题：编号与能力的对应关系藏在配置里、加技能要小心撞号；调试日志里只有 `"InputID 3"` 看不出是什么。用 GameplayTag 自带语义，日志直接显示 `"Input.Attack.Light"`。
- **为什么用 `FGameplayAbilitySpecHandle` 而不是 AbilityClass 激活**：同一个 GA 类可能被授予多次（不同等级、不同来源），Handle 精确指向"这一次授予"，不会误激活另一个实例。
- **为什么"登记"和"授予"必须拆成两步**：原先只有一个 `RegisterInputAbilities`（登记 + 授予一起做），只在 `HasAuthority()` 分支里调用 —— 于是**客户端的映射表永远是空的**，按任何键都会走到"这个输入标签没有绑定任何能力"。而这张表其实是**纯配置数据**（来自角色上的 StartupAbilities），不是运行时状态，两端都应该有；真正只能服务器做的只是"授予"那一步。拆开后角色初始化代码里的分工一眼可见：两端都调 `RegisterInputAbilityMappings()`，仅服务器 `GrantInputAbilities()`。

#### `URPG_AbilitySystemComponent::URPG_AbilitySystemComponent()`

- **干什么**：设置 ASC 的复制开关与复制模式。
- **关键实现**：
  - `SetIsReplicated(true)`。
  - `SetReplicationMode(EGameplayEffectReplicationMode::Mixed)`。
- **为什么这么写**（注释原话转述）：
  - 少 `SetIsReplicated(true)` 这一句，联机下整个 GAS 等于失效：能力激活不会同步到服务器（客户端放技能，服务器不知道）、GE 不会复制到其他客户端（别人看不到你身上的 Buff）、GameplayCue 不会在其他客户端播放（特效只有自己看得见）。**而且这三件事都不会报错，只是"没反应"。**
  - `Mixed` = 自己控制的角色走 Full、其他角色走 Minimal：Full 让客户端拿到完整 GE 信息才能做本地预测；Minimal 只同步 GameplayCue 与标签，省带宽（别人身上的 Buff 具体几层、还剩几秒，我不需要知道）。这是玩家类 ASC 的标准配置。敌人（AI 控制）在 Mixed 下走 Minimal，对 MVP 够用；若将来要让客户端对敌人做预测（比如"附身"机制），再单独调成 Full。
- **被谁调用 / 调用谁**：由 `ARPG_PlayerState` / `ARPG_Enemy` 以 `CreateDefaultSubobject` 创建（敌人侧见 `RPG_Enemy.cpp:16` 的 AttributeSet 同一段构造代码）。

#### `bool IsServerSideASC(const UAbilitySystemComponent& ASC)`（.cpp 匿名命名空间内的自由函数）

- **干什么**：判断这个 ASC 当前跑在服务器还是客户端，**只用于日志措辞**（例如 `RegisterInputAbilityMappings` 的结尾日志与 `TryActivateAbilityByInputTag` 的提示日志）。
- **关键实现**：取 `ASC.GetOwnerActor()`；为空时按"服务器"处理（`OwnerActor ? OwnerActor->HasAuthority() : true`），因为这条只在 `InitAbilityActorInfo` 之前发生，那时的日志本来就没什么意义。
- **为什么这么写**（注释给了三条理由，全部转述）：
  - 不用 `IsOwnerActorAuthoritative()`：它返回 `!bCachedIsNetSimulated`，那是个在 `InitAbilityActorInfo` 时算出来并缓存的标志 —— 对日志来说是一层不必要的间接，读代码的人还得再查那个缓存怎么算的。
  - 不用 `HasAuthority()`：那是 `AActor` 的方法，ASC 是**组件**，根本没有这个函数（**编译期就会报错**）。
  - 直接问"我的主人是不是权威"，语义最短，且 `AActor::HasAuthority()` 就是 `GetLocalRole() == ROLE_Authority`，是所有人都看得懂的判据。
- **被谁调用 / 调用谁**：被 `URPG_AbilitySystemComponent::RegisterInputAbilityMappings` 与 `URPG_AbilitySystemComponent::TryActivateAbilityByInputTag` 调用；自身调用 `AActor::HasAuthority()`。

#### `void URPG_AbilitySystemComponent::BeginPlay()`

- **干什么**：接管"能力激活失败"的汇报权。
- **关键实现**：`Super::BeginPlay()` 后执行 `AbilityFailedCallbacks.AddUObject(this, &URPG_AbilitySystemComponent::OnAbilityActivationFailed);`。
- **为什么这么写**：不接这个回调的话，GAS 对激活失败保持完全沉默（见头文件与 `OnAbilityActivationFailed` 的说明）。
- **被谁调用 / 调用谁**：引擎的 Actor 生命周期调用；自身调用 `OnAbilityActivationFailed`（通过委托）。

#### `void URPG_AbilitySystemComponent::OnAbilityActivationFailed(const UGameplayAbility* Ability, const FGameplayTagContainer& FailureTags)`

- **干什么**：把引擎"只在 `CanActivateAbility` 里往 `OptionalRelevantTags` 填一个原因标签就返回 false"的失败，变成一条带原因的 Warning 日志。
- **关键实现**：`FailureTags.IsEmpty()` 时把原因文案设为 `TEXT("(引擎未提供原因标签)")`，否则取 `FailureTags.ToStringSimple()`；用 `UE_LOG(LogRPG_Ability, Warning, TEXT("能力 %s 激活失败。原因：%s"), *GetNameSafe(Ability), *ReasonText)` 输出。
- **为什么这么写**：GAS 的 `TryActivateAbility` 失败时**默认不打印任何日志**，结果是"按键没反应，日志一片空白"；`AbilityFailedCallbacks` 专门用来补这个盲区，能把失败原因标签带出来（冷却、资源不足、还是被标签阻断）。
- **注释列出的常见 FailureTags 取值**（原样保留）：`Ability.ActivateFail.CanActivate`（GA 重写的 `CanActivateAbility` 返回 false）、`Ability.ActivateFail.BlockedTags`（被 ActivationBlockedTags 阻断）、`Ability.ActivateFail.MissingTags`（缺少 ActivationRequiredTags）、`Ability.ActivateFail.Cooldown`（冷却中）、`Ability.ActivateFail.Cost`（资源不足，Cost GE 检查失败）、`Ability.ActivateFail.Networking`（网络策略不允许，比如 ServerOnly 的能力被客户端请求）。
- **被谁调用 / 调用谁**：由 `BeginPlay` 注册进 `AbilityFailedCallbacks`；调用 `GetNameSafe`。

#### `void URPG_AbilitySystemComponent::RegisterInputAbilityMappings(const TMap<FGameplayTag, TSubclassOf<UGameplayAbility>>& InMappings)`

- **干什么**：登记"输入标签 → 能力类"的映射。**它不授予任何能力**，只是把配置记下来。**服务器和客户端都要调。**
- **关键实现**（三段）：
  1. `InputTagToAbilityClass = InMappings;` —— **整表覆盖**而不是逐条 Add。理由：这个函数可能被多次调用（`PossessedBy` / `OnRep_PlayerState` / `BeginPlay` 三个时机都会走初始化），覆盖能保证"配置改了之后重新登记"是幂等的，不会留下上一次配置的残渣。
  2. `WarnedUnmappedTags.Reset(); WarnedUnresolvedTags.Reset();` —— 配置重新登记了，之前那些"这个标签有问题"的去重标记全部作废。不清的话，改完配置也不会再收到提示（去重集合把新配置也一起挡了）。
  3. **重复类检查**（局部块）：用局部 `TSet<TSubclassOf<UGameplayAbility>> SeenClasses` 遍历 `InMappings`，跳过空 Value，用 `SeenClasses.Add(Pair.Value, &bAlreadySeen)` 的 `bAlreadySeen` 出参判断重复，命中就打 Warning。
  4. 结尾 `UE_LOG(LogRPG_Ability, Log, TEXT("登记输入能力映射：%d 条（%s）"), InMappings.Num(), IsServerSideASC(*this) ? TEXT("服务器") : TEXT("客户端"))`。
- **为什么这么写**：重复类检查针对的坑 —— 两个不同的输入标签映射到**同一个 GA 类**时，服务器会授予两个独立 Spec，而客户端的索引重建是按类反查的（`FindAbilitySpecFromClass` 只返回第一个），于是两个标签都会指向同一个 Spec，表现是"按 A 键触发的是 B 键那个实例"。注释明确说**现在不是问题**（每个输入标签一个独立 GA 类），但这是个**静默**的坑：将来有人想让"轻击"和"重击"共用同一个 GA 类、靠参数区分时就会踩到；在配置登记这一步就报出来，比等到表现诡异时再查便宜得多。
- **被谁调用 / 调用谁**：`ARPG_Player::InitializeAbilitySystem`（`RPG_Player.cpp:175`）、`ARPG_Enemy`（`RPG_Enemy.cpp:127`）在两端调用；自身调用 `IsServerSideASC`。

#### `void URPG_AbilitySystemComponent::GrantInputAbilities(int32 Level = 1)`

- **干什么**：按已登记的映射逐个授予能力。**只能服务器调。**
- **关键实现**：
  - 先把 `InputTagToAbilityClass` **整体复制**到局部变量 `MappingsToGrant`，再 `for (const TPair<...>& Pair : MappingsToGrant)` 逐个调 `RegisterInputAbility(Pair.Key, Pair.Value, Level)`，用 `GrantedCount` 统计成功数。
  - 结尾日志：`TEXT("授予输入能力完成：%d/%d 条")`。
- **为什么这么写**（这是本文件里最细的一段引擎机制说明，逐条转述）：
  - 必须遍历**副本**，因为下面调的 `RegisterInputAbility` 里有一句 `InputTagToAbilityClass.Add(InputTag, AbilityClass)` —— 在遍历一个容器的过程中往它里面写，就是迭代器失效的经典场景。
  - 机制上要说准：UE 5.8 的 `TMap::Add` 走 `Emplace`，而默认的 `TSet` 现在是 `TSparseSet`，它的 `Emplace` **先无条件 `AddUninitialized()` 一个新槽**，然后才去查重，命中已有键时把新值移动赋值过去再释放临时槽（`SparseSet.h.inl` 的 `Emplace`）。也就是说键已存在时**不会重哈希**，但**确实会往元素存储里追加一次** —— 那一下就可能触发扩容，让正在用的迭代器失效。
  - 加上 5.8 的 range-for 迭代器是按下标取元素的（`Array[GetIndex()]`），Realloc 之后继续遍历就是读已释放的内存。
  - 结论：复制一份是几微秒的成本，换掉一整类"偶尔崩一次"的问题。
  - 客户端的 AbilitySpec 是复制过来的，自己再授予一次会变成两份 —— 症状是"放一次技能触发两遍效果"，而且只在联机时出现（见头文件对 `GrantInputAbilities` 的说明）。
- **被谁调用 / 调用谁**：`ARPG_Player::InitializeAbilitySystem`（`RPG_Player.cpp:252`）、`ARPG_Enemy`（`RPG_Enemy.cpp:159`），均在服务器分支；自身调用 `RegisterInputAbility`。

#### `bool URPG_AbilitySystemComponent::RegisterInputAbility(FGameplayTag InputTag, TSubclassOf<UGameplayAbility> AbilityClass, int32 Level = 1)`

- **干什么**：注册一个输入标签对应的 GA 并**立即授予**（登记 + 授予合体版，供 `GrantInputAbilities` 批量调用）。返回是否注册成功。
- **关键实现**（按代码顺序）：
  1. `!InputTag.IsValid()` → Warning `"RegisterInputAbility 失败：输入标签无效"`，返回 false。
  2. `!AbilityClass` → Warning `"RegisterInputAbility 失败：%s 没有指定能力类"`，返回 false。
  3. **重复注册守卫**：`InputTagToSpecHandle.Find(InputTag)` 命中且 `OldHandle->IsValid()` 时，先 `ClearAbility(*OldHandle)`（Verbose 日志 `"输入标签 %s 重复注册，已撤销旧能力"`），再 `InputTagToSpecHandle.Remove(InputTag)`。
     - 理由（注释）：不这么做的话，同一个标签会挂上多个 GA 实例，激活时可能同时触发两个，很难查。
  4. `const FGameplayAbilitySpec NewSpec(AbilityClass, Level); const FGameplayAbilitySpecHandle Handle = GiveAbility(NewSpec);` —— 注释称"2 步走"创建并授予。Level 会传给 GA，进而影响它施加的 GE 的 Level，这是 GAS 里做"技能等级影响数值"的机制，现在固定 1 级，架构先留好。
  5. `!Handle.IsValid()` → `Error` 日志 `"RegisterInputAbility 失败：授予能力 %s 未成功"`，返回 false。
  6. 填两张映射表：`InputTagToAbilityClass.Add(InputTag, AbilityClass)`、`InputTagToSpecHandle.Add(InputTag, Handle)`。
  7. Log 日志 `"注册输入能力：%s → %s"`。
  8. **被动能力立即激活**：`AbilityClass->GetDefaultObject<URPG_GameplayAbilityBase>()` 取 CDO，若 `ShouldActivateOnGranted()` 为真则 `TryActivateAbility(Handle)`，并打日志 `"  └─ 该能力标记为「授予即激活（被动技能）」，已自动激活"`。
  9. 返回 true。
- **为什么这么写**：被动能力（耐力恢复这类常驻能力）没有输入触发，必须在这里主动拉起；激活后它们会一直保持激活（GA 内部不调 `EndAbility`），靠持续挂着的 GE 起作用。CDO 判断用的是 `const URPG_GameplayAbilityBase*`，取不到（非本项目基类的 GA）时整个被动分支静默跳过。
- **被谁调用 / 调用谁**：被 `GrantInputAbilities` 调用；自身调用 `GiveAbility`、`ClearAbility`、`TryActivateAbility`、`URPG_GameplayAbilityBase::ShouldActivateOnGranted`。**注意：它没有 `UFUNCTION` 标记，不是蓝图可调用接口。**

#### `bool URPG_AbilitySystemComponent::GivePassiveAbility(TSubclassOf<UGameplayAbility> AbilityClass, int32 Level = 1)`

- **干什么**：授予一个**不绑定任何输入标签**的被动能力。
- **关键实现**：
  1. `!AbilityClass` → Warning `"GivePassiveAbility 失败：没有指定能力类"`，返回 false。
  2. **按类去重守卫**：`if (FindAbilitySpecFromClass(AbilityClass))` 命中 → Verbose 日志 `"被动能力 %s 已经授予过，跳过（重生时会出现这种情况，属正常）"`，**返回 true**（注意语义：跳过也算成功）。
  3. `const FGameplayAbilitySpec NewSpec(AbilityClass, Level); const FGameplayAbilitySpecHandle Handle = GiveAbility(NewSpec);`（"2 步走"）。
  4. `!Handle.IsValid()` → Error `"GivePassiveAbility 失败：授予 %s 未成功"`，返回 false。
  5. Log `"注册被动能力：%s"`。
  6. 与 `RegisterInputAbility` 同样的 CDO 判断 + `TryActivateAbility(Handle)`，日志 `" └─ %s 被动能力已自动激活"`。返回 true。
- **为什么需要单独的入口**：`RegisterInputAbility` 的映射表是"输入标签 → 能力"，而耐力恢复这类被动能力根本没有输入触发 —— 硬塞一个假的输入标签进去只会让配置表变得莫名其妙。
- **为什么必须有按类去重**（注释重点）：玩家的 ASC 挂在 `PlayerState` 上，**重生时 PlayerState 不销毁** —— 新 Pawn 的 `InitializeAbilitySystem` 会再跑一遍，把被动能力又授予一次。结果就是挂了两份 `GA_StaminaRegen` → 耐力恢复速度翻倍，而且它**不报错**，只是数值悄悄不对。输入能力那边靠 `InputTagToSpecHandle` 这个映射表挡住了，被动能力没有那样的表，所以直接按类查。
- **被谁调用 / 调用谁**：`ARPG_Player::InitializeAbilitySystem`（`RPG_Player.cpp:257`）、`ARPG_Enemy`（`RPG_Enemy.cpp:164`）；自身调用 `FindAbilitySpecFromClass`、`GiveAbility`、`TryActivateAbility`。

#### `void URPG_AbilitySystemComponent::ReactivatePassiveAbilities()`

- **干什么**：把所有"授予即激活"且当前没在激活的被动能力重新拉起来。**重生时必调。**
- **关键实现**：
  1. `FScopedAbilityListLock AbilityListLock(*this);`
  2. `for (const FGameplayAbilitySpec& Spec : ActivatableAbilities.Items)`：`!Spec.Ability || Spec.IsActive()` 时 `continue`；否则 `Cast<URPG_GameplayAbilityBase>(Spec.Ability)` 取 CDO，若 `ShouldActivateOnGranted()` 为真则 `TryActivateAbility(Spec.Handle)` 并 `++ReactivatedCount`。
  3. 结尾 `UE_LOG(LogRPG_Ability, Log, TEXT("重生：重新激活 %d 个常驻被动能力"), ReactivatedCount)`。
- **为什么会有这个需求**：死亡流程会 `CancelAllAbilities()`，把包括 `GA_StaminaRegen` 在内的所有常驻能力一起停掉。但**授予关系还在** —— 能力只是不再激活。复活时如果不管它，就会出现一个非常隐蔽的问题："复活后耐力永远不再恢复"，而且不报任何错（分配点数时尤其难查：你会以为是数值配错了）。判定条件是"CDO 上标记了 `bActivateOnGranted` 且当前没在激活"，所以**重复调用是安全的** —— 正在跑的被动能力不会被重启。
- **为什么加锁**（注释全文要点）：遍历 `ActivatableAbilities.Items` 期间它不能被改大小。就目前的代码路径而言（`TryActivateAbility` 只会 `MarkAbilitySpecDirty`，不增删元素）不加锁也是安全的 —— 但"安全"是靠"恰好没有能力在激活时授予/撤销别的能力"维持的，那是个会随时间失效的前提。引擎自己在 `OnRemoveAbility` 里对"没持锁就调"是**硬断言**（`AbilitySystemComponent_Abilities.cpp:626` 的 `ensureMsgf`）；`GiveAbility` 则宽容一些 —— 持锁时它会延迟到 `AbilityPendingAdds`（同文件 `:309-314`），不持锁就自己加（`:316`）。遍历方主动持锁，才是和这两个写方都对称的写法。
- **为什么遍历 `ActivatableAbilities` 而不是角色的配置数组**：后者只有角色自己知道，ASC 不该反向依赖角色；`ActivatableAbilities` 就是"这个 ASC 现在到底有哪些能力"的权威来源。
- **为什么用 Handle 激活而不是标记 Spec 为 dirty**：`TryActivateAbility` 会走完整的 `CanActivateAbility` 检查（刚摘掉 `State.Dead`，所以能过），语义上更正确。
- **被谁调用 / 调用谁**：`ARPG_BaseCharacter`（`RPG_BaseCharacter.cpp:774`，重生流程）；自身调用 `TryActivateAbility`。

#### `bool URPG_AbilitySystemComponent::RebuildInputTagHandleMap()`

- **干什么**：从**已复制的**能力 Spec 重建"输入标签 → SpecHandle"缓存（客户端路径）。返回是否至少重建出一条。
- **关键实现**：
  1. `InputTagToAbilityClass.IsEmpty()` → 直接 `return false`。
  2. **建一张新表再整体换掉**：局部 `TMap<FGameplayTag, FGameplayAbilitySpecHandle> Rebuilt;`，遍历 `InputTagToAbilityClass`（跳过空 Value），对每个类调用引擎的 `FindAbilitySpecFromClass(Pair.Value)`，命中就 `Rebuilt.Add(Pair.Key, Spec->Handle)`。
  3. `InputTagToSpecHandle = MoveTemp(Rebuilt);`
  4. `RebuiltCount > 0` 时才打 Verbose：`"重建输入标签索引：%d 条"`。返回 `RebuiltCount > 0`。
- **为什么这么写**（三条独立理由，逐条转述）：
  - **"一个都没重建出来是正常情况，不是错误"**：可能映射表还没登记（初始化时序：能力 Spec 可能先于角色初始化复制到达），也可能能力 Spec 还没复制到。所以只在成功时打 Verbose，失败时不打任何东西 —— 调用方（`TryActivateAbilityByInputTag`）会在真正需要时给出更有用的提示。
  - **⚠️ 不加 `FScopedAbilityListLock` 是有意为之**：① 引擎自己的 `FindAbilitySpecFromClass` 也不加锁（`AbilitySystemComponent_Abilities.cpp:988`），它是公开 API，调用方遍布引擎各处；② 拿到的指针**立刻**就被消费掉了（只取 `Spec->Handle`），没有任何跨语句的持有；③ 引擎在"必须持锁"的地方会显式要求调用方传锁进去（见 `AbilitySystemComponent.h:1124` 的接口设计）。本文件的 `ReactivatePassiveAbilities` 加锁，是因为它**要遍历整个数组**；这里只是单点查找，两者要求不同。
  - **为什么整体替换而不是原地改**：原地改**永远删不掉过期的条目**。某个能力被撤销（或服务器重新授予、Handle 变了）之后，旧表里那条记录会一直留着 —— 而 `FGameplayAbilitySpecHandle::IsValid()` 只判断"不等于 `INDEX_NONE`"，它**看不出这个 Handle 还有没有对应的能力**。于是缓存命中、`TryActivateAbility(Handle)` 在引擎内部找不到 Spec、静默失败，项目这边只留一条 Verbose 日志。整体替换天然解决：这次没找到对应 Spec 的标签，就不在新表里。
- **已知局限**（注释自陈）：`FindAbilitySpecFromClass` 返回的是**第一个**类匹配的 Spec。如果两个不同的输入标签映射到**同一个 GA 类**，服务器上会授予两个独立的 Spec，而客户端这里会把两个标签都指向第一个 —— 于是其中一个标签激活的是"另一个标签的那份实例"。目前不是问题（每个输入标签对应一个独立的 GA 类），注册时有一道重复检查会警告这种配置（见 `RegisterInputAbilityMappings`）。
- **为什么用引擎的按类查找而不是自己遍历 `ActivatableAbilities`**：它内部判空、比较的是 `Spec.Ability->GetClass()`，而客户端的 `Spec.Ability` 指向的就是复制过来的能力 CDO。
- **被谁调用 / 调用谁**：`OnRep_ActivateAbilities`（客户端主路径）与 `TryActivateAbilityByInputTag`（缓存未命中时的现场重建）；自身调用 `FindAbilitySpecFromClass`。

#### `void URPG_AbilitySystemComponent::OnRep_ActivateAbilities()`（protected，override）

- **干什么**：能力列表复制到达（或又变了）时，调用 `RebuildInputTagHandleMap()` 按类反查重建输入标签索引。**客户端走这条路。**
- **关键实现**：`Super::OnRep_ActivateAbilities();` 之后直接 `RebuildInputTagHandleMap();`。
- **为什么用这个时机**：客户端的能力 Spec 是 `COND_ReplayOrOwner` 复制过来的，到达时间不确定。`OnRep_ActivateAbilities` 是引擎唯一保证"Spec 列表变了"的通知点，而且基类实现里已经处理了"Spec 还没复制好就 0.5 秒后重试"这种情况。
- **为什么必须调 `Super::`**：基类那里还负责跑"服务器已激活但客户端当时还没收到能力"的补激活队列，漏掉它会让那类激活永远丢失。
- **⚠️ 反射标记的坑（注释原文照录）**：这里**不能**再写 `UFUNCTION()` —— 实测 UHT 会直接报错：
  `"Override of UFUNCTION 'OnRep_ActivateAbilities' in parent 'UAbilitySystemComponent' cannot have a UFUNCTION() declaration above it; it will use the same parameters as the original declaration."`
  覆盖父类的反射函数时，反射信息沿用父类那份，子类只写 `virtual ... override`。
- **被谁调用 / 调用谁**：引擎复制系统；自身调用基类实现与 `RebuildInputTagHandleMap`。

#### `bool URPG_AbilitySystemComponent::TryActivateAbilityByInputTag(FGameplayTag InputTag)`（`UFUNCTION(BlueprintCallable, Category = "RPG|AbilitySystem")`）

- **干什么**：按输入标签尝试激活能力。PlayerController 和 AI 共用的入口 —— 两边都走同一条路径，行为完全一致，不会出现"玩家能放、AI 放不出来"的诡异问题。返回是否成功激活（失败原因通常是：未注册、冷却中、被标签阻断、正在被其他能力占用）。
- **关键实现**：
  1. `const FGameplayAbilitySpecHandle* Cached = InputTagToSpecHandle.Find(InputTag);` 命中且 `Cached->IsValid()` → `Handle = *Cached;`，跳到第 5 步。
  2. **未命中，先分两大类**：`!InputTagToAbilityClass.Contains(InputTag)` 时进入"情况 A"分支：
     - 再用 `const bool bMappingsNotRegisteredYet = InputTagToAbilityClass.IsEmpty();` 把 A 细分成 A1（整张表空 → 角色 GAS 还没初始化完，`RPG_Player::InitializeAbilitySystem` 在 PlayerState 还没就绪时会直接返回等下个时机，这是**正常的启动时序**）/ A2（表里有别的标签就是没有这一个 → 真的没配）。
     - `WarnedUnmappedTags` 去重后分别打两条不同的 Warning：A1 说"暂时无法激活：能力映射表还是空的 —— 角色的 GAS 尚未初始化完（通常是在等 PlayerState 复制）。这是启动时序，稍后重按即可……"；A2 说"没有绑定任何能力（映射表里已有 %d 条，不含这一条）—— 请在角色的 Startup Abilities 里加上这条映射"。
     - 返回 false。
  3. **情况 B（映射有、索引没建起来）**：现场调一次 `RebuildInputTagHandleMap()`，再 `InputTagToSpecHandle.Find(InputTag)` 重试；仍拿不到就进 `WarnedUnresolvedTags` 去重后打 Warning：`"输入标签 %s 已登记映射，但拿不到对应的能力实例 —— 通常是能力还没复制到（客户端刚进场，再按一次即可），或者服务器根本没有授予它。运行在%s，当前已授予能力 %d 个"`（`%s` 由 `IsServerSideASC(*this)` 填，`%d` 取 `ActivatableAbilities.Items.Num()`），返回 false。
  4. `Handle = *Retried;`
  5. `const bool bActivated = TryActivateAbility(Handle);`（用 Handle 而不是类激活）。
  6. Verbose 日志：`"按输入标签激活 %s：%s"`，成功/失败文案由 `bActivated` 决定。
  7. 返回 `bActivated`。
- **为什么这么写**（三条设计理由）：
  - **情况 A / B 必须分开**：两种的排查方向完全相反（改配置 vs 等复制），合成一条"没有绑定任何能力"的提示会把人带偏 —— 这正是这个 bug 之前拖了那么久才被发现的原因之一。
  - **"表里没有"也不能一口咬定是配置错误**：一口咬定"请去 Startup Abilities 里加映射"，会把一个正常的启动竞争说成配置错误，把人往完全错的方向带。
  - **值得在"按键"这条热路径上做重建**：重建只在**缓存未命中**时才跑（命中时是一次 TMap 查找，成本可忽略），而未命中本身就是异常路径。真正常见的场景是"客户端第一次按键时能力刚复制到、但 OnRep 因为时序原因没排上队"，这时候重建一次就永久修好了 —— 比让玩家自己发现"等一秒再按"要好。
  - **激活失败在这里是常态而非异常**：冷却中、耐力不足、被状态标签阻断、GA 自己的 `CanActivateAbility` 返回 false 都会走到这里，所以保持 Verbose 级别，需要时用 `Log LogRPG_Ability Verbose` 打开；真正的失败原因由 GA 内部用 Warning 报出来。
  - **两个去重集合必须分开**：否则客户端进场时先报一次"能力还没复制到"，那个标签就被永久标记了，之后真的配错也再不会报警 —— 那正好把这次特意做的诊断拆分又抹掉了。
- **被谁调用 / 调用谁**：`ARPG_PlayerController`（`RPG_PlayerController.cpp:347`）、`URPG_BTTask_Attack`（`AI/Tasks/RPG_BTTask_Attack.cpp:93`）；自身调用 `RebuildInputTagHandleMap`、`TryActivateAbility`、`IsServerSideASC`。

#### `bool URPG_AbilitySystemComponent::HasAbilityForInputTag(FGameplayTag InputTag) const`（`UFUNCTION(BlueprintPure, Category = "RPG|AbilitySystem")`）

- **干什么**：查询某个输入标签当前是否绑定了能力。
- **关键实现**：`return InputTagToAbilityClass.Contains(InputTag);` —— 查的是**映射表**，不是 Handle 缓存。
- **为什么这么写**：查缓存是错的（而且很容易写错）—— 客户端上 Handle 缓存要等能力 Spec 复制到、且 `RebuildInputTagHandleMap` 跑过之后才有内容。在那之前查缓存会返回 false，但"配置了没有"和"索引建好没有"是两个问题，前者在两端、任何时刻答案都一样。
- **被谁调用 / 调用谁**：`ARPG_PlayerController`（`RPG_PlayerController.cpp:384`）。

#### `void URPG_AbilitySystemComponent::NotifyInputReleased(FGameplayTag InputTag)`

- **干什么**：通知"某个输入被松开"，按住型能力（重击蓄力）靠它知道玩家什么时候松手。实现是：从输入标签找到对应的已激活能力实例，调用它的 `OnInputReleased()`。
- **关键实现**（四道早退，全部静默）：
  1. `InputTagToSpecHandle.Find(InputTag)` 未命中或 Handle 无效 → `return`（注释：这个输入可能压根没绑能力。静默返回 —— 按下时已经警告过一次了，松开再报一遍只是噪音）。
  2. `FindAbilitySpecFromHandle(*HandlePtr)` 为空 → `return`。
  3. `Spec->GetPrimaryInstance()` 为空 → `return`（注释：NonInstanced 的能力没有实例，直接在 CDO 上执行，拿不到可调用的对象；我们所有 GA 都是 `InstancedPerActor`，所以正常都能取到）。
  4. `Cast<URPG_GameplayAbilityBase>(Ability)` 成功才调 `RPGAbility->OnInputReleased()`（瞬发能力对这个调用无感，基类默认空实现）。
- **为什么不让 GA 自己监听输入**：那要求能力知道输入层的存在，破坏"能力不关心按键"的分层。由 ASC 充当这个翻译官更合适 —— 能力只需要知道自己"被松开了"，不需要知道是哪个键、更不需要知道是键盘还是手柄。
- **被谁调用 / 调用谁**：`ARPG_PlayerController`（`RPG_PlayerController.cpp:425`）；自身调用 `FindAbilitySpecFromHandle` 与 `URPG_GameplayAbilityBase::OnInputReleased`。

**成员变量（逐个列）**

| 名称 | 声明 | 说明 |
| --- | --- | --- |
| `InputTagToAbilityClass` | `UPROPERTY()`（**无任何复制标记**）`TMap<FGameplayTag, TSubclassOf<UGameplayAbility>>` | 输入标签 → 能力类。**只用于注册阶段**，运行期激活走下面的 Handle 表。注释要点：这张表是纯配置数据，两端都应持有；客户端靠它反查复制过来的 Spec 属于哪个输入标签（见 `RebuildInputTagHandleMap`）。 |
| `InputTagToSpecHandle` | `TMap<FGameplayTag, FGameplayAbilitySpecHandle>`，**非 UPROPERTY** | 输入标签 → 已授予能力的 SpecHandle。注册后立即填充，是运行期查找的唯一依据。 |
| `WarnedUnmappedTags` | `TSet<FGameplayTag>`，**非 UPROPERTY** | 已经警告过"**配置里没有**这个输入标签"的键。注释：这个查询会在每次按键时发生，"没绑能力"属于配置错误而不是运行时异常，值得用 Warning 级别报出来（之前用 Verbose，导致按键没反应时日志一片空白，完全无从下手）；但玩家连打时会反复触发，所以用这个集合去重。 |
| `WarnedUnresolvedTags` | `TSet<FGameplayTag>`，**非 UPROPERTY** | 已经警告过"**配置里有**但拿不到能力实例"的键。注释：★ 和上面那个**必须分开** —— 两种情况的排查方向完全相反（改配置 vs 等复制/查授权），共用一个去重集合会出现"客户端进场时先因为能力还没复制到报了一次 → 这个键被永久标记 → 之后真的配置错了也再也不会报出来"。 |

---

### `Source/RPG/AbilitySystem/RPG_AttributeSet.h`（实现：`Source/RPG/AbilitySystem/RPG_AttributeSet.cpp`）

**一句话职责**：敌我共用的属性集；持有 8 个可复制属性 + 1 个不复制元属性，并把 `PostGameplayEffectExecute` 做成**全项目唯一的扣血入口**（无敌判定 / 扣血 / 飘字 / 死亡与受击广播全在这里）。

**类/结构体**：`URPG_AttributeSet` —— 继承 `UAttributeSet`；`UCLASS()`。

**头文件里声明的数据流与机制说明（逐条转述）**

- **属性是怎么被改的**：GE 被应用 → 它的 Modifier（Additive / Multiplicative / Override）被求值 → 结果写进 BaseValue（永久值）和 CurrentValue（含 Buff 的当前值）→ `PostGameplayEffectExecute()` 被调用（我们在这里做后处理）。**关键点：Execution Calculation 捕获到的属性，是已经被所有 GE 修改过的当前值** —— 所以"伤害计算综合了攻防和 Buff/Debuff"这件事，不需要在计算里手动遍历 Buff，属性管线已经帮你算好了。这是最容易被误解的一点。
- **BaseValue 与 CurrentValue**：`BaseValue` = 所有 Instant GE 和 SetByCaller 的结果累积，不含临时 Buff；`CurrentValue` = BaseValue 叠加所有 Duration/Infinite GE 的 Modifier 后的结果。改 BaseValue → 永久生效（比如扣血）；加 Duration GE → 只影响 CurrentValue，到期自动还原（比如加攻 Buff）。
- **网络复制三件套**：每个需要同步的属性都要 ① `UPROPERTY(ReplicatedUsing = OnRep_Xxx)` 声明同步、② `void OnRep_Xxx(const FGameplayAttributeData& Old)` 处理客户端收值、③ `GetLifetimeReplicatedProps` 里 `DOREPLIFETIME_CONDITION_NOTIFY(...)` 注册规则。**三件套里最容易漏的是第 2 步**：漏了它，服务器改属性后客户端数值会同步，但不会触发任何回调 —— UI 不刷新、依赖属性变化的逻辑不执行，表现为"血条不动但实际血量已经变了"，是联机调试里最隐蔽的一类 bug。这也正是"一开始就把 ASC 挂对位置"的价值：属性集挂在 PlayerState 上，复制路径天然正确，不需要额外处理重生时的属性同步。
- **`IncomingDamage`（元属性）不参与复制**：它只是服务器上伤害计算的临时投递口，用完立即清零，同步它没有意义、还浪费带宽。

#### `URPG_AttributeSet::URPG_AttributeSet()`

- **干什么**：给所有属性写保底默认值。
- **关键实现**：依次 `InitHealth(100.f); InitMaxHealth(100.f); InitAttack(10.f); InitDefense(10.f); InitMana(100.f); InitMaxMana(100.f); InitStamina(100.f); InitMaxStamina(100.f); InitIncomingDamage(0.f);`
- **为什么这么写**：真正的数值应该由 `GE_InitAttributes` 在角色初始化时覆盖（数据驱动：改数值不用重新编译）。这里给值只保证一件事：即使忘了配那个 GE，角色也不会因为属性全为 0 而除零或瞬间暴毙。"构造函数给安全默认值 + GE 覆盖实际数值"是实际项目的标准做法 —— 纯靠 GE 初始化的话，一旦资产没配好，出现的是 NaN 之类的诡异问题，非常难查。
- **被谁调用 / 调用谁**：`ARPG_Enemy` 用 `CreateDefaultSubobject<URPG_AttributeSet>(TEXT("AttributeSet"))` 创建（`RPG_Enemy.cpp:16`）；玩家侧通过 `ARPG_PlayerState` 持有（`RPG_PlayerState.cpp:58` 的 `GetRPGAttributeSet()`）。

#### `void URPG_AttributeSet::PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue)`（override）

- **干什么**：钳制**当前值**（CurrentValue），**直接赋值**路径（`SetStamina()` 这类）走它。
- **关键实现**：`Super::PreAttributeChange(...)` 之后调 `ClampAttribute(Attribute, NewValue);`（唯一实现体在 `ClampAttribute`）。
- **为什么这么写**：它只在直接赋值路径上被调用，GE 的 Modifier **不经过这里** —— 见下面 `PreAttributeBaseChange` 的完整时序说明。
- **被谁调用 / 调用谁**：引擎；自身调用 `ClampAttribute`。

#### `void URPG_AttributeSet::PreAttributeBaseChange(const FGameplayAttribute& Attribute, float& NewValue) const`（override）

- **干什么**：钳制**基础值**（BaseValue）。**★ 这个才是 GE 修改必经的那道关。**
- **关键实现**：
  1. `Super::PreAttributeBaseChange(...)`。
  2. 先存一份 `const float Requested = NewValue;`，再 `ClampAttribute(Attribute, NewValue);`。
  3. **诊断日志**：`if (!FMath::IsNearlyEqual(NewValue, Requested))` 时用 `UE_LOG(LogRPG_Ability, Log, TEXT("[%s] 属性 %s 越界被钳制：%.2f → %.2f（合法区间 [0, 对应的 Max]）"), *GetNameSafe(GetOwningActor()), *Attribute.GetName(), Requested, NewValue)`。
- **为什么这么写**（本文件里最长、也最关键的一段论证，逐条转述）：
  - **只重写 `PreAttributeChange` 是拦不住 GE 的。**
  - **⚠️ 这两条路的区别是时序，不是"走不走"** —— 这是最容易搞错的一点：直接赋值 → `SetNumericValueChecked` → `PreAttributeChange`；GE 的 Modifier → **先把 BaseValue 写进去**（`SetAttributeBaseValue`，有聚合器时是 `Aggregator->SetBaseValue`），**之后**才由 `SetNumericValueChecked` 调到 `PreAttributeChange`。也就是说：**GE 的 Modifier 是会经过 `PreAttributeChange` 的。** 整个 GAS 插件里 `PreAttributeChange` 只有两处调用点（`AttributeSet.cpp:82` / `:95`），它们都在 `FGameplayAttribute::SetNumericValueChecked`（`AttributeSet.cpp:72-106`）里 —— 那是写属性值的**通用 setter**，GE 路径最终同样会走到它。真正的差别在时序：轮到 `PreAttributeChange` 执行时，**BaseValue 已经落盘了**，它只能纠正 CurrentValue，纠正不了 BaseValue。
  - 所以结论不变（两个都要写），但理由要换成上面这个：`PreAttributeBaseChange` 管住 BaseValue，`PreAttributeChange` 管住 CurrentValue 与直接赋值路径。引擎自己的注释也是这个意思（原文引用）：`"This function should enforce clamping (presuming you wish to clamp the base value along with the final value in PreAttributeChange)"` —— `AttributeSet.h:226-228`。
  - **漏写的后果**：耐力/生命可以突破 `[0, Max]`。① 耐力耗尽后继续攻击 → BaseValue 变成负数 → 界面读数长时间停在 0（恢复要先把负数填平），表现为"很久都不恢复"；② 耐力恢复是每 0.25 秒 +3.75 且没有上限 → 站着不动两分钟 BaseValue 能涨到几百，之后消耗 8 点根本看不出来，表现为"满耐力时消耗还是 100%"。**而且这两个症状都不会报错，只会让人觉得"数值怪怪的"。**
  - 诊断日志的用意：回答"耐力是不是跑到 [0,100] 外面去了"这类怀疑。正常情况下永不触发；一旦触发，说明数值平衡或某处配置有问题，而这条日志会直接告诉你是哪个属性、从多少被拉回多少。
- **被谁调用 / 调用谁**：引擎（`FGameplayAttribute::SetNumericValueChecked` / `SetAttributeBaseValue` 路径）；自身调用 `ClampAttribute`、`GetNameSafe(GetOwningActor())`。

#### `void URPG_AttributeSet::ClampAttribute(const FGameplayAttribute& Attribute, float& NewValue) const`（private）

- **干什么**：属性钳制规则本体 —— 上面两个 `Pre*` 回调共用同一套。
- **关键实现**（逐分支）：
  - `Attribute == GetHealthAttribute()` → `Clamp(NewValue, 0.f, GetMaxHealth())`
  - `Attribute == GetManaAttribute()` → `Clamp(NewValue, 0.f, GetMaxMana())`
  - `Attribute == GetStaminaAttribute()` → `Clamp(NewValue, 0.f, GetMaxStamina())`
  - `Attribute == GetMaxHealthAttribute() || GetMaxManaAttribute() || GetMaxStaminaAttribute()` → `FMath::Max(NewValue, 0.f)`（只保非负）
  - 其他属性（`Attack` / `Defense` / `IncomingDamage`）**不钳制**，直接落到函数末尾。
- **为什么这么写**：
  - 抽出来是为了保证"直接赋值"和"GE 修改"两条路径的行为**绝对一致**，不会出现"用 `SetStamina()` 会被钳制、用 GE 就不会"这种诡异差异 —— 那种差异查起来极其痛苦，因为两条路看起来都"应该"是同一个结果。
  - 为什么钳制放在 `Pre*AttributeChange` 而不是 `PostGameplayEffectExecute`：`Pre*AttributeChange` 的调用时机最早 —— 在属性值被真正写入**之前**修改待写入的值；代价是拿不到"是谁改的"这类上下文（那要用 `PostGameplayEffectExecute`）。另外 `NewValue` 是引用参数，**改它不会触发属性变化回调，也不会递归**。
  - **最大值类属性为什么必须单独挡**：如果被 Debuff 减成负数，上面几个 Clamp 的上下界会反转（Min > Max），`FMath::Clamp` 在那种情况下**行为未定义** —— 所以这里必须挡住。
- **被谁调用 / 调用谁**：`PreAttributeChange` 与 `PreAttributeBaseChange` 各调一次。

#### `void URPG_AttributeSet::PostGameplayEffectExecute(const FGameplayEffectModCallbackData& Data)`（override）

- **干什么**：伤害落地 —— **整个游戏里唯一的"扣血"入口**。只有元属性 `IncomingDamage` 会走到下面的逻辑；其他属性的修改（加攻、扣耐力等）到这里就直接返回了，它们由各自的使用方去响应。
- **关键实现**（严格按代码顺序）：
  1. `Super::PostGameplayEffectExecute(Data);`
  2. `if (Data.EvaluatedData.Attribute != GetIncomingDamageAttribute()) return;`
  3. `const float LocalDamage = GetIncomingDamage(); SetIncomingDamage(0.f);` —— 元属性用完立即清零，它不承载任何持久状态。
  4. `if (LocalDamage <= 0.f) return;`
  5. **解析"角色实体"**：`AActor* OwningActor = GetOwningActor();`，然后 `AActor* AvatarActor = Data.Target.GetAvatarActor();`；`!AvatarActor` 时兜底 `AvatarActor = OwningActor;`。
     - ★ 这里统一解析一次，后面的代码都用 `AvatarActor`，免得每处各写各的再漏一个。
     - 兜底覆盖的是"Avatar 还没指派上的窗口期（比如 GE 在 Possess 之前就落地）"；对敌人 `OwnerActor` 是对的，对玩家它会退化成"用 PlayerState 的位置"，聊胜于无。
     - **⚠️ `GetAvatarActor()` 内部第一句是 `check(AbilityActorInfo.IsValid())`（`AbilitySystemComponent.cpp:2184-2188`）—— AbilityActorInfo 无效时它直接断言/崩，而不是返回 null；只有 Avatar 弱指针本身为空时才返回 null。所以上面那个兜底分支覆盖的是后者，不是前者。**
  6. **无敌帧兜底检查**：`if (Data.Target.HasMatchingGameplayTag(RPGTags::State_Invulnerable))` → Verbose 日志 `"[%s] 伤害被无敌帧挡下：%.1f"` → `return`。
     - 第一道防线在 `GE_Damage` 资产上（TargetTagRequirements 组件，GE 层面直接拒绝应用，连 Execution 都不会跑，更省性能）；这里再判一次是防御性编程：万一将来有别的途径直接写 `IncomingDamage`（陷阱、脚本伤害、DOT），也不至于打穿无敌。
  7. 扣血：`const float OldHealth = GetHealth(); const float NewHealth = FMath::Clamp(OldHealth - LocalDamage, 0.f, GetMaxHealth()); SetHealth(NewHealth);`
  8. Log 日志：`"[%s] 受到 %.1f 点伤害：%.0f → %.0f"`（`LogRPG_Combat`，Log 级别）。
  9. **★ 尸体早退**：`if (OldHealth <= 0.f) { return; }` —— 已经死了就不再广播任何事件。
     - 理由（注释全文要点）：尸体是会被继续砍到的（连段的后几刀、范围伤害、别的敌人的攻击……），而这时候血量已经是 0，下面两条分支会走到"受击"那一条（因为死亡判定的条件是"从有血变成没血"，这次不满足）。后果是每一刀都广播一次 `Event.Combat.Death` → `GA_Death` 尝试激活 → 被 `ActivationBlockedTags` 里的 `State.Dead` 挡住 → 引擎通过 `AbilityFailedCallbacks` 报一条 Warning，表现是**打尸体时日志刷屏**，而且会掩盖真正有用的告警。
     - **⚠️ 因果要说准**（注释特别纠正）：注意走的是**死亡**分支不是受击分支 —— 死亡判据是 `NewHealth <= 0`，而尸体挨打时 `NewHealth` 恒为 0，判据是**成立**的。"从活到死只触发一次"这件事正是**由下面这道早退创造出来的**，不是它本来就成立 —— 把因果关系写反了会让人以为"删掉早退最多误报受击"。从语义上讲这也更对："受击"是活人才有的反应。
  10. `const bool bAuthority = OwningActor && OwningActor->HasAuthority();`
  11. **伤害飘字（只在 `bAuthority` 时）**：`Cast<ARPG_BaseCharacter>(AvatarActor)` 成功后：
      - 位置优先取命中点：`Data.EffectSpec.GetContext().GetHitResult()` 有值就用 `Hit->ImpactPoint`；否则回落到 `AvatarActor->GetActorLocation() + FVector(0.f, 0.f, VictimCharacter->GetSimpleCollisionHalfHeight())`（角色胸口高度）。注释：模拟命中、DOT、陷阱伤害都没有命中点，没有这个兜底的话那些伤害会一声不响，看起来像"打了没反应"。
      - 调 `VictimCharacter->Multicast_ShowDamageNumber(LocalDamage, NumberLocation);`
  12. **死亡判定**：`if (NewHealth <= 0.f)` 且 `bAuthority` → Log `"[%s] 生命归零，广播死亡事件"`，构造 `FGameplayEventData Payload`：`EventTag = RPGTags::Event_Combat_Death`、`Instigator = Data.EffectSpec.GetContext().GetInstigator()`、`Target = AvatarActor`、`EventMagnitude = LocalDamage`（把"最后一击的伤害"带出去，死亡表现可以据此区分轻重击），然后 `UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(OwningActor, RPGTags::Event_Combat_Death, Payload);`
  13. **否则（`else if (bAuthority)`）受击广播**：同样的 Payload 结构，`EventTag = RPGTags::Event_Combat_Hit`，`Target = AvatarActor`，`EventMagnitude = LocalDamage`，同样 `SendGameplayEventToActor(OwningActor, ...)`；之后 Verbose 日志 `"[%s] 广播受击事件（%.1f 点）"`。
- **为什么这么写**（五段论证，逐条转述，都是核心资产）：
  - **为什么要元属性中转站**：把所有伤害后处理集中到一个地方 —— `DamageExecution` 算出最终伤害 → 写进 `IncomingDamage` → `PostGameplayEffectExecute` 接住（无敌帧检查、扣 Health、将来加格挡减伤/伤害飘字/吸血/受击顿帧、死亡判定）。如果不设这个中转站而让 Execution 直接改 Health，那么每加一种后处理（格挡、无敌、飘字…）都要去改 Execution 或写一堆 GE，很快就会失控。
  - **★ "角色实体"一律取化身（Avatar），不要用 OwnerActor**：`OwningActor` 是 ASC 的 OwnerActor，而本项目的 ASC 归属是分开的 —— 敌人：ASC 在自己身上，OwnerActor 就是角色；玩家：ASC 在 `RPG_PlayerState` 上，OwnerActor 是 **PlayerState**。所以任何 `Cast<ARPG_BaseCharacter>(OwningActor)` 对**玩家**都会静默失败。这个坑已经踩过一次（伤害飘字整段被跳过，打玩家不冒数字），**而且不报任何错**。
  - **API 形态提醒**：`Data.Target` 是 `UAbilitySystemComponent&`（`GameplayEffectExtension.h:17-30`，Target 是第 29 行），**不是** `FGameplayAbilityActorInfo`，所以没有 `Data.Target.AvatarActor` 那种写法，要走 ASC 的 `GetAvatarActor()`。
  - **⚠️ 事件广播只在服务器（`bAuthority`）**：属性值的修改本身是预测的 —— 本地控制的角色在自己的客户端上会跑一遍这个函数（`PredictivelyExecuteEffectSpec`，`GameplayEffect.cpp:3069`），然后由 GAS 在预测失败时回滚。但**事件广播不会回滚** —— 委托一旦发出去就收不回来了。不做判断的话联机下会变成：客户端本地预测"我被打死了" → 播死亡蒙太奇、进布娃娃；服务器说"没死" → 回滚血量，但人已经躺下了；服务器后来真的判死 → 再躺一次。表现是"死亡/受击表现偶尔闪一下或播两遍"，而且只在延迟高的客户端上出现，非常难查。所以定下规矩：**属性集只负责广播事实，广播的公信力由服务器垄断。** 客户端要知道发生了什么靠复制（属性、标签、蒙太奇都是复制的），而不是靠自己也广播一遍。单机（Standalone）下 `HasAuthority()` 恒为 true，这条判断不产生任何影响。
  - **飘字为什么走 NetMulticast 而不是 GameplayEvent**：因为飘字是**每个客户端各自要画**的东西，而 GameplayEvent 只在服务器广播。属性集只负责把事实送到每一端，数字长什么样、往哪飘、飘多久全是 HUD 的事。**⚠️ 必须和事件一样只在服务器调用**：这个函数在预测路径上也会跑到，而 NetMulticast 在非服务器上调用的编译结果就是**在本地直接执行 `_Implementation`**（`Actor.cpp` 的 `GetFunctionCallspace`：非服务器且未标 `BlueprintAuthorityOnly` 时返回 Callspace，而不是拒绝）。于是预测的客户端先本地冒一个数字，随后服务器的权威广播又冒一个 —— 两份。单机（`NM_Standalone`）返回的是 Local，所以要保留"本地执行"这条路，不能用"只在服务器才调 RPC"以外的办法绕开。
  - **死亡为什么用 GameplayEvent 而不是直接调用死亡逻辑**：保持"表现层不做逻辑"的分层原则 —— 死亡的表现（蒙太奇、布娃娃、AI 停止）由 `GA_Death` 去处理，属性集只管广播事实。
  - **死亡/受击为什么二选一（`else if`）**：致命伤走死亡分支，其余走受击分支；两个都发的话，受击反应会和死亡蒙太奇抢动画，表现上会闪一下。
  - **`Payload.Target` 为什么用 AvatarActor 而不是 OwningActor**：`Target` 语义上是"被打的那个角色"，而玩家的 `OwningActor` 是 `PlayerState`。目前没有消费者读它，但留着就是同一个坑的下一次触发点。注意事件**发送目标**仍然是 `OwningActor`（事件发给 ASC 的持有者）。
- **被谁调用 / 调用谁**：引擎 GE 管线；自身调用 `GetIncomingDamage` / `SetIncomingDamage` / `GetHealth` / `SetHealth` / `GetMaxHealth` / `GetOwningActor` / `Data.Target.GetAvatarActor` / `Data.Target.HasMatchingGameplayTag` / `Data.EffectSpec.GetContext().GetHitResult()` / `Data.EffectSpec.GetContext().GetInstigator()` / `ARPG_BaseCharacter::Multicast_ShowDamageNumber` / `UAbilitySystemBlueprintLibrary::SendGameplayEventToActor`。

#### `void URPG_AttributeSet::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const`（override）

- **干什么**：注册 8 个属性的复制规则。
- **关键实现**：`Super::GetLifetimeReplicatedProps(OutLifetimeProps);` 之后 8 行 `DOREPLIFETIME_CONDITION_NOTIFY(URPG_AttributeSet, X, COND_None, REPNOTIFY_Always);`，依次为 `Health / MaxHealth / Attack / Defense / Mana / MaxMana / Stamina / MaxStamina`。`IncomingDamage` **有意不注册**。
- **为什么这么写**：
  - `COND_None` —— 复制给所有客户端。不能图省事用 `COND_OwnerOnly`：血条、队友状态、敌人血量都需要被别人看到。
  - `REPNOTIFY_Always` —— 即使新值和旧值相同也触发 `OnRep`。不用默认的 `REPNOTIFY_OnChanged` 的原因：GAS 的属性有 BaseValue / CurrentValue 两层，有时表面数值没变但底层状态变了（比如 Buff 叠加的中间态），用 Always 能保证客户端不漏更新，代价只是极少量冗余回调 —— 对属性同步完全值得。
  - `IncomingDamage` 不注册的理由：它只是服务器上伤害计算的临时投递口，用完立即清零，同步它没有意义。
- **被谁调用 / 调用谁**：引擎复制系统。

#### 8 个 `OnRep_*` 实现

- 签名统一为 `UFUNCTION() virtual void OnRep_X(const FGameplayAttributeData& OldX);`，声明在头文件、实现逐个写在 .cpp 里，函数体统一是 `GAMEPLAYATTRIBUTE_REPNOTIFY(URPG_AttributeSet, X, OldX);`。共 8 个：`OnRep_Health` / `OnRep_MaxHealth` / `OnRep_Attack` / `OnRep_Defense` / `OnRep_Mana` / `OnRep_MaxMana` / `OnRep_Stamina` / `OnRep_MaxStamina`。
- **干什么**：客户端收到同步值时的处理。
- **关键实现**：`GAMEPLAYATTRIBUTE_REPNOTIFY` 宏内部做两件事 —— ① `SetBaseAttributeValueFromReplication`，把同步过来的值写进本地属性；② 广播 `OnGameplayAttributeValueChange` 委托，所以 UI 只要注册了那个委托就会自动刷新，不需要手写任何同步逻辑。
- **为什么这么写**：注释明确两点 —— ① 只在客户端被调用（服务器是数据源，不会回调自己）；② 每个属性单独写一个（而不是用宏批量生成），是为了让"哪个属性漏了 OnRep"在代码里一眼可见 —— 漏 OnRep 不会编译报错，但客户端界面永远不刷新。
- **被谁调用 / 调用谁**：引擎复制系统（由 `ReplicatedUsing` 指定）；内部调用 `GAMEPLAYATTRIBUTE_REPNOTIFY` 宏展开的两个引擎函数。

**属性成员（逐个列，全部为 `FGameplayAttributeData` + `ATTRIBUTE_ACCESSORS_BASIC(URPG_AttributeSet, X)`）**

| 属性 | UPROPERTY 标记 | 说明 / 注释原文 |
| --- | --- | --- |
| `Health` | `BlueprintReadOnly, Category = "RPG\|Attributes\|Health", ReplicatedUsing = OnRep_Health` | 生命当前值，钳制 `[0, MaxHealth]` |
| `MaxHealth` | `BlueprintReadOnly, Category = "RPG\|Attributes\|Health", ReplicatedUsing = OnRep_MaxHealth` | 生命上限，钳制 `>= 0` |
| `Attack` | `BlueprintReadOnly, Category = "RPG\|Attributes\|Combat", ReplicatedUsing = OnRep_Attack` | 攻击力。伤害计算的基础值：`BaseDamage = Attack × 攻击段倍率` |
| `Defense` | `BlueprintReadOnly, Category = "RPG\|Attributes\|Combat", ReplicatedUsing = OnRep_Defense` | 防御值。参与减伤公式 `Mitigation = Defense / (Defense + K)`，K 默认 100 |
| `Mana` | `BlueprintReadOnly, Category = "RPG\|Attributes\|Mana", ReplicatedUsing = OnRep_Mana` | 法力当前值（"黑神话式的 3 个法术消耗"），钳制 `[0, MaxMana]` |
| `MaxMana` | `BlueprintReadOnly, Category = "RPG\|Attributes\|Mana", ReplicatedUsing = OnRep_MaxMana` | 法力上限，钳制 `>= 0` |
| `Stamina` | `BlueprintReadOnly, Category = "RPG\|Attributes\|Stamina", ReplicatedUsing = OnRep_Stamina` | 耐力当前值。注释：闪避/攻击/跳跃分次消耗，奔跑/蓄力持续消耗，停手 3 秒后缓慢恢复。钳制 `[0, MaxStamina]` |
| `MaxStamina` | `BlueprintReadOnly, Category = "RPG\|Attributes\|Stamina", ReplicatedUsing = OnRep_MaxStamina` | 耐力上限，钳制 `>= 0` |
| `IncomingDamage` | `BlueprintReadOnly, Category = "RPG\|Attributes\|Meta"`，**无 ReplicatedUsing、不注册复制** | 伤害中转站，**不是真正的属性**，只是一个"投递口"。注释给出的完整分工：`DamageExecution` 算出最终伤害 → 写进 `IncomingDamage` → `PostGameplayEffectExecute` 接住，做无敌帧检查 / 扣 Health / （将来加：格挡减伤、伤害飘字、吸血、受击顿帧）/ 死亡判定。注意：它用完立即清零，不承载任何持久状态 |

**静态常量**

- `static constexpr float DefenseConstant = 100.f;` —— 伤害计算里防御值的软化常数 K（**减伤 50% 所需的防御值**）。注释：放这里是为了让 Execution 能读到。
- `static constexpr float MinDamage = 1.f;` —— 单次伤害的保底值，避免高防目标完全免伤导致打不动。

---

### `Source/RPG/AbilitySystem/Abilities/RPG_GameplayAbilityBase.h`（实现：`Source/RPG/AbilitySystem/Abilities/RPG_GameplayAbilityBase.cpp`）

**一句话职责**：项目所有 GameplayAbility 的基类，提供常用查询快捷方式、蒙太奇播放的空检查封装、伤害施加与耐力消耗的统一入口、联机策略的默认值。

**类/结构体**：`URPG_GameplayAbilityBase` —— 继承 `UGameplayAbility`；`UCLASS(Abstract)`。

**头文件注释里的两个总纲**

- **它提供什么**：常用查询的快捷方式（角色 / 战斗组件 / 属性集 / 攻击模组）；蒙太奇播放的空检查封装（没有动画也能跑逻辑）；伤害施加与耐力消耗的统一入口（避免每个 GA 各写一遍）；联机策略的默认值。
- **关于"没有蒙太奇也能跑"**：`PlayMontageOrSkip` 在蒙太奇为空时返回 `nullptr` 并打一条日志，GA 侧据此跳过"播放动画 + 监听动画事件"，但**其余逻辑照常执行**。这不是妥协，而是刻意的设计：让数值链路（连段推进、伤害结算、耐力消耗）可以脱离动画独立验证。做动画时如果发现某个 Notify 位置不对，只会影响表现，不会连带数值一起崩 —— 排查范围小得多。

#### `URPG_GameplayAbilityBase::URPG_GameplayAbilityBase()`

- **干什么**：写死实例化策略、联机策略与一条全局阻断标签。
- **关键实现**（四项）：
  1. `InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;`
  2. `NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;`
  3. `NetSecurityPolicy = EGameplayAbilityNetSecurityPolicy::ClientOrServer;`
  4. `ActivationBlockedTags.AddTag(RPGTags::State_Dead);`
- **为什么这么写**（逐条转述）：
  - **实例化策略**：⚠️ UE 5.8 的 `UGameplayAbility` 构造函数把默认值设成了 `InstancedPerExecution`（见 `GameplayAbility.cpp:102`），这对我们**完全不适用**。引擎头文件原话的区别：`InstancedPerActor` 每个 Actor 一个实例、同一时刻只能有一个激活、**状态在激活之间保留**；`InstancedPerExecution` 每次执行都实例化、可同时运行多个、**状态不保留**。对我们的连段系统来说 `InstancedPerExecution` 是灾难性的：连按三次会创建三个实例同时跑（各自播各的动画、各扣各的耐力）；连段进度（`CurrentSegmentIndex`）存在实例成员里，新实例永远从第 1 段开始；`bComboWindowOpen` 之类的窗口状态也全部丢失。表现就是"连按没有衔接，每次都从第一段重来"。所以显式设回 `InstancedPerActor` —— 连段状态必须跨激活保留，而且我们**不希望**同一个攻击能力有多个实例同时跑。
  - **联机策略**：`LocalPredicted`（本地预测）—— 客户端按下按键后**立刻在本地执行**，不等服务器往返，这是动作游戏手感的前提（攻击、闪避如果等一个 RTT 才响应，玩家的感受就是"按键延迟很高"）；服务器同时执行一份权威版本，不一致时纠正客户端。**敌人 AI 的能力需要在自己的类里覆盖成 `ServerOnly`** —— AI 只在服务器跑，客户端执行它毫无意义还会造成表现重复。
  - **安全策略**：`ClientOrServer` 允许客户端主动请求激活；攻击、闪避需要这个，纯服务器逻辑的能力应改成 `ServerOnly` 防伪造。
  - **阻断标签**：注意这里是**实例级**的阻断（比 GE 的标签要求更轻量），适合"死亡后不能用任何技能"这类全局规则。
- **被谁调用 / 调用谁**：所有 `RPG_GA_*` 子类的构造链。

#### `bool URPG_GameplayAbilityBase::CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayTagContainer* SourceTags = nullptr, const FGameplayTagContainer* TargetTags = nullptr, OUT FGameplayTagContainer* OptionalRelevantTags = nullptr) const`（override）

- **干什么**：在引擎检查之上补一条死亡检查和两处诊断日志。
- **关键实现**（三个分支）：
  1. `if (!Super::CanActivateAbility(...))` → Warning 日志 `"[%s] CanActivateAbility 被引擎拒绝。原因标签：%s"`，原因取 `OptionalRelevantTags->ToStringSimple()`，为空时输出 `"(空) —— 常见原因：标签阻断 / 冷却中 / Cost 资源不足 / 未满足 ActivationRequiredTags"` → 返回 false。
  2. `if (!ActorInfo || !ActorInfo->AbilitySystemComponent.IsValid())` → Warning `"[%s] CanActivateAbility 失败：传入的 ActorInfo 无效，或它没有关联 ASC"` → 返回 false。
  3. `const UAbilitySystemComponent* ASC = ActorInfo->AbilitySystemComponent.Get(); if (ASC->HasMatchingGameplayTag(RPGTags::State_Dead))` → Verbose `"[%s] CanActivateAbility 失败：角色处于死亡状态"` → 返回 false。
  4. 全过 → 返回 true。
- **为什么这么写**（本文件最重要的两条踩坑记录，逐条转述）：
  - **父类拒绝时必须把原因打出来**：引擎在这里只会往 `OptionalRelevantTags` 填一个标签就返回 false，不打印任何东西 —— 表现为"按键没反应，日志一片空白"，排查时完全无从下手（这个坑我们踩过）。
  - **⚠️ 这里必须用参数里的 `ActorInfo`，绝不能用 `GetAbilitySystemComponentFromActorInfo()`**：`CanActivateAbility` 的调用时机比直觉更早 —— 它在**能力实例化之前**就要判断"这个能力现在能不能激活"，那时 `this` 指向的是 CDO（类默认对象），而 CDO 的 `CurrentActorInfo` **永远是空的**。所以凡是走 `CurrentActorInfo` 的便捷函数（`GetAbilitySystemComponentFromActorInfo`、`GetAvatarActorFromActorInfo` 等）在这里全部返回 nullptr —— 结果是**能力永远无法激活**，而且日志里的表现极具误导性：会显示成"拿不到 ASC / ActorInfo 没初始化"，让人往角色初始化方向去查，实际上调用方传进来的 ActorInfo 完全是好的。记住规则：**`CanActivateAbility` 是 const 函数，它只能依赖参数，不能依赖任何实例状态。**
  - **为什么还要再判一次死亡**：虽然基类已经用 `ActivationBlockedTags` 加了 `State.Dead`，但标签阻断依赖 GE 正确授予标签 —— 如果将来有别的途径让角色"死亡但没标签"（比如脚本直接扣血到 0 而没走属性集），这道检查能兜住。
- **被谁调用 / 调用谁**：引擎（`TryActivateAbility` 内部）；自身调用 `Super::CanActivateAbility`、`ActorInfo->AbilitySystemComponent.Get()`、`HasMatchingGameplayTag`。

#### `virtual void URPG_GameplayAbilityBase::OnInputReleased()`（头文件内联空实现）

- **干什么**：输入释放回调，供按住型能力（重击蓄力）重写。
- **关键实现**：函数体为 `{}`（**空实现而不是纯虚函数**）。
- **为什么这么写**：瞬发能力不需要处理，所以给空实现；调用链见下面"被谁调用"。注释给出的调用链：`PlayerController` 收到 EnhancedInput 的 `Completed` 事件 → `ASC::NotifyInputReleased(InputTag)` → 找到该输入标签对应的激活中能力实例 → 本函数。**注意：只有能力正在激活中才会收到这个回调。如果松开时能力已经结束，调用链在 `Spec->GetPrimaryInstance()` 那一步就断了，不会走到这里。**
- **被谁调用 / 调用谁**：`URPG_AbilitySystemComponent::NotifyInputReleased`；重写者如 `URPG_GA_HeavyAttack`。

#### `bool URPG_GameplayAbilityBase::ShouldActivateOnGranted() const`（`UFUNCTION(BlueprintPure, Category = "RPG|Ability")`，头文件内联）

- **干什么**：返回 `bActivateOnGranted`。
- **为什么用 getter 而不是把 `bActivateOnGranted` 直接公开**：保持"配置只能由子类和编辑器改、外部只读"的边界 —— 否则任何代码都能在运行时把某个能力改成被动，那会很难排查。
- **被谁调用 / 调用谁**：`URPG_AbilitySystemComponent::RegisterInputAbility`、`GivePassiveAbility`（取 CDO 后判断）、`ReactivatePassiveAbilities`。

#### `ARPG_BaseCharacter* URPG_GameplayAbilityBase::GetRPGCharacter() const`（`UFUNCTION(BlueprintPure, Category = "RPG|Ability")`）

- **干什么**：能力持有者对应的 RPG 角色，取不到返回 nullptr。
- **关键实现**：`return Cast<ARPG_BaseCharacter>(GetAvatarActorFromActorInfo());`
- **为什么这么写**：`AvatarActor` 才是"能力通过什么身体表现"，玩家和敌人都是角色本身。**不要用 `OwnerActor` —— 玩家的 Owner 是 `PlayerState`，不是角色。**
- **⚠️ 使用限制**（头文件注释）：只能在 **`ActivateAbility` 及之后**调用（包括各种事件回调）。它依赖 `CurrentActorInfo`，而 `CanActivateAbility` 可能在 CDO 上执行，那时 `CurrentActorInfo` 是空的，会返回 nullptr —— 详见 `CanActivateAbility` 的说明。
- **被谁调用 / 调用谁**：`RPG_GA_Death`（`:57`、`:182`）、`RPG_GA_HitReact`（`:90`）、`RPG_GA_Sprint`（`:53`、`:82`）；自身调用引擎的 `GetAvatarActorFromActorInfo`。（`ARPG_PlayerController` 也有同名函数，但那是 PC 自己的实现，与此无关。）

#### `URPG_CombatComponent* URPG_GameplayAbilityBase::GetCombatComponent() const`（`UFUNCTION(BlueprintPure, Category = "RPG|Ability")`）

- **干什么**：取战斗组件（输入缓存 + 连段索引），取不到返回 nullptr。
- **关键实现**：`const ARPG_BaseCharacter* RPGChar = GetRPGCharacter(); return RPGChar ? RPGChar->GetCombatComponent() : nullptr;`
- **被谁调用 / 调用谁**：`RPG_GA_HeavyAttack`（`:102`）、`RPG_GA_LightAttack`（`:87`、`:158`、`:433`、`:480`、`:701`）；自身调用 `GetRPGCharacter`、`ARPG_BaseCharacter::GetCombatComponent`。

#### `URPG_AttributeSet* URPG_GameplayAbilityBase::GetRPGAttributeSet() const`（`UFUNCTION(BlueprintPure, Category = "RPG|Ability")`）

- **干什么**：取属性集。
- **关键实现**：`const UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo(); return ASC ? const_cast<URPG_AttributeSet*>(ASC->GetSet<URPG_AttributeSet>()) : nullptr;`（`const_cast` 是因为 `GetSet<T>()` 返回 const 指针，而本函数对外承诺非 const）。
- **被谁调用 / 调用谁**：`RPG_GA_Sprint`（`:37`、`:94`）、基类自身的 `HasEnoughStamina`。注意 `RPG_GA_Dodge.cpp:53` 有一条注释说明它**刻意不走**这个函数（"那个依赖 `CurrentActorInfo`"），改用别的途径取属性集。

#### `URPG_AttackModuleData* URPG_GameplayAbilityBase::GetAttackModule() const`（`UFUNCTION(BlueprintPure, Category = "RPG|Ability")`）

- **干什么**：取当前攻击模组。
- **关键实现**：`const URPG_CombatComponent* Combat = GetCombatComponent(); return Combat ? Combat->GetAttackModule() : nullptr;`
- **被谁调用 / 调用谁**：`RPG_GA_HeavyAttack`（`:103`、`:190`、`:242`、`:291`、`:353`、`:462`、`:619`、`:698`）、`RPG_GA_LightAttack`（`:88`、`:233`、`:434`、`:600`）；自身调用 `GetCombatComponent`、`URPG_CombatComponent::GetAttackModule`。

#### `UAbilityTask_PlayMontageAndWait* URPG_GameplayAbilityBase::PlayMontageOrSkip(UAnimMontage* Montage, FName TaskName, float Rate = 1.f)`（protected）

- **干什么**：播放蒙太奇并返回 Task，调用方负责绑定委托并 `ReadyForActivation`。
- **关键实现**（三个分支）：
  1. `!Montage` → Verbose 日志 `"[%s] 未配置蒙太奇，跳过动画播放（数值逻辑照常执行）"` → 返回 `nullptr`。
  2. **混合时间体检**（花括号局部块）：取 `Montage->GetPlayLength()`、`GetDefaultBlendInTime()`、`GetDefaultBlendOutTime()`，当 `MontageLength > 0.f && (BlendIn + BlendOut) > MontageLength * 0.5f` 时打一条 Warning（详细列出淡入、淡出、合计、全长，并给出修法）。
  3. `UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(this, TaskName, Montage, Rate, NAME_None, true)` 并返回。最后一个实参 `true` 对应代理的 `bStopWhenAbilityEnds`（参数名以引擎签名为准）。
- **为什么这么写**：
  - **没有蒙太奇不是错误**：这是"数值链路先行、动画后补"的正常状态，打 Verbose 而不是 Warning，避免开发期刷屏。
  - **混合时间体检的理由**（注释重点）：新建的蒙太奇，Blend In / Blend Out 默认各是 **0.25 秒**（`UAnimMontage` 构造函数，`AnimMontage.cpp:76-77`）。对一段 0.6 秒的攻击动画来说：0.00~0.25 秒从移动姿势淡入（此时角色大半还保持站姿）、0.25~0.35 秒是唯一"打满"的 0.1 秒、0.35~0.60 秒淡出回移动姿势；结果就是"按了攻击键，人物只是微微动了一下"。**它不是 bug**，是引擎默认值不适合短促的攻击动作 —— 但肉眼分不清"动画太短"和"被混合吃掉了"，所以在这里让代码自己报出来。放在这个函数里是因为它是**所有蒙太奇的必经之路**，检查写一处就覆盖了轻击 / 重击 / 切手技 / 闪避全部路径。
  - **UE 5.8 没有 `PlayMontageAndWaitForEvent`**（那是 UE4 社区插件的类），所以"播动画"和"收 GameplayEvent"必须拆成两个 Task：这里只负责播，事件监听由调用方另外建 `WaitGameplayEvent`。
  - `Rate` 参数：1 = 原速，<1 放慢，>1 快放；默认 1，所以攻击类调用点不用改；受击/死亡蒙太奇会用角色上配的速率（见 `RPG_BaseCharacter`）。
- **被谁调用 / 调用谁**：`RPG_GA_Death`（`:112`，带 `Character->GetDeathMontagePlayRate()`）、`RPG_GA_Dodge`（`:105`）、`RPG_GA_HeavyAttack`（`:220`、`:263`、`:392`、`:713`）、`RPG_GA_HitReact`（`:99`，带 Rate）、`RPG_GA_LightAttack`（`:309`）；自身调用 `UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy`。

#### `void URPG_GameplayAbilityBase::ApplyDamageToTarget(AActor* Target, float DamageMultiplier, const FHitResult* HitResult = nullptr)`（protected）

- **干什么**：对目标施加伤害。内部构造 `GE_Damage` 的 Spec，通过 `SetByCaller(Data.Damage.Multiplier)` 传入倍率 —— 这样**一个 `GE_Damage` 资产就能服务所有攻击段**，不需要为每段做一个 GE。
- **关键实现**（按顺序）：
  1. `!Target` → 静默 return。
  2. `!DamageEffectClass` → Error `"[%s] 没有配置 DamageEffectClass，伤害无法施加"` → return。
  3. `UAbilitySystemComponent* SourceASC = GetAbilitySystemComponentFromActorInfo(); UAbilitySystemComponent* TargetASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Target);` 任一为空 → Verbose `"[%s] 伤害施加失败：源或目标的 ASC 为空"` → return。
  4. `FGameplayEffectContextHandle Context = SourceASC->MakeEffectContext(); Context.AddSourceObject(this);`，若 `HitResult` 非空则 `Context.AddHitResult(*HitResult)`。
  5. `SourceASC->MakeOutgoingSpec(DamageEffectClass, GetAbilityLevel(), Context)`；`!SpecHandle.IsValid()` → Error `"[%s] 伤害 GE 的 Spec 创建失败：%s"` → return。
  6. `SpecHandle.Data->SetSetByCallerMagnitude(RPGTags::Data_Damage_Multiplier, DamageMultiplier);`
  7. `SourceASC->ApplyGameplayEffectSpecToTarget(*SpecHandle.Data.Get(), TargetASC);`
  8. Verbose 日志 `"[%s] 对 %s 施加伤害，倍率 %.2f"`。
- **为什么这么写**：
  - 把命中信息塞进 `EffectContext` 是为了 GameplayCue 能拿到命中点来播特效；不传的话特效只能播在角色根位置（表现上会明显不对）。
  - 用 `SetByCaller` 传倍率是"一个 GE 服务所有攻击段"的关键：`GE_Damage` 里的 Modifier 不写死数值，而是读 `Data.Damage.Multiplier`，具体值由调用方在运行时填入。这个 `HitResult` 也正是在 `URPG_AttributeSet::PostGameplayEffectExecute` 里被飘字逻辑读出来当数字出现位置的（注释互相呼应）。
  - `GetAbilityLevel()` 作为 GE 的 Level —— 与 `FGameplayAbilitySpec(AbilityClass, Level)` 里的 Level 同源。
- **被谁调用 / 调用谁**：`RPG_GA_HeavyAttack::`（`:748`，带 `&Hit`）、`RPG_GA_LightAttack`（`:581`，`nullptr`；`:746`，带 `&Hit`）、以及本类的 `PerformSimulatedMeleeHit`；自身调用 `MakeEffectContext` / `MakeOutgoingSpec` / `ApplyGameplayEffectSpecToTarget`。

#### `void URPG_GameplayAbilityBase::ConsumeStamina(float Amount)`（protected）

- **干什么**：消耗耐力，走 `GE_StaminaCost`，通过 `SetByCaller(Data.Stamina.Cost)` 传消耗量，并顺带刷新"恢复阻断"。
- **关键实现**：
  1. `Amount <= 0.f` → return。
  2. `!StaminaCostEffectClass` → Warning `"[%s] 没有配置 StaminaCostEffectClass，耐力不会被扣除"` → return。
  3. `ASC` 为空 → return。
  4. 建 Context（`MakeEffectContext` + `AddSourceObject(this)`）→ `MakeOutgoingSpec(StaminaCostEffectClass, GetAbilityLevel(), Context)` → `!IsValid()` return。
  5. `SpecHandle.Data->SetSetByCallerMagnitude(RPGTags::Data_Stamina_Cost, -FMath::Abs(Amount));` → `ASC->ApplyGameplayEffectSpecToSelf(*SpecHandle.Data.Get());`
  6. 若 `StaminaRegenDelayEffectClass` 非空：**复用同一个 Context** 再 `MakeOutgoingSpec(StaminaRegenDelayEffectClass, GetAbilityLevel(), Context)`，`IsValid()` 时 `ApplyGameplayEffectSpecToSelf`。
  7. Verbose 日志 `"[%s] 消耗耐力 %.1f"`。
- **为什么这么写**：
  - **⚠️ 必须传负数**：`GE_StaminaCost` 的 Modifier Op 是 Additive，`SetByCaller` 的值会被直接加到 `Stamina` 上。消耗是减少，所以取负 —— **传正数会变成"攻击反而回耐力"**，而且属性集那边检测不到"耐力减少"，3 秒恢复阻断也不会触发。用 `-FMath::Abs()` 而不是 `-Amount`，是为了防止调用方不小心传了负数进来变成"负负得正"。
  - **刷新"恢复阻断"**：每消耗一次耐力就重新挂一次阻断 GE。因为它是"刷新持续时间"式的堆叠，连续消耗会把阻断时间不断往后推 —— 这正是我们想要的：只要玩家还在动作，耐力就不会恢复（注释类比《暗区突围》的耐力恢复）。恢复侧（`GE_StaminaRegen`）通过 `OngoingTagRequirements` 检查 `State.Stamina.Blocked` 来决定是否生效，所以这条链路里**没有任何计时代码** —— 时间纯粹由 GE 的 Duration 表达。
- **被谁调用 / 调用谁**：`RPG_GA_Dodge`（`:95`）、`RPG_GA_HeavyAttack`（`:198`、`:302` 蓄力持续消耗、`:382`）、`RPG_GA_Jump`（`:75`）、`RPG_GA_LightAttack`（`:297`）、`RPG_GA_Sprint`（`:92`，定时器驱动）；自身调用 `MakeOutgoingSpec` / `ApplyGameplayEffectSpecToSelf`。

#### `bool URPG_GameplayAbilityBase::HasEnoughStamina(float Amount) const`（protected）

- **干什么**：检查耐力是否足够，用于前置判断 —— 不够就干脆不激活，而不是激活后再扣成负数。
- **关键实现**：`const URPG_AttributeSet* Attributes = GetRPGAttributeSet();` 为空时**返回 true（放行）**；否则 `return Attributes->GetStamina() >= Amount;`
- **为什么这么写**：
  - **拿不到属性集时放行而不是拦截**：拿不到属性集说明初始化有问题，那是另一个 bug；在这里拦下来会让"技能全放不出来"，反而掩盖了真正的原因。
  - **⚠️ 本方法依赖 `CurrentActorInfo`，因此不能在 `CanActivateAbility` 里调用**（那个时机 `this` 可能是 CDO，`CurrentActorInfo` 为空，会拿不到属性集）。目前它只被当作"激活后"的辅助判断使用；如果将来要在 `CanActivateAbility` 里做耐力前置检查，需要改成接收参数里的 `ActorInfo` 版本。
- **被谁调用 / 调用谁**：`RPG_GA_HeavyAttack.cpp:320`（`if (!HasEnoughStamina(1.f))`，蓄力途中检查）；自身调用 `GetRPGAttributeSet`、`URPG_AttributeSet::GetStamina`。

#### `void URPG_GameplayAbilityBase::PerformSimulatedMeleeHit(float DamageMultiplier, float ForwardOffset = 150.f, float Radius = 80.f)`（protected）

- **干什么**：没有蒙太奇时的简化命中检测 —— 在角色身前做一次球形检测并施加伤害。
- **关键实现**：
  1. `AActor* Avatar = GetAvatarActorFromActorInfo(); UWorld* World = Avatar ? Avatar->GetWorld() : nullptr;` 任一为空 → return。
  2. `const FVector Origin = Avatar->GetActorLocation() + Avatar->GetActorForwardVector() * ForwardOffset;`
  3. `FCollisionQueryParams Params(TEXT("RPGSimulatedHit"), /*bTraceComplex*/ false, Avatar);`（第三个参数即忽略自己）。
  4. `FCollisionShape Shape = FCollisionShape::MakeSphere(Radius);`
  5. `World->OverlapMultiByChannel(Overlaps, Origin, FQuat::Identity, ECC_Pawn, Shape, Params);`
  6. **去重**：`TSet<AActor*> UniqueTargets;` 遍历 `Overlaps`，`Overlap.GetActor()` 为空或已见过就跳过，否则加入集合并 `ApplyDamageToTarget(HitActor, DamageMultiplier, nullptr);`（注意**命中信息传 nullptr**，所以飘字会走"角色位置 + 半高"的兜底）。
  7. `UniqueTargets.Num() == 0` 时 Verbose 日志 `"[%s] 模拟命中：身前没有目标"`。
- **为什么这么写**：
  - **⚠️ 这是纯开发期辅助，不是最终实现**。它的价值在于让连段推进、伤害结算、耐力消耗三条链路在动画做好之前就能独立验证 —— 动画接上后由 `WeaponTrace` 接管，这个函数不再被调用。
  - 去重是因为一次 `OverlapMultiByChannel` 可能对同一个 Actor 返回多个结果（多个碰撞体）。
- **被谁调用 / 调用谁**：`RPG_GA_HeavyAttack.cpp:231`（`ForwardOffset 180.f, Radius 90.f`）与 `:404`（`ForwardOffset 180.f, Radius 100.f`）—— 都是"蒙太奇缺失时的降级路径"；自身调用 `ApplyDamageToTarget`。

**GE 配置与被动能力成员（逐个列，全部 `EditDefaultsOnly, BlueprintReadOnly`）**

| 成员 | 类型 / 标记 | 说明 |
| --- | --- | --- |
| `DamageEffectClass` | `TSubclassOf<UGameplayEffect>`，`Category = "RPG\|Effects"` | 伤害 GE。注释：**里面挂的是 `RPG_DamageExecution`** |
| `StaminaCostEffectClass` | `TSubclassOf<UGameplayEffect>`，`Category = "RPG\|Effects"` | 耐力消耗 GE（Instant，SetByCaller 传消耗量） |
| `StaminaRegenDelayEffectClass` | `TSubclassOf<UGameplayEffect>`，`Category = "RPG\|Effects"` | 耐力恢复阻断 GE（Duration）。注释：每次消耗耐力后重新挂一次，**它的持续时间就是"停手多久才开始恢复"**；恢复用的 `GE_StaminaRegen` 通过 `OngoingTagRequirements` 检查它授予的 `State.Stamina.Blocked` 标签来决定是否生效。于是"停手 3 秒后缓慢恢复"这条规则**完全由标签驱动**：不需要 Tick、不需要计时器、不需要一行判断代码，而且联机下天然正确 —— 标签会复制，进度条在两端表现一致。**⚠️ 这个 GE 需要配成"每次应用刷新持续时间"（Stacking 相关设置），否则连续消耗时阻断时间不会延长。** |
| `bActivateOnGranted` | `bool`，`= false`，`Category = "RPG\|Ability"` | 授予后立刻自动激活。注释：用于耐力恢复这类**常驻被动能力** —— 它们没有输入触发，需要在角色初始化时就开始工作；激活后会一直保持激活状态（GA 内部不调 `EndAbility`），直到角色死亡或能力被强制结束。外部只读（经 `ShouldActivateOnGranted()` 访问） |

---

### `Source/RPG/AbilitySystem/Effects/RPG_DamageExecution.h`（实现：`Source/RPG/AbilitySystem/Effects/RPG_DamageExecution.cpp`）

**一句话职责**：自定义 Execution Calculation，算 `最终伤害 = 攻击方.Attack × 倍率 × (1 - 防御方.Defense / (防御方.Defense + K))` 并写进元属性 `IncomingDamage`；**只负责算，不负责扣血**。

**类/结构体**：`URPG_DamageExecution` —— 继承 `UGameplayEffectExecutionCalculation`；`UCLASS()`。

**头文件里的四段论证（逐条转述）**

- **为什么伤害用 Execution 而不是普通 Modifier**：普通 Modifier 只能做"属性 A 加减乘除一个值"这种线性运算，而伤害需要同时读**双方的属性**并做非线性计算；这种跨 Actor、非线性的公式只能靠 Execution。
- **Buff/Debuff 不需要在这里手动遍历 —— 这是最容易被误解的一点**：捕获到的 `Attack` 和 `Defense` **已经是被所有 GE 修改过的当前值**。加攻 Buff 是通过 GE 的 Modifier 提升 `Attack` 属性的，减防 Debuff 同理 —— 属性管线在 GE 求值阶段就处理完了，Execution 只需要读最终值。所以"综合防御值、buff、debuff 得出最终伤害"这件事，代码上只体现为两行 `AttemptCalculateCapturedAttributeMagnitude`。
- **Snapshot 的取舍**：本 Execution 的两个属性都用 `Snapshot = true`，含义是"在 GE 被应用的瞬间捕获属性值，之后属性变化不影响本次计算"。**为什么伤害要用 Snapshot**：玩家按下攻击键那一刻的 `Attack` 应该被**锁定**；否则如果一个投射物在飞行途中玩家吃了减攻 Debuff，伤害会莫名变低 —— 玩家无法理解"为什么我按的时候伤害是 100，打出去变成 60"。**什么时候不该用 Snapshot**：持续型效果（DOT）通常需要实时反映当前状态，那才用 `Snapshot = false`。
- **它只负责算，不负责扣血**：Execution 把最终伤害写进元属性 `IncomingDamage`，然后由 `URPG_AttributeSet::PostGameplayEffectExecute` 统一处理扣血、无敌检查、死亡判定、将来的飘字与吸血。这样所有伤害后处理集中在一处，加新机制不用回来改这个文件。

#### `struct FRPGDamageStatics`（.cpp 内文件作用域结构体，非 UCLASS）

- **干什么**：集中管理属性捕获定义。
- **关键实现**：
  - 三个 `DECLARE_ATTRIBUTE_CAPTUREDEF(Attack); / (Defense); / (IncomingDamage);` —— 宏生成两个成员：`FProperty* XxxProperty;`（属性的反射信息）与 `FGameplayEffectAttributeCaptureDefinition XxxDef;`（捕获配置，含 Snapshot 标志）。
  - 构造函数里三条 `DEFINE_ATTRIBUTE_CAPTUREDEF(...)`：
    - `DEFINE_ATTRIBUTE_CAPTUREDEF(URPG_AttributeSet, Attack, Source, true);` —— 从**来源**（攻击者）捕获，`Snapshot = true`（出手瞬间锁定）。
    - `DEFINE_ATTRIBUTE_CAPTUREDEF(URPG_AttributeSet, Defense, Target, true);` —— 从**目标**（受击者）捕获，`Snapshot = true`。注释：让"命中瞬间的防御"决定减伤 —— 否则一个飞行道具在途中目标吃了减防 Debuff，伤害会莫名变高。
    - `DEFINE_ATTRIBUTE_CAPTUREDEF(URPG_AttributeSet, IncomingDamage, Target, false);` —— 元属性，是**输出**目标，`Snapshot` 无意义（我们不读它，只写它）。
- **为什么这么写**：注释说明 `DECLARE_ATTRIBUTE_CAPTUREDEF` 生成两个成员、`DEFINE_ATTRIBUTE_CAPTUREDEF` 生成一个 `FGameplayEffectAttributeCaptureDefinition` 用于捕获属性；用一个静态结构体集中管理，避免每个 Execution 实例都重建一遍。

#### `static const FRPGDamageStatics& DamageStatics()`（.cpp 内文件作用域自由函数）

- **干什么**：返回唯一的静态捕获定义集合。
- **关键实现**：函数内 `static FRPGDamageStatics Statics; return Statics;`
- **为什么这么写**：函数内静态变量保证只构造一次，且线程安全（C++11 起）。
- **被谁调用 / 调用谁**：`URPG_DamageExecution` 的构造函数与 `Execute_Implementation`。

#### `URPG_DamageExecution::URPG_DamageExecution()`

- **干什么**：声明本 Execution 需要哪些属性。
- **关键实现**：`RelevantAttributesToCapture.Add(DamageStatics().AttackDef); .Add(DamageStatics().DefenseDef); .Add(DamageStatics().IncomingDamageDef);`
- **为什么这么写**：注释明确 —— 没在这里注册的属性，`AttemptCalculateCapturedAttributeMagnitude` 会拿不到值。

#### `void URPG_DamageExecution::Execute_Implementation(const FGameplayEffectCustomExecutionParameters& ExecutionParams, FGameplayEffectCustomExecutionOutput& OutExecutionOutput) const`（override）

- **干什么**：完成整条伤害公式计算，并把结果作为 `IncomingDamage` 的 Additive 输出。
- **关键实现**（严格按代码顺序）：
  1. `const FGameplayEffectSpec& Spec = ExecutionParams.GetOwningSpec();`
  2. **聚合标签**：`FAggregatorEvaluateParameters EvalParams; EvalParams.SourceTags = Spec.CapturedSourceTags.GetAggregatedTags(); EvalParams.TargetTags = Spec.CapturedTargetTags.GetAggregatedTags();` —— 注释：属性求值时需要考虑双方的标签（比如"对不死族伤害翻倍"这类规则）。目前只有这一处使用（传给了两次 `AttemptCalculateCapturedAttributeMagnitude`）。
  3. 读 `Attack`：`float Attack = 0.f; ExecutionParams.AttemptCalculateCapturedAttributeMagnitude(DamageStatics().AttackDef, EvalParams, Attack); Attack = FMath::Max(Attack, 0.f);`
  4. 读 `Defense`：同样的写法，`Defense = FMath::Max(Defense, 0.f);`
  5. **取倍率**：`const float Multiplier = Spec.GetSetByCallerMagnitude(RPGTags::Data_Damage_Multiplier, /*WarnIfNotFound*/ false, /*DefaultIfNotFound*/ 1.f);` —— 注释：第三个参数是"找不到时的默认值"，传 1.0 而不是 0，这样即使忘了传倍率也只是伤害偏低，而不是完全没伤害（后者更难排查）。
  6. `const float BaseDamage = Attack * FMath::Max(Multiplier, 0.f);`
  7. **防御减伤曲线**：`const float Mitigation = Defense / (Defense + URPG_AttributeSet::DefenseConstant);`
  8. `float FinalDamage = BaseDamage * (1.f - Mitigation);`
  9. **保底**：`FinalDamage = FMath::Max(FinalDamage, URPG_AttributeSet::MinDamage);`
  10. **写元属性**：`OutExecutionOutput.AddOutputModifier(FGameplayModifierEvaluatedData(DamageStatics().IncomingDamageProperty, EGameplayModOp::Additive, FinalDamage));`
  11. Verbose 日志（`LogRPG_Combat`）：`"伤害计算：攻击 %.1f × 倍率 %.2f = %.1f | 防御 %.1f → 减伤 %.1f%% | 最终 %.1f"`（含 `Mitigation * 100.f`）。
- **为什么这么写**：
  - **减伤曲线的选型理由**（注释原文）：用 `Defense / (Defense + K)` 而不是减法或百分比 —— 减法（`Damage - Defense`）防御堆到攻击力以上就完全免伤，数值失控；百分比（`Damage × (1 - Def%)`）堆到 100% 就无敌，同样失控；本公式是收益递减曲线，永远到不了 100%，且 K 有直观含义：**K 就是"减伤 50% 所需的防御值"**。
  - **保底伤害**：避免高防目标把伤害压到 0 导致"打不动"。
  - **写 `IncomingDamage` 而不是直接改 `Health`**：扣血、无敌判定、死亡广播都由 `AttributeSet::PostGameplayEffectExecute` 统一处理（分工见头文件）。
  - `Attack` / `Defense` / `Multiplier` 都做了非负钳制（`FMath::Max(x, 0.f)`），避免负属性把伤害算成治疗或负数。
- **被谁调用 / 调用谁**：GAS 的 Execution 求值管线（由挂了本 Execution 的 `GE_Damage` 资产触发）；自身调用 `DamageStatics()`、`AttemptCalculateCapturedAttributeMagnitude`、`GetSetByCallerMagnitude`、`OutExecutionOutput.AddOutputModifier`。

---

### `Source/RPG/AbilitySystem/Tasks/RPG_AbilityTask_WeaponTrace.h`（实现：`Source/RPG/AbilitySystem/Tasks/RPG_AbilityTask_WeaponTrace.cpp`）

**一句话职责**：逐帧采样武器/双手的骨骼 Socket 位置，在"上一帧线段"与"当前帧线段"之间做连续 Sweep（防隧穿），对本次挥砍内首次命中的目标广播 `OnHit`。

**类/结构体**：`URPG_AbilityTask_WeaponTrace` —— 继承 `UAbilityTask`；`UCLASS()`。

**委托**：`DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FRPGWeaponTraceHitDelegate, const TArray<FHitResult>&, Hits);` —— 注释：一次广播可能包含多个目标（一刀扫到两个人）。

**头文件里的三段论证（逐条转述）**

- **为什么必须用 Sweep 而不是 Overlap**：高速挥砍时，刀锋在**两帧之间**会整个"穿过"敌人 —— 第 N 帧刀在敌人左边、第 N+1 帧刀在敌人右边，Overlap 检测两个离散位置都检测不到，漏判。这就是"隧穿效应"，必须在两帧位置之间做**连续扫掠**（Sweep）才能抓到。**30fps 下这个问题尤其明显**：一帧内刀尖可以移动 40cm 以上，而敌人胶囊半径可能还不到 40cm。
- **去重：同一次挥砍只打一次**：检测窗口通常持续 3~5 帧，如果不去重，同一刀会命中同一目标 3~5 次 —— 伤害翻好几倍，受击特效也会叠着播。用 `TSet` 记录本次挥砍已命中的目标，Task 销毁时清空（下次挥砍自然重新开始）。
- **联机说明**：这个 Task 会在客户端和服务器**各跑一份**（因为 GA 是 `LocalPredicted`）。客户端的检测结果只用于即时反馈（顿帧、音效）；服务器那一份才是权威 —— 真实伤害从服务器产生并复制给所有人。所以这里不需要做"只在服务器跑"的限制，两份各司其职即可。

#### `URPG_AbilityTask_WeaponTrace::URPG_AbilityTask_WeaponTrace()`

- **干什么**：开启逐帧 Tick。
- **关键实现**：`bTickingTask = true;`
- **为什么这么写**：轨迹检测的本质就是"每帧采样连成线"，没有 Tick 就没法形成连续轨迹。注释特别提示：`bTickingTask` 定义在 `UGameplayTask` 基类，**不是** `UAbilityTask`。

#### `static URPG_AbilityTask_WeaponTrace* URPG_AbilityTask_WeaponTrace::CreateWeaponTraceTask(UGameplayAbility* OwningAbility, ERPG_TraceSource TraceSource, float TraceRadius, FName SocketStart, FName SocketEnd)`

- **反射标记**：`UFUNCTION(BlueprintCallable, Category = "Ability|Tasks", meta = (HidePin = "OwningAbility", DefaultToSelf = "OwningAbility", BlueprintInternalUseOnly = "TRUE"))`
- **干什么**：工厂函数，创建并配置 Task（**不负责激活**，调用方还要 `ReadyForActivation`）。
- **关键实现**：`NewAbilityTask<URPG_AbilityTask_WeaponTrace>(OwningAbility)` 建实例，然后逐个赋值 `TraceSource`、`TraceRadius = FMath::Max(TraceRadius, 1.f);`（下限 1cm，防 0 半径退化）、`SocketStart`、`SocketEnd`，返回 Task。
- **参数说明**（头文件）：`TraceSource` 检测源类型（双手 / 刀锋）；`TraceRadius` 检测半径（厘米）；`SocketStart` / `SocketEnd` 线段起点终点 Socket 名。
- **被谁调用 / 调用谁**：`RPG_GA_LightAttack.cpp:647`（随后 `TraceTask->OnHit.AddDynamic(this, &URPG_GA_LightAttack::OnWeaponTraceHit);`）、`RPG_GA_HeavyAttack.cpp:660`；自身调用 `NewAbilityTask`。

#### `void URPG_AbilityTask_WeaponTrace::Activate()`（override，protected）

- **干什么**：重置本次挥砍的检测状态。
- **关键实现**：`Super::Activate();` 后 `bHasPreviousSample = false; HitActorsThisSwing.Reset();`
- **为什么这么写**：首帧只记录基准位置、不做检测 —— 因为"上一帧在哪"还不存在，没有可扫掠的区间。

#### `void URPG_AbilityTask_WeaponTrace::TickTask(float DeltaTime)`（override，protected）

- **干什么**：每帧驱动检测。
- **关键实现**：`Super::TickTask(DeltaTime);` 后调用 `SampleAndSweep();`。**`DeltaTime` 在函数体里没有被使用**（采样用的是 Socket 的实际位置差，不是速度 × 时间）。

#### `void URPG_AbilityTask_WeaponTrace::OnDestroy(bool bInOwnerFinished)`（override，protected）

- **干什么**：销毁时清空去重表。
- **关键实现**：先 `HitActorsThisSwing.Reset();`，再 `Super::OnDestroy(bInOwnerFinished);`（**注意顺序：先清理自己的状态再调基类**）。
- **为什么这么写**：注释 —— 虽然 Task 每次都是新实例，但显式清理能避免万一被复用时的隐蔽 bug，也让意图更清楚。

#### `bool URPG_AbilityTask_WeaponTrace::GetCurrentSample(FVector& OutStart, FVector& OutEnd) const`（private）

- **干什么**：采样当前位置得到检测线段；拿不到骨骼/网格时返回 false。
- **关键实现**（四道早退）：
  1. `const AActor* Avatar = GetAvatarActor();` 为空 → false。
  2. `const USkeletalMeshComponent* MeshComp = Avatar->FindComponentByClass<USkeletalMeshComponent>();` 为空 → false。
  3. `SocketStart.IsNone() || SocketEnd.IsNone()` → false。
  4. `!MeshComp->DoesSocketExist(SocketStart) || !MeshComp->DoesSocketExist(SocketEnd)` → false（注释：缺失时静默返回 false 而不是每帧报错刷屏，但第一次出现时会在 `SampleAndSweep` 里打一条 Warning）。
  5. 成功：`OutStart = MeshComp->GetSocketLocation(SocketStart); OutEnd = MeshComp->GetSocketLocation(SocketEnd);` 返回 true。
- **为什么这么写**（注释说明的检测源约定）：目前两种检测源都从角色的骨骼网格体取 Socket —— 徒手 → `hand_l` / `hand_r`；近战武器 → 武器 Mesh 上的 Socket（武器挂在角色骨骼上时，这些 Socket 名在角色网格体上同样能取到）。**将来如果武器是独立的 Actor，这里改成先找武器组件再取其 Socket。**
- **被谁调用 / 调用谁**：`SampleAndSweep`；自身调用 `GetAvatarActor`、`FindComponentByClass`、`DoesSocketExist`、`GetSocketLocation`。

#### `void URPG_AbilityTask_WeaponTrace::SampleAndSweep()`（private）

- **干什么**：每帧的核心 —— 采样 → 扫掠 → 去重 → 广播（头文件对该函数的注释即此）。
- **关键实现**（严格按代码顺序）：
  1. `FVector CurrentStart, CurrentEnd; if (!GetCurrentSample(CurrentStart, CurrentEnd))` → **函数内静态 `static bool bWarnedOnce = false;`** 保证整进程只打一次 Warning：`"轨迹检测拿不到采样点：请检查 Socket 名「%s」/「%s」是否存在于角色的骨骼网格体上"`，然后 return。
     - **注意**：这是**函数内静态变量**，进程内所有 Task 实例共享它 —— 一旦任何一个实例打过一次，后续实例都不会再提示。
  2. **首帧只建立基准**：`if (!bHasPreviousSample)` → `PreviousStart = CurrentStart; PreviousEnd = CurrentEnd; bHasPreviousSample = true;` → return。
  3. `const AActor* Avatar = GetAvatarActor(); UWorld* World = Avatar ? Avatar->GetWorld() : nullptr;` `!World` → return。
  4. **把检测线段包成胶囊**：`CurrentCenter = (CurrentStart + CurrentEnd) * 0.5f;` `PreviousCenter = (PreviousStart + PreviousEnd) * 0.5f;` `Segment = CurrentEnd - CurrentStart;` `HalfHeight = FMath::Max(Segment.Size() * 0.5f, 1.f);` `Shape = FCollisionShape::MakeCapsule(TraceRadius, HalfHeight);`
     - 注释：胶囊的"高度方向"对齐线段方向，这样武器横着挥时检测体也横着，而不是永远竖着 —— 后者会导致横扫时判定严重失真。
  5. `const FQuat Orientation = Segment.IsNearlyZero() ? FQuat::Identity : FRotationMatrix::MakeFromZ(Segment).ToQuat();`
  6. `FCollisionQueryParams Params(TEXT("RPGWeaponTrace"), /*bTraceComplex*/ false, Avatar);` —— 注释：排除自己，否则刀会砍到自己身上。
  7. **统一走 Sweep 路径**：`MoveDelta = CurrentCenter - PreviousCenter;` `SafeDelta = MoveDelta.IsNearlyZero() ? FVector(0.f, 0.f, 0.01f) : MoveDelta;` 然后 `World->SweepMultiByChannel(Hits, PreviousCenter, PreviousCenter + SafeDelta, Orientation, ECC_Pawn, Shape, Params);`
     - 注释：这一帧如果手没动（比如前摇的停顿），给一个极小的位移让 Sweep 退化成重叠检测 —— 这样代码只有一条路径，不必维护两套逻辑，也避免了 Overlap 和 Sweep 返回类型不同带来的麻烦。
     - ★ 关键：从**上一帧位置**扫掠到**当前帧位置**，这一步就是防隧穿的全部意义所在。
  8. **更新基准位置**：`PreviousStart = CurrentStart; PreviousEnd = CurrentEnd;`（注意这发生在"命中去重/广播"之前，也发生在下面的空结果早退之前，所以任何分支下基准都会推进）。
  9. `if (Hits.IsEmpty()) return;`
  10. **去重**：`TArray<FHitResult> NewHits; NewHits.Reserve(Hits.Num());` 遍历 `Hits` —— `Hit.GetActor()` 为空跳过；`HitActorsThisSwing.Contains(HitActor)` 跳过；否则 `HitActorsThisSwing.Add(HitActor); NewHits.Add(Hit);`
  11. `if (NewHits.Num() > 0)` → Verbose 日志 `"轨迹检测命中 %d 个新目标"` → `OnHit.Broadcast(NewHits);`
- **为什么这么写**：见上面头文件三段论证（防隧穿、去重、联机各跑一份）。去重注释还补了一句：`ThisSwing` 在 Task 创建时清空，所以下一刀能重新命中同一个目标。

**成员变量（逐个列，全部 private，均无 UPROPERTY 复制标记）**

| 成员 | 类型 / 初值 | 说明 |
| --- | --- | --- |
| `OnHit` | `UPROPERTY(BlueprintAssignable) FRPGWeaponTraceHitDelegate`（**public**） | 检测到新目标时广播，GA 在这里绑定伤害逻辑 |
| `TraceSource` | `ERPG_TraceSource = ERPG_TraceSource::Hands` | 本 Task 的配置：检测源类型 |
| `TraceRadius` | `float = 30.f` | 检测半径（厘米） |
| `SocketStart` | `FName` | 线段起点 Socket |
| `SocketEnd` | `FName` | 线段终点 Socket |
| `bHasPreviousSample` | `bool = false` | 上一帧的采样线段是否存在。第一帧没有基准，只记录不检测 |
| `PreviousStart` | `FVector = FVector::ZeroVector` | 上一帧线段起点 |
| `PreviousEnd` | `FVector = FVector::ZeroVector` | 上一帧线段终点 |
| `HitActorsThisSwing` | `TSet<TWeakObjectPtr<AActor>>` | 本次挥砍已命中的目标（去重）。Task 销毁时清空。用**弱指针**，不会阻止目标 Actor 被 GC |

**用到的枚举（定义在 `Source/RPG/Combat/RPG_CombatTypes.h`，此处按其注释逐个列出）**：`ERPG_TraceSource : uint8`，`UENUM(BlueprintType)`，注释为"伤害检测源 —— 决定 WeaponTrace 采样什么位置。前两种走'连续 Sweep'（防止高速挥砍时穿透目标），第三种交给发射物自己处理。"

- `Hands`（DisplayName "双手骨骼"）：采样 `hand_l` / `hand_r` 的位置，两帧之间连成线段做 Sweep。
- `WeaponBlade`（DisplayName "武器刀锋"）：采样武器 Mesh 上两个 Socket 之间的线段。
- `Projectile`（DisplayName "发射物"）：生成 Projectile，由它自己的碰撞回调施加伤害。

---

# 四、具体能力（GA）

本章覆盖 `Source/RPG/AbilitySystem/Abilities/` 下除 `RPG_GameplayAbilityBase`（见第三章）之外的全部能力：

| 类 | 文件 | 形态 |
| --- | --- | --- |
| `URPG_GA_LightAttack` | `RPG_GA_LightAttack.h/.cpp` | 5 段轻击连段 |
| `URPG_GA_HeavyAttack` | `RPG_GA_HeavyAttack.h/.cpp` | 3 段蓄力重击 + 切手技（一个 GA 两种形态） |
| `URPG_GA_Dodge` | `RPG_GA_Dodge.h/.cpp` | 翻滚：冲量 + 无敌帧 + 耐力代价 |
| `URPG_GA_HitReact` | `RPG_GA_HitReact.h/.cpp` | 受击踉跄（事件触发） |
| `URPG_GA_Death` | `RPG_GA_Death.h/.cpp` | 死亡编排（事件触发） |
| `URPG_GA_Jump` | `RPG_GA_Jump.h/.cpp` | 跳跃（可变高度） |
| `URPG_GA_Sprint` | `RPG_GA_Sprint.h/.cpp` | 奔跑（按住，持续耗耐力） |
| `URPG_GA_Heal` | `RPG_GA_Heal.h/.cpp` | 治疗 |
| `URPG_GA_ApplyBuff` | `RPG_GA_ApplyBuff.h/.cpp` | 通用增益/减益容器 |
| `URPG_GA_StaminaRegen` | `RPG_GA_StaminaRegen.h/.cpp` | 耐力恢复（被动常驻） |

---

## 阅读前提：所有子类共享的默认值

以下默认值**不是**各子类构造函数设的，而是 `URPG_GameplayAbilityBase`（第三章）设的。读到"某能力没有设置 `InstancingPolicy`"时，含义是**继承了这个值**，而不是"没设置"。

| 配置项 | 基类默认值 | 基类给出的理由 |
| --- | --- | --- |
| `InstancingPolicy` | `InstancedPerActor` | UE 5.8 的 `UGameplayAbility` 构造函数默认是 `InstancedPerExecution`（`GameplayAbility.cpp:102`），对连段是灾难：状态不保留、多个实例并行跑（基类 .cpp 注释原文） |
| `NetExecutionPolicy` | `LocalPredicted` | 动作游戏手感前提：客户端按键立刻本地执行，不等一个 RTT |
| `NetSecurityPolicy` | `ClientOrServer` | 允许客户端主动请求激活 |
| `ActivationBlockedTags` | `State.Dead` | 死亡后不能用任何技能，一条声明覆盖全部能力 |

基类还提供四个**子类普遍依赖**的受保护成员函数（实现细节见第三章）：

- `UAbilityTask_PlayMontageAndWait* PlayMontageOrSkip(UAnimMontage* Montage, FName TaskName, float Rate = 1.f)` —— 蒙太奇为空时返回 `nullptr` 并打 Verbose 日志（不是错误）；非空时顺带做一次"混合时间体检"（BlendIn + BlendOut > 全长 × 0.5 就打 Warning，因为新建蒙太奇默认各 0.25 秒混合）。**返回的 Task 由调用方自己绑委托并 `ReadyForActivation()`**。
- `void ApplyDamageToTarget(AActor* Target, float DamageMultiplier, const FHitResult* HitResult = nullptr)` —— 用 `SetByCaller(Data.Damage.Multiplier)` 传倍率，一个 `GE_Damage` 服务所有攻击段。
- `void ConsumeStamina(float Amount)` —— 用 `-FMath::Abs(Amount)` 传 `Data.Stamina.Cost`（负数），并**在每次消耗后重新挂一次 `StaminaRegenDelayEffectClass`** 来刷新"恢复阻断"。
- `void PerformSimulatedMeleeHit(float DamageMultiplier, float ForwardOffset = 150.f, float Radius = 80.f)` —— 开发期用的球形 overlap 命中。
- `bool HasEnoughStamina(float Amount) const` —— 拿不到属性集时**放行**。

便捷查询（均走 `CurrentActorInfo`，因此**只能在 ActivateAbility 及之后调用**）：`GetRPGCharacter()` / `GetCombatComponent()` / `GetRPGAttributeSet()` / `GetAttackModule()`。

**输入链路**（各 GA 的输入来源）：`RPG_PlayerController` 按键 → `URPG_CombatComponent::PushInputTag(InputTag)`（联机下额外一次 `Server_PushInputTag`）→ `URPG_AbilitySystemComponent::TryActivateAbilityByInputTag(InputTag)` → GA 激活。松手走 `RPG_PlayerController` → `URPG_AbilitySystemComponent::NotifyInputReleased(InputTag)` → 该输入对应能力实例的 `URPG_GameplayAbilityBase::OnInputReleased()`（实现见 `RPG_AbilitySystemComponent.cpp:538`，通过 `Spec->GetPrimaryInstance()` 取实例，取不到就静默返回）。

**"是否攻击中"的真相源是 GameplayTag `State.Attacking`**，不是任何 bool 成员；`Ability.*` 是能力身份（给 GAS 的取消/查询用），`State.*` 是角色状态（给动画/AI/UI 查），两者分开是刻意约定。

---

### `RPG_GA_LightAttack`

**一句话职责**：用一个 GA 承载 5 段轻击连段 —— 内部循环播放 5 段蒙太奇，靠"输入缓存 + 衔接窗口事件"推进段位，靠 AnimNotify 广播的 GameplayEvent 驱动判定窗口、衔接窗口与收尾。

**关键配置**（构造函数里设的，逐条）：

- `SetAssetTags(FGameplayTagContainer(RPGTags::Ability_Attack_Light))` —— 用资产标签标记"这是轻击类能力"。理由（注释原文）：其他系统可据此查询（"正在攻击时禁止移动"这类规则），也方便在 GameplayDebugger 里辨认。**这个 AssetTags 还是 `GA_HeavyAttack` 显式调用 `ASC->CancelAbilities()` 时的匹配目标**（见 HeavyAttack 一节）。
- `ActivationOwnedTags.AddTag(RPGTags::Ability_Attack_Light)` —— 激活期间挂上同一个标签。理由（注释原文）：`GA_HeavyAttack` 靠查询这个标签判断"当前是否处于轻击连段中"，以此决定走切手技分支还是蓄力分支；它同时也是 `CancelAbilitiesWithTag` 的目标（重击激活时会自动取消轻击）。为什么用标签而不是 `CombatComponent` 的连段索引：**索引在起手那一瞬间是 0（表示"正在打第 1 段"），无法区分"不在连段中"和"正在打第 1 段"**。
- `ActivationOwnedTags.AddTag(RPGTags::State_Attacking)` —— 动画蓝图读取的状态标签。理由（注释原文）：`Ability.Attack.Light` 是"能力身份"，给 GAS 内部用；动画要问的是另一个问题"这个角色此刻在不在攻击"，那是 State 域的语义。两者生命周期**碰巧**一样，但语义不同，混用会在将来出问题（比如以后加一个"攻击时也能激活"的 Buff 能力，它也带 `Ability.*` 标签，动画就会误判成在攻击）。`State.Attacking` 是 `State.Attack.Windup / Active / Recovery` 的父标签，所以 `HasMatchingGameplayTag(State.Attacking)` 对这些子标签同样成立，将来挂分阶段标签时这里一行都不用改。
- `CancelAbilitiesWithTag.AddTag(RPGTags::Ability_Attack_Heavy)` —— 激活轻击时取消重击（包括蓄力中）。理由（注释原文）：`GA_HeavyAttack` 只声明了"激活时取消轻击"（那是切手技的机制），反过来没有 —— 结果就是蓄力期间按左键，两个 GA 会**并行跑**（各自播各自的蒙太奇、各扣各的耐力）。补上这个方向让"轻击打断蓄力"也成立，这是动作游戏里的常见规则：蓄力不是不可打断的霸体状态。
- **未覆盖**：`InstancingPolicy`（= `InstancedPerActor`）、`NetExecutionPolicy`（= `LocalPredicted`）、`NetSecurityPolicy`（= `ClientOrServer`）、`ActivationBlockedTags`（= `State.Dead`），全部继承基类。
- 没有 `AbilityTriggers` —— 本能力由输入标签激活，不由事件触发。

**成员变量**（全部）：

| 声明 | 含义与初值 |
| --- | --- |
| `UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="RPG|Debug", meta=(ClampMin="0.1")) float SimulatedSegmentDuration = 0.8f` | 没有蒙太奇时每段模拟持续多久（秒）。注释：只在开发期用于验证数值链路，动画接上后不再使用 |
| `UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="RPG|Debug", meta=(ClampMin="0.05")) float SimulatedComboWindowDuration = 0.5f` | 模拟模式下衔接窗口持续多久 |
| `int32 CurrentSegmentIndex = 0`（private，非 UPROPERTY） | 当前正在播放的段索引（0-based） |
| `float CurrentDamageMultiplier = 1.f` | 当前段的伤害倍率，进入段时从攻击模组缓存 |
| `bool bComboWindowOpen = false` | 衔接窗口是否开启中。**注意：本类中该变量只被写入、从未被读取**（全文件 grep 确认无读取点），真正的推进判断靠"缓存里有没有输入"，所以它目前是纯记录性状态 |
| `float PreviousSegmentStartTime = 0.f` | 上一段蒙太奇开始时的世界时间（段时长诊断用） |
| `float PreviousSegmentMontageLength = 0.f` | 上一段蒙太奇全长（秒）。0 表示上一段没有蒙太奇 |
| `UPROPERTY() TObjectPtr<URPG_AbilityTask_WeaponTrace> TraceTask` | 本次挥砍的轨迹检测任务 |
| `FTimerHandle SimulatedSegmentTimer` | 模拟时序用的定时器。**声明了但 .cpp 中没有任何引用**（全库 grep 只命中头文件这一行），实际使用的是下面那个 |
| `FTimerHandle SimulatedComboTimer` | 模拟时序的衔接窗口定时器 |
| `UPROPERTY() TObjectPtr<UAbilityTask_PlayMontageAndWait> CurrentSegmentMontageTask` | 当前这一段的蒙太奇任务 |
| `UPROPERTY() TObjectPtr<UAnimMontage> CurrentSegmentMontage` | 当前这一段的蒙太奇。切段时要停掉它，防止它的 Notify 迟到触发 |

头文件对 `CurrentSegmentMontageTask` 的说明（原文要点）：连段每接一段，都会留下上一段的蒙太奇和它的 `PlayMontageAndWait` 任务继续活着。不清掉的话，上一段动画播完时会广播 `OnCompleted`，把**正在播下一段**的这次能力判定为"整套打完了"。症状：连招永远打不全（每次接下一段都会给上一段埋一颗炸在它自己动画结束时刻的雷），而且日志里看不出异常。

#### `URPG_GA_LightAttack::URPG_GA_LightAttack()`

- **干什么**：设置资产标签、激活期标签与取消关系（见上面的"关键配置"）。
- **关键实现**：四行配置：`SetAssetTags`、两次 `ActivationOwnedTags.AddTag`、一次 `CancelAbilitiesWithTag.AddTag`。
- **为什么这么写**：见关键配置里逐条转述的注释。
- **被谁调用 / 调用谁**：UE 反射构造；不调用其它函数。

#### `void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)`

- **干什么**：决定本次从第几段起手，建立一次覆盖整轮连段的事件监听，然后播第一段。
- **关键实现**（按执行顺序）：
  1. `Super::ActivateAbility(...)` —— **先调父类**（注意 HeavyAttack 是反过来的，见那一节）。
  2. `CommitAbility(...)` 失败 → `UE_LOG(LogRPG_Ability, Verbose, "[%s] 轻击提交失败（冷却中或被标签阻断）")` → `FinishCombo(true)` → return。
  3. 取 `Combat = GetCombatComponent()`、`Module = GetAttackModule()`。三者任一不成立（含 `Module->GetLightSegmentCount() == 0`）→ 打 **Warning**，并且把三种失败原因分开报：`"角色上没有 CombatComponent"` / `"CombatComponent 没有配置攻击模组 DataAsset"` / `"攻击模组的轻击段列表为空"` → `FinishCombo(true)` → return。
  4. 消费触发本次激活的输入：`FGameplayTag TriggerInputTag; Combat->ConsumeInputTag(TriggerInputTag);`。理由（注释原文）：PC 在按键时会把输入推进缓存（为了让连段衔接能取到），但既然这次按键已经成功触发了激活，这条输入就已经被消费掉了；不消耗的话它会残留在缓存里，导致第一次衔接窗口一开就自动多打一段。⚠️ 注释说"用 if 而不是直接调用是为了明确表达'没有缓存条目也是正常的'"，但**实际代码是直接调用、忽略返回值**（注释与代码不一致，如实记录）。
  5. 决定起手段：`int32 StartIndex = Combat->GetComboIndex();`。`CombatComponent` 的索引语义是"下一段该打的索引"（0 = 还没打过任何一段 → 打索引 0；2 = 已经打完第 2 段 → 打索引 2）。若 `!Module->GetLightSegment(StartIndex)`（越界，说明上一轮打到底了，比如第 5 段结束时衔接窗口又收到输入）→ 打 `LogRPG_Combat` Verbose "连段索引 %d 越界，重新起手" → `Combat->ResetCombo()`、`StartIndex = 0`。
  6. 复位本次激活的私有状态：`CurrentSegmentIndex = StartIndex`、`CurrentDamageMultiplier = 1.f`、`bComboWindowOpen = false`，然后 `Combat->SetComboIndex(CurrentSegmentIndex)`。
  7. `BindGameplayEventListeners()`（每次激活只需一次，覆盖整轮连段的所有段）。
  8. `StartSegment(CurrentSegmentIndex)`。
- **为什么这么写**：三处注释理由已在上面逐条转述（分开报失败原因、消费触发输入、越界重新起手）。
- **被谁调用 / 调用谁**：由 `URPG_AbilitySystemComponent::TryActivateAbilityByInputTag()`（玩家 PC / `RPG_BTTask_Attack` 的 AI 路径）经 GAS 激活流程调用。内部调用 `CommitAbility`、`GetCombatComponent`、`GetAttackModule`、`Combat->ConsumeInputTag`、`Combat->GetComboIndex`、`Combat->ResetCombo`、`Combat->SetComboIndex`、`BindGameplayEventListeners`、`StartSegment`、`FinishCombo`。

#### `void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)`

- **干什么**：能力结束时的统一清理：重置连段索引、停轨迹任务、拆蒙太奇、清定时器。
- **关键实现**：
  1. `if (URPG_CombatComponent* Combat = GetCombatComponent()) Combat->ResetCombo();` —— 注释：`FinishCombo()` 里虽然已经重置过，但**被外部取消**这条路径不走 `FinishCombo`（比如切手技通过 `CancelAbilitiesWithTag` 取消轻击时 GAS 直接调 `EndAbility`）。少了这一句连段索引会残留，下次按左键会从中间某段开始（症状是"打着打着突然从第 3 段起手"）。`ResetCombo` 内部有"值没变就跳过"的判断，重复调用安全。
  2. `TraceTask` 有效 → `EndTask()`、置空。
  3. `StopCurrentSegmentMontage();` —— 注释：走 `FinishCombo` 进来时上面已摘过回调并清空指针，这里是空操作；**被外部取消**那条路径才有实际作用（不清理的话轻击蒙太奇会继续播，它的判定窗口 Notify 会在重击的动画上再打一次伤害）。
  4. `World->GetTimerManager().ClearTimer(SimulatedComboTimer);`
  5. `Super::EndAbility(...)`。
- **为什么这么写**：见上（注释给出了每一条的理由）。
- **被谁调用 / 调用谁**：GAS 内部（能力自然结束/被取消），以及本类的 `FinishCombo()`。调用 `GetCombatComponent`、`ResetCombo`、`TraceTask->EndTask`、`StopCurrentSegmentMontage`、`Super::EndAbility`。

#### `void BindGameplayEventListeners()`

- **干什么**：为 5 个 GameplayEvent 各建一个 `UAbilityTask_WaitGameplayEvent` 监听并立刻激活。
- **关键实现**：5 个独立的花括号作用域，每个里 `Task = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(this, Tag)` → `Task->EventReceived.AddDynamic(this, &URPG_GA_LightAttack::OnXxx)` → `Task->ReadyForActivation()`。对应的 Tag 与回调：
  - `Event.Combat.AttackWindow.Open` → `OnAttackWindowOpen`
  - `Event.Combat.AttackWindow.Close` → `OnAttackWindowClose`
  - `Event.Combat.ComboWindow.Open` → `OnComboWindowOpen`
  - `Event.Combat.ComboWindow.Close` → `OnComboWindowClose`
  - `Event.Combat.AttackEnd` → `OnAttackEndEvent`
- **为什么这么写**：注释原文 —— 不能写成循环 + 变量传函数名：`AddDynamic` 宏内部会把函数名字符串化（`#FuncName`）用于反射查找，所以必须是**字面量**函数名；写成 5 条重复语句虽然啰嗦，但这是宏的硬性要求。`OnlyTriggerOnce` 保持默认 `false` —— 一轮连段里每个事件都会触发多次（5 段就有 5 组窗口开/关）。
- **被谁调用 / 调用谁**：被 `ActivateAbility` 调用一次；调用 `UAbilityTask_WaitGameplayEvent::WaitGameplayEvent`。

#### `void StartSegment(int32 Index)`

- **干什么**：开始播放第 `Index` 段（0-based）：读配置 → 打诊断日志 → 扣耐力 → 清理上一段 → 播蒙太奇（或走模拟时序）。
- **关键实现**：
  1. `Module` 为空 → `FinishCombo(true)`；`Segment = Module->GetLightSegment(Index)` 为空 → `FinishCombo(false)`（注释：没有这一段，比如只有 3 段的武器打到了第 4 段）。
  2. `CurrentDamageMultiplier = Segment->DamageMultiplier; bComboWindowOpen = false;`
  3. **段时长诊断**：取 `World->GetTimeSeconds()`；仅当 `Index > 0 && PreviousSegmentMontageLength > 0` 时，算 `Played = Now - PreviousSegmentStartTime`、`bWasCut = Played < PreviousSegmentMontageLength * 0.9f`，打 `LogRPG_Combat` Log：`"[%s] ↳ 上一段（第 %d 段）实际播放 %.2f 秒 / 全长 %.2f 秒 —— %s"`，末句二选一 `"被下一段接走（衔接窗口开太早）"` / `"自然播完"`。
  4. 记录本段：`MontageLength = Segment->Montage ? Montage->GetPlayLength() : 0.f`；`PreviousSegmentStartTime = Now`；`PreviousSegmentMontageLength = MontageLength`。
  5. 打 `LogRPG_Combat` Log：`"轻击第 %d 段（倍率 %.2f，耐力 %.1f，动画 %s %.2f 秒）"`（段号打 `Index + 1`；动画名空时显示 `【未配置】`）。
  6. `ConsumeStamina(Segment->StaminaCost);` —— 注释：耐力消耗跟着"段的开始"走，而不是跟着伤害窗口，这样即使玩家挥到一半被打断，耐力也已经扣了，避免"打断了就能白嫖一次攻击"的漏洞。
  7. `StopCurrentSegmentMontage();` —— 注释：少了这一句连段永远打不全；放在播新蒙太奇之前，是因为清理动作本身会停掉"当前蒙太奇"，必须赶在新蒙太奇成为"当前"之前做。
  8. `PlayMontageOrSkip(Segment->Montage, FName(*FString::Printf(TEXT("LightAttack_%d"), Index)))`（任务名形如 `LightAttack_0` … `LightAttack_4`）。返回非空 → 绑 `OnCompleted`→`OnMontageCompleted`、`OnInterrupted`→`OnMontageInterrupted`、`ReadyForActivation()`，并记 `CurrentSegmentMontageTask = MontageTask; CurrentSegmentMontage = Segment->Montage;`。返回空 → `StartSimulatedSegment()`。
  9. 注释补充：蒙太奇的混合时间体检在 `PlayMontageOrSkip()` 里做 —— 那是所有蒙太奇的必经之路，检查放一处就够了。
- **为什么这么写**：`Index > 0` 的限制理由（注释原文）：一次全新的连段总是从 `Index = 0` 开始，那时上一段是**上一轮连段**的事，中间间隔可能有好几秒，把它当成"这一段播放了 3.6 秒"报出来会误导人（曾经真的误导过一次排查）。`Index > 0` 还隐含保证了"上一段就是 Index-1"：连段只能一段一段往上接。
- **被谁调用 / 调用谁**：被 `ActivateAbility` 与 `TryStartNextSegment` 调用。内部调用 `GetAttackModule`、`Module->GetLightSegment`、`GetWorld`、`ConsumeStamina`、`StopCurrentSegmentMontage`、`PlayMontageOrSkip`、`StartSimulatedSegment`、`FinishCombo`。

#### `bool TryStartNextSegment()`

- **干什么**：从输入缓存取一个输入，如果是轻击意图且还有下一段，就续段。
- **关键实现**：
  1. `Combat` / `Module` 任一为空 → `false`。
  2. `if (!Combat->ConsumeInputTag(BufferedTag)) return false;`（缓存为空 → 没得续）。
  3. `if (BufferedTag != RPGTags::Input_Attack_Light)` → `Combat->PushInputTag(BufferedTag);` 把输入**放回缓存** → `false`。注释：缓存里可能存的是别的意图（玩家在攻击后摇里按了闪避），那种情况不该由轻击 GA 处理，放回缓存让对应的能力去取。
  4. `const int32 NextIndex = CurrentSegmentIndex + 1;`；`if (!Module->GetLightSegment(NextIndex))` → 打 **Log** 级别的日志（不是 Verbose，理由见下）说明"当前第 %d 段，而攻击模组 %s 里只配了 %d 段轻击。想继续连就在该 DA 的 Light Attacks 数组里补上第 %d 条" → `false`。
  5. 成功路径：`CurrentSegmentIndex = NextIndex; Combat->SetComboIndex(NextIndex); StartSegment(NextIndex); return true;`
- **为什么这么写**：段数不够时用 Log 而非 Verbose 的理由（注释原文）："连招打不全"最常见的原因就是模组里段数不够，而 Verbose 在默认日志级别下看不见 —— 结果就是玩家按了没反应、日志一片空白，只能靠猜。这条日志直接把"差哪一段"说出来。
- **被谁调用 / 调用谁**：被 `OnComboWindowOpen`、`OnComboWindowClose`、`OnSimulatedWindowClosed` 调用。内部调用 `GetCombatComponent`、`GetAttackModule`、`ConsumeInputTag`、`PushInputTag`、`Module->GetLightSegment`、`SetComboIndex`、`StartSegment`。

#### `void FinishCombo(bool bWasCancelled)`

- **干什么**：连段收尾：清索引、停轨迹任务、摘蒙太奇回调（但**不停**动画）、清定时器、结束能力。
- **关键实现**：
  1. `Combat->ResetCombo()`（若拿得到）。注释：放在这里而不是"衔接窗口关闭时"，是因为可能有多条结束路径（`AttackEnd` / 蒙太奇完成 / 被打断），统一在收尾处清最可靠。
  2. `TraceTask` → `EndTask()`、置空。
  3. `DetachCurrentSegmentMontageTask(); CurrentSegmentMontage = nullptr;` —— 注释：摘掉最后一段的回调，但**不停**它的蒙太奇 —— 收尾时希望这一段的动画自然播完（尤其是 `AttackEnd` 之后那 5% 的后摇）；摘回调是因为能力结束后蒙太奇还会继续播完，那时它会广播 `OnInterrupted`/`OnCompleted`，没摘掉就会再进一次 `FinishCombo`，属于自己喊自己。
  4. `ClearTimer(SimulatedComboTimer)`。
  5. `EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, /*bReplicateEndAbility*/ true, bWasCancelled);`
- **为什么这么写**：见上。
- **被谁调用 / 调用谁**：被 `ActivateAbility`（提交失败 / 配置缺失）、`StartSegment`（段不存在 / Module 为空）、`OnSimulatedWindowClosed`、`OnAttackEndEvent`、`OnMontageCompleted`、`OnMontageInterrupted` 调用。内部调用 `ResetCombo`、`DetachCurrentSegmentMontageTask`、`EndAbility`。

#### `void DetachCurrentSegmentMontageTask()`

- **干什么**：摘掉注册在"当前段蒙太奇任务"上的两个回调并 `EndTask()`。**不停蒙太奇本身**。
- **关键实现**：`CurrentSegmentMontageTask` 为空直接 return；否则 `OnCompleted.RemoveDynamic(this, &OnMontageCompleted)`、`OnInterrupted.RemoveDynamic(this, &OnMontageInterrupted)`、`EndTask()`、指针置空。
- **为什么这么写**（注释原文，关键）：**必须先 `RemoveDynamic`，只调 `EndTask()` 是不够的**。`UAbilityTask_PlayMontageAndWait` 广播回调前的守卫是 `ShouldBroadcastAbilityTaskDelegates()`，它判断的是**能力**是否激活（`bool ShouldBroadcast = (Ability && Ability->IsActive()); // AbilityTask.cpp:199`）。连段期间能力当然一直激活着 —— 所以旧任务哪怕已经结束，它的 `OnCompleted` 照样会把我们喊醒。摘掉绑定是唯一不依赖引擎内部状态判断的做法。
- **被谁调用 / 调用谁**：被 `StopCurrentSegmentMontage()` 与 `FinishCombo()` 调用。

#### `void StopCurrentSegmentMontage()`

- **干什么**：切段前的完整清理：摘回调 + 停蒙太奇 + 清空引用。
- **关键实现**：`DetachCurrentSegmentMontageTask();` → 若 `CurrentSegmentMontage` 有效：取 ASC，`ASC->StopMontageIfCurrent(*CurrentSegmentMontage, 0.f)`，然后 `CurrentSegmentMontage = nullptr`。
- **为什么这么写**：
  - 三件事缺一不可（注释原文）：① `DetachCurrentSegmentMontageTask()` 摘掉**任务**的回调；② 停掉旧蒙太奇，让它立刻终止（不停的话它会一路播到自己的「攻击结束」通知，同样会让连段提前收尾）；③ 清空引用，避免下一段误用上一段的指针。
  - 为什么"停"不等于"它的 Notify 全都不会再来了"（注释原文）：引擎会为它身上**还活着的 NotifyState** 补发 `NotifyEnd`（`UAnimInstance::TriggerMontageEndedEvent`，`AnimInstance.cpp:2507`），所以窗口类的迟到事件仍然要靠 `IsEventFromCurrentSegment()` 按来源过滤 —— 停蒙太奇和过滤是两件事，谁也替代不了谁。
  - 用 `StopMontageIfCurrent` 而不是无脑 `CurrentMontageStop` 的理由（注释原文）：前者只在这个蒙太奇**确实是当前蒙太奇**时才停，万一将来有别的能力（闪避等）抢了动画，不会被我误停。混合时间传 0 —— 新一段紧接着就接上了，不需要它慢慢淡出。
- **被谁调用 / 调用谁**：被 `StartSegment`、`EndAbility` 调用。内部调用 `DetachCurrentSegmentMontageTask`、`GetAbilitySystemComponentFromActorInfo`、`ASC->StopMontageIfCurrent`。

#### `bool IsEventFromCurrentSegment(const FGameplayEventData& Payload) const`

- **干什么**：判断一条来自 AnimNotify 的 GameplayEvent 是不是**当前这一段**发出的。
- **关键实现**：`SourceMontage = Payload.OptionalObject2.Get()`；**任一侧为空就放行返回 true**（`!SourceMontage || !CurrentSegmentMontage`）；否则 `return SourceMontage == CurrentSegmentMontage;`
- **为什么这么写**（注释原文，本类最长的一段理由）：
  - 引擎在蒙太奇被停掉时，会**主动给所有还活着的 NotifyState 补发 `NotifyEnd`**（`UAnimInstance::TriggerMontageEndedEvent`，`AnimInstance.cpp:2507`，原注释 "Send end notifications for anim notify state when we are stopped"）。而连段接下一段的做法恰恰就是"停掉上一段的蒙太奇"。于是：第 N 段窗口开启 → 接上第 N+1 段 → 第 N 段蒙太奇被停 → 引擎补发第 N 段的「窗口关闭」→ 下一帧到达 GA → 若 GA 不辨来源，会当成"当前这段的窗口关闭"处理 → 顺手把缓存里的下一次按键吃掉，凭空多跳一段。表现是"快速连打时会跳段 / 连招打不全"，且只在连打时复现。
  - 判据是 Notify 在事件里带上的来源蒙太奇（`OptionalObject2`）。老资产没带这个字段时**放行**（向后兼容），不会因为忘配而完全不触发。
  - 放行的取舍：宁可放过一条迟到事件，也不要让正常连段整个失灵 —— 后者是"完全不能玩"，前者只是偶发跳段。
  - ⚠️ 已知局限（注释原文）：这里比的是**蒙太奇资产**，不是"哪一个播放实例"。如果连续两段用了同一个蒙太奇资产，上一段的迟到事件就认不出来。引擎自己对付这个问题用的是 `MontageInstanceID`（见 `UAnimInstance::TriggerMontageEndedEvent` 里的注释："Compare against the montage instance ID to prevent ending notify states from other instances of the same montage"），Notify 侧可以从 `EventReference` 里取到它；但 GA 侧拿不到"当前实例 ID"这个量，得再绕一圈去查 `AnimInstance`。本工程每段用的是各自独立的蒙太奇资产（`AM_Light_01~05`），踩不到这个边界，所以先不做；如果将来出现"两段共用同一个蒙太奇"的设计，这里要一起改。
- **被谁调用 / 调用谁**：被 `OnAttackWindowOpen`、`OnAttackWindowClose`、`OnComboWindowOpen`、`OnComboWindowClose` 调用（**`OnAttackEndEvent` 没有调用它** —— 见该函数）。不调用其它函数。

#### `void StartSimulatedSegment()`

- **干什么**：没有蒙太奇时的模拟时序：立刻施加一次命中、开启衔接窗口、定时关闭窗口。
- **关键实现**：`World` 为空 → `FinishCombo(false)`；打 Verbose 日志；`PerformSimulatedHit();`；`bComboWindowOpen = true;`；`SetTimer(SimulatedComboTimer, this, &URPG_GA_LightAttack::OnSimulatedWindowClosed, FMath::Min(SimulatedComboWindowDuration, SimulatedSegmentDuration), false)`（非循环）。
- **为什么这么写**：类头注释说明这条分支的存在意义 —— 在动画做好之前就能用日志验证：连段能不能推进、倍率对不对、耐力扣得对不对；动画接上后这条分支自然就不再走了。
- **被谁调用 / 调用谁**：被 `StartSegment` 调用。内部调用 `PerformSimulatedHit`、`FinishCombo`。

#### `void OnSimulatedWindowClosed()`

- **干什么**：模拟模式下衔接窗口关闭（没有后续动画事件，所以直接收尾）。
- **关键实现**：`bComboWindowOpen = false;` → `if (!TryStartNextSegment()) FinishCombo(false);`
- **为什么这么写**：注释 —— 模拟模式下没有"后摇结束"的动画事件，所以窗口一关就收尾。
- **被谁调用 / 调用谁**：由 `SimulatedComboTimer` 定时器回调。调用 `TryStartNextSegment`、`FinishCombo`。

#### `void PerformSimulatedHit()`

- **干什么**：在角色身前做一次球形 overlap 并施加伤害。仅用于没有蒙太奇时验证伤害链路。
- **关键实现**：取 `Avatar`、`World`；`Origin = Avatar->GetActorLocation() + Avatar->GetActorForwardVector() * 150.f`；`FCollisionQueryParams Params(TEXT("RPGSimulatedHit"), /*bTraceComplex*/ false, Avatar)`；`FCollisionShape::MakeSphere(80.f)`；`World->OverlapMultiByChannel(Overlaps, Origin, FQuat::Identity, ECC_Pawn, Shape, Params)`；用 `TSet<AActor*> UniqueTargets` 去重（Overlap 可能对同一个 Actor 返回多个结果 —— 多个碰撞体）；对每个去重后的 Actor 调 `ApplyDamageToTarget(HitActor, CurrentDamageMultiplier, nullptr)`（**注意没有传 HitResult**）；若 `UniqueTargets.Num() == 0` 打 Verbose "模拟命中：身前没有目标"。
- **为什么这么写**：注释 —— "在角色身前 150cm、半径 80cm 的范围内找目标。这只是为了验证伤害链路能跑通，不是真实的手感设计 —— 动画接上后由 WeaponTrace 接管。"
- **被谁调用 / 调用谁**：被 `StartSimulatedSegment` 调用。内部调用 `ApplyDamageToTarget`。
- ⚠️ 事实记录：本函数与基类的 `PerformSimulatedMeleeHit(倍率, 150, 80)` 逻辑等价（同样的查询参数与去重写法），但**本类没有调用基类那个封装**，而是在 .cpp 里自带一份私有实现。

#### `void OnAttackWindowOpen(FGameplayEventData Payload)`

- **干什么**：伤害判定窗口开启 → 创建武器轨迹检测任务。
- **关键实现**：
  1. `if (!IsEventFromCurrentSegment(Payload)) return;`（迟到事件过滤）。
  2. `Module` 为空 → return。
  3. 取默认值：`Source = Module->TraceSource`、`Radius = Module->TraceRadius`、`SocketStart = Module->LeftHandSocket`、`SocketEnd = Module->RightHandSocket`。
  4. `Cast<URPG_AttackWindowPayload>(Payload.OptionalObject.Get())` 成功且 `bOverrideTrace` → 用 payload 的 `TraceSource / TraceRadius / SocketStart / SocketEnd` 覆盖；并打 VeryVerbose 日志 `"判定窗口开启（%s）"`（`AttackTag` 无效时打 `未标标签`）。若 Cast 失败则连日志都不打。
  5. `Source == ERPG_TraceSource::WeaponBlade` → `SocketStart = Module->BladeStartSocket; SocketEnd = Module->BladeEndSocket;` —— 注释：刀锋检测用武器上的 Socket，不用手部。
  6. `Source == ERPG_TraceSource::Projectile` → 打 Verbose `"远程模组的发射物逻辑尚未实现（阶段 6）"` → return（不建任务）。
  7. 若 `TraceTask` 已存在 → `EndTask()`、置空（注释：上一段的检测任务可能还在跑，比如蒙太奇重叠）。
  8. `TraceTask = URPG_AbilityTask_WeaponTrace::CreateWeaponTraceTask(this, Source, Radius, SocketStart, SocketEnd);` → 非空则绑 `OnHit`→`OnWeaponTraceHit`、`ReadyForActivation()`。
- **为什么这么写**：检测参数为什么可以由 Notify 覆盖，见 `URPG_AttackWindowPayload` 的注释：检测参数（半径、Socket 名）本质上由动画决定；伤害倍率相反，属于数值设计，应该集中在 `AttackModuleData` 管理，所以 payload 里**没有**倍率字段。
- **被谁调用 / 调用谁**：由 `UAbilityTask_WaitGameplayEvent`（监听 `Event.Combat.AttackWindow.Open`）回调，事件的发送方是蒙太奇上的 AnimNotify。调用 `IsEventFromCurrentSegment`、`GetAttackModule`、`URPG_AbilityTask_WeaponTrace::CreateWeaponTraceTask`。

#### `void OnAttackWindowClose(FGameplayEventData Payload)`

- **干什么**：停止轨迹检测任务。
- **关键实现**：`if (!IsEventFromCurrentSegment(Payload)) return;` → `TraceTask` 有效则 `EndTask()`、置空。
- **为什么这么写**：注释（★ 标记）—— 这一条尤其重要：上一段的"判定窗口关闭"会在下一段刚播起来时到达，不过滤的话会把下一段刚开起来的轨迹检测任务直接掐掉 —— 症状是"连招里后面几段打不到人"，而且看起来像判定窗口配错了。
- **被谁调用 / 调用谁**：由 `UAbilityTask_WaitGameplayEvent`（`Event.Combat.AttackWindow.Close`）回调。调用 `IsEventFromCurrentSegment`。

#### `void OnComboWindowOpen(FGameplayEventData Payload)`

- **干什么**：开启衔接窗口，并立刻尝试消费缓存续段。
- **关键实现**：`if (!IsEventFromCurrentSegment(Payload)) return;` → `bComboWindowOpen = true;` → `if (!TryStartNextSegment())` 打 VeryVerbose `"衔接窗口开启，缓存暂时为空"`。
- **为什么这么写**：注释 —— 窗口一开就检查缓存：玩家可能早在动画前半段就按了下一段，那时输入已经在缓存里等着；**等窗口关闭才检查会让操作感觉迟滞**。
- **被谁调用 / 调用谁**：由 `UAbilityTask_WaitGameplayEvent`（`Event.Combat.ComboWindow.Open`）回调。调用 `IsEventFromCurrentSegment`、`TryStartNextSegment`。

#### `void OnComboWindowClose(FGameplayEventData Payload)`

- **干什么**：关闭衔接窗口；能续段就续，不能续就把连段索引重置（但**不结束能力**）。
- **关键实现**：`if (!IsEventFromCurrentSegment(Payload)) return;` → `bComboWindowOpen = false;` → `if (TryStartNextSegment()) return;` → 否则 `Combat->ResetCombo()`，打 VeryVerbose `"衔接窗口关闭，连段重置（等待后摇结束）"`。
- **为什么这么写**：
  - 过滤的理由（注释原文 ★）：这条是"跳段"的元凶 —— 上一段被停掉时引擎补发的「衔接窗口关闭」会在接上下一段后一帧到达；不过滤的话它会把缓存里的下一次按键吃掉，凭空多接一段，表现为快速连打时跳段。
  - 不结束能力的理由（注释原文）：动画还在播后摇，玩家可能还要做别的操作；只把连段索引清掉，这样等后摇结束再来一次攻击会从起手式开始，而不是莫名其妙地从第 4 段接上。
- **被谁调用 / 调用谁**：由 `UAbilityTask_WaitGameplayEvent`（`Event.Combat.ComboWindow.Close`）回调。调用 `IsEventFromCurrentSegment`、`TryStartNextSegment`、`GetCombatComponent`、`ResetCombo`。

#### `void OnAttackEndEvent(FGameplayEventData Payload)`

- **干什么**：动画最后一帧的 Notify 广播出来的"攻击结束"→ 连段收尾。
- **关键实现**：打 Log `"第 %d 段收到「攻击结束」通知 → 连段收尾"`（段号 `CurrentSegmentIndex + 1`）→ `FinishCombo(false)`。
- **为什么这么写**：注释 —— "这是这一段真正的结束点"。⚠️ 事实记录：本回调**没有**调用 `IsEventFromCurrentSegment()` 做来源过滤（三个窗口回调都调了），头文件与 .cpp 里**没有说明原因**（注释未说明）。
- **被谁调用 / 调用谁**：由 `UAbilityTask_WaitGameplayEvent`（`Event.Combat.AttackEnd`）回调（`RPG_BTTask_Attack` 也监听同一事件来结束它的潜在任务）。调用 `FinishCombo`。

#### `void OnMontageCompleted()`

- **干什么**：蒙太奇自然播完 → 兜底结束能力。
- **关键实现**：打 Log `"第 %d 段的蒙太奇自然播完 → 连段结束（若此刻你还在连打，说明有上一段的蒙太奇残留没清干净）"` → `FinishCombo(false)`。
- **为什么这么写**：注释 —— 正常情况下 `AttackEnd` Notify 会先触发，走到这里说明蒙太奇上没配那个 Notify，作为兜底结束能力。⚠️ 排查提示（注释原文）：这条日志**只应该在最后一段真正播完时出现一次**；如果它出现在"你还在连打"的时候，说明有上一段的蒙太奇残留没被清掉，它的完成回调把当前这一段误判成整套打完了 —— 那正是连招打不全的原因。
- **被谁调用 / 调用谁**：被 `CurrentSegmentMontageTask->OnCompleted` 委托回调（`StartSegment` 里绑定）。调用 `FinishCombo`。

#### `void OnMontageInterrupted()`

- **干什么**：蒙太奇被打断 → 以 `bWasCancelled = true` 结束连段。
- **关键实现**：打 Log `"第 %d 段的蒙太奇被打断 → 连段结束"` → `FinishCombo(true)`。
- **为什么这么写**：注释未说明（只有日志）。
- **被谁调用 / 调用谁**：被 `CurrentSegmentMontageTask->OnInterrupted` 委托回调。调用 `FinishCombo`。

#### `void OnWeaponTraceHit(const TArray<FHitResult>& Hits)`

- **干什么**：轨迹检测命中 → 逐个目标施加伤害。
- **关键实现**：`for (const FHitResult& Hit : Hits) ApplyDamageToTarget(Hit.GetActor(), CurrentDamageMultiplier, &Hit);` —— **传了 `&Hit`**，所以 GameplayCue 能拿到命中点播特效。
- **为什么这么写**：注释未说明（理由在基类 `ApplyDamageToTarget` 的参数说明里）。
- **被谁调用 / 调用谁**：被 `TraceTask->OnHit` 委托回调。调用 `ApplyDamageToTarget`。

---

### `RPG_GA_HeavyAttack`

**一句话职责**：一个能力两种形态 —— 不在轻击连段中时是**蓄力重击**（按住右键蓄力，最多 3 段，蓄力期间持续掉耐力，松手/耐力耗尽/到上限时释放）；正在轻击连段中时是**切手技**（打断轻击，播专门的蒙太奇，一次性伤害后收招）。

**关键配置**（构造函数里设的，逐条）：

- `SetAssetTags(FGameplayTagContainer(RPGTags::Ability_Attack_Heavy))` —— 能力身份标签（也是轻击 `CancelAbilitiesWithTag` 的目标）。
- `ActivationOwnedTags.AddTag(RPGTags::Ability_Attack_Heavy)` —— 供其他系统查询"当前是不是在重击"（比如动画、AI 决策）。
- `ActivationOwnedTags.AddTag(RPGTags::State_Attacking)` —— 状态标签，动画蓝图 / UI / AI 读的是这个。注释：轻击连段和重击（蓄力+释放）挂的是同一个 `State.Attacking`，动画侧因此只需要判断一次"在不在攻击"。**蓄力标签（`State.Attack.Charging.*`）故意不在这里** —— 它只覆盖"蓄力"那一小段时间，而本能力从蓄力起手到释放收招一直处于激活态；`ActivationOwnedTags` 表达不了"激活期间的一部分时间"，所以那部分交给 `UpdateChargeTags()` 手动增删。
- ⚠️ **故意不设 `CancelAbilitiesWithTag`**（头文件与 .cpp 都有大段说明，原文要点）：
  - 直觉上应该在这里声明 `CancelAbilitiesWithTag = Ability.Attack.Light` 让 GAS 自动取消轻击，**但那样切手技会永远判断不出来**。
  - 原因是引擎的执行顺序（`GameplayAbility.cpp:1020`）：`CallActivateAbility()` → `PreActivate()`（`:1022`）→ `ApplyAbilityBlockAndCancelTags()`（`:999`）→ `CancelAbilities` → 轻击的 `EndAbility` → 摘掉 `ActivationOwnedTags`（`Ability.Attack.Light` 就在里面）；然后才轮到 `ActivateAbility()`（`:1023`）—— 轮到这里时标签已经没了。
  - 也就是说：**声明式取消会先于我们自己的分支判断执行**。症状是"按右键永远走蓄力，切手技像不存在一样"，而且**不报任何错**。
  - 所以改成在 `ActivateAbility` 开头先读标签、读完再显式取消，牺牲声明式的优雅换行为可预测。
- 没有 `AbilityTriggers` —— 由输入标签激活。
- **未覆盖**：`InstancingPolicy` / `NetExecutionPolicy` / `NetSecurityPolicy` / `ActivationBlockedTags`（同基类默认）。

**成员变量**（全部）：

| 声明 | 含义与初值 |
| --- | --- |
| `bool bTransitionBranch = false` | 本次激活走的是切手技分支 |
| `bool bReleasing = false` | 是否已进入"释放"阶段。用途（注释原文）：释放时会播新蒙太奇，那会打断仍在播放的蓄力起手蒙太奇，从而触发 `OnMontageInterrupted`；没有这个标记会把正常的"蓄力→释放"误判成"蓄力被打断"而结束能力 |
| `int32 CurrentChargeLevel = 0` | 当前蓄力段位（0 = 还没到最低门槛，1~3 = 对应段位） |
| `float ChargeElapsed = 0.f` | 已累计的蓄力时间（秒） |
| `FGameplayTagContainer ActiveChargeTags` | 当前挂在 ASC 上的蓄力标签集合（父标签 + 段位标签）。为什么要保存"挂了哪些"（注释原文）：是为了精确摘除 —— 直接 `Clear` 固定几个标签也行，但那样一旦将来加了新标签而忘了同步两边，就会留下永久残留的状态标签；用集合记录实际挂上去的内容，增删永远成对 |
| `float CurrentDamageMultiplier = 1.f` | 本次攻击的伤害倍率 |
| `FTimerHandle ChargeTimer` | 蓄力检查定时器 |
| `UPROPERTY() TObjectPtr<URPG_AbilityTask_WeaponTrace> TraceTask` | 轨迹检测任务 |
| `UPROPERTY() TObjectPtr<UAbilityTask_PlayMontageAndWait> CurrentStageMontageTask` | 当前正在播的蒙太奇任务 |
| `UPROPERTY() TObjectPtr<UAnimMontage> CurrentStageMontage` | 当前正在播的蒙太奇 |
| `UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="RPG|Charge", meta=(ClampMin="0.02")) float ChargeTickInterval = 0.1f` | 蓄力检查频率（秒）。注释：0.1 秒够用 —— 再密也不能让蓄力段位变化更快，因为段位门槛本来就是零点几秒级别的 |
| `UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="RPG|Charge", meta=(ClampMin="0.1")) float MaxChargeTime = 3.f` | 最大蓄力时间（秒）。到顶自动释放。注释：防止玩家一直按着不放导致蓄力无限累积 |

播放顺序（头文件注释）：切手技只有 `ComboTransition`（一次）；蓄力是 `ChargeStart → ChargeLoop → Release`。每次换段都必须先把上一段拆干净。

#### `URPG_GA_HeavyAttack::URPG_GA_HeavyAttack()`

- **干什么**：设置标签（见"关键配置"），并**刻意留空**取消声明。
- **关键实现**：只有 `SetAssetTags` 与两次 `ActivationOwnedTags.AddTag`；没有任何 `CancelAbilitiesWithTag`。
- **为什么这么写**：见"关键配置"里对引擎执行顺序的完整转述。
- **被谁调用 / 调用谁**：UE 反射构造。

#### `void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)`

- **干什么**：先判定走哪个分支，再取消轻击，再分派到 `StartTransition()` / `StartCharge()`。
- **关键实现**（⭐ 顺序是本类的核心）：
  1. **在 `Super::ActivateAbility` 之前**：`UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo(); bTransitionBranch = ASC && ASC->HasMatchingGameplayTag(RPGTags::Ability_Attack_Light);` —— 注释：这是**唯一**还能看到轻击标签的时刻，一旦下面执行了取消它就没了。然后打 Verbose 日志 `"重击分支判断：轻击标签%s → 走%s"`（存在/不存在 → 切手技/蓄力重击）。注释：这条日志是排查"切手技打不出来"的第一现场；用 Verbose 是因为每次按右键都会打。
  2. `Super::ActivateAbility(...)`（**晚于分支判断**，与轻击相反）。
  3. `CommitAbility` 失败 → `FinishHeavyAttack(true)`。
  4. `Combat = GetCombatComponent()`、`Module = GetAttackModule()`；任一为空 → Warning（两种原因分开报：`"角色上没有 CombatComponent"` / `"没有配置攻击模组"`）→ `FinishHeavyAttack(true)`。
  5. `FGameplayTag TriggerInputTag; Combat->ConsumeInputTag(TriggerInputTag);` —— 注释：消耗掉触发本次激活的输入（理由同轻击 GA）。
  6. **取消轻击**：`if (ASC) { const FGameplayTagContainer CancelTags(RPGTags::Ability_Attack_Light); ASC->CancelAbilities(&CancelTags, nullptr, this); }`。注释要点：匹配用的是 GA 的 **AssetTags**（`GA_LightAttack` 通过 `SetAssetTags` 声明的那个），不是 `ActivationOwnedTags` —— 这是 `UAbilitySystemComponent::CancelAbilities()` 的实现细节，传错标签会静默地什么都不取消，又是一次"不报错但没效果"；`Ignore` 传 `this` 防止把自己也取消掉。
  7. 复位状态：`bReleasing = false; CurrentChargeLevel = 0; ChargeElapsed = 0.f; CurrentDamageMultiplier = 1.f;`
  8. `BindGameplayEventListeners();`
  9. `bTransitionBranch ? StartTransition() : StartCharge();`
- **为什么这么写**：见上（顺序不可颠倒的两处：分支判断在前、取消在后）。
- **被谁调用 / 调用谁**：GAS 激活流程（`TryActivateAbilityByInputTag`）。调用 `GetAbilitySystemComponentFromActorInfo`、`Super::ActivateAbility`、`CommitAbility`、`GetCombatComponent`、`GetAttackModule`、`Combat->ConsumeInputTag`、`ASC->CancelAbilities`、`BindGameplayEventListeners`、`StartTransition`/`StartCharge`、`FinishHeavyAttack`。

#### `void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)`

- **干什么**：统一兜底清理：停轨迹任务、摘蓄力标签、拆蒙太奇、清定时器。
- **关键实现**：
  1. `TraceTask` → `EndTask()`、置空。
  2. `ClearChargeTags();` —— 注释：兜底摘掉蓄力标签。正常路径上 `ReleaseCharge()` 已经摘过了，但"能力被取消 / 被打断"这条路不会经过 `ReleaseCharge`；漏掉这一句，角色会永久停在"蓄力中"，而且只在"蓄力时被敌人打断"这类特定时序下出现，很难复现。
  3. `StopCurrentStageMontage();` —— 注释：同上，"被外部取消"这条路不走 `FinishHeavyAttack`；正常结束时 `FinishHeavyAttack` 已经把指针清空了，这里是空操作。
  4. `ClearTimer(ChargeTimer)`。
  5. `Super::EndAbility(...)`。
- **为什么这么写**：见上。
- **被谁调用 / 调用谁**：GAS（外部取消路径）与 `FinishHeavyAttack()`。调用 `ClearChargeTags`、`StopCurrentStageMontage`、`Super::EndAbility`。

#### `void StartTransition()`

- **干什么**：切手技分支：扣耐力、挂切手技标签、播切手蒙太奇（无蒙太奇则立刻结算一次伤害并收招）。
- **关键实现**：
  1. `Module` 为空 → `FinishHeavyAttack(true)`。
  2. `CurrentDamageMultiplier = Module->ComboTransitionMultiplier;` → `ConsumeStamina(Module->ComboTransitionStaminaCost);`
  3. 挂标签：`ASC->AddLooseGameplayTags(FGameplayTagContainer(RPGTags::State_Attack_Transition), /*Count=*/1, EGameplayTagReplicationState::CountToOwner);` —— 注释：走的是和蓄力标签同一套机制（loose tag + CountToOwner 复制），理由也一样：这是**公共状态**，不是本能力的私有成员 —— HUD 要显示招式名、动画蓝图将来可能要单独处理切手技的收招姿势。摘除在 `ClearChargeTags()` 里一并做，保证"挂上的地方"和"摘掉的地方"成对。
  4. 打 Log `"切手技（倍率 %.2f，耐力 %.1f）"`。
  5. `StopCurrentStageMontage();`
  6. `PlayMontageOrSkip(Module->ComboTransitionMontage, TEXT("ComboTransition"))` → 非空则绑 `OnCompleted`→`OnMontageCompleted`、`OnInterrupted`→`OnMontageInterrupted`、`ReadyForActivation()`、`TrackCurrentStageMontage(MontageTask, Module->ComboTransitionMontage)`。
  7. 返回空（没配蒙太奇）→ `PerformSimulatedMeleeHit(CurrentDamageMultiplier, /*ForwardOffset*/ 180.f, /*Radius*/ 90.f); FinishHeavyAttack(false);`
- **为什么这么写**：见上（标签用 loose tag 的理由）。
- **被谁调用 / 调用谁**：被 `ActivateAbility` 调用（`bTransitionBranch == true`）。调用 `GetAttackModule`、`ConsumeStamina`、`AddLooseGameplayTags`、`StopCurrentStageMontage`、`PlayMontageOrSkip`、`TrackCurrentStageMontage`、`PerformSimulatedMeleeHit`、`FinishHeavyAttack`。

#### `void StartCharge()`

- **干什么**：蓄力分支：挂 `State.Attack.Charging` 父标签、播起手蒙太奇（不绑 `OnCompleted`）、启动循环定时器。
- **关键实现**：
  1. `Module` 为空 → `FinishHeavyAttack(true)`。
  2. 打 Log `"开始蓄力（最长 %.1f 秒，每秒耗耐力 %.1f）"`（读 `MaxChargeTime` 与 `Module->HeavyAttack.ChargeStaminaDrainPerSecond`）。
  3. `UpdateChargeTags(0);` —— 注释：此刻段位还是 0（还没到第一段门槛），所以只有父标签 `State.Attack.Charging`，覆盖起手动画那段时间。
  4. `StopCurrentStageMontage();`
  5. `PlayMontageOrSkip(Module->HeavyAttack.ChargeStartMontage, TEXT("ChargeStart"))` → 非空则 `OnCompleted` 绑 **`OnChargeStartMontageCompleted`**（不是 `OnMontageCompleted`）、`OnInterrupted` 绑 `OnMontageInterrupted`、`ReadyForActivation()`、`TrackCurrentStageMontage(...)`。
  6. 注释：没有起手蒙太奇也照常蓄力 —— 蓄力逻辑本身不依赖动画。
  7. `SetTimer(ChargeTimer, this, &URPG_GA_HeavyAttack::TickCharge, ChargeTickInterval, /*bLoop*/ true);` —— 注释：用循环定时器而不是 Tick，蓄力只需要按固定间隔检查"有没有升段"，不需要每帧精度。
- **为什么这么写**：绑错回调的理由（注释原文 ★）：起手动画播完只意味着"摆好蓄力姿势了"，玩家很可能还按着不放；绑 `OnMontageCompleted` 的话，按下右键的瞬间整套重击就打完了 —— 蓄力机制形同虚设。
- **被谁调用 / 调用谁**：被 `ActivateAbility` 调用（`bTransitionBranch == false`）。调用 `GetAttackModule`、`UpdateChargeTags`、`StopCurrentStageMontage`、`PlayMontageOrSkip`、`TrackCurrentStageMontage`、`SetTimer`。

#### `void TickCharge()`

- **干什么**：蓄力节拍：累计时间、持续扣耐力、检查升段、耐力耗尽强制释放、到上限自动释放。
- **关键实现**：
  1. `Module` 为空 → `ReleaseCharge(); return;`
  2. `ChargeElapsed += ChargeTickInterval;`
  3. `ConsumeStamina(Module->HeavyAttack.ChargeStaminaDrainPerSecond * ChargeTickInterval);` —— 注释：这是蓄力的代价：贪满蓄力会让耐力见底，之后连闪避都放不出来。
  4. `const int32 NewLevel = Module->GetChargeLevelForTime(ChargeElapsed);` 若 `!= CurrentChargeLevel` → 更新 `CurrentChargeLevel`、`UpdateChargeTags(NewLevel)`（注释：段位标签同步给 ASC —— 动画蓝图据此在起手/蓄满之间切换姿势）、打 Log `"蓄力升到 %d 段（%.2f 秒）"`。
  5. `if (!HasEnoughStamina(1.f))` → 打 Log `"耐力耗尽，强制释放蓄力"` → `ReleaseCharge(); return;` —— 注释：不强制释放的话，玩家可以在耐力耗尽后一直保持蓄力姿势，既不能再攻击也不受惩罚，等于白嫖一个无敌的僵持状态。
  6. `if (ChargeElapsed >= MaxChargeTime)` → 打 Log `"蓄力到达上限，自动释放"` → `ReleaseCharge();`
- **为什么这么写**：见上（每一条都有注释理由）。
- **被谁调用 / 调用谁**：由 `ChargeTimer` 循环定时器回调。调用 `GetAttackModule`、`ConsumeStamina`、`Module->GetChargeLevelForTime`、`UpdateChargeTags`、`HasEnoughStamina`、`ReleaseCharge`。

#### `void ReleaseCharge()`

- **干什么**：释放蓄力攻击：停表、摘蓄力标签、取段位配置、扣耐力、播释放蒙太奇（或立刻结算伤害）。
- **关键实现**：
  1. `ClearTimer(ChargeTimer);` —— 注释：先停掉计时，避免释放过程中 `TickCharge` 又被调用一次造成重复释放。
  2. `bReleasing = true;`
  3. `ClearChargeTags();` —— 注释：蓄力阶段到此结束，进入释放动作，摘掉蓄力标签；释放蒙太奇由 `DefaultSlot` 播放，动画蓝图应该切回"攻击中"而不是停在"蓄力中"。
  4. `Module` 为空 → `FinishHeavyAttack(false)`。
  5. `const FRPG_HeavyAttackLevel* Level = Module->GetHeavyLevel(CurrentChargeLevel);` 为空（蓄力时间还不够最低门槛）→ `Level = Module->GetHeavyLevel(1); CurrentChargeLevel = Level ? 1 : 0;` —— 注释：按第 1 段释放；不取消能力是因为"点一下右键"的意图很明确，取消会让玩家觉得按键失灵。
  6. 若仍为空 → Warning `"攻击模组的 HeavyAttack.Levels 为空，无法释放蓄力攻击"` → `FinishHeavyAttack(true)`。
  7. `CurrentDamageMultiplier = Level->DamageMultiplier; ConsumeStamina(Level->StaminaCost);` → 打 Log `"释放蓄力重击（%d 段，倍率 %.2f，耐力 %.1f）"`。
  8. `StopCurrentStageMontage();`（注释：蓄力循环动画到此为止，拆干净再播释放）→ `PlayMontageOrSkip(Level->ReleaseMontage, TEXT("HeavyRelease"))` → 绑 `OnCompleted`→`OnMontageCompleted`、`OnInterrupted`→`OnMontageInterrupted`、`ReadyForActivation()`、`TrackCurrentStageMontage(...)`。
  9. 无释放蒙太奇 → `PerformSimulatedMeleeHit(CurrentDamageMultiplier, /*ForwardOffset*/ 180.f, /*Radius*/ 100.f); FinishHeavyAttack(false);`（注释：重击范围比轻击大一些，所以偏移和半径都调大了）。
- **为什么这么写**：见上。
- **被谁调用 / 调用谁**：被 `TickCharge`（耐力耗尽 / 到上限）、`OnInputReleased` 调用。调用 `ClearTimer`、`ClearChargeTags`、`GetAttackModule`、`Module->GetHeavyLevel`、`ConsumeStamina`、`StopCurrentStageMontage`、`PlayMontageOrSkip`、`PerformSimulatedMeleeHit`、`FinishHeavyAttack`。

#### `void UpdateChargeTags(int32 NewLevel)`

- **干什么**：把蓄力标签同步到指定段位（先整批摘掉上一次挂的，再挂新的）。
- **关键实现**：
  1. ASC 为空 → return。
  2. `const EGameplayTagReplicationState RepState = EGameplayTagReplicationState::CountToOwner;` —— 注释：标签本身会复制给**所有**客户端（模拟代理也要看得出"这个敌人在蓄力"），只有计数只发给拥有者，对 `HasMatchingGameplayTag` 这类布尔查询没有影响。为什么不用默认的 `None`：默认参数是不复制，结果就是"自己看得到蓄力姿势，别人看你却是站着的"—— 单机测试完全正常，联机才暴露。
  3. `if (!ActiveChargeTags.IsEmpty()) { ASC->RemoveLooseGameplayTags(ActiveChargeTags, 1, RepState); ActiveChargeTags.Reset(); }` —— 注释：直接 Add 新的而不摘旧的会让计数越堆越高；状态标签的语义是"有没有"，计数堆高本身不会让判断出错，但 GameplayDebugger 里会很难看，而且一旦将来有人用 `GetGameplayTagCount` 做逻辑就会踩坑。
  4. `ActiveChargeTags.AddTag(RPGTags::State_Attack_Charging);` —— 注释：父标签覆盖"按住右键但还没到第 1 段门槛"的起手时间；少了它动画在起手那零点几秒里会以为"没在蓄力"。
  5. `switch (NewLevel)`：`case 1/2/3` 分别 `AddTag(State_Attack_Charging_Lv1/Lv2/Lv3)`；`default: break;`（还没到第 1 段门槛，只留父标签）。注释①：动画蓝图查的就是这三个，所以**必须**挂上 —— 不能只依赖数据资产里配的标签，否则一旦有人在 DA 里把 `ChargeLevelTag` 留空，蓄力姿势就永远不显示。
  6. 再挂数据资产里配的自定义标签：`GetAttackModule()` → `Module->GetHeavyLevel(NewLevel)` → 若 `Level->ChargeLevelTag.IsValid()` 则 `ActiveChargeTags.AddTag(Level->ChargeLevelTag)`。注释②：这是给"想做四段蓄力"或"想让别的系统（特效、音效）按段位响应"留的口子。
  7. `ASC->AddLooseGameplayTags(ActiveChargeTags, 1, RepState);`
- **为什么这么写**：两个来源都挂是刻意的（注释原文）：① 保证动画侧的契约稳定（动画只认固定的那三个标签）；② 保留数据驱动的扩展位（策划改 DA 就能新增段位，不用改动画）。如果只挂 ②，DA 配错了动画就瞎；只挂 ①，DA 里那个字段就成了摆设。
- **被谁调用 / 调用谁**：被 `StartCharge`（传 0）与 `TickCharge`（传新段位）调用。调用 `GetAbilitySystemComponentFromActorInfo`、`RemoveLooseGameplayTags`、`AddLooseGameplayTags`、`GetAttackModule`、`Module->GetHeavyLevel`。

#### `void ClearChargeTags()`

- **干什么**：摘掉全部蓄力标签，**外加**切手技标签。
- **关键实现**：
  1. **先**摘切手技标签：`ASC->RemoveLooseGameplayTags(FGameplayTagContainer(RPGTags::State_Attack_Transition), 1, CountToOwner);` —— 注释 ⚠️：必须在下面那个提前返回**之前**摘。切手技分支从来不碰 `ActiveChargeTags`，所以走到这里时它一定是空的 —— 把这段放到判空之后，切手技的标签就永远摘不掉，角色的"招式名"会一直卡在"切手技"上。用 `RemoveLooseGameplayTags` 而不是 `RemoveLooseGameplayTag` 是因为前者可以整批操作，也方便将来往这个集合里加标签。
  2. `if (ActiveChargeTags.IsEmpty()) return;` —— 注释：先判空再取 ASC，绝大多数调用发生在"没挂过标签"的情况下（比如直接走切手技分支），提前返回省掉一次组件查找。
  3. 非空 → `ASC->RemoveLooseGameplayTags(ActiveChargeTags, 1, CountToOwner);`
  4. `ActiveChargeTags.Reset();` —— 注释 ★：**无论 ASC 是否拿得到都要清空本地记录**。否则下次 `UpdateChargeTags` 会拿一份过期的集合去 Remove，而那份集合在 ASC 上早就不存在了 —— 摘了个寂寞，新标签也挂不上。
- **为什么这么写**：见上（三条注释理由均已转述）。
- **被谁调用 / 调用谁**：被 `ReleaseCharge`、`EndAbility` 调用。调用 `GetAbilitySystemComponentFromActorInfo`、`RemoveLooseGameplayTags`。

#### `void DetachCurrentStageMontageTask()`

- **干什么**：摘掉注册在"当前阶段蒙太奇任务"上的**三个**回调并 `EndTask()`。不停蒙太奇本身。
- **关键实现**：为空直接 return；否则依次 `OnCompleted.RemoveDynamic(this, &OnMontageCompleted)`、`OnCompleted.RemoveDynamic(this, &OnChargeStartMontageCompleted)`、`OnInterrupted.RemoveDynamic(this, &OnMontageInterrupted)`、`EndTask()`、置空。
- **为什么这么写**：注释原文 —— 必须先 `RemoveDynamic`，只调 `EndTask()` 拦不住迟到的回调：任务广播前的守卫是 `ShouldBroadcastAbilityTaskDelegates()`，它判断的是**能力**是否激活（`Ability && Ability->IsActive()`，`AbilityTask.cpp:199`），而不是任务是否结束；本能力从蓄力起手一路到释放收招都激活着，所以旧任务照样会把我们喊醒。三个都要摘：`ChargeStart` 蒙太奇绑的是 `OnChargeStartMontageCompleted`，其余两段绑的是 `OnMontageCompleted`，不摘干净会漏掉一条路径。
- **被谁调用 / 调用谁**：被 `StopCurrentStageMontage` 与 `FinishHeavyAttack` 调用。

#### `void StopCurrentStageMontage()`

- **干什么**：换段前的完整清理：摘回调 + 停蒙太奇 + 清空引用。
- **关键实现**：`DetachCurrentStageMontageTask();` → 若 `CurrentStageMontage` 有效：`ASC->StopMontageIfCurrent(*CurrentStageMontage, 0.f);`，然后置空。
- **为什么这么写**：注释 —— 三件事：摘回调、停蒙太奇、结束任务。理由同轻击 GA 的 `StopCurrentSegmentMontage()`：不停蒙太奇 → 它的 Notify 会继续广播（判定窗口、攻击结束）；不摘回调 → 它播完/被打断时会广播 `OnInterrupted`，把"蓄力起手播完切循环"这种正常切换误判成"蓄力被打断"。
- **被谁调用 / 调用谁**：被 `StartTransition`、`StartCharge`、`ReleaseCharge`、`OnChargeStartMontageCompleted`、`EndAbility`、`FinishHeavyAttack`（仅 `bWasCancelled` 时）调用。调用 `DetachCurrentStageMontageTask`、`ASC->StopMontageIfCurrent`。

#### `void TrackCurrentStageMontage(UAbilityTask_PlayMontageAndWait* Task, UAnimMontage* Montage)`

- **干什么**：记下刚播起来的这一段，供下次换段时清理。
- **关键实现**：两行赋值：`CurrentStageMontageTask = Task; CurrentStageMontage = Montage;`
- **为什么这么写**：头文件注释 —— 一个能力里先后播多段蒙太奇时，上一段的蒙太奇和它的 `PlayMontageAndWait` 任务会继续活着，在它自己动画播完/被打断时广播回调，干扰当前这一段的判断。
- **被谁调用 / 调用谁**：被 `StartTransition`、`StartCharge`、`ReleaseCharge`、`OnChargeStartMontageCompleted` 调用。

#### `void OnInputReleased()`

- **干什么**：松手 → 按累计时间决定释放段位。
- **关键实现**：`if (bTransitionBranch) return;`（切手技是瞬发招式，不响应松手）→ `if (bReleasing) return;`（已经在释放中就不要再触发一次）→ 打 Verbose `"松手，按 %.2f 秒的蓄力释放"` → `ReleaseCharge();`
- **为什么这么写**：见上两条守卫的注释。
- **被谁调用 / 调用谁**：由 `URPG_AbilitySystemComponent::NotifyInputReleased(InputTag)` 在能力仍激活时调用（`RPG_PlayerController` 收到 EnhancedInput 的 Completed 事件后触发）。调用 `ReleaseCharge`。

#### `void BindGameplayEventListeners()`

- **干什么**：建立 3 个 GameplayEvent 监听并激活。
- **关键实现**：与轻击同样的逐条写法（`AddDynamic` 要求字面量函数名）：`Event.Combat.AttackWindow.Open`→`OnAttackWindowOpen`、`Event.Combat.AttackWindow.Close`→`OnAttackWindowClose`、`Event.Combat.AttackEnd`→`OnAttackEndEvent`。
- **为什么这么写**：注释只在轻击里给出（`AddDynamic` 宏字符串化函数名），本类里的注释是 `// AddDynamic 要求函数名字面量，只能逐条写`。
- **被谁调用 / 调用谁**：被 `ActivateAbility` 调用一次。

#### `void OnAttackWindowOpen(FGameplayEventData Payload)`

- **干什么**：与轻击同类 —— 创建武器轨迹检测任务。
- **关键实现**：与 `URPG_GA_LightAttack::OnAttackWindowOpen` 几乎一致：`Module` 为空 return → 取模组默认检测配置 → `URPG_AttackWindowPayload` 且 `bOverrideTrace` 时覆盖 → `WeaponBlade` 时改用刀刃 Socket → `Projectile` 时**直接 `return`（本类不打日志，注释是 `// 远程模组的发射物逻辑属于阶段 6`）** → 旧的 `TraceTask` 先 `EndTask` → `CreateWeaponTraceTask` → 绑 `OnHit`→`OnWeaponTraceHit` → `ReadyForActivation()`。
- **为什么这么写**：同轻击。
- **被谁调用 / 调用谁**：由 `WaitGameplayEvent` 回调（事件来自蒙太奇 AnimNotify）。⚠️ 事实记录：本类**没有** `IsEventFromCurrentSegment()` 这套来源过滤（头文件里没有这个函数），三个事件回调都没有做迟到事件过滤。
- **注意**：本类不打印"判定窗口开启"的日志。

#### `void OnAttackWindowClose(FGameplayEventData Payload)`

- **干什么**：停止轨迹检测任务。
- **关键实现**：`TraceTask` 有效则 `EndTask()`、置空。
- **为什么这么写**：注释未说明（轻击里的同名函数解释了迟到事件过滤的必要性，本类没有做过滤）。
- **被谁调用 / 调用谁**：由 `WaitGameplayEvent`（`Event.Combat.AttackWindow.Close`）回调。

#### `void OnAttackEndEvent(FGameplayEventData Payload)`

- **干什么**：动画事件宣告攻击结束 → 收尾。
- **关键实现**：`FinishHeavyAttack(false);`
- **为什么这么写**：注释未说明。
- **被谁调用 / 调用谁**：由 `WaitGameplayEvent`（`Event.Combat.AttackEnd`）回调。调用 `FinishHeavyAttack`。

#### `void OnMontageCompleted()`

- **干什么**：切手技蒙太奇或释放蒙太奇自然播完 → 收尾。
- **关键实现**：`FinishHeavyAttack(false);`
- **为什么这么写**：注释未说明（头文件说明了它为什么**不能**和 `OnChargeStartMontageCompleted` 合成一个回调）。
- **被谁调用 / 调用谁**：被 `OnCompleted` 委托回调（`StartTransition` 与 `ReleaseCharge` 里绑定）。

#### `void OnChargeStartMontageCompleted()`

- **干什么**：蓄力起手动画播完 → 切到蓄力循环蒙太奇（如果配了）。
- **关键实现**：
  1. `if (bReleasing) return;` —— 注释：已经进入释放阶段，别再往上盖循环动画了；`ReleaseCharge` 会打断起手动画，那次打断同样会走到这里。
  2. `const URPG_AttackModuleData* Module = GetAttackModule(); if (!Module || !Module->HeavyAttack.ChargeLoopMontage) return;` —— 注释：没配循环动画是完全正常的做法：角色停在起手动画的最后一帧，保持一个"蓄势待发"的张力姿势；很多动作游戏就是这么做的，不循环反而更有力量感。
  3. `StopCurrentStageMontage();` —— 注释 ★：起手蒙太奇到此为止，必须先把它拆干净再播循环。不拆的话，播放循环动画会打断起手动画，起手任务的 `OnInterrupted` 立刻广播 → 被误判成"蓄力被打断"，蓄力刚摆好姿势就结束了。
  4. `PlayMontageOrSkip(Module->HeavyAttack.ChargeLoopMontage, TEXT("ChargeLoop"))` → **只绑 `OnInterrupted`**（→ `OnMontageInterrupted`）→ `ReadyForActivation()` → `TrackCurrentStageMontage(...)`。
- **为什么这么写**：不绑 `OnCompleted` 的理由（注释原文）：循环蒙太奇应该在资产里把最后一节指回自己（Section 的 Next Section 设成自身），那样它永远不会"播完"，只会被打断 —— 打断的处理已经在 `OnMontageInterrupted` 里；如果忘了配循环，`OnCompleted` 会触发一次但没人接，蒙太奇自然结束、角色停在最后一帧，效果和"没配循环动画"一样，不会出错。
- **被谁调用 / 调用谁**：被 `ChargeStart` 里绑定的 `OnCompleted` 委托回调。调用 `GetAttackModule`、`StopCurrentStageMontage`、`PlayMontageOrSkip`、`TrackCurrentStageMontage`。

#### `void OnMontageInterrupted()`

- **干什么**：蒙太奇被打断 → 以 `bWasCancelled = true` 收尾。
- **关键实现**：`if (bReleasing) return;` → 打 Verbose `"蓄力被打断"` → `FinishHeavyAttack(true);`
- **为什么这么写**：注释 ⚠️：这里必须排除"释放时打断蓄力起手动画"这种正常情况。释放阶段会播新的蒙太奇，那必然打断仍在播放的起手蒙太奇 —— 如果没有 `bReleasing` 这个判断，会把正常的蓄力→释放误判成"被打断"，于是能力在刚放出招的瞬间就被结束了。
- **被谁调用 / 调用谁**：被 `OnInterrupted` 委托回调（`StartTransition` / `StartCharge` / `ReleaseCharge` / `OnChargeStartMontageCompleted` 四处绑定）。调用 `FinishHeavyAttack`。

#### `void OnWeaponTraceHit(const TArray<FHitResult>& Hits)`

- **干什么**：逐目标施加伤害。
- **关键实现**：`for (const FHitResult& Hit : Hits) ApplyDamageToTarget(Hit.GetActor(), CurrentDamageMultiplier, &Hit);`
- **为什么这么写**：注释未说明。
- **被谁调用 / 调用谁**：被 `TraceTask->OnHit` 委托回调。调用 `ApplyDamageToTarget`。

#### `void FinishHeavyAttack(bool bWasCancelled)`

- **干什么**：统一的收尾函数，按"是否被打断"决定**停不停动画**。
- **关键实现**：
  1. `TraceTask` → `EndTask()`、置空。
  2. 两种路径（注释原文）：正常打完（`OnAttackEnd` 通知 / 蒙太奇播完）→ `DetachCurrentStageMontageTask(); CurrentStageMontage = nullptr;`，摘掉回调但**不停动画**，让最后几帧的后摇自然播完；被打断（闪避取消、切手技取消等）→ `StopCurrentStageMontage();`，连动画一起停掉，否则它的判定窗口 Notify 会在别人的动作上再结算一次伤害。
  3. `ClearTimer(ChargeTimer)`。
  4. `EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, /*bReplicateEndAbility*/ true, bWasCancelled);`
- **为什么这么写**：见上（两条路径的注释）。
- **被谁调用 / 调用谁**：被 `ActivateAbility`（提交失败 / 配置缺失）、`StartTransition`、`ReleaseCharge`、`OnAttackEndEvent`、`OnMontageCompleted`、`OnMontageInterrupted` 调用。调用 `DetachCurrentStageMontageTask`、`StopCurrentStageMontage`、`EndAbility`。

---

### `RPG_GA_Dodge`

**一句话职责**：翻滚闪避 —— 一次闪避包含三件事（头文件原文）：1. 位移（沿面朝方向施加冲量）、2. 无敌（翻滚某个窗口内免疫伤害，这是闪避的**核心价值**）、3. 代价（消耗耐力，否则玩家会无限翻滚规避一切）。

**关键配置**（构造函数里设的）：

- `SetAssetTags(FGameplayTagContainer(RPGTags::Ability_Dodge))` —— 能力身份（也是 `GA_HitReact` 的 `CancelAbilitiesWithTag` 目标）。
- `ActivationOwnedTags.AddTag(RPGTags::State_Dodging)` —— 注释：动画蓝图据此判断要不要播翻滚动作，AI 据此判断"这货正在躲，先别砍"。
- **为什么用 `ActivationOwnedTags` 而不是自己 `AddLooseGameplayTag`**（注释原文）：它的**生命周期由 GAS 托管** —— 能力结束、被打断、被取消，标签都会被自动摘掉。手写 Add/Remove 的话，一旦有某条路径忘了 Remove（比如能力被 `CancelAbilitiesWithTag` 强行取消），角色就会永久停在"闪避中"，而且这种 bug 只在特定时序下出现。
- 复制行为（注释原文）：`ActivationOwnedTags` 走 `EGameplayTagReplicationState::CountToOwner`，意味着**标签本身会复制给所有客户端**（模拟代理也看得到），只有计数只发给拥有者；对 `HasMatchingGameplayTag` 这种布尔查询没有影响。前提是工程设置 `GameplayAbilities → ReplicateActivationOwnedTags` 保持默认的开。
- **未覆盖**：`InstancingPolicy` / `NetExecutionPolicy` / `NetSecurityPolicy` / `ActivationBlockedTags`。

**为什么用 `LaunchCharacter` 而不是 RootMotion**（头文件原文）：`RootMotion` 位移与动画 100% 同步，但距离写死在动画里 —— 想调远一点就得让动画师重做动画；`LaunchCharacter` 距离是可调参数，还能按方向键微调翻滚方向，代价是位移和动画可能不完全贴合，需要手动调参。动作游戏里闪避距离是**要反复调的手感参数**，所以选后者。

**为什么无敌帧由 AnimNotifyState 控制而不是代码给固定秒数**（头文件原文）：无敌帧的起止点必须和动画的视觉表现对齐（脚离地那帧开、落地前那帧关）。

**成员变量**（全部）：

| 声明 | 含义与初值 |
| --- | --- |
| `UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="RPG|Dodge") TObjectPtr<UAnimMontage> DodgeMontage = nullptr` | 翻滚蒙太奇。留空则走模拟时序（仍会施加冲量并给无敌帧，便于验证逻辑） |
| `UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="RPG|Dodge", meta=(ClampMin="0.0")) float StaminaCost = 20.f` | 耐力消耗 |
| `UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="RPG|Dodge", meta=(ClampMin="0.0")) float DodgeImpulse = 1200.f` | 冲量强度（厘米/秒）。注释：600 约等于一次缓慢位移，1500 接近一次真正的翻滚 |
| `UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="RPG|Dodge") TSubclassOf<UGameplayEffect> InvulnerabilityEffectClass` | 无敌 GE。注释：它需要用 `TargetTags` 组件授予 `State.Invulnerable`；留空则无敌帧不生效（会有 Warning） |
| `UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="RPG|Debug", meta=(ClampMin="0.1")) float SimulatedDodgeDuration = 0.7f` | 模拟的翻滚总时长 |
| `UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="RPG|Debug", meta=(ClampMin="0.0", ClampMax="1.0")) float SimulatedInvulnerabilityRatio = 0.6f` | 模拟时无敌帧占前多少比例（0.6 = 前 60% 无敌，后 40% 有破绽） |
| `FActiveGameplayEffectHandle InvulnerabilityHandle`（private） | 当前无敌 GE 的句柄。有效表示正处于无敌中 |
| `FTimerHandle SimulatedTimer` | 模拟模式：翻滚结束的定时器 |
| `FTimerHandle SimulatedInvulnerabilityTimer` | 模拟模式：无敌帧结束的定时器（早于翻滚结束，留出破绽期） |

#### `URPG_GA_Dodge::URPG_GA_Dodge()`

- **干什么**：设置资产标签与 `State.Dodging`。
- **关键实现**：`SetAssetTags(...)` + `ActivationOwnedTags.AddTag(RPGTags::State_Dodging)`。
- **为什么这么写**：见"关键配置"。
- **被谁调用 / 调用谁**：UE 反射构造。

#### `bool CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayTagContainer* SourceTags, const FGameplayTagContainer* TargetTags, OUT FGameplayTagContainer* OptionalRelevantTags) const`

- **干什么**：激活前的耐力前置检查 —— 不够就干脆不激活。
- **关键实现**：
  1. `Super::CanActivateAbility(...)` 返回 false → 直接 false（基类会打印拒绝原因）。
  2. **用参数里的 `ActorInfo` 取属性集**：`const UAbilitySystemComponent* ASC = ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr;` → `ASC->GetSet<URPG_AttributeSet>()`。
  3. 拿不到属性集 → `return true`（注释：拿不到属性集时放行 —— 那是初始化问题，不该由闪避来背这个锅；拦下来会让"闪避按不出来"掩盖真正的初始化 bug）。
  4. `Attributes->GetStamina() < StaminaCost` → 打 Verbose `"闪避失败：耐力不足（%.1f / %.1f）"` → `return false`。
- **为什么这么写**：头文件注释 ⚠️：这里必须用参数传入的 `ActorInfo` 取属性集，不能用基类的 `GetRPGAttributeSet()` —— 后者依赖 `CurrentActorInfo`，而本函数可能在 CDO 上执行（那时 `CurrentActorInfo` 是空的）。
- **被谁调用 / 调用谁**：GAS 在激活前调用。调用 `Super::CanActivateAbility`、`ASC->GetSet<URPG_AttributeSet>`。

#### `void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)`

- **干什么**：按"扣耐力 → 建无敌监听 → 施加冲量 → 播蒙太奇（或模拟时序）"的顺序执行一次闪避。
- **关键实现**（.cpp 里的编号注释 1~4 就是顺序）：
  1. `Super::ActivateAbility(...)`；`CommitAbility` 失败 → `FinishDodge(true)`。
  2. 打 Log `"闪避（耐力 %.1f，冲量 %.0f）"`。
  3. 1. `ConsumeStamina(StaminaCost);`
  4. 2. `BindGameplayEventListeners();`（注释：由蒙太奇上的 `AnimNotifyState` 广播）。
  5. 3. `ApplyDodgeImpulse();` —— 注释：放在播蒙太奇之前，让身体和动画同时启动。
  6. 4. `UAbilityTask_PlayMontageAndWait* MontageTask = PlayMontageOrSkip(DodgeMontage, TEXT("DodgeMontage"));`
     - 非空 → 绑 `OnCompleted`→`OnMontageCompleted`、`OnInterrupted`→`OnMontageInterrupted`、`ReadyForActivation()`。
     - 为空 → 模拟时序：`World` 为空 → `FinishDodge(false)`；否则 `ApplyInvulnerability();`，再 `SetTimer(SimulatedInvulnerabilityTimer, this, &URPG_GA_Dodge::RemoveInvulnerability, SimulatedDodgeDuration * SimulatedInvulnerabilityRatio, false)`，再 `SetTimer(SimulatedTimer, this, &URPG_GA_Dodge::OnSimulatedDodgeFinished, SimulatedDodgeDuration, false)`。
  7. 模拟分支的注释（原文）：无敌帧只覆盖翻滚的前一段，之后留出破绽期 —— 全程无敌的闪避会让玩家可以无脑翻滚规避一切，战斗失去张力。（真实动画里这个边界由 `AnimNotifyState` 的位置决定，这里用比例模拟同样的时序，让无敌的开关逻辑能被验证。）
- **为什么这么写**：见上。
- **被谁调用 / 调用谁**：GAS 激活流程（`TryActivateAbilityByInputTag`）。调用 `CommitAbility`、`ConsumeStamina`、`BindGameplayEventListeners`、`ApplyDodgeImpulse`、`PlayMontageOrSkip`、`ApplyInvulnerability`、`SetTimer`、`FinishDodge`。

#### `void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)`

- **干什么**：任何结束路径的兜底清理：移除无敌 GE + 清两个模拟定时器。
- **关键实现**：`RemoveInvulnerability();` → `ClearTimer(SimulatedTimer); ClearTimer(SimulatedInvulnerabilityTimer);` → `Super::EndAbility(...)`。
- **为什么这么写**：注释原文 —— 统一清理，保证任何结束路径都不会残留无敌状态。如果无敌 GE 忘记移除，角色会变成永久无敌，而且很难查（表现为"敌人打不动我"，而不是报错）。
- **被谁调用 / 调用谁**：GAS（外部取消）与 `FinishDodge()`。调用 `RemoveInvulnerability`、`Super::EndAbility`。

#### `void BindGameplayEventListeners()`

- **干什么**：监听两个无敌帧事件。
- **关键实现**：逐条写（`AddDynamic` 要求字面量）：`Event.Character.Invulnerability.Begin` → `OnInvulnerabilityBegin`；`Event.Character.Invulnerability.End` → `OnInvulnerabilityEnd`。
- **为什么这么写**：注释 —— `AddDynamic` 要求函数名是字面量（宏内部会字符串化它），所以只能逐条写。
- **被谁调用 / 调用谁**：被 `ActivateAbility` 调用。

#### `void OnInvulnerabilityBegin(FGameplayEventData Payload)` / `void OnInvulnerabilityEnd(FGameplayEventData Payload)`

- **干什么**：分别调用 `ApplyInvulnerability()` / `RemoveInvulnerability()`。
- **关键实现**：单行转发；`Payload` 未被使用。
- **为什么这么写**：注释未说明。
- **被谁调用 / 调用谁**：由 `WaitGameplayEvent` 回调（发送方是蒙太奇上的无敌帧 `AnimNotifyState`）。

#### `void ApplyDodgeImpulse()`

- **干什么**：沿角色当前朝向施加冲量。
- **关键实现**：`ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo())`，为空则 return；`const FVector Impulse = Character->GetActorForwardVector() * DodgeImpulse;` → `Character->LaunchCharacter(Impulse, /*bXYOverride*/ true, /*bZOverride*/ false);`
- **为什么这么写**（注释原文）：`bXYOverride = true` —— 覆盖水平速度，不覆盖的话角色原本的移动速度会和冲量叠加，出现"跑动中闪避能翻特别远"的问题；`bZOverride = false` —— **不**覆盖垂直速度，保留垂直分量，这样从高处跳下时闪避不会让角色悬停在空中。
- **被谁调用 / 调用谁**：被 `ActivateAbility` 调用。

#### `void ApplyInvulnerability()`

- **干什么**：给自己挂无敌 GE，并记下句柄。
- **关键实现**：
  1. `if (InvulnerabilityHandle.IsValid()) return;` —— 注释：已经在无敌中，避免重复施加。
  2. `!InvulnerabilityEffectClass` → Warning `"没有配置 InvulnerabilityEffectClass —— 闪避不会提供无敌帧（需要在 GA 蓝图里指定 GE_Invulnerable）"` → return。
  3. ASC 为空 → return；建 `FGameplayEffectContextHandle Context = ASC->MakeEffectContext(); Context.AddSourceObject(this);` → `ASC->MakeOutgoingSpec(InvulnerabilityEffectClass, GetAbilityLevel(), Context)`；Spec 无效 → return。
  4. `InvulnerabilityHandle = ASC->ApplyGameplayEffectSpecToSelf(*SpecHandle.Data.Get());` → 打 Verbose `"无敌帧开始"`。
- **为什么这么写**：注释 —— 保存句柄，结束时靠它精确移除这一个 GE；用 `RemoveActiveGameplayEffect(Handle)` 而不是"移除所有该类 GE"，是因为将来可能有别的来源也给无敌（比如某个技能），按句柄移除不会误伤别人给的。
- **被谁调用 / 调用谁**：被 `ActivateAbility`（模拟分支）与 `OnInvulnerabilityBegin` 调用。

#### `void RemoveInvulnerability()`

- **干什么**：按句柄移除无敌 GE。
- **关键实现**：`InvulnerabilityHandle` 无效 → return；否则 `ASC->RemoveActiveGameplayEffect(InvulnerabilityHandle)` → `InvulnerabilityHandle.Invalidate();` → 打 Verbose `"无敌帧结束"`。
- **为什么这么写**：注释未说明（理由在 `ApplyInvulnerability` 的句柄说明里）。
- **被谁调用 / 调用谁**：被 `EndAbility`、`FinishDodge`、`OnInvulnerabilityEnd` 以及模拟模式的 `SimulatedInvulnerabilityTimer` 定时器调用（直接作为定时器回调）。

#### `void FinishDodge(bool bWasCancelled)`

- **干什么**：收尾：清无敌 + 清定时器 + 结束能力。
- **关键实现**：`RemoveInvulnerability();` → `ClearTimer(SimulatedTimer); ClearTimer(SimulatedInvulnerabilityTimer);` → `EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, /*bReplicateEndAbility*/ true, bWasCancelled);`
- **为什么这么写**：注释未说明（与 `EndAbility` 里的清理重复，属冗余兜底）。
- **被谁调用 / 调用谁**：被 `ActivateAbility`、`OnSimulatedDodgeFinished`、`OnMontageCompleted`、`OnMontageInterrupted` 调用。

#### `void OnSimulatedDodgeFinished()`

- **干什么**：模拟模式的翻滚结束 → 收尾。
- **关键实现**：`FinishDodge(false);`
- **为什么这么写**：注释 —— 模拟模式：翻滚结束时自动收尾（没有动画事件来触发）。
- **被谁调用 / 调用谁**：由 `SimulatedTimer` 定时器回调。

#### `void OnMontageCompleted()`

- **干什么**：翻滚蒙太奇播完 → `FinishDodge(false);`
- **关键实现 / 为什么这么写**：单行；注释未说明。

#### `void OnMontageInterrupted()`

- **干什么**：翻滚蒙太奇被打断 → `FinishDodge(true);`
- **关键实现**：单行。
- **为什么这么写**：注释 —— 被打断时也要走到 `FinishDodge → EndAbility → RemoveInvulnerability`，否则会残留无敌状态。
- **被谁调用 / 调用谁**：被 `OnInterrupted` 委托回调。

---

### `RPG_GA_HitReact`

**一句话职责**：挨打时的踉跄动作（硬直）—— 由 `Event.Combat.Hit` 事件声明式触发，打断当前攻击/闪避，播受击蒙太奇，播完自动结束硬直。

**它是怎么被触发的（头文件原文，全程没有一个调用点）**：

```
RPG_DamageExecution 算出伤害
  → GE_Damage 写 IncomingDamage
    → RPG_AttributeSet::PostGameplayEffectExecute（服务器）
      → SendGameplayEventToActor(Event.Combat.Hit)
        → ASC 查 AbilityTriggers 表
          → 本能力
```

连接点是**标签**而不是函数调用：能力的构造函数里声明"我监听 `Event.Combat.Hit`"，剩下的由 GAS 自己接。好处是伤害逻辑完全不需要知道"有受击反应这回事"—— 将来加"格挡反击""霸体免硬直"都是加新能力或改标签，不用回头动伤害链路。

**关键配置**（构造函数里设的）：

- `InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;` —— 与基类默认一致，这里**显式写出**。
- `NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;` —— 头文件理由：事件本身只在服务器广播（见 `RPG_AttributeSet.cpp` 里的权威判断），所以客户端根本收不到触发。设成 `ServerOnly` 有两个作用：明确表达意图（读代码的人不用去追"为什么客户端不播"），以及顺手堵死"客户端伪造受击事件让敌人硬直"这条路。客户端看到的受击表现来自复制（`RepAnimMontageInfo` 和 `State.Hit` 标签都会同步过去）。
- `bRetriggerInstancedAbility = true;` —— ⭐ 允许重入。注释原文：默认值 `false` 时，能力激活期间再来一次激活请求会被直接拒绝（`AbilitySystemComponent_Abilities.cpp:1848` "Can't activate instanced per actor ability ... already a currently active instance"）。表现上就是：被打第一下会踉跄，紧接着被打第二下**完全没反应**（敌人连击时特别明显，而且不报错）。打开之后引擎会先 `EndAbility(bWasCancelled=false)` 旧实例再重新激活（同文件 `:1836-1844`），所以 `EndAbility` 里必须把旧任务清理干净。
- `CancelAbilitiesWithTag.AddTag(RPGTags::Ability_Attack);` + `CancelAbilitiesWithTag.AddTag(RPGTags::Ability_Dodge);` —— 注释：用 `Ability.Attack` **父标签**一次覆盖轻击和重击，加新攻击类型不用改这里；闪避虽然大部分时间无敌（那时压根不会走到受击），但无敌帧结束后的后摇阶段是能被抓的，那时候应该被打断。头文件补充："被打断"就是动作游戏里受击的核心意义 —— 否则玩家可以顶着伤害把连段打完，战斗就没有博弈可言。⚠️ 被打断的攻击由它自己的 `EndAbility` 负责收尾（停蒙太奇、摘标签），**本能力不碰别人的动画** —— 这条边界很重要，互相清理一定会漏。
- `ActivationOwnedTags.AddTag(RPGTags::State_Hit);` —— 注释：`State.Hit` 在能力激活期间由 GAS 自动挂上/摘掉；动画蓝图可以据此整体切到"受击"姿态，AI 也可以用它判断目标在挨打。
- `AbilityTriggers`（★ 声明式触发）：构造 `FAbilityTriggerData TriggerData; TriggerData.TriggerTag = RPGTags::Event_Combat_Hit; TriggerData.TriggerSource = EGameplayAbilityTriggerSource::GameplayEvent; AbilityTriggers.Add(TriggerData);` 注释原文（⚠️ 少了这一段的后果是"整个功能是死代码"）：事件触发的 GA 不是在每次收到事件时去遍历所有能力找"谁想接"，而是在**授予能力时**就把能接事件的能力登记进一张表（`OnGiveAbility → RegisterAbilityTriggers(Spec, AbilityTriggers)`，`AbilitySystemComponent_Abilities.cpp:578`），之后 `HandleGameplayEvent` 只遍历这张表（同文件 `:2571`）。也就是说：**没有 `AbilityTriggers` 的能力，事件系统根本看不见它** —— 它会被正常授予、正常出现在 `ActivatableAbilities` 里、日志一切正常，但永远等不到触发。不报错，只是"挨打了没反应"。
- **未覆盖**：`NetSecurityPolicy`（= `ClientOrServer`）、`ActivationBlockedTags`（= `State.Dead`）。没有 `SetAssetTags`（本类不设资产标签）。

**成员变量**（全部）：

| 声明 | 含义与初值 |
| --- | --- |
| `UPROPERTY() TObjectPtr<UAbilityTask_PlayMontageAndWait> MontageTask` | 受击蒙太奇任务 |
| `FTimerHandle SimulatedTimer` | 没配蒙太奇时的硬直定时器 |
| `UPROPERTY(EditDefaultsOnly, Category="RPG|HitReact|Debug", meta=(ClampMin="0.05")) float SimulatedHitDuration = 0.4f` | 角色没配受击蒙太奇时的硬直时长（秒）。注释：存在的意义和攻击能力的"模拟命中检测"一样 —— 让"挨打会被打断"这条逻辑在动画做好之前就能验证；配了蒙太奇时这个值不参与。⚠️ 头文件特别注明：**没有 `BlueprintReadOnly`** —— 它是 private 成员，UHT 不允许在 private 上标蓝图可读（会直接编译失败） |

#### `URPG_GA_HitReact::URPG_GA_HitReact()`

- **干什么**：见上面六条关键配置。
- **关键实现**：按顺序设置 `InstancingPolicy`、`NetExecutionPolicy`、`bRetriggerInstancedAbility`、两次 `CancelAbilitiesWithTag.AddTag`、`ActivationOwnedTags.AddTag`，最后装配 `AbilityTriggers`。
- **为什么这么写**：见上（每条都有注释理由）。
- **被谁调用 / 调用谁**：UE 反射构造。

#### `void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)`

- **干什么**：清理旧任务 → 从角色上挑一个受击蒙太奇播放（没配则模拟一段硬直）。
- **关键实现**：
  1. `Super::ActivateAbility(...)`；`CommitAbility` 失败 → `EndAbility(Handle, ActorInfo, ActivationInfo, /*bReplicateEndAbility=*/true, /*bWasCancelled=*/true);` → return（**注意：直接调 `EndAbility`，不走 `FinishHitReact`**）。
  2. `DetachMontageTask();` —— 注释：旧任务必须先摘干净再建新任务，否则它的回调会冒充这一次的结果（详见 `DetachMontageTask` 的说明）。
  3. `ARPG_BaseCharacter* Character = GetRPGCharacter(); UAnimMontage* Montage = Character ? Character->PickHitReactMontage() : nullptr;`
  4. 有蒙太奇 → `const float Rate = Character->GetHitReactPlayRate();` → `MontageTask = PlayMontageOrSkip(Montage, TEXT("HitReactMontage"), Rate);` → 任务有效则绑 `OnCompleted`→`OnMontageCompleted`、`OnInterrupted`→`OnMontageInterrupted`、`OnCancelled`→`OnMontageInterrupted`（**同一个回调绑了两条委托**）、`ReadyForActivation()`，打 Verbose `"受击反应：播放 %s（%.2fx）"`，然后 `return`。
     - 注释：播放速率从角色上读 —— 同一个 GA 挂在玩家和敌人身上时，"这个角色挨打是什么节奏"是角色的属性，不是能力的属性。
  5. 没配蒙太奇 → `StartSimulatedHitReact();` —— 注释：用定时器模拟一段硬直，这样"挨打会被打断、会有一小段不能动"这条逻辑不会因为美术没做动画而失效。
- **为什么这么写**：见上。
- **被谁调用 / 调用谁**：GAS 事件触发流程（`HandleGameplayEvent` 命中 `AbilityTriggers` 里的 `Event.Combat.Hit`）。调用 `CommitAbility`、`EndAbility`、`DetachMontageTask`、`GetRPGCharacter`、`Character->PickHitReactMontage`、`Character->GetHitReactPlayRate`、`PlayMontageOrSkip`、`StartSimulatedHitReact`。

#### `void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)`

- **干什么**：摘任务回调、清硬直定时器、调父类。
- **关键实现**：`DetachMontageTask();` → `ClearTimer(SimulatedTimer);` → `Super::EndAbility(...)`。
- **为什么这么写**：注释 ⚠️ —— 顺序：先清任务再调 `Super`。`Super::EndAbility` 会摘掉 `ActivationOwnedTags` 并广播结束事件；那之后我们还去动任务是安全的，但保持"自己申请的资源自己先释放"这个顺序，读起来更不容易出错。另一条注释：最后一段动画故意留着播完 —— 受击被打断（比如又挨了一下）时，新的一次激活会立刻播新蒙太奇把它顶掉，不需要在这里停。
- **被谁调用 / 调用谁**：GAS（`bRetriggerInstancedAbility` 触发的旧实例结束、`CancelAllAbilities`）与 `FinishHitReact()`。调用 `DetachMontageTask`、`Super::EndAbility`。

#### `void DetachMontageTask()`

- **干什么**：摘掉三个委托绑定并结束任务、置空。
- **关键实现**：为空 return；`OnCompleted.RemoveDynamic(this, &OnMontageCompleted)`、`OnInterrupted.RemoveDynamic(this, &OnMontageInterrupted)`、`OnCancelled.RemoveDynamic(this, &OnMontageInterrupted)`；`EndTask()`；`MontageTask = nullptr;`
- **为什么这么写**：头文件 ⚠️ 原文 —— 只调 `EndTask()` 是不够的：`ShouldBroadcastAbilityTaskDelegates()` 判断的是**能力**是否激活（`AbilityTask.cpp:199`），不是任务自己的状态 —— 所以"结束掉的任务"照样会把 `OnCompleted` 回调送过来。重生时的表现就是：上一次受击的收尾回调把这一次刚播的蒙太奇当成"播完了"，硬直莫名其妙提前结束。**必须先 `RemoveDynamic` 再 `EndTask`，两件事缺一不可**（这个坑本项目在轻击连段上已经踩过一次，见 `RPG_GA_LightAttack`）。
- **被谁调用 / 调用谁**：被 `ActivateAbility`、`EndAbility` 调用。

#### `void OnMontageCompleted()` / `void OnMontageInterrupted()`

- **干什么**：分别以 `false` / `true` 调 `FinishHitReact`。
- **关键实现**：各一行转发。
- **为什么这么写**：头文件对 `OnMontageInterrupted` 的说明 —— 有两种情况会走到这里：又挨了一下（`bRetriggerInstancedAbility` 会 `EndAbility` 旧实例后再激活）、角色死了（`GA_Death` 的 `CancelAllAbilities` 把它扫掉）；两种都不该在本能力里做额外补偿 —— 该做的事各自的上游已经在做了。
- **被谁调用 / 调用谁**：被 `MontageTask` 的 `OnCompleted` / `OnInterrupted` / `OnCancelled` 委托回调（`OnCancelled` 也绑到 `OnMontageInterrupted`）。调用 `FinishHitReact`。

#### `void FinishHitReact(bool bWasCancelled)`

- **干什么**：硬直结束 → 结束能力。
- **关键实现**：`if (!IsActive()) return;` → `EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, /*bReplicateEndAbility=*/true, bWasCancelled);`
- **为什么这么写**：`IsActive()` 守卫的注释未说明。
- **被谁调用 / 调用谁**：被 `OnMontageCompleted`、`OnMontageInterrupted`、`StartSimulatedHitReact`（World 为空时）、`OnSimulatedHitReactFinished` 调用。

#### `void StartSimulatedHitReact()`

- **干什么**：没配蒙太奇时用定时器模拟一段硬直。
- **关键实现**：`World` 为空 → `FinishHitReact(false)`；打 Verbose `"角色没有配置受击蒙太奇，改用 %.2f 秒的模拟硬直"`；`SetTimer(SimulatedTimer, this, &URPG_GA_HitReact::OnSimulatedHitReactFinished, SimulatedHitDuration, false)`。
- **为什么这么写**：见 `SimulatedHitDuration` 的注释。
- **被谁调用 / 调用谁**：被 `ActivateAbility` 调用。

#### `void OnSimulatedHitReactFinished()`

- **干什么**：模拟硬直结束 → `FinishHitReact(/*bWasCancelled=*/false);`
- **关键实现 / 为什么这么写**：单行；注释未说明。

---

### `RPG_GA_Death`

**一句话职责**：整个死亡流程的**编排者**（头文件原话）。

**死亡的五个步骤，顺序不能乱**（头文件原文）：

1. 挂 `State.Dead` ← 必须最先做
2. 取消所有其它能力 ← 清理 `State.Attacking` / `State.Invulnerable` 等标签
3. 通知角色（停 AI / 停输入）
4. 播死亡蒙太奇
5. 蒙太奇播完 → 进布娃娃 → 启动重生倒计时

**为什么 ① 必须排在 ④ 前面**（头文件原文）：从"血量归零"到"死亡蒙太奇播完"之间有 1~2 秒，这段时间里角色**还是活的** —— 玩家还能按攻击键，敌人 AI 还能发起攻击。如果等蒙太奇播完才挂标签，玩家会看到一具尸体在打拳。挂上 `State.Dead` 之后，基类的 `ActivationBlockedTags`（见 `RPG_GameplayAbilityBase` 构造函数）会挡掉**所有**新的能力激活 —— 一条声明就够。而且这一步还顺带解决了"死两次"的问题：`State.Dead` 挂着时第二次死亡事件的触发会被同一个标签挡在门外，不会重播死亡动画。

**为什么 `State.Dead` 用 loose tag 而不是 GE**（头文件原文）：因为复活的流程里有一步是"清掉身上所有 GE"（`RemoveActiveEffects`）—— 如果死亡标记是个 GE，它会被那一步顺手清掉，复活逻辑就得反过来依赖"我清掉了什么"来推断状态，非常绕。用 loose tag 就没这个问题：**它不在 GE 的生命周期里，只能被显式增删**，复活时摘掉它是一句明确的代码，不依赖任何副作用。用 `CountToOwner` 复制状态是因为：`State.Dead` 必须复制到**每个**客户端，否则会出现"服务器上人已经倒了，客户端看他还在站着"。

**这个能力怎么被授予**（头文件原文）：放进角色的 **`StartupPassiveAbilities`** 里。名字叫"被动"但 `bActivateOnGranted` 保持 `false` —— 它被授予后只是**待命**，等 `Event.Combat.Death` 事件来触发（`AbilityTriggers` 声明的那条）。刻意不给它输入标签：它不是"玩家想做什么"，而是"发生了什么事"。

**关键配置**（构造函数里设的）：

- `InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;` —— 与基类默认一致，显式写出。
- `NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;` —— 注释：死亡只在服务器判定 —— 血归零、事件广播、AI 停止、重生计时全都是服务器的事；客户端靠复制看到结果。
- `bRetriggerInstancedAbility = false;` —— 注释：不重入。挂上 `State.Dead` 之后新的触发本来就会被 `ActivationBlockedTags` 挡住，这里再显式写一次，是为了让"死亡只能发生一次"这条规则在代码里可见。
- `AbilityTriggers`（★）：`TriggerData.TriggerTag = RPGTags::Event_Combat_Death; TriggerData.TriggerSource = EGameplayAbilityTriggerSource::GameplayEvent; AbilityTriggers.Add(TriggerData);` 注释原文：这一句等价于说"当 `Event.Combat.Death` 事件到达时，激活我"；之后不需要任何地方去 `Cast` 出 `GA_Death` 并手动激活 —— 属性集只管广播事件，谁关心谁自己声明。对比另一种写法（属性集里直接调用角色的死亡函数）：那样属性集就要知道"角色有死亡表现"这件事，分层被打破；加"死亡时掉装备""死亡时给击杀者经验"都得回头改属性集；而声明式触发下，这些各自是独立的能力或监听者，互不知道对方存在。
- ⚠️ **这里不要动 `ActivationBlockedTags`**（注释原文）：基类已经加了 `State.Dead` —— 那正是我们要的：`State.Dead` 一挂上，第二次死亡触发就再也进不来，不用额外写"防重复死亡"的代码。
- **未覆盖 / 未设置**：`NetSecurityPolicy`（= `ClientOrServer`）；**没有 `SetAssetTags`、没有 `ActivationOwnedTags`、没有 `CancelAbilitiesWithTag`**（取消别人是靠 `CancelAllAbilities` 显式做的）。⚠️ 事实记录：`RPGTags::Ability_Death`（`"Ability.Death"`）这个标签在 `RPG_GameplayTags.h/.cpp` 里有声明与定义，但**全库没有任何代码引用它**（grep 只命中声明与定义两处）。

**成员变量**（全部）：

| 声明 | 含义与初值 |
| --- | --- |
| `UPROPERTY() TObjectPtr<UAbilityTask_PlayMontageAndWait> MontageTask` | 死亡蒙太奇任务 |
| `bool bDeathFinished = false` | 保证 `FinishDeath` 只执行一次（注释：因为"蒙太奇播完""被打断""没配蒙太奇"三条路径最终都会走到这里，而没有蒙太奇时后者会立刻执行） |

#### `URPG_GA_Death::URPG_GA_Death()`

- **干什么**：见上面五条关键配置。
- **关键实现**：`InstancingPolicy` → `NetExecutionPolicy` → `bRetriggerInstancedAbility = false` → 装配 `AbilityTriggers`（`Event.Combat.Death`）。构造函数末尾的注释明确写着不要动 `ActivationBlockedTags`。
- **为什么这么写**：见上。
- **被谁调用 / 调用谁**：UE 反射构造。

#### `void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)`

- **干什么**：按"① 挂 `State.Dead` → ② 取消所有能力 → ③ 通知角色 → ④ 播死亡蒙太奇（或直接倒地）"执行。
- **关键实现**：
  1. `Super::ActivateAbility(...)`；`bDeathFinished = false;`
  2. `Character = GetRPGCharacter()`、`ASC = GetAbilitySystemComponentFromActorInfo()`；任一为空 → `UE_LOG(LogRPG_Combat, Error, "[%s] 死亡能力拿不到角色或 ASC，死亡流程中止（角色会停在血量为 0 但还活着的状态）")` → `EndAbility(Handle, ActorInfo, ActivationInfo, true, true)` → return。
  3. `CommitAbility` 失败 → `EndAbility(..., true, true)` → return。
  4. **①** `ASC->AddLooseGameplayTags(FGameplayTagContainer(RPGTags::State_Dead), /*Count=*/1, EGameplayTagReplicationState::CountToOwner);` —— 注释：从这一行开始，角色的所有能力激活都会被基类的 `ActivationBlockedTags` 挡掉；`CountToOwner` 的语义是标签本身复制给所有人（客户端才知道这个人死了），只有计数只发给拥有者。
  5. **②** `ASC->CancelAllAbilities(this);` —— 注释：目的是**清理它们挂着的 `ActivationOwnedTags`**：不停掉的话，一个"死在攻击中"的角色会一直带着 `State.Attacking`，动画蓝图（每帧查 ASC 标签）会让他保持战斗姿态，而不是瘫下去。用 `CancelAllAbilities` 而不是 `CancelAbilitiesWithTag`：前者把耐力恢复、疾跑、Buff 之类**全部**收掉，语义就是"人都死了"；后者要枚举标签，漏一个就是一个不显眼的残留状态。传 `this` 是为了把自己排除在外 —— 否则会取消自己，后面的蒙太奇就没得播了。
  6. **③** `Character->OnDeathStarted();` —— 注释：通知角色层：敌人的 AI 在这里停大脑，玩家在这里停输入。
  7. **④** `UAnimMontage* Montage = Character->GetDeathMontage();` 若有效 → `MontageTask = PlayMontageOrSkip(Montage, TEXT("DeathMontage"), Character->GetDeathMontagePlayRate());` → 任务有效则绑 `OnCompleted`→`OnMontageCompleted`、`OnInterrupted`→`OnMontageInterrupted`、`OnCancelled`→`OnMontageInterrupted`、`ReadyForActivation()`，然后 `return`。
  8. 没配死亡蒙太奇 → 打 Verbose `"角色没有配置死亡蒙太奇，直接进入布娃娃"` → `FinishDeath();` —— 注释：这不是错误配置：测试布娃娃链路时最快的方式就是留空，表现上是"人直接瘫下去"，反而比等一段动画更省事。
- **为什么这么写**：见上（每一步的理由都来自注释）。
- **被谁调用 / 调用谁**：GAS 事件触发流程（`Event.Combat.Death` 命中 `AbilityTriggers`；事件的发送方是 `RPG_AttributeSet` 在服务器上的伤害结算，见头文件的五步链路）。调用 `CommitAbility`、`AddLooseGameplayTags`、`CancelAllAbilities`、`Character->OnDeathStarted`、`Character->GetDeathMontage`、`Character->GetDeathMontagePlayRate`、`PlayMontageOrSkip`、`FinishDeath`、`EndAbility`。

#### `void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)`

- **干什么**：摘任务回调 → 调父类。
- **关键实现**：`DetachMontageTask(); Super::EndAbility(...);`
- **为什么这么写**：注释未说明（理由同 `RPG_GA_HitReact::DetachMontageTask` 的引用）。
- **被谁调用 / 调用谁**：GAS 与 `FinishDeath()`。

#### `void DetachMontageTask()`

- **干什么**：摘掉三个委托绑定并结束任务、置空。
- **关键实现**：为空 return；`OnCompleted.RemoveDynamic(this, &OnMontageCompleted)`、`OnInterrupted.RemoveDynamic(this, &OnMontageInterrupted)`、`OnCancelled.RemoveDynamic(this, &OnMontageInterrupted)`；`EndTask()`；置空。
- **为什么这么写**：注释 —— 先 `RemoveDynamic` 再 `EndTask`：只 `EndTask` 拦不住迟到的回调，理由见 `RPG_GA_HitReact::DetachMontageTask` 的注释。
- **被谁调用 / 调用谁**：被 `EndAbility` 调用。

#### `void OnMontageCompleted()` / `void OnMontageInterrupted()`

- **干什么**：都调用 `FinishDeath()`。
- **关键实现**：各一行。
- **为什么这么写**：`OnMontageInterrupted` 的注释 —— 死亡动画被打断也要倒地，躺下这件事不该依赖动画播完。（正常情况下没人能打断它：`State.Dead` 挡掉了所有新能力，而 `CancelAllAbilities` 又跳过了自己。这里是兜底。）
- **被谁调用 / 调用谁**：被 `MontageTask` 的三条委托回调（`OnCancelled` 也绑 `OnMontageInterrupted`）。调用 `FinishDeath`。

#### `void FinishDeath()`

- **干什么**：死亡流程的终点：进布娃娃 + 启动重生倒计时 + 结束能力。
- **关键实现**：
  1. `if (bDeathFinished) return; bDeathFinished = true;`
  2. `Character = GetRPGCharacter()` 有效则：⑤ `Character->EnterRagdoll();` —— 注释：交给物理，角色失去控制，网格由 Chaos 模拟；物理状态本身不复制，各端自己算（见 `RPG_BaseCharacter::OnRep_RagdollEnabled`）。然后 `Character->StartRespawnCountdown();` —— 注释：该不该重生由角色上的 `RespawnDelay` 决定（玩家通常配 3~5 秒，敌人配 0，死了就躺着）。
  3. `EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, /*bReplicateEndAbility=*/true, /*bWasCancelled=*/false);` —— 注释 ⚠️：**不能**在这里摘 `State.Dead` —— 那个标签就是"这个人死了"的定义，要一直挂到复活时才摘。
- **为什么这么写**：见上。
- **被谁调用 / 调用谁**：被 `ActivateAbility`（没配蒙太奇）、`OnMontageCompleted`、`OnMontageInterrupted` 调用。调用 `GetRPGCharacter`、`EnterRagdoll`、`StartRespawnCountdown`、`EndAbility`。

---

### `RPG_GA_Jump`

**一句话职责**：跳跃 —— 一次性消耗耐力，支持可变高度（短按跳得低、长按跳得高）。

**为什么跳跃也要做成 GA**（头文件原文）：单看"往上跳一下"，调用 `ACharacter::Jump()` 就够了。但跳跃有两个 GAS 该管的特征：**消耗耐力**（它和闪避、攻击竞争同一份资源，构成"跳一次等于花掉半次闪避"这样的战术取舍）、**可被规则限制**（将来要做"空中只能跳一次""受击时禁止跳跃"，有 GA 才有地方挂这些条件）。做成 GA 之后，这些规则都变成能力上的标签与配置，不会散落到输入层。

**为什么激活后不立刻结束**（头文件原文）：因为要实现**可变高度跳跃**，这需要"松手时调 `StopJumping()`"，也就要求能力在滞空期间保持激活。代价是跳跃期间角色身上会挂着一个激活中的能力 —— 这没有害处：攻击、闪避都能正常激活（它们没有把这些标签设为阻断条件）。用 `MaxJumpHoldTime` 超时兜底，避免玩家一直不松手导致能力永久激活。

**关键配置**（构造函数里设的）：

- `SetAssetTags(FGameplayTagContainer(RPGTags::Ability_Jump));` —— 只有这一行。
- **未设置**：`ActivationOwnedTags`（空，本类不挂任何状态标签）、`CancelAbilitiesWithTag`、`AbilityTriggers`；`InstancingPolicy` / `NetExecutionPolicy` / `NetSecurityPolicy` / `ActivationBlockedTags` 全部继承基类。

**成员变量**（全部）：

| 声明 | 含义与初值 |
| --- | --- |
| `UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="RPG|Jump", meta=(ClampMin="0.0")) float JumpStaminaCost = 10.f` | 跳跃的耐力消耗 |
| `UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="RPG|Jump", meta=(ClampMin="0.1")) float MaxJumpHoldTime = 1.5f` | 最长按住时间（秒）。超过就强制结束能力。注释：防止玩家一直不松手导致能力永久激活、永远收不到清理 |
| `FTimerHandle MaxJumpHoldTimer` | 超时兜底定时器 |

#### `URPG_GA_Jump::URPG_GA_Jump()`

- **干什么**：设置资产标签 `Ability.Jump`。
- **关键实现**：一行。
- **为什么这么写**：注释未说明。
- **被谁调用 / 调用谁**：UE 反射构造。

#### `bool CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayTagContainer* SourceTags, const FGameplayTagContainer* TargetTags, OUT FGameplayTagContainer* OptionalRelevantTags) const`

- **干什么**：检查两件事：耐力够不够、角色能不能跳（在不在空中）。
- **关键实现**：
  1. `Super::CanActivateAbility(...)` 为 false → false。
  2. ⚠️ 一律走参数里的 `ActorInfo`：`ASC = ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr;` → `Attributes = ASC ? ASC->GetSet<URPG_AttributeSet>() : nullptr;`
  3. 耐力检查：`if (Attributes && Attributes->GetStamina() < JumpStaminaCost)` → Verbose `"跳跃失败：耐力不足（%.1f / %.1f）"` → `return false;`（注意：拿不到属性集时**不拦截**，与基类 `HasEnoughStamina` 的放行策略一致）。
  4. 能不能跳：`const AActor* Avatar = ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr;` → `Cast<ACharacter>(Avatar)` → `!Character->CanJump()` → `return false;`
- **为什么这么写**：头文件注释 ⚠️：全部走参数里的 `ActorInfo`，不能用 `CurrentActorInfo` 系的便捷函数 —— 本函数可能在 CDO 上执行（详见 `RPG_GameplayAbilityBase.cpp` 的说明）。能否起跳这条的注释：`ACharacter::CanJump()` 会检查"是否在地面上、能否起跳"；不检查的话，玩家在空中按跳跃会白白消耗耐力。
- **被谁调用 / 调用谁**：GAS 激活前调用。调用 `Super::CanActivateAbility`、`ASC->GetSet<URPG_AttributeSet>`、`ACharacter::CanJump`。

#### `void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)`

- **干什么**：扣耐力 → `Jump()` → 起一个超时定时器 → **刻意不立即结束能力**。
- **关键实现**：
  1. `Super::ActivateAbility(...)`；`CommitAbility` 失败 → `EndAbility(..., true, true)` → return。
  2. `ConsumeStamina(JumpStaminaCost);`
  3. `if (ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo())) Character->Jump();` —— 注释：`ACharacter::Jump()` 内部会再做一次 `CanJump` 检查，还会处理"蹲伏中跳跃自动起立"这类细节，不需要我们重复实现。
  4. 打 Log `"跳跃（耐力 %.1f，最长按住 %.1f 秒）"`。
  5. `SetTimer(MaxJumpHoldTimer, this, &URPG_GA_Jump::FinishJump, MaxJumpHoldTime, false)`（非循环）—— 注释 ⚠️：刻意**不立即结束**能力 —— 要等玩家松手来实现可变高度跳跃；用超时兜底，防止一直不松手导致能力永久激活。
- **为什么这么写**：见上。
- **被谁调用 / 调用谁**：GAS 激活流程。调用 `CommitAbility`、`ConsumeStamina`、`ACharacter::Jump`、`SetTimer`。

#### `void OnInputReleased()`

- **干什么**：松手 → 停止上升 + 结束能力。
- **关键实现**：`if (ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo())) Character->StopJumping();` → `FinishJump();`
- **为什么这么写**：注释 —— 这是平台跳跃手感的基础：短按跳得低、长按跳得高。对 ARPG 里"跳跃接闪避/接攻击"的衔接也有帮助 —— 玩家可以通过短按快速落地来抢时间。
- **被谁调用 / 调用谁**：由 `URPG_AbilitySystemComponent::NotifyInputReleased(InputTag)` 在能力激活期间调用。调用 `ACharacter::StopJumping`、`FinishJump`。

#### `void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)`

- **干什么**：清超时定时器 → 调父类。
- **关键实现**：`ClearTimer(MaxJumpHoldTimer)` → `Super::EndAbility(...)`。
- **为什么这么写**：注释未说明。
- **被谁调用 / 调用谁**：GAS 与 `FinishJump()`。

#### `void FinishJump()`

- **干什么**：结束跳跃能力。
- **关键实现**：`if (!IsActive()) return;` → `EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, /*bReplicateEndAbility*/ true, /*bWasCancelled*/ false);`
- **为什么这么写**：`IsActive()` 守卫的注释未说明（因为松手与超时定时器可能都触发一次，这个守卫用来防重入）。
- **被谁调用 / 调用谁**：被 `OnInputReleased` 与 `MaxJumpHoldTimer` 定时器调用。

---

### `RPG_GA_Sprint`

**一句话职责**：奔跑（按住型，持续消耗耐力）—— 激活时切角色移动速度并起一个循环定时器按速率扣耐力；松手、耐力耗尽、被取消都会恢复速度。

**为什么奔跑要走 GAS，而走路不走**（头文件原文）：走路是"没有代价的移动"—— 它不该被任何东西阻断，也不该消耗任何资源。而奔跑有两个 GAS 该管的特征：**持续消耗耐力**（需要跟属性系统打交道）、**耐力耗尽要自动中断**（需要感知属性变化并做出决策）。这两件事放在 `PlayerController` 里手写也能跑，但那样耐力规则就散落在输入层了；放进 GA 之后，"什么行为消耗多少耐力"全部集中在能力层。

**为什么持续消耗用定时器而不是周期 GE**（头文件原文，原文要点）：更"GAS 原生"的做法是挂一个 Duration + Period 的 `GE_StaminaDrain`，这里选了定时器 + 复用 `ConsumeStamina`，原因是：`ConsumeStamina` 已经处理了"刷新恢复阻断"，走它才能保证奔跑期间耐力不会偷偷恢复；周期 GE 的 `SetByCaller` 速率配置比较绕，多一个容易配错的资产；0.1 秒一次的 GE 应用开销在这个量级下可以忽略。将来如果要做"奔跑速度随耐力衰减"这类复杂效果，再换成周期 GE 更合适。

**关键配置**（构造函数里设的）：

- `SetAssetTags(FGameplayTagContainer(RPGTags::Ability_Sprint));`
- `ActivationOwnedTags.AddTag(RPGTags::State_Sprinting);` —— 标记"正在奔跑"，供动画和 AI 查询。（`State.Sprinting` 的标签定义处还注明：玩家的冲刺和 AI 追击时切到的战斗速度都挂这个标签，动画蓝图靠它决定用哪条移动状态机。）
- **未设置**：`AbilityTriggers`、`CancelAbilitiesWithTag`；`InstancingPolicy` / `NetExecutionPolicy` / `NetSecurityPolicy` / `ActivationBlockedTags` 继承基类。

**成员变量**（全部）：

| 声明 | 含义与初值 |
| --- | --- |
| `UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="RPG|Sprint", meta=(ClampMin="0.0")) float StaminaDrainPerSecond = 5.f` | 每秒消耗的耐力 |
| `UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="RPG|Sprint", meta=(ClampMin="0.02")) float DrainTickInterval = 0.1f` | 扣耐力的检查间隔（秒）。注释：越密越平滑，但 GE 应用也越频繁 |
| `FTimerHandle DrainTimer` | 扣耐力的循环定时器 |

⚠️ 头文件里专门有一段注释提醒 UHT 约束（原文）：带 `BlueprintReadOnly` 的 `UPROPERTY` 必须放在 `protected`/`public` —— UHT 不允许 private 成员暴露给蓝图；这个错误在本项目里犯过两次，记住：**配置项放 protected，纯内部状态放 private**。

#### `URPG_GA_Sprint::URPG_GA_Sprint()`

- **干什么**：设置资产标签 + `State.Sprinting`。
- **关键实现**：两行。
- **为什么这么写**：见上。
- **被谁调用 / 调用谁**：UE 反射构造。

#### `void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)`

- **干什么**：检查耐力 → 切移动速度 → 启动循环扣耐力的定时器。
- **关键实现**：
  1. `Super::ActivateAbility(...)`；`CommitAbility` 失败 → `EndAbility(..., true, true)` → return。
  2. `const URPG_AttributeSet* Attributes = GetRPGAttributeSet();` 若 `Attributes && Attributes->GetStamina() <= 0.f` → Verbose `"耐力不足，无法奔跑"` → `EndAbility(..., true, true)` → return。注释：耐力已经见底时不进入奔跑 —— 直接失败比"跑一帧就停"体验更好。
  3. `World` 为空 → `EndAbility(..., true, true)` → return。
  4. `if (ARPG_BaseCharacter* Character = GetRPGCharacter()) Character->StartSprint();`
  5. 打 Log `"开始奔跑（每秒耗耐力 %.1f）"`。
  6. `SetTimer(DrainTimer, this, &URPG_GA_Sprint::TickStaminaDrain, DrainTickInterval, /*bLoop*/ true);`
- **为什么这么写**：见上（第 2 步的理由来自注释）。
- **被谁调用 / 调用谁**：GAS 激活流程。调用 `CommitAbility`、`EndAbility`、`GetRPGAttributeSet`、`GetRPGCharacter`、`Character->StartSprint`、`SetTimer`。

#### `void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)`

- **干什么**：统一清理：清定时器 + 恢复移动速度 + 调父类。
- **关键实现**：`ClearTimer(DrainTimer);` → `if (ARPG_BaseCharacter* Character = GetRPGCharacter()) Character->StopSprint();` → `Super::EndAbility(...)`。
- **为什么这么写**：注释原文 —— 统一清理，任何结束路径（松手、耐力耗尽、被技能打断、角色死亡）都必须把移动速度恢复回去，否则角色会一直保持冲刺速度。这个坑很隐蔽：GA 被外部取消时不会走 `FinishSprint`，如果只在 `FinishSprint` 里恢复速度，就会出现"被打断后永久加速"。
- **被谁调用 / 调用谁**：GAS 与 `FinishSprint()`。调用 `Character->StopSprint`、`Super::EndAbility`。

#### `void TickStaminaDrain()`

- **干什么**：按速率扣耐力；扣完发现耐力归零就停止奔跑。
- **关键实现**：`ConsumeStamina(StaminaDrainPerSecond * DrainTickInterval);` → `Attributes = GetRPGAttributeSet();` 若 `Attributes && Attributes->GetStamina() <= 0.f` → 打 Log `"耐力耗尽，停止奔跑"` → `FinishSprint(false);`
- **为什么这么写**：注释未说明（`StaminaDrainPerSecond` / `DrainTickInterval` 的注释说明了间隔选择的权衡）。
- **被谁调用 / 调用谁**：由 `DrainTimer` 循环定时器回调。调用 `ConsumeStamina`、`GetRPGAttributeSet`、`FinishSprint`。

#### `void OnInputReleased()`

- **干什么**：松开按键 → 停止奔跑。
- **关键实现**：`FinishSprint(false);`
- **为什么这么写**：注释未说明。
- **被谁调用 / 调用谁**：由 `URPG_AbilitySystemComponent::NotifyInputReleased(InputTag)` 在能力激活期间调用。

#### `void FinishSprint(bool bWasCancelled)`

- **干什么**：主动收尾 → 结束能力。
- **关键实现**：`EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, /*bReplicateEndAbility*/ true, bWasCancelled);`
- **为什么这么写**：注释 —— 实际的清理在 `EndAbility` 里统一做，这里只是显式表达"主动收尾"的意图。
- **被谁调用 / 调用谁**：被 `TickStaminaDrain`（耐力耗尽）与 `OnInputReleased` 调用。

---

### `RPG_GA_Heal`

**一句话职责**：治疗 —— 走 `GE_Heal`（Instant + `SetByCaller(Data.Heal.Amount)`），治疗量由 `HealAmount` 决定，可在运行时用 `SetHealAmount` 覆盖。

头文件补充：属性集侧不需要任何改动 —— Health 增加时会自动被 `PreAttributeChange` 夹到 `MaxHealth` 以内，不会出现"治疗溢出成超血"。

**关键配置**（构造函数里设的）：

- `SetAssetTags(FGameplayTagContainer(RPGTags::Ability_Heal));`
- `NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;` —— 注释：治疗是数值逻辑，服务器算一次复制给客户端即可；用 `LocalPredicted` 的话两端各算一次，**治疗量会翻倍**。
- **未设置**：`ActivationOwnedTags`、`CancelAbilitiesWithTag`、`AbilityTriggers`；`InstancingPolicy` / `NetSecurityPolicy` / `ActivationBlockedTags` 继承基类。

**成员变量**（全部）：

| 声明 | 含义与初值 |
| --- | --- |
| `UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="RPG|Heal", meta=(ClampMin="0.0")) float HealAmount = 30.f` | 治疗量 |
| `UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="RPG|Heal") TSubclassOf<UGameplayEffect> HealEffectClass` | 治疗 GE（Instant，Modifier 读 `SetByCaller(Data.Heal.Amount)`） |

#### `URPG_GA_Heal::URPG_GA_Heal()`

- **干什么**：设置资产标签与 `ServerOnly`。
- **关键实现**：两行。
- **为什么这么写**：见上（翻倍的理由）。
- **被谁调用 / 调用谁**：UE 反射构造。

#### `void SetHealAmount(float NewAmount)`

- **干什么**：覆盖本次治疗量（供法术系统在激活前调用）。
- **关键实现**：`{ HealAmount = FMath::Max(NewAmount, 0.f); }`（内联在头文件里）
- **为什么这么写**：头文件注明用途 —— 这样同一个 GA 蓝图能服务不同强度的治疗（比如小药和大药）。
- **被谁调用 / 调用谁**：`UFUNCTION(BlueprintCallable, Category="RPG|Heal")`，供蓝图/法术系统调用；源码内**没有**其它 C++ 调用点。

#### `void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)`

- **干什么**：构造 `GE_Heal` 的 Spec，填入治疗量，应用给自己，然后立刻结束能力。
- **关键实现**：
  1. `Super::ActivateAbility(...)`；`CommitAbility` 失败 → `EndAbility(..., true, true)` → return。
  2. `ASC = GetAbilitySystemComponentFromActorInfo();` 若 `!ASC || !HealEffectClass` → Warning（两种原因分开报：`"拿不到 ASC"` / `"没有配置 HealEffectClass"`）→ `EndAbility(..., true, true)` → return。
  3. `Context = ASC->MakeEffectContext(); Context.AddSourceObject(this);` → `SpecHandle = ASC->MakeOutgoingSpec(HealEffectClass, GetAbilityLevel(), Context);` → Spec 无效 → `EndAbility(..., true, true)` → return。
  4. `SpecHandle.Data->SetSetByCallerMagnitude(RPGTags::Data_Heal_Amount, HealAmount);` —— 注释：通过 `SetByCaller` 传治疗量 —— 同一个 `GE_Heal` 服务所有强度的治疗。
  5. `ASC->ApplyGameplayEffectSpecToSelf(*SpecHandle.Data.Get());` → 打 Log `"治疗 %.1f"`。
  6. `EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, /*bReplicateEndAbility*/ true, /*bWasCancelled*/ false);`（瞬发能力，做完就结束）
- **为什么这么写**：见上。
- **被谁调用 / 调用谁**：GAS 激活流程。调用 `CommitAbility`、`EndAbility`、`MakeEffectContext`、`MakeOutgoingSpec`、`SetSetByCallerMagnitude`、`ApplyGameplayEffectSpecToSelf`。

---

### `RPG_GA_ApplyBuff`

**一句话职责**：施加增益/减益 —— 一个**通用容器**：具体施加什么效果完全由 `BuffEffectClass` 决定（加攻、加防、减防减速都可以用同一个 GA 类，只是配不同的 GE 蓝图子类）。

**为什么不像 Heal 那样用 SetByCaller 传数值**（头文件原文）：治疗是"同一个效果、不同强度"，所以用 `SetByCaller` 传量；而 Buff 是"不同的效果、各自有各自的持续时间和修饰符"—— 加攻 20% 持续 15 秒 和 加防 30% 持续 10 秒 之间没有可参数化的共性，硬要抽象反而会做出一堆含义不明的参数。所以这里的做法是：**一个 Buff 一个 GE 资产**，GA 只负责施加。将来如果出现"同一种 Buff 的多个强度等级"，再给那个 GE 加 `SetByCaller` 支持。

**关键配置**（构造函数里设的）：

- `SetAssetTags(FGameplayTagContainer(RPGTags::Ability_Buff_AttackUp));` —— ⚠️ 事实记录：本类是通用容器，但构造函数给的是一个**具体**的标签 `Ability.Buff.AttackUp`（"加攻 Buff 能力"），并非泛化的 `Ability.Buff.*`（`RPGTags` 里也没有更泛的 Buff 父标签）。注释未说明这一点是否是有意为之。
- `NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;` —— 注释：Buff 的施加是状态变更，由服务器决定、复制给客户端；`LocalPredicted` 会让两端各挂一次 GE，叠加层数翻倍。
- **未设置**：`ActivationOwnedTags`、`CancelAbilitiesWithTag`、`AbilityTriggers`；`InstancingPolicy` / `NetSecurityPolicy` / `ActivationBlockedTags` 继承基类。

**成员变量**（全部）：

| 声明 | 含义与初值 |
| --- | --- |
| `UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="RPG|Buff") TSubclassOf<UGameplayEffect> BuffEffectClass` | 要施加的 GE。头文件列出常用资产：`GE_Buff_AttackUp`（Duration 15s，Attack += 20%）、`GE_Buff_DefenseUp`（Duration 15s，Defense += 30%）、`GE_Debuff_DefenseDown`（Duration 10s，Defense ×= 0.7） |

#### `URPG_GA_ApplyBuff::URPG_GA_ApplyBuff()`

- **干什么**：设置资产标签与 `ServerOnly`。
- **关键实现**：两行。
- **为什么这么写**：见上（层数翻倍的理由）。
- **被谁调用 / 调用谁**：UE 反射构造。

#### `void SetBuffEffect(TSubclassOf<UGameplayEffect> NewEffect)`

- **干什么**：运行时覆盖要施加的 GE（供法术/道具系统调用）。
- **关键实现**：`{ BuffEffectClass = NewEffect; }`（内联在头文件里）
- **为什么这么写**：注释未说明。
- **被谁调用 / 调用谁**：`UFUNCTION(BlueprintCallable, Category="RPG|Buff")`；源码内**没有**其它 C++ 调用点。

#### `void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)`

- **干什么**：构造 `BuffEffectClass` 的 Spec 并应用到自身，然后立刻结束能力。
- **关键实现**：与 `GA_Heal` 结构一致，但**不设 `SetByCaller`**：
  1. `Super::ActivateAbility(...)`；`CommitAbility` 失败 → `EndAbility(..., true, true)` → return。
  2. `!ASC || !BuffEffectClass` → Warning（`"拿不到 ASC"` / `"没有配置 BuffEffectClass"`）→ `EndAbility(..., true, true)` → return。
  3. `Context = ASC->MakeEffectContext(); Context.AddSourceObject(this);` → `SpecHandle = ASC->MakeOutgoingSpec(BuffEffectClass, GetAbilityLevel(), Context);` → 无效 → `EndAbility(..., true, true)` → return。
  4. `ASC->ApplyGameplayEffectSpecToSelf(*SpecHandle.Data.Get());` → 打 Log `"施加增益：%s"`（打的是 GE 资产名）。
  5. `EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, /*bReplicateEndAbility*/ true, /*bWasCancelled*/ false);`
- **为什么这么写**：见头文件的"为什么不参数化"说明。
- **被谁调用 / 调用谁**：GAS 激活流程。调用 `CommitAbility`、`EndAbility`、`MakeEffectContext`、`MakeOutgoingSpec`、`ApplyGameplayEffectSpecToSelf`。

---

### `RPG_GA_StaminaRegen`

**一句话职责**：耐力恢复（被动、常驻）—— 激活时把一个 `Infinite` 的 `GE_StaminaRegen` 挂到角色身上，然后**一直保持激活**；真正决定"什么时候恢复"的完全不是代码，而是 GE 上的 `Ongoing Tag Requirements`。

**这个类里没有一行"恢复逻辑" —— 这是刻意的**（头文件原文）。机制全貌（头文件原文）：

```
GE_StaminaRegen（Infinite，Period = 0.25s）
  ├─ Modifier:            Stamina += 3.75（即每秒 15 点）
  └─ OngoingTagRequirements:
       IgnoreTags: State.Stamina.Blocked
                   ↑ 角色有这个标签时，整个 GE 被抑制
```

而 `State.Stamina.Blocked` 由 `GE_StaminaRegenDelay`（Duration = 3 秒）授予，每次 `ConsumeStamina` 都会重新挂一次来刷新它的持续时间。于是"停手 3 秒后缓慢恢复"这条规则变成了：

```
消耗耐力 → 挂上阻断标签（3 秒）→ 恢复 GE 被抑制
         → 3 秒内没再消耗 → 标签过期消失 → 恢复 GE 自动生效
```

**为什么不自己写个计时器**（头文件原文）：手写方案（GA 里存一个"最后消耗时间"，Tick 里判断是否超过 3 秒）的问题：需要 Tick 或定时器，多一份状态要维护；联机下这份状态在客户端和服务器要各自维护，容易不一致；调试时看不出"现在为什么没恢复"，只能翻代码；策划想改"3 秒"就得改代码或加一堆配置项。标签方案则：零计时代码，时间纯粹由 GE 的 Duration 表达；标签会复制，两端表现天然一致；用 GameplayDebugger 一眼能看到"恢复被 `State.Stamina.Blocked` 挡住了"；改恢复延迟 = 改 GE 的 Duration，改恢复速度 = 改 Modifier，都不碰代码。头文件原话："这是本项目里最'GAS 原生'的一处设计。"

**关键配置**（构造函数里设的）：

- `bActivateOnGranted = true;` —— 注释：授予后自动激活（由 `URPG_AbilitySystemComponent::RegisterInputAbility` 处理）。基类对 `bActivateOnGranted` 的说明：用于耐力恢复这类常驻被动能力 —— 它们没有输入触发，需要在角色初始化时就开始工作。
- `NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;` —— 注释：恢复是纯数值逻辑，客户端不需要自己算（属性复制会把结果同步过去）；用 `ServerOnly` 而不是 `LocalPredicted` 还有一个好处：避免客户端和服务器各自跑一份周期 GE 导致恢复速度翻倍。
- `NetSecurityPolicy = EGameplayAbilityNetSecurityPolicy::ServerOnly;` —— 注释：只允许服务器请求激活，客户端无权干预。⚠️ 这是本目录里**唯一**覆写 `NetSecurityPolicy` 的能力（其余能力都继承基类的 `ClientOrServer`）。
- `SetAssetTags(FGameplayTagContainer(RPGTags::Ability_StaminaRegen));`
- `ActivationOwnedTags.AddTag(RPGTags::Ability_StaminaRegen);` —— 注释：标记为被动能力 —— 其他系统（比如"死亡时清空所有能力"）可以据此识别。⚠️ 事实记录：资产标签与激活期标签用的是**同一个** `Ability.StaminaRegen`（本目录里唯一一处两个标签域重合的写法）。
- **未设置**：`AbilityTriggers`、`CancelAbilitiesWithTag`、`ActivationBlockedTags`（后者继承基类的 `State.Dead` —— 这正是"角色死亡时恢复能力失效"的实现方式，见 `ActivateAbility` 末尾的注释）。

**成员变量**（全部）：

| 声明 | 含义与初值 |
| --- | --- |
| `UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="RPG|Regen") TSubclassOf<UGameplayEffect> RegenEffectClass` | 常驻恢复 GE。头文件列出必须的配置：Duration Policy: `Infinite`；`Period: 0.25`；Modifier: `Stamina += 每秒恢复量 × Period`；Components → Target Tag Requirements → Ongoing Tag Requirements → Ignore Tags: `State.Stamina.Blocked` |

#### `URPG_GA_StaminaRegen::URPG_GA_StaminaRegen()`

- **干什么**：见上面六条关键配置。
- **关键实现**：按顺序 `bActivateOnGranted = true` → `NetExecutionPolicy = ServerOnly` → `NetSecurityPolicy = ServerOnly` → `SetAssetTags` → `ActivationOwnedTags.AddTag`。
- **为什么这么写**：见上。
- **被谁调用 / 调用谁**：UE 反射构造。

#### `void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)`

- **干什么**：把恢复 GE 常驻挂上，然后**不结束能力**。
- **关键实现**：
  1. `Super::ActivateAbility(...)`。
  2. `ASC = GetAbilitySystemComponentFromActorInfo();` 为空 → `EndAbility(..., /*bReplicateEndAbility*/ true, /*bWasCancelled*/ true)` → return。⚠️ 注意：**本类没有调用 `CommitAbility`**（与其它能力都不同），注释未说明原因。
  3. `!RegenEffectClass` → Warning `"没有配置 RegenEffectClass —— 耐力不会自动恢复。请在 GA_StaminaRegen 蓝图里指定 GE_StaminaRegen"` → `EndAbility(..., true, true)` → return。
  4. `Context = ASC->MakeEffectContext(); Context.AddSourceObject(this);` → `SpecHandle = ASC->MakeOutgoingSpec(RegenEffectClass, GetAbilityLevel(), Context);` → 无效 → `EndAbility(..., true, true)` → return。
  5. `ASC->ApplyGameplayEffectSpecToSelf(*SpecHandle.Data.Get());` —— 注释：常驻挂上。注意这里**不做任何"现在该不该恢复"的判断** —— 那完全由 GE 自己的 `OngoingTagRequirements` 决定；我们只负责把它挂上去，剩下的交给 GAS。
  6. 打 Log `"耐力恢复已挂载（受 State.Stamina.Blocked 标签阻断）"`。
  7. ⚠️ 注释原文：刻意**不调用 `EndAbility`** —— 这个能力要保持激活状态，它施加的 Infinite GE 才会一直挂在角色身上。什么时候结束？角色死亡时由 `State.Dead` 标签阻断，或者角色销毁时随 ASC 一起清理。
- **为什么这么写**：见上。
- **被谁调用 / 调用谁**：由 `URPG_AbilitySystemComponent` 在授予能力后依据 `ShouldActivateOnGranted()` 自动拉起（见 `RPG_AbilitySystemComponent.cpp:139/194/235` 三处 `ShouldActivateOnGranted()` 分支；能力在角色的 `StartupPassiveAbilities` 里配置）。调用 `ApplyGameplayEffectSpecToSelf`、`MakeOutgoingSpec`、`EndAbility`（仅失败路径）。

---

## 附：阅读源码时值得注意的几处"代码与注释不一致 / 未使用项"（仅陈述事实，供后续排查）

以下全部由全库 grep 核实，写在这里是为了避免读者看到注释后产生错误预期：

1. `URPG_GA_LightAttack` 的 `bComboWindowOpen` 只被写入、从未被读取（6 处写入、0 处读取）。连段推进完全靠 `TryStartNextSegment()` 消费输入缓存。
2. `URPG_GA_LightAttack` 的 `FTimerHandle SimulatedSegmentTimer` 声明后从未使用。
3. `URPG_GA_LightAttack::ActivateAbility` 里"消耗触发输入"那段，注释说"用 if 而不是直接调用"，实际代码是直接调用并忽略返回值。
4. `RPG_GA_Death.cpp` 里没有 `SetAssetTags`；`RPGTags::Ability_Death`（`"Ability.Death"`）在全库只有声明与定义两处，无任何引用。
5. `URPG_GA_HeavyAttack` 的三个事件回调都没有 `IsEventFromCurrentSegment()` 那类迟到事件过滤（该类里没有这个函数）。
6. `URPG_GA_LightAttack::OnAttackEndEvent` 也没有做来源过滤（三个窗口回调都做了），注释未说明原因。
7. `FRPG_HeavyAttackSet::ChargeMoveSpeedScale`（默认 0.3）在 C++ 里没有任何读取点。
8. `RPGTags::Event_Combat_ChargeStart / ChargeLevelUp / ChargeRelease` 三个标签在全库只有声明与定义，`GA_HeavyAttack` 并未广播或监听它们（蓄力节奏靠 `State.Attack.Charging.*` 标签 + UI 自行计时）。
9. `RPGTags::State_Attack_Active` 与 `State_Attack_ComboWindow` 在 C++ 里没有引用点（是否在动画/配置资产里使用未核实）。
10. `URPG_GA_ApplyBuff` 是通用容器，但资产标签硬编码为 `Ability.Buff.AttackUp`。
11. `URPG_GA_StaminaRegen::ActivateAbility` 是全目录唯一不调用 `CommitAbility` 的能力。

---

# 五、战斗规则（Combat）与敌人 AI

本章覆盖 `Source/RPG/Combat/`（战斗类型定义、输入缓存、战斗状态组件、攻击模组数据资产、判定窗口载荷）与 `Source/RPG/AI/`（AIController、黑板键、行为树装饰器 / 服务 / 任务）共 15 个文件。

贯穿这两层的核心约定有三条，读下面任何一节时都可以拿它们当坐标：

1. **"是否正在攻击"这类状态一律以 GameplayTag 为准**，组件里只存"纯战斗逻辑状态"（打到第几段），避免出现两份真相（见 `RPG_CombatComponent.h` 头部注释）。
2. **AI 攻击走和玩家完全相同的入口**：把 `Input.*` 标签推进 `URPG_CombatComponent` 的输入缓存 → `ASC->TryActivateAbilityByInputTag()`，不存在 AI 专用路径（见 `RPG_AIController.h`、`RPG_BTTask_Attack.h` 头部注释）。
3. **行为树的"等一件事结束"必须用潜在任务（Latent Task）**：`return InProgress` + 之后的 `FinishLatentTask`，否则会出现"一边走一边挥拳"这类滑步（见 `RPG_BTTask_MoveBase.h`）。

---

### `Source/RPG/Combat/RPG_CombatComponent.h` / `Source/RPG/Combat/RPG_CombatComponent.cpp`

**一句话职责**：战斗状态组件 —— 持有输入缓存、维护连段索引、保存当前攻击模组；敌我共用，挂在 `ARPG_BaseCharacter` 上。

**类/结构体**：`URPG_CombatComponent` —— 继承 `UActorComponent`，`UCLASS(ClassGroup = (RPG), meta = (BlueprintSpawnableComponent))`。定位是"纯战斗逻辑状态的持有者"，刻意不承担"能力怎么触发 / 伤害怎么算 / 是否正在攻击"这三件事。

#### 头文件里给出的三条设计说明（不是函数说明，但决定了整个类的形态）

- **【管什么，不管什么】**：管输入缓存（`URPG_InputBuffer` 的持有者与生命周期管理）、连段索引（当前打到第几段）、当前攻击模组（徒手 / 近战 / 远程）。不管：能力怎么触发与动画怎么播（→ GA）、伤害怎么算（→ DamageExecution）、"是否正在攻击"这个状态（→ GameplayTag `State.Attacking`）。
- **【为什么"是否攻击中"不放这里，而用标签】**：如果组件里存一个 `bAttacking`，它和 GameplayTag 就有了两份真相 —— GA 被外部打断时标签会被 GAS 自动清理但 `bAttacking` 不会；联机下标签会复制但 `bAttacking` 不会。结果就是"有时判断为攻击中、有时判断为不攻击"，且只在特定时序下出现。所以凡是"角色当前处于什么状态"一律以 GameplayTag 为准。
- **【连段索引的生命周期】**：`Idle → 轻击输入 → ComboIndex = 1 → 衔接窗口开启 + 缓存有货 → ComboIndex = 2 → 衔接窗口关闭仍未输入 → ComboIndex = 0（重置）`。注释特别指出重置时机是最容易做错的地方：在 GA 结束时就重置则玩家永远连不上第二段；一直不重置则玩家停手后下一击会直接从第 5 段开始。

#### `URPG_CombatComponent()`

- **干什么**：构造函数，只做一件事。
- **关键实现**：`PrimaryComponentTick.bCanEverTick = false;`
- **为什么这么写**：注释原话 —— "输入缓存需要每帧处理吗？不需要。它只在'按键'和'消耗'两个时刻被访问，惰性清理足够。"
- **被谁调用 / 调用谁**：由 UObject 构造流程调用；不调用任何东西。

#### `virtual void BeginPlay() override`

- **干什么**：创建输入缓存对象、把蓝图里配的缓存策略同步进去、解析默认攻击模组，并对"没配模组"给出警告。
- **关键实现**（逐句）：
  1. `Super::BeginPlay();`
  2. `InputBuffer = NewObject<URPG_InputBuffer>(this, TEXT("InputBuffer"));` —— 用组件自己当 Outer，命名对象 `InputBuffer`。
  3. `if (InputBuffer) { InputBuffer->SetMode(BufferMode); }` —— 把 `UPROPERTY` 上的 `BufferMode` 传进缓存实例（缓存对象自己不知道蓝图配置）。
  4. `CurrentModule = DefaultModule;` —— 初始模组 = 蓝图里配的默认模组。
  5. 若 `!CurrentModule`：`UE_LOG(LogRPG_Combat, Warning, ...)`，提示"攻击会因为没有招式表而无法执行。请在角色蓝图里指定攻击模组 DataAsset"。
  6. 否则：`UE_LOG(LogRPG_Combat, Verbose, ...)` 打一条"战斗组件就绪，攻击模组：%s"。
- **为什么这么写**：注释原话 —— "在这里创建 InputBuffer 而不是构造函数：构造函数可能对 CDO 执行多次，而 NewObject 出来的对象不该挂在 CDO 上。"
- **被谁调用 / 调用谁**：由引擎在 BeginPlay 阶段调用；调用 `URPG_InputBuffer::SetMode()`；读 `TObjectPtr<URPG_AttackModuleData>::GetName()`。（日志类别来自 `Core/RPG_LogChannels.h` 的 `LogRPG_Combat`。）

#### `void PushInputTag(FGameplayTag InputTag)`

- **干什么**：把一个输入意图压入缓存。注释说明：由 PlayerController（玩家）或 AI 任务（敌人）调用。
- **关键实现**：`if (!InputBuffer) return;` 然后 `InputBuffer->Push(InputTag, InputLifeTime, GetNow());` —— 生命周期用组件上的 `InputLifeTime`，时间戳用 `GetNow()`。
- **为什么这么写**：注释未说明（该函数上方无实现层理由；相关的"缓存为什么要生命周期"写在 `RPG_InputBuffer.h` 与 `FRPG_BufferedInput` 的注释里）。
- **被谁调用 / 调用谁**：`UFUNCTION(BlueprintCallable)`。调用者包括 `ARPG_PlayerController::OnAbilityInputPressed`（`RPG_PlayerController.cpp:318`，本地按键路径）、`ARPG_PlayerController::Server_PushInputTag_Implementation`（`RPG_PlayerController.cpp:405`，服务器侧 RPC）、`URPG_BTTask_Attack::ExecuteTask`（`RPG_BTTask_Attack.cpp:89`，AI 路径）、`URPG_GA_LightAttack`（`RPG_GA_LightAttack.cpp:445`，把不匹配的缓存输入回推回去）。调用 `URPG_InputBuffer::Push()`。

#### `bool ConsumeInputTag(FGameplayTag& OutTag)`

- **干什么**：取出一个缓存输入。
- **关键实现**：`return InputBuffer && InputBuffer->Consume(OutTag, GetNow());` —— 一句话转发，`InputBuffer` 为空时短路返回 false。
- **为什么这么写**：注释只写了返回值语义 —— `@return 容器为空或全部过期时返回 false`（过期判断在 `URPG_InputBuffer::PruneExpired` 里做）。
- **被谁调用 / 调用谁**：**不是 UFUNCTION**（引用出参，仅供 C++）。调用者：`URPG_GA_LightAttack::TryStartNextSegment`（`RPG_GA_LightAttack.cpp:439`）、`URPG_GA_HeavyAttack`（`RPG_GA_HeavyAttack.cpp:117`）。调用 `URPG_InputBuffer::Consume()`。

#### `int32 GetBufferedInputCount() const`

- **干什么**：返回当前缓存条数。
- **关键实现**：`return InputBuffer ? InputBuffer->Num() : 0;`
- **为什么这么写**：注释未说明。
- **被谁调用 / 调用谁**：`UFUNCTION(BlueprintPure)`。调用者：`ARPG_PlayerController::Server_PushInputTag_Implementation` 的日志（`RPG_PlayerController.cpp:409`）。调用 `URPG_InputBuffer::Num()`。

#### `void ClearInputBuffer()`

- **干什么**：清空输入缓存。
- **关键实现**：`if (InputBuffer) { InputBuffer->Clear(); }`
- **为什么这么写**：注释未说明（在组件头文件里）；真正的理由写在调用点 `ARPG_BaseCharacter` 的重生流程注释里 —— "不清的话，死亡瞬间按下的那一堆输入会被带进新一条命：复活后角色自己就动起来了 —— 玩家会觉得'角色不受控制'。"
- **被谁调用 / 调用谁**：`UFUNCTION(BlueprintCallable)`。调用者：`ARPG_BaseCharacter` 重生流程（`RPG_BaseCharacter.cpp:783`）。调用 `URPG_InputBuffer::Clear()`。

#### `int32 GetComboIndex() const`

- **干什么**：读连段索引（0 表示不在连段中，1-based 对应第 N 段）。
- **关键实现**：头文件内联 `return ComboIndex;`
- **为什么这么写**：注释未说明。
- **被谁调用 / 调用谁**：`UFUNCTION(BlueprintPure)`。调用者：`URPG_AnimInstanceBase`（`RPG_AnimInstanceBase.cpp:457`，供动画蓝图使用）、`ARPG_HUDWidget`（`RPG_HUDWidget.cpp:412`）、`URPG_GA_LightAttack`（`RPG_GA_LightAttack.cpp:120`）。

#### `void SetComboIndex(int32 NewIndex)`

- **干什么**：推进到指定段。
- **关键实现**：先 `if (ComboIndex == NewIndex) return;` 早退（避免日志噪音），再 `UE_LOG(LogRPG_Combat, Verbose, TEXT("[%s] 连段索引 %d → %d"), ...)`，最后赋值 `ComboIndex = NewIndex;`。
- **为什么这么写**：注释未说明（早退判断的意图从代码可读；同文件 `URPG_GA_LightAttack.cpp:157` 的注释提到"ResetCombo 内部有'值没变就跳过'的判断，所以重复调用是安全的"，是同一风格的呼应）。
- **被谁调用 / 调用谁**：**不是 UFUNCTION**。调用者：`URPG_GA_LightAttack`（`RPG_GA_LightAttack.cpp:135`、`:468`）。

#### `void ResetCombo()`

- **干什么**：重置连段（衔接窗口关闭后仍未消耗到输入时调用）。
- **关键实现**：若 `ComboIndex != 0` 先打一条 Verbose 日志（"连段重置（原本在第 %d 段）"），然后无条件 `ComboIndex = 0;`。
- **为什么这么写**：注释未说明。
- **被谁调用 / 调用谁**：`UFUNCTION(BlueprintCallable)`。调用者：`URPG_GA_LightAttack`（`:127`、`:160`、`:482`、`:703`）、本类 `SetAttackModule()`（`:120`）、`ARPG_BaseCharacter` 重生流程（`RPG_BaseCharacter.cpp:784`）。

#### `URPG_AttackModuleData* GetAttackModule() const`

- **干什么**：返回当前生效的攻击模组，可能为 `nullptr`（角色没配）。
- **关键实现**：头文件内联 `return CurrentModule;`
- **为什么这么写**：注释未说明。
- **被谁调用 / 调用谁**：`UFUNCTION(BlueprintPure)`。调用者：`URPG_GameplayAbilityBase::GetAttackModule()`（`RPG_GameplayAbilityBase.cpp:154`，再被 GA_LightAttack / GA_HeavyAttack 大量使用）、`URPG_AnimInstanceBase`（`RPG_AnimInstanceBase.cpp:459`，读 `ModuleType`）、`ARPG_HUDWidget`（`RPG_HUDWidget.cpp:453`）。

#### `void SetAttackModule(URPG_AttackModuleData* NewModule)`

- **干什么**：切换攻击模组（换武器时调用）。传 `nullptr` 会回落到默认模组。
- **关键实现**：
  1. `URPG_AttackModuleData* Resolved = NewModule ? NewModule : DefaultModule.Get();` —— 传空时回落而不是清空；注释明确写了这里必须 `.Get()` 的原因。
  2. `CurrentModule = Resolved;`
  3. `UE_LOG(LogRPG_Combat, Log, TEXT("[%s] 切换攻击模组：%s"), ...)`，无模组时打 `(无)`。
  4. `ResetCombo();`
- **为什么这么写**：两条都在注释里 —— ① "传 nullptr 时回落到默认模组，而不是把当前模组清空 —— '卸下武器'应该回到徒手，而不是变成没有招式表。注意这里要 `.Get()`：`TObjectPtr` 和裸指针混在三元表达式里编译器无法推断公共类型（C2445），显式取出裸指针即可。" ② "换模组时连段必须重置 —— 否则从徒手第 3 段切到武器会直接从武器第 4 段开始，这在设计上没有意义，而且会取到越界的招式配置。"
- **被谁调用 / 调用谁**：`UFUNCTION(BlueprintCallable)`。**C++ 侧未找到调用点**（全库检索仅有定义本身）——即当前由蓝图 / 换武器流程调用。调用本类 `ResetCombo()`。

#### `FString GetCombatDebugString() const`

- **干什么**：把当前战斗状态格式化成一行字符串（连段索引 + 缓存内容 + 模组名）。
- **关键实现**：`FString::Printf(TEXT("连段=%d | 缓存=%s | 模组=%s"), ComboIndex, InputBuffer ? *InputBuffer->ToDebugString() : TEXT("(未初始化)"), *ModuleName)`；模组名在 `CurrentModule` 为空时取 `(无模组)`。
- **为什么这么写**：注释未说明。
- **被谁调用 / 调用谁**：`UFUNCTION(BlueprintPure)`。**C++ 侧未找到调用点**。调用 `URPG_InputBuffer::ToDebugString()`。

#### `float GetNow() const`（private）

- **干什么**：取当前世界时间；没有 World 时（CDO）返回 0。
- **关键实现**：`const UWorld* World = GetWorld(); return World ? World->GetTimeSeconds() : 0.f;`
- **为什么这么写**：注释只说明了 CDO 这一边界（写在头文件声明处："没有 World 时（CDO）返回 0"）。
- **被谁调用 / 调用谁**：本类 `PushInputTag()` / `ConsumeInputTag()` 调用（作为 `Push` / `Consume` 的 `Now` 参数）。

#### 成员逐个列（`URPG_CombatComponent`）

| 成员 | 属性说明 | 类型 / 默认值 |
| --- | --- | --- |
| `DefaultModule` | `UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG\|Combat")`，protected。注释："默认攻击模组。在角色蓝图里配" | `TObjectPtr<URPG_AttackModuleData>` |
| `BufferMode` | `UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG\|Combat")`，protected。注释："输入缓存策略。默认栈（尊重最新意图），可改成队列对比手感" | `ERPG_InputBufferMode = ERPG_InputBufferMode::Stack` |
| `InputLifeTime` | `UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG\|Combat", meta = (ClampMin = "0.05", ClampMax = "2.0"))`，protected。注释："缓存条目的生命周期（秒）。太短 → 玩家在动画后半段按的下一段会丢，感觉'不跟手'；太长 → 玩家乱按后角色会自己动，感觉'失控'" | `float = 1.f` |
| `InputBuffer` | `UPROPERTY()`（private），注释："输入缓存容器。BeginPlay 时创建" | `TObjectPtr<URPG_InputBuffer>` |
| `CurrentModule` | `UPROPERTY()`（private），无注释 | `TObjectPtr<URPG_AttackModuleData>` |
| `ComboIndex` | **没有 UPROPERTY**（纯 C++ 成员，private），注释："0 = 不在连段中；1..N = 当前处于第 N 段" | `int32 = 0` |

---

### `Source/RPG/Combat/RPG_InputBuffer.h` / `Source/RPG/Combat/RPG_InputBuffer.cpp`

**一句话职责**：输入缓存容器 —— 动作游戏"跟手"手感的关键组件，负责把玩家/AI 提前按下的输入记下来，等衔接窗口开启时再消费。

**类/结构体**：`URPG_InputBuffer` —— 继承 `UObject`，`UCLASS()`。它是**本机运行时对象**（由 `URPG_CombatComponent::BeginPlay` 用 `NewObject` 创建），从来不参与复制。

#### 头文件里的设计说明（决定了本类的全部行为）

- **【它解决什么问题】**：玩家打出第 1 段轻击后，动画还有 0.6 秒才结束，这时按第 2 下 —— 没有缓存 → 按键被丢弃，玩家必须等动画播完再按，感觉"不跟手"；有缓存 → 按键记下来，等衔接窗口一开就立刻打出第 2 段，连招流畅。注释点名《黑神话》《只狼》这类游戏的手感很大一部分来自这套机制。
- **【三个必须处理好的细节】**：
  1. **生命周期（过期作废）**：玩家在硬直里乱按的按键不能无限期保留，否则硬直一结束会突然打出一串。每个条目带时间戳，超过 `LifeTime` 就作废。推荐 0.4~0.6 秒。
  2. **容量上限（丢弃最旧）**：疯狂连打时不能无限堆积。满了就丢最旧的 —— 因为玩家**最新**按下的才代表当前意图，丢旧输入比拒绝新输入手感更好。
  3. **惰性清理（不开 Timer）**：容器只在 Push / Consume 两个时刻被访问，所以在这两处【顺带清理】即可。不需要额外的 Timer 或 Tick —— 那纯属浪费，而且会引入"清理时机不可控"的问题。
- **【联机说明】**：输入缓存本质上属于**客户端本地**：服务器上没有"玩家按键"这件事，它只收到"请求激活能力 X"。所以：客户端用自己的缓存决定"下一段打什么"；服务器直接执行被请求的能力，不读缓存；连段索引（ComboIndex）两边各自维护，逻辑相同所以结果一致。**MVP 阶段不处理预测不一致的深度问题**（那需要 PredictionKey 级别的处理）。—— 这段注释是理解"联机连段为什么需要 `Server_PushInputTag`"的关键：服务器侧也需要有人往它自己的缓存里推条目，玩家的按键不会自动出现在服务器上。

#### `static constexpr int32 MaxEntries = 4`

- **干什么**：容量上限常量。
- **为什么是 4**：注释原话 —— "4 是个够用的值：能容纳'提前按了下一段 + 一点乱按'，又不会积压成灾难。"

#### `void SetMode(ERPG_InputBufferMode NewMode)` / `ERPG_InputBufferMode GetMode() const`

- **干什么**：设置 / 读取取出策略。
- **关键实现**：两个都是头文件内联的一行赋值 / 返回。
- **为什么这么写**：注释未说明。
- **被谁调用 / 调用谁**：`SetMode` 的调用者是 `URPG_CombatComponent::BeginPlay`（把蓝图上的 `BufferMode` 灌进来）。`GetMode` 在本库中未被调用（`ToDebugString` 直接读私有成员 `Mode`）。

#### `void Push(const FGameplayTag& InputTag, float LifeTime, float Now)`

- **干什么**：压入一个输入意图。
- **关键实现**（逐分支）：
  1. `if (!InputTag.IsValid()) return;` —— 无效标签直接忽略。
  2. `if (Entries.Num() >= MaxEntries)` → `Entries.RemoveAt(0);` + `UE_LOG(LogRPG_Combat, VeryVerbose, TEXT("输入缓存已满（%d），丢弃最旧条目以腾出空间"), MaxEntries);`
  3. 构造 `FRPG_BufferedInput NewEntry`，填 `InputTag` / `Timestamp = Now` / `LifeTime = LifeTime`。
  4. `Entries.Add(MoveTemp(NewEntry));` —— 行内注释："MoveTemp 会将引用转换为右值引用"。
  5. `UE_LOG(LogRPG_Combat, VeryVerbose, TEXT("缓存输入 %s（当前缓存 %d 条）"), *InputTag.ToString(), Entries.Num());`
- **为什么这么写**：容量控制处给了理由 —— "为什么丢旧而不是拒绝新？因为玩家**最新**按下的才代表当前意图。比如玩家连按三次轻击，我们要保住最后一次的意图，而不是让容器被前两次占满后拒绝第三次。"
- **被谁调用 / 调用谁**：`URPG_CombatComponent::PushInputTag`（唯一调用者）。注意：**它不调用 `PruneExpired`** —— 清理只在 `Consume` 里发生。

#### `bool Consume(FGameplayTag& OutTag, float Now)`

- **干什么**：取出并移除一个输入意图。
- **关键实现**（逐分支）：
  1. `PruneExpired(Now);` 先顺带清理过期条目（行内注释："顺带清理过期条目 —— 惰性清理，不开 Timer"）。
  2. `if (Entries.IsEmpty()) return false;`
  3. 按策略决定索引：`Stack` → `Index = Entries.Num() - 1`（取末尾，最后压入的）；否则（`Queue`）→ `Index = 0`（取开头）。
  4. `OutTag = Entries[Index].InputTag; Entries.RemoveAt(Index);`
  5. `UE_LOG(LogRPG_Combat, Verbose, TEXT("消耗缓存输入 %s（剩余 %d 条）"), ...)`；`return true;`
- **为什么这么写**：注释把两种策略的意图写清楚了 —— 栈："取末尾（最后压入的）—— 尊重玩家最新的意图"；队列："取开头（最早压入的）—— 按输入顺序依次执行"。
- **被谁调用 / 调用谁**：`URPG_CombatComponent::ConsumeInputTag`（唯一调用者）。调用本类 `PruneExpired()`。

#### `void PruneExpired(float Now)`

- **干什么**：清理所有已过期的条目。
- **关键实现**：`const int32 RemovedCount = Entries.RemoveAll([Now](const FRPG_BufferedInput& Entry) { return Entry.IsExpired(Now); });`，若 `RemovedCount > 0` 打一条 VeryVerbose 日志（"清理了 %d 条过期输入（剩余 %d 条）"）。
- **为什么这么写**：注释原话 —— "惰性清理：只在 Push / Consume 时调用。为什么不开 Timer？因为容器只在这两个时刻被访问，惰性清理的时机恰好就是'需要准确内容'的时刻，而且零额外开销。用 Timer 反而会引入'清理时机与使用时机错位'的微妙问题。"
- **被谁调用 / 调用谁**：本类 `Consume()` 调用（**注意 `Push()` 不调用它**）。调用 `FRPG_BufferedInput::IsExpired()`。

#### `void Clear()` / `bool IsEmpty() const` / `int32 Num() const`

- **干什么**：清空 / 判空 / 取条数，均为头文件内联一行实现（`Clear()` 是 `Entries.Reset();`）。
- **为什么这么写**：注释未说明。
- **被谁调用 / 调用谁**：`Clear()` ← `URPG_CombatComponent::ClearInputBuffer()`；`Num()` ← `URPG_CombatComponent::GetBufferedInputCount()`；`IsEmpty()` 在本库中未被外部调用。

#### `FString ToDebugString() const`

- **干什么**：把当前缓存内容格式化成一行字符串。
- **关键实现**：
  1. 空容器直接返回 `TEXT("(空)")`。
  2. `TStringBuilder<256> Builder;`，先 `Appendf(TEXT("[%s] "), Mode == ERPG_InputBufferMode::Stack ? TEXT("栈") : TEXT("队列"))`。
  3. 遍历 `Entries`，对每一项判断 `bIsNext`：Stack 模式下是 `i == Entries.Num() - 1`，队列模式下是 `i == 0`。
  4. `Builder.Appendf(TEXT("%s%s%s"), bIsNext ? TEXT("→") : TEXT(""), *Entries[i].InputTag.ToString(), (i < Entries.Num() - 1) ? TEXT(", ") : TEXT(""))`。
  5. `return FString(Builder.ToString());`
- **为什么这么写**：行内注释 —— "标记栈顶/队头，调试时一眼能看出下一个会被消耗的是哪个"。
- **被谁调用 / 调用谁**：`URPG_CombatComponent::GetCombatDebugString()`。

#### 成员逐个列（`URPG_InputBuffer`）

| 成员 | 属性说明 | 类型 / 默认值 |
| --- | --- | --- |
| `Entries` | `UPROPERTY()`（private）。注释："缓存条目。末尾是'最新压入'（栈顶）—— Consume 在 Stack 模式下从这里取" | `TArray<FRPG_BufferedInput>` |
| `Mode` | 无 UPROPERTY（private） | `ERPG_InputBufferMode = ERPG_InputBufferMode::Stack` |
| `MaxEntries` | `static constexpr int32` | `4` |

---

### `Source/RPG/Combat/RPG_AttackModuleData.h` / `Source/RPG/Combat/RPG_AttackModuleData.cpp`

**一句话职责**：攻击模组数据资产 —— 一套武器的完整招式表（蒙太奇、倍率、耐力消耗、检测源与 Socket），并在编辑器里做配置体检。

**类/结构体**：`URPG_AttackModuleData` —— 继承 `UPrimaryDataAsset`，`UCLASS(BlueprintType)`。

#### 头文件里的两段设计说明

- **【这个资产存在的意义：加一把新武器不用改代码】**：徒手 / 近战 / 远程三套模组共用同一个 GA，差异全部由本资产描述（5 段轻击各自的蒙太奇、伤害倍率、耐力消耗；3 段蓄力重击的门槛时间与倍率；切手技的蒙太奇与倍率；伤害检测源及其 Socket 名）。注释原话："想加一把太刀？新建一个 `DA_AttackModule_Katana`，填 5 个蒙太奇和倍率，挂到角色上即可 —— **一行 C++ 都不用改**。"
- **【为什么用 DataAsset 而不是 DataTable】**：① 需要嵌套结构（重击组里含三个等级），DataTable 表达起来很别扭；② 含大量软引用（蒙太奇），DataAsset 里可以直接拖拽赋值并做引用校验；③ 可以在编辑器里做数据校验（`IsDataValid`），配错了当场提示；④ 数量少（三五个），不需要 DataTable 的批量编辑能力。

#### `const FRPG_AttackSegment* GetLightSegment(int32 Index) const`

- **干什么**：取第 `Index` 段轻击（0-based），越界返回 `nullptr`。
- **关键实现**：`if (!LightAttacks.IsValidIndex(Index)) return nullptr;` 否则 `return &LightAttacks[Index];`
- **为什么这么写**：注释 —— "返回指针而不是副本 —— 段结构比较大，而且调用方只读。"
- **被谁调用 / 调用谁**：`URPG_GA_LightAttack`（`RPG_GA_LightAttack.cpp:124`、`:240`、`:451`）。

#### `int32 GetLightSegmentCount() const`

- **干什么**：轻击段数。
- **关键实现**：头文件内联 `return LightAttacks.Num();`，`UFUNCTION(BlueprintPure, Category = "RPG|Attack")`。
- **为什么这么写**：注释未说明。
- **被谁调用 / 调用谁**：`URPG_GA_LightAttack`（`:90`、`:461`）。

#### `int32 GetChargeLevelForTime(float ChargeTime) const`

- **干什么**：按已蓄力的时间取对应的蓄力等级（1-based；0 表示还没到最低门槛）。
- **关键实现**：`int32 Result = 0;` 然后 `for (int32 i = 0; i < HeavyAttack.Levels.Num(); ++i)`，若 `ChargeTime >= Level.RequiredChargeTime` 则 `Result = i + 1;`（记"最后一个满足的"），返回 `Result`。
- **为什么这么写**：注释原话 —— "从低到高扫一遍，记录最后一个满足时间门槛的等级。用'最后一个满足的'而不是'第一个不满足的前一个'，是因为 Levels 数组的顺序由策划配置，不能假设它一定严格升序 —— 这样写即使配成乱序也能得到正确结果（取满足条件的最高级）。"对外用 1-based 是"更符合直觉"。
- **被谁调用 / 调用谁**：`URPG_GA_HeavyAttack`（`RPG_GA_HeavyAttack.cpp:305`，参数为 `ChargeElapsed`）。

#### `const FRPG_HeavyAttackLevel* GetHeavyLevel(int32 Level) const`

- **干什么**：取第 `Level` 级（1-based）的蓄力配置，越界返回 `nullptr`。
- **关键实现**：`const int32 Index = Level - 1;` → `if (!HeavyAttack.Levels.IsValidIndex(Index)) return nullptr;` → `return &HeavyAttack.Levels[Index];`
- **为什么这么写**：注释 —— "对外是 1-based，内部数组是 0-based"。
- **被谁调用 / 调用谁**：`URPG_GA_HeavyAttack`（`:361`、`:367` 取 `Level(1)` 作兜底、`:464`）、`ARPG_HUDWidget`（`RPG_HUDWidget.cpp:459`，用 `GetHeavyLevelCount()` 取最后一级做展示）。

#### `int32 GetHeavyLevelCount() const`

- **干什么**：蓄力等级数量。
- **关键实现**：头文件内联 `return HeavyAttack.Levels.Num();`，`UFUNCTION(BlueprintPure, Category = "RPG|Attack")`。
- **被谁调用 / 调用谁**：`ARPG_HUDWidget`（`RPG_HUDWidget.cpp:458`）。

#### `EDataValidationResult IsDataValid(FDataValidationContext& Context) const`（`#if WITH_EDITOR`）

- **干什么**：编辑器数据校验 —— "配错了在 Content Browser 里直接标黄"。
- **关键实现**（按顺序）：
  1. `EDataValidationResult Result = Super::IsDataValid(Context);` 先取父类结果。
  2. **轻击段数**：`LightAttacks.Num() > 5` → `AddWarning`（"轻击段数为 {0}，超过 5 段。输入缓存容量按 5 段连击设计，多余的段可能永远连不上。"）。不强制正好 5 段，因为设计上允许变体武器只有 3 段。
  3. **倍率是否递增**：从 `i = 1` 起比较 `LightAttacks[i].DamageMultiplier < LightAttacks[i-1].DamageMultiplier` → `AddWarning`（"第 {0} 段的伤害倍率低于前一段 —— 连段通常应该递增倍率"）。注释说明只警告不报错 —— "也许策划就是想要一个特殊的递减套路"。
  4. **蓄力等级的时间门槛**：从 `i = 1` 起，若 `HeavyAttack.Levels[i].RequiredChargeTime <= HeavyAttack.Levels[i-1].RequiredChargeTime` → `AddError`（"第 {0} 级的蓄力时间门槛没有比上一级更高，这一级永远达不到"）。这条是 **Error** 而不是 Warning。
  5. **检测源与 Socket 配置匹配**：`TraceSource == WeaponBlade` 且 `BladeStartSocket.IsNone() || BladeEndSocket.IsNone()` → `AddError`（"轨迹检测拿不到位置"）；`TraceSource == Projectile && !ProjectileClass` → `AddError`（"攻击不会产生任何效果"）。
  6. **蒙太奇 Notify 布置**（见下）：轻击循环调用 `ValidateAttackMontage`，非最后一段 `bExpectComboWindow = true`、最后一段 `bWarnOnUnexpectedComboWindow = true`；`ChargeStartMontage` / `ChargeLoopMontage` 用 `ValidateHoldMontage`；每个 `HeavyAttack.Levels[i].ReleaseMontage` 与 `ComboTransitionMontage` 用 `ValidateAttackMontage` 且两个开关都为 `false`。
  7. `return Result;`
- **为什么这么写**：注释在"蒙太奇 Notify 布置"一节给了完整理由 —— "这一组检查是为了把'连招打不全''人物只是微微动了一下'这类只能靠手感描述的问题，变成打开资产就能看见的具体百分比。"（详见下面两个匿名命名空间辅助函数。）
- **被谁调用 / 调用谁**：由编辑器资产校验流程调用。调用匿名命名空间里的 `ValidateAttackMontage()` / `ValidateHoldMontage()`（进而 `ScanMontageNotifies()` / `FormatNotifyLayout()`）。

#### 匿名命名空间辅助（`RPG_AttackModuleData.cpp` 内，仅 `WITH_EDITOR`）

- `struct FNotifyLayout` —— 扫描结果：`bHasAttackEnd` / `AttackEndPercent`；`bHasAttackWindow` / `AttackWindowBegin` / `AttackWindowEnd`；`bHasComboWindow` / `ComboWindowBegin` / `ComboWindowEnd`；`TotalNotifyCount`。初值用 `-1.f`（表示"无"）、`false`、`0`。
- `FString FormatNotifyLayout(const FNotifyLayout& Layout)` —— 用一个 lambda `RangeText` 把"有/无"渲染成 `"a%→b%"` 或 `"无"`，输出格式为 `"判定窗口 %s｜衔接窗口 %s｜攻击结束 %s"`。
- `FNotifyLayout ScanMontageNotifies(const UAnimMontage* Montage, float Length)` —— 遍历 `Montage->Notifies`：每项先 `++Layout.TotalNotifyCount`；`Begin = Event.GetTriggerTime() / Length`；`End = Event.GetDuration() > 0.f ? (Event.GetTriggerTime() + Event.GetDuration()) / Length : Begin`（行内注释："NotifyState 取两端时刻，单点 Notify 只有触发时刻"）。随后按 `Cast<URPG_AnimNotify_AttackEnd>(Event.Notify)`、`Cast<URPG_AnimNotifyState_AttackWindow>(Event.NotifyStateClass)`、`Cast<URPG_AnimNotifyState_ComboWindow>(Event.NotifyStateClass)` 依次分类记录（注意用 `else if`，一个通知只归一类）。
- `void ValidateAttackMontage(FDataValidationContext& Context, const UAnimMontage* Montage, const FText& Label, bool bExpectComboWindow, bool bWarnOnUnexpectedComboWindow)` —— 体检**攻击蒙太奇**。
  - 蒙太奇为空直接 `return`（注释："留空是合法的（GA 会走模拟时序分支），不算问题"）。
  - `Length <= 0.f` → `AddWarning`（用 `NSLOCTEXT("RPG", "ZeroLengthMontage", ...)`，"动画不会播放"）后返回。
  - 收集问题到 `TArray<FString> Problems`（注释解释了先收集后输出："是为了让每个蒙太奇**最多只标一条黄**，并且把位置表一并带上：一次就能看全，不用点开时间轴对数字"）。
  - ① 缺 `RPG 攻击结束` → "GA 退化成「蒙太奇播完才结束」，后摇偏长。建议在 95% 处加一个"；或 `AttackEndPercent < 0.7f` → 报"太靠前"，行内注释标注了这是"**连招打不全 + 人物只微微动了一下**的常见元凶"，后果描述为"GA 会在这一帧结束能力并停掉动画，表现为连段中途断掉、动作只播了个开头。拖到 95% 附近"。
  - ② 缺 `RPG 攻击判定窗口` → "这一刀不会造成任何伤害"。
  - ③ `bExpectComboWindow && !bHasComboWindow` → "不是最后一段却没有 `RPG 连段衔接窗口` → 这一招接不下去"；`else if (bWarnOnUnexpectedComboWindow && bHasComboWindow)` → "是最后一段却有 `RPG 连段衔接窗口` → 可以无限连下去，终结技一般不该开"。
  - `Problems.Num() == 0` → 直接返回，注释："没问题的蒙太奇不输出任何东西 —— 否则每个资产都常年标黄，真正的警告就被淹没了。"
  - 否则拼出 `"标签（蒙太奇名，全长 %.2f 秒）"` + 每条问题前缀 `"\n  ✗ "` + `"\n  Notify 实际位置：%s"`，最后 `Context.AddWarning(FText::FromString(Report));`。
- `void ValidateHoldMontage(FDataValidationContext& Context, const UAnimMontage* Montage, const FText& Label)` —— 体检**"保持姿势"蒙太奇**（蓄力起手 / 蓄力循环），规则与攻击蒙太奇**相反**：一个通知都不该有。
  - 空蒙太奇直接返回（注释："留空合法：没有起手动画时角色直接保持当前姿势"）；`Length <= 0.f` → Warning 后返回。
  - 反向查：有 `AttackEnd` → "起手姿势刚摆好能力就结束了，蓄力机制形同虚设。这类蒙太奇不该有任何通知"；有 `AttackWindow` → "每次摆蓄力姿势都会白送一次伤害"；有 `ComboWindow` → "蓄力中会意外接出下一段"。
  - 有任一问题则拼报告，末尾追加 `"\n  该蒙太奇共 %d 个通知，正确的做法是 0 个"`（`Layout.TotalNotifyCount`），再 `AddWarning`。
  - **为什么单独写一个函数**：注释原文 —— "这类蒙太奇只是'摆个姿势然后停住等玩家松手'，**一个 Notify 都不该有**。如果误加了通知，后果不是'少打一下'而是'机制直接坏掉'…… 所以这里**反过来查**：发现任何本项目的通知就报问题。早先的版本把它丢进了通用的攻击规则里，结果是'资产配对了反而被标黄'，而照着提示改恰恰会把蓄力搞坏 —— 比不检查更糟。"
- **被谁调用**：仅 `URPG_AttackModuleData::IsDataValid()`。

#### 成员逐个列（`URPG_AttackModuleData`）

**模组标识**（Category = `RPG|Module`）：

| 成员 | 属性说明 | 类型 / 默认值 |
| --- | --- | --- |
| `ModuleType` | `EditDefaultsOnly, BlueprintReadOnly`。注释："攻击模组类型。三套模组共用同一套 GA，靠这个枚举区分行为"（该说明在 `RPG_CombatTypes.h` 的枚举处） | `ERPG_AttackModuleType = Unarmed` |
| `ModuleTag` | `EditDefaultsOnly, BlueprintReadOnly`。注释："模组标签，如 `Attack.Module.Unarmed`。用于运行时查询与日志" | `FGameplayTag` |
| `DisplayName` | `EditDefaultsOnly, BlueprintReadOnly`。注释："用于 UI 与调试的可读名称" | `FText` |

**伤害检测配置**：

| 成员 | 属性说明 | 类型 / 默认值 |
| --- | --- | --- |
| `TraceSource` | `EditDefaultsOnly, BlueprintReadOnly, Category = "RPG\|Trace"`。注释："检测源类型。三套模组的核心差异就在这里" | `ERPG_TraceSource = Hands` |
| `TraceRadius` | `EditDefaultsOnly, BlueprintReadOnly, Category = "RPG\|Trace", meta = (ClampMin = "1.0")`。注释："检测半径（厘米）。徒手一般 25~35（拳头大小），刀锋可以设小一点更精确（15~25）。太大容易'隔空打人'，太小会频繁穿模漏判" | `float = 30.f` |
| `LeftHandSocket` | `EditDefaultsOnly, BlueprintReadOnly, Category = "RPG\|Trace\|Hands"` | `FName = "hand_l"` |
| `RightHandSocket` | 同上 | `FName = "hand_r"` |
| `BladeStartSocket` | `EditDefaultsOnly, BlueprintReadOnly, Category = "RPG\|Trace\|Blade"`。注释："刀锋起点（靠近刀柄）"；上方还有一行说明"需要在武器 Mesh 上建这两个 Socket，或者在武器 Actor 上配好" | `FName = "TraceStart"` |
| `BladeEndSocket` | 同上，注释："刀锋终点（刀尖）" | `FName = "TraceEnd"` |
| `ProjectileClass` | `EditDefaultsOnly, BlueprintReadOnly, Category = "RPG\|Trace\|Projectile"` | `TSubclassOf<AActor>` |
| `MuzzleSocket` | 同上 | `FName = "Muzzle"` |

**攻击招式表**：

| 成员 | 属性说明 | 类型 / 默认值 |
| --- | --- | --- |
| `LightAttacks` | `EditDefaultsOnly, BlueprintReadOnly, Category = "RPG\|LightAttack"`。注释："5 段轻击。索引 0 是起手式" | `TArray<FRPG_AttackSegment>` |
| `HeavyAttack` | `EditDefaultsOnly, BlueprintReadOnly, Category = "RPG\|HeavyAttack"`。注释："重击（蓄力）配置" | `FRPG_HeavyAttackSet` |

**切手技**（Category = `RPG|Transition`）：

| 成员 | 属性说明 | 类型 / 默认值 |
| --- | --- | --- |
| `ComboTransitionMontage` | `EditDefaultsOnly, BlueprintReadOnly`。注释："切手技蒙太奇。留空则切手技不可用" | `TObjectPtr<UAnimMontage> = nullptr` |
| `ComboTransitionMultiplier` | `EditDefaultsOnly, BlueprintReadOnly, meta = (ClampMin = "0.0")`。注释："切手技伤害倍率。介于普通轻击和满蓄力重击之间比较合理" | `float = 2.5f` |
| `ComboTransitionStaminaCost` | 同上（`ClampMin = "0.0"`） | `float = 15.f` |

（`ComboTransitionMontage` 的消费者是 `URPG_GA_HeavyAttack`：`RPG_GA_HeavyAttack.cpp:220` 播放、`:226` 传给 `TrackCurrentStageMontage`。）

---

### `Source/RPG/Combat/RPG_AttackTypes.h`

**一句话职责**：攻击模组的数据结构定义（轻击段、蓄力等级、重击组）。

**类/结构体**：三个 `USTRUCT(BlueprintType)` —— `FRPG_AttackSegment`、`FRPG_HeavyAttackLevel`、`FRPG_HeavyAttackSet`。

#### 文件头部：为什么蒙太奇可以留空

注释原文 —— 每个 `Montage` 字段都允许为空，GA 侧会做空检查：没有蒙太奇就跳过动画播放，**但连段推进、伤害结算、耐力消耗全部照常执行**。这不是"偷懒"，而是有意的设计：① 数值链路（倍率对不对、伤害公式对不对）可以用日志独立验证，不必等动画做好；② 动画是表现层，把它变成数值逻辑的硬依赖会让调试变得很痛苦；③ 做动画时如果发现某个 Notify 位置不对，只影响表现，不会连带数值一起崩。

#### `FRPG_AttackSegment`（一个轻击段）

头部注释给出 5 段的默认倍率设计：1.0 / 1.15 / 1.4 / 1.6 / 2.0，"这个递增曲线让连段的'最后一段'有明确的收益感，鼓励玩家打完整套"。

| 成员 | 属性说明 | 类型 / 默认值 |
| --- | --- | --- |
| `Montage` | `UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation")`。注释："这一段的蒙太奇。留空时跳过动画，但逻辑照常执行" | `TObjectPtr<UAnimMontage> = nullptr` |
| `DamageMultiplier` | `UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Damage", meta = (ClampMin = "0.0"))`。注释："伤害倍率（相对攻击力）。`BaseDamage = Attack × 本值`" | `float = 1.0f` |
| `StaminaCost` | `UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Cost", meta = (ClampMin = "0.0"))`。注释："耐力消耗" | `float = 8.f` |
| `AttackTag` | `UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tag")`。注释："该段的攻击标签，用于日志区分与 GameplayEvent 过滤" | `FGameplayTag` |

#### `FRPG_HeavyAttackLevel`（一个蓄力等级）

头部注释给出三段蓄力的默认倍率：3.0 / 4.5 / 6.5，并解释设计意图 —— "蓄力越久收益越高，但蓄力期间持续掉耐力 —— 这是'风险与收益'的设计：贪满蓄力可能被敌人打断，也可能因为耐力耗尽而无法闪避。"

| 成员 | 属性说明 | 类型 / 默认值 |
| --- | --- | --- |
| `RequiredChargeTime` | `UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Charge", meta = (ClampMin = "0.0"))`。注释："达到该等级所需的蓄力时间（秒）。从按下右键开始计时，累积时间跨过这个阈值就升到这一级" | `float = 0.5f` |
| `ReleaseMontage` | `UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation")`。注释："松开右键释放时播放的攻击蒙太奇" | `TObjectPtr<UAnimMontage> = nullptr` |
| `DamageMultiplier` | `UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Damage", meta = (ClampMin = "0.0"))` | `float = 3.0f` |
| `StaminaCost` | `UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Cost", meta = (ClampMin = "0.0"))`。注释："释放时的耐力消耗" | `float = 15.f` |
| `ChargeLevelTag` | `UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tag")`。注释："该等级对应的标签（用于给角色挂 `State.Attack.Charging.LvN`）" | `FGameplayTag` |

#### `FRPG_HeavyAttackSet`（重击配置）

| 成员 | 属性说明 | 类型 / 默认值 |
| --- | --- | --- |
| `ChargeStartMontage` | `UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation")`。注释："起手蒙太奇：按下右键后播放，进入蓄力状态" | `TObjectPtr<UAnimMontage> = nullptr` |
| `ChargeLoopMontage` | `UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation")`。注释："蓄力中循环播放的蒙太奇（可选）。留空时停在起手蒙太奇的最后一帧 —— 取决于动画本身怎么设计，有些项目喜欢'蓄力时身体保持一个张力姿势'，那就留空" | `TObjectPtr<UAnimMontage> = nullptr` |
| `Levels` | `UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Charge")`。注释："三个蓄力等级，按 `RequiredChargeTime` 升序排列" | `TArray<FRPG_HeavyAttackLevel>` |
| `ChargeStaminaDrainPerSecond` | `UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Cost", meta = (ClampMin = "0.0"))`。注释："蓄力期间每秒消耗的耐力" | `float = 10.f` |
| `ChargeMoveSpeedScale` | `UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Charge", meta = (ClampMin = "0.0", ClampMax = "1.0"))`。注释："蓄力时的移动速度倍率。蓄力中通常应该走得慢一些（0.3 = 30% 速度）" | `float = 0.3f` |

（注意：`Levels` 的注释说"按 `RequiredChargeTime` 升序排列"，但 `URPG_AttackModuleData::GetChargeLevelForTime()` 的注释明确说"不能假设它一定严格升序"，两条注释并不矛盾 —— 前者是配置约定，后者是查询实现的健壮性处理。）

---

### `Source/RPG/Combat/RPG_CombatTypes.h`

**一句话职责**：战斗层公共类型定义 —— 三个枚举 + 一条缓存输入结构体，不依赖任何 GAS 类。

#### 文件头部说明

注释原文 —— 这个文件不依赖任何 GAS 类（只有 `GameplayTag` 这种轻量类型），所以 `Combat/` 目录下的东西可以独立编译、独立测试 —— **"输入缓存容器甚至不需要 Actor 就能跑单元测试"**。

#### `enum class ERPG_AttackModuleType : uint8`（`UENUM(BlueprintType)`）

注释："攻击模组类型。三套模组共用同一套 GA，靠这个枚举区分行为"。

| 枚举值 | DisplayName | 注释 |
| --- | --- | --- |
| `Unarmed` | 徒手 | "检测源是双手骨骼" |
| `Melee` | 近战武器 | "检测源是武器上的刀锋 Socket" |
| `Ranged` | 远程武器 | "不走轨迹检测，生成发射物" |

#### `enum class ERPG_TraceSource : uint8`（`UENUM(BlueprintType)`）

注释："伤害检测源 —— 决定 WeaponTrace 采样什么位置。前两种走'连续 Sweep'（防止高速挥砍时穿透目标），第三种交给发射物自己处理。"

| 枚举值 | DisplayName | 注释 |
| --- | --- | --- |
| `Hands` | 双手骨骼 | "采样 `hand_l` / `hand_r` 的位置，两帧之间连成线段做 Sweep" |
| `WeaponBlade` | 武器刀锋 | "采样武器 Mesh 上两个 Socket 之间的线段" |
| `Projectile` | 发射物 | "生成 Projectile，由它自己的碰撞回调施加伤害" |

#### `enum class ERPG_InputBufferMode : uint8`（`UENUM(BlueprintType)`）

注释："输入缓存的取出策略。单键连打时两者完全等价（都是同一个标签）；差异只在混合输入（比如连按'左左右'）时体现。"

| 枚举值 | DisplayName | 注释 |
| --- | --- | --- |
| `Stack` | 栈 LIFO（尊重最新意图） | "移除最后压入的 —— 尊重玩家最新的意图" |
| `Queue` | 队列 FIFO（按顺序执行） | "移除最早压入的 —— 按输入顺序依次执行" |

#### `FRPG_BufferedInput`（`USTRUCT(BlueprintType)`）

注释："一条被缓存的输入意图。带时间戳是因为'输入缓存'必须有过期机制：玩家在硬直里乱按的按键，如果无限期保留，等硬直结束会突然打出一串招式，手感非常糟。生命周期太短又会'不跟手'——玩家在动画后半段按的下一段会丢。0.4~0.6 秒是动作游戏里比较舒服的区间。"

| 成员 | 属性说明 | 类型 / 默认值 |
| --- | --- | --- |
| `InputTag` | `UPROPERTY(BlueprintReadOnly, Category = "RPG\|Input")`。注释："输入标签，形如 `Input.Attack.Light`" | `FGameplayTag` |
| `Timestamp` | `UPROPERTY(BlueprintReadOnly, Category = "RPG\|Input")`。注释："压入时的世界时间" | `float = 0.f` |
| `LifeTime` | `UPROPERTY(BlueprintReadOnly, Category = "RPG\|Input")`。注释："生命周期（秒），超过就作废" | `float = 0.5f`（注意运行时会用 `URPG_CombatComponent::InputLifeTime` 覆盖） |

- `bool IsExpired(float Now) const` —— 内联实现 `return (Now - Timestamp) > LifeTime;`。注释只写了"是否过期"，边界语义是**严格大于**（恰好等于 `LifeTime` 时仍算有效）。**被谁调用**：`URPG_InputBuffer::PruneExpired()` 的 `RemoveAll` 谓词。

---

### `Source/RPG/Combat/RPG_AttackWindowPayload.h`

**一句话职责**：AnimNotify 广播"伤害判定窗口开启"时携带的数据载体。

**类/结构体**：`URPG_AttackWindowPayload` —— 继承 `UObject`，`UCLASS()`。**没有对应的 .cpp**（纯数据声明，无实现）。

#### 头文件里的两段设计说明

- **【为什么要用 UObject 包装】**：GameplayEvent 的载荷类型 `FGameplayEventData` 是引擎定义的固定结构，只提供 `OptionalObject` / `OptionalObject2` 这两个通用槽位来放自定义数据。想传结构化的信息就只能包成 UObject 塞进 `OptionalObject`。
- **【为什么这里只有"检测参数"，没有伤害倍率】**：检测参数（半径、Socket 名）配在动画通知上，是因为它们**本质上由动画决定** —— "这一刀挥出去手在哪个位置、扫过多大范围，看的是动画本身"。伤害倍率则相反，它属于**数值设计**，应该集中在 `AttackModuleData` 里管理 —— "改个倍率不该需要打开动画资产"。所以倍率由 GA 从攻击模组读，不从这里传。

#### 成员逐个列

| 成员 | 属性说明 | 类型 / 默认值 |
| --- | --- | --- |
| `AttackTag` | `UPROPERTY(BlueprintReadOnly, Category = "RPG\|Attack")`。注释："这一段攻击的标签，用于日志区分与事件过滤" | `FGameplayTag` |
| `bOverrideTrace` | 同上。注释："是否用下面的参数覆盖攻击模组里的检测配置" | `bool = false` |
| `TraceSource` | 同上 | `ERPG_TraceSource = ERPG_TraceSource::Hands` |
| `TraceRadius` | 同上 | `float = 30.f` |
| `SocketStart` | 同上。注释："检测线段的起点 Socket（双手检测时为左手，刀锋检测时为刀柄）" | `FName` |
| `SocketEnd` | 同上。注释："检测线段的终点 Socket（双手检测时为右手，刀锋检测时为刀尖）" | `FName` |

**被谁创建 / 被谁消费**：创建者是 `URPG_AnimNotifyState_AttackWindow`（`RPG_AnimNotifyState_AttackWindow.cpp:35` 用 `NewObject<URPG_AttackWindowPayload>(MeshComp)`，随后填 `TraceSource` 等字段）；消费者是 `URPG_GA_LightAttack`（`RPG_GA_LightAttack.cpp:610`，从 `Payload.OptionalObject.Get()` Cast 出来，若 `bOverrideTrace` 则用窗口参数覆盖 `Module->TraceSource`）与 `URPG_GA_HeavyAttack`（`RPG_GA_HeavyAttack.cpp:630`，同样的模式）。

---

### `Source/RPG/AI/RPG_AIController.h` / `Source/RPG/AI/RPG_AIController.cpp`

**一句话职责**：敌人的大脑 —— 配置感知（视觉 / 听觉）、把感知结果写进黑板、启动行为树、并在死亡 / 重生时启停 AI。

**类/结构体**：`ARPG_AIController` —— 继承 `AAIController`，`UCLASS()`。

#### 头文件里的三段设计说明

- **【它负责什么，不负责什么】**：负责感知配置（能不能看到玩家）、把感知结果写进黑板（看到了谁、在哪看到的）、启动行为树（开始思考）。不负责：决策逻辑本身（→ 行为树资产 `BT_RPG_Enemy`）、具体动作怎么做（→ 各个 `BTTask`）、攻击怎么打（→ **复用玩家那一套 GA**）。
- **【★ AI 和玩家走的是同一条能力链路】**：注释原话 —— "这是本项目 AI 设计里最重要的一条：**敌人不另写一套攻击逻辑**。"
  ```
  玩家：按键 → Input.Attack.Light → ASC 映射表 → GA_LightAttack
  敌人：BTTask → Input.Attack.Light → ASC 映射表 → GA_LightAttack
                 ↑ 完全相同的入口
  ```
  好处是"玩家能放、AI 放不出来"这类问题根本不会出现 —— 因为它们根本没有两条路径可以不一致。代价是 AI 也得往 `CombatComponent` 的输入缓存里推一个标签，"这个代价其实是收益：连段、输入缓存、耐力消耗全部自动对 AI 生效。"
- **【阵营：为什么构造函数里要把三个检测开关全打开】**：见 .cpp 里的详细说明（下次节），注释称这是"'AI 站着不动不看你'的头号原因"。

#### `ARPG_AIController()`

- **干什么**：建感知组件与两个感官配置，全部参数从本类的 `UPROPERTY` 灌入，然后注册回调。
- **关键实现**（逐段）：
  1. `PerceptionComponent = CreateDefaultSubobject<UAIPerceptionComponent>(TEXT("PerceptionComponent"));` —— 注释解释：`AAIController` 的构造函数里**只**创建了 `PathFollowingComponent`，`PerceptionComponent` 是个留给使用者填的公开成员（默认是空的）。不建它的话，后面所有 `ConfigureSense` 都是对空指针操作。
  2. **视觉**：`SightConfig = CreateDefaultSubobject<UAISenseConfig_Sight>(...)`，随后设置 `SightRadius` / `LoseSightRadius` / `PeripheralVisionAngleDegrees` / `SetMaxAge(SightMaxAge)` / `AutoSuccessRangeFromLastSeenLocation` —— 全部取自同名 `UPROPERTY`。
  3. **★ 阵营三开关**：`SightConfig->DetectionByAffiliation.bDetectEnemies / bDetectNeutrals / bDetectFriendlies` 全部置 `true`。理由链在注释里写全了（**查证自引擎源码，行号照抄**）：
     - ① `FAISenseAffiliationFilter` 的三个开关**默认全是 false**（`AIPerceptionTypes.h:218-224`），而 `UAISenseConfig` 和 `UAISenseConfig_Sight` 的构造函数都没有改过它们（`AISense.cpp:118-121`、`175-178`）。
     - ② 感知系统用 `GetAsFlags()` 把这三个开关压成一个位掩码，全 false → 掩码 = 0。
     - ③ 判定在 `FAISenseAffiliationFilter::ShouldSenseTeam()`：`return AffiliationFlags == AllFlags || ((1 << 态度) & AffiliationFlags);` —— 掩码为 0 时后面那一项永远是 0 → **任何阵营都感知不到**。
     - ④ 更绕的是"态度"怎么算。默认求解器是 `return A != B ? Hostile : Friendly;`（`AIInterfaces.cpp:30-33`），而 `AAIController` 和玩家默认都是 `FGenericTeamId::NoTeam`（255），255 == 255 → 算出 **Friendly**，不是 Neutral。所以只开 `bDetectNeutrals` 也没用。
     - 结论（注释原话）："在没有队伍系统的情况下，三个全开是最省事也最不会错的做法。将来真的做了阵营（比如'敌人之间会互相打'），再按需收紧。"症状提示："这三个开关忘了开的表现是'敌人站着不动、完全不看你'，而且**不报任何错** —— 感知系统认为一切正常，只是'没发现东西'。"
  4. **听觉**：`HearingConfig = CreateDefaultSubobject<UAISenseConfig_Hearing>(...)`，设 `HearingRange`，`SetMaxAge(SightMaxAge)`（**注意：听觉的 MaxAge 也用的是 `SightMaxAge` 这个属性**，不是单独的听觉属性），三个阵营开关同样全开（注释："同样要开 —— 理由同上"）。
  5. `PerceptionComponent->ConfigureSense(*SightConfig);` / `ConfigureSense(*HearingConfig);`
  6. `PerceptionComponent->SetDominantSense(SightConfig->GetSenseImplementation());` —— 注释："主感知设为视觉：当多个感官同时报告同一个目标时，以视觉的信息为准（比如'听到脚步'只知道大致方位，'看到人'知道精确位置）。"
  7. `PerceptionComponent->OnTargetPerceptionUpdated.AddDynamic(this, &ARPG_AIController::OnTargetPerceptionUpdated);` —— 注释："感知状态变化时回调。注意这里是 `AddDynamic` —— 委托是动态多播，回调必须是 `UFUNCTION`。"
  8. 末尾一段注释说明：`AAIController` 默认 `bStartAILogicOnPossess = false`，"我们自己在 `OnPossess` 里显式启动行为树 —— 这样能先校验资产配好了没有，而不是让引擎静默地什么都不做。"
- **被谁调用 / 调用谁**：UObject 构造流程；调用 `UAIPerceptionComponent::ConfigureSense()` / `SetDominantSense()`。

#### `virtual void BeginPlay() override`

- **干什么**：`Super::BeginPlay();` 后调用 `ApplyPerceptionSettings();`
- **被谁调用 / 调用谁**：引擎；调用本类 `ApplyPerceptionSettings()`。

#### `void ApplyPerceptionSettings()`

- **干什么**：把蓝图里配的感知参数重新同步到感知配置对象上，并通知感知系统更新登记。
- **关键实现**：
  1. `if (!PerceptionComponent || !SightConfig || !HearingConfig) return;` 三空检查。
  2. 视觉：`SightRadius` / `LoseSightRadius` / `PeripheralVisionAngleDegrees` / `SetMaxAge(SightMaxAge)` / `AutoSuccessRangeFromLastSeenLocation` 五项重新赋值。
  3. 听觉：`HearingRange` / `SetMaxAge(SightMaxAge)`。
  4. `PerceptionComponent->ConfigureSense(*SightConfig);`、`ConfigureSense(*HearingConfig);`、`RequestStimuliListenerUpdate();`
  5. `UE_LOG(LogRPG_AI, Log, TEXT("[%s] 感知已配置：视觉 %.0f/丢失 %.0f 半径、%.0f° 半角；听觉 %.0f"), *GetName(), SightRadius, LoseSightRadius, PeripheralVisionAngleDegrees, HearingRange);`
- **为什么这么写**：注释给了一个具体的踩坑例子 —— "构造函数只在**类默认对象**创建时执行一次。你在 `BP_RPG_Enemy` 里把 `SightRadius` 从 1500 改成 2500，**不会**重新执行构造函数 —— 那时 `SightConfig` 上的值仍然是 1500。这和 `ARPG_BaseCharacter` 里同步相机 / 移动参数是同一个坑，凡是'UPROPERTY 的值要影响一个子对象'就必须在运行时同步一次。"另外注释还指出"改完配置要通知感知系统重新登记，否则改动不生效"。
- **被谁调用 / 调用谁**：本类 `BeginPlay()`；调用 `UAIPerceptionComponent::ConfigureSense()` / `RequestStimuliListenerUpdate()`。

#### `virtual void OnPossess(APawn* InPawn) override`

- **干什么**：校验行为树资产 → 启动行为树 → 初始化黑板，每一步失败都打明确日志。
- **关键实现**：
  1. `Super::OnPossess(InPawn);`
  2. `if (!BehaviorTreeAsset)` → `UE_LOG(LogRPG_AI, Error, ...)`（"没有配置 Behavior Tree Asset —— 这个 AI 不会做任何事。在 BP_RPG_Enemy 的 AIController 类上挂 BT_RPG_Enemy"）后 `return`。
  3. `if (!RunBehaviorTree(BehaviorTreeAsset))` → `UE_LOG(LogRPG_AI, Error, ...)`（"行为树启动失败。最常见的原因是行为树资产没有设置 Blackboard Asset"）后 `return`。
  4. `InitializeBlackboardValues();`
  5. `UE_LOG(LogRPG_AI, Log, TEXT("[%s] 行为树已启动：%s"), *GetName(), *BehaviorTreeAsset->GetName());`
- **为什么这么写**：注释在 `RunBehaviorTree` 前 —— "`RunBehaviorTree` 会顺便根据行为树资产里引用的黑板资产创建 Blackboard 组件。所以**必须**先跑行为树，再写黑板 —— 反过来 Blackboard 还是空的。"
- **被谁调用 / 调用谁**：引擎在 Possess 时调用；调用 `AAIController::RunBehaviorTree()`（引擎）、本类 `InitializeBlackboardValues()`。

#### `void InitializeBlackboardValues()`

- **干什么**：行为树跑起来之后把初始值写进黑板。
- **关键实现**：
  1. `if (!Blackboard || !GetPawn()) return;`
  2. `Blackboard->SetValueAsVector(RPGBlackboardKeys::HomeLocation, GetPawn()->GetActorLocation());` —— 注释："用出生点而不是'巡逻点 0' —— 有些敌人可能根本没配巡逻点。"
  3. `if (const ARPG_Enemy* Enemy = Cast<ARPG_Enemy>(GetPawn())) { Blackboard->SetValueAsFloat(RPGBlackboardKeys::AttackRange, Enemy->AttackRange); }` —— 注释："这样行为树节点不需要知道它服务的具体是哪个角色类，将来的'远程敌人'只要在蓝图里把这个值改大就行。"
  4. `SetValueAsBool(bInCombat, false)`、`SetValueAsBool(bTargetVisible, false)`、`SetValueAsInt(PatrolIndex, 0)` —— 注释："初始状态：不在战斗、没看到任何东西"。
  5. **注意**：本函数**不**写 `LastKnownLocation` / `PatrolLocation` / `TargetActor`。
- **被谁调用 / 调用谁**：本类 `OnPossess()` 与 `RestartAI()`；读 `ARPG_Enemy::AttackRange`。

#### `void OnTargetPerceptionUpdated(AActor* Actor, FAIStimulus Stimulus)`

- **干什么**：感知更新回调 —— 把"看到 / 丢失"翻译成黑板值 + 战斗状态切换。
- **关键实现**（逐分支，顺序即代码顺序）：
  1. `if (!Actor || !Blackboard) return;`
  2. **排除自己**：`if (Actor == GetPawn()) return;` —— 注释："感知系统默认会把自身也纳入检测范围，不排除掉的话 AI 会追着自己跑。"
  3. **★ 死了就不再响应感知**：`if (const ARPG_BaseCharacter* SelfChar = Cast<ARPG_BaseCharacter>(GetPawn())) { if (!SelfChar->IsAlive()) return; }`。注释给了完整因果链 —— 感知组件不会因为角色死亡就停止工作，玩家从尸体旁边经过它照样会回调这里，然后往黑板写"我看到目标了"、`SetCombatState(true)` 给尸体挂上 `State.Sprinting` 并改 `MaxWalkSpeed`；"前者的后果最阴险：**复活时黑板上还留着'正在战斗'**，AI 一起来就直奔玩家，看起来像是有前世记忆。虽然 `RestartAI()` 里会清一遍黑板，但那属于'事后补救'——在源头挡住更省事，也省掉死亡期间每帧一次的无效回调。"并说明为什么用 `IsAlive()` 而不是查 `State.Dead` 标签 —— "接口层已经封装了'ASC 还没初始化时按存活处理'这个边界，直接用即可。"
  4. **只对"有 GAS 的角色"起反应**：`if (!Cast<ARPG_BaseCharacter>(Actor)) return;` —— 注释："地上的石头、门之类的 Actor 不该引起战斗状态。"
  5. **成功感知分支** `Stimulus.WasSuccessfullySensed()`：
     - `SetValueAsObject(TargetActor, Actor)`
     - `SetValueAsVector(LastKnownLocation, Actor->GetActorLocation())` —— 注释："★ 这里每次都更新'最后已知位置' —— 它和 `TargetActor` 是两回事：目标丢失时 `TargetActor` 会被清掉，但这个位置要留到最后。"
     - `SetValueAsBool(bTargetVisible, true)`
     - `SetCombatState(true);` —— 注释："★ 立刻进入战斗状态（黑板标记 + 移动速度一起切）。这一步是'玩家一进视野敌人就追'的关键时机：感知回调在**刚看到的那一瞬间**就触发了，比行为树的任何轮询都早，也比 `BTService` 的 0.2 秒周期早。放在这里才叫'立即'。"
     - `UAIPerceptionSystem::GetSenseClassForStimulus(this, Stimulus)` 取感官类型，Verbose 日志 `"[%s] 感知到目标：%s（来自 %s）"`（注释："顺带打出是哪个感官发现的 —— 排查'AI 靠听觉还是视觉找到我'时很有用"）。
  6. **丢失感知分支**（`else`）：只 `SetValueAsBool(bTargetVisible, false)` + Verbose 日志。注释给出关键理由 —— "注意这里**不清 `TargetActor`**，只标记'此刻看不见'。为什么：丢视野是一个瞬间事件（被柱子挡了一下），而'脱战'是一个持续判断（连续 N 秒都没再看到）。如果在这里就把目标清掉，AI 会变得极度健忘 —— 玩家绕着树跑一圈敌人就回去巡逻了。真正的清除交给 `BTService_CombatUpdate`：它盯着 `bTargetVisible`，连续为 false 超过阈值才清 `TargetActor` 和 `bInCombat`。"
- **被谁调用 / 调用谁**：由 `PerceptionComponent->OnTargetPerceptionUpdated` 动态多播委托调用（构造函数里 `AddDynamic` 注册）；调用 `ARPG_BaseCharacter::IsAlive()`、本类 `SetCombatState()`、`UAIPerceptionSystem::GetSenseClassForStimulus()`。
- **头文件里额外提醒的触发时机**：`⚠️ 注意它的触发时机：这个函数**不是每帧调用**的，只在'某个 Actor 的感知状态发生变化'时调用 —— 发现时调一次、丢失时调一次。所以不能在这里做'每帧检查距离'这种事。`

#### `void SetCombatState(bool bInCombat)`

- **干什么**：进入 / 退出战斗状态。★ 所有改变战斗状态的路径都必须走这里。
- **关键实现**：
  1. `if (Blackboard) { Blackboard->SetValueAsBool(RPGBlackboardKeys::bInCombat, bInCombat); }` —— 注意**不提前 return**。
  2. `if (ARPG_BaseCharacter* RPGChar = Cast<ARPG_BaseCharacter>(GetPawn())) { RPGChar->SetCombatMovement(bInCombat); }`
  3. Verbose 日志 `"[%s] %s战斗状态"`，`bInCombat ? TEXT("进入") : TEXT("退出")`。
- **为什么这么写**：注释给了两点 —— ① 统一入口的理由："'在不在战斗'这件事会同时影响**两处**：黑板的 `bInCombat` 键（行为树的根选择器靠它选分支）、角色的 `MaxWalkSpeed`（战斗时跑起来）。如果两处各改各的（比如感知回调改黑板、服务改速度），迟早会出现'黑板说在战斗、人还在慢悠悠走'这种不一致 —— 而它看起来像寻路问题，查起来会绕很远。收在一个入口里，两边就不可能不同步。" ② 不提前 return 的理由："即使黑板值没变，也要往下走把移动速度同步一遍。因为这两处可能因为某条路径的疏漏而不同步，每次调用都当成一次'校正'比省几次写入更划算。" ③ 局部变量命名的理由："局部变量不能叫 `Character` —— `AController` 自带一个同名成员，重名会报 C4458（而且它指向的确实是同一个 Pawn，很容易误用成那个）。"
- **被谁调用 / 调用谁**：`OnTargetPerceptionUpdated()`（成功分支传 true）、`StopAI()`（false）、`RestartAI()`（false）、`URPG_BTService_CombatUpdate::TickNode()`（无目标兜底，false）、`URPG_BTService_CombatUpdate::ClearTarget()`（false）。调用 `ARPG_BaseCharacter::SetCombatMovement()`。

#### `AActor* GetTargetActor() const`

- **干什么**：当前锁定的目标，没在战斗时为 `nullptr`。
- **关键实现**：`return Blackboard ? Cast<AActor>(Blackboard->GetValueAsObject(RPGBlackboardKeys::TargetActor)) : nullptr;`
- **被谁调用 / 调用谁**：`UFUNCTION(BlueprintPure, Category = "RPG|AI")`。C++ 侧未找到调用点（供蓝图使用）。

#### `FVector GetLastKnownLocation() const`

- **干什么**：目标最后被看到的位置。
- **关键实现**：`return Blackboard ? Blackboard->GetValueAsVector(RPGBlackboardKeys::LastKnownLocation) : FVector::ZeroVector;`
- **被谁调用 / 调用谁**：`UFUNCTION(BlueprintPure, Category = "RPG|AI")`。C++ 侧未找到调用点（供蓝图使用）。

#### `void StopAI()`

- **干什么**：停止思考 —— 清目标、停寻路、停行为树。**死亡时调用。**
- **关键实现**（四步，注释逐条给了理由）：
  1. `StopMovement();` —— "① 停寻路。注意 `StopMovement` 只清掉**当前的**移动请求；如果不清，`PathFollowingComponent` 会在角色已经进入布娃娃（移动模式 = `MOVE_None`）之后继续尝试重算路径，每帧失败。"
  2. `ClearFocus(EAIFocusPriority::Gameplay);` —— "② 清焦点。不清的话，AI 的'注视'会继续把尸体往目标方向扭。"
  3. `SetCombatState(false);` —— "③ 退出战斗状态。不做这一步的话，尸体身上会一直挂着 `State.Sprinting` 这个 loose tag，`MaxWalkSpeed` 也停在 `CombatMoveSpeed` —— 因为死亡流程里没有人会去清它。默认 `RespawnDelay = 0` 的敌人更是永远不清。对布娃娃来说这看着无害（网格由物理驱动，不读速度），但任何**靠 `State.Sprinting` 判断状态**的东西都会看到假信息：动画蓝图的 `MovementState`、将来可能有的 UI、调试面板。让它在源头停下来，比让下游各自判断'这个是不是尸体'便宜得多。"
  4. `if (BrainComponent) { BrainComponent->StopLogic(TEXT("角色死亡")); }` —— "④ 停行为树。`StopLogic(Safe)` 会让当前正在跑的分支走 Abort 流程 —— 那些 Latent Task（比如 `BTTask_Attack`）会收到 `AbortTask` 回调，有机会清理自己。这正是 `StopLogic` 比'直接不管'好的地方。"
  5. `UE_LOG(LogRPG_AI, Log, TEXT("[%s] AI 已停止（角色死亡）"), *GetName());`
- **为什么"必须由死亡显式调用"**：头文件给了一段完整说明 —— 敌人血量归零后行为树**不会自己停下来**（它没有"我死了"这个概念），还会继续写黑板、发起寻路、尝试激活攻击能力；而这时角色的移动模式已经被布娃娃关掉了（`MOVE_None`），于是每次寻路都失败。表现上尸体躺着不动看不太出来，但：行为树会卡在一个永远不结束的 Latent Task 里、每帧都有失败日志、复活时行为树的状态是脏的 AI 会僵住。"所以停 AI 这件事必须有人明确地做一次。做的人是 `GA_Death` —— 它是死亡流程的编排者，敌人通过 `OnDeathStarted()` 钩子转达。"
- **被谁调用 / 调用谁**：`ARPG_Enemy::OnDeathStarted()`（`RPG_Enemy.cpp:62`），而后者由 `URPG_GA_Death` 通过基类钩子调用（`RPG_GA_Death.cpp:104`，注释说明"只在服务器发生"）。调用 `AAIController::StopMovement()` / `ClearFocus()`、本类 `SetCombatState()`、`UBrainComponent::StopLogic()`。

#### `void RestartAI()`

- **干什么**：恢复思考 —— 重启行为树并把战斗状态复位。**重生时调用。**
- **关键实现**（四步）：
  1. `if (Blackboard) { Blackboard->ClearValue(TargetActor); ClearValue(bTargetVisible); SetValueAsVector(LastKnownLocation, FVector::ZeroVector); }` —— 注释："① 清掉上一条命留下的记忆。不清的话，复活瞬间 AI 会带着'我上次在追玩家'的状态醒过来 —— 表现是刚站起来就直奔玩家的旧位置，像是有前世记忆。"
  2. `SetCombatState(false);` —— "② 走统一入口复位战斗状态（黑板 `bInCombat` + 移动速度一起）。"
  3. `if (BrainComponent) { BrainComponent->RestartLogic(); }` —— "③ 重启行为树。`RestartLogic` 内部会处理'树曾被 `StopLogic` 停过'这种情况（`BehaviorTreeComponent.cpp:448` 的 `RestartTree` 会看 `bRequestedStop` 标志），不需要我们手动 `RunBehaviorTree` 重建一遍。"
  4. `InitializeBlackboardValues();` —— "④ 把出生点和攻击距离重新写一遍 —— 复活位置可能和最初不同。"
  5. `UE_LOG(LogRPG_AI, Log, TEXT("[%s] AI 已恢复（角色重生）"), *GetName());`
- **为什么这么写**：头文件补充 —— "必须和 `StopAI()` 配对 —— 只停不重启的话，敌人复活后会站在原地一动不动（行为树根本没在跑），而且不报任何错。"
- **被谁调用 / 调用谁**：`ARPG_Enemy::OnRespawned()`；调用 `UBlackboardComponent::ClearValue()` / `SetValueAsVector()`、本类 `SetCombatState()` / `InitializeBlackboardValues()`、`UBrainComponent::RestartLogic()`。

#### 成员逐个列（`ARPG_AIController`）

组件区（注释说明：**感知组件不用在这里声明** —— `AAIController` 已经有一个公开的 `PerceptionComponent` 成员，直接用继承来的那个；"重复声明会在 UHT 阶段报 shadowing 错误，和 `UGameplayAbility::CurrentMontage` 一样"）：

| 成员 | 属性说明 | 类型 |
| --- | --- | --- |
| `SightConfig` | `UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RPG\|AI\|Perception")`。注释："视觉配置。挂在感知组件上" | `TObjectPtr<UAISenseConfig_Sight>` |
| `HearingConfig` | 同上。注释："听觉配置" | `TObjectPtr<UAISenseConfig_Hearing>` |

配置区：

| 成员 | 属性说明 | 类型 / 默认值 |
| --- | --- | --- |
| `BehaviorTreeAsset` | `UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG\|AI")`。注释："行为树资产。留空的话 AI 会站着不动（并打一条 Error 日志）。放在 AIController 上而不是敌人身上：行为树描述的是'这个大脑怎么想'，换一个敌人模型不需要换大脑" | `TObjectPtr<UBehaviorTree>` |
| `SightRadius` | `UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG\|AI\|Perception", meta = (ClampMin = "0.0"))`。注释："能看见目标的距离（厘米）。1000 = 10 米" | `float = 1500.f` |
| `LoseSightRadius` | 同上。注释："丢失视野的距离。应该比 `SightRadius` 大 —— 两者相等会导致目标在边界上反复'看见/看不见'" | `float = 2000.f` |
| `PeripheralVisionAngleDegrees` | `... meta = (ClampMin = "0.0", ClampMax = "180.0")`。注释："视野半角（度）。45 = 总共 90 度的视野" | `float = 45.f` |
| `SightMaxAge` | `... meta = (ClampMin = "0.0")`。注释："目标被挡住后，视觉信息保留多久（秒）" | `float = 5.f`（**同时被用作听觉的 MaxAge**） |
| `AutoSuccessRangeFromLastSeenLocation` | 同上。注释："目标消失后，在这个距离内 AI 会'自动认为还看得见'（用于绕过墙角时的容错）" | `float = 900.f` |
| `HearingRange` | `... Category = "RPG\|AI\|Perception"`。注释："听觉半径（厘米）" | `float = 1200.f` |
| `LoseTargetAfterSeconds` | `... meta = (ClampMin = "0.0")`。注释："目标丢失后多久算脱战（秒）。超过这个时间还没重新看到目标 → 清掉 `TargetActor`，回到巡逻" | `float = 6.f` |

> 注意：`LoseTargetAfterSeconds` 在这个类里**只有声明和默认值，C++ 侧没有任何读取点**（全库检索确认）；实际的脱战计时由 `URPG_BTService_CombatUpdate` 自己的同名属性负责。两个默认值恰好都是 6.f。

---

### `Source/RPG/AI/RPG_BlackboardKeys.h` / `Source/RPG/AI/RPG_BlackboardKeys.cpp`

**一句话职责**：黑板键名的集中定义（`namespace RPGBlackboardKeys` 下的 `extern const FName` 常量），把"FName 拼错编译期不报错"这个坑堵死。

**类/结构体**：无类，只有命名空间与常量。

#### 头文件里的两段设计说明

- **【为什么需要这个文件】**：黑板读写全靠 `FName`，而 **FName 拼错编译期不报错** —— 注释给了一个例子 `Blackboard->GetValueAsObject(FName("TargetActro")); // 少了个 e`，"这行代码会安安静静地返回 `nullptr` —— 表现出来就像'AI 没发现玩家'，你会去查感知配置、查碰撞通道、查阵营设置，就是想不到是键名拼错了。"和 GameplayTag 用 C++ 声明（而不是纯 ini）是同一个理由：把它变成真正的 C++ 变量，拼错就编译不过。
- **【和编辑器的一致性】**：在 `BB_RPG_Enemy` 黑板资产里建键时，名字必须和这里**逐字一致**（区分大小写）。类型也必须对得上 —— "键存在但类型不对时，`SetValueAsXxx` 会静默失败"。键名和类型的对照表见 `Docs/PHASE5_AI_SETUP.md`。
- 关于 `SelfActor`：注释说明它用引擎自带的 `FBlackboard::KeySelf`，**不在本文件重复定义** —— 引擎在建黑板资产时会自动加上这个键；需要时 `#include "BehaviorTree/Blackboard/BlackboardKey.h"` 后用 `FBlackboard::KeySelf`。

#### `.cpp` 里的实现方式与理由

```cpp
const FName TargetActor(TEXT("TargetActor"));
// ... 其余 7 个同形
```

注释给出的理由 —— "定义在这里（而不是头文件里 inline）是 UE 的惯例：`FName` 的构造依赖名字池，全局构造的顺序有讲究，放在 .cpp 里由模块静态初始化负责，比头文件里的 inline 变量更稳妥。引擎自己定义 `FBlackboard::KeySelf` 用的也是这个写法。"

#### 常量逐个列（含类型、写入方、读取方 —— 全部来自实际代码检索）

| 常量 | FName 字面量 | 黑板类型 | 谁写 | 谁读 |
| --- | --- | --- | --- | --- |
| `TargetActor` | `"TargetActor"` | Object | `ARPG_AIController::OnTargetPerceptionUpdated()`（成功时设为目标）；清空于 `RestartAI()`、`URPG_BTService_CombatUpdate::ClearTarget()` | `URPG_BTDecorator_CanAttack::CalculateRawConditionValue()`、`URPG_BTService_CombatUpdate::TickNode()`、`URPG_BTTask_MoveToTarget::PrepareMove()`、`ARPG_AIController::GetTargetActor()` |
| `LastKnownLocation` | `"LastKnownLocation"` | Vector | `OnTargetPerceptionUpdated()`（每次成功感知都刷新）、`URPG_BTService_CombatUpdate::TickNode()`（可见时持续刷新）、`RestartAI()`（置零） | `URPG_BTTask_MoveToTarget::PrepareMove()`、`ARPG_AIController::GetLastKnownLocation()` |
| `bTargetVisible` | `"bTargetVisible"` | Bool | `OnTargetPerceptionUpdated()`（true/false）、`InitializeBlackboardValues()`（false）、`ClearTarget()`（false） | `URPG_BTService_CombatUpdate::TickNode()`、`URPG_BTTask_MoveToTarget::IsTargetVisible()` |
| `HomeLocation` | `"HomeLocation"` | Vector | `ARPG_AIController::InitializeBlackboardValues()`（写入 Pawn 出生点） | `URPG_BTTask_Patrol::PrepareMove()`（没有巡逻点时的回落点） |
| `PatrolLocation` | `"PatrolLocation"` | Vector | `URPG_BTTask_Patrol::PrepareMove()` | C++ 侧无读取点（写入目的是让编辑器/调试时能看到当前巡逻目标） |
| `PatrolIndex` | `"PatrolIndex"` | Int | `InitializeBlackboardValues()`（0）、`URPG_BTTask_Patrol::PrepareMove()`（环形自增后写回） | `URPG_BTTask_Patrol::PrepareMove()` |
| `AttackRange` | `"AttackRange"` | Float | `InitializeBlackboardValues()`（从 `ARPG_Enemy::AttackRange` 读出） | `URPG_BTDecorator_CanAttack::CalculateRawConditionValue()` |
| `bInCombat` | `"bInCombat"` | Bool | `ARPG_AIController::SetCombatState()`（唯一正规入口）、`InitializeBlackboardValues()`（false）；两条兜底路径也直接写：`URPG_BTService_CombatUpdate::TickNode()` 与 `ClearTarget()` 中当 `AIOwner` 不是 `ARPG_AIController` 时 | `URPG_BTService_CombatUpdate::TickNode()`（还有行为树资产的根选择器） |

两个键的语义差别在注释里被反复强调：`TargetActor` 是"锁定"，丢了要能撑一段时间；`LastKnownLocation` 是"记忆"，**丢目标时不清**（`ClearTarget()` 里明确只清 `TargetActor` 与 `bTargetVisible`）。`bTargetVisible` 与 `TargetActor` 的区别是"这个是即时的"。

---

### `Source/RPG/AI/Decorators/RPG_BTDecorator_CanAttack.h` / `Source/RPG/AI/Decorators/RPG_BTDecorator_CanAttack.cpp`

**一句话职责**："现在能不能打" —— 行为树的分支条件（够得着 + 双方存活）。

**类/结构体**：`URPG_BTDecorator_CanAttack` —— 继承 `UBTDecorator`，`UCLASS(meta = (DisplayName = "RPG 可以攻击"))`。

#### 头文件里的两段设计说明

- **【装饰器（Decorator）的两种用法】**：挂在 `Sequence` 上 → 决定**要不要进入这个分支**；挂在节点上 → 进入后持续监控，条件不满足就**打断（Abort）**。本装饰器用在第一种："攻击分支只在'够得着 + 活着'时才被选中，够不着就让下面的'追击'分支接手。"
- **【为什么直接算距离，而不从黑板读】**：注释原话 —— "距离是**每帧都在变**的。如果由服务每 0.2 秒写一次黑板，装饰器读到的就是最多 0.2 秒前的旧值 —— 玩家快速冲刺时会出现'明明已经跑到面前了 AI 还在往前追'（读到的还是 0.2 秒前的远距离）。`CalculateRawConditionValue` 每次条件评估都会被调用，当场算最准。两次减法 + 一次平方和，比查黑板还便宜。而 `AttackRange` 那种**很少变**的值就适合放黑板 —— 按变化频率决定数据放哪里，是个挺有用的判断标准。"

#### `URPG_BTDecorator_CanAttack()`

- **干什么**：设节点名，并把默认的 Flow Abort 模式设为 `LowerPriority`。
- **关键实现**：`NodeName = TEXT("RPG 可以攻击");` → `FlowAbortMode = EBTFlowAbortMode::LowerPriority;`
- **为什么这么写**：注释是一整段 ★ 说明 —— "★ 默认就要能打断'追击'，否则敌人会贴着你走却不打。行为树的 Selector 从左往右选：攻击分支在前、追击分支在后。敌人正在追击时，Selector 就'卡'在追击上 —— **不会主动回头重新检查攻击分支的条件**。结果是：追到玩家面前了也不停下来挥拳，一路贴着走。这和'玩家进视野敌人不切换战斗分支'是同一类问题，修法也一样：把装饰器设成观察者。区别在于这条**不用你在编辑器里配** —— 这是我们自己的装饰器，可以在构造函数里把默认值定好。（引擎自带的 Blackboard 装饰器就只能在 Details 里手动设 Flow Abort Mode。）"并补充实现约束："`FlowAbortMode` 是 `UBTDecorator` 的 protected 成员，子类可以设。`bAllowAbortLowerPri` 默认为 true，所以引擎允许这个值。"
- **被谁调用 / 调用谁**：UObject 构造流程。

#### `virtual bool CalculateRawConditionValue(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) const override`

- **干什么**：每次条件评估时当场算"能不能打"。
- **关键实现**（逐分支）：
  1. 取 `const AAIController* AIController = OwnerComp.GetAIOwner();` 与 `const UBlackboardComponent* Blackboard = OwnerComp.GetBlackboardComponent();`；任一为空 → `false`。
  2. `APawn* SelfPawn = AIController->GetPawn();` 为空 → `false`。
  3. 读 `Target = Cast<AActor>(Blackboard->GetValueAsObject(RPGBlackboardKeys::TargetActor));` 为空 → `false`。
  4. **双方存活**：自己 `Cast<ARPG_BaseCharacter>(SelfPawn)` → `!IsAlive()` → `false`；目标 `Cast<ARPG_BaseCharacter>(Target)` → `!IsAlive()` → `false`。注释："自己死了不该继续挥拳；目标死了更不该（虽然服务会清目标，但那是 0.2 秒一次 —— 这一层是更快的兜底）。"注意两次 Cast 失败时**不**返回 false（非角色 Actor 不参与存活判定）。
  5. **距离判定**：`CenterDistance = FVector::Dist(SelfPawn->GetActorLocation(), Target->GetActorLocation())`；`SurfaceDistance = CenterDistance - SelfPawn->GetSimpleCollisionRadius() - Target->GetSimpleCollisionRadius()`；`AttackRange = Blackboard->GetValueAsFloat(RPGBlackboardKeys::AttackRange)`；`return SurfaceDistance <= AttackRange * RangeTolerance;`
- **为什么这么写**：用表面距离的理由 —— "用**表面的距离**而不是两个原点的距离：角色的原点在脚底，而胶囊体有半径。直接用原点距离的话，两个 34 半径的胶囊体贴在一起时距离仍有 0，而稍微分开一点就'超范围'了 —— 手感上会觉得'明明挨着却打不到'。"负数的情况也有说明 —— "注意 `SurfaceDistance` 可能是负数（两个胶囊体重叠）—— 那正好说明绝对够得着，和正数比较的结果自然就是 true。"
- **被谁调用 / 调用谁**：行为树的条件评估流程。调用 `ARPG_BaseCharacter::IsAlive()`、`Blackboard->GetValueAsObject/GetValueAsFloat`。

#### `virtual FString GetStaticDescription() const override`

- **干什么**：返回显示在行为树节点上的描述。
- **关键实现**：`return FString::Printf(TEXT("距离 ≤ 攻击范围 × %.2f，且双方存活"), RangeTolerance);`
- **为什么这么写**：注释 —— "显示在行为树节点上，一眼看出容差配成了多少 —— 调试'AI 为什么不出手'时不用点开 Details 面板。"

#### 成员

| 成员 | 属性说明 | 类型 / 默认值 |
| --- | --- | --- |
| `RangeTolerance` | `UPROPERTY(EditAnywhere, Category = "RPG\|AI", meta = (ClampMin = "0.5", ClampMax = "3.0"))`，protected。注释："攻击距离的容差倍数。1.0 = 必须真的进入 `AttackRange` 才打；1.2 = 允许在范围外 20% 就出手。稍微大于 1 是有实战意义的：攻击动作本身有前摇，等'确实进范围'再出手，玩家往往已经走出去了，会出现'AI 一直追但永远打不到'。留一点提前量，打起来才跟手" | `float = 1.15f` |

---

### `Source/RPG/AI/Services/RPG_BTService_CombatUpdate.h` / `Source/RPG/AI/Services/RPG_BTService_CombatUpdate.cpp`

**一句话职责**：定期的战斗状态维护 —— 把"感知到的事实"翻译成"行为树能用的判断依据"。

**类/结构体**：`URPG_BTService_CombatUpdate` —— 继承 `UBTService`，`UCLASS(meta = (DisplayName = "RPG 战斗状态更新"))`。

#### 头文件里的三段设计说明

- **【服务（Service）和任务（Task）的分工】**：任务：**做一件事**，有开始有结束（走过去、打一拳）；服务：**持续维护某种状态**，挂在分支上，只要分支还活着就定期跑。
- **【它解决的三个问题】**：① **持续更新"最后已知位置"** —— 不能只在"刚看到"那一刻记一次，玩家是移动的，记忆也要跟着更新。② **丢视野 ≠ 脱战** —— 玩家躲到柱子后面感知系统会立刻报告"丢失"，那时就把目标清掉的话 AI 会变得极度健忘（"玩家绕树跑一圈敌人就回去巡逻了"），所以这里做**计时**：连续 N 秒没再看到才真的脱战。③ **目标死了要立刻脱战** —— "死亡不会触发'感知丢失'（尸体还在视野里），没人管的话 AI 会对着尸体继续挥拳。这是很常见的一个疏漏。"
- **【为什么不用 Tick，而是用服务自己的 Interval】**："这些判断不需要每帧做 —— 0.2 秒一次的精度对'脱战计时'完全够用，而每帧跑会白白吃 CPU（尤其敌人多的时候）。`Interval` 由行为树服务框架负责调度，比自己在 Tick 里数秒干净得多。"

#### `URPG_BTService_CombatUpdate()`

- **干什么**：设节点名、周期、随机偏移，并打开逐实例节点对象。
- **关键实现**：
  1. `NodeName = TEXT("RPG 战斗状态更新");`
  2. `Interval = 0.2f; RandomDeviation = 0.05f;` —— 注释："0.2 秒一次。加一点随机偏移是为了让多只敌人的服务不要在同一帧集中跑，避免周期性掉帧（十几只敌人每帧同时做感知查询会很明显）。"
  3. `bCreateNodeInstance = true;` —— 注释："有逐实例状态（连续不可见时长），每棵树各持一份"。
- **被谁调用 / 调用谁**：UObject 构造流程。

#### `virtual void OnBecomeRelevant(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override`

- **干什么**：`Super::OnBecomeRelevant(...)` 后 `TimeSinceLastSeen = 0.f;`
- **为什么这么写**：注释 —— "每次进入这个分支都从头计时 —— 否则上一轮战斗残留的计时会让 AI 刚打完一场就立刻脱战。"
- **被谁调用 / 调用谁**：行为树服务框架在分支激活时调用。

#### `virtual void TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override`

- **干什么**：每个间隔周期检查一次目标与可见性，做脱战计时。
- **关键实现**（逐分支）：
  1. `Super::TickNode(...)`；`UBlackboardComponent* Blackboard = OwnerComp.GetBlackboardComponent();` 为空 → `return`。
  2. `Target = Cast<AActor>(Blackboard->GetValueAsObject(RPGBlackboardKeys::TargetActor));`
  3. **本来就没目标**：`TimeSinceLastSeen = 0.f;` 然后兜底 —— 若 `Blackboard->GetValueAsBool(bInCombat)` 为真，则优先 `Cast<ARPG_AIController>(OwnerComp.GetAIOwner())->SetCombatState(false)`，Cast 失败时退回 `Blackboard->SetValueAsBool(bInCombat, false)`；最后 `return`。注释给出两条理由："兜底：确保 `bInCombat` 和'有没有目标'始终一致。少了这一步，一旦某条路径漏了清 `bInCombat`，AI 会卡在'战斗分支'里但没有任何目标，两个分支都不走 —— 站着发呆。同样走统一入口 —— 直接写黑板的话，速度会停在战斗速度上。"
  4. **目标死了**：`Cast<ARPG_BaseCharacter>(Target)` 且 `!IsAlive()` → `UE_LOG(LogRPG_AI, Log, TEXT("[%s] 目标已死亡，脱战"), *GetName());` → `ClearTarget(OwnerComp);` → `return`。注释："死亡**不会**触发感知丢失（尸体还在视野里），所以必须单独判。"
  5. **可见**（`bTargetVisible == true`）：`TimeSinceLastSeen = 0.f;` 并 `SetValueAsVector(LastKnownLocation, Target->GetActorLocation())`。注释："看得见的时候持续刷新'最后已知位置'。只在'刚看到'那一刻记一次是不够的 —— 玩家一直在动，追到最后已知位置时会停在玩家**几秒前**待过的地方。"
  6. **不可见**：`TimeSinceLastSeen += DeltaSeconds;` 若 `>= LoseTargetAfterSeconds` → `UE_LOG(LogRPG_AI, Log, TEXT("[%s] 目标已 %.1f 秒不可见，脱战返回巡逻"), ...)` → `ClearTarget(OwnerComp);`
- **注意**：本函数用的是 `DeltaSeconds` 累加，而 `Interval = 0.2f` 决定了累加的步长；`TimeSinceLastSeen` 是每实例状态（靠 `bCreateNodeInstance` 保证）。
- **被谁调用 / 调用谁**：行为树服务框架按 `Interval` 调度；调用 `ARPG_AIController::SetCombatState()`、本类 `ClearTarget()`、`ARPG_BaseCharacter::IsAlive()`。

#### `void ClearTarget(UBehaviorTreeComponent& OwnerComp)`（private）

- **干什么**：清掉目标，回到非战斗状态。
- **关键实现**：
  1. `UBlackboardComponent* Blackboard = OwnerComp.GetBlackboardComponent();` 为空 → `return`。
  2. `Blackboard->ClearValue(RPGBlackboardKeys::TargetActor);` 与 `SetValueAsBool(bTargetVisible, false);` —— **不清 `LastKnownLocation`**。
  3. `Cast<ARPG_AIController>(OwnerComp.GetAIOwner())`：成功 → `AIController->SetCombatState(false);`；失败（兜底）→ `Blackboard->SetValueAsBool(bInCombat, false);`
  4. `TimeSinceLastSeen = 0.f;`
- **为什么这么写**：两条注释 —— ① "注意**不清 `LastKnownLocation`**：留着它，AI 下次进战斗时如果一开始就丢视野，至少还有个搜索目标。而且调试时能看到'上一次是在哪发现玩家的'，很有用。" ② "★ 走 AIController 的统一入口，而不是直接写 `bInCombat` —— 它还要负责把移动速度降回巡逻速度。只改黑板的话，敌人会'脱战了但还在用战斗速度走路'，看起来像脱战没生效。"兜底分支的理由 —— "万一 AI 控制器不是我们的类型（比如关卡里误配了基类），至少把黑板清干净，行为树还能正常回到巡逻分支。"
- **被谁调用 / 调用谁**：本类 `TickNode()`（目标死亡、超时脱战两处）。

#### 成员

| 成员 | 属性说明 | 类型 / 默认值 |
| --- | --- | --- |
| `LoseTargetAfterSeconds` | `UPROPERTY(EditAnywhere, Category = "RPG\|AI", meta = (ClampMin = "0.5"))`，protected。注释："目标'看不见'持续多久才真正脱战（秒）。太短 → 玩家一躲起来 AI 就忘了，感觉很傻；太长 → 玩家早跑远了 AI 还在原地转圈找。3~8 秒是动作游戏里比较自然的区间" | `float = 6.f` |
| `TimeSinceLastSeen` | private，无 UPROPERTY。注释："目标已经连续不可见多久了" | `float = 0.f` |
| `ClearTarget(...)` | private 成员函数 | 见上 |

---

### `Source/RPG/AI/Tasks/RPG_BTTask_Attack.h` / `Source/RPG/AI/Tasks/RPG_BTTask_Attack.cpp`

**一句话职责**：发动一次攻击，并等它播完（潜在任务）。

**类/结构体**：`URPG_BTTask_Attack` —— 继承 `UBTTaskNode`，`UCLASS(meta = (DisplayName = "RPG 攻击"))`。

#### 头文件里的三段设计说明

- **【★ 这个节点为什么不能"发完指令就返回成功"】**："攻击动画要播 0.5~1 秒。如果 `ExecuteTask` 里发动完攻击就 `return Succeeded`，行为树会立刻执行下一个节点 —— 于是 AI 会在挥拳的同时开始走向玩家，看起来像在'滑步出拳'。正确做法是返回 **InProgress**，让行为树停在这个节点上，等攻击真正结束了再 `FinishLatentTask`。这就是**潜在任务（Latent Task）**模式，也是 AI + 技能系统结合最经典的一个坑。"
- **【★ AI 和玩家走的是同一条能力链路】**：注释明确写出本节点做的事和 `RPG_PlayerController::OnAbilityInputPressed` **一模一样**：（1）把 `InputTag` 推进 `CombatComponent` 的输入缓存；（2）`ASC->TryActivateAbilityByInputTag(InputTag)`。并给出三条为什么不直接 `TryActivateAbilityByTag(Ability.Attack.Light)` 的理由：直接调能力会**绕过输入缓存** → AI 打不出连段（连段靠缓存推进）；两条路径分开写早晚会不一致 → "玩家能放、AI 放不出来"这类问题；现在这个写法下，将来给攻击加"前摇期间不能转身"之类规则，玩家和 AI 自动同时生效。
- **【怎么知道攻击结束了】**：注释说两条路都用过，最后选了**轮询标签**：监听 `Event.Combat.AttackEnd` 事件 —— 精确，但只覆盖"动画播到 AttackEnd 通知"这一条结束路径（攻击被取消、耐力不足直接失败、蒙太奇没配走模拟时序……这些情况下事件不会来，AI 会一直等下去）；轮询 `State.Attacking` 标签 —— 不管因为什么原因结束的，能力一结束这个标签就会被 GAS 自动摘掉，**覆盖所有路径**。代价是每帧一次标签查询（哈希表查找，可忽略）。并点出这是"用'状态标签'而不是'结束事件'当判据，正是本项目'标签是唯一真相源'这条原则带来的直接好处"。

#### `URPG_BTTask_Attack()`

- **干什么**：设节点名与默认输入标签，打开 tick，打开逐实例节点，显式设置 `bIgnoreRestartSelf`。
- **关键实现**：
  1. `NodeName = TEXT("RPG 攻击");`
  2. `InputTag = RPGTags::Input_Attack_Light;` —— 注释："默认轻击 —— 敌人在阶段 5 用的是同一套徒手连招"。
  3. `bNotifyTick = true;` —— 注释："超时判断在 `TickTask` 里做"。
  4. `bCreateNodeInstance = true;` —— 注释："本节点有逐实例的状态（已等待时长），必须每棵行为树各持一份。共享节点对象的话，两只敌人同时攻击会互相覆盖计时。详见 `RPG_BTTask_MoveBase` 构造函数里的说明。"
  5. `bIgnoreRestartSelf = false;` —— 注释："失败时不要把整棵子树标成'失败需要重新规划' —— 攻击失败（比如耐力不够）是正常情况，让行为树按 Selector 继续试别的分支就好"。
- **被谁调用 / 调用谁**：UObject 构造流程。

#### `virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override`

- **干什么**：推缓存 → 激活能力 → 返回 `InProgress` 等结束。
- **关键实现**（逐分支）：
  1. `AAIController* AIController = OwnerComp.GetAIOwner();` 为空 → `Failed`。
  2. `ARPG_BaseCharacter* Character = Cast<ARPG_BaseCharacter>(AIController->GetPawn());` 为空 → `Failed`。
  3. `URPG_AbilitySystemComponent* ASC = Cast<URPG_AbilitySystemComponent>(Character->GetAbilitySystemComponent());` 为空 → `UE_LOG(LogRPG_AI, Warning, "[%s] 角色上没有 URPG_AbilitySystemComponent —— AI 无法发动攻击")` + `Failed`。注释解释了为什么这么取 ASC —— "走基类的 `IAbilitySystemInterface` 拿 ASC，再 Cast 成项目扩展的类型 —— 这样本节点对玩家和敌人**一视同仁**：谁的 ASC 都行，只要它支持'按输入标签激活'这个能力。用 `GetRPGAbilitySystemComponent()`（只在 `RPG_Enemy` 上有）的话，这段代码就写死成'只能给敌人用'了。"
  4. `if (!InputTag.IsValid())` → `UE_LOG(LogRPG_AI, Error, "[%s] 没有配置 InputTag —— 这个攻击节点永远不会生效")` + `Failed`。
  5. **① 推输入缓存**：`if (URPG_CombatComponent* Combat = Character->GetCombatComponent()) { Combat->PushInputTag(InputTag); }`。注释："这一句看着多余（反正马上要激活了），但它决定了**连段能不能推进** —— `GA_LightAttack` 的连段靠'在衔接窗口开启时从缓存里取出下一条输入'来推进，不推缓存的话 AI 永远只能打第 1 段。也就是说：AI 想打 3 连击，就得让行为树连续三次执行本节点。这正好是行为树擅长的事 —— 在 `Sequence` 里排三个攻击节点即可。"
  6. **② 激活能力**：`if (!ASC->TryActivateAbilityByInputTag(InputTag))` → Verbose 日志（"攻击能力激活失败（%s）—— 交给行为树尝试别的分支"）+ `Failed`。注释："失败是很正常的（耐力不够、还在冷却、被 `State.Dead` 阻断……）。用 Verbose 而不是 Warning —— 否则 AI 每次想打但打不出来都刷一条警告。真正的失败原因由 ASC 的 `AbilityFailedCallbacks` 打出来。"
  7. 成功则 `UE_LOG(LogRPG_AI, Verbose, "[%s] 发动攻击（%s）")`；`CachedOwnerComp = &OwnerComp;`；`ElapsedSeconds = 0.f;`；`return EBTNodeResult::InProgress;`（注释："★ 返回 InProgress 而不是 Succeeded —— 见头文件里的说明。行为树会停在这个节点上，等我们 FinishLatentTask。"）
- **被谁调用 / 调用谁**：行为树。调用 `URPG_CombatComponent::PushInputTag()`、`URPG_AbilitySystemComponent::TryActivateAbilityByInputTag()`。

#### `virtual EBTNodeResult::Type AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override`

- **干什么**：被打断时的处理 —— **故意什么都不做**，只打一条 Verbose 日志并返回 `Aborted`。
- **关键实现**：`UE_LOG(LogRPG_AI, Verbose, TEXT("[%s] 攻击被行为树打断 —— 让这一招自然播完"), *GetName()); return EBTNodeResult::Aborted;`
- **为什么这么写**：注释给了完整理由 —— "── 被打断时故意**不取消**攻击能力 ── 被打断的原因通常是'条件不再满足'（比如目标死了、或者距离变了）。但招式已经挥出去了，中途硬停下来会看到动作卡在半空 —— 而且伤害判定窗口可能已经开过了，取消能力会造成'看起来打中了却没伤害'。让这一招自然打完（通常不到 1 秒），行为树下一轮自然会重新决策。如果将来要做'被打断就收招'，在这里调 `ASC->CancelAbilities` 即可。"
- **被谁调用 / 调用谁**：行为树（`StopLogic` / 条件打断时）。**本函数不解除任何已注册的东西**（本节点没有注册委托）。

#### `virtual void TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override`

- **干什么**：每帧检查 `State.Attacking` 是否还在，以及超时兜底。
- **关键实现**：
  1. `Super::TickTask(...)`；`ElapsedSeconds += DeltaSeconds;`
  2. 重新取 `AIController` → `Character` → `const UAbilitySystemComponent* ASC`（链式三元，任一为空则为 `nullptr`）。
  3. **判据**：`if (ASC && !ASC->HasMatchingGameplayTag(RPGTags::State_Attacking))` → `if (UBehaviorTreeComponent* BTComp = CachedOwnerComp.Get()) { FinishLatentTask(*BTComp, EBTNodeResult::Succeeded); }` → `return;`。注释列出标签覆盖的四条结束路径："蒙太奇播到 AttackEnd 通知 → 标签摘掉 ✅；蒙太奇自然播完（没配通知）→ 标签摘掉 ✅；能力被取消 / 被打断 → 标签摘掉 ✅；能力压根没激活成功 → 标签本来就没有 ✅；而事件只覆盖第一条。"
  4. **超时兜底**：`if (ElapsedSeconds >= AttackTimeoutSeconds)` → `UE_LOG(LogRPG_AI, Warning, "[%s] 攻击超时（%.1f 秒）仍未结束 —— 检查 State.Attacking 是否被正常清理，或者敌人是不是卡在了一个不会结束的能力里")` → `FinishLatentTask(*BTComp, EBTNodeResult::Failed);`
  - 注意两个分支都用 `CachedOwnerComp.Get()` 取组件（`TWeakObjectPtr`），取不到就什么都不做（不崩）。
- **被谁调用 / 调用谁**：行为树（因为 `bNotifyTick = true`）。调用 `UAbilitySystemComponent::HasMatchingGameplayTag()`（读 `RPGTags::State_Attacking`）、`UBTTaskNode::FinishLatentTask()`。

#### 成员

| 成员 | 属性说明 | 类型 / 默认值 |
| --- | --- | --- |
| `InputTag` | `UPROPERTY(EditAnywhere, Category = "RPG\|AI", meta = (Categories = "Input"))`，protected。注释："用哪个输入标签触发攻击。用 Input 域的标签（而不是 Ability 域的），因为这个入口是'模拟一次按键'，和玩家走的是同一个。想细分'轻击 / 重击 / 技能'，就在这里配不同的 `Input.*` 标签" | `FGameplayTag`，构造函数里默认 `RPGTags::Input_Attack_Light` |
| `AttackTimeoutSeconds` | `UPROPERTY(EditAnywhere, Category = "RPG\|AI", meta = (ClampMin = "0.5"))`，protected。注释："攻击最长允许多久（秒）。超时则判失败，让行为树继续往下走。没有它的话，万一 `State.Attacking` 因为某个 bug 没被摘掉，AI 会永远卡在这个节点上 —— 表现是'敌人打完一拳之后就不动了'" | `float = 3.f` |
| `ElapsedSeconds` | private，无 UPROPERTY。注释："本次攻击已经等了多久" | `float = 0.f` |
| `CachedOwnerComp` | private，无 UPROPERTY。注释："发起攻击的行为树组件（完成时要靠它调 `FinishLatentTask`）" | `TWeakObjectPtr<UBehaviorTreeComponent>` |

---

### `Source/RPG/AI/Tasks/RPG_BTTask_MoveBase.h` / `Source/RPG/AI/Tasks/RPG_BTTask_MoveBase.cpp`

**一句话职责**："走到某个地方并等它走到" —— 巡逻与追击的共同逻辑（抽象基类）。

**类/结构体**：`URPG_BTTask_MoveBase` —— 继承 `UBTTaskNode`，`UCLASS(Abstract)`。

#### 头文件里的两段设计说明

- **【★ 为什么必须用潜在任务（Latent Task）模式】**：行为树节点有两种写法 —— **瞬时完成**（在 `ExecuteTask` 里做完事，直接 `return Succeeded`）与**潜在任务**（`return InProgress`，等某件事发生后再 `FinishLatentTask`）。"移动"必须用后者，因为 `MoveToLocation` 只是**下了一个移动请求**，它是异步的 —— 函数返回时角色才刚抬脚。如果在这里 `return Succeeded`，行为树会以为"走完了"，立刻执行下一个节点（比如发起攻击）—— 于是你会看到 AI **一边往你这边走一边挥拳头**。正确做法是返回 `InProgress`，等 `ReceiveMoveCompleted` 回调（或者我们自己判超时）再 `FinishLatentTask`。注释还加了一句："这是 AI 里最经典的一个坑，也是面试高频题。"
- **【为什么抽一个基类】**："巡逻和追击的差别只有'去哪'这一件事，剩下的（下请求、绑回调、处理到达/失败/超时/中断）完全一样。让子类只实现 `PrepareMove()` 一个函数，能保证两者的中断处理逻辑**绝对一致** —— 而中断处理恰恰是最容易写漏、漏了又最难查的部分。"

#### `URPG_BTTask_MoveBase()`

- **干什么**：打开 tick，打开逐实例节点对象。
- **关键实现**：
  1. `bNotifyTick = true;` —— 注释："超时判断要在 `TickTask` 里做，所以必须打开 tick。不打开的话 `TickTask` 根本不会被调用 —— 超时保护形同虚设。"
  2. `bCreateNodeInstance = true;` —— 注释是一整段 ★ 说明："行为树节点默认是**共享**的：`bCreateNodeInstance = false` 时，所有跑这棵树的 AI 用的是**同一个**节点对象（资产里那个），逐实例的状态要靠 `NodeMemory` 那块内存传。本类在成员变量里存了'当前请求 ID'和'已等待时长'。如果节点是共享的，两只敌人同时移动就会互相覆盖这两个值 —— 表现是'其中一只永远走不到目的地'或'走着走着突然停下'，而且只在**同屏出现第二只敌人**时才复现。打开这个开关，引擎会为每个行为树实例克隆一份节点对象，成员变量就天然是'每只敌人各一份'。代价是每实例多一个 UObject —— 对本项目这点规模可以忽略。（引擎自己的节点多用 `NodeMemory` 方案以避免这个开销，但那套写法要自己算内存大小、自己 Placement New，复杂度高得多。）"
- **被谁调用 / 调用谁**：UObject 构造流程（子类构造时会先跑它）。

#### `virtual bool PrepareMove(UBehaviorTreeComponent& OwnerComp, FVector& OutDestination, AActor*& OutGoalActor)`（纯虚）

- **干什么**：决定这次去哪。返回 `false` 表示"这次不走了"，任务直接失败。
- **关键实现**：`PURE_VIRTUAL(URPG_BTTask_MoveBase::PrepareMove, return false;);` —— 只有声明，无实现。
- **为什么这么写**：头文件注释 —— "子类在这里读黑板、取巡逻点、推进索引 —— 一切'目的地从哪来'的逻辑。"出参语义 —— `OutDestination`：走到这个位置，`OutGoalActor` 非空时忽略；`OutGoalActor`：★ 跟随这个 Actor，"非空时引擎会**持续观察它的位置**，移动超过阈值就自动重算路径 —— 这才是'追击'该有的行为。用固定坐标的话只是'走到你刚才站的地方'。"
- **被谁调用 / 调用谁**：本类 `StartMove()`；实现者是 `URPG_BTTask_Patrol::PrepareMove()` 与 `URPG_BTTask_MoveToTarget::PrepareMove()`。

#### `EBTNodeResult::Type StartMove(UBehaviorTreeComponent& OwnerComp)`（protected）

- **干什么**：发起一次移动。抽出来的目的是"追击过程中要能换目标"。
- **关键实现**（逐分支）：
  1. `AAIController* AIController = OwnerComp.GetAIOwner();` 若 `!AIController || !AIController->GetPawn()` → `Failed`。
  2. 准备 `FVector Destination = FVector::ZeroVector; AActor* GoalActor = nullptr;`
  3. `if (!PrepareMove(OwnerComp, Destination, GoalActor)) return EBTNodeResult::Failed;` —— 注释："子类说这次没地方可去（比如巡逻点一个都没配）"。
  4. **先解后绑**：`AIController->ReceiveMoveCompleted.RemoveDynamic(this, &URPG_BTTask_MoveBase::OnMoveCompleted);` 然后 `AddDynamic(...)`。注释："不能只靠'绑一次'的假设 —— `StartMove` 在一轮任务里可能被调用多次（追击中丢失视野要换目标）。动态多播委托重复绑定会在一次移动完成时触发 N 次回调，表现是'AI 走着走着突然跳一大步'。先解再绑是幂等的，无论调用几次都只有一个绑定。"
  5. **构造移动请求**：`FAIMoveRequest MoveRequest = GoalActor ? FAIMoveRequest(GoalActor) : FAIMoveRequest(Destination);`。注释 ★："用 `FAIMoveRequest(GoalActor)` 而不是 `FAIMoveRequest(位置)`：传位置 → 引擎只记住那一个坐标，角色走到就停了（是'快照'，不是'跟随'）；传 Actor → 引擎会调 `Path->SetGoalActorObservation(Actor, 100)`，**持续观察目标位置，移动超过阈值就自动重算路径**（`AIController.cpp:913`，`NavigationData.h:362` 的接口注释：`"enables path observing specified AActor's location and update itself if actor changes location"`）。追击必须用后者。用前者的话，AI 永远在走向你**几秒前**站的地方，到了之后再拍一次快照重新出发 —— 表现就是'一顿一顿地追'。"
  6. `MoveRequest.SetAcceptanceRadius(AcceptanceRadius);`；`SetUsePathfinding(true);`；`SetReachTestIncludesAgentRadius(true);` —— 注释："把角色半径算进到达判定 —— 否则'到了'是以胶囊体中心算的，视觉上角色还有半个身子在外面就被判定到达了"。
  7. `if (!GoalActor) { MoveRequest.SetProjectGoalLocation(true); }` —— 注释："目标会被投影到导航网格上。跟随 Actor 时由引擎自己处理目标位置，不需要这个开关。"
  8. `const FPathFollowingRequestResult MoveResult = AIController->MoveTo(MoveRequest); CurrentRequestID = MoveResult.MoveId;`
  9. `MoveResult.Code == EPathFollowingRequestResult::Failed` → 先 `RemoveDynamic` 解绑，打 `UE_LOG(LogRPG_AI, Warning, "[%s] 移动请求失败：目标 %s 可能不在导航网格上（关卡里放 NavMeshBoundsVolume 了吗）")`（`GoalActor` 为空时用 `Destination.ToCompactString()`），返回 `Failed`。
  10. `MoveResult.Code == EPathFollowingRequestResult::AlreadyAtGoal` → 解绑，返回 `Succeeded`。注释："已经站在目标点上了 —— 不用等，直接成功。不处理这个分支的话 AI 会站在那儿干等超时。"
  11. `RequestSuccessful` → `ElapsedSeconds = 0.f; return EBTNodeResult::InProgress;`
- **返回语义说明**（头文件）：`@return InProgress = 已受理，等回调；其他值 = 立刻结束`。
- **被谁调用 / 调用谁**：本类 `ExecuteTask()`、`URPG_BTTask_MoveToTarget::TickTask()`（中途变卦时重发）。调用 `PrepareMove()`、`AAIController::MoveTo()`、`FAIMoveRequest` 的各 setter。

#### `virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override`

- **干什么**：两行 —— 记下组件、发起移动。
- **关键实现**：`CachedOwnerComp = &OwnerComp; return StartMove(OwnerComp);`

#### `virtual EBTNodeResult::Type AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override`

- **干什么**：解绑回调 + 停止移动。
- **关键实现**：`if (AAIController* AIController = OwnerComp.GetAIOwner())` → 先 `RemoveDynamic` 再 `StopMovement()`；返回 `Aborted`。
- **为什么这么写**：注释 —— "★ 解绑要在停移动之前还是之后？之前 —— 因为 `StopMovement` 可能会触发一次移动完成回调（结果是 Aborted），那时我们已经被打断了，再跑一遍 `OnMoveCompleted` 会去 `FinishLatentTask` 一个已经结束的任务。"
- **被谁调用 / 调用谁**：行为树（`StopLogic` / 条件打断）。调用 `AAIController::StopMovement()`。

#### `virtual void TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override`

- **干什么**：移动超时兜底。
- **关键实现**：`Super::TickTask(...)`；`ElapsedSeconds += DeltaSeconds;` 若 `ElapsedSeconds >= MoveTimeoutSeconds`：
  1. `UE_LOG(LogRPG_AI, Warning, "[%s] 移动超时（%.1f 秒）—— AI 会放弃这次移动。检查目标点是否可达、导航网格是否覆盖了那片区域")`；
  2. `if (AAIController* AIController = OwnerComp.GetAIOwner())` → 解绑 + `StopMovement()`；
  3. `FinishLatentTask(OwnerComp, EBTNodeResult::Failed);`
- **为什么这么写**：注释 —— "走到这里说明既没收到'到达'也没收到'失败' —— 通常是寻路卡住了（目标点在障碍物里、导航网格有洞、或者角色被卡住）。这一步的意义不在于'处理得多好'，而在于**让 AI 恢复行动能力**：不兜底的话行为树会永远停在这个节点上，表现为'敌人突然站着不动了'。"
- **被谁调用 / 调用谁**：行为树（`bNotifyTick = true`）。注意：**子类 `URPG_BTTask_MoveToTarget` 重写本函数，在"中途变卦"分支里会跳过 `Super::TickTask`**（见该节）。

#### `void OnMoveCompleted(FAIRequestID RequestID, EPathFollowingResult::Type Result)`（`UFUNCTION()`）

- **干什么**：移动完成回调。
- **关键实现**：`if (CurrentRequestID.IsValid() && RequestID != CurrentRequestID) { return; }` —— 只认自己那次请求；然后 `if (UBehaviorTreeComponent* OwnerComp = CachedOwnerComp.Get())` → `FinishLatentTask(*OwnerComp, Result == EPathFollowingResult::Success ? EBTNodeResult::Succeeded : EBTNodeResult::Failed);`
- **为什么这么写**：注释 —— "AI 身上可能同时有其他来源的移动请求（被击退、被别的节点挪动），不过滤的话会误判成'我到了'。"
- **被谁调用 / 调用谁**：`AAIController::ReceiveMoveCompleted` 动态多播委托（在 `StartMove()` 里 `AddDynamic` 注册，在 `AbortTask()` / 超时 / `Failed` / `AlreadyAtGoal` 处 `RemoveDynamic` 解绑）。调用 `UBTTaskNode::FinishLatentTask()`。

#### 成员

| 成员 | 属性说明 | 类型 / 默认值 |
| --- | --- | --- |
| `AcceptanceRadius` | `UPROPERTY(EditAnywhere, Category = "RPG\|AI", meta = (ClampMin = "-1.0"))`，protected。注释："认为'到达了'的距离容差（厘米）。-1 表示用导航系统默认值（一般取角色的胶囊体半径）。调大一点能让 AI 少做一次'微调位置'的小碎步" | `float = 60.f` |
| `MoveTimeoutSeconds` | `UPROPERTY(EditAnywhere, Category = "RPG\|AI", meta = (ClampMin = "0.5"))`，protected。注释："移动超时（秒）。没有它的话，'寻路失败'或'目标点在导航网格外'会让 AI 永久卡在 `InProgress` 状态 —— 行为树停在原地不动，而且不报错。这是'AI 突然变傻站着不动'最常见的原因" | `float = 10.f` |
| `CurrentRequestID` | private，无 UPROPERTY。注释："本次移动请求的 ID。用来过滤掉不是我们发起的移动完成回调" | `FAIRequestID` |
| `ElapsedSeconds` | private，无 UPROPERTY。注释："本次移动已经等了多久" | `float = 0.f` |
| `CachedOwnerComp` | private，无 UPROPERTY。注释："发起这次移动的行为树组件。为什么必须自己存：完成回调是在**别的时机**被调用的，那时手上没有 `OwnerComp` 参数。而节点对象的 Outer 是**行为树资产**而不是运行中的组件 —— 用 `GetOuter()` 会拿到错的东西" | `TWeakObjectPtr<UBehaviorTreeComponent>` |

---

### `Source/RPG/AI/Tasks/RPG_BTTask_MoveToTarget.h` / `Source/RPG/AI/Tasks/RPG_BTTask_MoveToTarget.cpp`

**一句话职责**：追击 —— 走向目标；看得见走"活的玩家"，看不见走"最后已知位置"。

**类/结构体**：`URPG_BTTask_MoveToTarget` —— 继承 `URPG_BTTask_MoveBase`，`UCLASS(meta = (DisplayName = "RPG 追击目标"))`。

#### 头文件里的设计说明

- **【走"活的玩家"还是"最后已知位置"？】**：两种模式由 `bChaseVisibleTargetOnly` 控制 —— 看得见时直接走向玩家本体（每帧更新的活目标）；看不见时走向 `LastKnownLocation`（记忆里的那个点）。注释："第二种才是'有脑子'的关键。如果视野一丢就放弃、直接回去巡逻，玩家绕一根柱子就能把敌人耍得团团转；而如果永远追着玩家本体跑，那就是透视挂，玩家躲在哪里都没用。正确行为是：**去你最后出现的地方找一圈**，找不到才回去巡逻。这一段'找一圈'的耐心由 `BTService_CombatUpdate` 的脱战计时控制。"

#### `URPG_BTTask_MoveToTarget()`

- **干什么**：只设节点名 —— `NodeName = TEXT("RPG 追击目标");`。（没有额外配置；`bCreateNodeInstance` / `bNotifyTick` 由基类构造函数打开。）

#### `virtual bool PrepareMove(UBehaviorTreeComponent& OwnerComp, FVector& OutDestination, AActor*& OutGoalActor) override`

- **干什么**：读黑板决定去哪、以及用哪种走法。
- **关键实现**（逐分支）：
  1. `OutGoalActor = nullptr; bLastMoveFollowedActor = false;` 先复位两个出参/状态。
  2. `UBlackboardComponent* Blackboard = OwnerComp.GetBlackboardComponent();` 为空 → `false`。
  3. 读 `Target = Cast<AActor>(Blackboard->GetValueAsObject(RPGBlackboardKeys::TargetActor));`
  4. **看得见且有目标**（`bTargetVisible && Target`）：`OutGoalActor = Target; bLastMoveFollowedActor = true; return true;`。注释 ★："这两种写法的差别是'追击手感'的分水岭：`OutDestination = Target->GetActorLocation()` ← 一个坐标快照；`OutGoalActor = Target` ← 跟随这个 Actor。传坐标的话，引擎只知道'走到那个点为止'。角色走到时玩家早就走了，于是到了 → 停 → 重新拍快照 → 再走 —— 表现就是一顿一顿的，而且永远在追你**几秒前**站的地方。传 Actor 则会让引擎调用 `Path->SetGoalActorObservation(Actor, 100)`（`AIController.cpp:913`），**持续观察目标位置，移动超过阈值就自动重算路径** —— 真正的'时刻确认玩家在哪'。"
  5. `if (bChaseVisibleTargetOnly) { return false; }` —— 注释："看不见就认输，交给别的分支"。
  6. 读 `const FVector LastKnown = Blackboard->GetValueAsVector(RPGBlackboardKeys::LastKnownLocation);`，若 `LastKnown.IsNearlyZero()` → `return false;`。注释："这是'有记忆的追击'：不是立刻放弃，而是去你最后出现的地方找一圈。注意 `LastKnownLocation` **不是零向量**时才有效 —— 从没看到过目标时它是 (0,0,0)，直接走过去会让 AI 冲向世界原点。"
  7. `OutDestination = LastKnown;` + Verbose 日志 `"[%s] 目标不可见，前往最后已知位置 %s 搜索"`，`return true;`
- **被谁调用 / 调用谁**：`URPG_BTTask_MoveBase::StartMove()`（`ExecuteTask` 与 `TickTask` 两处）。读黑板 `TargetActor` / `bTargetVisible` / `LastKnownLocation`。

#### `bool IsTargetVisible(const UBehaviorTreeComponent& OwnerComp) const`（private）

- **干什么**：从黑板读"目标此刻可不可见"。
- **关键实现**：`const UBlackboardComponent* Blackboard = OwnerComp.GetBlackboardComponent(); return Blackboard && Blackboard->GetValueAsBool(RPGBlackboardKeys::bTargetVisible);`
- **被谁调用 / 调用谁**：本类 `TickTask()`。

#### `virtual void TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override`

- **干什么**：检测"中途变卦"（可见性翻转）并就地换一套走法。
- **关键实现**：
  1. `if (IsTargetVisible(OwnerComp) != bLastMoveFollowedActor)` —— 判据是"当前可见性"与"上次发起的走法"是否一致。
  2. 进入分支后 `const EBTNodeResult::Type RestartResult = StartMove(OwnerComp);`
  3. 若 `RestartResult != EBTNodeResult::InProgress` → `FinishLatentTask(OwnerComp, RestartResult); return;`
  4. 否则 `return;`（**这一帧不调用 `Super::TickTask`**）。注释："`StartMove` 已经把计时清零了，这一帧不用再累加超时。"
  5. 没有变卦时才 `Super::TickTask(OwnerComp, NodeMemory, DeltaSeconds);`（即交给基类做超时判断）。
- **为什么这么写**：注释给了完整的场景说明 —— "追击过程中'看得见'和'看不见'会来回切换。切换时必须换走法：看得见 → 看不见：不能再跟着 Actor 跑了（那等于透视），改成去最后已知位置；看不见 → 看得见：重新锁定，跟着真人跑。为什么在 `TickTask` 里就地换、而不是 `return Succeeded` 让行为树重来：行为树的巡逻/追击分支后面跟着一个 `Wait` 节点，绕一圈就意味着**原地站 0.2 秒**再出发 —— 那正是要消掉的'一顿'。"
- **注意**：这里 `StartMove()` 会重新走一遍"先解后绑"，这正是基类里那句"先解再绑是幂等的"所服务的主要场景。
- **被谁调用 / 调用谁**：行为树（基类构造里 `bNotifyTick = true`）。调用本类 `IsTargetVisible()`、基类 `StartMove()` / `TickTask()` / `FinishLatentTask()`。

#### 成员

| 成员 | 属性说明 | 类型 / 默认值 |
| --- | --- | --- |
| `bChaseVisibleTargetOnly` | `UPROPERTY(EditAnywhere, Category = "RPG\|AI")`，protected。注释："只追看得见的目标。false（默认）= 看不见时去'最后已知位置'找 —— 有记忆的追击；true = 看不见就失败，让行为树的别的分支接手" | `bool = false` |
| `bLastMoveFollowedActor` | private，无 UPROPERTY。注释："上一次发起移动时，采用的是'跟随目标'还是'去固定点'。用来发现**中途变卦**：追着追着目标丢了（或者又看见了），就得换一套走法。不检测的话，AI 会一路跟着你跑到 6 秒脱战为止，哪怕你早就躲进墙后面了 —— 那等于透视" | `bool = false` |

---

### `Source/RPG/AI/Tasks/RPG_BTTask_Patrol.h` / `Source/RPG/AI/Tasks/RPG_BTTask_Patrol.cpp`

**一句话职责**：巡逻 —— 依次走到敌人身上配的每一个巡逻点，走完一圈从头再来。

**类/结构体**：`URPG_BTTask_Patrol` —— 继承 `URPG_BTTask_MoveBase`，`UCLASS(meta = (DisplayName = "RPG 巡逻"))`。

#### 头文件里的设计说明

注释说明巡逻点来自 `ARPG_Enemy::PatrolPoints` —— "在关卡里摆几个 `TargetPoint`，然后在**这个敌人的实例上**把它们拖进数组。用 `EditInstanceOnly` 配的理由：每个敌人可以有自己的巡逻路线，但不需要为每只敌人都做一个蓝图子类。摆十个敌人 = 拖十次，比'做十个蓝图'轻得多。"

#### `URPG_BTTask_Patrol()`

- **干什么**：只设节点名 —— `NodeName = TEXT("RPG 巡逻");`，注释："名字会显示在行为树节点的标题上，Debug 时一眼认出是哪个节点"。

#### `virtual bool PrepareMove(UBehaviorTreeComponent& OwnerComp, FVector& OutDestination, AActor*& OutGoalActor) override`

- **干什么**：取下一个巡逻点作为目的地（环形推进索引）。
- **关键实现**（逐分支）：
  1. `OutGoalActor = nullptr;` —— 注释："巡逻永远是'走到某个固定点'，不跟随任何 Actor"。
  2. 取 `AIController` / `Blackboard`，`if (!AIController || !Blackboard) return false;`（写在同一行）。
  3. `const ARPG_Enemy* Enemy = Cast<ARPG_Enemy>(AIController->GetPawn()); if (!Enemy) return false;`
  4. **一个巡逻点都没配** `Enemy->PatrolPoints.IsEmpty()`：若 `!bFallbackToHomeLocation` → `return false;`；否则 `OutDestination = Blackboard->GetValueAsVector(RPGBlackboardKeys::HomeLocation); return true;`。注释给出一条容易看错的地方 —— "回出生点待着。用 `LastKnownLocation` 而不是 `PatrolLocation` 传目的地 —— 后者是'巡逻点的位置'，把它写成一个固定点会误导调试时看到的值。"（**注：这段注释里的 "`LastKnownLocation`" 疑为笔误，代码实际读的是 `HomeLocation`，注释后半句讨论的 `PatrolLocation` 则说得通。此处如实转述，不做修改。**）
  5. **取下一个巡逻点**：`Count = Enemy->PatrolPoints.Num(); CurrentIndex = Blackboard->GetValueAsInt(RPGBlackboardKeys::PatrolIndex); NextIndex = (CurrentIndex + 1) % Count;`（注释："环形，走完一圈从头开始"）；`Blackboard->SetValueAsInt(RPGBlackboardKeys::PatrolIndex, NextIndex);`
  6. **空元素跳过**：`const AActor* Point = Enemy->PatrolPoints[NextIndex];` 若为空 → `UE_LOG(LogRPG_AI, Warning, "[%s] 巡逻点数组第 %d 项是空的 —— 检查一下敌人的 PatrolPoints 配置")` + `return false;`
  7. `OutDestination = Point->GetActorLocation();` 然后 `Blackboard->SetValueAsVector(RPGBlackboardKeys::PatrolLocation, OutDestination); return true;`
- **为什么这么写（索引存黑板）**：注释 —— "索引存在黑板上而不是本节点里，是为了**能在编辑器里实时看到** AI 巡逻到第几个点了（运行时选中 AI，看它的黑板即可）。存在节点成员里的话，调试时只能靠日志。"注意这是**先自增、再用新索引取点**，所以每次执行本节点都会前进一个点。
- **被谁调用 / 调用谁**：`URPG_BTTask_MoveBase::StartMove()`。读 `ARPG_Enemy::PatrolPoints`（成员在 `RPG_Enemy.h:58`，`EditInstanceOnly, BlueprintReadOnly, Category = "RPG|AI"`）。

#### 成员

| 成员 | 属性说明 | 类型 / 默认值 |
| --- | --- | --- |
| `bFallbackToHomeLocation` | `UPROPERTY(EditAnywhere, Category = "RPG\|AI")`，protected。注释："一个巡逻点都没配时怎么办。true = 回出生点附近待着（默认）；false = 任务直接失败 —— 行为树会往下走，可能变成'原地发呆'" | `bool = true` |

---

## 附：本章涉及但不在本章范围内的关键外部依赖（仅登记，不展开）

| 名称 | 位置 | 与本章的关系 |
| --- | --- | --- |
| `ARPG_BaseCharacter::IsAlive()` | `Character/RPG_BaseCharacter.h:64` / `.cpp:334` | 被 AI 的装饰器、服务、感知回调用作存活判定；接口层封装了"ASC 未初始化时按存活处理" |
| `ARPG_BaseCharacter::SetCombatMovement(bool)` | `Character/RPG_BaseCharacter.h:102` / `.cpp:408` | `ARPG_AIController::SetCombatState()` 的第二个副作用（切 `MaxWalkSpeed`） |
| `ARPG_BaseCharacter::GetCombatComponent()` | `Character/RPG_BaseCharacter.h:116` | `URPG_BTTask_Attack` 由此拿到输入缓存 |
| `ARPG_BaseCharacter::OnDeathStarted()` / `OnRespawned()` | `Character/RPG_BaseCharacter.h:184` 起 | 敌人侧挂 `StopAI()` / `RestartAI()` 的钩子；由 `URPG_GA_Death` 调用（`RPG_GA_Death.cpp:104`） |
| `ARPG_Enemy::PatrolPoints` / `AttackRange` / `LoseTargetDistance` | `Character/RPG_Enemy.h:58` / `:62` / `:66` | 巡逻点与攻击距离的数据来源（`AttackRange` 默认 250） |
| `URPG_AbilitySystemComponent::TryActivateAbilityByInputTag()` | `AbilitySystem/RPG_AbilitySystemComponent.h:152` | AI 与玩家共用的能力激活入口 |
| `RPGTags::Input_Attack_Light` / `RPGTags::State_Attacking` | `Core/RPG_GameplayTags.h` | BT 任务默认输入标签；攻击结束轮询的状态标签 |
| `URPG_GA_LightAttack` / `URPG_GA_HeavyAttack` | `AbilitySystem/Abilities/` | 输入缓存的真正消费者（`ConsumeInputTag` / `SetComboIndex` / `ResetCombo` 的调用方） |
| `URPG_AnimNotifyState_AttackWindow` | `Animation/Notifies/` | `URPG_AttackWindowPayload` 的唯一创建者 |
| `URPG_GameplayAbilityBase::GetCombatComponent()` / `GetAttackModule()` | `AbilitySystem/Abilities/RPG_GameplayAbilityBase.cpp:139` / `:151` | GA 侧访问战斗组件的统一封装（注意 `GetRPGCharacter()` 用 `AvatarActor` 而非 `OwnerActor`） |

---

# 六、战斗 HUD 与表现层

本章覆盖 `Source/RPG/UI/` 下的全部 6 组文件（12 个文件）。这一层是**纯表现层**：它只读 GAS 的状态、只画东西，不产生任何游戏结果（不扣血、不改标签、不发 RPC）。

**三层结构**

| 层 | 类 | 生命周期 | 职责 |
|---|---|---|---|
| 宿主 | `ARPG_HUD`（`AHUD`） | 每个本地玩家一份，引擎管创建/销毁 | 建主 HUD、托管伤害飘字（世界坐标 → 屏幕坐标） |
| 主界面 | `URPG_HUDWidget`（`UUserWidget`） | 跟着 `ARPG_HUD` | 订阅本地玩家 ASC、把属性/标签翻译成控件状态 |
| 子控件 | `URPG_AttributeBarWidget` / `URPG_OverheadHealthBarWidget` / `URPG_DamageNumberWidget` | 各自独立 | 只负责画；不知道自己的数据从哪来 |

外加一个 `URPG_OverheadHealthBarComponent`（`UWidgetComponent`），它存在的唯一理由是**把"我是谁的血条"显式传给控件**。

**两条贯穿本章的取舍**（`RPG_HUDWidget.h` 的类注释里写死了，其它文件也遵守）：

1. **连续量走委托，离散状态每帧读标签。**
   血量/法力/耐力/攻防 → `GetGameplayAttributeValueChangeDelegate`（属性有"值"、会变，委托能在变化那一刻给出新旧值，客户端属性复制到达时引擎也会自动广播）；
   是否在蓄力、招式名、闪避、死亡 → `HasMatchingGameplayTag` 每帧查（布尔状态，注册 6~8 个标签事件再各自维护解绑是代码量翻倍；标签查询本身就是一次哈希表查找）。这套写法和 `URPG_AnimInstanceBase::UpdateCombatState()` 一致，是项目惯例。
2. **表现层不读 GA 的私有成员，也不信 `IsValid()` 能判"控件还在不在"。**
   前者见蓄力条（GA 会被回收、私有成员读不到、0.1 秒才更新一次）；后者见 `PruneDamageNumbers()`（要用 `IsInViewport()`）。

---

### `Source/RPG/UI/RPG_HUD.h` / `Source/RPG/UI/RPG_HUD.cpp`

**一句话职责**：本地玩家 HUD 的宿主 Actor —— 在 `BeginPlay` 里创建主 HUD 控件，并充当"伤害飘字"的落点（世界坐标 → 屏幕坐标 → 造控件）。

**类/结构体**：`ARPG_HUD` —— 继承 `AHUD`。每个本地玩家一份，是"本地表现"的边界。

> 类注释给出的三条选型理由（为什么用 `AHUD` 而不是在 `PlayerController` 里 `CreateWidget`）：
> · `AHUD` 天生是"每个本地玩家一份"的语义，引擎负责在正确时机创建/销毁它；放在 `PlayerController` 里的话，那片代码要自己处理"这台机器上哪些 PC 是本地的"（listen server 主机上有多个 `PlayerController`，只有一个是自己的）。
> · `PlayerController` 已经有一件正事：把输入翻译成能力激活。再塞进 UI 的创建、销毁、输入模式切换，那个类会变成什么都往里装的抽屉。
> · 引擎自带的 `HUDClass` 配置位本来就为此准备着 —— 在 GameMode 里指定，零代码切换。（实际指定点：`ARPG_GameModeBase` 构造函数里 `HUDClass = ARPG_HUD::StaticClass();`，见 `RPG_GameModeBase.cpp:41`。）
>
> 另一段注释说明它**同时是飘字的落点**：角色那边把"谁挨了多少伤害、在哪个世界坐标"通过 `NetMulticast` 广播到各端，各端再由**自己那个** `AHUD` 变成屏幕上的数字。这一层中转是必要的 —— 世界坐标 → 屏幕坐标要相机信息，而相机是每个客户端各自的。

包含：`CoreMinimal.h`、`GameFramework/HUD.h`、`RPG_HUD.generated.h`；前置声明 `URPG_HUDWidget`、`URPG_DamageNumberWidget`。

---

#### `ARPG_HUD::ARPG_HUD()`
- **干什么**：唯一的动作是把 `PrimaryActorTick.bCanEverTick = true`。
- **关键实现**：只有一行赋值。
- **为什么这么写**：注释原话 —— 飘字的"跟随世界坐标"模式（`bDamageNumbersTrackWorld`）靠 `AHUD::Tick` 每帧重新投影位置，所以这里显式打开 tick；并说明 `AActor` 默认关 tick，但 **`AHUD` 的构造函数里已经打开了 —— 这一句是冗余的**。留着是因为它把"本类依赖 Tick"这个事实写在代码里，将来若有人把它关掉，至少能找到一处说明为什么不该关。
- **被谁调用 / 调用谁**：引擎实例化 `AHUD` 时调用；不调用任何东西。

#### `void ARPG_HUD::ShowDamageNumber(float Amount, const FVector& WorldLocation)`
- **干什么**：在指定的世界坐标冒一个伤害数字。**注意：不是 `UFUNCTION`**，是普通 C++ 成员函数。
- **关键实现**（按代码顺序，6 个早退分支 + 1 个溢出裁剪）：
  1. `if (!DamageNumberWidgetClass) return;` —— **静默**返回，不打日志。
  2. `ProjectToScreen(WorldLocation, ScreenPosition)`，返回 `false` 直接 `return`（相机背后的点不显示）。
  3. `APlayerController* PC = GetOwningPlayerController();` 为空则 `return`。
  4. **散开**：`if (DamageNumberScatterRadius > 0.f)`，`ScreenPosition.X += FMath::FRandRange(-R, R)`、`ScreenPosition.Y += FMath::FRandRange(-R*0.4f, R*0.4f)`（X 全幅、Y 只有 40%）。
  5. `CreateWidget<URPG_DamageNumberWidget>(PC, DamageNumberWidgetClass)`；为空则 `return`。
  6. **先入屏再初始化**：`Widget->AddToViewport(/*ZOrder=*/10)` → `Widget->InitializeDamageNumber(Amount, ScreenPosition, /*InLifetime=*/0.f)`（`0.f` 表示用控件自己的 `DefaultLifetime`）。
  7. 两个数组同步 `Add`：`ActiveDamageNumbers.Add(Widget)`、`ActiveDamageNumberLocations.Add(WorldLocation)`。
  8. **溢出裁剪**：`if (ActiveDamageNumbers.Num() > MaxDamageNumbers)` → `Overflow = Num - MaxDamageNumbers`，正序遍历 `[0, Overflow)`，对 `IsValid(...)` 的元素调 `RemoveFromParent()`；然后两个数组各 `RemoveAt(0, Overflow, EAllowShrinking::No)`。即**最旧的先消失**。
- **为什么这么写**（逐条对应注释）：
  · 没配 Widget 类时静默返回：飘字是纯锦上添花，缺了不影响任何玩法验证，"每次挨打都打一条 Warning 只会刷屏"（与 `HUDWidgetClass` 缺失时的 Warning 处理刻意不同）。
  · 散开的原因：同一帧多个伤害（AOE、连段多段判定）会投影到几乎同一个点，数字完全重叠就只剩最上面那个看得见；用随机偏移，**不需要确定性，纯观感**。
  · 先 `AddToViewport` 再 `InitializeDamageNumber`：注释说两个顺序其实都可以（位置和对齐走的是 `GameViewportSubsystem`，槽位信息在控件还没上屏时也会被存下来，之后 `AddToViewport` 会重新捡起它），但"先入屏再摆位置"更符合直觉，也避免依赖那套内部行为。
  · 上限的原因：群怪混战时飘字能轻松堆到上百个，每个都是一份 Slate 控件，不设上限一场大乱斗能让帧率掉一截，而且屏幕上糊成一片也看不清。
  · `ZOrder = 10`：飘字盖在 ZOrder 0 的主 HUD 之上。
- **被谁调用 / 调用谁**：
  · 被 `ARPG_BaseCharacter::Multicast_ShowDamageNumber_Implementation()`（`RPG_BaseCharacter.cpp:303`）调用 —— 后者遍历 `World->GetPlayerControllerIterator()` 挑出 `IsLocalController()` 的那个 PC，再 `PC->GetHUD<ARPG_HUD>()`；而那个 Multicast 由 `URPG_AttributeSet::PostGameplayEffectExecute()` 在**服务器**上发起（`RPG_AttributeSet.cpp:240`，外面套着 `if (bAuthority)`）。
  · 调用 `ProjectToScreen()`、`CreateWidget<URPG_DamageNumberWidget>()`、`URPG_DamageNumberWidget::InitializeDamageNumber()`、`URPG_DamageNumberWidget::AddToViewport()`、`URPG_DamageNumberWidget::RemoveFromParent()`。

#### `URPG_HUDWidget* ARPG_HUD::GetHUDWidget() const`
- **干什么**：`BlueprintPure` 的内联 getter，返回主 HUD 控件指针。
- **关键实现**：`return HUDWidget;`。
- **为什么这么写**：注释未说明。
- **被谁调用 / 调用谁**：**本仓库 C++ 内未见调用点**（给蓝图用的取用口，未核实蓝图侧是否真的用了）。

#### `virtual void ARPG_HUD::BeginPlay()`
- **干什么**：确认自己是本地 HUD，然后创建并挂上主 HUD 控件。
- **关键实现**（顺序）：
  1. `Super::BeginPlay();`
  2. **本地性判断**：`if (APlayerController* PC = GetOwningPlayerController()) { if (!PC->IsLocalController()) return; }` —— 注意写法是"拿得到 PC 且不是本地才返回"，拿不到 PC 时**不**返回。
  3. `if (!HUDWidgetClass)` → `UE_LOG(LogRPG_Combat, Warning, ...)` 并 `return`。日志文案明确写着"界面上不会显示任何东西，请在 BP_RPG_HUD 的 Class Defaults 里指定 WBP_RPG_HUD"。
  4. `HUDWidget = CreateWidget<URPG_HUDWidget>(GetOwningPlayerController(), HUDWidgetClass);` 为空 → `UE_LOG(..., Error, ...)` 并 `return`。
  5. `HUDWidget->AddToViewport(/*ZOrder=*/0);`
  6. `UE_LOG(LogRPG_Combat, Log, "[%s] HUD 已创建：%s", ...)`。
- **为什么这么写**：
  · 本地性判断**是冗余的**，注释自己承认了：引擎的 `SpawnDefaultHUD` 对非 `ULocalPlayer` 直接返回（引 `PlayerController.cpp:1180-1185`），配置的 HUD 类只会在拥有它的客户端上生成（`GameModeBase` 的 `ClientSetHUD` → `PlayerController::ClientSetHUD`），所以 listen server 主机上不会为远程玩家凭空多出几个 `AHUD`。保留它的理由："它便宜，而且把'HUD 属于本地玩家'这个语义写在了代码里，而不是靠读者知道引擎那条实现细节。单机（Standalone）下恒为 true，不影响单人开发。"
  · `HUDWidgetClass` 缺失故意用 Warning 而不是 Error：想临时看纯画面的场景（截图、录演示）是有用的；但**一定要报**，否则表现是"进游戏什么都没有"，会让人去查 Widget 是不是没显示。
  · `ZOrder = 0`：HUD 在底层，将来加背包/菜单时用更大的 ZOrder 盖在上面；显式写出来是为了把"层级顺序"变成代码里可见的约定。
- **被谁调用 / 调用谁**：引擎在 `AActor::BeginPlay` 派发时调用；调用 `CreateWidget`、`URPG_HUDWidget::AddToViewport`。

#### `virtual void ARPG_HUD::EndPlay(const EEndPlayReason::Type EndPlayReason)`
- **干什么**：显式摘掉主 HUD 控件和所有飘字记录。
- **关键实现**：
  1. `if (HUDWidget) { HUDWidget->RemoveFromParent(); HUDWidget = nullptr; }`
  2. `ActiveDamageNumbers.Reset(); ActiveDamageNumberLocations.Reset();`（**不是**逐个 `RemoveFromParent`）
  3. `Super::EndPlay(EndPlayReason);`
- **为什么这么写**：注释原话 —— 关卡切换/玩家退出时，Slate 那边会异步销毁这些控件，而 `HUDWidget` 指针会变成悬垂；虽然 `UPROPERTY` 会拦住 GC，但"先自己收拾干净"是更稳的写法。飘字同理："它们本该自己把自己移除，但关卡切换时来不及走完那一帧。"
- **被谁调用 / 调用谁**：引擎；调用 `URPG_HUDWidget::RemoveFromParent`。

#### `virtual void ARPG_HUD::Tick(float DeltaSeconds)`
- **干什么**：清理已结束的飘字；若开了跟随模式则每帧重投影。
- **关键实现**：
  1. `Super::Tick(DeltaSeconds);`
  2. `PruneDamageNumbers();` —— **无条件**先清理。
  3. `if (!bDamageNumbersTrackWorld) return;`
  4. `const int32 Count = FMath::Min(ActiveDamageNumbers.Num(), ActiveDamageNumberLocations.Num());` 然后 `for (Index < Count)`：取 `Widget`，`if (!IsValid(Widget)) continue;`；`ProjectToScreen(ActiveDamageNumberLocations[Index], ScreenPosition)` 成功才调 `Widget->UpdateScreenPosition(ScreenPosition)`。**投影失败（点在相机背后）时不更新**，飘字停在上一帧的位置。
- **为什么这么写**：清理放在 Tick 开头而不是 `ShowDamageNumber` 里 —— 注释原话："飘字的死亡和生成是两件独立的事，只在生成时清理的话，一段时间不打架就永远留着陈旧的指针。" 跟随模式的开关取舍见头文件（默认 false：屏幕坐标只在生成时算一次，飘字活不到 1 秒，镜头造成的偏移看不出来，还省掉每帧 N 次投影；true 时数字"钉"在敌人身上，代价是每帧几十次 `ProjectWorldToScreen`）。
- **被谁调用 / 调用谁**：引擎；调用 `PruneDamageNumbers()`、`ProjectToScreen()`、`URPG_DamageNumberWidget::UpdateScreenPosition()`。

#### `bool ARPG_HUD::ProjectToScreen(const FVector& WorldLocation, FVector2D& OutScreenPosition) const`（private）
- **干什么**：把世界坐标投影成屏幕坐标；在相机背后时返回 `false`。
- **关键实现**：`if (!PlayerOwner) return false;` 然后 `return PlayerOwner->ProjectWorldLocationToScreen(WorldLocation, OutScreenPosition, /*bPlayerViewportRelative=*/false);`
- **为什么这么写**：注释原话 —— `ProjectWorldLocationToScreen` 的返回值含义是"这个点在不在相机前面"，**一定要判它**。不判的话，角色背后的点会被投影成一组**镜像的合法坐标**，表现是"敌人在我身后挨打，屏幕另一侧冒出数字"；这个 bug 在第三人称里特别容易被忽略（因为玩家很少注意身后）。
- **被谁调用 / 调用谁**：被 `ShowDamageNumber()`、`Tick()` 调用；调用 `APlayerController::ProjectWorldLocationToScreen`。

#### `void ARPG_HUD::PruneDamageNumbers()`（private）
- **干什么**：把已经自己结束的飘字从两个数组里摘掉。
- **关键实现**：**倒序**遍历 `ActiveDamageNumbers`；`const bool bStillAlive = IsValid(Widget) && Widget->IsInViewport();`；`if (!bStillAlive)` → 两个数组各 `RemoveAt(Index, 1, EAllowShrinking::No)`。
- **为什么这么写**（注释里一段很长的教训）：
  · 倒序的原因：`RemoveAt` 会挪动后面的元素，"正序遍历 + RemoveAt 是经典的漏删 bug（相邻两个元素里会跳过一个）"。
  · **判活不能只靠 `IsValid()`**：飘字寿命到了会调 `RemoveFromParent()`，但那个函数**不会**把对象标记成待回收（它只是把自己从 `GameViewportSubsystem` 里摘掉，注释引了 `GameViewportSubsystem` 的 `RemoveWidget`）；而 `ActiveDamageNumbers` 是个 `UPROPERTY` 数组，持的是**强引用**，GC 永远不会回收它们。两个因素加起来 —— `!IsValid(...)` 永远为 false，清理循环一个都删不掉，数组里会长期挂着几十个已经不在屏幕上的死控件，而且 `bDamageNumbersTrackWorld` 那条路径还会继续对它们调 `UpdateScreenPosition`。正确判据是"还在不在 viewport 里"，那才是 `RemoveFromParent` 真正改变的状态。
- **被谁调用 / 调用谁**：被 `Tick()` 调用；调用 `URPG_DamageNumberWidget::IsInViewport()`。

#### 成员变量与 UPROPERTY

| 名称 | 声明 | 默认值 | 说明 |
|---|---|---|---|
| `HUDWidgetClass` | `UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="RPG\|UI")` `TSubclassOf<URPG_HUDWidget>` | 空 | 主 HUD 的 Widget 类。**WBP 侧**：在 `BP_RPG_HUD` 的 Class Defaults 里指定 `WBP_RPG_HUD`；留空则 HUD 什么都不显示并打一条 Warning —— 头文件注释说保留这个空状态是有用的（"想单独看画面的时候不用改代码"） |
| `DamageNumberWidgetClass` | 同上，`TSubclassOf<URPG_DamageNumberWidget>` | 空 | 伤害飘字的 Widget 类。**WBP 侧**：在 `BP_RPG_HUD` 的 Class Defaults 里指定 `WBP_DamageNumber`；留空时飘字功能静默关闭 |
| `bDamageNumbersTrackWorld` | `UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, ...)` `bool` | `false` | 飘字是否每帧跟随世界坐标。取舍见头文件（见上文 `Tick`） |
| `MaxDamageNumbers` | `UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, ..., meta=(ClampMin="1"))` `int32` | `32` | 飘字最多同时存在多少个，超过时**最旧的先消失**，防止群怪混战刷屏 |
| `DamageNumberScatterRadius` | `UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, ..., meta=(ClampMin="0.0"))` `float` | `22.f` | 同一帧内多个飘字的位置散开半径（像素），避免数字完全重叠看不清 |
| `HUDWidget` | `UPROPERTY()` `TObjectPtr<URPG_HUDWidget>` | 空 | private。主 HUD 控件实例 |
| `ActiveDamageNumbers` | `UPROPERTY()` `TArray<TObjectPtr<URPG_DamageNumberWidget>>` | — | private。活着的飘字。注释里两条警告：用 `TObjectPtr` 数组而不是裸指针（飘字是**自己**在 Tick 末尾调 `RemoveFromParent`，裸指针下一帧就是悬垂）；**不能**靠 `IsValid()` 判断它还在不在（见 `PruneDamageNumbers`） |
| `ActiveDamageNumberLocations` | `TArray<FVector>`（**没有 UPROPERTY**） | — | private。与 `ActiveDamageNumbers` 一一对应的世界坐标，仅在 `bDamageNumbersTrackWorld` 时用得上。因为不是 `UPROPERTY`，它不参与 GC —— 里面存的只是 `FVector` 值，无所谓 |

**类级别注意**：`ARPG_HUD` **没有** `UCLASS(Abstract)`，可以直接实例化/被蓝图继承（实际用法是 `BP_RPG_HUD`）。

---

### `Source/RPG/UI/RPG_HUDWidget.h` / `Source/RPG/UI/RPG_HUDWidget.cpp`

**一句话职责**：主 HUD 控件 —— 订阅本地玩家 ASC 的属性委托刷三条属性条与攻防数值，每帧读 GameplayTag 刷闪避图标、招式区、蓄力条、死亡面板与重生倒计时。

**类/结构体**：`URPG_HUDWidget` —— 继承 `UUserWidget`，`UCLASS(Abstract)`。**必须由 WBP 继承后使用（`WBP_RPG_HUD`）**。

> 头文件顶部有一张布局 ASCII 图，标明了设计意图：左下角是三条属性条 + 攻防数值，右下角是招式名 + 蓄力条。
>
> 类注释三段：
> · **两套数据源**（连续量委托 / 布尔状态轮询）—— 见本章开头的总述。注释里明说"这个取舍本身就是个面试话题"。
> · **为什么要有"惰性绑定"**：HUD 是在 `AHUD::BeginPlay` 里创建的，那一刻玩家的 `PlayerState`（ASC 的宿主）可能还没复制过来 —— 联机时尤其如此。绑不上**不会报错**，表现是"整条 HUD 永远不动"，非常难查。所以 `NativeTick` 里会一直重试到接上为止，代价只是一个空指针比较。

---

#### `URPG_HUDWidget::URPG_HUDWidget(const FObjectInitializer& ObjectInitializer)`
- **干什么**：构造函数体是空的（`: Super(ObjectInitializer) {}`）。
- **关键实现**：无。
- **为什么这么写**：`.cpp` 里这个函数**上方**有一大段注释，是这个类最重要的隐式依赖说明，原文要点：
  · UMG 不会无条件给 Widget 开 tick。`UUserWidget::UpdateCanTick()`（引 `UserWidget.cpp:2347-2381`）只在下列任意一条成立时才开：① 蓝图侧实现了 Tick 事件（`bHasScriptImplementedTick`）；② 派生它的 WBP 里有动画、或有 Latent 节点；③ **本类没有被标记 `DisableNativeTick`** —— 编译器读的就是这个 meta 标签（引 `WidgetBlueprint.cpp:1562-1563`：`bClassRequiresNativeTick = !NativeParent->HasMetaData("DisableNativeTick")`）；④ WBP 的 Class Defaults → Tick Frequency 是 `Auto`（默认值）。
  · 本类靠第三条拿到 tick。所以两条禁令：**① 不要给 `URPG_HUDWidget` 加 `UCLASS(meta = (DisableNativeTick))`；② 不要在 WBP 里把 Tick Frequency 改成 `Never`。** 这两个都能编译通过、也能正常显示，只是**永远停在初始状态**（血量不动、蓄力条不走、死亡面板不弹），而且一条日志都没有。
  · 注释明说"这里没有在构造函数据做什么，写下这段是为了让这个隐式依赖是可见的"。
- **被谁调用 / 调用谁**：由 `CreateWidget` 调用；不调用任何东西。

#### `void URPG_HUDWidget::RefreshAllAttributes()`
- **干什么**：按当前属性值整表刷一次（遍历 `BoundAttributes` 逐个调 `RefreshAttribute`）。
- **关键实现**：`for (const FGameplayAttribute& Attribute : BoundAttributes) { RefreshAttribute(Attribute); }` —— **没有** ASC 判空，因为 `RefreshAttribute` 内部自己判。
- **为什么这么写**：函数本体没注释；调用点（`TryBindToLocalPlayer()` 末尾）的注释说明了它存在的理由 —— 绑完立刻整表刷一次，不刷的话要等第一次属性变化，**满血的玩家会看到三条空条，直到他挨第一刀**；这是纯 UI bug，但看起来像"属性没初始化"。
- **被谁调用 / 调用谁**：被 `TryBindToLocalPlayer()` 调用；`UFUNCTION(BlueprintCallable)` 也允许蓝图主动调用；调用 `RefreshAttribute()`。

#### `bool URPG_HUDWidget::IsBound() const`
- **干什么**：返回"当前是否已经接上本地玩家的 ASC"。
- **关键实现**：内联 `return BoundASC.IsValid();`。
- **为什么这么写**：注释未说明。
- **被谁调用 / 调用谁**：`UFUNCTION(BlueprintPure)`，本仓库 C++ 内未见调用点（供蓝图/调试用，未核实蓝图侧是否使用）。

#### `virtual void URPG_HUDWidget::NativeConstruct()`
- **干什么**：尝试绑一次 ASC。
- **关键实现**：`Super::NativeConstruct();` → `TryBindToLocalPlayer();`（**不检查返回值**）。
- **为什么这么写**：`.cpp` 里这句的注释是"尝试绑一次。绑不上也没关系 —— `NativeTick` 会一直重试。"（头文件里则解释了为什么联机时第一次多半绑不上。）
- **被谁调用 / 调用谁**：UMG 生命周期；调用 `TryBindToLocalPlayer()`。

#### `virtual void URPG_HUDWidget::NativeDestruct()`
- **干什么**：解绑所有属性委托。
- **关键实现**：`UnbindFromASC();` 然后 `Super::NativeDestruct();`（**先解绑再 Super**）。
- **为什么这么写**：注释未说明（`UnbindFromASC` 内部有详细理由）。
- **被谁调用 / 调用谁**：UMG 生命周期；调用 `UnbindFromASC()`。

#### `virtual void URPG_HUDWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)`
- **干什么**：惰性重试绑定 + 刷战斗状态。
- **关键实现**：
  1. `Super::NativeTick(...)`；
  2. `if (!BoundASC.IsValid()) { TryBindToLocalPlayer(); }`；
  3. `UpdateCombatState(InDeltaTime);` —— **无条件**调用（即使没绑上，`UpdateCombatState` 自己会判空返回）。
- **为什么这么写**：惰性绑定的理由见头文件；`.cpp` 注释补充"每次失败只是一个 WeakPtr 判空 + 一次 Cast，开销可以忽略"。
- **被谁调用 / 调用谁**：Slate/UMG 每帧；调用 `TryBindToLocalPlayer()`、`UpdateCombatState()`。

#### `ARPG_BaseCharacter* URPG_HUDWidget::GetLocalCharacter() const`（protected）
- **干什么**：取本地玩家角色，取不到返回 `nullptr`。
- **关键实现**：`return Cast<ARPG_BaseCharacter>(GetOwningPlayerPawn());`
- **为什么这么写**：注释原话 —— `GetOwningPlayerPawn()`：这个 Widget 属于哪个玩家，就返回哪个 Pawn；HUD 是由本地 `PlayerController` 创建的，所以拿到的就是本地玩家。（这一点和头顶血条正相反，那里 `GetOwningPlayerPawn()` 是**错的**。）
- **被谁调用 / 调用谁**：被 `GetLocalCombatComponent()`、`TryBindToLocalPlayer()`、`UpdateCombatState()`（取 `RespawnDelay`）调用；调用 `UUserWidget::GetOwningPlayerPawn()`。

#### `URPG_CombatComponent* URPG_HUDWidget::GetLocalCombatComponent() const`（protected）
- **干什么**：取当前玩家的战斗组件（连段索引/攻击模组用），取不到返回 `nullptr`。
- **关键实现**：`const ARPG_BaseCharacter* Character = GetLocalCharacter(); return Character ? Character->GetCombatComponent() : nullptr;`
- **为什么这么写**：注释未说明（头文件只写了"连段索引用"）。
- **被谁调用 / 调用谁**：被 `UpdateSkillPanel()` 调用（两次：取招式名的段位、取蓄力满格时间）；调用 `ARPG_BaseCharacter::GetCombatComponent()`。

#### `bool URPG_HUDWidget::TryBindToLocalPlayer()`（protected）
- **干什么**：绑定本地玩家 ASC 上的 8 个属性变化委托；返回是否绑上（注意：返回值在 `NativeTick`/`NativeConstruct` 里都没被使用）。
- **关键实现**（顺序）：
  1. `GetLocalCharacter()`，为空 → `return false`。
  2. `Character->GetAbilitySystemComponent()`，为空 → `return false`。
  3. `BoundASC = ASC;`
  4. `BoundAttributes.Reset(); AttributeDelegateHandles.Reset();` —— 先清空再重建（防止重复绑定时数组叠加）。
  5. 依次 `Add` 8 个属性访问器：`GetHealthAttribute`、`GetMaxHealthAttribute`、`GetManaAttribute`、`GetMaxManaAttribute`、`GetStaminaAttribute`、`GetMaxStaminaAttribute`、`GetAttackAttribute`、`GetDefenseAttribute`。
  6. `for (const FGameplayAttribute& Attribute : BoundAttributes)`：`AttributeDelegateHandles.Add(ASC->GetGameplayAttributeValueChangeDelegate(Attribute).AddUObject(this, &URPG_HUDWidget::HandleAttributeChanged));` —— **保存返回的 handle**。
  7. `RefreshAllAttributes();`
  8. `UE_LOG(LogRPG_Combat, Log, "[%s] HUD 已接上本地玩家的 ASC（%s）", ...)`（用 `GetNameSafe(Character)`）。
  9. `return true;`
- **为什么这么写**：
  · 走角色基类接口而不是直接找 `PlayerState` —— 注释原话："'玩家的 ASC 挂在 PlayerState 上'这个差异已经被基类抹平了，HUD 不该知道这件事。"（`ARPG_BaseCharacter` 实现了 `IAbilitySystemInterface`。）
  · **一次绑 8 个属性、共用一个回调**：分派逻辑放在 `HandleAttributeChanged` 里按 `Data.Attribute` 走；写 8 个独立回调函数的话，每个都要重复一遍"读最大值、算比例"，而且加属性时要记得再加一个函数 —— 忘了就是"某个条永远不动"。
  · 用 `AddUObject` 而不是 `AddDynamic`：这是引擎的**非动态**多播委托。触发时机有两处，正好覆盖单机和联机 —— ① 服务器上属性被 GE 改动的瞬间；② 客户端上 `GAMEPLAYATTRIBUTE_REPNOTIFY` 把复制来的值写进属性时。
- **被谁调用 / 调用谁**：被 `NativeConstruct()`、`NativeTick()` 调用；调用 `GetLocalCharacter()`、`ARPG_BaseCharacter::GetAbilitySystemComponent()`、`RefreshAllAttributes()`、`UAbilitySystemComponent::GetGameplayAttributeValueChangeDelegate()`。

#### `void URPG_HUDWidget::UnbindFromASC()`（protected）
- **干什么**：逐个解绑那 8 个属性委托并清空状态。
- **关键实现**：
  1. `UAbilitySystemComponent* ASC = BoundASC.Get();`
  2. **若 ASC 为空**：`AttributeDelegateHandles.Reset(); BoundAttributes.Reset();` 然后 `return;` —— 注意这条分支里**没有** `BoundASC.Reset()`（此时 WeakPtr 本来就已经取不到对象，重置与否等价，但读代码时要知道它不像另一条分支那样显式清）。
  3. `const int32 Count = FMath::Min(AttributeDelegateHandles.Num(), BoundAttributes.Num());` —— 用 `Min` 兜底两个数组不等长的情况。
  4. `for (Index < Count)`：`ASC->GetGameplayAttributeValueChangeDelegate(BoundAttributes[Index]).Remove(AttributeDelegateHandles[Index]);`
  5. `AttributeDelegateHandles.Reset(); BoundAttributes.Reset(); BoundASC.Reset();`
- **为什么这么写**：`★` 注释原话 —— 用保存下来的 handle 逐个解绑，**而不是 `RemoveAll`**。`RemoveAll` 会把**别人**（比如头顶血条、将来的其他 UI）注册在同一个属性上的回调一起摘掉 —— 症状是"开过一次菜单之后敌人血条就不动了"，而且完全查不到原因。
- **被谁调用 / 调用谁**：被 `NativeDestruct()` 调用；调用 `UAbilitySystemComponent::GetGameplayAttributeValueChangeDelegate()` + `Remove()`。

#### `void URPG_HUDWidget::HandleAttributeChanged(const FOnAttributeChangeData& Data)`（protected）
- **干什么**：8 个属性共用的回调，转手交给 `RefreshAttribute(Data.Attribute)`。
- **关键实现**：一行 `RefreshAttribute(Data.Attribute);` —— 完全不看 `Data.OldValue` / `Data.NewValue`。
- **为什么这么写**：头文件注释"8 个属性共用一个回调，在内部按 `Data.Attribute` 分派"；不看新旧值的原因见 `RefreshAttribute`（比例要的是属性集当前值，而委托一次只送一个属性）。
- **被谁调用 / 调用谁**：被 ASC 的属性变化委托调用；调用 `RefreshAttribute()`。

#### `void URPG_HUDWidget::RefreshAttribute(const FGameplayAttribute& Attribute)`（protected）
- **干什么**：读一次某个属性的当前值并刷新对应的控件。
- **关键实现**：
  1. `UAbilitySystemComponent* ASC = BoundASC.Get();` 为空 → `return`。
  2. `const URPG_AttributeSet* AttributeSet = ASC->GetSet<URPG_AttributeSet>();` 为空 → `return`。
  3. **无条件**读 6 个局部量：`Health`、`MaxHealth`、`Mana`、`MaxMana`、`Stamina`、`MaxStamina`。
  4. `if / else if` 链（**不是**全部刷一遍）：
     · `Attribute == GetHealthAttribute() || Attribute == GetMaxHealthAttribute()` → `HealthBar->SetAttributeValues(Health, MaxHealth)`
     · `Mana || MaxMana` → `ManaBar->SetAttributeValues(Mana, MaxMana)`
     · `Stamina || MaxStamina` → `StaminaBar->SetAttributeValues(Stamina, MaxStamina)`
     · `Attack` → `AttackText->SetText(FText::Format(AttackValueFormat, FText::AsNumber(FMath::RoundToInt(AttributeSet->GetAttack()))))`
     · `Defense` → `DefenseText->SetText(... DefenseValueFormat ... Defense())`
     · 没有 `else` 兜底分支。
  5. 每个分支里都先判控件指针非空（控件是 `BindWidgetOptional`，WBP 里可能没建）。
- **为什么这么写**：
  · **`MaxHealth` 变化时也要刷血条**（`.cpp` 里一段 `★` 注释）：血条显示比例是 `Current / Max`，最大值变了（吃了个加生命的 Buff、或者 Buff 到期掉回去），**当前值可能一点没变**，但血条该变长/变短 —— 因为"满血"的定义变了。只监听 `Health` 的话表现是"血量上限变了但条没动，要掉一次血才对齐"。
  · 用 if-else 链而不是全刷 8 个：每次挨打会触发 1~2 个属性回调，全刷 8 个属性 + 8 个控件是纯浪费；属性条内部的 `SetPercent` 会触发排版，能省则省。
- **被谁调用 / 调用谁**：被 `HandleAttributeChanged()`、`RefreshAllAttributes()` 调用；调用 `URPG_AttributeBarWidget::SetAttributeValues()`、`UTextBlock::SetText()`、`UAbilitySystemComponent::GetSet<>()`。

#### `void URPG_HUDWidget::UpdateCombatState(float InDeltaTime)`（protected）
- **干什么**：每帧读标签，刷闪避图标、蓄力计时、招式区、死亡面板与重生倒计时。这是"布尔状态用轮询"那半边的总入口。
- **关键实现**（顺序）：
  1. `UAbilitySystemComponent* ASC = BoundASC.Get();` 为空 → `return`（**整个函数早退**，连 `bWasCharging`/`bWasDead` 都不更新）。
  2. **闪避图标**：`if (DodgeIcon)` → `bDodging = ASC->HasMatchingGameplayTag(RPGTags::State_Dodging)` → `SetVisibility(bDodging ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden)`。
  3. **蓄力计时**：`bCharging = ASC->HasMatchingGameplayTag(RPGTags::State_Attack_Charging)`；
     · `if (bCharging)`：`if (!bWasCharging) ChargeDisplayTime = 0.f;`（边沿：刚开始蓄力 → 计时归零重新数）然后 `ChargeDisplayTime += InDeltaTime;`
     · `else`：`ChargeDisplayTime = 0.f;`
     · 最后 `bWasCharging = bCharging;`（**无论哪个分支都写**）。
  4. `UpdateSkillPanel(bCharging);` —— 把本帧的蓄力状态**作为参数**传进去。
  5. **死亡**：`bDead = ASC->HasMatchingGameplayTag(RPGTags::State_Dead)`；
     · `if (bDead)`：`if (!bWasDead)` → `const ARPG_BaseCharacter* Character = GetLocalCharacter(); RespawnCountdown = Character ? Character->GetRespawnDelay() : 0.f;`（边沿：刚死那一帧用角色上配的重生时长启动倒计时）；`else` → `RespawnCountdown = FMath::Max(0.f, RespawnCountdown - InDeltaTime);`
     · `else` → `RespawnCountdown = 0.f;`
     · `bWasDead = bDead;`
  6. **死亡面板**：`if (DeathPanel)` → `SetVisibility(bDead ? HitTestInvisible : Hidden)`。
  7. **倒计时文本**：`if (RespawnCountdownText)` → **再取一次** `Character` 与 `RespawnDelay`；`bWillRespawn = bDead && RespawnDelay > 0.f`；`SetVisibility(bWillRespawn ? HitTestInvisible : Hidden)`；`if (bWillRespawn)` → `SetText(FText::Format(RespawnCountdownFormat, FText::AsNumber(FMath::CeilToInt(RespawnCountdown))))`。
- **为什么这么写**（注释逐条给了理由）：
  · 闪避图标：""闪避时亮起，之后熄灭（常态）" —— 直接映射成标签在不在。"
  · 蓄力归零：松手/被打断/耐力耗尽强制释放三种结束方式都走到同一个 `else`，因为它们的共同点是 `State.Attack.Charging` 被摘掉了。注释原话："这就是需求里'右键松开后条归零'的实现：不需要订阅输入事件，标签消失本身就是最可靠的信号。"
  · 倒计时在**客户端自己数**：`State.Dead` 是复制的、重生时长是配置常量，两端都能拿到 —— 不需要服务器再复制一个"还剩几秒"。
  · ⚠️ `RespawnDelay = 0` 表示**不重生**（注释说是敌人的默认值），这时候不能显示"0 秒后重生" —— "那是在骗玩家，他会一直等着复活"。所以直接隐藏倒计时，只留一句"你死了"。
  · 剩余秒数**向上取整**：显示 "3 秒后重生" 应该对应剩 2.1 秒，而不是剩 3.0 秒的那一瞬间 —— "玩家看到 0 的时候正好该复活"。
  · 所有可见性都用 `HitTestInvisible` 而不是 `Visible`：不吃鼠标事件（这一点在 `URPG_OverheadHealthBarWidget::RevealForDamage()` 里有更明确的表述）。
- **被谁调用 / 调用谁**：被 `NativeTick()` 调用；调用 `UAbilitySystemComponent::HasMatchingGameplayTag()`、`GetLocalCharacter()`、`UpdateSkillPanel()`、`UWidget::SetVisibility()`、`UTextBlock::SetText()`。

#### `void URPG_HUDWidget::UpdateSkillPanel(bool bCharging)`（protected）
- **干什么**：刷新右下角招式区（整块显隐 + 招式名 + 蓄力条进度）。
- **关键实现**：
  1. `UAbilitySystemComponent* ASC = BoundASC.Get();` 为空 → `return`（`UpdateCombatState` 已经判过，这里是二次保护）。
  2. `FText MoveName; bool bShowPanel = false;`
  3. **判定顺序：切手 > 蓄力 > 轻击**（三个互斥分支）：
     · `HasMatchingGameplayTag(RPGTags::State_Attack_Transition)` → `MoveName = TransitionAttackText; bShowPanel = true;`
     · `else if (State_Attack_Charging)` → `MoveName = HeavyAttackText; bShowPanel = true;`（**注意：这里重新查了一次标签，用的不是传进来的 `bCharging`**）
     · `else if (State_Attacking)` → `const URPG_CombatComponent* Combat = GetLocalCombatComponent(); const int32 ComboIndex = Combat ? Combat->GetComboIndex() : 0;` → `if (ComboIndex > 0) { MoveName = FText::Format(LightAttackFormat, FText::AsNumber(ComboIndex)); bShowPanel = true; }`
  4. `if (SkillPanel)` → `SetVisibility(bShowPanel ? HitTestInvisible : Hidden)`。
  5. **招式名文本**：`if (MoveNameText && !MoveName.EqualTo(LastMoveName)) { LastMoveName = MoveName; MoveNameText->SetText(MoveName); }`（**脏检查**）。
  6. **蓄力条**：`if (ChargeBar)`：`float ChargePercent = 0.f;` `if (bCharging)` → 取 `Combat`、`Module = Combat ? Combat->GetAttackModule() : nullptr`；`float FullChargeTime = 0.f;` `if (Module)` → `const int32 LevelCount = Module->GetHeavyLevelCount(); if (const FRPG_HeavyAttackLevel* LastLevel = Module->GetHeavyLevel(LevelCount)) FullChargeTime = LastLevel->RequiredChargeTime;`（外层判 Module 非空，内层判 `GetHeavyLevel` 非空 —— `LevelCount` 越界时返回 `nullptr`）；`ChargePercent = FullChargeTime > KINDA_SMALL_NUMBER ? FMath::Clamp(ChargeDisplayTime / FullChargeTime, 0.f, 1.f) : 0.f;`。最后 `ChargeBar->SetPercent(ChargePercent);`（**无论蓄不蓄力都写**，非蓄力时写 0）。
- **为什么这么写**：
  · **顺序不能乱**：切手技和蓄力共用同一个 GA、同一个输入，判定依据是"激活瞬间在不在轻击连段里"。如果先判 `State.Attacking`（切手技激活时它可能还在），就会出现"放的是切手技、UI 显示轻击"。
  · 轻击段位从**战斗组件**读而不是数标签："第几段"本来就不是标签能表达的（没有上限）；注释原文"它才是连段索引的持有者"。连段索引 0 表示"不在连段中"，理论上不会和 `State.Attacking` 同时出现，"但真出现了也没必要显示'第 0 段'"。
  · 招式名**只在内容真变了**时写：`SetText` 会触发 Slate 的排版与失效重算，每帧无脑写等于每帧做一次文本布局 —— 而招式名一秒才变几次。
  · 蓄力条满格的定义 = **最后一段蓄力的时间门槛**，从数据资产读而不是写死（"DA 里把第三段改成 2.5 秒，UI 自动跟上"）。`⚠️` 注释明确说：**不用 GA 上的 `MaxChargeTime`** —— 那是"强制释放"的保护上限，通常比第三段门槛大；用它当分母的话，蓄满三段时条才走到一半，玩家会以为还能继续蓄。
  · `bCharging` 作为**参数**传进来而不是让函数去读 `bWasCharging`：头文件注释原话 —— 后者是个"必须在 `UpdateCombatState` 之后调用"的隐式约定，将来有人调整调用顺序就会静默出错（蓄力条永远不显示）。
- **被谁调用 / 调用谁**：被 `UpdateCombatState()` 调用；调用 `GetLocalCombatComponent()`、`URPG_CombatComponent::GetComboIndex()`、`URPG_CombatComponent::GetAttackModule()`、`URPG_AttackModuleData::GetHeavyLevelCount()` / `GetHeavyLevel()`、`URPG_AttributeBarWidget` 之外的 `UProgressBar::SetPercent()`、`UTextBlock::SetText()`。

#### `UPROPERTY` 绑定控件（WBP 侧契约）

> 头文件原话："名字即契约 —— WBP 里必须建同名控件。详见 `PHASE7_UI_SETUP.md`。"

| 名称 | 类型 | 绑定标记 | WBP 里要建什么 |
|---|---|---|---|
| `HealthBar` | `URPG_AttributeBarWidget*` | `BindWidgetOptional` | 血条，类型必须是 `WBP_AttributeBar` 的子类 |
| `ManaBar` | `URPG_AttributeBarWidget*` | `BindWidgetOptional` | 蓝条，同上 |
| `StaminaBar` | `URPG_AttributeBarWidget*` | `BindWidgetOptional` | 耐力条，同上 |
| `AttackText` | `UTextBlock*` | `BindWidgetOptional` | 攻击力数值 |
| `DefenseText` | `UTextBlock*` | `BindWidgetOptional` | 防御力数值 |
| `DodgeIcon` | `UImage*` | `BindWidgetOptional` | 闪避图标，只在 `State.Dodging` 存在时显示 |
| `SkillPanel` | `UWidget*` | `BindWidgetOptional` | 招式区的**整块容器**，不在出招时整块隐藏（`PHASE7_UI_SETUP.md` 推荐 `Canvas Panel` / `Size Box`） |
| `ChargeBar` | `UProgressBar*` | `BindWidgetOptional` | 蓄力进度条 |
| `MoveNameText` | `UTextBlock*` | `BindWidgetOptional` | 招式名文本 |
| `DeathPanel` | `UWidget*` | `BindWidgetOptional` | 死亡面板整块，`State.Dead` 存在时显示 |
| `RespawnCountdownText` | `UTextBlock*` | `BindWidgetOptional` | 重生倒计时文本 |

> **全是 `Optional`**：`PHASE7_UI_SETUP.md` 明确写"你可以先只做血条跑通，再逐步补。少哪个就少哪个功能，不会编译失败。" 代码侧对应地每个使用点都先判空。
> 注意 `BindWidgetOptional` 在**变量名与控件名不一致**时是**静默**失败（指针为 `nullptr`，不报错）；只有标了 `BindWidget` 而找不到/类型不对才会编译报错 —— 这一点在 `RPG_AttributeBarWidget.h` 的类注释里讲得最细。

**可配置文案（蓝图可改，不用重编 C++）**

| 名称 | 声明 | 默认文案 | 用途 |
|---|---|---|---|
| `LightAttackFormat` | `UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="RPG\|UI\|Text")` `FText` | `"轻击 · 第 {0} 段"`（`NSLOCTEXT("RPG","HudLightAttack",...)`） | 轻击招式名，`{0}` = 连段索引 |
| `HeavyAttackText` | 同上 | `"重击 · 蓄力中"` | 蓄力中的招式名 |
| `TransitionAttackText` | 同上 | `"切手技"` | 切手技招式名 |
| `RespawnCountdownFormat` | 同上 | `"{0} 秒后重生"` | 死亡面板倒计时，`{0}` = 剩余秒数 |
| `AttackValueFormat` | 同上 | `"攻 {0}"` | 攻击力文本 |
| `DefenseValueFormat` | 同上 | `"防 {0}"` | 防御力文本 |

#### 其它成员变量

| 名称 | 声明 | 初值 | 说明 |
|---|---|---|---|
| `BoundASC` | `UPROPERTY()` `TWeakObjectPtr<UAbilitySystemComponent>`（private） | 空 | 已绑定的本地玩家 ASC。用 `TWeakObjectPtr` 所以 `IsBound()` 就是它的 `IsValid()` |
| `AttributeDelegateHandles` | `TArray<FDelegateHandle>`（private） | 空 | 8 个属性的委托凭据。头文件注释："解绑时要用 —— `RemoveAll` 会误伤别人的订阅" |
| `BoundAttributes` | `TArray<FGameplayAttribute>`（private） | 空 | 已经绑过的那 8 个属性，`RefreshAllAttributes()` 按它遍历。与 `AttributeDelegateHandles` **按下标一一对应**（`UnbindFromASC` 依赖这一点） |
| `ChargeDisplayTime` | `float`（private） | `0.f` | UI 侧自己累计的蓄力时间。头文件 `⚠️` 注释完整交代了取舍：不从 GA 读 `ChargeElapsed` —— 它是 `URPG_GA_HeavyAttack` 的私有成员，GA 还需要 Cast 才能拿到、实例还会随能力结束被回收；更要紧的是它每 0.1 秒才更新一次（`ChargeTickInterval`），直接拿来画进度条会看出明显的台阶。所以用**标签当节拍器 + UI 自己计时**：`State.Attack.Charging` 出现 → 从这里开始累计；标签消失（松手/被打断）→ 归零；段位（第几段）仍然读权威的 `Lv1/Lv2/Lv3` 标签，不由 UI 猜。结论："误差只可能是一帧，而且只影响'填充条过没过刻度线'，不会出现'UI 说二段、实际打了三段'这种事 —— 段位是标签说了算的。" |
| `bWasCharging` | `bool`（private） | `false` | 上一帧 `State.Attack.Charging` 是否存在，用来做边沿检测（判断"刚刚开始蓄力"） |
| `bWasDead` | `bool`（private） | `false` | 上一帧是否已死，用来在"刚死"那一刻启动重生倒计时 |
| `RespawnCountdown` | `float`（private） | `0.f` | UI 侧的重生倒计时剩余秒数，由 `State.Dead` 出现时的 `RespawnDelay` 初始化 |
| `LastMoveName` | `FText`（private） | 空 | 上一次显示的招式名，用来避免每帧写 `TextBlock`（会触发排版） |

**外部依赖的类型**（本类用到的、不在本文件里的接口）：`ARPG_BaseCharacter::GetAbilitySystemComponent()` / `GetCombatComponent()` / `GetRespawnDelay()`、`URPG_CombatComponent::GetComboIndex()` / `GetAttackModule()`、`URPG_AttackModuleData::GetHeavyLevelCount()` / `GetHeavyLevel()`、`FRPG_HeavyAttackLevel::RequiredChargeTime`（定义在 `RPG_AttackTypes.h:63-72`）、`URPG_AttributeSet::GetXxxAttribute()` 静态访问器与 `GetXxx()` 取值器（由 `ATTRIBUTE_ACCESSORS_BASIC` 宏生成，见 `RPG_AttributeSet.h:123-166`）。

---

### `Source/RPG/UI/RPG_AttributeBarWidget.h` / `Source/RPG/UI/RPG_AttributeBarWidget.cpp`

**一句话职责**：一条属性条 —— 把"当前值 / 最大值"换算成 0~1 的进度比例填进 `ProgressBar`，顺带刷数字文本，并给蓝图一个变化通知点。

**类/结构体**：`URPG_AttributeBarWidget` —— 继承 `UUserWidget`，`UCLASS(Abstract)`。血/蓝/耐力三条共用，由 WBP 子类做皮肤。

> 类注释三段：
> · **它只做三件事**：① 换算比例填进度条；② 把数字文本刷成 "72 / 100"；③ 给蓝图一个回调做"掉血闪一下"这类表现。它**不知道**这个值是血量还是耐力，也不知道值是从哪来的 —— 那由 `URPG_HUDWidget` 去接 ASC 的属性委托。
> · **为什么要有这个类**：三条属性条的"数值 → 进度 + 文本"换算完全一样，只有颜色和图标不同；各写三遍的话，将来要加"数值平滑过渡"就得改三处 —— 而且必然漏一处。抽成基类后，`WBP_HealthBar` / `WBP_ManaBar` / `WBP_StaminaBar` 只是三个**皮肤不同、行为相同**的子类蓝图。
> · **★ `BindWidget` 的坑**：标了 `BindWidget` 的成员会在 Widget 构造时按**变量名**去 WBP 里找同名控件。找不到 → 编译期报错（还好）；名字对但类型错 → 编译期报错；**没标 `BindWidget` → 运行时永远 `nullptr`，不报错**。所以 `Bar` / `ValueText` / `Icon` 这三个名字就是**契约**。

---

#### `void URPG_AttributeBarWidget::SetAttributeValues(float Current, float Max)`
- **干什么**：更新这条属性条（缓存值 + 进度条 + 数字文本 + 通知蓝图）。
- **关键实现**（顺序很关键）：
  1. `const bool bValidMax = Max > KINDA_SMALL_NUMBER;`
  2. `const float PreviousValue = CachedCurrent;` —— **先记旧值再覆盖**。
  3. `CachedCurrent = Current; CachedMax = Max; CachedPercent = bValidMax ? FMath::Clamp(Current / Max, 0.f, 1.f) : 1.f;`（`Max` 非法时按**满**处理，即 `1.f`）
  4. `const bool bIncreased = Current > PreviousValue;`
  5. `if (Bar) Bar->SetPercent(CachedPercent);`
  6. `if (ValueText)` → `FText::Format(ValueFormat, FText::AsNumber(FMath::RoundToInt(Current)), FText::AsNumber(FMath::RoundToInt(Max)))` → `SetText`。
  7. `BP_OnAttributeValuesChanged(Current, Max, bIncreased);` —— **无条件**调用（不带 `if`，且传的是原始 `Current`/`Max` 而不是取整值）。
- **为什么这么写**：
  · **`Max` 可能是 0**：属性集在角色初始化完成之前，`MaxHealth` / `MaxStamina` 都是构造函数里的默认值（100），但**如果 `InitAttributesEffect` 还没应用**，或者某个属性根本没配 Max，就会出现 `Max = 0` 的窗口期。除以 0 得到的 `NaN` 塞进 `ProgressBar`，表现是进度条**完全不显示**或者变成一条细白线 —— "看起来像'UI 没接上'，很容易往错的方向查"。按"满"处理是有意的：**宁可短暂显示满血，也不要显示 NaN**。
  · `⚠️` **先记旧值再覆盖**：顺序反了的话 `bIncreased` 永远是 `false`，蓝图那边的"掉血闪红"就永远不会触发，**而且不报错**。
  · 显式 `Clamp` 的理由：`SetPercent` 只接受 0~1，超范围会被引擎静默 Clamp（注释引 `SProgressBar` 里夹了一下、不打日志），但先夹一次是为了让 `CachedPercent` 这个**对外暴露的读数**和条上真正显示的一致 —— 不然 WBP 里拿它做逻辑会和眼睛看到的不符。
  · 数字取整：属性是 `float`（因为要支持"每秒掉 10 点"这种连续消耗），但玩家不需要看到 `72.35 / 100` 这种精度。
- **被谁调用 / 调用谁**：被 `URPG_HUDWidget::RefreshAttribute()` 在三个分支里调用（血/蓝/耐力）；**头顶血条不走这个函数**（`URPG_OverheadHealthBarWidget::RefreshBarValues()` 直接写自己的 `HealthBar->SetPercent`）。调用 `UProgressBar::SetPercent()`、`UTextBlock::SetText()`、`BP_OnAttributeValuesChanged()`（蓝图事件）。

#### `float URPG_AttributeBarWidget::GetPercent() const`
- **干什么**：`BlueprintPure` 内联返回 `CachedPercent`。
- **关键实现**：`return CachedPercent;`
- **为什么这么写**：注释未说明（上面 `SetAttributeValues` 里解释了为什么要把 `CachedPercent` 存下来并且和真实显示值一致）。
- **被谁调用 / 调用谁**：`UFUNCTION(BlueprintPure)`，本仓库 C++ 内未见调用点（供 WBP 读）。

#### `float URPG_AttributeBarWidget::GetCurrentValue() const`
- **干什么**：`BlueprintPure` 内联返回 `CachedCurrent`。
- **关键实现**：`return CachedCurrent;`
- **为什么这么写**：注释未说明。
- **被谁调用 / 调用谁**：同上，C++ 内未见调用点。

#### `float URPG_AttributeBarWidget::GetMaxValue() const`
- **干什么**：`BlueprintPure` 内联返回 `CachedMax`。
- **关键实现**：`return CachedMax;`
- **为什么这么写**：注释未说明。
- **被谁调用 / 调用谁**：同上，C++ 内未见调用点。

#### `void URPG_AttributeBarWidget::BP_OnAttributeValuesChanged(float Current, float Max, bool bIncreased)`（protected）
- **干什么**：数值变化的**通知点**，`BlueprintImplementableEvent`（`meta = (DisplayName = "On Attribute Values Changed")`），WBP 里实现。
- **关键实现**：C++ 无实现（由 UHT 生成空的事件桩），只在 `SetAttributeValues` 末尾被调用一次。
- **为什么这么写**：头文件注释 —— **在 C++ 里改完控件之后调用**，给 WBP 补做表现的余地。典型用法：掉血时让整条闪一下红、血量低于 30% 时把填充色换成红色、播放"数值滚动"的动画（用 UMG 的动画 + 这里的 `Current` 做插值起点/终点）。做成 `BlueprintImplementableEvent` 而不是让 WBP 去覆写 `SetAttributeValues` 的原因：覆写的话父类的赋值逻辑就有被跳过或重复执行的风险（**UMG 里没有"调 Super"的强制**）；拆成一个纯通知，父类的职责边界才守得住。
- **参数**：`bIncreased` true = 这次是涨（法力恢复、回血）；false = 掉了。
- **被谁调用 / 调用谁**：被 `SetAttributeValues()` 调用；由 WBP 实现。

#### 成员变量与 UPROPERTY

| 名称 | 声明 | 默认值 | 说明 |
|---|---|---|---|
| `Bar` | `UPROPERTY(meta = (BindWidget))` `TObjectPtr<UProgressBar>`（protected） | — | 填充条，**必填**。头文件注释："WBP 里必须建同名控件，名字对不上编译不过"。WBP 侧：拖一个 `Progress Bar` 命名为 `Bar`，填充色由 WBP 自己定 |
| `ValueText` | `UPROPERTY(meta = (BindWidgetOptional))` `TObjectPtr<UTextBlock>`（protected） | — | 数值文本，显示 "72 / 100"。可选：WBP 里没有这个控件也能通过编译，指针为 `nullptr`（注释里把 `BindWidgetOptional` 误写成了 "OptionalBindWidget"，以代码为准） |
| `Icon` | `UPROPERTY(meta = (BindWidgetOptional))` `TObjectPtr<UImage>`（protected） | — | 图标，可选 —— 三条属性条的图标不同，由 WBP 自己指定图片 |
| `ValueFormat` | `UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="RPG\|UI")` `FText`（protected） | `"{0} / {1}"`（`NSLOCTEXT("RPG","AttributeValueFormat",...)`） | 数值文本格式，`{0}` = 当前值，`{1}` = 最大值。WBP 侧可在 Class Defaults 改，不用重编 C++ |
| `CachedCurrent` | `float`（private，**非 UPROPERTY**） | `0.f` | 缓存当前值，供 `GetCurrentValue()` 与 `bIncreased` 判定 |
| `CachedMax` | `float`（private，**非 UPROPERTY**） | `0.f` | 缓存最大值，供 `GetMaxValue()` |
| `CachedPercent` | `float`（private，**非 UPROPERTY**） | `0.f` | 缓存比例，供 `GetPercent()`；`SetAttributeValues` 保证它与进度条实际显示值一致 |

**WBP 侧要做的事**：建 `WBP_AttributeBar`（父类选 `URPG_AttributeBarWidget`），里面放名为 `Bar` 的 `Progress Bar`（必填）、可选的 `ValueText`（`Text`）与 `Icon`（`Image`）；再派生 `WBP_HealthBar` / `WBP_ManaBar` / `WBP_StaminaBar`，各自在 Class Defaults 的 `Icon` 里换图、在 `Bar` 的 `Fill Color and Opacity` 里换色。也可以只做一个 WBP 在 `WBP_RPG_HUD` 里放三个实例分别设值（`PHASE7_UI_SETUP.md` §2 给了两种做法）。

---

### `Source/RPG/UI/RPG_OverheadHealthBarComponent.h` / `Source/RPG/UI/RPG_OverheadHealthBarComponent.cpp`

**一句话职责**：头顶血条的 `UWidgetComponent`，比引擎自带的多做**一件**事 —— 在 `InitWidget()` 里把 owner 显式传给控件。

**类/结构体**：`URPG_OverheadHealthBarComponent` —— 继承 `UWidgetComponent`，`UCLASS(ClassGroup = (RPG), meta = (BlueprintSpawnableComponent))`。它是 `ARPG_BaseCharacter` 的默认子对象（`RPG_BaseCharacter.cpp:80`：`CreateDefaultSubobject<URPG_OverheadHealthBarComponent>(TEXT("OverheadHealthBar"))`，暴露为 `UPROPERTY(VisibleAnywhere, BlueprintReadOnly)`）。

> 类注释两段（`★`）：
> **【为什么必须自己写一个组件】** 血条 Widget 必须知道"我是谁的血条"，直觉上应该能从组件或 outer 链问出来，但两条路都是断的：
> · ① `UUserWidget::GetOwningPlayerPawn()` 拿到的是**本地玩家**，不是血条主人。因为 `UWidgetComponent::InitWidget()` 用的是 `CreateWidget(World, WidgetClass)`（引 `WidgetComponent.cpp:1761`），那个重载最终走 `CreateWidgetInstance(UGameInstance&)`，里面用 `GameInstance.GetFirstGamePlayer()` 设 PlayerContext（引 `UserWidget.cpp:2759-2762`、`:2813`）。→ 所有敌人的血条都会显示**玩家自己的**血量和名字，**而且完全不报错**。
> · ② `GetTypedOuter<UWidgetComponent>()` 也拿不到 —— Widget 的 outer 是 **GameInstance**，不是这个组件（同上一处 `:2813`）。
> 所以只能显式传，而且必须在"Widget 刚被造出来"的那一刻做 —— `InitWidget()` 就是那个时刻，而且它是 `virtual`。
>
> **【何时被调用】** `UWidgetComponent::BeginPlay()` → `InitWidget()`（引 `WidgetComponent.cpp:751`）。组件的 `BeginPlay` 由 `AActor::BeginPlay` 派发，**早于**角色的 `ReceiveBeginPlay`，所以角色自己的 `BeginPlay` 里拿到 `GetUserWidgetObject()` 时它一定已经就绪。不过本类不依赖这个顺序 —— 只要 Widget 一被创建就立刻传，后面谁什么时候读都行。

---

#### `URPG_OverheadHealthBarComponent::URPG_OverheadHealthBarComponent()`
- **干什么**：配置组件为 Screen 空间、固定像素尺寸、关闭离屏 tick、构造期先隐藏。
- **关键实现**（四行，各有注释）：
  1. `SetWidgetSpace(EWidgetSpace::Screen);`
  2. `SetDrawSize(FVector2D(140.f, 20.f));`
  3. `SetTickWhenOffscreen(false);`
  4. `SetVisibility(false);`
- **为什么这么写**（逐条转述）：
  · Screen 空间："始终正对相机、大小不随距离变 —— 血条/名牌想要的就是这个。World 空间会让血条跟着透视缩放，离远了糊成一团。"
  · `SetDrawSize`："绘制尺寸（像素）。Screen 空间下这就是它在屏幕上**恒定**的大小。"
  · `SetTickWhenOffscreen(false)`："每帧更新会重建 Slate 布局，白白吃 CPU。血条数值变化时自己会重绘，不需要这个开关。"
  · `SetVisibility(false)`："默认隐藏，由 `ARPG_BaseCharacter::RefreshOverheadWidgetVisibility()` 在运行时决定。构造期先关掉是为了避免'生成的那一帧先闪一下血条'。"
- **被谁调用 / 调用谁**：`CreateDefaultSubobject` 时；调用 `UWidgetComponent::SetWidgetSpace` / `SetDrawSize` / `SetTickWhenOffscreen` / `SetVisibility`。

#### `virtual void URPG_OverheadHealthBarComponent::InitWidget()`（protected）
- **干什么**：`Super::InitWidget()` 之后，把 `GetOwner()` 作为"血条主人"塞给控件。
- **关键实现**：
  1. `Super::InitWidget();`
  2. `URPG_OverheadHealthBarWidget* OverheadWidget = Cast<URPG_OverheadHealthBarWidget>(GetUserWidgetObject());`
  3. `if (!OverheadWidget)` → `UE_LOG(LogTemp, Warning, ...)` 并 `return`。日志文案："[%s] 头顶血条没有设置 Widget Class（或类型不是 RPG_OverheadHealthBarWidget）—— 它不会显示任何东西。请在角色蓝图的 OverheadHealthBar 组件上指定 WBP_OverheadHealthBar"。
  4. `OverheadWidget->SetOwningActor(GetOwner());`
- **为什么这么写**：
  · 打日志的理由："Widget Class 没配是个静默失败：组件在那儿、可见性也对，就是什么都不显示。这类问题靠肉眼排查会绕很远，所以在生成时就报出来。" 并说明 `⚠️`：打 Verbose 之外的级别会有点吵（每个敌人一条），但**只在配错时才会走到这里**，配好了就永远不会触发。
  · `★` 显式传 owner：注释原话"把'我是谁的血条'显式告诉 Widget。为什么不能让它自己去问 `GetOwningPlayerPawn()`，见头文件里的说明 —— 那条路会拿到**本地玩家**，于是所有敌人的血条都显示玩家自己的血量。"
  · **注意日志类别**：这里用的是 `LogTemp` 而不是本项目统一的 `LogRPG_Combat`（本 `.cpp` 只 include 了 `UI/RPG_OverheadHealthBarWidget.h`，没 include `Core/RPG_LogChannels.h`）。这是代码里的既成事实，如实记录。
- **被谁调用 / 调用谁**：被 `UWidgetComponent::BeginPlay()` 调用（`WidgetComponent.cpp:751`）；调用 `URPG_OverheadHealthBarWidget::SetOwningActor()`。

**成员变量**：本类**没有**自己的成员变量，全部行为在构造函数和 `InitWidget()` 里。

**WBP/蓝图侧要做的事**（`PHASE7_UI_SETUP.md` §2 / §3.3）：在角色蓝图（`BP_RPG_Player` / `BP_RPG_Enemy`）的 `OverheadHealthBar` 组件上，把 `Widget Class` 设为 `WBP_OverheadHealthBar`。`Screen` 空间与 140×20 的绘制尺寸由 C++ 构造函数给出，蓝图里可以覆盖。

---

### `Source/RPG/UI/RPG_OverheadHealthBarWidget.h` / `Source/RPG/UI/RPG_OverheadHealthBarWidget.cpp`

**一句话职责**：挂在其他玩家和 AI 头上的血条 —— 常规收起，检测到"血量下降"时亮出来 5 秒。

**类/结构体**：`URPG_OverheadHealthBarWidget` —— 继承 `UUserWidget`，`UCLASS(Abstract)`。每个角色一个实例，**每个实例自己订阅自己主人的 ASC**。

> 类注释三段：
> **【本地玩家自己不显示这个】** 显示与否由 `ARPG_BaseCharacter::RefreshOverheadWidgetVisibility()` 决定，判据是 `IsLocallyControlledPlayer()`（**不是** `IsLocallyControlled()` —— 那个在敌人身上恒为 true，理由见那个函数的注释，其中引了 `Controller.cpp:90-113`）。那件事放在角色上而不是这里，是因为"谁在控制我"是 Pawn 的状态，而且要在 Controller 复制到达的**第一时间**就纠正 —— 放在 Widget 里会晚一帧（Widget 是在组件可见之后才被创建的），表现是"复活/进场时自己的血条闪一下"。
> **【它怎么拿到数据】** 头顶血条是**每个角色一个**的，所以每条血条订阅自己那个角色的 ASC 就够了，不需要中心化的 HUD 转发。这样：敌人多了也不会在 HUD 里排队；敌人销毁时 Widget 跟着销毁，订阅自然断开。
> **【联机下它是怎么工作的】** 敌人的 ASC 在客户端走的是 Minimal 那一路（项目用的是 Mixed：自己控制的角色 Full、其他角色 Minimal），但**属性值本身照样复制** —— `SpawnedAttributes` 是 `COND_None`，且由 `ReplicateSubobjects` 无条件复制，跟 GE 的复制模式无关。`RPG_AttributeSet` 里 `Health` 也是 `COND_None`，注释写得很清楚："血条、队友状态、敌人血量都需要被别人看到"。所以客户端的血条读的是复制过来的值 —— **不需要服务器推任何额外的东西**。

---

#### `void URPG_OverheadHealthBarWidget::SetOwningActor(AActor* InOwner)`
- **干什么**：告诉这个血条"你是谁的血条"，并立刻按新主人刷一次 + 试图订阅。
- **关键实现**（顺序，四个动作都有理由）：
  1. `OwningActor = InOwner;`
  2. `SetVisibility(ESlateVisibility::Collapsed);`
  3. `LastSeenHealth = -1.f;`
  4. `RefreshFromOwner();`
  5. `TryBindToOwnerASC();`
- **为什么这么写**：
  · 步骤 2 放在这里**而不是只放 `NativeConstruct`**：两个调用时机不保证先后 —— `UWidgetComponent::InitWidget()` 造出 Widget 后立刻就会调本函数，而 `NativeConstruct` 要等控件真正上屏才跑。只在一处收，另一处之前的那一两帧就会露出来。
  · 步骤 3：换主人了 → 之前记的血量作废，"否则新主人的第一帧会被误判成'掉血了'"。
  · 步骤 4："让血条在**出生那一帧**就是对的"。
  · 步骤 5：注释说"这一次多半是失败的 —— 组件 BeginPlay 早于角色的 GAS 初始化，此刻 ASC 还是空的"，失败会自动挂上重试。
- **被谁调用 / 调用谁**：被 `URPG_OverheadHealthBarComponent::InitWidget()` 调用（这是**唯一**调用点，本仓库 C++ 内）；调用 `RefreshFromOwner()`、`TryBindToOwnerASC()`。

#### `void URPG_OverheadHealthBarWidget::RefreshFromOwner()`
- **干什么**：立刻按当前属性值刷一次血条，并刷新名字文本。
- **关键实现**：`RefreshBarValues();` 然后 `if (NameText)` → `if (const AActor* Owner = OwningActor.Get()) NameText->SetText(FText::FromString(Owner->GetName()));`
- **为什么这么写**：头文件注释"立刻按当前属性值刷一次（构建完就能显示正确的血量，不用等下一次变化）"。`⚠️` 用 `OwningActor` 而不是 `GetOwningPlayerPawn()` —— 后者返回的是**本地玩家**，会让每个敌人的血条都顶着玩家的名字（原因见组件头文件）。
- **被谁调用 / 调用谁**：被 `SetOwningActor()`、`NativeConstruct()`、`TryBindToOwnerASC()`（绑定成功后最后一次）调用；`UFUNCTION(BlueprintCallable)` 也开放给蓝图；调用 `RefreshBarValues()`、`UTextBlock::SetText()`。

#### `virtual void URPG_OverheadHealthBarWidget::NativeConstruct()`
- **干什么**：收起血条、清血量基线、刷一次、试着订阅。
- **关键实现**：`Super::NativeConstruct();` → `SetVisibility(Collapsed)` → `LastSeenHealth = -1.f;` → `RefreshFromOwner();` → `TryBindToOwnerASC();`
- **为什么这么写**：`★` 注释说明 `NativeConstruct` **是会跑第二遍的**，不是"每个 Widget 只跑一次"：组件不可见时 `UWidgetComponent::UpdateWidget()` 会 `RemoveWidgetFromScreen()` → Slate 对象析构 → `NativeDestruct()`；恢复可见时重新 `TakeWidget()` → 控件被**重建** → 再跑一次 `NativePreConstruct` + `NativeConstruct`（注释指明是 `Widget.cpp` 的 `OnWidgetRebuild` 路径）。所以必须把血量基线清掉：不清的话，组件隐藏期间血量掉了，重建时 `RefreshBarValues` 拿旧基线和当前血量一比 → 判成"掉血了" → **血条凭空亮一下**。注释还坦白："`SetOwningActor` 里对同一类误判是显式防了的，这里原本漏了，两处语义不一致。"
  · 主动刷一次的理由：属性委托只在**变化时**触发，而血条出现的那一刻（比如敌人刚进入视野）往往没有变化发生 —— 不主动读一次的话，"满血的敌人挨第一刀时亮出来的会是空条"。
- **被谁调用 / 调用谁**：UMG 生命周期（可能多次）；调用 `RefreshFromOwner()`、`TryBindToOwnerASC()`。

#### `virtual void URPG_OverheadHealthBarWidget::NativeDestruct()`
- **干什么**：清掉两个定时器 + 解绑两个属性委托 + 重置全部状态。
- **关键实现**：
  1. `if (UWorld* World = GetWorld())` → `ClearTimer(HideTimerHandle);` `ClearTimer(BindRetryHandle);`
  2. `if (UAbilitySystemComponent* ASC = BoundASC.Get())`：`if (HealthChangedHandle.IsValid())` → `ASC->GetGameplayAttributeValueChangeDelegate(URPG_AttributeSet::GetHealthAttribute()).Remove(HealthChangedHandle);`；`MaxHealthChangedHandle` 同理（用 `GetMaxHealthAttribute()`）。
  3. `HealthChangedHandle.Reset(); MaxHealthChangedHandle.Reset(); BoundASC.Reset();`
  4. `Super::NativeDestruct();`
- **为什么这么写**：
  · 定时器必须先清：不清的话，控件销毁后那个 5 秒的回调还会到点触发一次 —— "`UObject` 委托对已销毁的对象会安全跳过（不会崩），但那是靠引擎兜底；而且如果 World 还在（换关卡时），它会一直挂在 `TimerManager` 里。" 重试定时器更严重：它是个**循环**定时器，控件已经没了会白跑一辈子。
  · `★` **解绑必须成对**：不解的话，角色销毁后 ASC 上的委托还指着这个 Widget —— 引擎会安全跳过已 GC 的对象，但 handle 会一直堆积；更要紧的是"Widget 被复用（List View 之类）后重复 `AddUObject`"，那会让同一个回调被调**两次**。
- **被谁调用 / 调用谁**：UMG 生命周期（配合上面说的重建路径，可能多次触发）；调用 `FTimerManager::ClearTimer()`、`GetGameplayAttributeValueChangeDelegate().Remove()`。

#### `bool URPG_OverheadHealthBarWidget::CanBeRevealed() const`（protected）
- **干什么**：收回去之后是否还可以再被亮出来 —— 角色死了就不再亮。
- **关键实现**：`const URPG_AttributeSet* AttributeSet = ResolveOwnerAttributeSet(); return AttributeSet && AttributeSet->GetHealth() > 0.f;`
- **为什么这么写**：注释原话 —— 少了这一条，尸体会在"血量归零"时亮一次然后挂满 5 秒，"对着一具躺地的尸体显示一根空血条，既没意义又难看"。用血量判而不是查 `State.Dead`：血条本来就只关心血量，多引一个标签依赖不值得；而且"血量归零"和"死亡"在本项目里是同一件事（`GA_Death` 就是被血量归零触发的）。
- **被谁调用 / 调用谁**：被 `RevealForDamage()` 调用；调用 `ResolveOwnerAttributeSet()`。

#### `void URPG_OverheadHealthBarWidget::RevealForDamage()`（protected）
- **干什么**：把血条亮出来并重新开始 5 秒计时。
- **关键实现**：
  1. `if (!CanBeRevealed())` → `UE_LOG(LogRPG_Combat, Verbose, "[%s] 头顶血条该亮但被拦下（血量已 ≤ 0 或属性集读不到）", ...)` → `return;`
  2. `const bool bWasRevealed = (GetVisibility() == ESlateVisibility::HitTestInvisible);`
  3. `SetVisibility(ESlateVisibility::HitTestInvisible);`
  4. `if (!bWasRevealed) BP_OnRevealChanged(true);`
  5. `if (RevealDuration <= 0.f) return;`
  6. `if (UWorld* World = GetWorld())` → `World->GetTimerManager().SetTimer(HideTimerHandle, this, &URPG_OverheadHealthBarWidget::HideAfterDamage, RevealDuration, /*bLoop=*/false);`
- **为什么这么写**：
  · 步骤 1 的日志："这里是个**静默**分支，专门打一条日志。不然'该亮没亮'和'压根没走到这儿'在日志上分不开。"级别是 `Verbose`。
  · 步骤 2/4 的**边沿检测**：已经亮着就只重置计时、**不重发通知**。少了这道检测，5 秒窗口内每挨一下 `BP_OnRevealChanged(true)` 都会重播一次，WBP 那边的淡入动画被反复重启 —— 观感是"闪"。注释还自陈：本文件末尾对 `BP_OnLowHealth` 是**特意**做了边沿检测的（原话"每帧无脑调的话，WBP 里的闪烁动画会被反复重启 —— 看起来像在抽搐"），"这里原本漏了同一条纪律"。
  · `HitTestInvisible` 而不是 `Visible`：血条是纯展示，不该吃掉鼠标事件（"将来加'点击选中敌人'时不希望被它挡住"）。
  · **用 `TimerManager` 而不是自己 tick 计时**，三条理由：① 控件被隐藏（`Collapsed`）之后，UMG 的 tick 不保证还会跑 —— 用 tick 计时的话，收回的那一刻计时就死了，再也不会亮第二次；② 一次挨打只需要一个 5 秒后的回调，为此每帧跑一次是纯浪费；③ 定时器跟着 World 走，关卡切换/暂停时的行为是引擎定好的。
  · **同一个 handle 再 `SetTimer` 会替换掉上一个** —— "这正是我们要的'连续挨打刷新计时，而不是叠加成 10 秒'"。
  · `⚠️` 步骤 5 的必要性：时长为 0（或负）时**必须**提前返回，否则血条会亮起来再也不收回 —— `meta = (ClampMin = "0.1")` 只约束编辑器输入框；运行时被设成 0 的话，`FTimerManager::InternalSetTimer` 在 `InRate > 0.f` 为假时**只清掉旧定时器、不建新的**（注释引 `TimerManager.cpp`），于是 `HideAfterDamage` 永不触发。
- **被谁调用 / 调用谁**：被 `RefreshBarValues()` 在检测到掉血时调用；调用 `CanBeRevealed()`、`BP_OnRevealChanged()`、`FTimerManager::SetTimer()`。

#### `void URPG_OverheadHealthBarWidget::HideAfterDamage()`（protected）
- **干什么**：计时到点，把血条收回去。
- **关键实现**：`SetVisibility(ESlateVisibility::Collapsed);` → `BP_OnRevealChanged(false);`（**无条件**发通知，没有边沿检测 —— 因为定时器只会在"亮着"的时候存在）。
- **为什么这么写**：函数上方的注释只有一句："角色在亮着的这 5 秒里死了 → 直接收掉，不用等下次判定"。
- **被谁调用 / 调用谁**：被 `HideTimerHandle` 的定时器回调调用；调用 `BP_OnRevealChanged()`。

#### `UAbilitySystemComponent* URPG_OverheadHealthBarWidget::ResolveOwnerASC() const`（protected）
- **干什么**：拿到"我的主人"的 ASC；取不到返回 `nullptr`（这是常态而非异常）。
- **关键实现**：
  1. `const AActor* Owner = OwningActor.Get();` 为空 → `return nullptr;`
  2. `if (const ARPG_BaseCharacter* Character = Cast<ARPG_BaseCharacter>(Owner)) return Character->GetAbilitySystemComponent();`
  3. 否则 `return nullptr;`（**非 `ARPG_BaseCharacter` 的主人不支持**）
- **为什么这么写**：`⚠️` 注释 —— **不要**用 `GetOwningPlayerPawn()`。`UWidgetComponent` 造 Widget 走的是 `CreateWidget(World, WidgetClass)`，那个重载把 Widget 的 `PlayerContext` 设成了**第一个本地玩家**（引 `UserWidget.cpp` 里的 `CreateWidgetInstance(UGameInstance&)` → `GetFirstGamePlayer`），Widget 本身还被 outer 到 `GameInstance` 上。后果是每条敌人的血条都以为自己是玩家的血条 —— 显示玩家的血量、玩家的名字，**而且完全不报错**。所以主人只能由 `URPG_OverheadHealthBarComponent` 显式传进来。走角色基类接口的理由：它实现了 `IAbilitySystemInterface`，会把"玩家去 `PlayerState` 找、敌人从自己身上拿"这个差异抹平。
- **被谁调用 / 调用谁**：被 `ResolveOwnerAttributeSet()`、`TryBindToOwnerASC()` 调用；调用 `ARPG_BaseCharacter::GetAbilitySystemComponent()`。

#### `const URPG_AttributeSet* URPG_OverheadHealthBarWidget::ResolveOwnerAttributeSet() const`（protected）
- **干什么**：取当前主人的属性集；ASC 还没就位时返回 `nullptr`。
- **关键实现**：`UAbilitySystemComponent* ASC = BoundASC.IsValid() ? BoundASC.Get() : ResolveOwnerASC();` → `return ASC ? ASC->GetSet<URPG_AttributeSet>() : nullptr;`
- **为什么这么写**：注释交代了一次真实的重构 —— **统一从"主人现在是谁"这条路径去取，不看订阅状态**。之前这里的分叉是：`CanBeRevealed()` 只认 `BoundASC`，而 `RefreshBarValues()` 会在 `BoundASC` 为空时退回 `ResolveOwnerASC()`。两个判定于是可能不一致 —— 在"属性集拿得到、但还没订阅上"的窗口里，`RefreshBarValues` 判出"掉血了"，`CanBeRevealed` 却因为 `BoundASC` 为空而拒绝亮起。"那种窗口很短，但正是敌人刚出生就被打的时刻。统一走这一条路，两边就不可能打架。"
- **被谁调用 / 调用谁**：被 `CanBeRevealed()`、`RefreshBarValues()` 调用；调用 `ResolveOwnerASC()`、`UAbilitySystemComponent::GetSet<>()`。

#### `void URPG_OverheadHealthBarWidget::TryBindToOwnerASC()`（protected）
- **干什么**：订阅主人的 `Health` + `MaxHealth` 属性变化委托；失败就挂一个**循环**定时器每 0.2 秒重试，直到成功。
- **关键实现**（三个情形，注释明确说顺序不能换）：
  0. `UWorld* World = GetWorld();`、`UAbilitySystemComponent* ASC = ResolveOwnerASC();`
  1. **情形 1 · 还没就位**：`const URPG_AttributeSet* ReadySet = ASC ? ASC->GetSet<URPG_AttributeSet>() : nullptr;` `if (!ASC || !ReadySet)` →
     · `++BindAttemptCount;`
     · `if (ASC && BindAttemptCount == AttributeSetWarnAfterAttempts)` → `UE_LOG(LogRPG_Combat, Warning, ...)`（文案会打出 owner、ASC 名、ASC 的 owner、重试次数，并提示检查 `AddSpawnedAttribute` 和 `SpawnedAttributes` 的复制）。注意判定是 `ASC &&` 且 `==` 而不是 `>=`（只报一次）。
     · `if (World && !World->GetTimerManager().IsTimerActive(BindRetryHandle))` → `SetTimer(BindRetryHandle, this, &URPG_OverheadHealthBarWidget::TryBindToOwnerASC, BindRetryInterval, /*bLoop=*/true);`
     · `return;`
  2. **情形 2 · 主人没变**：`if (ASC == BoundASC.Get())` → `if (World) ClearTimer(BindRetryHandle);` → `return;`
  3. **情形 3 · 主人换了（或第一次拿到）**：`if (UAbilitySystemComponent* OldASC = BoundASC.Get())` → 用 `IsValid()` 逐个 handle 判定并 `Remove`（`Health` / `MaxHealth`）→ `Reset()` 两个 handle；然后 `BoundASC.Reset(); BoundASC = ASC;`
  4. 绑定：`HealthChangedHandle = ASC->GetGameplayAttributeValueChangeDelegate(URPG_AttributeSet::GetHealthAttribute()).AddUObject(this, &URPG_OverheadHealthBarWidget::HandleHealthChanged);`；`MaxHealthChangedHandle = ...GetMaxHealthAttribute()...`（**同一个回调**）。
  5. `if (World) ClearTimer(BindRetryHandle);`、`BindAttemptCount = 0;`
  6. `UE_LOG(LogRPG_Combat, Log, "[%s] 头顶血条已接上（ASC 在 %s 上），当前血量 %.1f", ..., ReadySet->GetHealth());`
  7. `RefreshFromOwner();`
- **为什么这么写**（这是全章注释最密的一处）：
  · **为什么情形 1 必须排在情形 2 前面**（`⚠️⚠️`）：之前是反的 —— 用一个 `ASC == BoundASC.Get()` 同时表达"已经订上了"和"还没拿到"，而这两件事在**初始状态**下值是一样的（两边都是 `null`），判断成立，于是第一次调用就认定"已接上"并 `return`，**重试定时器压根没被挂起来过，重试逻辑是死代码**。后果："能不能订上"完全取决于 Widget 被构造的那一刻 ASC 在不在 —— 敌人（ASC 在自己身上，本地一构造就有）订得上；玩家（ASC 在 `PlayerState` 上，要等复制到达）**订不上**。于是表现为"客户端看 AI 有血条、看玩家没有"，而两次的日志长得一模一样。教训原话：**"用一个条件同时表达两种状态时，先问'它们在初始值上会不会撞车'。`null == null` 这种撞车在指针判等里是最常见的一种。"**
  · **就位判定要 ASC 和属性集都有**：只判 ASC 不够 —— 敌人的 ASC 是 `CreateDefaultSubobject` 建的，组件 `BeginPlay` 那一刻就已经存在；但它的**属性集**是在 `ARPG_Enemy::InitializeAbilitySystem()` 里才 `AddSpawnedAttribute` 的，而那要等角色的 `BeginPlay` —— **在组件 `BeginPlay` 之后**。所以"ASC 在、属性集还没登记"是**每个敌人必经的中间态**，不是故障。注释还记了上一版的错误做法（直接判"订上了但没属性集"并打 Warning），结果是每刷一个敌人就报一次"属性集没复制过来"，**内容还是错的，非常误导**。
  · **`AttributeSetWarnAfterAttempts` 为什么用"重试次数"而不是"属性集为空"判定故障**（头文件注释）：见上一条，用"为空"会把必经中间态误报成故障。`25 × 0.2s ≈ 5 秒` —— "真故障 5 秒内一定报；正常的初始化几十毫秒就过去了"。
  · **为什么重试走 `TimerManager` 而**不是** `NativeTick`**（头文件注释）：这个控件常规状态下是 `Collapsed` 的，隐藏的 Widget 还 tick 不 tick 属于 Slate 的实现细节（"当前行为是会 tick，但没有任何东西保证它"）；把"能不能绑上"押在那上面，等于埋一个"血条永远不亮、且不报错"的雷。定时器由 `TimerManager` 驱动，跟控件可不可见完全无关。
  · 情形 2 的意义：重复 `AddUObject` 会让同一个回调被调两次，表现是血条"莫名跳两下"。
  · 情形 4 里注释强调"走到这里 ASC 一定非空 —— 空的那条已经在开头返回了"，并点明这正是那个 `if (!ASC || !ReadySet)` 必须排在最前面的原因："放在这里的话，初始状态会被上面的判等提前吃掉，永远走不到。"
  · 监听 `MaxHealth` 的理由：吃了个加生命上限的 Buff，**当前血量一点没变**，但血条该变短 —— 因为"满血"的定义变了；只监听 `Health` 的话表现是"上限涨了但条没动，要挨一刀才对得齐"（并注明 `URPG_HUDWidget` 里对玩家那三条血条做了同样的事）。
  · `AddUObject` 对应 `Remove(Handle)`：头文件注释说明"没有叫 `RemoveUObject` 的 API，只有 `Remove(FDelegateHandle)` 和 `RemoveAll(对象)`；后者会误伤同一个属性上别人的订阅。"
- **被谁调用 / 调用谁**：被 `SetOwningActor()`、`NativeConstruct()`、绑定失败时自身的循环定时器调用；调用 `ResolveOwnerASC()`、`UAbilitySystemComponent::GetSet<>()` / `GetGameplayAttributeValueChangeDelegate()`、`FTimerManager::SetTimer()` / `ClearTimer()` / `IsTimerActive()`、`RefreshFromOwner()`。

#### `void URPG_OverheadHealthBarWidget::HandleHealthChanged(const FOnAttributeChangeData& Data)`（protected）
- **干什么**：`Health` / `MaxHealth` 共用的属性变化回调。
- **关键实现**：一行 `RefreshBarValues();` —— **完全不用 `Data`**。
- **为什么这么写**：注释原话 —— 委托本身只带了"哪一个属性、新旧值是多少"，够不够用取决于业务；血条的比例是 `Health / MaxHealth` **两个属性一起**决定的，而委托一次只送一个 —— 所以统一去属性集读当前值，不看 `Data`。（也因此 `Health` 和 `MaxHealth` 可以共用这一个回调。）
- **被谁调用 / 调用谁**：被 ASC 的属性变化委托调用（**非动态委托，所以不是 `UFUNCTION`**）；调用 `RefreshBarValues()`。

#### `void URPG_OverheadHealthBarWidget::RefreshBarValues()`（protected）
- **干什么**：按属性集当前值重画血条 + 残血边沿检测 + **掉血检测**（"挨打才亮"的触发点）。
- **关键实现**：
  1. `const URPG_AttributeSet* AttributeSet = ResolveOwnerAttributeSet();` 为空 → `return`。
  2. `const float Health = AttributeSet->GetHealth(); const float MaxHealth = AttributeSet->GetMaxHealth();`
  3. **掉血检测**：`const bool bWasInitialized = LastSeenHealth >= 0.f;` `const bool bLostHealth = bWasInitialized && (Health < LastSeenHealth - KINDA_SMALL_NUMBER);`
  4. `if (bLostHealth)` → `UE_LOG(LogRPG_Combat, Verbose, "[%s] 头顶血条：检测到掉血 %.1f → %.1f", ...)`。
  5. `LastSeenHealth = Health;`（**无条件更新**）
  6. `if (bLostHealth) RevealForDamage();`
  7. `const float Percent = MaxHealth > KINDA_SMALL_NUMBER ? FMath::Clamp(Health / MaxHealth, 0.f, 1.f) : 0.f;`（注意与 `URPG_AttributeBarWidget` 不同：这里 `Max` 非法时取 **0**，那边取 **1**）
  8. `if (HealthBar) HealthBar->SetPercent(Percent);`
  9. **残血边沿检测**：`const bool bIsLow = Percent > 0.f && Percent <= LowHealthThreshold;` `if (bIsLow != bWasLowHealth) { bWasLowHealth = bIsLow; BP_OnLowHealth(bIsLow); }`
- **为什么这么写**：
  · `⚠️` 函数开头第一条注释：**这里不写 `BoundASC`**。订阅状态只有 `TryBindToOwnerASC` 一处负责 —— 如果这里顺手把 `BoundASC` 也设了，那边的 `ASC != BoundASC.Get()` 判定就会认为"已经接上了"，于是**永远不绑委托**：血条初始值是对的，但之后再也不更新。注释说那种 bug "看起来像'血条卡住不动'，很难往订阅逻辑上想"。
  · 用**自己缓存的上一次血量**做比较，而不是用 `FOnAttributeChangeData` 里的 `OldValue`：① `OldValue` 在"客户端收到复制"这条路径上**不一定被填**；② 这个函数还有第二个入口（刚接上 ASC 时主动读一次），那时候压根没有 `Data` 可看。自己存一份，"从哪进来的"就都不影响了。
  · 用 `KINDA_SMALL_NUMBER` 做容差而不是直接 `<`：浮点数的相等比较在本项目里已经踩过坑（属性钳制的那些），而且"血量小幅震动不应该触发亮起"。
  · Verbose 日志记下**变化前后**两个值：注释说"只看当前值判断不出是'没掉血'还是'没收到'"（排查时开 `Log LogRPG_Combat Verbose`）。
  · `MaxHealth = 0` 的窗口期必须挡掉，理由同 `URPG_AttributeBarWidget`（除零 NaN 塞进 `ProgressBar` 会让血条整个不显示，看起来像"UI 没接上"）。
  · `bIsLow` 里带 `Percent > 0.f`：血量归零走的是"死亡"而不是"残血"，所以 0 不算 low。
  · 边沿检测：**只在"跨过阈值"的那一次**通知蓝图；"每帧无脑调的话，WBP 里的闪烁动画会被反复重启 —— 看起来像在抽搐。"
- **被谁调用 / 调用谁**：被 `HandleHealthChanged()`、`RefreshFromOwner()` 调用；调用 `ResolveOwnerAttributeSet()`、`RevealForDamage()`、`UProgressBar::SetPercent()`、`BP_OnLowHealth()`。

#### 成员变量与 UPROPERTY

**绑定控件**

| 名称 | 声明 | 说明 |
|---|---|---|
| `HealthBar` | `UPROPERTY(meta = (BindWidget))` `TObjectPtr<UProgressBar>`（protected） | 血条填充，**必填**。WBP 侧：`WBP_OverheadHealthBar` 里拖一个 `Progress Bar` 命名为 `HealthBar` |
| `NameText` | `UPROPERTY(meta = (BindWidgetOptional))` `TObjectPtr<UTextBlock>`（protected） | 名字文本，**可选** —— "敌人可以不显示名字"。填的是 `Owner->GetName()`（`AActor` 的名字，不是 `PlayerState` 的玩家名） |

**可配置参数**

| 名称 | 声明 | 默认值 | 说明 |
|---|---|---|---|
| `RevealDuration` | `UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="RPG\|UI", meta=(ClampMin="0.1"))` `float`（protected） | `5.f` | 挨打后亮多久（秒）。WBP 侧可在 Class Defaults 改。注意 `ClampMin` 只约束编辑器，运行时为 0 会让血条永不收回（见 `RevealForDamage`） |
| `LowHealthThreshold` | `UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="RPG\|UI", meta=(ClampMin="0.0", ClampMax="1.0"))` `float` | `0.3f` | 低血量阈值，低于/等于它会调 `BP_OnLowHealth(true)` 做"残血闪烁" |

**蓝图事件**

| 名称 | 声明 | 说明 |
|---|---|---|
| `BP_OnRevealChanged(bool bRevealed)` | `UFUNCTION(BlueprintImplementableEvent, Category="RPG\|UI", meta=(DisplayName="On Reveal Changed"))`（protected） | 亮起/收回的通知点，给 WBP 做淡入淡出用。`true` 在 `RevealForDamage` 里**只在从未亮到亮的那一次**发；`false` 在 `HideAfterDamage` 里无条件发 |
| `BP_OnLowHealth(bool bIsLow)` | `UFUNCTION(BlueprintImplementableEvent, Category="RPG\|UI", meta=(DisplayName="On Low Health Changed"))`（protected） | 残血状态变化时通知蓝图（进入/退出各一次，不是每帧调） |

**状态与定时器**

| 名称 | 声明 | 初值 | 说明 |
|---|---|---|---|
| `LastSeenHealth` | `float`（protected，**非 UPROPERTY**） | `-1.f` | 上一次观察到的血量；`-1` 表示"还没基线"。用来判断"这次是不是掉血了"。在 `SetOwningActor()` 与 `NativeConstruct()` 里都被重置为 `-1.f` |
| `HideTimerHandle` | `FTimerHandle`（protected） | 空 | 收回血条的定时器。头文件注释："走 `TimerManager` 而不是每帧 tick —— 见 .cpp 里的说明" |
| `BindRetryHandle` | `FTimerHandle`（protected） | 空 | 订阅 ASC 失败时的**循环**重试定时器；订阅成功即清除 |
| `BindRetryInterval` | `static constexpr float`（protected） | `0.2f` | 订阅失败的轮询间隔（秒）—— "只是启动时兜底，不成功就一直试" |
| `AttributeSetWarnAfterAttempts` | `static constexpr int32`（protected） | `25` | 重试多少次之后如果**仍然只有 ASC、没有属性集**就打 Warning（`25 × 0.2s ≈ 5 秒`）。头文件注释详细解释了为什么用"重试次数"而不是"属性集为空"判定故障（见 `TryBindToOwnerASC` 的"为什么"） |
| `BindAttemptCount` | `int32`（protected） | `0` | 当前连续重试了多少次。绑定成功时清零 |
| `OwningActor` | `TWeakObjectPtr<AActor>`（private，**非 UPROPERTY**） | 空 | 这个血条的主人，由组件 `InitWidget()` 塞进来。头文件注释解释为什么用 `WeakPtr`：角色比 Widget 先死是完全可能的（敌人被销毁、关卡切换），裸指针会变成悬垂 |
| `BoundASC` | `TWeakObjectPtr<UAbilitySystemComponent>`（private，**非 UPROPERTY**） | 空 | 当前订阅的 ASC。角色销毁 / ASC 换了（比如玩家 Pawn 重生成）时要能察觉 |
| `HealthChangedHandle` | `FDelegateHandle`（private） | 无效 | 订阅 `Health` 的凭据。头文件："解绑要用 —— 没有它就只能 `RemoveAll`，会误伤别的订阅者" |
| `MaxHealthChangedHandle` | `FDelegateHandle`（private） | 无效 | 订阅 `MaxHealth` 的凭据。"**健康上限变化时血条也必须重画**" |
| `bWasLowHealth` | `bool`（private） | `false` | 上一次的残血状态，用来做边沿检测 |

**两层可见性的分工**（头文件专门写了一段澄清）：`RefreshOverheadWidgetVisibility()` 控制的是**组件**的可见性（角色层规则："谁在看我"），本地玩家的组件整个是关的，本类根本不会亮；本类管的是**控件层**的状态（"刚发生了什么"），只管自己被亮出来之后的事。两层各管一个维度，所以不冲突。

**WBP 侧要做的事**（`PHASE7_UI_SETUP.md` §2 / §3.2）：建 `WBP_OverheadHealthBar`（父类选 `URPG_OverheadHealthBarWidget`），里面放名为 `HealthBar` 的 `Progress Bar`（必填）和可选的 `NameText`（`Text`）；再在角色蓝图的 `OverheadHealthBar` 组件上把 `Widget Class` 指到它。淡入淡出/残血闪烁可选实现 `On Reveal Changed` / `On Low Health Changed` 两个事件。

---

### `Source/RPG/UI/RPG_DamageNumberWidget.h` / `Source/RPG/UI/RPG_DamageNumberWidget.cpp`

**一句话职责**：一个伤害飘字 —— 纯屏幕空间的短命控件，自己往上飘 + 淡出，时间到了自己 `RemoveFromParent`。

**类/结构体**：`URPG_DamageNumberWidget` —— 继承 `UUserWidget`，`UCLASS(Abstract)`。由 `ARPG_HUD::ShowDamageNumber()` 创建、每份伤害一个实例。

> 类注释三段：
> **【它不是 `WidgetComponent`】** 头顶血条是每个角色一个、长期存在的，所以用 `WidgetComponent` 挂在角色身上；飘字正相反：**短命、数量多、每帧都在动**。给每个飘字造一个 `WidgetComponent` 意味着给每次命中都生成一个场景组件 —— 群怪混战时开销很难看，而且组件注册/注销本身就比排版贵。所以飘字走**纯屏幕空间**：① HUD 把世界坐标投影成屏幕坐标；② 造一个本控件，用 `SetPositionInViewport` 摆在那个点上；③ 控件自己 Tick 往上飘 + 淡出，时间到了自己 `RemoveFromParent`。
> **【代价：飘字不会跟着角色走】** 屏幕位置只在**生成的那一瞬间**算一次，之后镜头一转，数字不会跟着原来那个世界坐标跑 —— 它会留在屏幕上原地飘完。"对短命（不到 1 秒）的飘字来说这个误差看不出来，是业界普遍做法。" 要做"钉在世界坐标上"的版本得在 Tick 里每帧重新投影，代价是每帧 N 次 `ProjectWorldToScreen` —— 项目里留了这个口子，见 `ARPG_HUD::bDamageNumbersTrackWorld`（默认关）。
> **【运动参数为什么在 C++】**（写在这组 `UPROPERTY` 上方的注释）把"往上飘 + 淡出"写在 C++ 而不是让 WBP 做 UMG 动画，两个理由：① 每个飘字的生命周期由 C++ 管（自己 `RemoveFromParent`），动画时长和生命周期分散在两个地方，改一个忘一个就会出现"数字淡完了还挂在屏幕上"或者"还没飘完就消失"；② WBP 的 UMG 动画在 Widget 被销毁时**不会自动停**，容易留下残留状态。需要更花哨的表现（缩放回弹、抖动）时，WBP 里再加动画叠加即可。

---

#### `void URPG_DamageNumberWidget::InitializeDamageNumber(float Amount, const FVector2D& ScreenPosition, float InLifetime)`
- **干什么**：初始化一个飘字（记原点、定时长、写数字、摆位置、对齐、设不透明、通知蓝图）。
- **关键实现**（顺序）：
  1. `OriginPosition = ScreenPosition;`
  2. `TotalLifetime = InLifetime > 0.f ? InLifetime : DefaultLifetime;`
  3. `ElapsedTime = 0.f;`
  4. `if (AmountText)` → `AmountText->SetText(FText::AsNumber(FMath::RoundToInt(Amount)));`
  5. `SetPositionInViewport(ScreenPosition, /*bRemoveDPIScale=*/true);`
  6. `SetAlignmentInViewport(FVector2D(0.5f, 0.5f));`
  7. `SetRenderOpacity(1.f);`
  8. `BP_OnDamageNumberInitialized(Amount);`
- **为什么这么写**：
  · 取整显示的理由："属性是 `float`（要支持'每秒掉 10 点'），但飘字上出现小数会很怪。"
  · `bRemoveDPIScale = true`：我们算出来的屏幕坐标是**像素**，而 UMG 的位置是 Slate 单位（受 DPI 缩放影响）；不除 DPI 的话，在 125%/150% 缩放的显示器上飘字会偏出去一大截 —— "而且只在'别人的机器上'复现，自己开发时完全正常。"
  · 轴心 `(0.5, 0.5)`：数字"以命中点为中心"冒出来，而不是以左上角对齐 —— 后者在数字位数变化时会左右跳。
  · 初始不透明 1："之后由 Tick 逐渐降到 0。"
- **被谁调用 / 调用谁**：被 `ARPG_HUD::ShowDamageNumber()` 调用（**注意**：头文件里把这个调用者写成了 `URPG_HUDWidget`，与实现不一致，以 `.cpp` 为准）；调用 `UTextBlock::SetText()`、`UUserWidget::SetPositionInViewport()` / `SetAlignmentInViewport()` / `SetRenderOpacity()`、`BP_OnDamageNumberInitialized()`。

#### `void URPG_DamageNumberWidget::UpdateScreenPosition(const FVector2D& ScreenPosition)`
- **干什么**：更新屏幕坐标 —— **仅**在"飘字跟随世界坐标"模式（`ARPG_HUD::bDamageNumbersTrackWorld == true`）下由 HUD 每帧调用。
- **关键实现**：一行 `OriginPosition = ScreenPosition;`
- **为什么这么写**：注释原话 —— 更新**原点**而不是当前位置：上飘的偏移量是相对原点算的，直接改当前位置会把已经飘出去的位移吃掉。
- **被谁调用 / 调用谁**：被 `ARPG_HUD::Tick()` 调用；不调用其它东西。

#### `float URPG_DamageNumberWidget::GetElapsedRatio() const`
- **干什么**：`BlueprintPure` 内联返回"这次飘字已经活了多久"的 0~1 比例。
- **关键实现**：`return TotalLifetime > 0.f ? FMath::Clamp(ElapsedTime / TotalLifetime, 0.f, 1.f) : 1.f;`
- **为什么这么写**：注释未说明（`TotalLifetime <= 0` 时返回 1 是防御式写法，`TotalLifetime` 在 `InitializeDamageNumber` 里已经保证 `> 0`）。
- **被谁调用 / 调用谁**：`UFUNCTION(BlueprintPure)`，本仓库 C++ 内未见调用点（给 WBP 用，比如做进度驱动的动画）。

#### `virtual void URPG_DamageNumberWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)`
- **干什么**：累加寿命、到点自毁、否则做上飘 + 淡出。
- **关键实现**：
  1. `Super::NativeTick(...);`
  2. `ElapsedTime += InDeltaTime;`
  3. `if (ElapsedTime >= TotalLifetime)` → `RemoveFromParent();` → `return;`
  4. `const float Ratio = FMath::Clamp(ElapsedTime / TotalLifetime, 0.f, 1.f);`
  5. **上飘**：`const float RiseRatio = FMath::Pow(Ratio, RiseEasingExponent);` `const float OffsetY = -RiseDistance * RiseRatio;`（屏幕坐标 Y 向下为正，所以取负）→ `SetRenderTranslation(FVector2D(0.f, OffsetY));`
  6. **淡出**：`if (Ratio > FadeOutStartRatio)` → `const float FadeRatio = (Ratio - FadeOutStartRatio) / FMath::Max(1.f - FadeOutStartRatio, KINDA_SMALL_NUMBER);` `SetRenderOpacity(1.f - FadeRatio);` `else` → `SetRenderOpacity(1.f);`
- **为什么这么写**：
  · **自己送走自己**的理由（注释原话）："让飘字管自己的生命周期，而不是由 HUD 维护一个'飘字列表 + 定时清理'：后者要在 HUD 里额外维护容器，还得处理'角色中途死了/关卡切换了列表里的指针已经失效'这类情况。谁生的谁管，边界最清楚。"（HUD 那边确实只做**清理记录**，见 `ARPG_HUD::PruneDamageNumbers()`。）
  · 缓动：`pow(Ratio, Exponent)` 在 `Exponent > 1` 时是"先快后慢"，"观感上像被击打顶出去再减速，比匀速自然得多"。
  · 用 `RenderTranslation` 而不是改 Canvas 槽位：前者是**纯渲染偏移，不触发布局重算**，"几十个飘字同时飘也不会有性能问题"。
  · 淡出分段：`Ratio <= FadeOutStartRatio` 期间保持不透明（`1.f`），之后线性降到 0 —— 分母用 `FMath::Max(1 - FadeOutStartRatio, KINDA_SMALL_NUMBER)` 防止 `FadeOutStartRatio == 1` 时除零。
- **被谁调用 / 调用谁**：Slate/UMG 每帧；调用 `RemoveFromParent()`、`SetRenderTranslation()`、`SetRenderOpacity()`。

#### 成员变量与 UPROPERTY

| 名称 | 声明 | 默认值 | 说明 |
|---|---|---|---|
| `AmountText` | `UPROPERTY(meta = (BindWidget))` `TObjectPtr<UTextBlock>`（protected） | — | 数字文本，**必填**。WBP 侧：`WBP_DamageNumber` 里拖一个 `Text` 命名为 `AmountText` |
| `RiseDistance` | `UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="RPG\|UI\|Motion", meta=(ClampMin="0.0"))` `float`（protected） | `70.f` | 飘字总共往上飘多少像素 |
| `RiseEasingExponent` | 同上（`meta=(ClampMin="0.1")`） | `2.f` | 上飘缓动指数。头文件注释：`1` = 匀速；`2~3` = 先快后慢（更像"被顶出去然后减速"）；`<1` = 先慢后快 |
| `FadeOutStartRatio` | 同上（`meta=(ClampMin="0.0", ClampMax="1.0")`） | `0.45f` | 从生命的百分之几开始淡出（`0.5` = 后一半时间在淡出） |
| `DefaultLifetime` | 同上（`meta=(ClampMin="0.05")`） | `0.9f` | 默认存活时长（秒）。`InitializeDamageNumber` 传 `InLifetime <= 0` 时用它 |
| `OriginPosition` | `FVector2D`（private，**非 UPROPERTY**） | `FVector2D::ZeroVector` | 生成时的屏幕坐标 —— 上飘是相对它算的 |
| `ElapsedTime` | `float`（private，**非 UPROPERTY**） | `0.f` | 已存活时间，`NativeTick` 里累加 |
| `TotalLifetime` | `float`（private，**非 UPROPERTY**） | `0.9f` | 本次寿命（注意默认值与 `DefaultLifetime` 的 0.9 重复声明，`InitializeDamageNumber` 会覆盖它） |

#### `void URPG_DamageNumberWidget::BP_OnDamageNumberInitialized(float Amount)`（protected）
- **干什么**：飘字刚初始化完的通知点，`BlueprintImplementableEvent`（`meta = (DisplayName = "On Damage Number Initialized")`），WBP 里实现。
- **关键实现**：C++ 无实现，只在 `InitializeDamageNumber` 末尾被调用一次，传的是**原始 `Amount`（未取整）**。
- **为什么这么写**：头文件注释 —— 在这里做 WBP 那边的表现：按数值大小换字号/颜色、播"弹出来再缩回去"的动画、按暴击播不同的音效…… 参数 `Amount` 用于让 WBP 决定"这个数字算大还是小"。
- **被谁调用 / 调用谁**：被 `InitializeDamageNumber()` 调用；由 WBP 实现。

**WBP 侧要做的事**（`PHASE7_UI_SETUP.md` §2 / §3.3）：建 `WBP_DamageNumber`（父类选 `URPG_DamageNumberWidget`），里面放名为 `AmountText` 的 `Text`（必填）；再把 `ARPG_HUD` 蓝图（`BP_RPG_HUD`）的 Class Defaults 里 `Damage Number Widget Class` 指向它。上飘与淡出不需要 WBP 做任何动画（C++ 驱动），四个运动参数可在 Class Defaults 里调。

---

### 附：这一层与其它模块的接口一览

| 方向 | 接口 | 位置 |
|---|---|---|
| 进 | `ARPG_BaseCharacter::Multicast_ShowDamageNumber()` → `ARPG_HUD::ShowDamageNumber()` | `RPG_BaseCharacter.cpp:247-309` |
| 进 | `URPG_AttributeSet::PostGameplayEffectExecute()` 发起上面那个 Multicast（**仅在 `HasAuthority()`**） | `RPG_AttributeSet.cpp:203-242` |
| 进 | `ARPG_BaseCharacter::RefreshOverheadWidgetVisibility()` 控制头顶血条**组件**的可见性（`IsLocallyControlledPlayer()` 为真则隐藏） | `RPG_BaseCharacter.cpp:184-245` |
| 进 | `ARPG_BaseCharacter` 的 `OverheadHealthBar` 组件默认子对象 | `RPG_BaseCharacter.cpp:80`、`RPG_BaseCharacter.h:352-353` |
| 进 | `ARPG_GameModeBase` 构造函数 `HUDClass = ARPG_HUD::StaticClass()` | `RPG_GameModeBase.cpp:41` |
| 读 | `URPG_CombatComponent::GetComboIndex()` / `GetAttackModule()` | `RPG_CombatComponent.h:88 / :103` |
| 读 | `URPG_AttackModuleData::GetHeavyLevelCount()` / `GetHeavyLevel()`、`FRPG_HeavyAttackLevel::RequiredChargeTime` | `RPG_AttackModuleData.h:151 / :148`、`RPG_AttackTypes.h:63-72` |
| 读 | `URPG_AttributeSet` 的 8 个属性（`Health` / `MaxHealth` / `Mana` / `MaxMana` / `Stamina` / `MaxStamina` / `Attack` / `Defense`） | `RPG_AttributeSet.h:123-166` |
| 读 | `RPGTags::State_Dodging` / `State_Attack_Charging` / `State_Attack_Transition` / `State_Attacking` / `State_Dead` | `RPG_GameplayTags.h:107-146` |

**日志类别**：本章绝大多数日志走 `LogRPG_Combat`（`Core/RPG_LogChannels.h`）；唯一的例外是 `URPG_OverheadHealthBarComponent::InitWidget()` 的"没配 Widget Class"警告，用的是 `LogTemp`。排查"血条不显示"时两条都要看。

---

## 附录：已知的代码与注释不一致项

> 这一节汇总写作过程中**全库 grep 核实**出来的问题：注释说了但代码没做、声明了但没人用、
> 或者两处注释互相矛盾。**它们不影响编译**，也不一定都是 bug（有些是预留的扩展点），
> 但看到相关注释时不要产生错误预期。
>
> 各章的详细上下文见对应章节末尾。

### 声明了但从未被引用（疑似预留或残留）

| # | 项 | 说明 |
|---|---|---|
| 1 | `URPG_GA_LightAttack::bComboWindowOpen` | 6 处写入、**0 处读取**。连段推进完全靠 `TryStartNextSegment()` 消费输入缓存 |
| 2 | `URPG_GA_LightAttack::SimulatedSegmentTimer` | 声明后从未使用 |
| 3 | `FRPG_HeavyAttackSet::ChargeMoveSpeedScale` | C++ 里没有任何读取点 |
| 4 | `RPGTags::Ability_Death` | 全库只有声明与定义两处，无引用（`GA_Death` 里也没调 `SetAssetTags`） |
| 5 | `RPGTags::Event_Combat_ChargeStart / ChargeLevelUp / ChargeRelease` | 三个标签只有声明，`GA_HeavyAttack` 并未广播或监听（蓄力节奏靠 `State.Attack.Charging.*` 标签 + UI 自行计时） |
| 6 | `RPGTags::State_Attack_Active` / `State_Attack_ComboWindow` | C++ 里没有引用点（是否在动画/配置资产里使用未核实） |
| 7 | `URPG_AbilitySystemComponent::HasAbilityForInputTag()` | 补跑阶段 8 审查时加了 RPC 白名单后**才第一次有了调用方** |

### 注释与代码不符

| # | 位置 | 情况 |
|---|---|---|
| 8 | `URPG_GA_LightAttack::ActivateAbility` 的"消耗触发输入"段 | 注释说"用 if 而不是直接调用，为了明确表达'没有缓存条目也是正常的'"，实际代码是**直接调用并忽略返回值** |
| 9 | `ARPG_BaseCharacter::ExitRagdoll()` 的幂等守卫理由 | **头文件与 .cpp 互相矛盾**：头文件说 `OnRep` 只在值真变时触发（引 `RepLayout.cpp:3372-3392`），.cpp 说引擎会为所有复制属性补发一次包括默认值 |
| 10 | `RPG_GA_Death.cpp` | 事件回调缺少迟到事件过滤（其他 GA 有 `IsEventFromCurrentSegment()` 那类检查） |
| 11 | `URPG_GA_LightAttack::OnAttackEndEvent` | 没做来源过滤，而三个窗口回调都做了 —— 注释未说明为什么这里不同 |

### 结构上的不对称（可能是刻意，但没写理由）

| # | 情况 |
|---|---|
| 12 | `ARPG_AnimNotifyState_Invulnerability` 的 `NotifyEnd` 既不设 `Target` 也不带 `OptionalObject2` 来源蒙太奇，与另外两个 `NotifyState` **不同构**，代码与注释里都没说明原因 |
| 13 | `URPG_GA_StaminaRegen::ActivateAbility` 是全目录**唯一不调用 `CommitAbility`** 的能力（它是常驻被动，这个选择可能合理，但没写理由） |
| 14 | `URPG_GA_ApplyBuff` 是通用容器，但资产标签**硬编码**为 `Ability.Buff.AttackUp` |

> 上面这些**没有一条是编译器或运行时能发现的**。写下来是因为：
> 下一个读代码的人（包括三个月后的作者）看到注释会先相信注释。

---

*本文档由 6 个独立读者分头通读源码写成 · 最后更新：阶段 8-⑦（`9ae0c93`）之后*
