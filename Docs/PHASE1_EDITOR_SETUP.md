# 阶段 1 · 编辑器操作清单

> 代码部分已完成并编译通过。以下资产**必须在编辑器里创建**（`.uasset` 是二进制格式，我无法直接生成）。
> 按顺序做完，就能在 PIE 里跑起来并看到 ASC 属性。

---

## 目录

- [步骤 1 · 输入动作 InputAction（8 个）](#步骤-1--输入动作-inputaction8-个)
- [步骤 2 · 输入映射上下文 IMC](#步骤-2--输入映射上下文-imc)
- [步骤 3 · 输入配置资产 DA_RPG_InputConfig](#步骤-3--输入配置资产-da_rpg_inputconfig)
- [步骤 4 · 初始属性 GE](#步骤-4--初始属性-ge)
- [步骤 5 · 蓝图（4 个）](#步骤-5--蓝图4-个)
- [步骤 6 · 测试关卡](#步骤-6--测试关卡)
- [步骤 7 · 验收](#步骤-7--验收)
- [常见问题排查](#常见问题排查)

---

## 步骤 1 · 输入动作 InputAction（8 个）

**创建位置**：`Content/RPG/Input/Actions/`
右键 → Input → Input Action

每个资产的配置：

| 资产名 | Value Type | 说明 |
|---|---|---|
| `IA_RPG_Move` | **Axis2D (Vector2D)** | 移动，二维向量 |
| `IA_RPG_Look` | **Axis2D (Vector2D)** | 视角 |
| `IA_RPG_Jump` | Digital (bool) | 跳跃 |
| `IA_RPG_Crouch` | Digital (bool) | 蹲伏 |
| `IA_RPG_Sprint` | Digital (bool) | 奔跑（按住） |
| `IA_RPG_Attack_Light` | Digital (bool) | 轻击（5 段连段） |
| `IA_RPG_Attack_Heavy` | Digital (bool) | 重击（蓄力 + 切手技） |
| `IA_RPG_Dodge` | Digital (bool) | 闪避 |

> 💡 **Value Type 只在 Move / Look 上需要改**，其余保持默认的 Digital。
> 这个类型决定了代码里 `Value.Get<FVector2D>()` 能不能取到值——类型不匹配会在运行时
> 静默返回零向量，表现为"按键没反应"，很难查。

---

## 步骤 2 · 输入映射上下文 IMC

**创建位置**：`Content/RPG/Input/`
右键 → Input → Input Mapping Context，命名 `IMC_RPG_Default`

在这个 IMC 里添加 8 条映射（点 `+` 号，Mappings 数组）：

| Input Action | 按键 | 需要的 Modifier | 说明 |
|---|---|---|---|
| `IA_RPG_Move` | `W` | Swizzle Input Axis Values (YXZ) | 把 X 轴的值挪到 Y 轴 |
| `IA_RPG_Move` | `S` | Swizzle Input Axis Values (YXZ) + **Negate** | 后退取反 |
| `IA_RPG_Move` | `A` | **Negate** | 左移取反 |
| `IA_RPG_Move` | `D` | （无） | 右移 |
| `IA_RPG_Look` | `Mouse XY 2D-Axis` | **Negate**（只勾 Y 轴） | 鼠标上推视角上抬 |
| `IA_RPG_Jump` | `Space Bar` | （无） | |
| `IA_RPG_Crouch` | `Left Ctrl` | （无） | |
| `IA_RPG_Sprint` | `Left Shift` | （无） | 按住型 |
| `IA_RPG_Attack_Light` | `Left Mouse Button` | （无） | |
| `IA_RPG_Attack_Heavy` | `Right Mouse Button` | （无） | |
| `IA_RPG_Dodge` | `Left Alt` | （无） | |

> 💡 **为什么 Move 需要 Swizzle？**
> 键盘的 `W` 触发时产生的是 1D 值，默认落在 **X 轴**上。但我们的代码约定
> `MovementVector.X = 左右、Y = 前后`（和手柄摇杆一致）。所以要用
> `Swizzle Input Axis Values (YXZ)` 把它从 X 挪到 Y。`S` 键还要再加 `Negate`
> 变成负值。这是增强输入里最容易配错、也最难查的一环。
>
> ⚠️ `Negate` 默认会把三个轴全部取反。用鼠标做 Look 时，**必须点开 Negate 的
> Details，把 X / Y / Z 的勾选改成只勾 Y** —— 否则左右移动鼠标的视角方向也会反。

---

## 步骤 3 · 输入配置资产 DA_RPG_InputConfig

**创建位置**：`Content/RPG/Input/`
右键 → Miscellaneous → Data Asset → 选择 **RPG_InputConfig**

命名 `DA_RPG_InputConfig`，然后填：

**基础：**
- `Default Mapping Context` = `IMC_RPG_Default`
- `Mapping Context Priority` = `0`

**Locomotion（直接驱动移动，不走 GAS）：**
- `Move Action` = `IA_RPG_Move`
- `Look Action` = `IA_RPG_Look`
- `Crouch Action` = `IA_RPG_Crouch`

**Ability Input Mappings（走 GAS，点 `+` 加 5 条）：**

| Input Action | Input Tag | Description |
|---|---|---|
| `IA_RPG_Attack_Light` | `Input.Attack.Light` | 轻击 |
| `IA_RPG_Attack_Heavy` | `Input.Attack.Heavy` | 重击 |
| `IA_RPG_Dodge` | `Input.Dodge` | 闪避 |
| `IA_RPG_Jump` | `Input.Jump` | 跳跃 |
| `IA_RPG_Sprint` | `Input.Sprint` | 奔跑 |

> ⚠️ `Input Tag` 字段有标签选择器，直接搜 `Input.` 就能看到我们在 C++ 里注册的标签。
> **如果搜不到任何 `Input.*` 标签**，说明 GameplayTags 没注册成功——重新编译一次即可，
> 这些标签是编译期注册的（见 `Source/RPG/Core/RPG_GameplayTags.cpp`）。

---

## 步骤 4 · 初始属性 GE

**创建位置**：`Content/RPG/AbilitySystem/Effects/`
右键 → Gameplay → Gameplay Effect，命名 `GE_InitAttributes`

**基础设置：**
- `Duration Policy` = **Instant**
- `Components` 数组 → `+` → 选 **Target Tags** 来授予 `State.Combat.InCombat`？→ **不需要**，保持简单

**Modifiers（点 `+` 加 8 条）：**

| Attribute | Modifier Op | Magnitude |
|---|---|---|
| `MaxHealth` | **Override** | 100 |
| `Health` | **Override** | 100 |
| `Attack` | **Override** | 10 |
| `Defense` | **Override** | 10 |
| `MaxMana` | **Override** | 100 |
| `Mana` | **Override** | 100 |
| `MaxStamina` | **Override** | 100 |
| `Stamina` | **Override** | 100 |

> 💡 **为什么用 Override 而不是 Additive？**
> `URPG_AttributeSet` 的构造函数已经给了一套保底默认值（100/10/100/100）。
> 如果用 `Additive`，GE 会在默认值基础上**再加**一遍，结果生命变成 200 —— 这类
> "数值翻倍"的 bug 很隐蔽。用 `Override` 直接设定，语义清晰：构造函数只负责
> "不让程序崩"，GE 负责"真正的数值"。
>
> 数值改起来不用重新编译，这就是把初始化放 GE 而不是 C++ 的意义。

**保存后**记得点工具栏的 **Compile**。

---

## 步骤 5 · 蓝图（4 个）

### 5.1 `BP_RPG_Player`
**位置**：`Content/RPG/Character/Player/`
右键 → Blueprint Class → 展开 All Classes → 搜 `RPG_Player`

| 类别 | 属性 | 值 |
|---|---|---|
| RPG\|Abilities | `Init Attributes Effect` | `GE_InitAttributes` |
| RPG\|Abilities | `Startup Abilities` | 见下表 |

`Startup Abilities` 点 `+` 加 5 条（键 = Input Tag，值 = 能力类）：

| Key | Value |
|---|---|
| `Input.Attack.Light` | `GA_LightAttack`（阶段 2 才有，先跳过） |
| `Input.Jump` | （阶段 2） |
| ... | ... |

> 📌 **阶段 1 暂时留空即可**——能力类是阶段 2 的产物。留空的话，按攻击键不会有反应，
> 但移动、跳跃、视角、属性打印都能正常工作，足够验收阶段 1。

**然后给角色配上骨骼网格体**（否则你看不到人物，只有一个空胶囊）：
- 选中 `Mesh` 组件 → `Skeletal Mesh` = `SKM_Manny`（在 `Content/Characters/Mannequins/Meshes/`）
- `Anim Class` 先留空（阶段 5 才做动画蓝图）

### 5.2 `BP_RPG_PlayerController`
**位置**：`Content/RPG/Character/Player/`
父类选 `RPG_PlayerController`

- `RPG|Input` → `Input Config` = `DA_RPG_InputConfig`

### 5.3 `BP_RPG_GameModeBase`
**位置**：`Content/RPG/Core/`
父类选 `RPG_GameModeBase`

- `Default Pawn Class` = `BP_RPG_Player`
- `Player Controller Class` = `BP_RPG_PlayerController`
- `Player State Class` = `RPG_PlayerState`（**这一项千万别漏**，见下方说明）

> ⚠️ **`Player State Class` 必须指向 `RPG_PlayerState`（或它的蓝图子类）**。
> 因为玩家的 ASC 就挂在这个类上。如果这里还是引擎默认的 `PlayerState`，
> `GetASCInternal()` 会一直返回 `nullptr`，症状是**技能全部无效但没有任何报错**——
> 这是本项目最隐蔽的一个配置陷阱。

### 5.4 `BP_RPG_Enemy`（可选，阶段 1 用不上）
**位置**：`Content/RPG/Character/Enemy/`
父类选 `RPG_Enemy`，同样配上 `SKM_Manny` 和 `GE_InitAttributes`。

---

## 步骤 6 · 测试关卡

**创建位置**：`Content/RPG/Maps/`
File → New Level → 选 **Basic**（或 Empty Level），保存为 `L_RPG_TestArena`

> ⚠️ **路径和名字必须完全一致**。`Config/DefaultEngine.ini` 里已经把默认地图
> 指向 `/Game/RPG/Maps/L_RPG_TestArena`，名字对不上编辑器会提示找不到地图。

关卡里需要放：

| Actor | 位置 | 说明 |
|---|---|---|
| `Player Start` | 地面上方 | 玩家出生点，**必须有**，否则会从 0,0,0 掉落 |
| `Floor` / 任意静态网格体 | 铺一块地面 | 用 `Content/LevelPrototyping` 里的白盒方块，或直接拖一个 Cube 拉大 |
| `Nav Mesh Bounds Volume` | 覆盖地面 | **阶段 4 的 AI 要用**，现在放着不影响 |
| `BP_RPG_Enemy`（可选） | 地面上 | 用来看属性集是否生效 |

**最后一步**：World Settings → `GameMode Override` = `BP_RPG_GameModeBase`
（或者用 ini 里已配好的 C++ 版 `RPG_GameModeBase`，但那样就没有蓝图里的角色配置了）

---

## 步骤 7 · 验收

按 **Play** 进入 PIE，逐项确认：

| # | 操作 | 预期结果 |
|---|---|---|
| 1 | 移动鼠标 | 视角转动，弹簧臂跟随 |
| 2 | 按 `W` / `A` / `S` / `D` | 角色相对镜头方向移动，身体自动转向移动方向 |
| 3 | 按 `Space` | 角色跳跃（阶段 1 还是原生跳跃，阶段 2 会改成消耗耐力的 GA） |
| 4 | 按 `Left Shift` | 移动速度明显变快 |
| 5 | 按 `Left Ctrl` | 角色蹲下，再按起立 |
| 6 | 按 `~` 打开控制台，输入 `RPGPrintAttributes` | 屏幕左上角显示 8 项属性，生命 100/100、攻击 10、防御 10 |
| 7 | 控制台输入 `RPGPrintTags` | 显示当前拥有的标签（阶段 1 应该只有 ASC 自带的基础标签） |

**第 6 步能打出属性 = 阶段 1 成功**。那意味着整条链路都通了：

```
GameMode 指定 PlayerState 类
  → PlayerState 创建 ASC + AttributeSet 并登记
  → Player 被占有 → InitAbilityActorInfo(Owner=PlayerState, Avatar=Player)
  → 应用 GE_InitAttributes
  → 通过接口从任意位置都能取到属性集
```

---

## 常见问题排查

| 症状 | 原因 | 修法 |
|---|---|---|
| 完全无法操作 | `InputConfig` 没配，或 `DA_RPG_InputConfig` 是空的 | 看 Output Log 里 `LogRPG` 的 Error，代码里已经写了具体缺什么 |
| 角色能跳但不会动 | `IA_RPG_Move` 的 Value Type 不是 Axis2D，或 IMC 里缺 Swizzle 修饰符 | 回步骤 1、2 检查 |
| 鼠标上下动，视角左右转 | `Look` 的 Negate 修饰符勾选了错误的轴 | Negate 的 Details 里只勾 Y |
| `RPGPrintAttributes` 提示拿不到属性集 | `Player State Class` 没设成 `RPG_PlayerState` | 回步骤 5.3 |
| 属性全是 0 或 100（不是 GE 里的值） | `Init Attributes Effect` 没配 | 回步骤 5.1 |
| 输入标签选择器里搜不到 `Input.*` | 标签未注册（未重新编译） | 重新编译一次 C++ |
| 启动报找不到地图 | 关卡路径/名字不对 | 必须是 `Content/RPG/Maps/L_RPG_TestArena` |

排查时先看 **Output Log** 过滤 `LogRPG`，代码里的报错信息都写了具体原因和修法。
