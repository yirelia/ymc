// 实例化 pass 测试（partialInst + expand + instClass）：解析 → SCode → 实例化，断言 FlatTree 结构。
// v1 第一里程碑：标量内建组件、单继承 extends 就地展开、复合组件、protected extends。
#include "ymc_test.h"

#include <string>
#include <vector>

#include "absyn/absyn.h"
#include "base/diag.h"
#include "base/symbol.h"
#include "inst/inode.h"
#include "inst/inst.h"
#include "parse/lexer.h"
#include "parse/parser.h"
#include "scode/absyn_to_scode.h"
#include "scode/scode.h"

using namespace ymc;

namespace {

// 解析源码 → SCode，建 Inst，实例化根类 root。所有权都在 Harness 里，测试拿引用查。
struct Harness {
  Interner in;
  Diagnostics d;
  Absyn a{in};
  SCode sc{a};
  Inst inst{in, a, sc};
  Instantiator I{inst, d};
  NodeId root;

  Harness(const char* src, const char* root_name) {
    Lexer lx(src, 0, in, d);
    std::vector<Token> toks = lx.tokenize();
    Parser p(toks, a, d);
    p.parse_program();
    translate_program(sc, a.top);
    I.make_top_node();
    root = I.instantiate_root(in.intern(root_name));
  }

  const ClassTree& flat_tree(NodeId cls) { return inst.trees[inst.node_class(cls).tree]; }
  std::string comp_name(const ClassTree& t, std::size_t i) { return std::string(in.str(inst.nodes[t.components[i]].name)); }
  const Component& comp(const ClassTree& t, std::size_t i) { return inst.comps[inst.nodes[t.components[i]].comp]; }
  Visibility comp_vis(const ClassTree& t, std::size_t i) { return inst.nodes[t.components[i]].visibility; }
};

}  // namespace

// 标量内建组件：M 的 FlatTree = [x:Real, n:Integer, b:Boolean]，各 FullyInstantiated。
YMC_TEST(inst_scalar_builtin_components) {
  Harness h("model M\n  Real x;\n  Integer n;\n  Boolean b;\n  String s;\nend M;\n", "M");
  YMC_CHECK(h.root.is_valid() && !h.inst.nodes[h.root].is_empty());
  YMC_CHECK(!h.d.has_errors());
  const ClassTree& t = h.flat_tree(h.root);
  YMC_CHECK_EQ(t.components.size(), std::size_t{4});
  YMC_CHECK_EQ(h.comp_name(t, 0), std::string("x"));
  YMC_CHECK(h.comp(t, 0).ty.kind == TypeKind::Real);
  YMC_CHECK(h.comp(t, 0).state == CompState::FullyInstantiated);
  YMC_CHECK_EQ(h.comp_name(t, 1), std::string("n"));
  YMC_CHECK(h.comp(t, 1).ty.kind == TypeKind::Integer);
  YMC_CHECK_EQ(h.comp_name(t, 2), std::string("b"));
  YMC_CHECK(h.comp(t, 2).ty.kind == TypeKind::Boolean);
  YMC_CHECK_EQ(h.comp_name(t, 3), std::string("s"));
  YMC_CHECK(h.comp(t, 3).ty.kind == TypeKind::String);
}

// 组件绑定保留为 Raw（Absyn 表达式 + 作用域），供 instExpressions 定型。
YMC_TEST(inst_component_binding_kept_raw) {
  Harness h("model M\n  Real x = 1.0;\n  Real y = x + 2;\nend M;\n", "M");
  YMC_CHECK(!h.d.has_errors());
  const ClassTree& t = h.flat_tree(h.root);
  YMC_CHECK_EQ(t.components.size(), std::size_t{2});
  YMC_CHECK(h.comp(t, 0).binding.kind == BindingKind::Raw);
  YMC_CHECK(h.comp(t, 0).binding.raw_exp.is_valid());
  YMC_CHECK(h.comp(t, 1).binding.kind == BindingKind::Raw);
}

// 单继承 extends 就地展开：B 的 FlatTree = [b1, a1, a2, b2]（extends 在其子句位置）。
YMC_TEST(inst_single_inheritance_in_place) {
  Harness h(
      "model A\n  Real a1;\n  Real a2;\nend A;\n"
      "model B\n  Real b1;\n  extends A;\n  Real b2;\nend B;\n",
      "B");
  YMC_CHECK(!h.d.has_errors());
  const ClassTree& t = h.flat_tree(h.root);
  YMC_CHECK_EQ(t.components.size(), std::size_t{4});
  YMC_CHECK_EQ(h.comp_name(t, 0), std::string("b1"));
  YMC_CHECK_EQ(h.comp_name(t, 1), std::string("a1"));
  YMC_CHECK_EQ(h.comp_name(t, 2), std::string("a2"));
  YMC_CHECK_EQ(h.comp_name(t, 3), std::string("b2"));
}

// 多层继承：C extends B extends A，压平顺序 = 各层就地展开。
YMC_TEST(inst_multilevel_inheritance) {
  Harness h(
      "model A\n  Real a;\nend A;\n"
      "model B\n  extends A;\n  Real b;\nend B;\n"
      "model C\n  extends B;\n  Real c;\nend C;\n",
      "C");
  YMC_CHECK(!h.d.has_errors());
  const ClassTree& t = h.flat_tree(h.root);
  YMC_CHECK_EQ(t.components.size(), std::size_t{3});
  YMC_CHECK_EQ(h.comp_name(t, 0), std::string("a"));
  YMC_CHECK_EQ(h.comp_name(t, 1), std::string("b"));
  YMC_CHECK_EQ(h.comp_name(t, 2), std::string("c"));
}

// 复合组件：M 有 A comp；comp 的类型是 Complex，其 class_inst 的 FlatTree = [a]。
YMC_TEST(inst_composite_component) {
  Harness h(
      "model A\n  Real a;\nend A;\n"
      "model M\n  A comp;\nend M;\n",
      "M");
  YMC_CHECK(!h.d.has_errors());
  const ClassTree& t = h.flat_tree(h.root);
  YMC_CHECK_EQ(t.components.size(), std::size_t{1});
  YMC_CHECK_EQ(h.comp_name(t, 0), std::string("comp"));
  const Component& c = h.comp(t, 0);
  YMC_CHECK(c.ty.kind == TypeKind::Complex);
  YMC_CHECK(c.class_inst.is_valid() && !h.inst.nodes[c.class_inst].is_empty());
  const ClassTree& sub = h.flat_tree(c.class_inst);
  YMC_CHECK_EQ(sub.components.size(), std::size_t{1});
  YMC_CHECK_EQ(h.comp_name(sub, 0), std::string("a"));
}

// protected extends 使继承来的组件变 protected（R11 §3.4-2）。
YMC_TEST(inst_protected_extends) {
  Harness h(
      "model A\n  Real z;\nend A;\n"
      "model B\nprotected\n  extends A;\nend B;\n",
      "B");
  YMC_CHECK(!h.d.has_errors());
  const ClassTree& t = h.flat_tree(h.root);
  YMC_CHECK_EQ(t.components.size(), std::size_t{1});
  YMC_CHECK_EQ(h.comp_name(t, 0), std::string("z"));
  YMC_CHECK(h.comp_vis(t, 0) == Visibility::Protected);
}

// 每个组件使用点各有私有实例：M 有两个 A（comp1、comp2），它们的 class_inst 不共享。
YMC_TEST(inst_each_use_gets_own_instance) {
  Harness h(
      "model A\n  Real a;\nend A;\n"
      "model M\n  A comp1;\n  A comp2;\nend M;\n",
      "M");
  YMC_CHECK(!h.d.has_errors());
  const ClassTree& t = h.flat_tree(h.root);
  YMC_CHECK_EQ(t.components.size(), std::size_t{2});
  NodeId i1 = h.comp(t, 0).class_inst;
  NodeId i2 = h.comp(t, 1).class_inst;
  YMC_CHECK(i1.is_valid() && i2.is_valid());
  YMC_CHECK(!(i1 == i2));   // 私有克隆（不同节点）
}

// 找不到基类 → v1 最小 lookup 标 unsupported（driver 退 66；完整 lookup 落地后再区分真错误）。
YMC_TEST(inst_missing_base_class_unsupported) {
  Harness h("model B\n  extends Nonexistent;\nend B;\n", "B");
  YMC_CHECK(h.I.unsupported());
}

YMC_TEST_MAIN
