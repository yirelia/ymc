// 修饰与绑定（S3 实例化）——对标 OMC NFFrontEnd/NFModifier.mo + NFBinding.mo。
//
// 设计（ADR-006 D3、研究底稿 R15 §对 ymc 建议）：
//   - `Modifier` 存进独立 `ModifierArena`，对外传 `ModId = Id<Modifier>`；`Nomod` 用**固定哨兵 Id 0**
//     （arena 构造时预置），省一次分配、便于短路。
//   - 子修饰用**按 name 排序的小 vector**（不用 map/AVL）：实测个位数规模、缓存友好，且要主动选定遍历序
//     以复刻 OMC 的字典序（L1 文本对拍，R15）。
//   - **合并产生新节点、绝不原地改**：`merge(outer,inner)` 在 arena append 新节点，未变子树复用旧 Id（结构共享）。
//     同一修饰会传播到数组每元素、下传多层类、被 InstNode 克隆共享——任何原地写都污染共享者（R15 硬约束）。
//   - **作用域记在 binding 上**（修饰右边的表达式"在写下它的那个类里解析"，R12 §2.4）；不建全局修饰环境。
//
// v1 竖切片（ADR-006 D6）：标量组件 + extends 子句修饰 + 内建属性修饰 + 无维度短类修饰。
// Redeclare、each 数组切分（SPLIT_PROXY）先给字段占位，逻辑等 redeclare/数组竖切片再长肉。
#pragma once
#include <cstdint>
#include <vector>

#include "base/arena.h"
#include "base/loc.h"
#include "base/symbol.h"
#include "inst/expr.h"

namespace ymc {

struct Exp;       // 前置声明：Absyn 表达式（S1/S2）。raw 绑定持 Id<Exp>，instBinding 才转成类型化 Expr
struct INode;     // 前置声明：绑定作用域是一个实例节点
struct Modifier;  // 前置声明：SubMod 持 ModId = Id<Modifier>（Id 只是 u32，声明即可用）

using ModId = Id<Modifier>;

// ---- 绑定（对标 NFBinding.mo 的 Binding）----
// 状态机（R15 §4）：Unbound → Raw（Absyn 表达式 + 作用域）→ Typed（类型化 Expr）。
// v1 只走 Unbound/Raw/Typed；FLAT/CEVAL 等后续状态先并进 Typed。
enum class BindingKind : std::uint8_t { Unbound, Raw, Typed };

struct Binding {
  BindingKind kind = BindingKind::Unbound;
  // Raw：源表达式 + **写下这条绑定的类实例**（表达式在此作用域解析，R12 §2.4）。
  Id<Exp> raw_exp;
  Id<INode> scope;
  // Typed：instBinding + typeBinding 后的类型化表达式。
  Id<Expr> typed_exp;
  Variability variability = Variability::Continuous;  // 定型后回填
  bool is_each = false;                                // `each` 前缀（数组修饰切分用；v1 无数组）
  SourceLoc loc;

  [[nodiscard]] bool is_bound() const noexcept { return kind != BindingKind::Unbound; }
  static Binding unbound() { return Binding{}; }
};

// ---- 子修饰：(名字, 子修饰 Id) ----
// subs 保持**按 name 的字符串字典序**升序（不是 Symbol id 序——id 是 intern 顺序，复刻不了 OMC 的 AVL
// 字典序，R15：多个不存在元素时 OMC 报字典序最小者）。排序/合并/查找都要比字符串，因此这些操作在修饰
// 合并 pass 里做（那里有 `Interner`），本文件的 arena 只存储、不比较。
struct SubMod {
  Symbol name;
  ModId mod;
};

// ---- 修饰（对标 NFModifier.mo 的 Modifier）----
// 三态：Nomod（哨兵 Id 0）/ Mod（普通修饰）/ Redeclare（v1 不用，占位）。
enum class ModKind : std::uint8_t { Nomod, Mod, Redeclare };

struct Modifier {
  ModKind kind = ModKind::Nomod;
  bool is_final = false;
  bool is_each = false;
  Symbol name;                    // 被修饰的名字（子修饰里才有意义；顶层类修饰名为空）
  Binding binding;                // 顶层绑定（`= expr`），无则 Unbound
  std::vector<SubMod> subs;       // 子修饰，按 name 升序；结构共享（未变的子树复用旧 ModId）
  SourceLoc loc;
  // Redeclare（v1 占位）：被 redeclare 的元素 + 三份修饰（对标 REDECLARE 的 innerMod/outerMod/ccMod）。
  Id<INode> redecl_elem;
  ModId redecl_inner_mod, redecl_outer_mod, redecl_cc_mod;

  [[nodiscard]] bool is_nomod() const noexcept { return kind == ModKind::Nomod; }
};

// ---- 修饰竞技场 ----
// Id 0 恒为 Nomod：构造时预置，`nomod()` 直接返回它。查子修饰用二分（subs 有序）。
class ModifierArena {
 public:
  ModifierArena() {
    Id<Modifier> id = mods_.add(Modifier{});  // kind 默认 Nomod
    YMC_ASSERT(id.raw == 0, "Nomod 必须是 arena 的第 0 个元素");
    (void)id;
  }

  [[nodiscard]] static ModId nomod() noexcept { return ModId::from_index(0); }

  [[nodiscard]] ModId add(Modifier m) { return mods_.add(std::move(m)); }
  [[nodiscard]] Modifier& operator[](ModId id) { return mods_[id]; }
  [[nodiscard]] const Modifier& operator[](ModId id) const { return mods_[id]; }
  [[nodiscard]] std::size_t size() const noexcept { return mods_.size(); }

 private:
  Arena<Modifier> mods_;
};

}  // namespace ymc
