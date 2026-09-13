# 阶段 6 · 受击 / 死亡 / 布娃娃 / 重生 配置指南

> 让"打到人"和"被打死"这两件事有反馈。
> 逻辑全在 C++ 里，编辑器里要做的是**做蒙太奇 + 填表 + 确认物理资产**。

---

## 0. C++ 侧已经就绪的东西

| 类 | 编辑器里显示为 | 作用 |
|---|---|---|
| `URPG_GA_HitReact` | — | 受击反应。监听 `Event.Combat.Hit`，打断攻击/闪避，播踉跄动画 |
| `URPG_GA_Death` | — | 死亡编排者。监听 `Event.Combat.Death`，挂 `State.Dead` → 播死亡动画 → 布娃娃 → 重生倒计时 |
| `ARPG_BaseCharacter` | — | 新增了布娃娃开关、重生复位、蒙太奇池配置 |
| `ARPG_AIController` | — | 新增了 `StopAI` / `RestartAI`（死亡停大脑、重生重启） |

### 阶段 6 之前的死亡判定长什么样

`RPG_AttributeSet::PostGameplayEffectExecute` 里**早就在广播** `Event.Combat.Death` 了
（阶段 2 写的），但那时的日志是：

```
[BP_RPG_Enemy_C_0] 生命归零，广播死亡事件
```

然后……什么都没发生。因为**没人监听**这个事件。
这一阶段补上的就是"监听端"，顺便把整条死亡链路串起来。

---

## 1. 先看懂死亡链路（配置之前必须理解的东西）

```
敌人身上 GE_Damage 结算（服务器）
   │
   ├─ 血量 100 → 0
   │
   └─► SendGameplayEventToActor(Event.Combat.Death)
          │
          └─► ASC 查 AbilityTriggers 表
                 │   ← ★ 连接点是**标签**，不是函数调用
                 └─► URPG_GA_Death::ActivateAbility
                        │
                        ├─ ① 挂 State.Dead（loose tag，CountToOwner 复制）
                        │     ↑ 从这一刻起，所有能力激活都被 ActivationBlockedTags 挡死
                        │
                        ├─ ② CancelAllAbilities(this)
                        │     ↑ 清掉 State.Attacking / State.Invulnerable 等残留标签
                        │
                        ├─ ③ Character->OnDeathStarted()
                        │     ↑ 敌人的 AI 在这里 StopAI()（停寻路、停行为树）
                        │
                        ├─ ④ 播死亡蒙太奇（AbilityTask 跟踪结束）
                        │
                        └─ ⑤ 蒙太奇播完 → EnterRagdoll() → StartRespawnCountdown()
```

受击是同一套机制的轻量版：

```
扣血但没死 → SendGameplayEventToActor(Event.Combat.Hit)
                └─► URPG_GA_HitReact
                       ├─ CancelAbilitiesWithTag = [Ability.Attack, Ability.Dodge]  ← 打断
                       ├─ ActivationOwnedTags = [State.Hit]                        ← 挂状态
                       └─ 播受击蒙太奇 → 播完结束能力（标签自动摘掉）
```

> 💡 **为什么死亡标记用 loose tag 而不是 GE？**
> 因为复活时要"清空身上所有 GE"（`RemoveActiveEffects`）。
> 如果死亡标记本身是个 GE，它会被那一步顺手清掉 ——
> 复活逻辑就得反过来依赖"我清掉了什么"来推断状态，非常绕。
> loose tag 不在 GE 的生命周期里，只能被显式增删。

> ⚠️ **事件只在服务器广播。**
> 客户端的血量是本地预测的（`PredictivelyExecuteEffectSpec`），
> 但**事件广播不会回滚** —— 两端都发的话，预测失败时客户端会"死一次又活过来"。
> 所以 `RPG_AttributeSet` 里做了 `HasAuthority()` 判断。
> 客户端看到的受击/死亡表现全部来自复制（蒙太奇信息、标签、布娃娃开关）。

---

## 2. 步骤 1 · 做四个受击蒙太奇 + 一个死亡蒙太奇

### 2.1 用现成的动画

工程里已经导入了 Mixamo 的受击和死亡动画，直接用：

| 用途 | 源动画 | 建议命名 |
|---|---|---|
| 受击（轻·正面） | `standing_react_small_from_front` | `AM_Hit_01` |
| 受击（轻·爆头） | `standing_react_small_from_headshot` | `AM_Hit_02` |
| 受击（重·来自左侧） | `standing_react_large_from_left` | `AM_Hit_03` |
| 受击（重·腹部） | `standing_react_large_gut` | `AM_Hit_04` |
| 死亡（向后倒） | `standing_death_backward_01` | `AM_Death_01` |
| 死亡（向前扑） | `standing_death_forward_01` | `AM_Death_02` |

源动画都在 `Content/_My/Animation/Anim/` 下。

**四个受击而不是一个**，是因为连续挨打时反复播同一个动作会明显发假 ——
打击感的一半来自"每次反馈略有不同"。代码里是随机抽的。

### 2.2 建蒙太奇

在 `Content/_My/Animation/Montages/` 下新建文件夹 `HitReact`，把上面几个动画
**右键 → Create → Anim Montage**（或者直接把动画拖进内容浏览器选 Anim Montage）。

**每个蒙太奇都要做的三件事**（和攻击蒙太奇完全一样，
详细步骤见 [`PHASE4_MONTAGE_SETUP.md`](PHASE4_MONTAGE_SETUP.md) 的步骤 1 / 3 / 4）：

| 项 | 值 | 为什么 |
|---|---|---|
| Slot 名字 | `DefaultSlot` | 不一致 → 蒙太奇**不播且不报错** |
| Slot 段落长度 | 覆盖整段动画 | 只覆盖前几帧 → 播到一半弹回移动姿势 |
| `Blend In` → `Blend Time` | `0.05` | 默认 0.25 秒。受击动画本身很短，默认值会把它整个吃掉 |
| `Blend Out` → `Blend Time` | `0.1` | 同上 |

> ⚠️ **死亡蒙太奇额外注意**：确认 `Blend Out` 不要太长（建议 `0.1`）。
> 蒙太奇播完的下一帧就进布娃娃了，淡出时间太长会在"站着"和"躺下"之间留一帧空档。

> 💡 **Root Motion**：`AnimInstance` 的 `Root Motion Mode` 默认是
> `RootMotionFromMontagesOnly`（`AnimInstance.cpp:202`），所以死亡动画里的位移
> 会正常生效，不需要额外配置。

### 2.3 关于"蒙太奇播完之后"

- **受击蒙太奇**播完 → 能力结束 → `State.Hit` 摘掉 → 恢复正常。
- **死亡蒙太奇**播完 → `EnterRagdoll()` → 网格交给物理。

如果死亡蒙太奇**末尾已经在躺着了**，那么"蒙太奇最后一帧 → 布娃娃第一帧"的过渡
会非常自然。如果动画末尾还站着就进布娃娃，会看到人突然软倒 —— 也能用，
但不如选一个末帧躺地的动画。

---

## 3. 步骤 2 · 建两个能力蓝图

### 3.1 `GA_HitReact`

`Content/_My/GAS/GA/` 右键 → **Blueprint Class** → 展开 **All Classes** → 搜 `RPG_GA_HitReact`
→ 命名 `GA_HitReact`。

**不需要配任何东西。** 打开 Class Defaults 可以看到下面这些已经被 C++ 构造函数设好了
（这一节列出来是让你知道**它们是什么、为什么是这样**）：

| 字段 | 值 | 位置 |
|---|---|---|
| `Instancing Policy` | `Instanced Per Actor` | Class Defaults → Ability |
| `Net Execution Policy` | `Server Only` | 同上 |
| `Retrigger Instanced Ability` | ✅ **勾上** | 同上 |
| `Ability Triggers` | `Event.Combat.Hit`（On Gameplay Event） | 同上 |
| `Cancel Abilities With Tag` | `Ability.Attack`、`Ability.Dodge` | 同上 → Tags |
| `Activation Owned Tags` | `State.Hit` | 同上 → Tags |
| `Activate On Granted` | ⬜ **不要勾** | Class Defaults → RPG → Ability |

> ⚠️ **`Retrigger Instanced Ability` 是这一阶段的头号坑。**
> 不勾的话，受击能力激活期间再来一次激活请求会被引擎直接拒绝
> （`AbilitySystemComponent_Abilities.cpp:1846`），
> 表现是**被打第一下会踉跄，紧接着被打第二下完全没反应**。
> 敌人连击的时候特别明显，而且不报任何错。
>
> 勾上之后引擎会先 `EndAbility` 旧实例再重新激活（同文件 `:1836-1844`），
> 所以代码里 `EndAbility` 会把旧的蒙太奇任务回调**先 RemoveDynamic 再 EndTask**
> —— 少任何一步，上一次的收尾回调都会冒充这一次的结果。

> ⚠️ **`Activate On Granted` 必须保持不勾。** 勾上意味着"授予后立刻激活"，
> 那么角色一出生就会自动播一次受击动画，然后一直卡在 `State.Hit` 里。

### 3.2 `GA_Death`

同样步骤，搜 `RPG_GA_Death` → 命名 `GA_Death`。同样**不需要配任何东西**：

| 字段 | 值 |
|---|---|
| `Instancing Policy` | `Instanced Per Actor` |
| `Net Execution Policy` | `Server Only` |
| `Retrigger Instanced Ability` | ⬜ 不勾（死亡只能发生一次） |
| `Ability Triggers` | `Event.Combat.Death`（On Gameplay Event） |
| `Activate On Granted` | ⬜ **不要勾** |

> 💡 死亡只能发生一次，靠的不是 `Retrigger` 这个开关，而是
> `State.Dead` 被加进了基类的 `ActivationBlockedTags` ——
> 死亡一挂标签，第二次死亡事件的触发就被挡在门外了。
> `Retrigger Instanced Ability` 不勾只是让规则在编辑器里也看得见。

### 3.3 编辑器里的调试技巧

`Ability Triggers` 是**声明式**的 —— 没有"谁调用了 GA_Death"这样的代码可查。
如果事件迟迟不触发，在编辑器控制台里跑：

```
Log LogRPG_Ability Verbose
Log LogRPG_Combat  Verbose
```

`RPG_AttributeSet` 会打 `[角色名] 生命归零，广播死亡事件`。
**看到这行但角色没反应** = 事件发出去了但没人接 → 检查能力有没有被授予（步骤 3/4）。
**看不到这行** = 伤害没结算到 0，问题在伤害链路，不在这一阶段。

---

## 4. 步骤 3 · 配置玩家蓝图 `BP_RPG_Player`

打开 `Content/_My/Character/Player/BP_RPG_Player.uasset` → **Class Defaults**。

### 4.1 授予两个能力

`RPG > Abilities` → **`Startup Passive Abilities`** → 加两条：

| 数组元素 | 值 |
|---|---|
| `[0]` | `GA_HitReact` |
| `[1]` | `GA_Death` |

> ⚠️ **`Startup Passive Abilities` 这个名字有点误导。** 它装的是
> **所有没有输入标签的能力**，其中有两类：
> - `Activate On Granted` 勾上的 → 授予后立刻常驻激活（比如 `GA_StaminaRegen`）
> - 没勾的 → 只是**待命**，等自己的 `Ability Triggers` 事件（就是这两个）
>
> 别把 `GA_HitReact` / `GA_Death` 放进 `Startup Abilities`（那张表是
> "输入标签 → 能力"的映射，塞进去需要编一个假的输入标签，而且永远不会被触发）。

### 4.2 配受击和死亡蒙太奇

`RPG > Animation > Hit`：

| 字段 | 值 |
|---|---|
| `Hit React Montages` | 加 4 条 → `AM_Hit_01` ~ `AM_Hit_04` |
| `Hit React Play Rate` | `1.0` |
| `Death Montage` | `AM_Death_01` |
| `Death Montage Play Rate` | `1.0`（觉得拖沓可以调到 `1.3`） |

### 4.3 配重生

`RPG > Death`：

| 字段 | 值 | 说明 |
|---|---|---|
| `Respawn Delay` | `3.0` | **留 0 就是不重生，玩家会永远躺着** |

> ⚠️ 这是最容易漏的一步。默认值是 `0`，而 `0` 的语义是"不重生"，
> 是给敌人用的默认值。玩家蓝图必须显式改成 3~5 秒。

---

## 5. 步骤 4 · 配置敌人蓝图 `BP_RPG_Enemy`

同样在 **Class Defaults** 里：

### 5.1 能力

`Startup Passive Abilities` 追加 `GA_HitReact` 和 `GA_Death`。

### 5.2 蒙太奇

`RPG > Animation > Hit`：

| 字段 | 玩家 | 敌人 | 说明 |
|---|---|---|---|
| `Hit React Montages` | 4 条 | 同样 4 条（可以换别的） | |
| `Hit React Play Rate` | `1.0` | `0.9` | 敌人放慢一点显得笨重 |
| `Death Montage` | `AM_Death_01` | `AM_Death_02` | 玩家向后倒，敌人向前扑 |
| `Death Montage Play Rate` | `1.0` | `1.0` | |

### 5.3 重生

| 字段 | 值 | 说明 |
|---|---|---|
| `Respawn Delay` | `0`（**保持默认**） | 死了就躺着。玩家刚打赢又冒出来一个会让战斗失去意义 |

> 💡 想让某个精英怪重生（比如训练木桩），把它自己的 `Respawn Delay` 改成 `5` 就行。
> 复活位置是**这个敌人在关卡里被摆放的位置**（`CachedSpawnTransform`，BeginPlay 时记录）。
> 重生的敌人会重启行为树、清空黑板记忆 —— 不会带着"我上次在追玩家"的状态醒过来。

---

## 6. 步骤 5 · 确认骨架网格体有 Physics Asset ★

**布娃娃的前提条件。少了它，`SetAllBodiesSimulatePhysics` 完全不生效，
而且不报任何错** —— 表现是"死亡动画播完，人站着不动"。

### 怎么确认

1. 双击角色用的骨架网格体（内容浏览器里搜 `X_Bot`）
2. 右侧 **Details** 面板搜 `Physics Asset`
3. 这个字段**必须非空**

> 工程里已经有 `Content/_My/Animation/Mixmo/X_Bot_PhysicsAsset.uasset`，
> 所以多半已经是挂好的。**确认一下就行。**

### 如果是空的

1. 内容浏览器 → 右键 → 在 Add 菜单的搜索框里搜 **`Physics`**
2. 选 **Physics Asset** 工厂 → 在 "Pick Skeletal Mesh" 里选 `X_Bot`
3. 弹出的 "New Body" 对话框直接点 **OK**（默认参数对主角色的胶囊体生成已经够用）
4. 创建完它会**自动挂回网格体上**（工厂内部 `bSetToMesh = true`）

> ⚠️ 生成完要**检查一下胶囊体有没有明显穿模**。物理资产是自动生成的，
> 四肢的胶囊体常常比网格体粗。穿模的后果是布娃娃倒地的姿势有点怪 ——
> 对 MVP 可以接受，但如果差得离谱，在 Physics Asset 编辑器里拖一下半径即可。

---

## 7. 步骤 6 · 确认关卡里有 PlayerStart

玩家重生走的是 `AGameModeBase::FindPlayerStart()`，而不是"出生时站的位置"。

1. 打开 `Content/_My/Maps/L_RPG_TestArena`
2. 在世界大纲里搜 `PlayerStart`

> 没有的话：放置 Actors 面板搜 `Player Start`，拖一个到关卡里希望玩家复活的位置。
> 第三人称模板创建的关卡默认是有的。

> 💡 **为什么玩家和敌人的重生位置规则不一样？**
> 敌人"回到自己出生的地方"是对的 —— 它就是这个关卡里的一个固定守卫。
> 玩家则应该回到关卡的正式入口。用 `FindPlayerStart` 而不是自己遍历 Actor 找，
> 是因为它内部还处理了"这个点是否已被占用"、多人时按玩家编号分配等规则。

### 兜底行为

拿不到 PlayerStart 时（比如直接拖一个 Player 进空关卡测试），
会回落到"出生时站的位置"并打一条 Warning：

```
LogRPG_Combat: Warning: [BP_RPG_Player_C_0] 拿不到 PlayerStart，重生回出生位置
```

---

## 8. 测试清单

按顺序做，每一条都能单独定位问题。

### 8.1 受击

| 操作 | 预期 |
|---|---|
| 敌人打玩家一下（不掉血到 0） | 玩家播受击动画，正在出的招被打断 |
| 连挨两下 | **两次都有反应**（第二下刷新踉跄，不是没反应） |
| 玩家打敌人一下 | 敌人播受击动画，敌人正在出的招被打断 |
| 看日志 | `LogRPG_Combat Verbose` → `[角色名] 广播受击事件（12.0 点）` |

### 8.2 死亡 + 布娃娃

| 操作 | 预期 |
|---|---|
| 把敌人的血打空 | 敌人播死亡动画 → 播完**整个人瘫下去**（不是站着不动） |
| 再砍尸体 | **没有受击反应**，日志也不刷 Warning |
| 观察日志 | `[敌人名] 生命归零，广播死亡事件` → `[敌人名] 播放死亡蒙太奇 ...` |

> ⚠️ **布娃娃没反应时的排查顺序**：
> ① 骨架网格体有没有 Physics Asset（步骤 5）→ ② 死亡蒙太奇配了没有 →
> ③ `LogRPG_Combat Verbose` 看有没有"播放死亡蒙太奇"这行。

### 8.3 重生

| 操作 | 预期 |
|---|---|
| 玩家死亡后等 3 秒 | 回到 PlayerStart，满血，镜头朝向正确 |
| 复活后立刻按左键 | 能正常攻击（说明 `State.Dead` 被摘掉了） |
| 复活后跑两步再站着 | 耐力**会恢复**（说明常驻被动被重启了） |
| 复活后按住 Shift | 能跑 |
| 敌人（把 `Respawn Delay` 临时改成 5 测试） | 回到放置位置，重新开始巡逻/追击，且不会直奔玩家 |

> ⚠️ **"复活后耐力不恢复"是个很容易漏的 bug。**
> 死亡流程里的 `CancelAllAbilities()` 会把 `GA_StaminaRegen` 一起停掉，
> 而它不会自己起来。`ResetForRespawn()` 里的 `ReactivatePassiveAbilities()`
> 就是修这个的 —— 如果你在别的地方加了新的常驻被动能力，它自动也会被重启。

### 8.4 联机（PIE，Number of Players = 2）

| 操作 | 预期 |
|---|---|
| 客户端 2 看客户端 1 被打死 | 尸体正常倒地（布娃娃开关是复制的） |
| 客户端 2 看客户端 1 挨打 | 受击蒙太奇正常播（蒙太奇信息是复制的） |
| 客户端 2 打敌人 | 敌人正常受击、正常死亡 |
| **客户端 1 死亡后复活** | 在客户端 1 的屏幕上**立刻**回到 PlayerStart，不是"停一会儿才被拽回去" |
| 观察日志 | **受击/死亡事件只在服务器的日志里出现一次**，客户端不重复广播 |

> 💡 **为什么复活要显式调 `ClientSetLocation`？**
> 本地控制的角色在客户端是 `AutonomousProxy`，位置由客户端预测驱动，
> 而承载位置的 `AActor::ReplicatedMovement` 复制条件是 `COND_SimulatedOnly`
> —— **根本不会发给 AutonomousProxy**。服务器光调 `SetActorTransform`
> 的话，客户端要等下一次移动纠偏才知道自己被传送了。

> 💡 布娃娃的**姿势**在两端会有细微差异 —— 这是设计如此。
> 复制的只是"倒没倒"这个开关，物理由各端的 Chaos 各自算。
> 想逐帧一致要用 Network Physics 那一套，那属于 L3 范围（本项目不做）。

---

## 9. 排查表

| 症状 | 原因 | 修法 |
|---|---|---|
| **打死敌人，人站着不动** | 骨架网格体没有 Physics Asset | 见步骤 5 |
| 打死敌人，尸体**倒下去又站起来** | 复活后没传送 / `Respawn Delay` 配在了敌人身上 | 敌人保持 `0` |
| **挨打完全没反应** | `GA_HitReact` 没进 `Startup Passive Abilities` | 见步骤 3.1 / 4.1 |
| | 或 `Activate On Granted` 被勾上了（能力变成常驻，被 `CancelAbilitiesWithTag` 之外的因素影响） | 取消勾选 |
| **连挨两下只有第一下有反应** | `Retrigger Instanced Ability` 没勾 | 见步骤 3.1 |
| 挨打了但**没打断出招** | `Cancel Abilities With Tag` 里没有 `Ability.Attack` | 检查 Class Defaults → Tags |
| **死亡动画播不出来** | 蒙太奇 Slot 名字不是 `DefaultSlot`（不报错） | 见步骤 2.2 |
| 死亡动画播了但**只动了一下** | `Blend In` / `Blend Out` 还是默认的 0.25 秒 | 调到 0.05 / 0.1 |
| **玩家死后不复活** | `Respawn Delay` 还是默认的 `0` | 改成 3~5 |
| 复活后**按键全没反应** | `State.Dead` 没被摘掉 | 检查 `ResetForRespawn` 有没有被执行（看日志有没有"已重生到"） |
| 复活后**耐力不恢复** | `ReactivatePassiveAbilities` 没跑到 | 同上 |
| 复活后**敌人站着不动** | `OnRespawned` 没接上 / AI 重启失败 | `Log LogRPG_AI Verbose` 看有没有"AI 已恢复" |
| 联机时**客户端复活后位置不对** | `ClientSetLocation` 没生效 | 见 §8.4 的说明；确认 `ARPG_Player::OnRespawned` 跑到了 |
| 打尸体时**日志刷屏** | 尸体被反复广播受击事件 | 已在 `RPG_AttributeSet` 里用 `OldHealth <= 0` 挡住 |
| 联机时**受击表现播两遍** | 事件的权威判断失效 | 检查 `RPG_AttributeSet` 里的 `HasAuthority()` |

---

## 10. 本期没做的（明确记下来，避免以后误以为是 bug）

| 项 | 说明 |
|---|---|
| **受击硬直（hit stun）** | 受击动画期间**玩家仍可移动**。`Move()` 只挡了"死亡中"，没挡"受击中"。想要真正的硬直需要再加一个 `State.Hit` 检查 + 时长参数 |
| **尸体清理** | `Respawn Delay = 0` 的敌人尸体会永远留在场上。没有溶解/延迟销毁 |
| **死亡掉落 / 经验** | 没有任何击杀奖励。这些在标签驱动架构下都是**新增监听者**，不用改现有代码 |
| **轻重受击分级** | `Payload.EventMagnitude` 里带着本次伤害值，但目前没有用它区分"轻踉跄/重击飞"。要做的话在 `GA_HitReact` 里读 `TriggerEventData->EventMagnitude` 选不同蒙太奇池 |
| **死亡时的受击方向** | Mixamo 有 `from_left` / `from_right` / `from_headshot` 三种方向，但目前是**随机抽**，没按攻击来向选。要做需要在 `FGameplayEventData` 里带上命中方向（`HitResult` 里有） |
| **玩家死亡 UI** | 没有"你死了"提示，只有 3 秒黑屏期 |
| **复活无敌帧** | 复活瞬间没有保护，会被守在出生点的敌人立刻再打死 |

> 这几条里，**受击硬直**和**受击方向**是面试时最容易被追问的两个点。
> 现在的架构留了口子（`Payload.EventMagnitude`、`State.Hit` 标签），
> 想做的话都是增量改动，不需要动链路。
