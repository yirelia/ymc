// 实例树数据结构（S3）——对标 OMC NFFrontEnd/NFInstNode.mo + NFClass.mo + NFClassTree.mo + NFComponent.mo。
//
// 四层嵌套（R11 §2）：INode（身份 + 父指针）→ Class/Component（可变单元，多节点共享）→ Class 持 ClassTree
// → ClassTree 持子 INode 数组。可变性用 arena 格模拟 OMC 的 Pointer/Mutable（R11 §7.0、ADR-006 D1）：
//   - `Pointer<Class>` = `ClassId`：改 `classes[id]` = update（共享者同步）；新分配一格 = create（克隆、断共享）。
//   - `Pointer<Component>` = `CompId` 同理。
//   - `Mutable<InstNode>`（ClassTree 数组槽的间接层，redeclare/去重用）→ SlotId：**v1 暂不引入**（单继承链、
//     无 redeclare、无菱形，不需要多视图共享；R11 §7.0/§7.4 的告警在 redeclare/数组竖切片时再补，见 ClassTree 注）。
//
// 双父链（ADR-006 D2、R12 §对 ymc 建议）：`parent_scope`（词法外层，查名向外走）与 `instance_parent`
// （实例树父亲，resolveInner/压平前缀）**分开存**，不像 OMC 那样复用一个 parentScope 字段来回改写——
// 避免"取词法外层必须在克隆前做"的坑（R11 §7.1 不变量 2）。
//
// v1 竖切片（ADR-006 D6）：标量组件 + 单继承链 + 修饰合并 + 内建类型属性 + inner/outer + 无维度短类。
#pragma once
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "absyn/absyn.h"
#include "base/arena.h"
#include "base/loc.h"
#include "base/symbol.h"
#include "inst/expr.h"
#include "inst/modifier.h"
#include "inst/type.h"
#include "scode/scode.h"

namespace ymc {

struct INode;
struct Class;
struct ClassTree;
struct Component;
using NodeId = Id<INode>;
using ClassId = Id<Class>;
using ClassTreeId = Id<ClassTree>;
using CompId = Id<Component>;

enum class Visibility : std::uint8_t { Public, Protected };

// ---- InstNodeType：打在 INode 上的 tagged nodetype（R11 §2.2）----
// TopScope 的 generated_inners / annotation_scope 只有一个节点用，放在 `Inst` 容器上（不进每个 InstNodeType）。
enum class NodeTypeKind : std::uint8_t {
  NormalClass,      // 默认
  BaseClass,        // extends 拿来的基类节点：parent=派生类/实例，def=那条 EXTENDS，restore_kind=原 kind
  DerivedClass,     // 短类定义实例化后
  BuiltinClass,     // 内建类型 / <annotations> 内 / 带 __OpenModelica_builtin 注解的顶层类
  TopScope,         // <top>
  RootClass,        // 被实例化的根类：parent=引用它的组件（函数时）
  NormalComp,       // 组件默认
  RedeclaredComp,   // redeclare 组件（v1 占位）：parent=被替换组件的 parent
  RedeclaredClass,  // redeclare 类（v1 占位）
  GeneratedInner,   // 找不到 inner 时自动生成
  ImplicitScope,    // <annotations> 等不计入路径的作用域
};

struct InstNodeType {
  NodeTypeKind kind = NodeTypeKind::NormalClass;
  NodeId parent;                                   // BaseClass / RootClass / RedeclaredComp / RedeclaredClass
  Id<SElement> def;                                // BaseClass：那条 EXTENDS 元素
  NodeTypeKind restore_kind = NodeTypeKind::NormalClass;  // BaseClass：原 nodeType 的 kind（简化 inner_ty，v1 够用）

  static InstNodeType normal_class() { return InstNodeType{}; }
  static InstNodeType of(NodeTypeKind k) { InstNodeType t; t.kind = k; return t; }
};

// ---- INode（对标 NFInstNode.mo 的 InstNode，R11 §2.1、§7.1）----
enum class NodeKind : std::uint8_t {
  Empty,        // 哨兵（id 0）：往上走的终点；不得解引用 cls/comp
  Class,        // 一个类，或一条 extends（extends 也用 Class 节点 + BaseClass nodeType）
  Component,    // 一个组件
  InnerOuter,   // outer 链接到 inner 后的包装（inner/outer 两端）
  Ref,          // ClassTree.components 里占位"第 ext_index 条 extends 的组件"（保序，R11 §3.1）
  Name,         // 无实体名字（`:` 维度 cref 等）
  ImplicitScope,// for 迭代器等隐式作用域
  Iterator,     // for 迭代器
};

struct INode {
  NodeKind kind = NodeKind::Empty;
  Symbol name;
  Visibility visibility = Visibility::Public;
  InstNodeType node_type;
  // 双父链（ADR-006 D2）
  NodeId parent_scope;      // 词法外层（类=定义处外层类；组件=其类型定义处的外层）——查名向外走
  NodeId instance_parent;   // 实例树父亲——resolveInner / 压平前缀
  // Class 节点
  Id<SElement> def;         // SCode 定义元素（<top> 用合成，v1 可暂 invalid）
  ClassId cls;              // 共享的 Class 单元
  // Component 节点
  CompId comp;              // 共享的 Component 单元
  // InnerOuter 节点
  NodeId inner, outer;
  // Ref 节点
  std::uint32_t ext_index = 0;

  [[nodiscard]] bool is_empty() const noexcept { return kind == NodeKind::Empty; }
};

// ---- Class（对标 NFClass.mo，R11 §2.3、§7.3）----
// 状态机主线：NotInstantiated → PartialClass → ExpandedClass → InstancedClass；
// 短类：PartialClass(空) → ExpandedDerived → TypedDerived；内建：PartialBuiltin → InstancedBuiltin。
enum class ClassState : std::uint8_t {
  NotInstantiated,
  PartialClass,      // 建好目录（本地类/import 进表，组件未进），修饰待消费
  PartialBuiltin,    // 内建类型 / enumeration（FLAT_TREE 装属性组件）
  ExpandedClass,     // extends 已解析、最终下标已算（树 ExpandedTree）；克隆后树变 InstantiatedTree
  ExpandedDerived,   // 短类：base + dims
  InstancedClass,    // instExpressions 后：ty 已定、sections 已挂（树 FlatTree）
  InstancedBuiltin,  // 内建实例化后
  TypedDerived,      // 短类定型后（无维度的随后塌陷成基类 Class）
};

struct ClassPrefixes {
  bool encapsulated = false;
  bool is_partial = false;
  bool is_final = false;
  bool is_inner = false;
  bool is_outer = false;
  bool replaceable = false;
};

struct Class {
  ClassState state = ClassState::NotInstantiated;
  ClassTreeId tree;
  ModId modifier = ModifierArena::nomod();   // 外部施加的类修饰，等消费
  ModId cc_mod = ModifierArena::nomod();     // constrainedby 修饰
  ClassPrefixes prefixes;
  SRestriction restriction = SRestriction::Class;
  Type ty;                                   // Instanced*/TypedDerived
  // 短类（ExpandedDerived/TypedDerived）
  NodeId base_class;
  std::vector<Dimension> dims;               // v1 无维度短类时恒空
  // `extends ExternalObject` 的外部对象类（NFInst.expandExternalObject，NFInst.mo:736-737 / :885-901 v1.25.0）：只含
  // constructor / destructor 两个函数，组件压平成按类名打印的叶变量（见 flatten 的 emit_ext_object）。expand 里认、校验。
  bool is_ext_object = false;
  // InstancedClass 的方程/算法段：v1 在 instExpressions 时从 def 的 SClassDef 取并类型化，
  // 类型化方程表放这里（先占位，instExpressions 落地时填）。
  // ponytail: sections 用类型化 Expr 的方程/语句，等 instExpressions pass 定义 InstEquation 后接上。
};

// ---- ClassTree（对标 NFClassTree.mo，R11 §2.4、§7.4）----
enum class TreeState : std::uint8_t { Empty, PartialTree, ExpandedTree, InstantiatedTree, FlatTree };

struct LEntry {
  enum class Kind : std::uint8_t { Class, Component, Import } kind = Kind::Class;
  std::uint32_t index = 0;   // 在 classes / components / imports 数组里的下标
};

struct ClassTree {
  TreeState state = TreeState::Empty;
  // 本地类 + import 在 PartialTree 就登记；本地组件 + 继承元素等 expand 算完偏移才登记（R11 §3.1-3.2）。
  std::unordered_map<Symbol, LEntry> lookup;
  std::vector<NodeId> classes;              // 本地 + 继承（继承类排局部之后，不保序，R11 §7.4 不变量 2）
  std::vector<NodeId> components;           // 含 Ref 占位；顺序 = SCode 声明序（保序！压平变量顺序的根）
  std::vector<NodeId> exts;                 // 各 extends 节点，按源码顺序
  std::vector<std::uint32_t> local_components;  // 本类自己声明的组件在 components 里的下标
  // ponytail: InstantiatedTree 阶段 OMC 用 Mutable<InstNode>（SlotId 间接层）让继承元素与基类共享、
  //   redeclare/去重"一处改多视图同步"（R11 §7.0 不变量 4）。v1 单继承链、无 redeclare、无菱形，
  //   继承组件直接以 NodeId 拷进 components 即可；引入 SlotId 推迟到 redeclare/数组竖切片。
  // imports / duplicates：v1 最小，先不建 duplicates 树（单继承无重复继承元素）。
};

// ---- Component（对标 NFComponent.mo，R11 §2.5、§7.5）----
// 单 record + state 枚举（1.25.0 已把 COMPONENT_DEF/UNTYPED/TYPED 合并，R11 §2.5）。
enum class CompKind : std::uint8_t {
  Comp,           // 普通组件
  Iterator,       // for 迭代器
  EnumLiteral,    // 枚举字面量（挂在枚举类的 FLAT_TREE 里）
  TypeAttribute,  // 内建属性 start/unit/…（值存在 modifier.binding）
  Invalid, Wild,
};
enum class CompState : std::uint8_t {
  CompDef,               // 初态：只有 def + 累积 modifier
  PartiallyInstantiated, // class_inst=EMPTY、ty=UNKNOWN、binding=Raw（EMPTY 占位打破循环，R11 §3.3-3、§7.5-2）
  FullyInstantiated,     // binding 已就位、维度未定
  Typed,                 // ty 已定
  TypeChecked,           // binding 已定型
};

struct CompAttributes {
  SConnectorType connector = SConnectorType::Potential;  // flow/stream（压平不打印，R13 §3）
  SVariability variability = SVariability::Var;           // discrete/parameter/constant
  SDirection direction = SDirection::Bidir;               // input/output
  bool is_final = false;
  bool is_redeclare = false;
  bool is_inner = false, is_outer = false;
};

struct Component {
  CompKind kind = CompKind::Comp;
  CompState state = CompState::CompDef;
  Id<SElement> def;                          // SCode 组件元素（内建属性/枚举字面量可 invalid）
  ModId modifier = ModifierArena::nomod();   // CompDef：累积的外部修饰，等 instComponent 消费
  NodeId class_inst;                         // 组件的类型实例（PartiallyInstantiated 时 = EMPTY）
  Type ty;
  std::vector<Id<SMod>> derived_mods;        // 短类链上的属性修饰（`type V=Real(unit=…)`），外层派生在前（优先级高）
  std::vector<NodeId> derived_scopes;        // 与 derived_mods 平行：各短类定义所在的类（修饰值在那里解析）
  // 与 derived_mods 平行：那层类型本身带维（`type V3 = Real[3](start = {1, 2, 3})`）→ 修饰按类型形状切分；否则（标量类型的
  // 修饰落到数组组件上）按组件维铺开（each_all）。
  std::vector<bool> derived_split;
  std::vector<Id<SMod>> extends_mods;        // extends 修饰里改本组件的子修饰（`extends A(x(start = 2))`、`extends A(c(x = 2))`），外层在前
  std::vector<NodeId> extends_scopes;        // 与 extends_mods 平行：写这条 extends 子句的类实例（修饰值在那里解析）
  // 声明（def）写在哪个类实例：声明自己的修饰 / 维 / 条件在这里解析——继承来的组件 = 声明它的基类实例（名字按基类的词法作用域
  // 找，派生类自己的元素看不见），重声明的 = 写重声明的类。inst_component 记；没经过 inst_component 的为空。
  NodeId decl_scope;
  // 被 extends 修饰重声明过（`extends M(redeclare B a(q = 3))`）：def 换成合成的元素（重声明元素 + 原声明补的维 / 条件 /
  // 前缀 / 注释，见 Instantiator::redeclare_component），orig_def = 最初的原声明，cc_mods = 约束修饰（外层在前），flatten 排在
  // def 自己的修饰之后。
  Id<SElement> orig_def;
  std::vector<Id<SMod>> cc_mods;
  // 类型经过短类（`SI.Temperature T`）/ 短类链上有前缀或是连接器短类（`connector RealInput = input Real`）：flatten 据此
  // 判 omc 会不会把组件的 final 丢掉（见 flatten 的 attr_final_dropped）。
  bool derived_type = false;
  bool derived_attrs = false;
  // 声明类型带 `annotation(absoluteValue = false)`：omc 把组件注释换成类型定义的注释（见 inst_component），flatten 用 type_comment。
  bool comment_from_type = false;
  Symbol type_comment;
  Binding binding;
  Id<Expr> condition;                        // 条件组件 `if cond`（v1 占位，条件组件不在 v1）
  CompAttributes attributes;
  Symbol comment;
  SourceLoc loc;
};

// ---- 容器：拥有 inst 层全部 arena（对标 NFInst 里的 program + 全局树）----
struct Inst {
  Interner& interner;
  Absyn& absyn;    // 类型化 Expr 的 raw 绑定从这里取 Absyn 表达式
  SCode& scode;    // 定义源（INode.def / Component.def 指向 SCode 元素）

  Arena<INode> nodes;
  Arena<Class> classes;
  Arena<ClassTree> trees;
  Arena<Component> comps;
  ExprArena exprs;
  ModifierArena mods;

  // <top> 及其附属（R11 §2.2 TopScope）
  NodeId top;
  NodeId annotation_scope;
  std::unordered_map<Symbol, NodeId> generated_inners;  // 自动生成的 inner，按名字（同名 outer 共用一个）
  // 实例化时已施加的短类修饰里的组件重声明（SMod raw，见 inst_component）：flatten 不再把它们当修饰。
  std::unordered_set<std::uint32_t> applied_redecls;

  Inst(Interner& in, Absyn& a, SCode& sc) : interner(in), absyn(a), scode(sc) {
    // id 0 = EMPTY 哨兵节点（往上走的终点，R11 §7.1 不变量 1）。
    Id<INode> empty = nodes.add(INode{});
    YMC_ASSERT(empty.raw == 0, "EMPTY 必须是 nodes arena 的第 0 个");
    (void)empty;
  }

  [[nodiscard]] static NodeId empty_node() noexcept { return NodeId::from_index(0); }

  // 便捷访问器（对标 InstNode.getClass / Class.classTree / getComponent）。
  [[nodiscard]] Class& node_class(NodeId n) { return classes[nodes[n].cls]; }
  [[nodiscard]] ClassTree& class_tree(ClassId c) { return trees[classes[c].tree]; }
  [[nodiscard]] Component& node_comp(NodeId n) { return comps[nodes[n].comp]; }
};

}  // namespace ymc
