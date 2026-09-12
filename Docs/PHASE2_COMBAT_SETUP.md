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
