# RPG —— UE5 动作角色扮演游戏（GAS 深度实践）

> 一个用 **Unreal Engine 5.8 + C++** 从零搭起来的 ARPG 战斗原型。
> 重点不是"做完了多少功能"，而是**每个系统都按引擎的设计意图实现，并且知道为什么**。

[![引擎](https://img.shields.io/badge/Unreal-5.8-0E1128)](https://www.unrealengine.com/)
[![语言](https://img.shields.io/badge/C%2B%2B-20-00599C)]()
[![网络](https://img.shields.io/badge/Networking-L1%20%2B%20L2-4B8BBE)]()
[![文档](https://img.shields.io/badge/Docs-15%20%E7%AF%87-6A9955)](./Docs/)

---

## 这是什么

一个第三人称 ARPG 的战斗核心：**5 段轻击连段、3 段蓄力重击、切手技、无敌帧闪避**，
敌人有完整的感知与行为树 AI，支持 **Listen Server 双人联机**。

技术栈的重心是 **GameplayAbilitySystem（GAS）** —— 状态、能力、效果、表现四层
严格分开，扩展一个新技能不需要改动输入层和 UI 层的一行代码。

```
按键 → 输入标签 → ASC 映射表 → GameplayAbility → GameplayEffect → GameplayCue
         ↑                                          ↓
      增强输入                                  属性集（唯一扣血入口）
                                                     ↓
                                            事件 / 复制 / 委托 → 动画、AI、HUD
```

---

## 已经能跑起来的

| 系统 | 内容 |
|---|---|
| **战斗** | 5 段轻击连段（输入缓存 + 衔接窗口）、3 段蓄力重击、切手技、闪避（冲量位移 + 无敌帧） |
| **资源** | 耐力消耗与恢复（停手 3 秒后自动恢复）、生命 / 法力，全部走 GAS 属性 |
| **敌人 AI** | 视觉感知、行为树（巡逻 / 追击 / 攻击，Latent Task）、受击打断、死亡布娃娃 |
| **生死流程** | 受击反应、死亡五步编排、布娃娃、玩家/敌人重生 |
| **场景效果** | 可碰撞的治疗 / 增益 / 减益：药水、卷轴、毒瓶、治疗泉、毒池、陷阱。同一个 C++ 类（`ARPG_EffectVolume`）靠配置覆盖全部玩法 |
| **战斗 HUD** | 属性条（血/蓝/耐力）+ 攻防数值、招式段名、蓄力条、闪避图标、伤害飘字、头顶血条、死亡面板 |
| **联机** | L1 基础复制 + L2 权威与预测（Listen Server），单机自动降级 |

---

## 技术亮点

> 每一条都能在面试里展开讲 5 分钟，并在代码里找到对应实现。

### 架构

| 亮点 | 一句话 | 代码 |
|---|---|---|
| **ASC 归属分离** | 玩家 ASC 挂在 `PlayerState`（死亡重生不丢状态），敌人挂在自己身上；用**一个虚函数** `GetASCInternal()` 把差异收敛到一处 | `Character/RPG_BaseCharacter.cpp` |
| **输入 → 标签 → 能力** | 新增技能只改配置资产，`PlayerController` 一行都不用动 | `Core/RPG_PlayerController.cpp` |
| **"是否攻击中"的真相源是标签** | 不是 bool —— 生命周期由 GAS 托管，被打断时自动清理，还天然支持联机复制 | `Core/RPG_GameplayTags.h` |
| **`Ability.*` 与 `State.*` 分离** | 前者是能力身份（给 GAS 机制用），后者是角色状态（给动画/AI/UI 查） | 同上 |

### GAS

| 亮点 | 一句话 |
|---|---|
| **零计时代码的"停手 3 秒恢复"** | 用 `GE_Duration` 表达时间、`Target Tag Requirements` 表达开关。没有一行 `SetTimer`，没有一个倒计时变量 |
| **自定义 AbilityTask** | 武器轨迹检测：连续扫掠防隧穿、命中去重、生命周期自管理 |
| **自定义 ExecutionCalculation** | 伤害公式要同时读攻防双方属性，用 `D/(D+K)` 减伤曲线而不是减法 |
| **元属性作为伤害入口** | 所有伤害写进 `IncomingDamage`，在 `PostGameplayEffectExecute` 里统一后处理（无敌判定 / 飘字 / 死亡广播） |
| **事件驱动编排** | 死亡流程五步没有调用点 —— 加"死亡掉装备"只需新增一个监听者 |

### 联机

| 亮点 | 一句话 |
|---|---|
| **两级解耦的联机适配** | "配置数据"（标签→能力映射）两端都要有；"运行时状态"（`FGameplayAbilitySpecHandle`）只能服务器产生。混在一起就会出现"客户端永远拿不到映射表" |
| **AI 和玩家走完全相同的输入入口** | 不是给 AI 另写一条路径 —— 连段、缓存、耐力消耗自动全部生效 |
| **权威与预测的边界** | 客户端命中判定只为手感，真实伤害必须来自服务器；事件广播**不会回滚**，所以只由服务器垄断 |

### 工作流

这个项目最特别的地方不是代码，是**怎么写的**：

- **先查引擎源码，再写代码。** 所有"和网上教程不一样"的写法，都能在
  [`ARCHITECTURE.md` 附录 B](./Docs/ARCHITECTURE.md) 里找到引擎依据。
  查证过程中发现三处照着旧资料写的 API 在 5.8 **已经不存在**。
- **提交前跑独立代码审查。** 阶段 6 那次一口气抓出 5 个问题（其中一个让整个受击功能是死代码）；
  阶段 8 补跑时抓出 15 个 —— 包括"为消灭假警报写的诊断代码自己引入了新假警报"。
  审查记录见 [`REVIEW_PHASE8.md`](./Docs/REVIEW_PHASE8.md)。
- **注释解释"为什么"，不复述代码字面。** 尤其是那些**踩过的坑**——
  比如为什么 `InstancingPolicy` 必须显式设成 `InstancedPerActor`（引擎默认值对连段是灾难）。

---

## 快速开始

```bash
# 1. 克隆
git clone <repo-url> RPG

# 2. 生成工程文件（Windows）
"C:/Program Files/Epic Games/UE_5.8/Engine/Build/BatchFiles/GenerateProjectFiles.bat" -project="RPG.uproject" -game

# 3. 编译
"<UE路径>/Engine/Build/BatchFiles/Build.bat" RPGEditor Win64 Development \
    -Project="RPG.uproject" -WaitMutex
```

打开 `RPG.uproject`，**蓝图资产需要按下面对应阶段的文档配置**（C++ 代码可以直接编译，
但蒙太奇、GE、行为树这些是资产，需要手工创建）。

| 想跑起来 | 按顺序读 |
|---|---|
| 角色能走能打 | [`PHASE1_EDITOR_SETUP.md`](./Docs/PHASE1_EDITOR_SETUP.md) |
| 战斗手感（连段/蓄力/闪避） | [`PHASE2_COMBAT_SETUP.md`](./Docs/PHASE2_COMBAT_SETUP.md) |
| 耐力 / 增益 | [`PHASE3_RESOURCE_SETUP.md`](./Docs/PHASE3_RESOURCE_SETUP.md) |
| 动画蓝图与蒙太奇 | [`PHASE4_ANIMATION_SETUP.md`](./Docs/PHASE4_ANIMATION_SETUP.md) · [`PHASE4_MONTAGE_SETUP.md`](./Docs/PHASE4_MONTAGE_SETUP.md) |
| 敌人 AI | [`PHASE5_AI_SETUP.md`](./Docs/PHASE5_AI_SETUP.md) |
| 受击 / 死亡 / 重生 | [`PHASE6_HIT_DEATH_SETUP.md`](./Docs/PHASE6_HIT_DEATH_SETUP.md) |
| 战斗 HUD | [`PHASE7_UI_SETUP.md`](./Docs/PHASE7_UI_SETUP.md) |
| 联机验证 | [`PHASE8_NETWORKING.md`](./Docs/PHASE8_NETWORKING.md) |
| 治疗 / 增益 / 减益拾取物 | [`PHASE9_EFFECT_PICKUP_SETUP.md`](./Docs/PHASE9_EFFECT_PICKUP_SETUP.md) |

### 联机测试

编辑器 Play 按钮旁的下拉箭头 → `Number of Players = 2` + `Net Mode = Play As Listen Server`。
详细步骤与 28 项验收清单见 [`PHASE8_NETWORKING.md`](./Docs/PHASE8_NETWORKING.md)。

---

## 目录结构

```
Source/RPG/
├── Core/           框架层：日志、GameplayTag 体系、PlayerState/Controller/GameMode、输入配置
├── Character/      角色层：BaseCharacter（接口默认实现 + 相机 + 布娃娃）/ Player / Enemy
├── AbilitySystem/  GAS 层
│   ├── Abilities/      GA 基类 + 10 个具体能力
│   ├── Effects/        自定义 ExecutionCalculation
│   ├── Tasks/          自定义 AbilityTask
│   ├── ASC 扩展、属性集、函数库
├── Combat/         战斗规则：输入缓存、战斗状态组件、攻击模组 DataAsset
├── Animation/      AnimInstance 基类、AnimNotify 桥接
├── AI/             AIController、黑板键常量、行为树装饰器/服务/任务
├── World/          场景效果触发器（治疗 / 增益 / 减益的拾取物与区域）
├── UI/             HUD、属性条、头顶血条、伤害飘字
└── Interfaces/     只补充引擎接口没有的东西
```

**用 Feature Folder 而不是 UE 默认的 `Public/Private` 镜像层** ——
`.h`/`.cpp` 同目录，靠 `Build.cs` 的 `PublicIncludePaths.Add(ModuleDirectory)`
让 `#include "AbilitySystem/Abilities/Xxx.h"` 这样的模块根相对路径生效。

---

## 文档

这个项目的文档量和代码量是同一个量级 —— 因为**踩坑的原因在代码里看不出来**。

| 文档 | 回答什么 | 适合谁 |
|---|---|---|
| [`REVIEW_GUIDE.md`](./Docs/REVIEW_GUIDE.md) | **19 次提交的变更编年史**：每个决定在什么背景下做的、踩了什么坑、怎么修的。含"审查锚点"（哪些地方看着可疑但其实是对的）和**诚实的未验证清单** | **想快速 review 整个项目的人** |
| [`CODE_REFERENCE.md`](./Docs/CODE_REFERENCE.md) | **逐文件、逐函数、逐枚举的完整参考**（5400+ 行）。每个函数：干什么 / 关键实现 / 为什么这么写 / 被谁调用 | 想深挖实现的人 |
| [`ARCHITECTURE.md`](./Docs/ARCHITECTURE.md) | 现在**是什么样**：目录结构、类依赖、GAS 链路、标签体系、32 条面试技术亮点、**UE 5.8 API 核查速查表** | 想了解设计的人 |
| [`REVIEW_PHASES_0-4.md`](./Docs/REVIEW_PHASES_0-4.md) | 这些零件**怎么咬合成一条链** | 动手改代码前 |
| [`REVIEW_PHASE8.md`](./Docs/REVIEW_PHASE8.md) | 阶段 8 的**独立代码审查记录**：15 个确认问题 + 4 个待办 + "已核实无误别再怀疑"清单 | 想知道代码哪里有问题的人 |
| `PHASE1~8_*.md` | 每个阶段的**编辑器配置步骤**（蓝图资产怎么建、参数填什么） | 要跑起来的人 |

---

## 已知边界

**明确不做的**（不是"没做"，是"决定不做"）：

| 项 | 为什么 |
|---|---|
| L3 联机（延迟补偿 / 位置回滚 / 专用服务器） | MVP 阶段投入产出比差，且未经真实网络环境验证容易在面试中被问穿 |
| 格挡 | 本期范围外 |
| 网络平滑调参 | 同上 |

**已知但未修的问题**（记录在 [`REVIEW_PHASE8.md`](./Docs/REVIEW_PHASE8.md) §3）：

| 严重度 | 问题 |
|---|---|
| 高 | `Server_PushInputTag` 与 `ServerTryActivateAbility` 走**不同的 ActorChannel**，UE 只保证同通道内有序。真实网络下 RPC 晚到会让服务器凭空多打一段。**PIE 抓不到**（loopback 无损） |
| 中低 | 头顶血条的"主人变成 null"路径会保留旧订阅（当前无调用点可达） |
| 低 | `HideAfterDamage` 立刻 `Collapsed`，WBP 的淡出动画看不到 |
| 待决定 | 尸体挨打不冒伤害飘字（那道"已死就早退"把飘字一起吞了） |

**尚未验证的**：联机部分需要 PIE 双客户端实跑 —— 见
[`PHASE8_NETWORKING.md`](./Docs/PHASE8_NETWORKING.md) §3 的 28 项清单。
代码编译通过 ≠ 功能验证通过，这一点在这个项目里是硬规矩。

---

## 代码规模

| | |
|---|---|
| 提交数 | 22 |
| C++ 源文件 | 103（`.h` + `.cpp`） |
| 引擎 API 查证记录 | `ARCHITECTURE.md` 附录 B，约 60 条 |
| 独立代码审查 | 2 轮，累计 20 个确认问题（阶段 6 五个 / 阶段 8 十五个） |

---

*用 UE 5.8 + C++ 实现 · 工程由第三人称模板创建，已移除全部模板变体代码*
