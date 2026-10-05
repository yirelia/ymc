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

std::string Instantiator::path_str(const std::vector<Symbol>& path) const {
  std::string s;
  for (Symbol p : path) {
    if (!s.empty()) s += '.';
    s += I_.interner.str(p);
  }
  return s;
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
  // 沿词法外层链逐层查（R12 §2）。每层先看本地类，再看这一层的 import（本地元素 > import，NFClassTree.mo:1654
  // addLocalElementConflict；import 名对本类里嵌套的类可见——它就是本类表里的一项，向外层走自然碰到）。
  NodeId cur = scope;
  while (cur.is_valid() && !I_.nodes[cur].is_empty()) {
    if (I_.nodes[cur].kind == NodeKind::Class && I_.nodes[cur].cls.is_valid()) {
      const ClassTreeId tid = I_.classes[I_.nodes[cur].cls].tree;
      if (tid.is_valid()) {
        const ClassTree& t = I_.trees[tid];
        auto it = t.lookup.find(name);
        if (it != t.lookup.end() && it->second.kind == LEntry::Kind::Class) return t.classes[it->second.index];
      }
      const NodeId im = import_class(name, cur);   // 可能 partial_inst / expand 别的包 → arena add，上面的引用不再用
      if (im.is_valid() && !I_.nodes[im].is_empty()) return im;
      if (unsupported_) return Inst::empty_node();   // 冲突 / 解析不到的 import：不再往外层找（omc 在这里报错）
      // encapsulated 类是查找的边界：本层（本地 + import）找不到，就只剩内建类（开头已短路），不再往外层走
      //（MLS §5.3.1；NFLookup.lookupSimpleName 的 isEncapsulated 支 → topScope + require_builtin，NFLookup.mo:465-470
      // v1.25.0）。packages/Encapsulated3 的 `extends B.C` 在 encapsulated 的 C2 里查不到 B → omc 报错。
      const Id<SElement> cd = I_.nodes[cur].def;
      if (cd.is_valid() && I_.scode.elems[cd].kind == SElementKind::Class && I_.scode.elems[cd].is_encapsulated)
        return Inst::empty_node();
    }
    cur = I_.nodes[cur].parent_scope;
  }
  return Inst::empty_node();
}

// 类节点 cls 自己的 import 按名找类（MLS §13.2；NFClassTree / NFLookup.lookupImport :276，R12 §5；flatten 的 import_lookup 是
// 同一套规则在 SCode 上的实现，表达式里的名字走那边）：
//   - 限定 `import A.B.C;` / 重命名 `import X = A.B;` / 组导入 `import A.{c, e = d}` 优先于非限定 `import A.*;`
//     （addImportConflict，NFClassTree.mo:1757-1800）；同一档里两个同名项 = 冲突（即使目标相同），用到才报错；
//   - 目标永远从顶层查（`import P.M` 的 P 即使本类有同名局部包也不算）；
//   - import 不被继承：只看 cls 自己的 SCode 元素。
// 返回找到的类节点；没有这个名字的 import → empty；冲突 / 目标解析不到 / 目标不是类 → mark_unsupported 并返回 empty
//（omc 报错的情形，ymc 不产错误文案）。任务卡 T-S3-06。
NodeId Instantiator::import_class(Symbol name, NodeId cls) {
  const Id<SElement> d = I_.nodes[cls].def;
  if (!d.is_valid() || I_.scode.elems[d].kind != SElementKind::Class) return Inst::empty_node();
  const std::vector<Id<SElement>> elems = I_.scode.elems[d].def.elements;   // 拷贝（下面的查找会扩 inst 的 arena）
  std::vector<std::vector<Symbol>> targets;   // 限定 / 重命名 / 组导入里叫 name 的各项的目标路径
  bool has_wild = false;
  for (Id<SElement> eid : elems) {
    const SElement& el = I_.scode.elems[eid];
    if (el.kind != SElementKind::Import) continue;
    if (el.imp.wildcard) { has_wild = true; continue; }
    if (!el.imp.multi.empty()) {
      for (std::size_t k = 0; k < el.imp.multi.size(); ++k) {
        const bool aliased = k < el.imp.multi_alias.size() && el.imp.multi_alias[k].is_valid();
        if ((aliased ? el.imp.multi_alias[k] : el.imp.multi[k]) != name) continue;
        std::vector<Symbol> path = el.imp.path;
        path.push_back(el.imp.multi[k]);
        targets.push_back(std::move(path));
      }
    } else if (el.imp.alias.is_valid() ? el.imp.alias == name : (!el.imp.path.empty() && el.imp.path.back() == name)) {
      targets.push_back(el.imp.path);
    }
  }
  if (targets.size() >= 2) { mark_unsupported("conflicting imports"); return Inst::empty_node(); }
  if (targets.size() == 1) {
    const NodeId t = lookup_class_name(targets[0], I_.top);
    if (!t.is_valid() || I_.nodes[t].is_empty()) { mark_unsupported("unresolved import"); return Inst::empty_node(); }
    return t;
  }
  if (!has_wild) return Inst::empty_node();
  // `.*`：包里的每个类（和组件）以原名可见（不滤 protected）；两个包都提供同名元素 = 冲突。包本身找不到时 omc 在建表期
  // 就报错（initImports），那一条由 flatten_class 的检查拦下，这里只跳过。
  NodeId found = Inst::empty_node();
  int matches = 0;
  for (Id<SElement> eid : elems) {
    const SElement& el = I_.scode.elems[eid];
    if (el.kind != SElementKind::Import || !el.imp.wildcard) continue;
    const std::vector<Symbol> ppath = el.imp.path;   // 拷贝：下面会扩 arena（SCode 本身不动，保险起见）
    const NodeId pkg = lookup_class_name(ppath, I_.top);
    if (!pkg.is_valid() || I_.nodes[pkg].is_empty() || !I_.nodes[pkg].def.is_valid()) continue;
    if (I_.node_class(pkg).state == ClassState::NotInstantiated) partial_inst(pkg);
    if (I_.node_class(pkg).state == ClassState::PartialClass) expand(pkg);
    const ClassTreeId pt = I_.node_class(pkg).tree;
    if (!pt.is_valid()) continue;
    const ClassTree& t = I_.trees[pt];
    const auto it = t.lookup.find(name);
    if (it == t.lookup.end()) continue;
    ++matches;
    found = it->second.kind == LEntry::Kind::Class ? t.classes[it->second.index] : Inst::empty_node();
  }
  if (matches >= 2) { mark_unsupported("conflicting imports"); return Inst::empty_node(); }
  if (matches == 1 && (!found.is_valid() || I_.nodes[found].is_empty())) {   // 类型名查到的是组件（常量）→ omc 报错
    mark_unsupported("import names a component, not a class");
    return Inst::empty_node();
  }
  return found;
}

NodeId Instantiator::lookup_class_name(const std::vector<Symbol>& path, NodeId scope, std::vector<NodeId>* path_nodes) {
  if (path.empty()) return Inst::empty_node();
  NodeId cur = lookup_simple_class(path[0], scope);
  if (path_nodes && cur.is_valid() && !I_.nodes[cur].is_empty()) path_nodes->push_back(cur);
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
    if (path_nodes && cur.is_valid() && !I_.nodes[cur].is_empty()) path_nodes->push_back(cur);
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
        // 声明的前缀先放上：包里的常量等永不实例化的组件（CompDef）也有对的变率——短类定义在包里时，其修饰值在包作用域
        // 解析，引用的包常量要按 constant 看（`package P constant Real c = 4; model B = A(k = c); end P;`）。
        // inst_component 会再叠上短类链的前缀（`type C = constant Real`）重算。
        comp.attributes.variability = e.attr.var;
        comp.attributes.direction = e.attr.dir;
        comp.attributes.connector = e.attr.ct;
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
    // extends 带整体绑定 `extends A = expr` 不合法/未做；子修饰 `extends A(x=1)` 在 inst_class 合并（见下）。
    if (ee.mod.is_valid() && I_.scode.mods[ee.mod].binding.is_valid()) mark_unsupported("extends whole binding");
    // 基类名从**扩展类自身**开始查（MLS §7.1：在扩展类的部分展平形态里查；extends 节点的 parent 是扩展类本身，
    // NFClassTree.mo:202 newExtends(e, parent)、NFInst.mo:775-776 `scope := InstNode.parent(ext)`，v1.25.0）——看得见它
    // 自己的嵌套类（`package Modelica … extends Icons.Library;`），看不见继承来的元素（此时只登记了本地类）。任务卡 T-S3-07。
    std::vector<NodeId> base_nodes;
    NodeId base = lookup_class_name(ee.base_path, cls, &base_nodes);
    if (!base.is_valid() || I_.nodes[base].is_empty()) {
      // v1 的最小 lookup 没做 import/内建枚举——基类查不到先当 v1 gap 标 unsupported（driver 退 66），不硬报错。
      mark_unsupported("unresolved base class", path_str(ee.base_path));
      continue;
    }
    // 基类名的各段须传递地非 replaceable（MLS §7.1.4；checkReplaceableBaseClass，NFInst.mo:778 / :842-883）：
    // `replaceable model M … extends M;` → omc 报 "the base class name must be transitively non-replaceable"。
    for (NodeId bn : base_nodes) {
      const Id<SElement> bd = I_.nodes[bn].def;
      if (bd.is_valid() && I_.scode.elems[bd].prefixes.is_replaceable) mark_unsupported("replaceable base class");
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
      NodeId base_def = lookup_class_name(ee.base_path, def_node);   // 同 expand：从扩展类自身查（T-S3-07）
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
      const Id<SMod> ext_mod = ee.mod;   // extends 修饰（在 base_def lookup 之后取，避免跨 add 持 SElement 引用）
      NodeId base_inst = instantiate(base_def, inst_node);
      // 基类实例按 extends 出现序记进本实例的类树：基类的方程 / 算法段由 flatten 的 collect_inherited_sections 收
      //（段序 = 本类在前、各 extends 按**逆序**在后，NFInst.instExpressions + Sections.join 头插，NFInst.mo:2511-2514 /
      // NFSections.mo:192-209；名字在基类实例里解析，前缀与本实例相同）。只记实例化后的基类节点（FlatTree），不记 Ref /
      // 定义节点；多层继承由递归的 instantiate → inst_class 各自记好。任务卡 T-S3-05。
      flat.exts.push_back(base_inst);
      bool prot = I_.nodes[ext].visibility == Visibility::Protected;
      ClassTreeId btree = I_.node_class(base_inst).tree;
      const std::vector<NodeId> base_comps = I_.trees[btree].components;   // 拷一份
      for (NodeId bc : base_comps) {
        if (prot) I_.nodes[bc].visibility = Visibility::Protected;   // protected extends（R11 §3.4-2）
        // 继承成员的词法作用域 = 基类（限定名/常量按基类作用域解析，const16 的 `P.P.i` 要在 P.M 里找）。
        I_.nodes[bc].parent_scope = base_inst;
        apply_extends_modifier(bc, ext_mod, inst_node);              // extends 修饰合并到继承组件
        flat.components.push_back(bc);
      }
      // extends 修饰命名了非组件（嵌套类型/类，如 mod9 `extends A(MyReal(start=y))`）→ v1 未做类修饰，标 unsupported。
      if (ext_mod.is_valid())
        for (const SSubMod& s : I_.scode.mods[ext_mod].subs) {
          bool found = false;
          for (NodeId bc : base_comps)
            if (I_.nodes[bc].name == s.ident) { found = true; break; }
          if (!found) mark_unsupported("extends modifier names non-component (nested class)");
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

// extends 修饰合并到继承组件（R15 §3.2(b)：extends 修饰当作外来修饰压进基类元素）。
// 只处理叶组件的绑定/属性/final；复合组件带子修饰、redeclare/break、each 数组等 v1 未做 → 标 unsupported。
// 不做 arena add：只改已存在的 comp 字段（绑定/属性引用留给 flatten 在派生作用域解析）。
void Instantiator::apply_extends_modifier(NodeId bc, Id<SMod> mod, NodeId scope) {
  if (!mod.is_valid()) return;
  const Symbol name = I_.nodes[bc].name;
  const CompId cid = I_.nodes[bc].comp;
  for (const SSubMod& s : I_.scode.mods[mod].subs) {
    if (s.ident != name) continue;
    const SMod& sm = I_.scode.mods[s.mod];
    if (sm.kind != SModKind::Mod) { mark_unsupported("extends redeclare/break modifier"); return; }
    Component& comp = I_.comps[cid];
    if (comp.ty.kind == TypeKind::Complex && !sm.subs.empty()) {
      mark_unsupported("extends modifier on composite");   // 需往子树传，v1 未做（easy slice 都是叶）
      return;
    }
    if (sm.binding.is_valid()) {   // 绑定覆盖（extends A(x = 1)）——值在派生作用域解析
      comp.binding.kind = BindingKind::Raw;
      comp.binding.raw_exp = sm.binding;
      comp.binding.scope = scope;
    }
    if (sm.is_final) comp.attributes.is_final = true;
    // 属性修饰（`extends A(x(start = 2))`）：比组件自己声明的修饰外一层（修饰由内向外 merge：声明 → 各层 extends →
    // 实例修饰），flatten 排在声明修饰之前（EX 探针 EX1：A 里 `Real x(start = 1)`，omc 取 2.0）。多层继承时外层的 extends
    // 后施加（基类实例先建好），插到前面 = 外层在前。此前混进 derived_mods（类型链修饰，排在声明修饰之后），优先级反了。
    if (!sm.subs.empty()) comp.extends_mods.insert(comp.extends_mods.begin(), s.mod);
  }
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

  // outer 组件指向别处的 inner，不实例化自己的子树（否则 `outer B b`（B 里又含 outer B）会无限递归）。
  // v1 只做标量 outer：解析类型名，是内建标量就记类型（flatten 靠名字连到 inner）；复合 outer 暂不做。
  if (el.prefixes.io == SInnerOuter::Outer || el.prefixes.io == SInnerOuter::InnerOuter) {
    NodeId tn = lookup_class_name(el.type.path, enclosing);   // arena add：下面重新取组件引用
    Type oty = Type::unknown();
    for (int i = 0; i < 4; ++i)
      if (tn == builtin_[i]) { oty = Type::of(kBuiltinKinds[i]); break; }
    if (oty.kind == TypeKind::Unknown) mark_unsupported("composite/unresolved outer");
    Component& comp = I_.comps[cid];
    comp.ty = oty;
    comp.class_inst = Inst::empty_node();
    comp.state = CompState::FullyInstantiated;
    return;
  }
  NodeId ty_node = lookup_class_name(el.type.path, enclosing);   // 可能触发 partial_inst/expand → arena add
  if (!ty_node.is_valid() || I_.nodes[ty_node].is_empty()) {
    // v1 的查找是最小版（没做内建枚举 StateSelect/AssertionLevel、import、复合名全解析）——查不到组件类型
    // 时先当 v1 gap 标 unsupported（driver 退 66 记 SKIP），不当硬错误。真正的类型错误等 lookup 完整后再报。
    mark_unsupported("unresolved component type", path_str(el.type.path));
    I_.comps[cid].kind = CompKind::Invalid;
    return;
  }

  // 短类塌陷（R15 EXPANDED_DERIVED）：`type V = Base(mods)` 顺链解析到内建/枚举底，累积属性修饰
  // （外层派生在前 = 优先级高）。lookup_class_name 会 arena add，循环里不缓存 INode 引用（只用 Id）。
  std::vector<Id<SMod>> derived_mods;
  std::vector<NodeId> derived_scopes;   // 各短类定义所在的类（= 短类节点的词法外层）
  // 短类的前缀属性（`type InputReal = input Real`、`type C = constant …`）累积到组件上：
  // 组件自身 > 靠近用处的派生 > 更远派生（loop 外→内，first-non-default 胜）。
  bool alias_partial = false;            // 链上有 partial 的短类
  std::vector<SRestriction> alias_res;   // 链上各短类的受限类型（与链底长类比，见下）
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
    // inner / outer 的短类定义（`outer model M = A;`，InnerOuterClass1 / Missing5 / InvalidMod4 / InvalidType2）：类的
    // inner / outer 配对 v1 未做 → SKIP。
    if (I_.scode.elems[ce].prefixes.io != SInnerOuter::None) mark_unsupported("inner/outer class");
    if (I_.scode.elems[ce].is_partial) alias_partial = true;
    alias_res.push_back(I_.scode.elems[ce].restriction);
    // 派生类型带维度（`type V = Real[3]`，维度在 derived_attr.dims）→ 组件维要拼上类型维（TypeDim1/Subscript4），
    // v1 未做 → SKIP。
    if (!cd.derived_attr.dims.empty()) mark_unsupported("derived-type array dimension");
    if (cd.derived_mod.is_valid()) {
      const SMod& dm = I_.scode.mods[cd.derived_mod];
      if (!dm.subs.empty() || dm.binding.is_valid()) {
        derived_mods.push_back(cd.derived_mod);
        derived_scopes.push_back(I_.nodes[base].parent_scope);
      }
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
    // 短类别名到长类（`model B = A(k = 2); B b;`，MLS §4.5.1：短类定义 = 基类 + 修饰）：实例化的是链底的长类，链上的
    // 修饰记在 derived_mods / derived_scopes，flatten 按最低优先级并进组件的修饰（见 flatten_class 的复合组件分支）。
    // 短类只带名字和修饰，链底长类的性质要在这里补查：
    // - partial：`partial model A …; model B = A; B b;` → omc "Component 'b' has partial type 'B'"（PartialType2；
    //   packages/Import1 / 3 / 4 的 `model E = B.C(x = 6)`）。replaceable 组件也一样（SC 探针 s3）。
    // - 受限类型不一致：组件的种类按短类的受限类型算——`class C2 = C`（C 是连接器）的 C2 不是连接器，Connect3 的
    //   connect 在 1.25.0 报 "c1 is not a valid connector"；`type B = A(k = 2)`（A 是 model）1.25.0 把 b 整个丢掉（SC 探针
    //   s1）。未做 → SKIP。
    const SElement& be = I_.scode.elems[I_.nodes[base].def];
    if (!alias_res.empty() && (be.is_partial || alias_partial)) mark_unsupported("partial type");   // 直接写的类型归 bad_type_path
    for (SRestriction ar : alias_res)
      if (ar != be.restriction) mark_unsupported("short class restriction mismatch");
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
  comp.derived_scopes = std::move(derived_scopes);
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
