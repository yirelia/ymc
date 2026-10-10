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
#include <unordered_set>

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
  void check_ext_object(NodeId cls);              // 外部对象类的元素校验（见 inst.cpp）
  void dedup_components(std::vector<NodeId>& comps, NodeId def_node);   // 继承来的同名组件去重（见 inst.cpp）
  bool binding_exp_equal(Id<Exp> a, Id<Exp> b) const;
  void collect_class_names(NodeId def_node, std::unordered_set<std::uint32_t>& out, int depth);
  // 类重声明集：名字 → 顶替进来的类节点（外层在前；见 inst.cpp 的 inst_class）。
  using ClassRedecls = std::vector<std::pair<Symbol, NodeId>>;
  // 克隆 def 节点 + instClass 填实例树；redecls 非空：实例的类树里这些名字换成新类（基类实例看到派生类的元素级类重声明）。
  NodeId instantiate(NodeId def_node, NodeId parent, const ClassRedecls* redecls = nullptr);
  NodeId instantiate_root_class(NodeId def_node);      // 根类：短类别名顺链到长类、补生成的 inner（见 inst.cpp）
  void inst_class(NodeId inst_node, NodeId def_node, const ClassRedecls* redecls = nullptr);  // 建 FlatTree：递归组件 + extends 就地展开
  bool mentions_any(Id<SElement> cls, const ClassRedecls& r, int depth) const;   // 类体里写到 r 的名字（见 inst_component）
  // 修饰里的类重声明（`redeclare model B = X(p = 4)`）：元素的类节点（词法作用域 scope = 写修饰的类，按元素缓存）；修饰 m 里的
  // 这类子修饰追加进 r（已有的名字不覆盖 = 外层优先），base 里没有这个类 → SKIP。见 inst.cpp。
  NodeId redecl_class_node(Id<SElement> rel, NodeId scope);
  bool class_redeclare_ok(NodeId orig, NodeId repl);   // 顶替的合法性（枚举子类型、outer 类，见 inst.cpp）
  void collect_class_redecls(Id<SMod> m, NodeId scope, NodeId base, ClassRedecls& r);
  bool nested_in(NodeId cls, NodeId inst) const;   // cls 的词法外层链经过 inst 的类或它的各层基类
  // 解析组件类型（叶/复合）。generated_inner：为找不到 inner 的 outer 生成的 inner（见 insert_generated_inners）——照 inner 实例化。
  void inst_component(NodeId comp_node, NodeId enclosing, bool generated_inner = false);
  void insert_generated_inners(NodeId root);   // 找不到 inner 的 outer：在根类末尾生成同名 inner（见 inst.cpp）
  // extends 修饰 `extends A(x = 1)`：把 mod 按名合并到继承来的组件 bc 上（绑定覆盖 + 属性 + final）。
  void apply_extends_modifier(NodeId bc, Id<SMod> mod, NodeId scope);
  // 长类型只有一个 extends（`type TypeReal extends Real; end TypeReal;`）：返回那个 extends 元素；否则 invalid。
  Id<SElement> type_extends_only(Id<SElement> ce) const;
  // extends 修饰里的组件重声明（见 inst.cpp 的说明）。scope = 写 extends 的类的实例。
  void redeclare_component(NodeId bc, Id<SElement> rel, NodeId scope);
  Id<SElement> merge_redeclared_elem(NodeId bc, Id<SElement> od, Id<SElement> rel, NodeId scope);
  void redeclare_local_components(ClassTree& flat, NodeId inst_node);   // 元素级组件重声明（见 inst.cpp）

  // ---- 查找（最小版，只够 v1：内建类型 + 本类/嵌套类 + 顶层）----
  // path_nodes 非空：顺路记下路径各段解析到的类节点（基类名的 replaceable 检查用）。
  NodeId lookup_class_name(const std::vector<Symbol>& path, NodeId scope, std::vector<NodeId>* path_nodes = nullptr);
  NodeId lookup_simple_class(Symbol name, NodeId scope);
  // 类节点 cls 自己的 import 里叫 name 的类（见 inst.cpp 的规则说明）；没有 → empty。
  NodeId import_class(Symbol name, NodeId cls);
  NodeId tree_class(ClassTreeId tid, Symbol name);   // 类树里（本地 + 继承来的）名为 name 的类；没有 → empty
  bool ext_mod_hits_consts(NodeId base, Id<SMod> m);  // extends 修饰改到基类的常量 / 类（见 expand）
  Id<SElement> member_def(NodeId cls, Symbol name, int depth);   // 类自己的或继承来的元素声明

  // ---- 内建 ----
  void make_builtin_types();
  NodeId builtin_node(TypeKind k) const;

  void mark_unsupported(const char* why, std::string detail = {}) {
    if (!unsupported_) { unsupported_ = true; unsupported_reason_ = why; unsupported_detail_ = std::move(detail); }
  }
  std::string path_str(const std::vector<Symbol>& path) const;   // `A.B.C`（报 SKIP 子原因用）

  Inst& I_;
  Diagnostics& diags_;
  NodeId builtin_[5];   // Real/Integer/Boolean/String/Clock（同 kBuiltinKinds 序）
  std::vector<NodeId> builtin_enums_;   // 顶层合成的内建枚举类（StateSelect / AssertionLevel），见 make_top_node
  bool unsupported_ = false;
  std::string unsupported_reason_;
  std::string unsupported_detail_;
  int depth_ = 0;   // instantiate 递归深度（防无限递归栈溢出）
  // redeclare_component 合成的组件元素：(原声明 << 32 | 重声明元素) → 合成元素；elems 的预留上限（见 make_top_node）。
  std::unordered_map<std::uint64_t, Id<SElement>> redecl_elems_;
  std::unordered_map<std::uint32_t, ClassRedecls> inst_redecls_;   // 实例节点 → 施加在它类树上的类重声明集
  std::unordered_map<std::uint32_t, NodeId> redecl_class_nodes_;   // 修饰里的类重声明元素 → 它的类节点
  // class extends 类合成的隐式 extends 元素（class extends 的 SCode 元素 raw → 合成的 Extends 元素）与合成元素的集合（见 partial_inst）。
  std::unordered_map<std::uint32_t, Id<SElement>> ce_ext_elem_;
  std::unordered_set<std::uint32_t> ce_ext_elems_;
  std::size_t synth_cap_ = 0;
};

}  // namespace ymc
