// 实例化 pass（S3）——对标 OMC NFFrontEnd/NFInst.mo 的 makeTopNode / partialInstClass / expandClass / instClass。
//
// 三趟里的第一趟（R11 §1 要点、§3）：instantiate = **只建树 + 解析 extends + 递归组件**，**不碰表达式**
//（绑定/维度/方程的 cref 解析留给后续 instExpressions）。本文件实现到 instClass 为止；
// instExpressions/typeClass/flatten 是后续 pass。
//
// v1 竖切片第一里程碑（ADR-006 D6）：单类 + 标量内建组件（Real/Integer/Boolean/String）+ 单继承 extends +
// 复合组件（组件类型是用户类）。**尚未做**：修饰合并、绑定/维度实例化、短类、内建属性、inner/outer、
// redeclare、数组——按落地顺序在后续子步补，每步配探针对拍。
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>

#include "base/diag.h"
#include "inst/inode.h"

namespace ymc {

class Instantiator {
 public:
  Instantiator(Inst& inst, Diagnostics& diags) : I_(inst), diags_(diags) {}

  // 建 <top>：内建类型节点 + 顶层类节点（NotInstantiated），登记进 top 的 ClassTree。
  void make_top_node();

  // 实例化一个顶层类（按名字），返回其实例节点（root）；找不到/不可实例化返回 invalid 并报错。
  // 对标 lookupRootClass + instantiateRootClass。
  NodeId instantiate_root(Symbol name);
  // 按全限定名实例化根类（`Modelica.Blocks.Examples.PID_Controller`，库里的示例模型）：从顶层逐段查。
  NodeId instantiate_root_path(const std::vector<Symbol>& path);

  // v1 还没做的构造（内建枚举/时钟类型、extends 内建类型…）在实例化阶段就撞上时标记，
  // driver 据此退 66（记 SKIP，不是 FAIL）。与 flatten 的 unsupported 合并判断。
  [[nodiscard]] bool unsupported() const noexcept { return unsupported_; }
  [[nodiscard]] const std::string& unsupported_reason() const noexcept { return unsupported_reason_; }
  // 子原因（查不到的类名等），driver 打在 `[…]` 里，供 nf_live_sweep 排桶；主原因不变。
  [[nodiscard]] const std::string& unsupported_detail() const noexcept { return unsupported_detail_; }

 private:
  // ---- 三趟 ----
  void partial_inst(NodeId cls);                 // 建 ClassTree（本地类/组件/extends 占位）
  void expand(NodeId cls);                        // 解析 extends、递归 expand 基类
  NodeId instantiate(NodeId def_node, NodeId parent);  // 克隆 def 节点 + instClass 填实例树
  void inst_class(NodeId inst_node, NodeId def_node);  // 建 FlatTree：递归组件 + extends 就地展开
  void inst_component(NodeId comp_node, NodeId enclosing);  // 解析组件类型（叶/复合）
  // extends 修饰 `extends A(x = 1)`：把 mod 按名合并到继承来的组件 bc 上（绑定覆盖 + 属性 + final）。
  void apply_extends_modifier(NodeId bc, Id<SMod> mod, NodeId scope);
  // 长类型只有一个 extends（`type TypeReal extends Real; end TypeReal;`）：返回那个 extends 元素；否则 invalid。
  Id<SElement> type_extends_only(Id<SElement> ce) const;
  // extends 修饰里的组件重声明（见 inst.cpp 的说明）。scope = 写 extends 的类的实例。
  void redeclare_component(NodeId bc, Id<SElement> rel, NodeId scope);

  // ---- 查找（最小版，只够 v1：内建类型 + 本类/嵌套类 + 顶层）----
  // path_nodes 非空：顺路记下路径各段解析到的类节点（基类名的 replaceable 检查用）。
  NodeId lookup_class_name(const std::vector<Symbol>& path, NodeId scope, std::vector<NodeId>* path_nodes = nullptr);
  NodeId lookup_simple_class(Symbol name, NodeId scope);
  // 类节点 cls 自己的 import 里叫 name 的类（见 inst.cpp 的规则说明）；没有 → empty。
  NodeId import_class(Symbol name, NodeId cls);

  // ---- 内建 ----
  void make_builtin_types();
  NodeId builtin_node(TypeKind k) const;

  void mark_unsupported(const char* why, std::string detail = {}) {
    if (!unsupported_) { unsupported_ = true; unsupported_reason_ = why; unsupported_detail_ = std::move(detail); }
  }
  std::string path_str(const std::vector<Symbol>& path) const;   // `A.B.C`（报 SKIP 子原因用）

  Inst& I_;
  Diagnostics& diags_;
  NodeId builtin_[4];   // Real/Integer/Boolean/String（按 TypeKind 序）
  std::vector<NodeId> builtin_enums_;   // 顶层合成的内建枚举类（StateSelect / AssertionLevel），见 make_top_node
  bool unsupported_ = false;
  std::string unsupported_reason_;
  std::string unsupported_detail_;
  int depth_ = 0;   // instantiate 递归深度（防无限递归栈溢出）
  // redeclare_component 合成的组件元素：(原声明 << 32 | 重声明元素) → 合成元素；elems 的预留上限（见 make_top_node）。
  std::unordered_map<std::uint64_t, Id<SElement>> redecl_elems_;
  std::size_t synth_cap_ = 0;
};

}  // namespace ymc
