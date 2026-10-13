// 实例化 pass 实现（S3 第一里程碑）。锚点见 inst.h 头注与各处 R11 引用。
#include "inst/inst.h"

#include <algorithm>

#include "absyn/absyn_util.h"

namespace ymc {

namespace {
// 内建类型名 ↔ TypeKind（R11 §4.3：Real/Integer/Boolean/String 是硬编码查找的短路名，NFLookup.lookupSimpleBuiltinName
// :803-813 v1.25.0）。Clock 不短路：它是顶层作用域里 NFModelicaBuiltin.mo 声明的类，照常查找（见 make_top_node）。
constexpr int kBuiltinCount = 5;
constexpr int kShortCircuitCount = 4;   // 前 4 个按名短路
constexpr TypeKind kBuiltinKinds[kBuiltinCount] = {TypeKind::Real, TypeKind::Integer, TypeKind::Boolean,
                                                   TypeKind::String, TypeKind::Clock};
const char* builtin_name(int i) {
  switch (i) {
    case 0: return "Real";
    case 1: return "Integer";
    case 2: return "Boolean";
    case 3: return "String";
    case 4: return "Clock";
  }
  return "";
}
Visibility vis_of(SVisibility v) { return v == SVisibility::Protected ? Visibility::Protected : Visibility::Public; }

// 内建枚举类型及其字面量（NFModelicaBuiltin.mo v1.25.0：StateSelect :32-37、AssertionLevel :80；omc 的压平结果打
// `enumeration(never, avoid, default, prefer, always)` / `enumeration(warning, error)`）。
// ponytail: Uncertainty（`uncertain` 属性用）没收，用到再加。
struct BuiltinEnum {
  const char* name;
  std::vector<const char*> literals;
};
const std::vector<BuiltinEnum>& builtin_enum_table() {
  static const std::vector<BuiltinEnum> t = {{"StateSelect", {"never", "avoid", "default", "prefer", "always"}},
                                             {"AssertionLevel", {"warning", "error"}}};
  return t;
}
}  // namespace

// -------------------------------------------------------------- 内建
void Instantiator::make_builtin_types() {
  for (int i = 0; i < kBuiltinCount; ++i) {
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
  for (int i = 0; i < kBuiltinCount; ++i)
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
  // 内建枚举类型：omc 的顶层作用域里有 NFModelicaBuiltin.mo 声明的 `type StateSelect = enumeration(…)` 等（带
  // __OpenModelica_builtin 注解）。合成 SCode 枚举类挂进顶层查找表，`parameter StateSelect s` 才查得到（MSL 示例里 112 个
  // 卡在这里）。用户自己定义了同名顶层类的不覆盖（flatten 按 "class name collides with builtin" SKIP）。
  for (const BuiltinEnum& be : builtin_enum_table()) {
    const Symbol ts = I_.interner.intern(be.name);
    if (tt.lookup.count(ts)) continue;
    SElement e;
    e.kind = SElementKind::Class;
    e.name = ts;
    e.restriction = SRestriction::Type;
    e.def.kind = SClassDefKind::Enumeration;
    for (const char* l : be.literals) e.def.enum_items.push_back(SEnumItem{I_.interner.intern(l), {}});
    Class c;  // NotInstantiated
    INode cn;
    cn.kind = NodeKind::Class;
    cn.name = ts;
    cn.def = I_.scode.elems.add(std::move(e));
    cn.node_type = InstNodeType::of(NodeTypeKind::BuiltinClass);
    cn.cls = I_.classes.add(std::move(c));
    const NodeId cnid = I_.nodes.add(std::move(cn));
    tt.lookup[ts] = LEntry{LEntry::Kind::Class, static_cast<std::uint32_t>(tt.classes.size())};
    tt.classes.push_back(cnid);
    builtin_enums_.push_back(cnid);
  }
  // 内建 Clock 类型（NFModelicaBuiltin.mo 的 `type Clock`，MLS §16.3）：同样挂进顶层查找表——`connector ClockInput =
  // input Clock`（MSL Clocked 的 74 个示例）、`Clock c;` 才查得到。用户的同名顶层类同上不覆盖（flatten 里 SKIP）。
  if (const Symbol cs = I_.nodes[builtin_[4]].name; !tt.lookup.count(cs)) {
    tt.lookup[cs] = LEntry{LEntry::Kind::Class, static_cast<std::uint32_t>(tt.classes.size())};
    tt.classes.push_back(builtin_[4]);
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
  // 给 redeclare_component 合成的组件元素预留位置：实例化中途各层都持着 SElement 引用（inst_component 的 `el` 等），elems
  // 不能搬家——reserve 之后、不超过容量的 add 不重新分配。
  // ponytail: 固定预留 4096 个（按 (原声明, 重声明元素) 缓存，MSL 远用不完），超了 SKIP；要根治得把 SCode 元素表换成不搬家的存储。
  synth_cap_ = I_.scode.elems.size() + 4096;
  I_.scode.elems.reserve(synth_cap_);
}

// -------------------------------------------------------------- 查找（最小版）
NodeId Instantiator::lookup_simple_class(Symbol name, NodeId scope) {
  // 内建名先短路，不看作用域（R11 §4.3、R12 §2.1）。
  for (int i = 0; i < kShortCircuitCount; ++i)
    if (I_.nodes[builtin_[i]].name == name) return builtin_[i];
  // 沿词法外层链逐层查（R12 §2）。每层先看本地类，再看这一层的 import（本地元素 > import，NFClassTree.mo:1654
  // addLocalElementConflict；import 名对本类里嵌套的类可见——它就是本类表里的一项，向外层走自然碰到）。
  NodeId cur = scope;
  while (cur.is_valid() && !I_.nodes[cur].is_empty()) {
    if (I_.nodes[cur].kind == NodeKind::Class && I_.nodes[cur].cls.is_valid()) {
      const ClassTreeId tid = I_.classes[I_.nodes[cur].cls].tree;
      if (tid.is_valid())
        if (const NodeId hit = tree_class(tid, name); !I_.nodes[hit].is_empty()) return hit;
      const NodeId im = import_class(name, cur);   // 可能 partial_inst / expand 别的包 → arena add，上面的引用不再用
      if (im.is_valid() && !I_.nodes[im].is_empty()) return im;
      if (unsupported_) return Inst::empty_node();   // 冲突 / 解析不到的 import：不再往外层找（omc 在这里报错）
      // encapsulated 类是查找的边界：本层（本地 + import）找不到，就只剩内建类（开头已短路），不再往外层走
      //（MLS §5.3.1；NFLookup.lookupSimpleName 的 isEncapsulated 支 → topScope + require_builtin，NFLookup.mo:465-470
      // v1.25.0）。packages/Encapsulated3 的 `extends B.C` 在 encapsulated 的 C2 里查不到 B → omc 报错。
      const Id<SElement> cd = I_.nodes[cur].def;
      if (cd.is_valid() && I_.scode.elems[cd].kind == SElementKind::Class && I_.scode.elems[cd].is_encapsulated) {
        for (NodeId b : builtin_enums_)   // 顶层的内建类照样看得见（require_builtin 支，NFLookup.mo:465-470）
          if (I_.nodes[b].name == name) return b;
        if (I_.nodes[builtin_[4]].name == name) return builtin_[4];   // Clock
        return Inst::empty_node();
      }
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
    if (!I_.trees[pt].lookup.count(name)) continue;
    ++matches;
    found = tree_class(pt, name);
  }
  if (matches >= 2) { mark_unsupported("conflicting imports"); return Inst::empty_node(); }
  if (matches == 1 && (!found.is_valid() || I_.nodes[found].is_empty())) {   // 类型名查到的是组件（常量）→ omc 报错
    mark_unsupported("import names a component, not a class");
    return Inst::empty_node();
  }
  return found;
}

// 类树 tid 里名为 name 的类（本地或继承来的，见 expand）；没有 / 是组件 → empty。经改了基类常量 / 类的 extends、或本类有元素级
// 重声明时继承来的 → 标 SKIP（见 expand）。
NodeId Instantiator::tree_class(ClassTreeId tid, Symbol name) {
  const ClassTree& t = I_.trees[tid];
  const auto it = t.lookup.find(name);
  if (it == t.lookup.end() || it->second.kind != LEntry::Kind::Class) return Inst::empty_node();
  if (t.classes_via_mod.count(it->second.index))
    mark_unsupported("inherited class seen through modifier / redeclare", std::string(I_.interner.str(name)));
  return t.classes[it->second.index];
}

// 进类查名前顺短类链到底：omc 进类前先 instPackage（NFLookup.lookupLocalName :679-742，Inst.instPackage NFInst.mo:1312
// v1.25.0），短类展开成 EXPANDED_DERIVED，类树取基类的（Class.classTree，NFClass.mo:337-350）——`package Medium = P.Water`
// 里查 BaseProperties 等于查 P.Water 的（ADR-007 第 2 阶段 65a，PK 探针）。短类带修饰的：omc 的基类是带着修饰实例化出来的那份
//（包常量 / 类重声明随之改），ymc 的基类节点是共享的未改那份 → SKIP。partial：链上有一层是 partial（派生自 partial 类的短类
// 本身也是 partial，expandClassDerived NFInst.mo:1046-1049）。
NodeId Instantiator::through_short_classes(NodeId cls, bool& partial) {
  NodeId cur = cls;
  for (int guard = 0; guard < 32; ++guard) {
    if (!cur.is_valid() || I_.nodes[cur].is_empty() || I_.nodes[cur].kind != NodeKind::Class || !I_.nodes[cur].def.is_valid())
      return cur;
    const SElement& ce = I_.scode.elems[I_.nodes[cur].def];
    if (ce.kind == SElementKind::Class && ce.is_partial) partial = true;
    if (ce.kind != SElementKind::Class || ce.def.kind != SClassDefKind::Derived) return cur;
    if (ce.def.derived_mod.is_valid()) {
      const SMod& dm = I_.scode.mods[ce.def.derived_mod];
      if (!dm.subs.empty() || dm.binding.is_valid()) {
        mark_unsupported("lookup into modified short class", std::string(I_.interner.str(ce.name)));
        return Inst::empty_node();
      }
    }
    // 递归的短类定义（`class A type Env = Env.Env; …`：基类名查回自己，others/RecursiveShort）→ omc 报 "Recursive short class
    // definition"；正在解析的短类再被解析到 → SKIP（此前无界递归、栈溢出）。
    if (std::find(short_active_.begin(), short_active_.end(), cur.raw) != short_active_.end()) {
      mark_unsupported("recursive short class definition", std::string(I_.interner.str(ce.name)));
      return Inst::empty_node();
    }
    const std::vector<Symbol> bp = ce.def.derived_type.path;   // 拷贝：查找会 arena add
    const NodeId scope = ce.def.derived_type.leading_dot ? I_.top : I_.nodes[cur].parent_scope;
    short_active_.push_back(cur.raw);
    cur = lookup_class_name(bp, scope);
    short_active_.pop_back();
  }
  mark_unsupported("short class chain too deep");
  return Inst::empty_node();
}

NodeId Instantiator::lookup_class_name(const std::vector<Symbol>& path, NodeId scope, std::vector<NodeId>* path_nodes) {
  if (path.empty()) return Inst::empty_node();
  NodeId cur = lookup_simple_class(path[0], scope);
  if (path_nodes && cur.is_valid() && !I_.nodes[cur].is_empty()) path_nodes->push_back(cur);
  for (std::size_t i = 1; i < path.size() && cur.is_valid() && !I_.nodes[cur].is_empty(); ++i) {
    // 进类查名前顺短类链到底（`package Medium = P.Water` 里查 BaseProperties = 查 P.Water 的，见 through_short_classes）。
    bool partial = false;
    cur = through_short_classes(cur, partial);
    if (!cur.is_valid() || I_.nodes[cur].is_empty()) break;
    // partial 类里不许查名（lookupLocalName / lookupLocalNames 进类前的 isPartial 检查，NFLookup.mo:703-706 / :772-777 v1.25.0；
    // PKP 探针：`P.PM.T x`、没被重声明的 `replaceable package M = PM` 里的 `M.T x` → omc "name lookup is not allowed in
    // partial classes" / "contains the definition of a partial class"）→ SKIP。omc 对自引用（首段是书写处的外层类本身）、
    // 要被 redeclare 换掉的元素不查，ymc 一律 SKIP（保守）。
    if (partial) {
      mark_unsupported("lookup in partial class", std::string(I_.interner.str(I_.nodes[cur].name)));
      return Inst::empty_node();
    }
    // 复合名后续段：进入该类内部查（需先 expand 出树）。
    if (I_.node_class(cur).state == ClassState::NotInstantiated ||
        I_.node_class(cur).state == ClassState::PartialClass) {
      partial_inst(cur);
      expand(cur);
    }
    const ClassTreeId ct = I_.node_class(cur).tree;
    cur = ct.is_valid() ? tree_class(ct, path[i]) : Inst::empty_node();
    if (path_nodes && cur.is_valid() && !I_.nodes[cur].is_empty()) path_nodes->push_back(cur);
  }
  return (cur.is_valid() && !I_.nodes[cur].is_empty()) ? cur : Inst::empty_node();
}

// -------------------------------------------------------------- partialInst（建目录）
void Instantiator::partial_inst(NodeId cls) {
  // class extends（`redeclare model extends B(mod) … end B;`）：类树的第一个 extends 预留给原类（ClassTree.fromSCode 的
  // isClassExtends 支，NFClassTree.mo:139-160 v1.25.0：exts[1] 与组件数组首位的 REF；redeclareClass 时 setClassExtends 填上）——
  // 原类的元素排在本体元素之前。ymc 合成一个 extends 元素占这个位（修饰 = class extends 的修饰），基类在 expand 里按外层类记下的
  // 目标取（Inst::class_extends_target），不按名字查。
  std::vector<Id<SElement>> elems = I_.scode.elems[I_.nodes[cls].def].def.elements;   // 拷贝
  if (I_.scode.elems[I_.nodes[cls].def].def.kind == SClassDefKind::ClassExtends) {
    const Id<SElement> cd = I_.nodes[cls].def;
    Id<SElement> xe;
    if (const auto it = ce_ext_elem_.find(cd.raw); it != ce_ext_elem_.end()) {
      xe = it->second;
    } else if (I_.scode.elems.size() >= synth_cap_) {
      mark_unsupported("class extends redeclare", "synthesized element budget");
    } else {
      SElement x;
      x.kind = SElementKind::Extends;
      x.name = I_.scode.elems[cd].name;
      x.base_path = {I_.scode.elems[cd].name};
      x.mod = I_.scode.elems[cd].def.class_extends_mod;
      x.loc = I_.scode.elems[cd].loc;
      xe = I_.scode.elems.add(std::move(x));
      ce_ext_elem_[cd.raw] = xe;
      ce_ext_elems_.insert(xe.raw);
    }
    if (xe.is_valid()) elems.insert(elems.begin(), xe);
  }
  const SElement& el = I_.scode.elems[I_.nodes[cls].def];
  ClassTree tree;
  tree.state = TreeState::PartialTree;
  // v1 只处理 PARTS（长类）；短类/枚举等后续子步。
  for (Id<SElement> eid : elems) {
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

// 同名组件（多重继承、本地声明与继承元素同名）：留先出现的（书写位置靠前），其余须与它相同，否则 omc 报
// DUPLICATE_ELEMENTS_NOT_IDENTICAL → SKIP（addInheritedElementConflict / replaceDuplicates / checkDuplicates，
// NFClassTree.mo:2068-2157 / :726-743 / :1334-1380 v1.25.0）。"相同"照 Component.isIdentical（NFComponent.mo:727-752）：
// 两个都是内建类型才比类型（Class.isIdentical 对长类只比前缀、实例化后恒真，NFClass.mo:475-498），绑定按书写式子比（RAW
// 绑定的 AbsynUtil.expEqual，NFBinding.mo:504-517；extends 修饰已并进绑定）；维、变率、属性、条件都不比。
void Instantiator::dedup_components(std::vector<NodeId>& comps, NodeId def_node) {
  // 组件与（本地或继承来的）嵌套类同名：类对组件恒"不相同"（InstNode.checkIdentical，NFInstNode.mo:1659-1683）→ omc 报错。
  std::unordered_set<std::uint32_t> class_names;
  collect_class_names(def_node, class_names, 0);
  for (NodeId c : comps)
    if (class_names.count(I_.nodes[c].name.id)) {
      mark_unsupported("duplicate elements not identical", std::string(I_.interner.str(I_.nodes[c].name)));
      return;
    }
  std::vector<NodeId> kept;
  std::unordered_map<std::uint32_t, NodeId> first;   // 名字 → 留下的那个
  for (NodeId c : comps) {
    const auto [it, fresh] = first.emplace(I_.nodes[c].name.id, c);
    if (fresh) {
      kept.push_back(c);
      continue;
    }
    if (it->second == c) continue;
    const Component& a = I_.comps[I_.nodes[it->second].comp];
    const Component& b = I_.comps[I_.nodes[c].comp];
    const Type ta = a.ty.is_array() ? *a.ty.array_elem : a.ty, tb = b.ty.is_array() ? *b.ty.array_elem : b.ty;
    auto builtin = [](const Type& t) { return t.kind != TypeKind::Complex && t.kind != TypeKind::Unknown; };
    bool same = !(builtin(ta) && builtin(tb)) || (ta.kind == tb.kind && ta.enum_path == tb.enum_path);
    const bool ra = a.binding.kind == BindingKind::Raw && a.binding.raw_exp.is_valid();
    const bool rb = b.binding.kind == BindingKind::Raw && b.binding.raw_exp.is_valid();
    same = same && ra == rb && (!ra || binding_exp_equal(a.binding.raw_exp, b.binding.raw_exp));
    if (!same) {
      mark_unsupported("duplicate elements not identical", std::string(I_.interner.str(I_.nodes[c].name)));
      return;
    }
  }
  comps = std::move(kept);
}

// AbsynUtil.expEqual（AbsynUtil.mo:2922-2938 v1.25.0）：最外层一个整数、一个实数字面量按数值比（`x = 1` 与 `x = 1.0` 相同，
// DU 探针 d11），其余逐结构比。
bool Instantiator::binding_exp_equal(Id<Exp> a, Id<Exp> b) const {
  const Exp& x = I_.absyn.exps[a];
  const Exp& y = I_.absyn.exps[b];
  if (x.kind == ExpKind::IntLit && y.kind == ExpKind::RealLit) return static_cast<double>(x.ival) == y.rval;
  if (x.kind == ExpKind::RealLit && y.kind == ExpKind::IntLit) return x.rval == static_cast<double>(y.ival);
  return exp_equal(I_.absyn, a, b);
}

// 类的元素里（本地 + 顺 extends 继承来的）嵌套类的名字。顺 expand 解析好的基类走（被同名本地组件挡在查找表外的继承类也要算上）。
void Instantiator::collect_class_names(NodeId def_node, std::unordered_set<std::uint32_t>& out, int depth) {
  if (depth > 50 || !def_node.is_valid() || I_.nodes[def_node].is_empty() || !I_.nodes[def_node].cls.is_valid()) return;
  const ClassTreeId tid = I_.node_class(def_node).tree;
  if (!tid.is_valid()) return;
  for (NodeId k : I_.trees[tid].classes) out.insert(I_.nodes[k].name.id);
  for (std::size_t i = 0; i < I_.trees[tid].ext_bases.size(); ++i) collect_class_names(I_.trees[tid].ext_bases[i], out, depth + 1);
}

// 外部对象类的合法形态（makeExternalObjectType，NFInst.mo:918-990 v1.25.0）：不能有组件；除 ExternalObject 外的 extends
// 只能是空类（图标包）；嵌套类恰是两个非 replaceable 的函数 constructor / destructor。不合法 omc 报错 → SKIP。
void Instantiator::check_ext_object(NodeId cls) {
  const ClassTree& tree = I_.trees[I_.node_class(cls).tree];
  for (NodeId cn : tree.components)
    if (I_.nodes[cn].kind == NodeKind::Component) { mark_unsupported("external object element", std::string(I_.interner.str(I_.nodes[cn].name))); return; }
  for (NodeId ext : tree.exts) {
    const SElement& ee = I_.scode.elems[I_.nodes[ext].def];
    if (ee.base_path.size() == 1 && I_.interner.str(ee.base_path[0]) == "ExternalObject") continue;
    const NodeId b = lookup_class_name(ee.base_path, cls);
    const Id<SElement> bd = b.is_valid() && !I_.nodes[b].is_empty() ? I_.nodes[b].def : Id<SElement>{};
    if (!bd.is_valid()) { mark_unsupported("external object element", "extends"); return; }
    for (Id<SElement> x : I_.scode.elems[bd].def.elements)
      if (I_.scode.elems[x].kind != SElementKind::Import) { mark_unsupported("external object element", "extends"); return; }
  }
  bool ctor = false, dtor = false;
  for (NodeId k : I_.trees[I_.node_class(cls).tree].classes) {
    const SElement& ke = I_.scode.elems[I_.nodes[k].def];
    const std::string_view kn = I_.interner.str(ke.name);
    if ((kn != "constructor" && kn != "destructor") || ke.kind != SElementKind::Class ||
        (ke.restriction != SRestriction::Function && ke.restriction != SRestriction::ExternalFunction)) {
      mark_unsupported("external object element", std::string(kn));
      return;
    }
    if (ke.prefixes.is_replaceable) { mark_unsupported("replaceable external object structor"); return; }
    (kn == "constructor" ? ctor : dtor) = true;
  }
  if (!ctor || !dtor) mark_unsupported("external object structor missing");
}

// -------------------------------------------------------------- expand（解析 extends）
void Instantiator::expand(NodeId cls) {
  Class& c = I_.node_class(cls);
  if (c.state != ClassState::PartialClass) return;
  c.state = ClassState::ExpandedClass;   // 占位（extends 环检测的信号，R11 §3.2）
  ClassTreeId tree_id = I_.node_class(cls).tree;
  // 校验并展开各基类（其树要就绪，inst_extends 才能取到继承组件）。
  const std::vector<NodeId> exts = I_.trees[tree_id].exts;   // 拷一份避免遍历中 arena 变动
  std::vector<NodeId> bases(exts.size(), Inst::empty_node());   // 各 extends 解析到的基类（记进 ext_bases，之后不重查）
  for (std::size_t xi = 0; xi < exts.size(); ++xi) {
    const SElement& ee = I_.scode.elems[I_.nodes[exts[xi]].def];
    // extends 带整体绑定 `extends A = expr` 不合法/未做；子修饰 `extends A(x=1)` 在 inst_class 合并（见下）。
    if (ee.mod.is_valid() && I_.scode.mods[ee.mod].binding.is_valid()) mark_unsupported("extends whole binding");
    // 基类名从**扩展类自身**开始查（MLS §7.1：在扩展类的部分展平形态里查；extends 节点的 parent 是扩展类本身，
    // NFClassTree.mo:202 newExtends(e, parent)、NFInst.mo:775-776 `scope := InstNode.parent(ext)`，v1.25.0）——看得见它
    // 自己的嵌套类（`package Modelica … extends Icons.Library;`），看不见继承来的元素（此时只登记了本地类）。任务卡 T-S3-07。
    std::vector<NodeId> base_nodes;
    NodeId base;
    if (ce_ext_elems_.count(I_.nodes[exts[xi]].def.raw)) {
      // class extends 的隐式基类：外层类 expand 时记下的继承来的同名原类（见下"class extends 的目标"）；按名字查会查到自己。
      const auto it = I_.class_extends_target.find(I_.nodes[cls].def.raw);
      if (it == I_.class_extends_target.end()) {
        mark_unsupported("class extends target not found", std::string(I_.interner.str(I_.nodes[cls].name)));
        continue;
      }
      base = it->second;
    } else {
      base = lookup_class_name(ee.base_path, cls, &base_nodes);
    }
    if (!base.is_valid() || I_.nodes[base].is_empty()) {
      // 查不到同名用户类的 `extends ExternalObject` = 内建的外部对象基类（omc 在 expandExtends 里认出内建基类名
      // "ExternalObject"，NFInst.mo:736-737 v1.25.0）：只记标志，元素的校验在循环之后（check_ext_object）。
      if (ee.base_path.size() == 1 && !ee.base_leading_dot && I_.interner.str(ee.base_path[0]) == "ExternalObject") {
        I_.node_class(cls).is_ext_object = true;
        continue;
      }
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
    bases[xi] = base;
  }
  if (I_.node_class(cls).is_ext_object) check_ext_object(cls);
  // 本地组件登记进 lookup（下标 = def 组件数组里的位置；继承来的组件不登记——压平按实例树找组件）。
  {
    ClassTree& tree = I_.trees[tree_id];
    for (std::uint32_t li : tree.local_components) {
      const INode& cn = I_.nodes[tree.components[li]];
      tree.lookup[cn.name] = LEntry{LEntry::Kind::Component, li};
    }
  }
  // 继承来的类进本类的查找表（ClassTree.expand → expandExtends / addInheritedElement，NFClassTree.mo:349-426 / :1941-2010
  // v1.25.0）：本地元素优先，其次按 extends 先后、先到先得（addInheritedElementConflict 留下标小的；同名的两个类 omc 不比
  // 内容，Class.isIdentical NFClass.mo 只比前缀）；import 不继承。基类名在合并前就都解析完了（上面的循环 = expandExtends 先于
  // ClassTree.expand，R12 p11）。继承类节点与基类共享，词法父亲仍是基类定义；OMC 里父亲是带修饰的 extends 实例——extends 改了
  // 基类的常量 / 类时嵌套类读到改过的值（IC11 探针：`extends Base(n = 3)` 里继承来的 B 读 n 得 3），ymc 读不到 → 记下，用到
  // 时 SKIP（tree_class）。本类有元素级重声明（`redeclare model M2 … end M2;`）同理：OMC 里继承类按 extends 实例查到的是替换后
  // 的类（RedeclareClass2：P2 继承来的 M1 里的 `M2 m` 是 P2 重声明的 M2），ymc 的继承类节点看到的是基类里原来的。
  bool local_redecl = false;
  for (Id<SElement> e : I_.scode.elems[I_.nodes[cls].def].def.elements)
    local_redecl = local_redecl || (I_.scode.elems[e].prefixes.is_redeclare && I_.scode.elems[e].kind != SElementKind::Extends);
  for (std::size_t xi = 0; xi < exts.size(); ++xi) {
    const NodeId base = bases[xi];
    if (I_.nodes[base].is_empty() || !I_.nodes[base].cls.is_valid() || !I_.node_class(base).tree.is_valid()) continue;
    const ClassTreeId bt = I_.node_class(base).tree;
    const bool modded = local_redecl || ext_mod_hits_consts(base, I_.scode.elems[I_.nodes[exts[xi]].def].mod);
    for (std::uint32_t j = 0; j < I_.trees[bt].classes.size(); ++j) {
      const NodeId bc = I_.trees[bt].classes[j];
      const auto bit = I_.trees[bt].lookup.find(I_.nodes[bc].name);
      // 基类里被遮住的同名类（基类本地的那个才在表里）不往下传。
      if (bit == I_.trees[bt].lookup.end() || bit->second.kind != LEntry::Kind::Class || bit->second.index != j) continue;
      ClassTree& tree = I_.trees[tree_id];
      if (tree.lookup.count(I_.nodes[bc].name)) continue;
      const auto idx = static_cast<std::uint32_t>(tree.classes.size());
      tree.lookup[I_.nodes[bc].name] = LEntry{LEntry::Kind::Class, idx};
      tree.classes.push_back(bc);
      if (modded || I_.trees[bt].classes_via_mod.count(j)) tree.classes_via_mod.insert(idx);
    }
  }
  // class extends 的目标：本类的 `model extends B … end B;`（带不带 redeclare 前缀都是）顶替继承来的同名类，隐式 extends 它
  //（redeclareClass 的 isClassExtends 支 → setClassExtends，NFInst.mo:1734-1762 v1.25.0）。基类里没有同名类 → omc 报
  // CLASS_EXTENDS_TARGET_NOT_FOUND（NFClassTree.mo:2246-2249）→ SKIP。
  {
    const std::vector<Id<SElement>> lel = I_.scode.elems[I_.nodes[cls].def].def.elements;   // 拷贝
    for (Id<SElement> e : lel) {
      if (I_.scode.elems[e].kind != SElementKind::Class || I_.scode.elems[e].def.kind != SClassDefKind::ClassExtends) continue;
      const Symbol nm = I_.scode.elems[e].name;
      NodeId target;
      for (NodeId b : bases) {
        if (I_.nodes[b].is_empty() || !I_.nodes[b].cls.is_valid() || !I_.node_class(b).tree.is_valid()) continue;
        if (const NodeId t = tree_class(I_.node_class(b).tree, nm); !I_.nodes[t].is_empty()) { target = t; break; }
      }
      if (!target.is_valid() || I_.nodes[target].is_empty()) {
        mark_unsupported("class extends target not found", std::string(I_.interner.str(nm)));
        continue;
      }
      I_.class_extends_target[e.raw] = target;
    }
  }
  I_.trees[tree_id].ext_bases = std::move(bases);
  I_.trees[tree_id].state = TreeState::ExpandedTree;
}

// extends 修饰 m 改到了基类 base（含它继承来的）的常量或类：嵌套类按词法读得到它们（外层作用域里查到的组件须是常量，
// MLS §5.3.1；IC10 探针：读外层的 parameter → omc "found in an enclosing scope but is not a constant"）。名字找不到 → 算改到了
//（保守）。
bool Instantiator::ext_mod_hits_consts(NodeId base, Id<SMod> m) {
  if (!m.is_valid()) return false;
  for (const SSubMod& sm : I_.scode.mods[m].subs) {
    const Id<SElement> e = member_def(base, sm.ident, 0);
    if (!e.is_valid() || I_.scode.elems[e].kind != SElementKind::Component ||
        I_.scode.elems[e].attr.var == SVariability::Const)
      return true;
  }
  return false;
}

// 已 expand 的类 cls 自己声明的、或顺 extends 继承来的、名为 name 的元素（类 / 组件）的 SCode 声明；没有 → invalid。
Id<SElement> Instantiator::member_def(NodeId cls, Symbol name, int depth) {
  if (depth > 50 || I_.nodes[cls].is_empty() || !I_.nodes[cls].def.is_valid()) return {};
  for (Id<SElement> e : I_.scode.elems[I_.nodes[cls].def].def.elements) {
    const SElement& el = I_.scode.elems[e];
    if ((el.kind == SElementKind::Component || el.kind == SElementKind::Class) && el.name == name) return e;
  }
  if (!I_.nodes[cls].cls.is_valid() || !I_.node_class(cls).tree.is_valid()) return {};
  const ClassTreeId t = I_.node_class(cls).tree;
  for (std::size_t i = 0; i < I_.trees[t].ext_bases.size(); ++i)
    if (const Id<SElement> e = member_def(I_.trees[t].ext_bases[i], name, depth + 1); e.is_valid()) return e;
  return {};
}

// -------------------------------------------------------------- instClass（克隆 + 建 FlatTree）
NodeId Instantiator::instantiate(NodeId def_node, NodeId parent, const ClassRedecls* redecls) {
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
  // 类重声明（派生类的元素级 `redeclare model B … end B;`，NFInst.redeclareClass :1707-1801 v1.25.0）：OMC 在实例的类树里用新类
  // 节点顶替原来的（Mutable 槽，基类的视图同步），继承来的组件的类型于是查到新类。ymc 给这个实例复制一份定义类树、把那些名字的项
  // 换成新类节点；被顶替的原类记进 Inst::redeclared_classes（flatten 的 SCode 查找撞到 → SKIP）。
  bool specialized = false;
  std::vector<std::uint32_t> replaced;   // 顶替掉的原类（实例建好后记进 Inst::replaced_in）
  if (redecls && !redecls->empty() && inst_c.tree.is_valid()) {
    ClassTree t = I_.trees[inst_c.tree];   // 拷贝
    for (const auto& [n, node] : *redecls) {
      const auto it = t.lookup.find(n);
      if (it == t.lookup.end() || it->second.kind != LEntry::Kind::Class || t.classes[it->second.index] == node) continue;
      if (!class_redeclare_ok(t.classes[it->second.index], node)) break;
      if (const Id<SElement> od = I_.nodes[t.classes[it->second.index]].def; od.is_valid()) {
        I_.redeclared_classes.insert(od.raw);
        replaced.push_back(od.raw);
      }
      t.classes[it->second.index] = node;
      t.classes_via_mod.erase(it->second.index);
      specialized = true;
    }
    if (specialized) inst_c.tree = I_.trees.add(std::move(t));
  }
  INode inst_n = I_.nodes[def_node];                   // 拷贝节点（parent_scope 保留词法外层）
  inst_n.cls = I_.classes.add(std::move(inst_c));
  inst_n.instance_parent = parent;
  NodeId inst_node = I_.nodes.add(std::move(inst_n));
  if (specialized) {
    inst_redecls_[inst_node.raw] = *redecls;
    I_.replaced_in[inst_node.raw].insert(replaced.begin(), replaced.end());
  }
  inst_class(inst_node, def_node, redecls);
  --depth_;
  return inst_node;
}

// 类 cls 的体里（组件类型、extends 名、短类的基类、嵌套类递归）首段写到 r 里的某个名字。
bool Instantiator::mentions_any(Id<SElement> cls, const ClassRedecls& r, int depth) const {
  if (depth > 30 || !cls.is_valid() || I_.scode.elems[cls].kind != SElementKind::Class) return false;
  auto hit = [&](const std::vector<Symbol>& path) {
    if (path.empty()) return false;
    for (const auto& [n, node] : r)
      if (n == path[0]) return true;
    return false;
  };
  const SClassDef& d = I_.scode.elems[cls].def;
  if (d.kind == SClassDefKind::Derived && hit(d.derived_type.path)) return true;
  for (Id<SElement> e : d.elements) {
    const SElement& el = I_.scode.elems[e];
    if (el.kind == SElementKind::Component && hit(el.type.path)) return true;
    if (el.kind == SElementKind::Extends && hit(el.base_path)) return true;
    if (el.kind == SElementKind::Class && mentions_any(e, r, depth + 1)) return true;
  }
  return false;
}

// cls 的词法外层链（parent_scope）经过实例 inst 的类定义或它的某层基类定义——即 cls 是在 inst 的继承层级里声明的嵌套类。
bool Instantiator::nested_in(NodeId cls, NodeId inst) const {
  std::unordered_set<std::uint32_t> defs;
  std::vector<NodeId> todo{inst};
  for (int guard = 0; !todo.empty() && guard < 256; ++guard) {
    const NodeId n = todo.back();
    todo.pop_back();
    if (!n.is_valid() || I_.nodes[n].is_empty() || !I_.nodes[n].cls.is_valid()) continue;
    if (I_.nodes[n].def.is_valid()) defs.insert(I_.nodes[n].def.raw);
    const ClassTreeId t = I_.classes[I_.nodes[n].cls].tree;
    if (!t.is_valid()) continue;
    for (NodeId b : I_.trees[t].ext_bases) todo.push_back(b);
    for (NodeId b : I_.trees[t].exts) todo.push_back(b);
  }
  for (NodeId p = I_.nodes[cls].parent_scope; p.is_valid() && !I_.nodes[p].is_empty(); p = I_.nodes[p].parent_scope)
    if (I_.nodes[p].def.is_valid() && defs.count(I_.nodes[p].def.raw)) return true;
  return false;
}

// 类 orig 被 repl 顶替合不合法（omc 报错 → SKIP）：
//   - orig 是枚举（redeclareClass 的 PARTIAL_BUILTIN 支 → redeclareEnum，NFInst.mo v1.25.0）：`enumeration(:)` 只收枚举类型（直接
//     写的或短类别名到枚举）；带字面量的只收直接写的、字面量表相同的枚举（RedeclareEnum2 / 4 / 6：多了字面量、换成 Real → 报
//     "not a subtype"）；
//   - orig 是 outer 类：重声明修饰落在 outer 元素上（InnerOuterInvalidMod5："Modifier … found on outer element"）。
bool Instantiator::class_redeclare_ok(NodeId orig, NodeId repl) {
  const Id<SElement> od = I_.nodes[orig].def;
  if (!od.is_valid()) return true;
  const SElement& oe = I_.scode.elems[od];
  if (oe.prefixes.io == SInnerOuter::Outer || oe.prefixes.io == SInnerOuter::InnerOuter) {
    mark_unsupported("modifier on outer element");
    return false;
  }
  if (oe.kind != SElementKind::Class || oe.def.kind != SClassDefKind::Enumeration) return true;
  // repl 顺短类链到底（`type E = E2`），记下直接写的那个是不是枚举。
  NodeId cur = repl;
  bool direct = true;
  for (int guard = 0; guard < 20 && cur.is_valid() && !I_.nodes[cur].is_empty() && I_.nodes[cur].def.is_valid(); ++guard) {
    const SElement& ce = I_.scode.elems[I_.nodes[cur].def];
    if (ce.kind != SElementKind::Class || ce.def.kind != SClassDefKind::Derived) break;
    const std::vector<Symbol> bp = ce.def.derived_type.path;   // 拷贝：查找会 arena add
    cur = lookup_class_name(bp, I_.nodes[cur].parent_scope);
    direct = false;
  }
  const Id<SElement> rd = cur.is_valid() && !I_.nodes[cur].is_empty() ? I_.nodes[cur].def : Id<SElement>{};
  const bool repl_enum = rd.is_valid() && I_.scode.elems[rd].kind == SElementKind::Class &&
                         I_.scode.elems[rd].def.kind == SClassDefKind::Enumeration;
  bool ok = repl_enum;
  if (ok && !oe.def.enum_unspecified) {
    const std::vector<SEnumItem>& a = oe.def.enum_items;
    const std::vector<SEnumItem>& b = I_.scode.elems[rd].def.enum_items;
    ok = direct && a.size() == b.size();
    for (std::size_t i = 0; ok && i < a.size(); ++i) ok = a[i].literal == b[i].literal;
  }
  if (!ok) mark_unsupported("enumeration redeclare not a subtype", std::string(I_.interner.str(oe.name)));
  return ok;
}

// 修饰里的类重声明元素（`redeclare model B = X(p = 4)`、长形态 `redeclare model B … end B`）的类节点：词法作用域 = 写修饰的类
//（R15 §1：重声明元素的父亲是写修饰处；新类的修饰 `p = 4` 在那里解析），按元素缓存（同一元素总写在同一处）。
NodeId Instantiator::redecl_class_node(Id<SElement> rel, NodeId scope) {
  if (const auto it = redecl_class_nodes_.find(rel.raw); it != redecl_class_nodes_.end()) return it->second;
  INode cn;
  cn.kind = NodeKind::Class;
  cn.name = I_.scode.elems[rel].name;
  cn.def = rel;
  cn.parent_scope = scope;
  cn.visibility = vis_of(I_.scode.elems[rel].prefixes.vis);
  cn.node_type = InstNodeType::normal_class();
  cn.cls = I_.classes.add(Class{});
  const NodeId n = I_.nodes.add(std::move(cn));
  redecl_class_nodes_[rel.raw] = n;
  return n;
}

// 修饰 m（写在 scope 这个类里）中的类重声明子修饰追加进 r：已有的名字不覆盖（外层的修饰先收 = 外层优先，Modifier.merge）。
// 施加的子修饰记进 applied_redecls（flatten 不再把它当修饰）。class extends 形态未做、base（被修饰的类，含它继承来的）里没有
// 这个名字（omc 报错）→ SKIP。
void Instantiator::collect_class_redecls(Id<SMod> m, NodeId scope, NodeId base, ClassRedecls& r) {
  if (!m.is_valid()) return;
  // base 的继承元素要等它展开（member_def 顺 ext_bases 找）。
  if (I_.nodes[base].cls.is_valid() && I_.node_class(base).state == ClassState::NotInstantiated) partial_inst(base);
  if (I_.nodes[base].cls.is_valid() && I_.node_class(base).state == ClassState::PartialClass) expand(base);
  const std::vector<SSubMod> subs = I_.scode.mods[m].subs;   // 拷贝：建类节点会往 arena add
  for (const SSubMod& s : subs) {
    const SMod& sm = I_.scode.mods[s.mod];
    if (sm.kind != SModKind::Redecl || !sm.element.is_valid() || I_.scode.elems[sm.element].kind != SElementKind::Class) continue;
    if (I_.scode.elems[sm.element].def.kind == SClassDefKind::ClassExtends) { mark_unsupported("class extends redeclare"); continue; }
    if (!member_def(base, s.ident, 0).is_valid()) {
      mark_unsupported("redeclare of missing element", std::string(I_.interner.str(s.ident)));
      continue;
    }
    bool have = false;
    for (const auto& [n, node] : r) have = have || n == s.ident;
    if (!have) r.push_back({s.ident, redecl_class_node(sm.element, scope)});
    I_.applied_redecls.insert(s.mod.raw);
  }
}

void Instantiator::inst_class(NodeId inst_node, NodeId def_node, const ClassRedecls* redecls) {
  ClassTreeId def_tree_id = I_.node_class(def_node).tree;
  // 传给各基类实例的类重声明集 = 外来的（更外层派生类的，优先）+ 本类自己的元素级类重声明（`redeclare model B … end B;` /
  // `redeclare model B = X;`，本类查找表里的本地项，词法作用域 = 本类，R15 §1）。class extends 形态（`redeclare model extends B`）
  // 未做；重声明了基类里没有的类（omc 报错）→ SKIP。
  ClassRedecls base_redecls = redecls ? *redecls : ClassRedecls{};
  if (def_tree_id.is_valid() && I_.nodes[def_node].def.is_valid()) {
    const std::vector<Id<SElement>> elems = I_.scode.elems[I_.nodes[def_node].def].def.elements;   // 拷贝
    for (Id<SElement> e : elems) {
      const SElement& el = I_.scode.elems[e];
      // class extends（`model extends B … end B;`，带不带 redeclare 前缀）同样顶替继承来的同名类（新类隐式 extends 原类，见 expand）。
      if (el.kind != SElementKind::Class || (!el.prefixes.is_redeclare && el.def.kind != SClassDefKind::ClassExtends)) continue;
      bool outer = false;
      for (const auto& [n, node] : base_redecls) outer = outer || n == el.name;
      if (outer) continue;
      bool inherited = false;
      for (NodeId b : I_.trees[def_tree_id].ext_bases)
        inherited = inherited || (!I_.nodes[b].is_empty() && member_def(b, el.name, 0).is_valid());
      if (!inherited) { mark_unsupported("redeclare of missing element", std::string(I_.interner.str(el.name))); continue; }
      const auto it = I_.trees[def_tree_id].lookup.find(el.name);
      if (it == I_.trees[def_tree_id].lookup.end() || it->second.kind != LEntry::Kind::Class) continue;
      base_redecls.push_back({el.name, I_.trees[def_tree_id].classes[it->second.index]});
    }
  }
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
      // expand 解析好的基类（从扩展类自身、只看本地类查的，T-S3-07）；此时再查会看到继承来的同名类。
      const NodeId base_def = ext_i < I_.trees[def_tree_id].ext_bases.size() ? I_.trees[def_tree_id].ext_bases[ext_i]
                                                                              : Inst::empty_node();
      if (!base_def.is_valid() || I_.nodes[base_def].is_empty()) continue;  // expand 已报错
      // extends 内建类型（extends Real 之类）v1 还没做，标 unsupported 并跳过（避免实例化无类体的内建节点）。
      bool base_builtin = false;
      for (int k = 0; k < kBuiltinCount; ++k)
        if (base_def == builtin_[k]) base_builtin = true;
      if (base_builtin) { mark_unsupported("extends builtin type"); continue; }
      // extends 短类/枚举/derived 基类（`model B = A; extends B`）v1 未做，标 unsupported。
      Id<SElement> bdef = I_.nodes[base_def].def;
      if (!bdef.is_valid() || (I_.scode.elems[bdef].def.kind != SClassDefKind::Parts &&
                               I_.scode.elems[bdef].def.kind != SClassDefKind::ClassExtends)) {
        mark_unsupported("extends short/enum/derived class", path_str(ee.base_path));
        continue;
      }
      const Id<SMod> ext_mod = ee.mod;   // extends 修饰（在 base_def lookup 之后取，避免跨 add 持 SElement 引用）
      // extends 修饰里的类重声明（`extends A(redeclare model B = X)`）：施加到这个基类实例的类树（排在本类的元素级重声明之后——
      // 同名的照那个），新类的词法作用域 = 本类（写修饰处）。
      ClassRedecls ext_redecls = base_redecls;
      collect_class_redecls(ext_mod, def_node, base_def, ext_redecls);
      NodeId base_inst = instantiate(base_def, inst_node, ext_redecls.empty() ? nullptr : &ext_redecls);
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
          bool found = I_.applied_redecls.count(s.mod.raw) > 0;   // 施加过的类重声明（见上）
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
      // 元素级组件重声明等基类都展开了再与继承来的原组件合并（见 redeclare_local_components）。
      const bool rdcl = I_.scode.elems[I_.comps[I_.nodes[icomp].comp].def].prefixes.is_redeclare;
      if (!rdcl) inst_component(icomp, inst_node);
      flat.components.push_back(icomp);
    }
  }
  redeclare_local_components(flat, inst_node);
  dedup_components(flat.components, def_node);
  // 实例的类树照样带定义里的类（本地 + 继承来的；OMC 的 INSTANTIATED_TREE 保留 classes 与查找表）：实例化完了还要从实例查类型
  // 的（生成的 inner 在 outer 声明处的类实例里查类型，InnerOuterMissing8 的 `outer B b`，B 是那个类的嵌套类）。组件项的下标
  // 对不上 FlatTree 的组件数组，不搬。
  {
    const ClassTree& dt = I_.trees[def_tree_id];
    flat.classes = dt.classes;
    flat.classes_via_mod = dt.classes_via_mod;
    for (const auto& [n, le] : dt.lookup)
      if (le.kind == LEntry::Kind::Class) flat.lookup.emplace(n, le);
  }
  Class& ic = I_.classes[I_.nodes[inst_node].cls];
  ic.tree = I_.trees.add(std::move(flat));
  ic.state = ClassState::InstancedClass;
}

// extends 修饰合并到继承组件（R15 §3.2(b)：extends 修饰当作外来修饰压进基类元素）：绑定 / final 直接改组件，子修饰（叶的
// 属性、复合组件的成员修饰）记进 extends_mods 由 flatten 往下传；组件重声明见 redeclare_component；break 未做 → unsupported。
// 不做 arena add：只改已存在的 comp 字段（绑定/属性引用留给 flatten 在派生作用域解析）。
void Instantiator::apply_extends_modifier(NodeId bc, Id<SMod> mod, NodeId scope) {
  if (!mod.is_valid()) return;
  const Symbol name = I_.nodes[bc].name;
  const CompId cid = I_.nodes[bc].comp;
  // 改 final 元素（声明带 final 前缀、或更内层的 extends 修饰给了 final）→ omc 报错：声明的 final 前缀并进声明修饰
  //（Modifier.patchElementModFinal，NFModifier.mo:291-319 v1.25.0），外层修饰与之合并时 checkFinalOverride（:399 / :691-709）。
  // E 探针 e20 / e21 / e118（此前叶组件被放过）。同一 extends 修饰里的兄弟子修饰是同层（mergeLocal），不算 → 先取值。
  const bool final_before = I_.comps[cid].attributes.is_final;
  // 同一 extends 修饰里同名的子修饰有重声明（`extends M(redeclare B a, a(p = 9))`）→ omc 报 DUPLICATE_MODIFICATIONS：
  // Modifier.mergeLocal 只合并两个普通修饰（NFModifier.mo:711-756 v1.25.0；rx-r1 的 R124，复合组件的 extends 修饰放开后露出）。
  {
    int hits = 0;
    bool redecl = false;
    for (const SSubMod& s : I_.scode.mods[mod].subs)
      if (s.ident == name) {
        ++hits;
        redecl = redecl || I_.scode.mods[s.mod].kind != SModKind::Mod;
      }
    if (hits > 1 && redecl) { mark_unsupported("duplicate modification"); return; }
  }
  for (const SSubMod& s : I_.scode.mods[mod].subs) {
    if (s.ident != name) continue;
    const SMod& sm = I_.scode.mods[s.mod];
    if (sm.kind == SModKind::Redecl && sm.element.is_valid()) {
      redeclare_component(bc, sm.element, scope);
      continue;
    }
    if (sm.kind != SModKind::Mod) { mark_unsupported("extends redeclare/break modifier"); return; }
    if (final_before) { mark_unsupported("final element override", std::string(I_.interner.str(name))); return; }
    Component& comp = I_.comps[cid];
    if (sm.binding.is_valid()) {   // 绑定覆盖（extends A(x = 1)）——值在派生作用域解析
      comp.binding.kind = BindingKind::Raw;
      comp.binding.raw_exp = sm.binding;
      comp.binding.scope = scope;
    }
    if (sm.is_final) comp.attributes.is_final = true;
    // 子修饰（叶的属性 `extends A(x(start = 2))`、复合组件的成员 `extends A(c(x = 2))`）：比组件自己声明的修饰外一层（修饰
    // 由内向外 merge：声明 → 各层 extends → 实例修饰），flatten 排在声明修饰之前（EX 探针 EX1：A 里 `Real x(start = 1)`，omc
    // 取 2.0；E 探针 e11：A 里 `C c(x = 3)`，omc 取 2.0）。多层继承时外层的 extends 后施加（基类实例先建好），插到前面 = 外层
    // 在前。此前混进 derived_mods（类型链修饰，排在声明修饰之后），优先级反了。
    if (!sm.subs.empty()) {
      comp.extends_mods.insert(comp.extends_mods.begin(), s.mod);
      comp.extends_scopes.insert(comp.extends_scopes.begin(), scope);
    }
  }
}

// extends 修饰里的组件重声明 `extends M(redeclare B a(q = 3))`（MLS §7.3；NFInst.instComponent 的 REDECLARE 支与
// redeclareComponent，NFInst.mo:1894-1904 / :2153-2232 v1.25.0）：
//   - 新组件 = 重声明元素，缺的从原声明补（redeclareComponent :2208-2224 + Attributes.mergeRedeclaredComponentAttributes，
//     NFAttributes.mo:275-347）：维（重声明不带维 → 原声明的，R 探针 r16 / r119）、条件（总用原声明的，r13 / r14；重声明自己
//     带条件 → omc 报错 REDECLARE_CONDITION）、变率 / 方向 / 流前缀（重声明没写 → 原声明的，r17 / r18 / r23；写了不同的 →
//     omc 报错，变率只许往常量方向降，r19 / r110 / r111）、注释与注解（原声明的，:2224 `cmt := orig_comp.comment`）。合成一个
//     元素当组件的定义（类型名、修饰在写 extends 的类里查 / 解析）；
//   - 约束修饰（并在重声明自己的修饰之后，外层在前）：重声明元素自己 constrainedby 的修饰（没有时：被替掉的内层重声明的
//     constrainedby 修饰，Modifier.merge 的 REDECLARE 支 :428-430，r22）> 原声明的约束修饰（有 constrainedby 取它的修饰、原声明
//     的修饰就不用了，否则原声明的修饰；NFInst.getConstrainingMod :2064-2083、SCodeUtil.getConstrainingMod :5273-5286；r10 /
//     r11 / r212）；
//   - 绑定：重声明的 > 原声明的（:2203-2206）；
//   - 外层又重声明一次：整个替掉内层的（merge 的 `case (REDECLARE(), _) then outerMod` :432），原声明仍是最初那个（r115 / r21：
//     内层 `B a(q = 4)` 的 q 不见了）；
//   - 原声明不是 replaceable 而类型不同 → omc 报错 REDECLARE_NON_REPLACEABLE（:2193-2199）。
// 约束修饰按写 extends 的类的作用域解析（omc 在原声明所在的基类里解析）：它引用的多是基类的参数，继承下来名字、前缀都一样。
// ponytail: 基类所在包里的常量 / 类型这类只在基类词法作用域可见的名字会解析不到（→ SKIP），要精确得让修饰带各自的作用域。
Id<SElement> Instantiator::type_extends_only(Id<SElement> ce) const {
  const SElement& c = I_.scode.elems[ce];
  if (c.kind != SElementKind::Class || c.restriction != SRestriction::Type || c.def.kind != SClassDefKind::Parts ||
      c.prefixes.io != SInnerOuter::None || !c.def.equations.empty() || !c.def.initial_equations.empty() ||
      !c.def.algorithms.empty() || !c.def.initial_algorithms.empty() || c.def.external.present)
    return {};
  // 只许一个 extends；嵌套的类（MSL Frames.TransformationMatrices.Orientation 里的 equalityConstraint 函数）不影响它当短类用。
  // 有 protected 元素：omc 报 "Protected sections are not allowed in type"（declarations/TypeRestriction3）→ 不当短类，照旧 SKIP。
  Id<SElement> ext;
  for (Id<SElement> e : c.def.elements) {
    const SElementKind k = I_.scode.elems[e].kind;
    if (I_.scode.elems[e].prefixes.vis == SVisibility::Protected) return {};
    if (k == SElementKind::Class) continue;
    if (k != SElementKind::Extends || ext.is_valid()) return {};
    ext = e;
  }
  return ext;
}

// 重声明元素 rel 与原声明 od 合成新组件的定义（规则见上：维 / 条件 / 前缀 / 注释取原声明的）。bc = 被顶替的组件（类型名撞组件
// 时看它所在的基类实例），scope = 写重声明的类。不合法 / 未做的形态 → mark_unsupported，返回无效 id。
Id<SElement> Instantiator::merge_redeclared_elem(NodeId bc, Id<SElement> od, Id<SElement> rel, NodeId scope) {
  // 拷贝：下面要往 elems 里 add 合成元素（预留容量内不搬家，拷贝只图省心）；组件元素没有类体，拷贝便宜。
  const SElement o = I_.scode.elems[od];
  const SElement r = I_.scode.elems[rel];
  if (r.kind != SElementKind::Component) { mark_unsupported("class redeclare in extends modifier"); return {}; }
  // v1 未做的形态 → SKIP：inner / outer。
  if (o.prefixes.io != SInnerOuter::None || r.prefixes.io != SInnerOuter::None) {
    mark_unsupported("redeclare form", "inner/outer");
    return {};
  }
  if (r.condition.is_valid()) { mark_unsupported("redeclare with condition"); return {}; }   // omc 报错
  // 重声明元素的类型名首段先撞上组件（被重声明的组件同名 `redeclare B B`、本类 / 基类的组件）：omc 的查找先找到组件 →
  // "Found a component with same name when looking for type B"（others/ComponentNamesRedecl）→ SKIP。
  if (!r.type.leading_dot && !r.type.path.empty()) {
    const Symbol tn = r.type.path[0];
    bool hits = tn == I_.nodes[bc].name;
    const NodeId base_inst = I_.nodes[bc].parent_scope;
    if (base_inst.is_valid() && !I_.nodes[base_inst].is_empty() && I_.node_class(base_inst).tree.is_valid())
      for (NodeId c : I_.trees[I_.node_class(base_inst).tree].components) hits = hits || I_.nodes[c].name == tn;
    if (I_.node_class(scope).tree.is_valid()) {
      const ClassTree& st = I_.trees[I_.node_class(scope).tree];
      auto it = st.lookup.find(tn);
      hits = hits || (it != st.lookup.end() && it->second.kind == LEntry::Kind::Component);
    }
    if (hits) { mark_unsupported("type name resolves to a component"); return {}; }
  }
  if (!o.prefixes.is_replaceable) {
    const NodeId ot = lookup_class_name(o.type.path, I_.nodes[bc].parent_scope);
    const NodeId nt = lookup_class_name(r.type.path, scope);
    if (ot != nt) { mark_unsupported("redeclare of non-replaceable component"); return {}; }
  }
  // 前缀合并（mergeRedeclaredComponentAttributes）：重声明写了的用它的（与原声明的冲突 → omc 报错），没写的取原声明的。
  // 变率：ymc 的枚举序 Var < Discrete < Param < Const（越大越常量），重声明比原声明更"变" → 报错。
  SElement m = r;
  m.condition = o.condition;
  if (r.attr.dims.empty() && r.type.dims.empty()) {
    m.attr.dims = o.attr.dims;
    m.type.dims = o.type.dims;
  }
  bool clash = false;
  if (r.attr.var != SVariability::Var)
    clash = clash || static_cast<std::uint8_t>(r.attr.var) < static_cast<std::uint8_t>(o.attr.var);
  else
    m.attr.var = o.attr.var;
  if (r.attr.dir != SDirection::Bidir)
    clash = clash || (o.attr.dir != SDirection::Bidir && o.attr.dir != r.attr.dir);
  else
    m.attr.dir = o.attr.dir;
  if (r.attr.ct != SConnectorType::Potential)
    clash = clash || (o.attr.ct != SConnectorType::Potential && o.attr.ct != r.attr.ct);
  else
    m.attr.ct = o.attr.ct;
  if (clash) { mark_unsupported("redeclare prefix mismatch"); return {}; }
  m.cmt = o.cmt;
  // 合成元素按 (原声明, 重声明元素) 缓存——同一处 extends 每实例化一次就走一遍这里。
  const std::uint64_t key = (static_cast<std::uint64_t>(od.raw) << 32) | rel.raw;
  if (auto it = redecl_elems_.find(key); it != redecl_elems_.end()) return it->second;
  if (I_.scode.elems.size() >= synth_cap_) { mark_unsupported("redeclare form", "synthesized element budget"); return {}; }
  const Id<SElement> mid = I_.scode.elems.add(std::move(m));
  redecl_elems_.emplace(key, mid);
  return mid;
}

void Instantiator::redeclare_component(NodeId bc, Id<SElement> rel, NodeId scope) {
  const CompId cid = I_.nodes[bc].comp;
  const Id<SElement> prev = I_.comps[cid].def;   // 当前定义（被内层 extends 重声明过 = 那次合成的元素）
  const Id<SElement> od = I_.comps[cid].orig_def.is_valid() ? I_.comps[cid].orig_def : prev;   // 最初的原声明
  // 先被更内层的 extends 修饰改过（omc 把那些修饰丢掉，r117：`N extends M(a(p = 2))`、再 `extends N(redeclare B a)` → a.p 取
  // 原声明的 9）→ SKIP。
  if (!I_.comps[cid].extends_mods.empty()) { mark_unsupported("redeclare form", "after extends modifier"); return; }
  const Id<SElement> mid = merge_redeclared_elem(bc, od, rel, scope);
  if (!mid.is_valid()) return;
  const SElement& o = I_.scode.elems[od];
  const SElement& r = I_.scode.elems[rel];
  // 约束修饰，外层在前（见上）。
  std::vector<Id<SMod>> cc;
  if (r.prefixes.has_cc) cc.push_back(r.prefixes.cc.mod);
  else if (prev != od && I_.scode.elems[prev].prefixes.has_cc) cc.push_back(I_.scode.elems[prev].prefixes.cc.mod);
  cc.push_back(o.prefixes.has_cc ? o.prefixes.cc.mod : o.mod);
  std::erase_if(cc, [](Id<SMod> x) { return !x.is_valid(); });
  const bool r_bound = r.mod.is_valid() && I_.scode.mods[r.mod].binding.is_valid();
  const Id<Exp> o_binding = o.mod.is_valid() ? I_.scode.mods[o.mod].binding : Id<Exp>{};
  // 取自原声明的条件 / 维的书写处（见 Component::cond_scope）：原组件的（inst_component 会把 decl_scope 换成 scope）。原组件
  // 已被内层重声明过、那次用了自己的维，这次又要原声明的（非字面量）维：原声明写在哪不知道 → SKIP。
  const bool take_dims = r.attr.dims.empty() && r.type.dims.empty();
  const NodeId old_decl = I_.comps[cid].decl_scope;
  const NodeId cond_at = I_.comps[cid].cond_scope.is_valid() ? I_.comps[cid].cond_scope : old_decl;
  const NodeId dims_at = I_.comps[cid].dims_scope.is_valid() ? I_.comps[cid].dims_scope : old_decl;
  // 字面量维在哪求都一样（rf 探针 R33：`A a[2]` → 中间一层 `B a[3]` → `redeclare C a` 取 a[2]）。
  auto literal = [&](const std::vector<Id<Exp>>& ds) {
    return std::all_of(ds.begin(), ds.end(), [&](Id<Exp> d) { return I_.absyn.exps[d].kind == ExpKind::IntLit; });
  };
  if (take_dims && prev != od && !I_.comps[cid].dims_scope.is_valid() && (!literal(o.attr.dims) || !literal(o.type.dims))) {
    mark_unsupported("redeclare form", "original dimensions through redeclare chain");
    return;
  }
  {
    Component& comp = I_.comps[cid];
    comp.orig_def = od;
    comp.cc_mods = std::move(cc);
    comp.def = mid;
    comp.derived_mods.clear();
    comp.derived_scopes.clear();
    comp.binding = Binding{};
    comp.state = CompState::CompDef;
  }
  inst_component(bc, scope);
  I_.comps[cid].cond_scope = cond_at;
  I_.comps[cid].dims_scope = take_dims ? dims_at : NodeId{};
  if (!r_bound && o_binding.is_valid()) {   // 重声明没写绑定：取原声明的
    Component& comp = I_.comps[cid];
    comp.binding.kind = BindingKind::Raw;
    comp.binding.raw_exp = o_binding;
    comp.binding.scope = I_.nodes[bc].parent_scope;   // 原声明所在的基类实例
  }
}

// 元素级组件重声明（`extends A; redeclare Real x = 2.0;`，MLS §7.3）：照 redeclareElements / redeclareComponentElement
//（NFInst.mo:1649-1705 v1.25.0）——本类的重声明元素顶替继承来的同名组件，留在它自己的位置（ClassTree.flatten 去重时留下
// REDECLARE 项本身、继承来的是它的子项，NFClassTree.mo:2150-2190 / :2225-2260；ER 探针 ER1 / ER12）。合并照 redeclareComponent
//（merge_redeclared_elem）；与修饰里的重声明不同，约束修饰为空（redeclareComponentElement 传 NOMOD）：原声明的修饰、改原组件
// 的 extends 修饰都不带过来（ER2 `max = 5.0` 没有原声明的 start / min，ER4 extends 的 `start = 3` 也没有），只有绑定照
// "外来修饰 > 重声明的 > 原组件的（含 extends 修饰给的）"（ER15 `extends A(x = 7)` → 7.0）。原组件已被基类里的重声明顶替过
// 就以那个为原声明（链，ER13 注释仍是最初的 "orig"）。本类没有继承来的同名组件 → omc 报 REDECLARE_NONEXISTING_ELEMENT（ER9）
// → SKIP；继承来不止一个（多重继承的重复项）未做 → SKIP。
void Instantiator::redeclare_local_components(ClassTree& flat, NodeId inst_node) {
  std::vector<NodeId>& comps = flat.components;
  for (std::size_t i = 0; i < comps.size(); ++i) {
    const NodeId lc = comps[i];
    if (I_.nodes[lc].parent_scope.is_valid() || I_.comps[I_.nodes[lc].comp].state != CompState::CompDef) continue;
    const Id<SElement> rel = I_.comps[I_.nodes[lc].comp].def;   // 本类的重声明元素（inst_class 留着没实例化的）
    std::vector<std::size_t> hits;
    for (std::size_t k = 0; k < comps.size(); ++k)
      if (k != i && I_.nodes[comps[k]].name == I_.nodes[lc].name && I_.nodes[comps[k]].parent_scope.is_valid()) hits.push_back(k);
    const std::string nm(I_.interner.str(I_.nodes[lc].name));
    if (hits.empty()) { mark_unsupported("redeclare of missing element", nm); return; }
    if (hits.size() > 1) { mark_unsupported("redeclare of duplicated inherited element", nm); return; }
    const NodeId bc = comps[hits[0]];
    const CompId bcid = I_.nodes[bc].comp;
    const Id<SElement> od = I_.comps[bcid].def;
    const Id<SElement> mid = merge_redeclared_elem(bc, od, rel, inst_node);
    if (!mid.is_valid()) return;
    // 原组件的绑定：extends 修饰给的（apply_extends_modifier 记在组件上，值在写 extends 的类里解析）> 原声明自己的（在它所在的
    // 基类实例里解析）。
    Binding fb = I_.comps[bcid].binding;
    if (fb.kind != BindingKind::Raw || !fb.raw_exp.is_valid()) {
      fb = Binding{};
      const SElement& o = I_.scode.elems[od];
      if (o.mod.is_valid() && I_.scode.mods[o.mod].binding.is_valid()) {
        fb.kind = BindingKind::Raw;
        fb.raw_exp = I_.scode.mods[o.mod].binding;
        fb.scope = I_.nodes[bc].parent_scope;
      }
    }
    const SElement& r = I_.scode.elems[rel];
    const bool r_bound = r.mod.is_valid() && I_.scode.mods[r.mod].binding.is_valid();
    const bool take_dims = r.attr.dims.empty() && r.type.dims.empty();
    // 取自原声明的条件 / 维在原组件写它们的地方求（见 Component::cond_scope）。
    const NodeId old_decl = I_.comps[bcid].decl_scope;
    const NodeId cond_at = I_.comps[bcid].cond_scope.is_valid() ? I_.comps[bcid].cond_scope : old_decl;
    const NodeId dims_at = I_.comps[bcid].dims_scope.is_valid() ? I_.comps[bcid].dims_scope : old_decl;
    I_.comps[I_.nodes[lc].comp].def = mid;
    I_.comps[I_.nodes[lc].comp].redecl_home = inst_node;   // 先记：inst_component 据此不并约束修饰（见那里）
    inst_component(lc, inst_node);
    I_.comps[I_.nodes[lc].comp].cond_scope = cond_at;
    I_.comps[I_.nodes[lc].comp].dims_scope = take_dims ? dims_at : NodeId{};
    // 基类实例里的那一项也换成它（omc 的重声明链各槽 Mutable.update 成同一个节点，NFInst.redeclareElements :1649-1675）：
    // 基类自己的段 / 绑定按名查到的是重声明后的组件（ER 探针 EQ4）。
    std::vector<NodeId> todo(flat.exts.begin(), flat.exts.end());
    for (int guard = 0; !todo.empty() && guard < 1024; ++guard) {
      const NodeId b = todo.back();
      todo.pop_back();
      if (!b.is_valid() || I_.nodes[b].is_empty() || !I_.nodes[b].cls.is_valid() || !I_.node_class(b).tree.is_valid()) continue;
      ClassTree& bt = I_.trees[I_.node_class(b).tree];
      std::replace(bt.components.begin(), bt.components.end(), bc, lc);
      todo.insert(todo.end(), bt.exts.begin(), bt.exts.end());
    }
    if (!r_bound && fb.kind == BindingKind::Raw) I_.comps[I_.nodes[lc].comp].binding = fb;
    comps.erase(comps.begin() + static_cast<std::ptrdiff_t>(hits[0]));
    if (hits[0] < i) --i;
  }
}

void Instantiator::inst_component(NodeId comp_node, NodeId enclosing, bool generated_inner) {
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
    comp.attributes.is_inner = generated_inner || el.prefixes.io == SInnerOuter::Inner || el.prefixes.io == SInnerOuter::InnerOuter;
    comp.attributes.is_outer = !generated_inner && (el.prefixes.io == SInnerOuter::Outer || el.prefixes.io == SInnerOuter::InnerOuter);
    comp.class_inst = Inst::empty_node();
    comp.state = CompState::PartiallyInstantiated;
    comp.decl_scope = enclosing;   // 元素修饰 / 维的作用域 = 声明所在类（NFInst.instComponentDef 的 parent，NFInst.mo:1942 / :1951）
    // 没被重声明的 replaceable 元素的约束子句修饰（`replaceable Real x constrainedby Real(start = 1.0)`）：instElementModifier
    //（NFInst.mo:2025-2040 v1.25.0）mod := merge(声明修饰, 约束修饰)——约束修饰排在声明修饰之后（flatten 照 cc_mods 的次序）。被重声明
    // 过的由 redeclare_component 另算（那时 orig_def 已置）。1.25.0 不查声明类型是不是约束类型的子类型。
    // 元素级重声明合并出的组件不并它自己的约束修饰（instElementModifier 的 isRedeclared 支只 propagateRedeclaredMod，
    // NFInst.mo v1.25.0；ER 探针 ER18 `redeclare replaceable Real x = 2.0 constrainedby Real(start = 4)` → 没有 start）。
    if (el.prefixes.has_cc && el.prefixes.cc.mod.is_valid() && !comp.orig_def.is_valid() && !comp.redecl_home.is_valid() &&
        comp.cc_mods.empty())
      comp.cc_mods.push_back(el.prefixes.cc.mod);
  }

  // outer 组件指向别处的 inner，不实例化自己的子树（否则 `outer B b`（B 里又含 outer B）会无限递归）。解析类型名：内建标量
  // 记类型；复合（模型 / 记录 / 连接器）记成 Complex——flatten 按名连到最近的 inner，经它的引用都换到 inner 上（R11 §5）。
  // `inner outer` 照常实例化：它对子层是 inner（omc 在子层 outer 连上它时实例化它，没连的 SKIP，见 insert_generated_inners）。
  if (!generated_inner && el.prefixes.io == SInnerOuter::Outer) {
    NodeId tn = lookup_class_name(el.type.path, enclosing);   // arena add：下面重新取组件引用
    Type oty = Type::unknown();
    for (int i = 0; i < kBuiltinCount; ++i)
      if (tn == builtin_[i]) { oty = Type::of(kBuiltinKinds[i]); break; }
    if (oty.kind == TypeKind::Unknown && tn.is_valid() && !I_.nodes[tn].is_empty()) oty = Type::of(TypeKind::Complex);
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
  // 所在实例的类树带类重声明时，类型是那个继承层级里声明的嵌套类（不是顶替进来的新类）、且它的体里写到被重声明的名字：OMC 里
  // 嵌套类的父亲是实例（看得到新类，RE5 探针 `c.m.b2.y`），ymc 的嵌套类节点父亲是未特化的定义 → SKIP。
  if (const auto rit = inst_redecls_.find(enclosing.raw); rit != inst_redecls_.end()) {
    bool replacement = false;
    for (const auto& [n, node] : rit->second) replacement = replacement || node == ty_node;
    if (!replacement && nested_in(ty_node, enclosing) && mentions_any(I_.nodes[ty_node].def, rit->second, 0)) {
      mark_unsupported("redeclared class seen from nested class", std::string(I_.interner.str(el.name)));
      return;
    }
  }
  // 声明类型是带 `annotation(absoluteValue = false)` 的 type（MSL 的 SI.TemperatureDifference）：omc 把组件的注释换成类型
  // 定义的注释（instComponentDef :1990-1996 v1.25.0，issue 12533 的修补）——类型定义大多没有描述串，组件的注释就没了（MSL
  // ThermalConductor 的 `dT "port_a.T - port_b.T"` 打成 `Real TC1.dT(…)`，HeatingRectifier 等）。只看声明的那个类型，不顺链。
  if (const Id<SElement> te = I_.nodes[ty_node].def; te.is_valid() && I_.scode.elems[te].kind == SElementKind::Class &&
                                                    I_.scode.elems[te].restriction == SRestriction::Type) {
    const Id<SMod> ann = I_.scode.elems[te].cmt.annotation;
    bool abs_false = false;
    if (ann.is_valid())
      for (const SSubMod& s : I_.scode.mods[ann].subs)
        if (I_.interner.str(s.ident) == "absoluteValue" && I_.scode.mods[s.mod].binding.is_valid()) {
          const Exp& b = I_.absyn.exps[I_.scode.mods[s.mod].binding];
          abs_false = b.kind == ExpKind::BoolLit && !b.bval;
        }
    if (abs_false) {
      I_.comps[cid].comment_from_type = true;
      I_.comps[cid].type_comment = I_.scode.elems[te].cmt.str;
    }
  }

  // 短类塌陷（R15 EXPANDED_DERIVED）：`type V = Base(mods)` 顺链解析到内建/枚举底，累积属性修饰
  // （外层派生在前 = 优先级高）。lookup_class_name 会 arena add，循环里不缓存 INode 引用（只用 Id）。
  std::vector<Id<SMod>> derived_mods;
  std::vector<NodeId> derived_scopes;   // 各短类定义所在的类（= 短类节点的词法外层）
  // 短类的前缀属性（`type InputReal = input Real`、`type C = constant …`）累积到组件上：
  // 组件自身 > 靠近用处的派生 > 更远派生（loop 外→内，first-non-default 胜）。
  bool alias_partial = false;            // 链上有 partial 的短类
  bool derived_type = false, derived_attrs = false;   // 见 Component::derived_type
  std::string unresolved_base;           // 链上查不到的基类名（报 SKIP 子原因）
  std::vector<SRestriction> alias_res;   // 链上各短类的受限类型（与链底长类比，见下）
  SVariability mvar = el.attr.var;
  SDirection mdir = el.attr.dir;
  SConnectorType mct = el.attr.ct;
  // 链上短类带的维（`type Axis = Real[3](…)`，外层在前）：拼在组件自己的维后面（MLS §10.1；XB 探针 x2：`Axis n` → n[3]、
  // `V3 x[2]` → x[2, 3]）。mod_dims_at[i] = derived_mods[i] 那一层开始时 type_dims 的长度——之后的维就是那层类型的形状。
  std::vector<Id<Exp>> type_dims;
  std::vector<std::size_t> mod_dims_at;
  NodeId base = ty_node;
  bool past_extends_type = false;   // 链上已经过一个"只 extends 的长类型"（见下：其后短类的修饰 1.25.0 会丢）
  for (int guard = 0; guard < 100 && base.is_valid() && !I_.nodes[base].is_empty(); ++guard) {
    bool builtin = false;
    for (int i = 0; i < kBuiltinCount; ++i)
      if (base == builtin_[i]) { builtin = true; break; }
    if (builtin) break;
    const Id<SElement> ce = I_.nodes[base].def;
    if (!ce.is_valid()) break;
    // 长类型只 extends 一个类型（MSL Icons.TypeReal：`type TypeReal extends Real; end TypeReal;`；`type T extends Real(unit =
    // "m"); end T;`）：等同短类 `type T = Real(unit = "m")`——extends 的修饰当链上修饰、基类当下一跳（XB 探针 x1：属性照打、
    // 后续短类 `type L = TR(unit = "m")` 照常）。此前实例化成长类、撞上 "extends builtin type"。
    if (const Id<SElement> xe = type_extends_only(ce); xe.is_valid()) {
      past_extends_type = true;
      derived_type = true;
      alias_res.push_back(SRestriction::Type);
      const Id<SMod> xm = I_.scode.elems[xe].mod;
      if (xm.is_valid() && (!I_.scode.mods[xm].subs.empty() || I_.scode.mods[xm].binding.is_valid())) {
        derived_mods.push_back(xm);
        derived_scopes.push_back(I_.nodes[base].parent_scope);
        mod_dims_at.push_back(type_dims.size());
      }
      const std::vector<Symbol> bp = I_.scode.elems[xe].base_path;   // 拷贝：lookup 会 arena add
      NodeId nxt = lookup_class_name(bp, I_.nodes[base].parent_scope);
      if (!nxt.is_valid() || I_.nodes[nxt].is_empty()) unresolved_base = path_str(bp);
      base = (nxt.is_valid() && !I_.nodes[nxt].is_empty()) ? nxt : Inst::empty_node();
      continue;
    }
    if (I_.scode.elems[ce].def.kind != SClassDefKind::Derived) break;   // Enumeration/Parts/… 到底
    const SClassDef& cd = I_.scode.elems[ce].def;
    // inner / outer 的短类定义（`outer model M = A;`，InnerOuterClass1 / Missing5 / InvalidMod4 / InvalidType2）：类的
    // inner / outer 配对 v1 未做 → SKIP。
    if (I_.scode.elems[ce].prefixes.io != SInnerOuter::None) mark_unsupported("inner/outer class");
    derived_type = true;
    {
      const SRestriction cr = I_.scode.elems[ce].restriction;
      const SAttributes& da = I_.scode.elems[ce].def.derived_attr;
      if (cr == SRestriction::Connector || cr == SRestriction::ExpandableConnector || da.var != SVariability::Var ||
          da.dir != SDirection::Bidir || da.ct != SConnectorType::Potential)
        derived_attrs = true;
    }
    if (I_.scode.elems[ce].is_partial) alias_partial = true;
    alias_res.push_back(I_.scode.elems[ce].restriction);
    if (cd.derived_mod.is_valid()) {
      const SMod& dm = I_.scode.mods[cd.derived_mod];
      // 长类型 extends 一个带修饰的短类（`type A = Real(unit = "m"); type B extends A; end B;`）：1.25.0 把短类的修饰丢了
      //（`Real b`，XB 探针 x40-x45；modification/UsertypeModifications），MLS 说该继承——不复刻 → SKIP（入账）。
      if (past_extends_type && (!dm.subs.empty() || dm.binding.is_valid()))
        mark_unsupported("extends of short class with modifiers");
      if (!dm.subs.empty() || dm.binding.is_valid()) {
        derived_mods.push_back(cd.derived_mod);
        derived_scopes.push_back(I_.nodes[base].parent_scope);
        mod_dims_at.push_back(type_dims.size());
      }
    }
    // 派生类型带维（`type V = Real[3]`，维在 derived_attr.dims）：只收字面量维——维式在短类定义处求值，搬到组件上会换作用域
    //（`type V = Real[n]` 的 n 在别处可能是另一个量）→ 非字面量 SKIP。
    for (Id<Exp> d : cd.derived_attr.dims) {
      if (I_.absyn.exps[d].kind != ExpKind::IntLit) mark_unsupported("derived-type array dimension");
      type_dims.push_back(d);
    }
    if (mvar == SVariability::Var && cd.derived_attr.var != SVariability::Var) mvar = cd.derived_attr.var;
    if (mdir == SDirection::Bidir && cd.derived_attr.dir != SDirection::Bidir) mdir = cd.derived_attr.dir;
    if (mct == SConnectorType::Potential && cd.derived_attr.ct != SConnectorType::Potential) mct = cd.derived_attr.ct;
    NodeId nxt = lookup_class_name(cd.derived_type.path, I_.nodes[base].parent_scope);
    if (!nxt.is_valid() || I_.nodes[nxt].is_empty()) unresolved_base = path_str(cd.derived_type.path);
    base = (nxt.is_valid() && !I_.nodes[nxt].is_empty()) ? nxt : Inst::empty_node();
  }

  // base = 内建 / 枚举 / 长类（复合）/ 解析失败。把结果算进局部，最后回写组件。
  NodeId class_inst = Inst::empty_node();
  Type ty;
  bool leaf = false;
  for (int i = 0; i < kBuiltinCount; ++i)
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
      mark_unsupported("unresolved derived base", unresolved_base);
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
    // 例外：operator record 的短类别名换成记录 / 连接器（`connector ComplexOutput = output Complex`、`record R2 = Complex`，
    // MSL ComplexBlocks / SI.Complex*）——类型仍是记录（makeRecordComplexType 取最外层短类作记录类型，NFInst.mo:2580-2591
    // v1.25.0），组件按短类的受限类型算连接器（flatten 的 short_class_connector）；rp62x SPEC OR2，探针 a08 / d02 / d07。
    for (SRestriction ar : alias_res) {
      const bool oprec_alias = be.restriction == SRestriction::OperatorRecord &&
                               (ar == SRestriction::Record || ar == SRestriction::Connector);
      if (ar != be.restriction && !oprec_alias) mark_unsupported("short class restriction mismatch");
    }
    // 组件声明修饰（外层）与短类链修饰（外层的短类在前）里的类重声明（`A a(redeclare model B = X)`、`model A2 = A(redeclare
    // model B = X(p = 4))`）：施加到这个组件的类实例（及其各层基类实例）的类树。外来的修饰里的（`d(a(redeclare …))`）inst 看不到，
    // 留给 flatten 照旧 SKIP。
    ClassRedecls comp_redecls;
    collect_class_redecls(el.mod, enclosing, base, comp_redecls);
    for (std::size_t i = 0; i < derived_mods.size(); ++i)
      collect_class_redecls(derived_mods[i], derived_scopes[i], base, comp_redecls);
    class_inst = instantiate(base, comp_node, comp_redecls.empty() ? nullptr : &comp_redecls);   // ← arena add：此后不得用先前取的组件引用
    ty.kind = TypeKind::Complex;
    ty.complex_node = class_inst.raw;
    // 短类修饰里的组件重声明（`record SalientInductance = Salient(redeclare SI.Inductance d, redeclare SI.Inductance q)`，MSL
    // FundamentalWave.Types、SI.ComplexCurrent 一类）：同 extends 修饰里的（redeclare_component），施加到这次实例化出的类里的
    // 同名组件上——类型按短类定义所在的作用域解析，新类型的属性（quantity / unit）照打、原声明的修饰照留（RS 探针 RS1-RS5）。
    // 施加过的记进 applied_redecls，flatten 不再把它当修饰。同一短类修饰里同名的另一个子修饰（`R(redeclare B d, d(start = 1))`）
    // omc 报 DUPLICATE_MODIFICATIONS（同 apply_extends_modifier）→ SKIP。
    if (class_inst.is_valid() && !I_.nodes[class_inst].is_empty())
      for (std::size_t i = 0; i < derived_mods.size(); ++i) {
        const std::vector<SSubMod> subs = I_.scode.mods[derived_mods[i]].subs;   // 拷贝：重声明会往 arena add
        for (const SSubMod& s : subs) {
          const SMod& sm = I_.scode.mods[s.mod];
          if (sm.kind != SModKind::Redecl || !sm.element.is_valid()) continue;
          // 类重声明实例化前已施加（见上）；不能按 applied_redecls 判——组件重声明施加一次后也记在里面，同一短类类型的第二个组件
          // 照样要施加（ORU4 的 `P.CV v; P.CV w;`）。
          if (I_.scode.elems[sm.element].kind == SElementKind::Class) continue;
          int same = 0;
          for (const SSubMod& t : subs) same += t.ident == s.ident ? 1 : 0;
          if (same > 1) { mark_unsupported("duplicate modification"); continue; }
          NodeId fc;
          for (NodeId c : I_.trees[I_.node_class(class_inst).tree].components)
            if (I_.nodes[c].name == s.ident && I_.nodes[c].comp.is_valid()) { fc = c; break; }
          if (!fc.is_valid()) { mark_unsupported("short class redeclare of missing element"); continue; }
          redeclare_component(fc, sm.element, derived_scopes[i]);
          I_.applied_redecls.insert(s.mod.raw);
        }
      }
    // 组件声明修饰里的成员重声明（MSL 多相电流 / 电压传感器 `ComplexOutput i[m](redeclare each final SI.Current re, redeclare
    // each final SI.Current im)`；scodeinst RedeclareMod1 / redeclare2 / redeclare4 …）：同上施加到这个组件的类实例里（声明修饰
    // 在短类修饰之外，后施加 = 外层的替掉内层的，redeclare_component 的"外层又重声明一次"）；类型名在声明所在的类里解析
    //（NFInst.instComponentDef 的 parent）。数组元素共用这一个类实例，故只收 each 的。
    // 只收不引用名字的：重声明元素的修饰 / 绑定 / 维、被替的原声明的维都只含字面量。引用名字的要在写修饰的类（外层实例，前缀
    // 少一段）里解析——flatten 拿组件自己的前缀去解析（`A a(redeclare Real x = p)` 成了 `a.p`、`redeclare Real x[n]` 的 n 找成
    // `a.n`，RD 探针 / scodeinst RedeclareDim1），未做 → 照旧留给 flatten → SKIP。
    if (class_inst.is_valid() && !I_.nodes[class_inst].is_empty() && el.mod.is_valid()) {
      auto lit_exp = [&](auto& self, Id<Exp> e) -> bool {
        if (!e.is_valid()) return true;
        const Exp& x = I_.absyn.exps[e];
        if (x.kind == ExpKind::CrefExp || x.kind == ExpKind::Call || x.kind == ExpKind::Reduction) return false;
        for (Id<Exp> k : {x.a, x.b, x.c})
          if (!self(self, k)) return false;
        for (Id<Exp> k : x.args)
          if (!self(self, k)) return false;
        return true;
      };
      auto lit_mod = [&](auto& self, Id<SMod> m) -> bool {
        if (!m.is_valid()) return true;
        const SMod& md = I_.scode.mods[m];
        if (md.kind != SModKind::Mod || !lit_exp(lit_exp, md.binding)) return false;
        for (const SSubMod& sub : md.subs)
          if (!self(self, sub.mod)) return false;
        return true;
      };
      auto lit_dims = [&](const SElement& d) {
        for (const std::vector<Id<Exp>>* ds : {&d.attr.dims, &d.type.dims})
          for (Id<Exp> x : *ds)
            if (!lit_exp(lit_exp, x)) return false;
        return true;
      };
      const std::vector<SSubMod> subs = I_.scode.mods[el.mod].subs;   // 拷贝：重声明会往 arena add
      const bool is_array = !el.attr.dims.empty() || !el.type.dims.empty() || !type_dims.empty();
      for (const SSubMod& s : subs) {
        const SMod& sm = I_.scode.mods[s.mod];
        if (sm.kind != SModKind::Redecl || !sm.element.is_valid() || (is_array && !sm.is_each)) continue;
        int same = 0;
        for (const SSubMod& t : subs) same += t.ident == s.ident ? 1 : 0;
        if (same > 1) { mark_unsupported("duplicate modification"); continue; }
        NodeId fc;
        for (NodeId c : I_.trees[I_.node_class(class_inst).tree].components)
          if (I_.nodes[c].name == s.ident && I_.nodes[c].comp.is_valid()) { fc = c; break; }
        if (!fc.is_valid()) continue;   // 不是成员：照旧留给 flatten
        // 被替的成员是 protected / final：omc 报 "Protected element 'x' may not be modified"（redeclare/RedeclareVisibility2、
        // RV3 探针）/ 改 final 元素 → 不施加，留给 flatten 照旧 SKIP。
        if (I_.nodes[fc].visibility == Visibility::Protected || I_.comps[I_.nodes[fc].comp].attributes.is_final) continue;
        const SElement& re = I_.scode.elems[sm.element];
        const SElement& oe = I_.scode.elems[I_.comps[I_.nodes[fc].comp].def];
        // 重声明元素自带约束子句（`redeclare replaceable Real x(min = 1.0) constrainedby Real(start = 2.0)`）：新约束修饰排在原约束
        // 修饰之前（redeclare_component 的 cc_mods，ConstrainingClass3 → `start = 2.0`），照常施加；其修饰同样只收字面量。
        if (!lit_mod(lit_mod, re.mod) || (re.prefixes.has_cc && !lit_mod(lit_mod, re.prefixes.cc.mod)) || !lit_dims(re) ||
            !lit_dims(oe))
          continue;
        redeclare_component(fc, sm.element, enclosing);
        I_.applied_redecls.insert(s.mod.raw);
      }
    }
  }
  Id<Exp> bind = el.mod.is_valid() ? I_.scode.mods[el.mod].binding : Id<Exp>{};
  // 类型带维：类型修饰按那层类型的形状切分（`type V3 = Real[3](start = {1, 2, 3})` → x[j].start = 第 j 个，x25），不再按组件
  // 维铺开；组件自己也有维时（`V3 x[2]`）要"组件维铺开 + 类型维切分"，未做 → 有不带 each 的值就 SKIP。合成一个维拼好的
  // 元素当组件定义（同 redeclare_component 的做法），flatten 按元素读维。
  std::vector<bool> derived_split(derived_mods.size(), false);
  Id<SElement> dims_def;
  if (!type_dims.empty() && !unsupported_) {
    const bool own_dims = !el.attr.dims.empty() || !el.type.dims.empty();
    for (std::size_t i = 0; i < derived_mods.size(); ++i) {
      derived_split[i] = mod_dims_at[i] < type_dims.size();
      if (!derived_split[i] || !own_dims) continue;
      for (const SSubMod& s : I_.scode.mods[derived_mods[i]].subs)
        if (!I_.scode.mods[s.mod].is_each && I_.scode.mods[s.mod].binding.is_valid())
          mark_unsupported("derived-type array dimension", "type modifier over component dimensions");
    }
    const std::uint64_t key = (static_cast<std::uint64_t>(0xFFFFFFFFu) << 32) | def_id.raw;   // 与重声明的键不撞（od 不会是全 1）
    if (auto it = redecl_elems_.find(key); it != redecl_elems_.end()) {
      dims_def = it->second;
    } else if (I_.scode.elems.size() >= synth_cap_) {
      mark_unsupported("derived-type array dimension", "synthesized element budget");
    } else {
      SElement m = I_.scode.elems[def_id];   // 拷贝后再 add（预留容量内不搬家）
      m.type.dims.insert(m.type.dims.end(), type_dims.begin(), type_dims.end());
      dims_def = I_.scode.elems.add(std::move(m));
      redecl_elems_.emplace(key, dims_def);
    }
  }

  // 全部 add 已完成，重新取引用回写最终值（R12 §2.4：绑定右式在 enclosing 作用域解析，留给 instExpressions）。
  Component& comp = I_.comps[cid];
  comp.class_inst = class_inst;
  comp.ty = ty;
  comp.derived_mods = std::move(derived_mods);
  comp.derived_scopes = std::move(derived_scopes);
  comp.derived_split = std::move(derived_split);
  if (dims_def.is_valid()) comp.def = dims_def;   // 维拼好的合成元素（见上）
  comp.derived_type = derived_type;
  comp.derived_attrs = derived_attrs;
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
  return instantiate_root_class(def_node);
}

// 根类是短类别名（`model R = A(p = 2);`）：照 expandClassDerived（NFInst.mo:1012-1061 v1.25.0，基类名从别名的词法外层查）与
// instClass 的 EXPANDED_DERIVED 支（:1200-1222：别名的修饰并到外来修饰上，再实例化基类）顺链走到长类定义，实例化它；别名与链上
// 各层修饰记进 Inst::root_alias（压平名 / 注释取别名，修饰当根实例的外来修饰，SR 探针）。链上带维 / 前缀 / redeclare 修饰、底是
// 内建或枚举 → SKIP。
NodeId Instantiator::instantiate_root_class(NodeId def_node) {
  I_.root_alias = {};
  NodeId cur = def_node;
  for (int guard = 0; guard < 20; ++guard) {
    const Id<SElement> ce = I_.nodes[cur].def;
    if (!ce.is_valid() || I_.scode.elems[ce].kind != SElementKind::Class ||
        I_.scode.elems[ce].def.kind != SClassDefKind::Derived)
      break;
    const SClassDef& cd = I_.scode.elems[ce].def;
    const SAttributes& da = cd.derived_attr;
    if (!da.dims.empty() || da.var != SVariability::Var || da.dir != SDirection::Bidir || da.ct != SConnectorType::Potential) {
      mark_unsupported("short/derived/enum root class", "dimensions / prefixes");
      return Inst::empty_node();
    }
    if (cd.derived_mod.is_valid()) {
      // 类重声明（`model R = A(redeclare model B = X)`）收进根实例的类重声明集（下面实例化长类时施加）；组件重声明未做 → SKIP。
      for (const SSubMod& s : I_.scode.mods[cd.derived_mod].subs) {
        const SMod& sm = I_.scode.mods[s.mod];
        const bool cls_redecl = sm.kind == SModKind::Redecl && sm.element.is_valid() &&
                                I_.scode.elems[sm.element].kind == SElementKind::Class;
        if (sm.kind != SModKind::Mod && !cls_redecl) {
          mark_unsupported("short/derived/enum root class", "redeclare");
          return Inst::empty_node();
        }
      }
      I_.root_alias.mods.push_back(cd.derived_mod);
      I_.root_alias.scopes.push_back(I_.nodes[cur].parent_scope);
    }
    const std::vector<Symbol> bp = cd.derived_type.path;   // 拷贝：查找会 arena add
    const NodeId nxt = lookup_class_name(bp, I_.nodes[cur].parent_scope);
    if (!nxt.is_valid() || I_.nodes[nxt].is_empty()) {
      mark_unsupported("unresolved base class", path_str(bp));
      return Inst::empty_node();
    }
    if (nxt == cur) {   // `class A = A`（RECURSIVE_SHORT_CLASS_DEFINITION）
      mark_unsupported("short/derived/enum root class", "recursive");
      return Inst::empty_node();
    }
    I_.root_alias.node = def_node;
    cur = nxt;
  }
  for (int k = 0; k < kBuiltinCount; ++k)
    if (cur == builtin_[k]) { mark_unsupported("short/derived/enum root class", "builtin"); return Inst::empty_node(); }
  ClassRedecls root_redecls;
  for (std::size_t k = 0; k < I_.root_alias.mods.size(); ++k)
    collect_class_redecls(I_.root_alias.mods[k], I_.root_alias.scopes[k], cur, root_redecls);
  const NodeId root = instantiate(cur, Inst::empty_node(), root_redecls.empty() ? nullptr : &root_redecls);
  insert_generated_inners(root);
  return root;
}

// 找不到 inner 的 outer 组件：omc 在顶层按名生成一个 inner（Lookup.generateInner / makeInnerNode，NFLookup.mo:1072-1150 v1.25.0：
// outer 的声明换成 inner 前缀，同名的 outer 共用第一个生成的），insertGeneratedInners（NFInst.mo:3740-3807）把它们接到根类组件
// 末尾、照常实例化——生成表的值数组逐个头插进链表再追加，末尾的次序与生成次序相反（GI11 探针：先生成 a 后生成 w，压平出
// `w.g` 在 `a.x` 前）。类型在 outer 声明处的作用域查（节点照搬 outer 的）。omc 另报 Warning（ymc 不产告警）。根类里已有同名元素
//（checkTopLevelOuter :3809-3846：MISSING_INNER_NAME_CONFLICT / TOP_LEVEL_OUTER）→ omc 报错 → SKIP；生成的 inner 里又有找不到
// inner 的 outer（omc 取生成表时已定长，不再补）→ SKIP。外层可见的 inner = 实例链上各层类实例的 inner 组件（lookupInner 顺
// instanceParent 上溯，同 flatten 的 inner 作用域）。
//
// `inner outer` 组件：omc 的类树里放的是 INNER_OUTER_NODE（ClassTree.instantiate 对 isOuter 的组件 linkInnerOuter，NFClassTree.mo:
// 560-575 v1.25.0）。它的 outer 一面从所在实例的 instanceParent 找起——实例自己是组件节点、类还没建好，getClass 失败，根类的树还是
// EXPANDED_TREE、resolveComponent 也失败（lookupInner，NFLookup.mo:389-420；NFClassTree.mo:472-475）——所以不会连到自己，根类里的
// 还会生成同名 inner、撞 TOP_LEVEL_OUTER（IY 探针 IY6）。实例化它的类时 applyLocalComponents 对它调 instComponent，那里先
// resolveInner（NFInst.mo instComponent），实例化的是它连上的外层 inner；它自己只在子层某个 outer 连上它（lookupInner 取
// resolveOuter = 它自己）时才被实例化，没有子层 outer 连它 → 定型时 "NFTyping.typeComponent got noninstantiated component"
//（IY 探针 IY5 / IY12 / IY13）→ SKIP。
//
// 前缀漏进 inner：被 resolveInner 实例化的 inner 用的是触发它的 outer 所在类传下的 attributes（instClassDef 的
// applyLocalComponents(instComponent(attributes = …))），即那条链上组件的 final / 变率前缀（IY 探针 IY14：
// `final L l` 里的 outer 连上 m.x → `final Real m.x`；IY19：`final L l; inner Real x` → `final Real x`）。inner outer、生成的
// inner 必是这样实例化的；普通 inner 在实例化序（先各 extends 的组件、再本地组件，instClassDef）里排在那条链之后才是。链上（inner
// 那层起、到 outer 的父实例）有这类前缀 → SKIP。
// ponytail: 实例化序按类树里的位置算（继承来的整体在前）；extends 写在本地元素之后的多层继承里位置会偏，要精确得记各基类的实例化序。
void Instantiator::insert_generated_inners(NodeId root) {
  if (!root.is_valid() || I_.nodes[root].is_empty() || unsupported_) return;
  struct Gen { Symbol name; NodeId outer, scope; };
  std::vector<Gen> gens;
  struct Vis { Symbol name; NodeId node; std::size_t level; int order; };   // 可见 inner：所在层、在那层实例化序里的位置
  struct Step { int order; bool prefixed; };                                // 实例链上各层通往当前实例的组件
  std::vector<Step> path;
  std::unordered_set<std::uint32_t> linked;   // 被子层 outer 连上的 inner outer
  std::vector<NodeId> inner_outers;
  bool leak = false;
  auto order_of = [&](NodeId c, std::size_t idx) {   // 继承来的组件 parent_scope = 基类实例，本地组件的为空
    return static_cast<int>(idx) + (I_.nodes[c].parent_scope.is_valid() ? 0 : (1 << 20));
  };
  auto lazy_leak = [&](std::size_t level) {
    for (std::size_t k = level; k < path.size(); ++k) leak = leak || path[k].prefixed;
  };
  // 实例树先序遍历（组件序 = 实例化序）：visible = 祖先各层与本层的 inner。
  auto walk = [&](auto& self, NodeId ci, std::vector<Vis> visible, int depth) -> void {
    if (depth > 200 || !ci.is_valid() || I_.nodes[ci].is_empty() || I_.nodes[ci].kind != NodeKind::Class ||
        !I_.nodes[ci].cls.is_valid() || !I_.node_class(ci).tree.is_valid())
      return;
    const std::vector<NodeId> comps = I_.trees[I_.node_class(ci).tree].components;   // 拷贝
    const std::size_t outer_n = visible.size();   // 祖先层的 inner（inner outer 的 outer 一面只看这些，见上）
    for (std::size_t i = 0; i < comps.size(); ++i)
      if (I_.nodes[comps[i]].kind == NodeKind::Component && I_.comps[I_.nodes[comps[i]].comp].attributes.is_inner)
        visible.push_back({I_.nodes[comps[i]].name, comps[i], path.size(), order_of(comps[i], i)});
    for (std::size_t i = 0; i < comps.size(); ++i) {
      const NodeId c = comps[i];
      if (I_.nodes[c].kind != NodeKind::Component) continue;
      const Component& comp = I_.comps[I_.nodes[c].comp];
      if (comp.attributes.is_outer) {
        const Symbol n = I_.nodes[c].name;
        const Vis* t = nullptr;
        for (std::size_t k = comp.attributes.is_inner ? outer_n : visible.size(); k-- > 0;)
          if (visible[k].name == n) { t = &visible[k]; break; }
        if (t) {
          const bool io = I_.comps[I_.nodes[t->node].comp].attributes.is_outer;
          if (io) linked.insert(t->node.raw);
          if (io || (t->level < path.size() && path[t->level].order < t->order)) lazy_leak(t->level);
        } else {
          bool seen = false;
          for (const Gen& g : gens) seen = seen || g.name == n;
          if (!seen) gens.push_back({n, c, ci});
          lazy_leak(0);
        }
        if (!comp.attributes.is_inner) continue;   // 纯 outer 不实例化子树
        inner_outers.push_back(c);
      }
      path.push_back({order_of(c, i), comp.attributes.is_final || comp.attributes.variability != SVariability::Var});
      self(self, comp.class_inst, visible, depth + 1);
      path.pop_back();
    }
  };
  walk(walk, root, {}, 0);
  auto io_checks = [&]() {
    for (NodeId c : inner_outers)
      if (!linked.count(c.raw)) { mark_unsupported("inner outer without outer linked to it"); return; }
    if (leak) mark_unsupported("outer link leaks prefixes into inner");
  };
  if (gens.empty()) { io_checks(); return; }
  for (auto it = gens.rbegin(); it != gens.rend(); ++it) {
    const Gen g = *it;
    // 根类里（本地类 / 组件、继承来的组件）或顶层已有同名元素 → omc 报错。
    bool clash = false;
    for (NodeId c : I_.trees[I_.node_class(root).tree].components)
      clash = clash || (I_.nodes[c].kind == NodeKind::Component && I_.nodes[c].name == g.name);
    for (Id<SElement> e : I_.scode.elems[I_.nodes[root].def].def.elements)
      clash = clash || ((I_.scode.elems[e].kind == SElementKind::Class || I_.scode.elems[e].kind == SElementKind::Component) &&
                        I_.scode.elems[e].name == g.name);
    for (Id<SElement> e : I_.scode.program) clash = clash || I_.scode.elems[e].name == g.name;
    if (clash) { mark_unsupported("generated inner name conflict", std::string(I_.interner.str(g.name))); return; }
    const Id<SElement> def = I_.comps[I_.nodes[g.outer].comp].def;
    if (!I_.scode.elems[def].attr.dims.empty() || !I_.scode.elems[def].type.dims.empty()) {
      mark_unsupported("outer array");
      return;
    }
    Component nc;
    nc.kind = CompKind::Comp;
    nc.state = CompState::CompDef;
    nc.def = def;
    nc.loc = I_.scode.elems[def].loc;
    INode nn;
    nn.kind = NodeKind::Component;
    nn.name = g.name;
    nn.def = def;
    nn.node_type = InstNodeType::of(NodeTypeKind::GeneratedInner);
    // 照搬 outer 的声明（makeInnerNode 只把 innerOuter 前缀换掉）：protected 声明的 outer 生成的 inner 也是 protected（MSL
    // StateGraph PartialStep 的 `protected outer CompositeStepState stateGraphRoot`，TrueHoldWithReset 的 omc 输出打 protected）。
    nn.visibility = vis_of(I_.scode.elems[def].prefixes.vis);
    nn.parent_scope = I_.nodes[g.outer].parent_scope;
    nn.instance_parent = root;
    nn.comp = I_.comps.add(std::move(nc));
    const NodeId gi = I_.nodes.add(std::move(nn));
    inst_component(gi, g.scope, /*generated_inner=*/true);
    I_.generated_inners[g.name] = gi;
    I_.trees[I_.node_class(root).tree].components.push_back(gi);
  }
  // 生成的 inner 的子树里又有找不到 inner 的 outer → SKIP（见上）。
  const std::vector<NodeId>& rcomps = I_.trees[I_.node_class(root).tree].components;
  std::vector<Vis> top;
  for (std::size_t i = 0; i < rcomps.size(); ++i)
    if (I_.nodes[rcomps[i]].kind == NodeKind::Component && I_.comps[I_.nodes[rcomps[i]].comp].attributes.is_inner)
      top.push_back({I_.nodes[rcomps[i]].name, rcomps[i], 0, order_of(rcomps[i], i)});
  const std::size_t before = gens.size();
  for (const auto& [n, gi] : I_.generated_inners) {
    const Component& gc = I_.comps[I_.nodes[gi].comp];
    path.push_back({1 << 21, gc.attributes.is_final || gc.attributes.variability != SVariability::Var});   // 排在根类组件之后
    walk(walk, gc.class_inst, top, 1);
    path.pop_back();
  }
  if (gens.size() != before) mark_unsupported("outer inside generated inner");
  io_checks();
}

NodeId Instantiator::instantiate_root_path(const std::vector<Symbol>& path) {
  if (path.size() == 1) return instantiate_root(path[0]);
  const NodeId def_node = lookup_class_name(path, I_.top);
  if (!def_node.is_valid() || I_.nodes[def_node].is_empty()) {
    diags_.error(SourceLoc{}, "Class " + path_str(path) + " not found in scope <top>.");
    return Inst::empty_node();
  }
  return instantiate_root_class(def_node);
}

}  // namespace ymc
