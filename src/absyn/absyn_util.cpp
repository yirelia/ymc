// Absyn 小工具实现（S2）。
#include "absyn/absyn_util.h"

namespace ymc {

Id<ClassDef> find_nested_class(const Absyn& a, Id<ClassDef> parent, Symbol name) {
  const ClassDef& p = a.classes[parent];
  for (Id<Element> eid : p.elements) {
    const Element& el = a.elems[eid];
    if (el.kind == ElementKind::ClassDef && el.cls.is_valid() && a.classes[el.cls].name == name)
      return el.cls;
  }
  return Id<ClassDef>{};
}

Id<ClassDef> find_class(const Absyn& a, const std::vector<Symbol>& path) {
  if (path.empty()) return Id<ClassDef>{};
  Id<ClassDef> cur;
  for (Id<ClassDef> id : a.top) {
    if (a.classes[id].name == path[0]) {
      cur = id;
      break;
    }
  }
  for (std::size_t i = 1; cur.is_valid() && i < path.size(); ++i)
    cur = find_nested_class(a, cur, path[i]);
  return cur;
}

InsertResult insert_class(Absyn& a, Id<ClassDef> parent, Id<ClassDef> cls, bool is_protected) {
  if (a.classes[parent].is_short) return InsertResult::ParentIsShortClass;  // MLS §13.4.3
  if (find_nested_class(a, parent, a.classes[cls].name).is_valid())
    return InsertResult::DuplicateName;  // MLS §13.4.1
  Element el;
  el.kind = ElementKind::ClassDef;
  el.loc = a.classes[cls].loc;
  el.is_protected = is_protected;
  el.cls = cls;
  Id<Element> eid = a.add_elem(std::move(el));
  a.classes[parent].elements.push_back(eid);  // 注意：先 add_elem 再取 classes[parent]，避免引用跨越 add
  return InsertResult::Ok;
}

std::string qualified_name(const Absyn& a, const std::vector<Symbol>& path) {
  std::string s;
  for (std::size_t i = 0; i < path.size(); ++i) {
    if (i) s += '.';
    s += a.interner.str(path[i]);
  }
  return s;
}

bool exp_equal(const Absyn& ab, Id<Exp> a, Id<Exp> b) {
  if (a == b) return true;
  if (!a.is_valid() || !b.is_valid()) return false;
  const Exp& x = ab.exps[a];
  const Exp& y = ab.exps[b];
  if (x.kind != y.kind || x.ival != y.ival || x.bval != y.bval || x.sval != y.sval || x.op != y.op ||
      x.partial_fn != y.partial_fn || x.args.size() != y.args.size() || x.arg_names != y.arg_names ||
      x.iters.size() != y.iters.size() || x.cref.is_valid() != y.cref.is_valid() ||
      x.call_suffix.is_valid() || y.call_suffix.is_valid())
    return false;
  if (x.cref.is_valid()) {
    const Cref& p = ab.crefs[x.cref];
    const Cref& q = ab.crefs[y.cref];
    if (p.leading_dot != q.leading_dot || p.parts.size() != q.parts.size()) return false;
    for (std::size_t i = 0; i < p.parts.size(); ++i) {
      if (p.parts[i].name != q.parts[i].name || p.parts[i].subscripts.size() != q.parts[i].subscripts.size()) return false;
      for (std::size_t k = 0; k < p.parts[i].subscripts.size(); ++k)
        if (!exp_equal(ab, p.parts[i].subscripts[k], q.parts[i].subscripts[k])) return false;
    }
  }
  for (std::size_t i = 0; i < x.args.size(); ++i) if (!exp_equal(ab, x.args[i], y.args[i])) return false;
  for (std::size_t i = 0; i < x.iters.size(); ++i)
    if (x.iters[i].name != y.iters[i].name || !exp_equal(ab, x.iters[i].range, y.iters[i].range)) return false;
  return exp_equal(ab, x.a, y.a) && exp_equal(ab, x.b, y.b) && exp_equal(ab, x.c, y.c);
}

}  // namespace ymc
