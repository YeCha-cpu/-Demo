# 阶段 2 · 战斗系统配置指南（批次 1）

> 代码已完成并编译通过。本文档说明如何在编辑器里把战斗链路接起来。
> 顺序是**先数值、后动画** —— 每一步都能独立验证，出问题好定位。

---

## 数据流总览

```
  按键
   ↓
输入缓存（RPG_InputBuffer）
   ↓
GA_LightAttack ──── 扣耐力 ──→ GE_StaminaCost
   ↓
播放蒙太奇
   ↓
AnimNotify ──── 广播 GameplayEvent ───┐
   ↓                                 ↓
（动画表现）              AttackWindow / ComboWindow / AttackEnd
                                     ↓
                            GA 监听并驱动状态推进
                                     ↓
                          WeaponTrace 检测（连续 Sweep）
                                     ↓
                             GE_Damage（含 Execution）
                                     ↓
                          伤害计算 → IncomingDamage
                                     ↓
                          AttributeSet 扣血 + 广播死亡
```

**关键设计**：数值链路和动画链路是**解耦**的。没有蒙太奇时，代码会走"模拟时序"分支，
用定时器代替动画事件驱动状态推进——所以下面步骤 1~6 完全不需要动画就能验证。

---

## 步骤 1 · 攻击模组 DataAsset

### 1.1 创建

`Content/_My/GAS/AttackModules/` 下右键 → **Miscellaneous → Data Asset** → 选 **RPG_AttackModuleData**

先做徒手就够：命名 `DA_AttackModule_Unarmed`

### 1.2 基础配置

| 分类 | 字段 | 值 |
|---|---|---|
| `RPG\|Module` | Module Type | **Unarmed** |
| `RPG\|Module` | Module Tag | `Attack.Module.Unarmed` |
| `RPG\|Module` | Display Name | 徒手 |
| `RPG\|Trace` | Trace Source | **Hands** |
| `RPG\|Trace` | Trace Radius | `30` |
| `RPG\|Trace\|Hands` | Left Hand Socket | `hand_l` |
| `RPG\|Trace\|Hands` | Right Hand Socket | `hand_r` |

> 💡 **Socket 名怎么确认**：打开 `SKM_Manny_Simple` 的 Skeleton，在左侧骨骼树里搜 `hand`，
> Manny 的标准命名就是 `hand_l` / `hand_r`。

### 1.3 五段轻击

`Light Attacks` 数组点 `+` 五次，按下表填：

| 索引 | Damage Multiplier | Stamina Cost | Attack Tag | Montage |
|---|---|---|---|---|
| 0（第1段） | **1.0** | 8 | `Attack.Light.01` | **留空** |
| 1（第2段） | **1.15** | 8 | `Attack.Light.02` | **留空** |
| 2（第3段） | **1.4** | 8 | `Attack.Light.03` | **留空** |
| 3（第4段） | **1.6** | 8 | `Attack.Light.04` | **留空** |
| 4（第5段） | **2.0** | 10 | `Attack.Light.05` | **留空** |

> ⚠️ **蒙太奇留空是故意的**，不是漏配。留空时代码会走模拟时序分支，
> 让你先验证连段推进、倍率、耐力三条链路。动画做好后回来填上即可。

> ⚠️ `Attack.Light.01` 这些标签目前**还没在 C++ 里定义**。可以直接在标签选择器里
> 手动输入新建（GameplayTag 支持在资产里创建新标签）。它们只用于日志区分，不影响逻辑。

`Heavy Attack` 和 `Combo Transition` 批次 2 才用，先不配。

---

## 步骤 2 · GE_Damage（伤害 GE）

### 2.1 创建

`Content/_My/GAS/GE/` 右键 → **Gameplay → Gameplay Effect**，命名 `GE_Damage`

### 2.2 配置

| 字段 | 值 |
|---|---|
| Duration Policy | **Instant** |
| Executions | `+` 一条 → **RPG_DamageExecution** |

> ⚠️ **Modifiers 数组保持为空**。伤害不是通过 Modifier 算的 ——
> 它需要同时读攻防双方的属性并做非线性计算，必须走 Execution。
> 如果在这里加了 Modifier，会额外再算一遍，伤害翻倍。

### 2.3 可选：命中特效

`GameplayCues` 数组加一条，`GameplayCueTags` 填 `GameplayCue.Combat.Hit`。

需要先做 GC 资产（`GameplayCueNotify_Actor` 或 `_Static`），没有也不影响数值验证。

---

## 步骤 3 · GE_StaminaCost（耐力消耗）

### 3.1 创建

命名 `GE_StaminaCost`

### 3.2 配置

| 字段 | 值 |
|---|---|
| Duration Policy | **Instant** |
| Modifiers | 见下 |

**Modifiers 加一条：**

| 字段 | 值 |
|---|---|
| Attribute | `Stamina` |
| Modifier Op | **Additive** |
| Magnitude | **SetByCaller** |
| SetByCaller Tag | `Data.Stamina.Cost` |

> ⚠️ **不用做任何"取负"处理**。C++ 侧的 `ConsumeStamina()` 已经传的是负数
> （见 `RPG_GameplayAbilityBase.cpp` 里的注释说明）。Additive + 负值 = 扣减。

---

## 步骤 4 · GA_LightAttack 蓝图

### 4.1 创建

`Content/_My/GAS/Abilities/` 右键 → **Blueprint Class** → 展开 All Classes → 搜 `RPG_GA_LightAttack`

命名 `GA_LightAttack`

### 4.2 配置

| 分类 | 字段 | 值 |
|---|---|---|
| `RPG\|Effects` | Damage Effect Class | `GE_Damage` |
| `RPG\|Effects` | Stamina Cost Effect Class | `GE_StaminaCost` |
| `RPG\|Debug` | Simulated Segment Duration | `0.8` |
| `RPG\|Debug` | Simulated Combo Window Duration | `0.5` |

`RPG|Debug` 那两个只在没有蒙太奇时生效，用来调模拟连段的节奏。

---

## 步骤 5 · 挂到角色上

### 5.1 BP_RPG_Player

| 位置 | 字段 | 值 |
|---|---|---|
| `CombatComponent` → `RPG\|Combat` | Default Module | `DA_AttackModule_Unarmed` |
| 角色根 → `RPG\|Abilities` | Startup Abilities | 加一条：`Input.Attack.Light` → `GA_LightAttack` |

### 5.2 确认 `DA_RPG_InputConfig`

`Input.Attack.Light` 应该在 `Ability Input Mappings` 数组里映射到 `IA_RPG_Attack_Light`。
（阶段 1 就配过了，这里只是确认。）

---

## 步骤 6 · 测试（不需要动画）

PIE 里按左键，观察 **Output Log**（过滤 `LogRPG_Combat`）：

### 预期日志

```
[BP_RPG_Player_C_0] 轻击第 1 段（倍率 1.00，耐力 8.0）
[BP_RPG_Player_C_0] 消耗耐力 8.0
[BP_RPG_Player_C_0] 战斗中：连段索引 0 → 0
```

**连按左键**应该看到第 2、3、4、5 段依次打出，倍率递增到 2.00。

**停手 1 秒后再按** → 应该回到第 1 段（连段重置生效）。

### 验证伤害

在角色正前方放一个 `BP_RPG_Enemy`，攻击它应该看到：

```
[BP_RPG_Enemy_C_0] 受到 1.0 点伤害：100 → 99
```

数值算不对的话，看这两条日志定位：
- `伤害计算：攻击 10.0 × 倍率 1.00 = 10.0 | 防御 10.0 → 减伤 9.1% | 最终 9.1`
  → 这条是 Execution 打的，能看出攻防和减伤各是多少
- `[BP_RPG_Enemy_C_0] 受到 X 点伤害`
  → 这条是属性集打的，是最终落地值

### 用控制台命令查看状态

在 `RPGPrintAttributes` 之外，可以再加一条查看战斗状态（如果实现了的话）；
暂时可以直接看日志里的连段索引输出。

---

## 步骤 7 · 接动画（做好蒙太奇之后）

### 7.1 创建蒙太奇

1. 在 `Content/Characters/Mannequins/Anims/Unarmed/` 里找合适的攻击动画
   （比如 `MM_Attack_01` ~ `MM_Attack_05` 之类）
2. 右键动画序列 → **Create → Create AnimMontage**
3. 命名为 `AM_Player_LightAttack_01` … `_05`，放在 `Content/_My/Character/Player/Animations/`

### 7.2 在蒙太奇里加三个 Notify

打开蒙太奇，在时间轴上右键 → **Add Notify State / Notify**：

| Notify 类型 | 放置位置 | 作用 |
|---|---|---|
| **RPG 攻击判定窗口** | 刀/拳**即将接触到目标**到**挥过**之间 | 这段时间内的碰撞才产生伤害 |
| **RPG 连段衔接窗口** | 后摇的**前 1/3 ~ 1/2** 处，长约 0.3~0.5 秒 | 这段时间内按键才能接下一段 |
| **RPG 攻击结束** | 蒙太奇的**最后一帧** | GA 据此结束能力 |

**判定窗口**上的 `Attack Tag` 填对应的段标签（`Attack.Light.01` 等）。

> 💡 判定窗口的位置直接决定手感：
> 开太早 → "还没挥到就掉血"；开太晚 → "明明打中了没反应"。
> 建议先在动画预览里找到刀锋/拳头扫过角色正前方的帧，从那一帧开始开窗口。

### 7.3 填回攻击模组

回到 `DA_AttackModule_Unarmed`，把 5 个蒙太奇填进对应的 `Light Attacks[i].Montage`。

填上之后，模拟时序分支就不会再走了 —— 一切由真实的 Notify 事件驱动。

### 7.4 验证

PIE 里攻击应该能看到动画播放，伤害出现在判定窗口开启的那几帧。

如果动画播了但没伤害 → 判定窗口 Notify 没加或位置不对，看 `LogRPG_Animation` 的日志确认窗口有没有开。

---

## 常见问题排查

| 症状 | 最可能的原因 |
|---|---|
| 按左键完全没反应 | `Startup Abilities` 里没配 `Input.Attack.Light` → `GA_LightAttack` |
| 日志显示"角色上没有 CombatComponent" | 角色不是 `RPG_BaseCharacter` 子类 |
| 日志显示"没有配置攻击模组 DataAsset" | `BP_RPG_Player` 的 `CombatComponent.DefaultModule` 为空 |
| 伤害是 0 | `GE_Damage` 没挂 `RPG_DamageExecution`，或 GA 里没配 `Damage Effect Class` |
| 攻击反而**增加**耐力 | `GE_StaminaCost` 的 Modifier Op 配成了 Subtract，或 C++ 侧的负数被改动过 |
| 连段永远停在第 1 段 | 攻击模组的 `Light Attacks` 只填了 1 条 |
| 连段停不下来，一直连 | 衔接窗口 Notify 的时长过长，或蒙太奇没加"攻击结束"Notify |
| 一次攻击打出多段伤害 | 判定窗口拖得太长（去重只防同一目标重复，不防窗口过长） |

排查时先过滤 `LogRPG_Combat` 和 `LogRPG_Ability`，代码里的日志写了具体原因。

---

# 批次 2 · 重击蓄力 / 切手技 / 闪避

> 批次 1 验证通过后再做这部分。这三样共用批次 1 建立的基础设施
> （攻击模组、伤害 GE、事件监听），所以配置量不大。

## 步骤 8 · 重击（蓄力 + 切手技）

### 8.1 在 `DA_AttackModule_Unarmed` 里配 Heavy Attack

**`HeavyAttack` 分类：**

| 字段 | 值 |
|---|---|
| Charge Start Montage | （留空，动画做好后再填） |
| Charge Loop Montage | （留空） |
| Charge Stamina Drain Per Second | `10` |
| Charge Move Speed Scale | `0.3` |

**`HeavyAttack.Levels` 数组加 3 条：**

| 索引 | Required Charge Time | Release Montage | Damage Multiplier | Stamina Cost |
|---|---|---|---|---|
| 0（第1段） | `0.5` | 留空 | **3.0** | 15 |
| 1（第2段） | `1.0` | 留空 | **4.5** | 20 |
| 2（第3段） | `1.8` | 留空 | **6.5** | 25 |

> ⚠️ `Required Charge Time` 必须严格递增 —— 冲击模组的数据校验会检查这一点。
> 但 `GetChargeLevelForTime` 本身做了兼容处理（取满足条件的最高级），
> 所以即使配成乱序也不会算出错误结果，只是编辑器里会报错提示。

### 8.2 配切手技

**`Transition` 分类：**

| 字段 | 值 |
|---|---|
| Combo Transition Montage | （留空） |
| Combo Transition Multiplier | `2.5` |
| Combo Transition Stamina Cost | `15` |

### 8.3 创建 `GA_HeavyAttack` 蓝图

同 `GA_LightAttack` 的做法（`Content/_My/GAS/GA/`，父类选 `RPG_GA_HeavyAttack`），
配好 `Damage Effect Class` 和 `Stamina Cost Effect Class`。

### 8.4 挂到角色

`BP_RPG_Player` 的 `Startup Abilities` 加一条：
`Input.Attack.Heavy` → `GA_HeavyAttack`

---

## 步骤 9 · 闪避

### 9.1 创建 `GE_Invulnerable` ⚠️ 关键

这是无敌帧的载体，配错了闪避就没有无敌效果。

`Content/_My/GAS/GE/` → 右键 → Gameplay Effect，命名 `GE_Invulnerable`

| 字段 | 值 |
|---|---|
| Duration Policy | **Has Duration** |
| Duration Magnitude | `5.0`（秒） |

**然后加组件**：Details 面板 → **`Components` 数组** → `+` → 选 **`Target Tags`**

| 组件内字段 | 值 |
|---|---|
| Add Tags | **`State.Invulnerable`** |

> ⚠️ **必须用 `Target Tags` 组件**（`UTargetTagsGameplayEffectComponent`）。
> 老教程里说的 `GrantedTags` 内联字段在 UE 5.3 就被废弃了，5.8 的 Details
> 面板里根本找不到它。
>
> 持续时间设 5 秒只是"上限"—— GA 会在无敌帧结束时**主动移除**这个 GE，
> 所以实际无敌时长由动画上的 Notify 决定，不是这 5 秒。

### 9.2 创建 `GA_Dodge` 蓝图

| 分类 | 字段 | 值 |
|---|---|---|
| `RPG\|Dodge` | Invulnerability Effect Class | **`GE_Invulnerable`** |
| `RPG\|Dodge` | Dodge Montage | （留空，动画做好后再填） |
| `RPG\|Dodge` | Stamina Cost | `20` |
| `RPG\|Dodge` | Dodge Impulse | `1200` |
| `RPG\|Debug` | Simulated Dodge Duration | `0.7` |
| `RPG\|Debug` | Simulated Invulnerability Ratio | `0.6` |

### 9.3 挂到角色

`Startup Abilities` 加一条：`Input.Dodge` → `GA_Dodge`

---

## 步骤 10 · 测试批次 2

### 重击蓄力

**按住右键不放**，日志应该逐级升段：

```
开始蓄力（最长 3.0 秒，每秒耗耐力 10.0）
蓄力升到 1 段（0.50 秒）
蓄力升到 2 段（1.00 秒）
蓄力升到 3 段（1.80 秒）
```

**松手**：

```
松手，按 2.10 秒的蓄力释放
释放蓄力重击（3 段，倍率 6.50，耐力 25.0）
```

**按一下立刻松**（不足 0.5 秒）→ 按第 1 段释放，倍率 3.0。

**按住不放超过 3 秒** → 自动释放。
**蓄力期间耐力耗尽** → 强制释放。

### 切手技

先左键打轻击，**在轻击动画播放期间**按右键：

```
轻击第 1 段（倍率 1.00，耐力 8.0）
切手技（倍率 2.50，耐力 15.0）
```

> 切手技不蓄力 —— 它是"变招"，按下去立刻出招。

### 闪避

```
闪避（耐力 20.0，冲量 1200）
无敌帧开始
无敌帧结束
```

**验证无敌是否真的生效**：让敌人打你（或者手动用 `GE_Damage` 打自己），
在无敌帧窗口内应该看到：

```
[BP_RPG_Player_C_0] 伤害被无敌帧挡下：9.1
```

这条日志来自 `URPG_AttributeSet::PostGameplayEffectExecute` 的兜底检查。

---

## 批次 2 常见问题排查

| 症状 | 最可能的原因 |
|---|---|
| 按右键永远是蓄力，切手技不触发 | 轻击 GA 的 `Activation Owned Tags` 里没有 `Ability.Attack.Light`（C++ 里已加，检查蓝图有没有覆盖成空） |
| 蓄力不升段 | `HeavyAttack.Levels` 的 `Required Charge Time` 没配，或全是 0 |
| 松手没反应 | 输入释放链路断了 —— 看有没有 `输入释放：Input.Attack.Heavy` 日志；没有的话是 IMC 里右键的映射没配 `Completed` 触发 |
| 一直蓄力不释放 | `Max Charge Time` 配得过大；或耐力没在掉（`Charge Stamina Drain Per Second` 为 0） |
| 闪避没有无敌 | `GE_Invulnerable` 没加 `Target Tags` 组件，或 GA 里没配 `Invulnerability Effect Class` |
| 闪避后一直无敌 | 无敌 GE 没被移除 —— 检查是否有别的途径给了无敌标签 |
| 切手技把轻击打断后角色定住 | `GA_HeavyAttack` 的 `Transition Montage` 配了但 GA 没收到 `Attack End` 通知 |
