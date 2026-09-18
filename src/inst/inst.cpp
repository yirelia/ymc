// 实例化 pass 实现（S3 第一里程碑）。锚点见 inst.h 头注与各处 R11 引用。
#include "inst/inst.h"

namespace ymc {

namespace {
// 内建类型名 ↔ TypeKind（R11 §4.3：Real/Integer/Boolean/String 是硬编码查找的短路名）。
constexpr TypeKind kBuiltinKinds[4] = {TypeKind::Real, TypeKind::Integer, TypeKind::Boolean,
                                       TypeKind::String};
const char* builtin_name(int i) {
  switch (i) {
    case 0: return "Real";
    case 1: return "Integer";
    case 2: return "Boolean";
    case 3: return "String";
  }
  return "";
}
Visibility vis_of(SVisibility v) { return v == SVisibility::Protected ? Visibility::Protected : Visibility::Public; }
}  // namespace

// -------------------------------------------------------------- 内建
void Instantiator::make_builtin_types() {
  for (int i = 0; i < 4; ++i) {
    // 内建类型节点：BuiltinClass，cls 是 PartialBuiltin（v1 标量无属性，tree 留空）。
    Class c;
    c.state = ClassState::PartialBuiltin;
    c.restriction = SRestriction::Type;
    c.ty = Type::of(kBuiltinKinds[i]);
    INode n;
    n.kind = NodeKind::Class;
    n.name = I_.interner.intern(builtin_name(i));
    n.node_type = InstNodeType::of(NodeTypeKind::BuiltinClass);
    n.parent_scope = Inst::empty_node();
    n.cls = I_.classes.add(std::move(c));
    builtin_[i] = I_.nodes.add(std::move(n));
  }
}

NodeId Instantiator::builtin_node(TypeKind k) const {
  for (int i = 0; i < 4; ++i)
    if (kBuiltinKinds[i] == k) return builtin_[i];
  return Inst::empty_node();
}

// -------------------------------------------------------------- <top>
void Instantiator::make_top_node() {
  make_builtin_types();
  // 顶层类各建一个 NotInstantiated 的 Class 节点，登记进 top 的 ClassTree。
  ClassTree tt;
  tt.state = TreeState::ExpandedTree;
  for (Id<SElement> cls_el : I_.scode.program) {
    const SElement& el = I_.scode.elems[cls_el];
    if (el.kind != SElementKind::Class) continue;
    Class c;  // NotInstantiated
    INode cn;
    cn.kind = NodeKind::Class;
    cn.name = el.name;
    cn.def = cls_el;
    cn.node_type = InstNodeType::normal_class();
    cn.cls = I_.classes.add(std::move(c));
    std::uint32_t idx = static_cast<std::uint32_t>(tt.classes.size());
    NodeId cnid = I_.nodes.add(std::move(cn));
    tt.classes.push_back(cnid);
    tt.lookup[el.name] = LEntry{LEntry::Kind::Class, idx};
  }
  Class topc;
  topc.state = ClassState::ExpandedClass;
  topc.restriction = SRestriction::Package;
  topc.tree = I_.trees.add(std::move(tt));
  INode topn;
  topn.kind = NodeKind::Class;
  topn.name = I_.interner.intern("<top>");
  topn.node_type = InstNodeType::of(NodeTypeKind::TopScope);
  topn.parent_scope = Inst::empty_node();
  topn.cls = I_.classes.add(std::move(topc));
  I_.top = I_.nodes.add(std::move(topn));
  // 顶层类节点的词法外层 = <top>
  ClassTree& t = I_.trees[I_.classes[I_.nodes[I_.top].cls].tree];
  for (NodeId cn : t.classes) I_.nodes[cn].parent_scope = I_.top;
}

// -------------------------------------------------------------- 查找（最小版）
NodeId Instantiator::lookup_simple_class(Symbol name, NodeId scope) {
  // 内建名先短路，不看作用域（R11 §4.3、R12 §2.1）。
  for (int i = 0; i < 4; ++i)
    if (I_.nodes[builtin_[i]].name == name) return builtin_[i];
  // 沿词法外层链逐层查（R12 §2）。
  NodeId cur = scope;
  while (cur.is_valid() && !I_.nodes[cur].is_empty()) {
    const INode& n = I_.nodes[cur];
    if (n.kind == NodeKind::Class && n.cls.is_valid()) {
      const Class& c = I_.classes[n.cls];
      if (c.tree.is_valid()) {
        const ClassTree& t = I_.trees[c.tree];
        auto it = t.lookup.find(name);
        if (it != t.lookup.end() && it->second.kind == LEntry::Kind::Class)
          return t.classes[it->second.index];
      }
    }
    cur = I_.nodes[cur].parent_scope;
  }
  return Inst::empty_node();
}

NodeId Instantiator::lookup_class_name(const std::vector<Symbol>& path, NodeId scope) {
  if (path.empty()) return Inst::empty_node();
  NodeId cur = lookup_simple_class(path[0], scope);
  for (std::size_t i = 1; i < path.size() && cur.is_valid() && !I_.nodes[cur].is_empty(); ++i) {
    // 复合名后续段：进入该类内部查（需先 expand 出树）。
    if (I_.node_class(cur).state == ClassState::NotInstantiated ||
        I_.node_class(cur).state == ClassState::PartialClass) {
      partial_inst(cur);
      expand(cur);
    }
    const Class& c = I_.node_class(cur);
    NodeId next = Inst::empty_node();
    if (c.tree.is_valid()) {
      const ClassTree& t = I_.trees[c.tree];
      auto it = t.lookup.find(path[i]);
      if (it != t.lookup.end() && it->second.kind == LEntry::Kind::Class)
        next = t.classes[it->second.index];
    }
    cur = next;
  }
  return (cur.is_valid() && !I_.nodes[cur].is_empty()) ? cur : Inst::empty_node();
}

// -------------------------------------------------------------- partialInst（建目录）
void Instantiator::partial_inst(NodeId cls) {
  const SElement& el = I_.scode.elems[I_.nodes[cls].def];
  const SClassDef& def = el.def;
  ClassTree tree;
  tree.state = TreeState::PartialTree;
  // v1 只处理 PARTS（长类）；短类/枚举等后续子步。
  for (Id<SElement> eid : def.elements) {
    const SElement& e = I_.scode.elems[eid];
    switch (e.kind) {
      case SElementKind::Class: {
        Class c;  // NotInstantiated
        INode cn;
        cn.kind = NodeKind::Class;
        cn.name = e.name;
        cn.def = eid;
        cn.parent_scope = cls;
        cn.visibility = vis_of(e.prefixes.vis);
        cn.node_type = InstNodeType::normal_class();
        cn.cls = I_.classes.add(std::move(c));
        std::uint32_t idx = static_cast<std::uint32_t>(tree.classes.size());
        tree.classes.push_back(I_.nodes.add(std::move(cn)));
        tree.lookup[e.name] = LEntry{LEntry::Kind::Class, idx};   // 类在 partialInst 就登记（R11 §3.1）
        break;
      }
      case SElementKind::Component: {
        Component comp;
        comp.kind = CompKind::Comp;
        comp.state = CompState::CompDef;
        comp.def = eid;
        comp.loc = e.loc;
        INode cn;
        cn.kind = NodeKind::Component;
        cn.name = e.name;
        cn.def = eid;
        cn.visibility = vis_of(e.prefixes.vis);
        cn.node_type = InstNodeType::of(NodeTypeKind::NormalComp);
        cn.comp = I_.comps.add(std::move(comp));
        std::uint32_t idx = static_cast<std::uint32_t>(tree.components.size());
        tree.components.push_back(I_.nodes.add(std::move(cn)));
        tree.local_components.push_back(idx);   // 组件不进 lookup（等 expand 算偏移，R11 §3.1）
        break;
      }
      case SElementKind::Extends: {
        INode en;
        en.kind = NodeKind::Class;
        en.name = e.base_path.empty() ? Symbol{} : e.base_path.back();
        en.def = eid;
        en.parent_scope = cls;
        en.visibility = vis_of(e.prefixes.vis);
        en.node_type = InstNodeType::of(NodeTypeKind::BaseClass);
        en.node_type.def = eid;
        en.node_type.parent = cls;
        std::uint32_t ext_i = static_cast<std::uint32_t>(tree.exts.size());
        tree.exts.push_back(I_.nodes.add(std::move(en)));
        // 在组件数组占一个 Ref，保住"extends 在其子句位置就地展开"的顺序（R11 §3.1）。
        INode ref;
        ref.kind = NodeKind::Ref;
        ref.ext_index = ext_i;
        tree.components.push_back(I_.nodes.add(std::move(ref)));
        break;
      }
      case SElementKind::Import:
        break;  // v1 跳过
    }
  }
  Class& c = I_.node_class(cls);
  c.state = ClassState::PartialClass;
  c.restriction = el.restriction;
  c.prefixes.is_partial = el.is_partial;
  c.prefixes.encapsulated = el.is_encapsulated;
  c.tree = I_.trees.add(std::move(tree));
}

// -------------------------------------------------------------- expand（解析 extends）
void Instantiator::expand(NodeId cls) {
  Class& c = I_.node_class(cls);
  if (c.state != ClassState::PartialClass) return;
  c.state = ClassState::ExpandedClass;   // 占位（extends 环检测的信号，R11 §3.2）
  ClassTreeId tree_id = I_.node_class(cls).tree;
  // 校验并展开各基类（其树要就绪，inst_extends 才能取到继承组件）。
  const std::vector<NodeId> exts = I_.trees[tree_id].exts;   // 拷一份避免遍历中 arena 变动
  for (NodeId ext : exts) {
    const SElement& ee = I_.scode.elems[I_.nodes[ext].def];
    // extends 带修饰 `extends A(x = 1)` 要修饰合并——v1 未做，标 unsupported。
    if (ee.mod.is_valid() && I_.node_class(cls).state == ClassState::ExpandedClass) {
      const SMod& m = I_.scode.mods[ee.mod];
      if (!m.subs.empty() || m.binding.is_valid()) mark_unsupported("extends modifier");
    }
    NodeId base = lookup_class_name(ee.base_path, I_.nodes[cls].parent_scope);
    if (!base.is_valid() || I_.nodes[base].is_empty()) {
      // v1 的最小 lookup 没做 import/内建枚举——基类查不到先当 v1 gap 标 unsupported（driver 退 66），不硬报错。
      mark_unsupported("unresolved base class");
      continue;
    }
    if (I_.node_class(base).state == ClassState::NotInstantiated) partial_inst(base);
    expand(base);
  }
  // 本地组件登记进 lookup（下标 = def 组件数组里的位置；v1 未做继承元素合并到 lookup）。
  ClassTree& tree = I_.trees[tree_id];
  for (std::uint32_t li : tree.local_components) {
    const INode& cn = I_.nodes[tree.components[li]];
    tree.lookup[cn.name] = LEntry{LEntry::Kind::Component, li};
  }
  tree.state = TreeState::ExpandedTree;
}

// -------------------------------------------------------------- instClass（克隆 + 建 FlatTree）
NodeId Instantiator::instantiate(NodeId def_node, NodeId parent) {
  // 递归深度守卫：递归定义（RECURSIVE_DEFINITION，R11 §6.2）或 v1 未做的 inner/outer 链接可能无限递归；
  // 超过上限就标 unsupported 并返回克隆但不再下钻，避免栈溢出（driver 退 66）。
  if (++depth_ > 200) {
    --depth_;
    mark_unsupported("recursive/deep instantiation");
    Class inst_c = I_.classes[I_.nodes[def_node].cls];
    INode inst_n = I_.nodes[def_node];
    inst_n.cls = I_.classes.add(std::move(inst_c));
    inst_n.instance_parent = parent;
    NodeId stub = I_.nodes.add(std::move(inst_n));
    ClassTree empty;
    empty.state = TreeState::FlatTree;
    Class& sc = I_.classes[I_.nodes[stub].cls];
    sc.tree = I_.trees.add(std::move(empty));
    sc.state = ClassState::InstancedClass;
    return stub;
  }
  if (I_.node_class(def_node).state == ClassState::NotInstantiated) partial_inst(def_node);
  if (I_.node_class(def_node).state == ClassState::PartialClass) expand(def_node);
  // 克隆：新 Class 单元（Pointer.create，断开与定义节点共享）+ 新节点（R11 §7.0）。
  Class inst_c = I_.classes[I_.nodes[def_node].cls];   // 拷贝 def 的 Class（含 ExpandedTree 引用，供查找）
  INode inst_n = I_.nodes[def_node];                   // 拷贝节点（parent_scope 保留词法外层）
  inst_n.cls = I_.classes.add(std::move(inst_c));
  inst_n.instance_parent = parent;
  NodeId inst_node = I_.nodes.add(std::move(inst_n));
  inst_class(inst_node, def_node);
  --depth_;
  return inst_node;
}

void Instantiator::inst_class(NodeId inst_node, NodeId def_node) {
  ClassTreeId def_tree_id = I_.node_class(def_node).tree;
  if (!def_tree_id.is_valid()) {
    // 无类体（内建类型 / 尚未支持的短类/derived）——给个空 FlatTree 防崩，标 unsupported（driver 退 66）。
    mark_unsupported("no class body (builtin/derived type)");
    ClassTree empty;
    empty.state = TreeState::FlatTree;
    Class& ic0 = I_.classes[I_.nodes[inst_node].cls];
    ic0.tree = I_.trees.add(std::move(empty));
    ic0.state = ClassState::InstancedClass;
    return;
  }
  const std::vector<NodeId> def_comps = I_.trees[def_tree_id].components;  // 拷一份
  ClassTree flat;
  flat.state = TreeState::FlatTree;
  for (NodeId cn : def_comps) {
    if (I_.nodes[cn].kind == NodeKind::Ref) {
      // extends 就地展开：实例化基类，把它的 FlatTree 组件整段拼进来（R11 §3.3 instExtendsComps）。
      std::uint32_t ext_i = I_.nodes[cn].ext_index;
      NodeId ext = I_.trees[def_tree_id].exts[ext_i];
      const SElement& ee = I_.scode.elems[I_.nodes[ext].def];
      NodeId base_def = lookup_class_name(ee.base_path, I_.nodes[def_node].parent_scope);
      if (!base_def.is_valid() || I_.nodes[base_def].is_empty()) continue;  // expand 已报错
      // extends 内建类型（extends Real 之类）v1 还没做，标 unsupported 并跳过（避免实例化无类体的内建节点）。
      bool base_builtin = false;
      for (int k = 0; k < 4; ++k)
        if (base_def == builtin_[k]) base_builtin = true;
      if (base_builtin) { mark_unsupported("extends builtin type"); continue; }
      // extends 短类/枚举/derived 基类（`model B = A; extends B`）v1 未做，标 unsupported。
      Id<SElement> bdef = I_.nodes[base_def].def;
      if (!bdef.is_valid() || I_.scode.elems[bdef].def.kind != SClassDefKind::Parts) {
        mark_unsupported("extends short/enum/derived class");
        continue;
      }
      // 基类带方程/算法 → 压平时要按 R11 §3.4-6 的"extends 逆序"合并，flatten 里没做基类方程收集，标 unsupported。
      const SClassDef& bd = I_.scode.elems[bdef].def;
      if (!bd.equations.empty() || !bd.initial_equations.empty() || !bd.algorithms.empty() ||
          !bd.initial_algorithms.empty())
        mark_unsupported("extends with equations/algorithms");
      NodeId base_inst = instantiate(base_def, inst_node);
      bool prot = I_.nodes[ext].visibility == Visibility::Protected;
      ClassTreeId btree = I_.node_class(base_inst).tree;
      for (NodeId bc : I_.trees[btree].components) {
        if (prot) I_.nodes[bc].visibility = Visibility::Protected;   // protected extends（R11 §3.4-2）
        flat.components.push_back(bc);
      }
    } else {  // 普通组件：克隆 + 实例化
      const INode& dc = I_.nodes[cn];
      Component nc = I_.comps[dc.comp];   // 拷贝（CompDef）
      INode nn = dc;
      nn.comp = I_.comps.add(std::move(nc));
      nn.instance_parent = inst_node;
      NodeId icomp = I_.nodes.add(std::move(nn));
      inst_component(icomp, inst_node);
      flat.components.push_back(icomp);
    }
  }
  Class& ic = I_.classes[I_.nodes[inst_node].cls];
  ic.tree = I_.trees.add(std::move(flat));
  ic.state = ClassState::InstancedClass;
}

void Instantiator::inst_component(NodeId comp_node, NodeId enclosing) {
  // 纪律（arena.h 头注）：不持有 Component 引用跨越任何 arena add——lookup_class_name / instantiate
  // 都会往 comps/nodes/... 里 add 触发扩容，使旧引用失效。这里每次触碰组件都重新取引用。
  const CompId cid = I_.nodes[comp_node].comp;
  const Id<SElement> def_id = I_.comps[cid].def;
  const SElement& el = I_.scode.elems[def_id];   // SCode 只读，引用稳定

  // 组件先进 PartiallyInstantiated + class_inst=EMPTY 占位（R11 §3.3-3 打破循环），再解析类型。
  {
    Component& comp = I_.comps[cid];
    comp.attributes.connector = el.attr.ct;
    comp.attributes.variability = el.attr.var;
    comp.attributes.direction = el.attr.dir;
    comp.attributes.is_final = el.prefixes.is_final;
    comp.attributes.is_redeclare = el.prefixes.is_redeclare;
    comp.attributes.is_inner = el.prefixes.io == SInnerOuter::Inner || el.prefixes.io == SInnerOuter::InnerOuter;
    comp.attributes.is_outer = el.prefixes.io == SInnerOuter::Outer || el.prefixes.io == SInnerOuter::InnerOuter;
    comp.class_inst = Inst::empty_node();
    comp.state = CompState::PartiallyInstantiated;
  }

  // outer 组件指向别处的 inner，不应实例化自己的类型——v1 没做 inner/outer 链接，若照常实例化会在
  // `outer B b`（B 里又含 outer B）这类结构上无限递归。标 unsupported 并当叶处理（不递归）。
  if (el.prefixes.io == SInnerOuter::Outer || el.prefixes.io == SInnerOuter::InnerOuter) {
    mark_unsupported("inner/outer");
    Component& comp = I_.comps[cid];
    comp.ty = Type::unknown();
    comp.state = CompState::FullyInstantiated;
    return;
  }
  NodeId ty_node = lookup_class_name(el.type.path, enclosing);   // 可能触发 partial_inst/expand → arena add
  if (!ty_node.is_valid() || I_.nodes[ty_node].is_empty()) {
    // v1 的查找是最小版（没做内建枚举 StateSelect/AssertionLevel、import、复合名全解析）——查不到组件类型
    // 时先当 v1 gap 标 unsupported（driver 退 66 记 SKIP），不当硬错误。真正的类型错误等 lookup 完整后再报。
    mark_unsupported("unresolved component type");
    I_.comps[cid].kind = CompKind::Invalid;
    return;
  }

  // 短类塌陷（R15 EXPANDED_DERIVED）：`type V = Base(mods)` 顺链解析到内建/枚举底，累积属性修饰
  // （外层派生在前 = 优先级高）。lookup_class_name 会 arena add，循环里不缓存 INode 引用（只用 Id）。
  std::vector<Id<SMod>> derived_mods;
  // 短类的前缀属性（`type InputReal = input Real`、`type C = constant …`）累积到组件上：
  // 组件自身 > 靠近用处的派生 > 更远派生（loop 外→内，first-non-default 胜）。
  SVariability mvar = el.attr.var;
  SDirection mdir = el.attr.dir;
  SConnectorType mct = el.attr.ct;
  NodeId base = ty_node;
  for (int guard = 0; guard < 100 && base.is_valid() && !I_.nodes[base].is_empty(); ++guard) {
    bool builtin = false;
    for (int i = 0; i < 4; ++i)
      if (base == builtin_[i]) { builtin = true; break; }
    if (builtin) break;
    const Id<SElement> ce = I_.nodes[base].def;
    if (!ce.is_valid()) break;
    if (I_.scode.elems[ce].def.kind != SClassDefKind::Derived) break;   // Enumeration/Parts/… 到底
    const SClassDef& cd = I_.scode.elems[ce].def;
    if (cd.derived_mod.is_valid()) {
      const SMod& dm = I_.scode.mods[cd.derived_mod];
      if (!dm.subs.empty() || dm.binding.is_valid()) derived_mods.push_back(cd.derived_mod);
    }
    if (mvar == SVariability::Var && cd.derived_attr.var != SVariability::Var) mvar = cd.derived_attr.var;
    if (mdir == SDirection::Bidir && cd.derived_attr.dir != SDirection::Bidir) mdir = cd.derived_attr.dir;
    if (mct == SConnectorType::Potential && cd.derived_attr.ct != SConnectorType::Potential) mct = cd.derived_attr.ct;
    NodeId nxt = lookup_class_name(cd.derived_type.path, I_.nodes[base].parent_scope);
    base = (nxt.is_valid() && !I_.nodes[nxt].is_empty()) ? nxt : Inst::empty_node();
  }

  // base = 内建 / 枚举 / 长类（复合）/ 解析失败。把结果算进局部，最后回写组件。
  NodeId class_inst = Inst::empty_node();
  Type ty;
  bool leaf = false;
  for (int i = 0; i < 4; ++i)
    if (base == builtin_[i]) { ty = Type::of(kBuiltinKinds[i]); class_inst = base; leaf = true; break; }
  if (!leaf && base.is_valid() && !I_.nodes[base].is_empty() &&
      I_.scode.elems[I_.nodes[base].def].def.kind == SClassDefKind::Enumeration) {
    const SClassDef& cd = I_.scode.elems[I_.nodes[base].def].def;
    ty.kind = TypeKind::Enumeration;
    ty.enum_path = {I_.nodes[base].name};   // 类型名（dump 用 enum_literals 打声明，enum_path 备用）
    for (const SEnumItem& it : cd.enum_items) ty.enum_literals.push_back(it.literal);
    class_inst = base;
    leaf = true;
  }
  if (!leaf) {
    if (!base.is_valid() || I_.nodes[base].is_empty()) {   // 派生链解析失败
      mark_unsupported("unresolved derived base");
      I_.comps[cid].kind = CompKind::Invalid;
      return;
    }
    // 短类别名到长类（`model M = P.M`）会牵出常量内联等（ExtendsShort1 里 `x = n` → omc 内联成 2），v1 未做 → SKIP。
    if (base != ty_node) mark_unsupported("short class aliasing composite");
    class_inst = instantiate(base, comp_node);   // ← arena add：此后不得用先前取的组件引用
    ty.kind = TypeKind::Complex;
    ty.complex_node = class_inst.raw;
  }
  Id<Exp> bind = el.mod.is_valid() ? I_.scode.mods[el.mod].binding : Id<Exp>{};

  // 全部 add 已完成，重新取引用回写最终值（R12 §2.4：绑定右式在 enclosing 作用域解析，留给 instExpressions）。
  Component& comp = I_.comps[cid];
  comp.class_inst = class_inst;
  comp.ty = ty;
  comp.derived_mods = std::move(derived_mods);
  comp.attributes.variability = mvar;   // 含短类前缀（constant/input/…）累积
  comp.attributes.direction = mdir;
  comp.attributes.connector = mct;
  if (bind.is_valid()) {
    comp.binding.kind = BindingKind::Raw;
    comp.binding.raw_exp = bind;
    comp.binding.scope = enclosing;
    comp.binding.loc = el.loc;
  }
  comp.state = CompState::FullyInstantiated;
}

// -------------------------------------------------------------- 根类实例化
NodeId Instantiator::instantiate_root(Symbol name) {
  NodeId def_node = lookup_simple_class(name, I_.top);
  if (!def_node.is_valid() || I_.nodes[def_node].is_empty()) {
    diags_.error(SourceLoc{}, "Class " + std::string(I_.interner.str(name)) + " not found in scope <top>.");
    return Inst::empty_node();
  }
  return instantiate(def_node, Inst::empty_node());
}

}  // namespace ymc
