// 函数名、分支顺序照 OMC v1.25.0：NBAlias.mo 的 aliasDefault :209-284、checkReplacements / filterPre :286-348、aliasCausalize :410-456、
// findSimpleEquation :458-596、findCrefs / findCrefsFail :598-654、isSimpleExp / checkOp :656-739、getSimpleSets :741-768、
// createReplacementRules :770-835、chooseVariableToKeep :891-926、rateVar :1288-1342；NBReplacements.mo 的 simple / addSimple :99-141、
// applySimple / applySimpleExp / applySimpleVar :143-234、single :81-97；NBCausalize.simple（Modules/1_Main/NBCausalize.mo:193-212）；
// NBSorting.tarjan / tarjanScalar / strongConnect / predecessors（Modules/1_Main/NBSorting.mo:229-327、:637-695）；NBSolve.solveBody /
// solveSimpleLhsRhs / solveLinear（Modules/3_Post/NBSolve.mo:576-806）；NBDifferentiate.differentiateExpression / ComponentRef / Binary /
// Multary（Util/NBDifferentiate.mo:444-748、:1706-1948）；NBVariable.mo 的 hasNonTrivialAliasBinding / hasConstOrParamAliasBinding :1489-1503、
// setBindingAsStartAndFix :1476-1481、VariablePointers.removeList / add :1748-1803；Equation.generateBindingEquation（NBEquation.mo:2186-2240）、
// getResidualExp :1937-1949、EqData.addUntypedList :4555-4579、typeList :4661-4706。
//
// 走不到的分支不写：ARRAY_EQUATION、记录元素（findCrefs :615-617）、下标 cref：lower 挡了。aliasClocks（:364-408）：没有 Clock 变量和
// clocked 方程（lower SKIP），它的 aliasCausalize 什么也找不到、applySimple 遇到空表直接返回，是空操作。
//
// 属性合并：AttributeCollector.fixValues :1378-1445、setNewAttributes :837-889、getMaximum / getMinimum :928-984、setStartFixed :986-1037、
// checkNominalThreshold :1039-1131、stateSelectAlways :1133-1156、chooseStateSelect / chooseTearingSelect :1187-1238；Expression.getNominal
// （NFExpression.mo:2257-2299）。
//
// 没做、SKIP 的（见各处 stop）：Solve.solveBody 走到 solveUnique（导数里还有这个变量）；omc 自己在这个模块失败、没有这一段的形状
// （环、两个常量绑定、导数是 0、fixed 没有 start、fixed 的 start 不等、两个 always、nominal 为 0、0 个桶的 Map）。
#include "bdae/alias.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "bdae/exp_compare.h"
#include "bdae/simplify.h"

namespace ymc::bdae {

namespace {

// 一条简单方程：方程，和 findCrefs 收到的未知量（cr_lst 的序：表头在前）。集合里存的是 Pointer.create(eq) 造的副本（:494、:540），
// 原指针随即被 foldRemovePtr 改成 DUMMY（NBEquation.mo:4174-4185）；ymc 的 arena 里那一格不动，只把它记成删除，效果相同。
struct SimpleEqn {
  Id<Eqn> eqn;
  std::vector<std::string> crefs;
};

// AliasSet（:164-170）。两个 list 都按 MetaModelica 的表序存：[0] 是表头。
struct AliasSet {
  std::vector<std::string> vars;   // simple_variables
  std::vector<SimpleEqn> eqns;     // simple_equations
  bool has_const = false;          // const_opt
  SimpleEqn const_eqn;
};

// CrefTpl（:197-207）。cont = false 是 FAILED_CREF_TPL（cr_lst 一并清空）。
struct CrefTpl {
  bool cont = true;
  std::vector<std::string> crefs;  // 表头在前
};

// UnorderedMap<ComponentRef, T> 用到的部分：插入序（valueList / toList），已有的键只改值、不挪位置（UnorderedMap.add :161-177）。
template <class T>
struct OrderedMap {
  std::vector<std::pair<std::string, T>> items;
  [[nodiscard]] const T* get(const std::string& k) const {
    for (const auto& [key, v] : items)
      if (key == k) return &v;
    return nullptr;
  }
  void add(const std::string& k, T v) {
    for (auto& [key, old] : items)
      if (key == k) {
        old = std::move(v);
        return;
      }
    items.emplace_back(k, std::move(v));
  }
};

// AttributeCollector（:1344-1353），键是变量名，按 rateVar 的调用序插入。stateSelect 存枚举字面量本身，tearingSelect 存成员名。
struct Collector {
  OrderedMap<Id<Expr>> min, max, start, fixed, nominal, state_select;
  OrderedMap<Symbol> tearing_select;
};

class AliasModule {
 public:
  AliasModule(BackendDAE& d, Interner& in) : d_(d), in_(in) {}

  ModuleResult run() {
    index_vars();
    // aliasCausalize(varData.unknowns, eqData.simulation)（:231、:430-432）：Map 的桶数是 unknowns 的个数，只影响查找；0 个桶的 Map 一加就失败。
    const std::size_t buckets = d_.vars.unknowns.items.size();
    for (Id<Eqn> id : d_.eqns.simulation.items) {
      find_simple_equation(id, buckets);
      if (r_.unsupported) return r_;
    }
    for (int s : simple_sets()) {
      if (r_.unsupported) return r_;
      replacement_rules(sets_[static_cast<std::size_t>(s)]);
    }
    if (r_.unsupported) return r_;
    check_replacements();
    if (!r_.unsupported) apply_simple();
    if (!r_.unsupported) finish();
    return r_;
  }

 private:
  void stop(std::string why) {
    if (r_.unsupported) return;
    r_.unsupported = true;
    r_.unsupported_reason = std::move(why);
  }

  // ---- 变量与名字：BVariable.getVarPointer。lower 时 cref 已指向后端变量，这里按点分名找。

  void index_vars() {
    const VarData& vd = d_.vars;
    for (const VarSet* set : {&vd.unknowns, &vd.states, &vd.algebraics, &vd.discretes, &vd.top_level_inputs, &vd.parameters,
                              &vd.constants, &vd.artificials})
      for (Id<Var> v : set->items)
        if (d_.var_arena[v].kind != VarKind::Dummy) vars_.emplace(key(d_.var_arena[v].name), v);
  }
  std::string key(const ExprCref& c) const {
    std::string s;
    for (const ExprCrefPart& p : c.parts) {
      if (!s.empty()) s += '.';
      s += in_.str(p.name);
    }
    return s;
  }
  Id<Var> var_of(const std::string& k) const {
    const auto it = vars_.find(k);
    return it == vars_.end() ? Id<Var>{} : it->second;
  }
  bool is_cref(Id<Expr> id, const std::string& k) const {
    const Expr& e = d_.exprs[id];
    return e.kind == ExprKind::Cref && key(e.cref) == k;
  }
  // Expression.containsCref。
  bool contains(Id<Expr> id, const std::string& k) const {
    if (!id.is_valid()) return false;
    const Expr& e = d_.exprs[id];
    if (e.kind == ExprKind::Cref) return key(e.cref) == k;
    if (contains(e.a, k) || contains(e.b, k) || contains(e.c, k)) return true;
    for (Id<Expr> x : e.args)
      if (contains(x, k)) return true;
    for (Id<Expr> x : e.inv_args)
      if (contains(x, k)) return true;
    return false;
  }

  // ---- isSimpleExp :656-722 + checkOp :724-739。返回 (simple, num_cref)。参数也算 cref（:672 的 TODO），所以 `p * x` 不简单。

  static bool check_op(BinOp op, int n) {
    switch (op) {
      case BinOp::Add:
      case BinOp::Sub: return true;
      case BinOp::Mul:
      case BinOp::Div: return n < 2;
      case BinOp::Pow: return n == 0;
    }
    YMC_UNREACHABLE("BinOp");
  }
  std::pair<bool, int> simple_exp(Id<Expr> id) const {
    const Expr& e = d_.exprs[id];
    switch (e.kind) {
      case ExprKind::IntLit:
      case ExprKind::RealLit:
      case ExprKind::BoolLit:
      case ExprKind::StringLit: return {true, 0};
      case ExprKind::Cref: return {true, 1};
      case ExprKind::Cast: return simple_exp(e.a);
      case ExprKind::Unary:          // UMINUS、NOT 的 checkOp 恒真
      case ExprKind::LogicalNot: return simple_exp(e.a);
      case ExprKind::Binary: {
        // 先看右边：右边有 cref 的除法不简单（1/x）。
        const auto [s2, n2] = simple_exp(e.b);
        if (!s2 || (e.bin == BinOp::Div && n2 != 0)) return {false, n2};
        const auto [s1, n1] = simple_exp(e.a);
        if (!s1) return {false, 0};
        return {check_op(e.bin, n1 + n2), n1 + n2};
      }
      case ExprKind::Logical: {      // and / or 的 checkOp 走 else：cref_num == 0
        const auto [s1, n1] = simple_exp(e.a);
        if (!s1) return {false, 0};
        const auto [s2, n2] = simple_exp(e.b);
        if (!s2) return {false, 0};
        return {n1 + n2 == 0, n1 + n2};
      }
      case ExprKind::Multary: {      // 逆项先：积的逆项里有 cref 不简单
        int n = 0;
        for (Id<Expr> x : e.inv_args) {
          const auto [s, k] = simple_exp(x);
          if (!s) return {false, n};
          n += k;
        }
        if (e.bin == BinOp::Mul && n != 0) return {false, n};
        for (Id<Expr> x : e.args) {
          const auto [s, k] = simple_exp(x);
          if (!s) return {false, n};
          n += k;
        }
        return {check_op(e.bin, n), n};
      }
      default: return {false, 0};    // 枚举字面量、关系、if、调用……
    }
  }

  // ---- findCrefs :598-654，用 Expression.fold（NFExpression.mo:3274-3381）：先子式后本节点，和的正项先于逆项。

  void find_crefs(Id<Expr> id, CrefTpl& t) {
    if (!id.is_valid() || r_.unsupported) return;
    const Expr e = d_.exprs[id];
    find_crefs(e.a, t);
    find_crefs(e.b, t);
    find_crefs(e.c, t);
    for (Id<Expr> x : e.args) find_crefs(x, t);
    for (Id<Expr> x : e.inv_args) find_crefs(x, t);
    if (!t.cont) return;
    switch (e.kind) {
      case ExprKind::Cref: {
        const std::string k = key(e.cref);
        const Id<Var> v = var_of(k);
        if (!v.is_valid()) return stop("reference to an unknown variable");
        const VarKind kind = d_.var_arena[v].kind;
        if (kind == VarKind::Parameter || kind == VarKind::Constant || k == "time") return;
        if (t.crefs.size() < 2) {
          t.crefs.insert(t.crefs.begin(), k);
          return;
        }
        t = CrefTpl{false, {}};
        return;
      }
      case ExprKind::Relation:   // findCrefsFail :638-654（RECORD 走不到）
      case ExprKind::IfExp:
      case ExprKind::Call:
        t = CrefTpl{false, {}};
        return;
      default:
        return;
    }
  }

  // ---- findSimpleEquation :458-596

  void add_key(const std::string& k, int set, std::size_t buckets) {
    // UnorderedMap.add：已有的键只改值，不占新位置。
    if (const auto it = set_of_.find(k); it != set_of_.end()) {
      it->second = set;
      return;
    }
    // UnorderedMap.new(size = 0) 的 Map 一加新键就失败（loadFactor 除零，omc_unordered.h），模块失败，omc 没有这一段。
    if (buckets == 0) return stop("simple equation with no unknowns (omc fails on a map with zero buckets)");
    keys_.push_back(k);
    set_of_[k] = set;
  }

  void find_simple_equation(Id<Eqn> id, std::size_t buckets) {
    const Eqn& q = d_.eqn_arena[id];
    if (q.kind != EqnKind::Scalar) return;
    if (!simple_exp(q.lhs).first || !simple_exp(q.rhs).first) return;
    CrefTpl t;
    find_crefs(q.rhs, t);
    find_crefs(q.lhs, t);
    if (r_.unsupported || !t.cont || t.crefs.empty()) return;
    SimpleEqn se{id, t.crefs};
    if (t.crefs.size() == 1) {
      // 一个未知量连着参数 / 常数（:488-509）。
      const std::string& cr1 = t.crefs[0];
      const auto it = set_of_.find(cr1);
      if (it == set_of_.end()) {
        AliasSet s;
        s.vars = {cr1};
        s.has_const = true;
        s.const_eqn = se;
        sets_.push_back(std::move(s));
        add_key(cr1, static_cast<int>(sets_.size()) - 1, buckets);
      } else {
        AliasSet& s = sets_[static_cast<std::size_t>(it->second)];
        if (s.has_const) return stop("overdetermined set with two constant bindings (omc fails)");
        s.has_const = true;
        s.const_eqn = se;
      }
    } else {
      // 两个未知量（:512-591）。cr1 是表头：rhs 先 fold、lhs 后，所以 lhs 的最后一个在前。
      // 同一变量出现两次（`a = -a`、`x = 17 + 2 * x`）时 cr1 == cr2：不在表里就照常新建 {a, a}，Map 第二次 add 只改值；在表里就是两边都在、
      // 同一个集合指针，走"成环"失败。getSimpleSets 标记第二个 a 时 addUnique 失败，被 try 接住只报错（:760-764），实跑照样有这一段
      //（语料 b ScopeDeclaration1：a 解成 -0.0，进 Parameter 桶）。
      const std::string cr1 = t.crefs[0], cr2 = t.crefs[1];
      const auto i1 = set_of_.find(cr1), i2 = set_of_.find(cr2);
      if (i1 != set_of_.end() && i2 != set_of_.end()) {
        const int p1 = i1->second, p2 = i2->second;
        if (p1 == p2) return stop("simple equations form a loop (omc fails)");
        const AliasSet set1 = sets_[static_cast<std::size_t>(p1)], set2 = sets_[static_cast<std::size_t>(p2)];
        if (set1.has_const && set2.has_const) return stop("merged set with two constant bindings (omc fails)");
        AliasSet s;
        s.has_const = set1.has_const || set2.has_const;
        s.const_eqn = set1.has_const ? set1.const_eqn : set2.const_eqn;
        // 短的表接在长的表前面（listAppendDestroy(短, 长)）；变量两支写的都是 set2 ++ set1（:547、:553，照抄）。
        const bool eq1_longer = set1.eqns.size() > set2.eqns.size();
        const AliasSet& first = eq1_longer ? set2 : set1;
        const AliasSet& second = eq1_longer ? set1 : set2;
        s.eqns.push_back(se);
        s.eqns.insert(s.eqns.end(), first.eqns.begin(), first.eqns.end());
        s.eqns.insert(s.eqns.end(), second.eqns.begin(), second.eqns.end());
        s.vars = set2.vars;
        s.vars.insert(s.vars.end(), set1.vars.begin(), set1.vars.end());
        // 变量多的那个指针留下，另一边的变量改指它（UnorderedMap.add 已有的键只改值，键序不动）。
        if (set1.vars.size() > set2.vars.size()) {
          sets_[static_cast<std::size_t>(p1)] = std::move(s);
          for (const std::string& cr : set2.vars) set_of_[cr] = p1;
        } else {
          sets_[static_cast<std::size_t>(p2)] = std::move(s);
          for (const std::string& cr : set1.vars) set_of_[cr] = p2;
        }
      } else if (i1 != set_of_.end() || i2 != set_of_.end()) {
        const bool has1 = i1 != set_of_.end();
        const int p = has1 ? i1->second : i2->second;
        const std::string& fresh = has1 ? cr2 : cr1;
        AliasSet& s = sets_[static_cast<std::size_t>(p)];
        s.vars.insert(s.vars.begin(), fresh);
        s.eqns.insert(s.eqns.begin(), se);
        add_key(fresh, p, buckets);
      } else {
        AliasSet s;
        s.vars = {cr1, cr2};
        s.eqns = {se};
        sets_.push_back(std::move(s));
        const int p = static_cast<int>(sets_.size()) - 1;
        add_key(cr1, p, buckets);
        if (!r_.unsupported) add_key(cr2, p, buckets);
      }
    }
    if (!r_.unsupported) deleted_.insert(id.raw);
  }

  // ---- getSimpleSets :741-768：按 Map 的插入序（UnorderedMap.toList）走键，没标记过的键取它的集合、头插进结果，标记集合里的变量。
  // 所以结果是"集合首次出现的序"倒过来。
  std::vector<int> simple_sets() {
    std::unordered_set<std::string> marks;
    std::vector<int> out;
    for (const std::string& k : keys_) {
      if (marks.count(k)) continue;
      const int s = set_of_.at(k);
      out.insert(out.begin(), s);
      // 同一变量在 simple_variables 里出现两次时 addUnique 失败，try 接住只报内部错、不失败（:760-764）。
      for (const std::string& v : sets_[static_cast<std::size_t>(s)].vars) marks.insert(v);
    }
    return out;
  }

  // ---- createReplacementRules :770-835

  void replacement_rules(const AliasSet& set) {
    std::vector<std::string> vars;
    std::vector<SimpleEqn> eqns;
    Collector col;
    std::string keep;
    if (set.has_const) {
      // 有常量绑定：一个都不留，全换成常量（:791-799），不选代表、不合并属性。
      vars = set.vars;
      eqns.push_back(set.const_eqn);
      eqns.insert(eqns.end(), set.eqns.begin(), set.eqns.end());
    } else {
      keep = keep_variable(set.vars, vars, col);
      eqns = set.eqns;
    }
    if (r_.unsupported) return;
    // VariablePointers.fromList（NBVariable.mo:1738-1746）逐个 add，已有的只更新、不占新位置（:1766-1784）：自指方程（`a = 17 + 2 * a`）让 a 在
    // simple_variables 里出现两次，进因果化时只剩一个、留在第一次出现的位置。之后 `VariablePointers.toList(vars)`（:813）也是去重后的表。
    std::vector<std::string> unique;
    for (const std::string& v : vars)
      if (std::find(unique.begin(), unique.end(), v) == unique.end()) unique.push_back(v);
    vars = std::move(unique);
    const std::vector<std::pair<std::size_t, std::size_t>> comps = causalize(vars, eqns);
    if (r_.unsupported) return;   // 因果化失败时 comps 是空的，不能再往下走（替换表里没有这些变量）
    for (const auto& [v, e] : comps) {
      if (r_.unsupported) return;
      // addSimple（NBReplacements.mo:109-141）：解出 v，前面的替换代进右边（Expression.map + applySimpleExp），再化简。
      const Id<Expr> rhs = solve(eqns[e].eqn, vars[v]);
      if (r_.unsupported) return;
      const Id<Expr> value = simplify_exp(replace(rhs, repl_));
      if (r_.unsupported) return;
      repl_keys_.push_back(vars[v]);
      repl_[vars[v]] = value;
    }
    if (set.has_const) return;
    // :817-829：每个被换掉的变量的属性折算到代表变量上（fixValues），检查，再定代表变量的新属性。diffTearingSelect 只发通知。
    for (const std::string& v : vars) {
      fix_values(col, v, keep);
      if (r_.unsupported) return;
    }
    state_select_always(col);
    check_nominal_threshold(col);
    if (!r_.unsupported) set_new_attributes(keep, col);
  }

  // chooseVariableToKeep :891-926：评分严格更高才换，所以同分时留表里最先的；acc 头插。rateVar 顺带按表序收属性。
  std::string keep_variable(const std::vector<std::string>& list, std::vector<std::string>& acc, Collector& col) {
    std::string keep = list.front();
    int max_rating = rate_var(keep, col);
    for (std::size_t i = 1; i < list.size(); ++i) {
      const int r = rate_var(list[i], col);
      if (r > max_rating) {
        max_rating = r;
        acc.insert(acc.begin(), keep);
        keep = list[i];
      } else {
        acc.insert(acc.begin(), list[i]);
      }
    }
    return keep;
  }
  // rateVar :1288-1342：函数别名 / 时钟别名（名字首段以 $FUN / $CLK 开头）-10000，否则 -depth；Real 的 stateSelect = always 再加 100。
  // 属性按 VAR_ATTR_REAL / INT / BOOL 三支收进 AttributeCollector（String、枚举不收）。
  int rate_var(const std::string& k, Collector& col) {
    const Var& v = d_.var_arena[var_of(k)];
    const std::string_view first = in_.str(v.name.parts.front().name);
    int r = first.rfind("$FUN", 0) == 0 || first.rfind("$CLK", 0) == 0 ? -10000 : -static_cast<int>(v.name.parts.size());
    const auto attr = [&](std::string_view name) -> Id<Expr> {
      for (const FlatVarAttr& a : v.attrs)
        if (in_.str(a.name) == name) return a.value;
      return {};
    };
    const auto collect = [&](OrderedMap<Id<Expr>>& map, std::string_view name) {
      if (const Id<Expr> x = attr(name); x.is_valid()) map.add(k, x);
    };
    switch (v.ty.kind) {
      case TypeKind::Real:
        collect(col.min, "min");
        collect(col.max, "max");
        collect(col.start, "start");
        collect(col.fixed, "fixed");
        collect(col.nominal, "nominal");
        if (const Id<Expr> s = attr("stateSelect"); s.is_valid()) {
          if (state_select_rank(s) == 4) r += 100;
          col.state_select.add(k, s);
        }
        if (v.tearing_select.is_valid()) col.tearing_select.add(k, v.tearing_select);
        break;
      case TypeKind::Integer:
        collect(col.min, "min");
        collect(col.max, "max");
        collect(col.start, "start");
        collect(col.fixed, "fixed");
        break;
      case TypeKind::Boolean:
        collect(col.start, "start");
        collect(col.fixed, "fixed");
        break;
      default:
        break;
    }
    return r;
  }
  // StateSelect / TearingSelect 的枚举序（NFBackendExtension.mo:1477-1478：NEVER, AVOID, DEFAULT, PREFER, ALWAYS），比较用。
  int select_rank(std::string_view name) {
    static constexpr std::string_view kNames[] = {"never", "avoid", "default", "prefer", "always"};
    for (int i = 0; i < 5; ++i)
      if (kNames[i] == name) return i;
    stop("unknown StateSelect / TearingSelect value");
    return 0;
  }
  int state_select_rank(Id<Expr> e) { return select_rank(in_.str(d_.exprs[e].enum_path.back())); }

  // ---- AttributeCollector.fixValues :1378-1445：被换掉的变量 var 的 min / max / start / nominal 折算到代表变量的尺度上。
  // 先把 `var = 替换式` 对代表变量解出来（Solve.solveBody），右边 g 是 var 的式子；属性值代进 g 再化简就是代表变量那边的值。
  // g 对 var 的导数是负的、min 和 max 都有时两者对调（只有一个时不动，照抄）。nominal 不化简，走 getNominal。
  //
  // 解和求导不论 var 有没有属性都做（:817-821 的循环无条件调 solveBody，:1414-1420 无条件求导）：结果用不上时也可能失败。String 拼接的集合
  //（`s2 + "x" = s1`）在 solveBody → solveLinear 里对 String 残差求导，makeOne(String) 报内部错，omc 没有这一段（评审探针 LalX_str1）；
  // 这里 number() 同样 stop。
  void fix_values(Collector& col, const std::string& var, const std::string& keep) {
    const Id<Expr>* mn = col.min.get(var);
    const Id<Expr>* mx = col.max.get(var);
    const Id<Expr>* st = col.start.get(var);
    const Id<Expr>* nm = col.nominal.get(var);
    const Id<Expr> min0 = mn ? *mn : Id<Expr>{}, max0 = mx ? *mx : Id<Expr>{}, start0 = st ? *st : Id<Expr>{}, nom0 = nm ? *nm : Id<Expr>{};
    const Var& v = d_.var_arena[var_of(var)];
    Expr lhs;
    lhs.kind = ExprKind::Cref;
    lhs.ty = v.ty;
    lhs.cref = v.name;
    const Id<Expr> g = solve(d_.exprs.add(std::move(lhs)), repl_.at(var), keep, /*strict=*/false);
    if (r_.unsupported) return;
    const auto at = [&](Id<Expr> value) { return map_exp(g, [&](Id<Expr> x) { return is_cref(x, var) ? value : x; }); };
    Id<Expr> min_val = min0, max_val = max0;
    if (min_val.is_valid()) {
      min_val = simplify_exp(at(min0));
      col.min.add(var, min_val);
    }
    if (max_val.is_valid()) {
      max_val = simplify_exp(at(max0));
      col.max.add(var, max_val);
    }
    bool swap = false;
    const TypeKind tk = d_.exprs[g].ty.kind;
    if (tk == TypeKind::Real || tk == TypeKind::Integer) {
      const Id<Expr> der = simplify_exp(differentiate(g, var));
      if (r_.unsupported) return;
      swap = is_negative(der);
    }
    if (swap && min_val.is_valid() && max_val.is_valid()) {
      col.min.add(var, max_val);
      col.max.add(var, min_val);
    }
    if (start0.is_valid()) col.start.add(var, simplify_exp(at(start0)));
    if (nom0.is_valid()) col.nominal.add(var, nominal(at(nom0)));
  }
  // Expression.isNegative / isPositive（NFExpression.mo:4573-4585 一带）：字面量、CAST、UNARY；CREF 要查 max / min 属性，没接。
  bool is_negative(Id<Expr> id, bool negative = true) {
    const Expr& e = d_.exprs[id];
    switch (e.kind) {
      case ExprKind::IntLit: return negative ? e.ival < 0 : e.ival > 0;
      case ExprKind::RealLit: return negative ? e.rval < 0.0 : e.rval > 0.0;
      case ExprKind::Cast: return is_negative(e.a, negative);
      case ExprKind::Unary: return is_negative(e.a, !negative);
      case ExprKind::Cref: stop("sign of a reference"); return false;
      default: return false;
    }
  }
  // Expression.getNominal :2257-2299：computeNominal（cref 换成它的 nominal、去掉所有负号、减变加）映射一遍，再化简。
  Id<Expr> nominal(Id<Expr> id) {
    const Id<Expr> mapped = map_exp(id, [&](Id<Expr> x) {
      Expr e = d_.exprs[x];
      switch (e.kind) {
        case ExprKind::Cref: {
          const Id<Var> v = var_of(key(e.cref));
          if (v.is_valid())
            for (const FlatVarAttr& a : d_.var_arena[v].attrs)
              if (in_.str(a.name) == "nominal") return a.value;
          return x;
        }
        case ExprKind::IntLit: e.ival = e.ival < 0 ? -e.ival : e.ival; return d_.exprs.add(std::move(e));
        case ExprKind::RealLit: e.rval = e.rval < 0.0 ? -e.rval : e.rval; return d_.exprs.add(std::move(e));
        case ExprKind::Unary: return e.a;
        case ExprKind::Binary:
          if (e.bin != BinOp::Sub) return x;
          e.bin = BinOp::Add;
          return d_.exprs.add(std::move(e));
        case ExprKind::Multary:
          if (e.bin != BinOp::Add || e.inv_args.empty()) return x;
          e.args.insert(e.args.end(), e.inv_args.begin(), e.inv_args.end());
          e.inv_args.clear();
          return d_.exprs.add(std::move(e));
        default: return x;
      }
    });
    return simplify_exp(mapped);
  }

  // stateSelectAlways :1133-1156：两个以上 always，omc 报错，模块失败。
  void state_select_always(Collector& col) {
    int count = 0;
    for (const auto& [k, s] : col.state_select.items) count += state_select_rank(s) == 4;
    if (count > 1) stop("several StateSelect.always in an alias set (omc fails)");
  }
  // checkNominalThreshold :1039-1131：标量只看一组；常数里有 0 就报错失败（相差超过 1000 倍、非字面量只是告警）。
  void check_nominal_threshold(const Collector& col) {
    for (const auto& [k, v] : col.nominal.items)
      if (is_const_number(v) && is_zero(v)) return stop("zero nominal value in an alias set (omc fails)");
  }
  // isConstNumber :4712-4723
  bool is_const_number(Id<Expr> id) const {
    const Expr& e = d_.exprs[id];
    switch (e.kind) {
      case ExprKind::IntLit:
      case ExprKind::RealLit: return true;
      case ExprKind::Cast:
      case ExprKind::Unary: return is_const_number(e.a);
      default: return false;
    }
  }

  // ---- setNewAttributes :837-889：代表变量的新属性（都 overwrite），回写进 collector。
  void set_new_attributes(const std::string& keep, Collector& col) {
    Var& kv = d_.var_arena[var_of(keep)];
    if (const Id<Expr> m = extreme(col.min, /*maximum=*/true); m.is_valid()) {   // getMaximum：min 取最大
      set_attr(kv, in_.intern("min"), m, true);
      col.min.add(keep, m);
    }
    if (const Id<Expr> m = extreme(col.max, /*maximum=*/false); m.is_valid()) {  // getMinimum：max 取最小
      set_attr(kv, in_.intern("max"), m, true);
      col.max.add(keep, m);
    }
    if (r_.unsupported) return;
    const OrderedMap<Id<Expr>> fixed_start = start_fixed(col);
    if (r_.unsupported) return;
    if (fixed_start.items.size() == 1) {   // 恰好一个 fixed：它让给代表变量（避免两个 fixed），代表变量取它的 start
      const auto& [fixed_var, new_start] = fixed_start.items.front();
      set_attr(d_.var_arena[var_of(fixed_var)], in_.intern("fixed"), boolean(false), true);
      col.fixed.add(fixed_var, boolean(false));
      set_attr(kv, in_.intern("fixed"), boolean(true), true);
      col.fixed.add(keep, boolean(true));
      set_attr(kv, in_.intern("start"), new_start, true);
      col.start.add(keep, new_start);
    }
    // chooseStateSelect :1187-1215：只在代表变量自己有 stateSelect 时才改；选中 always 时 start 取那个变量的。
    if (!col.state_select.items.empty() && col.state_select.get(keep) != nullptr) {
      std::string cref = col.state_select.items.front().first;
      Id<Expr> value = col.state_select.items.front().second;
      if (col.state_select.items.size() > 1) {
        int best = 0;
        for (const auto& [k, s] : col.state_select.items) {
          if (state_select_rank(s) > best) {
            best = state_select_rank(s);
            cref = k;
            value = s;
          }
        }
      }
      set_attr(kv, in_.intern("stateSelect"), value, true);
      col.state_select.add(keep, value);
      if (state_select_rank(value) == 4) {
        const Id<Expr>* s = col.start.get(cref);
        if (s == nullptr) return stop("StateSelect.always without start (omc fails)");
        set_attr(kv, in_.intern("start"), *s, true);
        col.start.add(keep, *s);
      }
    }
    // chooseTearingSelect :1217-1238：同上，只在代表变量自己有时改。
    if (!col.tearing_select.items.empty() && col.tearing_select.get(keep) != nullptr) {
      Symbol value = col.tearing_select.items.front().second;
      if (col.tearing_select.items.size() > 1) {
        int best = 0;
        for (const auto& [k, s] : col.tearing_select.items) {
          if (select_rank(in_.str(s)) > best) {
            best = select_rank(in_.str(s));
            value = s;
          }
        }
      }
      kv.tearing_select = value;
    }
  }
  // getMaximum / getMinimum :928-984：常数（isConstNumber）取最值做成 REAL 放表头，其余原样跟在后面；一个就是它，多个就是 max(…) 调用。
  // getMinimum 造的也是 MAX_REAL（:977，照抄），所以打出来同样是 max。realValue 只认 REAL / INTEGER（:993-1001），别的常数 omc 失败。
  Id<Expr> extreme(const OrderedMap<Id<Expr>>& map, bool maximum) {
    std::vector<Id<Expr>> constants, rest;
    for (const auto& [k, v] : map.items) (is_const_number(v) ? constants : rest).push_back(v);
    if (!constants.empty()) {
      double best = 0.0;
      for (std::size_t i = 0; i < constants.size(); ++i) {
        const Expr& c = d_.exprs[constants[i]];
        if (c.kind != ExprKind::RealLit && c.kind != ExprKind::IntLit) {
          stop("min / max constant that is not a literal (omc fails)");
          return {};
        }
        const double x = c.kind == ExprKind::RealLit ? c.rval : static_cast<double>(c.ival);
        if (i == 0 || (maximum ? best < x : x < best)) best = x;
      }
      Expr r;
      r.kind = ExprKind::RealLit;
      r.ty = Type::real();
      r.rval = best;
      rest.insert(rest.begin(), d_.exprs.add(std::move(r)));
    }
    if (rest.empty()) return {};
    if (rest.size() == 1) return rest.front();
    Expr call;
    call.kind = ExprKind::Call;
    call.ty = Type::real();
    call.call_name = {in_.intern("max")};
    call.args = std::move(rest);
    return d_.exprs.add(std::move(call));
  }
  // setStartFixed :986-1037：fixed = true 的变量取 start（没有 start 时 getSafe 失败）；多于一个时 start 不全相等就报错失败，相等只告警。
  OrderedMap<Id<Expr>> start_fixed(const Collector& col) {
    OrderedMap<Id<Expr>> out;
    for (const auto& [k, f] : col.fixed.items) {
      const Expr& e = d_.exprs[f];
      if (e.kind != ExprKind::BoolLit || !e.bval) continue;   // Expression.isTrue
      const Id<Expr>* s = col.start.get(k);
      if (s == nullptr) {
        stop("fixed variable without start in an alias set (omc fails)");
        return out;
      }
      out.add(k, *s);
    }
    if (out.items.size() > 1) {
      const ExpCompare compare(d_.exprs, in_);
      for (const auto& [k, s] : out.items)
        if (compare(out.items.front().second, s) != 0) {
          stop("alias set fixed to different start values (omc fails)");
          return out;
        }
    }
    return out;
  }
  Id<Expr> boolean(bool b) {
    Expr e;
    e.kind = ExprKind::BoolLit;
    e.ty = Type::boolean();
    e.bval = b;
    return d_.exprs.add(std::move(e));
  }

  // ---- Causalize.simple（NBCausalize.mo:193-212）：全邻接 → 匹配（MATCHING）→ 升到 SORTING → Sorting.tarjan。
  //
  // 集合的方程连成一棵树（每条方程连一到两个变量，合并成环时 findSimpleEquation 已失败），去掉留下的变量（或加上常量绑定）后变量数 = 方程数，
  // 森林的完美匹配唯一，所以 PFPlusExternal 的结果不用复刻：逐个剥叶子就是它。
  // 排序：Sorting.tarjan 第一遍只为找代数环（树里没有），第二遍 SuperNode.create 把 m 换成 transposeScalar(mT)（NBSorting.mo:470），行内变量号
  // 降序（transposeScalar 的 List.sort(intLt)，NBAdjacency.mo:1095-1097），第三遍在它上面跑 tarjanScalar：按变量号从小到大起头，前驱按行序走，
  // 分量按完成序输出（strongConnect 头插、tarjanScalar 末尾 listReverse）。返回 (变量号, 方程号)，0 基。
  std::vector<std::pair<std::size_t, std::size_t>> causalize(const std::vector<std::string>& vars, const std::vector<SimpleEqn>& eqns) {
    std::vector<std::pair<std::size_t, std::size_t>> comps;
    const std::size_t n = vars.size();
    // 方程比变量多：自指方程和别的简单方程连进同一个集合，去重后少了变量（评审探针 LalX_selfext0 / dupconst）。这时最大匹配不唯一，选哪条由
    // PFPlusExternal（Compiler/runtime/matching.c match_pf_fair，带 lookahead 和正反交替扫描）定，没移植，SKIP。
    if (eqns.size() != n) {
      stop("alias set with more equations than variables (PFPlusExternal matching not ported)");
      return comps;
    }
    std::unordered_map<std::string, std::size_t> index;   // VariablePointers.fromList 的序
    for (std::size_t i = 0; i < n; ++i) index.emplace(vars[i], i);
    std::vector<std::vector<std::size_t>> occ(n);
    for (std::size_t e = 0; e < n; ++e) {
      for (const std::string& c : eqns[e].crefs) {
        const auto it = index.find(c);
        if (it != index.end()) occ[e].push_back(it->second);
      }
      // 邻接行是出现集合（UnorderedSet），`a = -a` 里的 a 只算一次。
      std::sort(occ[e].begin(), occ[e].end(), [](std::size_t x, std::size_t y) { return x > y; });
      occ[e].erase(std::unique(occ[e].begin(), occ[e].end()), occ[e].end());
    }
    constexpr std::size_t kNone = static_cast<std::size_t>(-1);
    std::vector<std::size_t> var_to_eqn(n, kNone), eqn_to_var(n, kNone);
    for (std::size_t matched = 0; matched < n; ++matched) {
      bool found = false;
      for (std::size_t e = 0; e < n && !found; ++e) {   // 只剩一个未匹配变量的方程
        if (eqn_to_var[e] != kNone) continue;
        std::size_t last = kNone, free = 0;
        for (std::size_t v : occ[e])
          if (var_to_eqn[v] == kNone) { ++free; last = v; }
        if (free == 1) {
          eqn_to_var[e] = last;
          var_to_eqn[last] = e;
          found = true;
        }
      }
      for (std::size_t v = 0; v < n && !found; ++v) {   // 只出现在一条未匹配方程里的变量
        if (var_to_eqn[v] != kNone) continue;
        std::size_t last = kNone, count = 0;
        for (std::size_t e = 0; e < n; ++e)
          if (eqn_to_var[e] == kNone && std::find(occ[e].begin(), occ[e].end(), v) != occ[e].end()) { ++count; last = e; }
        if (count == 1) {
          eqn_to_var[last] = v;
          var_to_eqn[v] = last;
          found = true;
        }
      }
      if (!found) {
        stop("alias set without a unique matching");
        return comps;
      }
    }
    // tarjanScalar（NBSorting.mo:300-327）+ strongConnect（:637-680）+ predecessors（:682-691）。
    std::vector<int> number(n, -1), lowlink(n, -1);
    std::vector<bool> on_stack(n, false);
    std::vector<std::size_t> stack;
    int idx = 0;
    std::vector<std::vector<std::size_t>> sccs;
    const auto connect = [&](const auto& self, std::size_t e) -> void {
      number[e] = lowlink[e] = idx++;
      on_stack[e] = true;
      stack.push_back(e);
      for (std::size_t v : occ[e]) {
        const std::size_t e2 = var_to_eqn[v];
        if (e2 == e) continue;
        if (number[e2] == -1) {
          self(self, e2);
          lowlink[e] = std::min(lowlink[e], lowlink[e2]);
        } else if (on_stack[e2]) {
          lowlink[e] = std::min(lowlink[e], number[e2]);
        }
      }
      if (lowlink[e] == number[e]) {
        std::vector<std::size_t> scc;
        std::size_t e2;
        do {
          e2 = stack.back();
          stack.pop_back();
          on_stack[e2] = false;
          scc.push_back(e2);
        } while (e2 != e);
        sccs.push_back(std::move(scc));
      }
    };
    for (std::size_t v = 0; v < n; ++v) {
      const std::size_t e = var_to_eqn[v];
      if (number[e] == -1) connect(connect, e);
    }
    for (const std::vector<std::size_t>& scc : sccs) {
      // 多于一条方程的分量是代数环：addSimple 只收 SINGLE_COMPONENT（:136-138），失败。
      if (scc.size() != 1) {
        stop("algebraic loop in an alias set (omc fails)");
        return {};
      }
      comps.emplace_back(eqn_to_var[scc.front()], scc.front());
    }
    return comps;
  }

  // ---- Solve.solveBody（NBSolve.mo:576-639）：solveSimple，不行再求导：线性就 solveLinear，否则 solveUnique（没做）。最后 Equation.simplify。
  // 返回解出的右边。
  //
  // 导数为 0 时 solveBody 不失败，只回 UNSOLVABLE、方程原样（:604-606）。addSimple 见到非 EXPLICIT 才报错失败（NBReplacements.mo:131-133），
  // 所以造替换时（strict）这是 omc 失败；fixValues 不看 status（NBAlias.mo:820-821），拿原方程化简后的右边接着用：替换式里已经没有代表变量的时候
  //（Integer 的 solveLinear 截断成 0、除以 inf 成 0.0，实跑 LeiT_int_lin、BdaR226）就是这样。
  Id<Expr> solve(Id<Eqn> id, const std::string& k) { return solve(d_.eqn_arena[id].lhs, d_.eqn_arena[id].rhs, k); }
  Id<Expr> solve(Id<Expr> lhs, Id<Expr> rhs, const std::string& k, bool strict = true) {
    Id<Expr> out;
    const Expr& l = d_.exprs[lhs];
    const Expr& r = d_.exprs[rhs];
    // solveSimpleLhsRhs :698-756，六种：cref = e、e = cref、-cref = e、not cref = e、e = -cref、e = not cref（都要求另一边不含 cref）。
    if (is_cref(lhs, k) && !contains(rhs, k)) out = rhs;
    else if (is_cref(rhs, k) && !contains(lhs, k)) out = lhs;
    else if (l.kind == ExprKind::Unary && is_cref(l.a, k) && !contains(rhs, k)) out = negate(rhs);
    else if (l.kind == ExprKind::LogicalNot && is_cref(l.a, k) && !contains(rhs, k)) out = logic_negate(rhs);
    else if (r.kind == ExprKind::Unary && is_cref(r.a, k) && !contains(lhs, k)) out = negate(lhs);
    else if (r.kind == ExprKind::LogicalNot && is_cref(r.a, k) && !contains(lhs, k)) out = logic_negate(lhs);
    else out = solve_linear(lhs, rhs, k, strict);
    if (r_.unsupported) return lhs;
    return simplify_exp(out);   // Equation.simplify(eqn)（:633）：左边是 cref，化简不变
  }

  // getResidualExp（NBEquation.mo:1947-1949）= MULTARY({rhs}, {lhs}, ADD(typeOf(lhs)))；导数 = simplify(differentiate(res, cref))；
  // 导数不含 cref 就是线性：cref = -(res[cref := 0] / 导数)（solveLinear :782-806，UNARY 和 MULTARY 用变量的类型）。
  Id<Expr> solve_linear(Id<Expr> lhs, Id<Expr> rhs, const std::string& k, bool strict) {
    Expr res;
    res.kind = ExprKind::Multary;
    res.ty = d_.exprs[lhs].ty;
    res.bin = BinOp::Add;
    res.args = {rhs};
    res.inv_args = {lhs};
    const Id<Expr> residual = d_.exprs.add(std::move(res));
    const Id<Expr> der = simplify_exp(differentiate(residual, k));
    if (r_.unsupported) return lhs;
    if (is_zero(der)) {
      if (!strict) return rhs;   // UNSOLVABLE，方程原样（见 solve）
      stop("equation not solvable for an alias variable (omc fails)");
      return lhs;
    }
    if (contains(der, k)) {
      stop("equation needs Solve.solveUnique");
      return lhs;
    }
    const Type ty = d_.var_arena[var_of(k)].ty;
    const Id<Expr> zero = number(ty, 0);
    if (r_.unsupported) return lhs;
    // Replacements.single（NBReplacements.mo:81-97）：Expression.map，和 cref 相等的节点换成 0。
    const Id<Expr> numerator = map_exp(residual, [&](Id<Expr> x) { return is_cref(x, k) ? zero : x; });
    Expr div;
    div.kind = ExprKind::Multary;
    div.ty = ty;
    div.bin = BinOp::Mul;
    div.args = {numerator};
    div.inv_args = {der};
    Expr neg;
    neg.kind = ExprKind::Unary;
    neg.ty = ty;
    neg.a = d_.exprs.add(std::move(div));
    return d_.exprs.add(std::move(neg));
  }

  // ---- differentiateExpression（NBDifferentiate.mo:444-578），SIMPLE：对 cref 求偏导，别的变量和 time 都是常数。只收简单方程里会有的节点。

  Id<Expr> differentiate(Id<Expr> id, const std::string& k) {
    if (r_.unsupported) return id;
    const Expr e = d_.exprs[id];
    switch (e.kind) {
      case ExprKind::IntLit: return number(Type::integer(), 0);
      case ExprKind::RealLit: return number(Type::real(), 0);
      case ExprKind::StringLit:
      case ExprKind::BoolLit:
      case ExprKind::Logical:
      case ExprKind::LogicalNot:
      case ExprKind::Relation:
      case ExprKind::EnumLit:
        return id;
      case ExprKind::Cref:   // differentiateComponentRef :580-748：time → 0；SIMPLE 下 x → 1、别的 → 0（类型是 cref 的）
        return number(e.ty, key(e.cref) == k ? 1 : 0);
      case ExprKind::Unary:
      case ExprKind::Cast: {
        Expr out = e;
        out.a = differentiate(e.a, k);
        return d_.exprs.add(std::move(out));
      }
      case ExprKind::Multary: return differentiate_multary(e, k);
      case ExprKind::Binary: return differentiate_binary(e, k);
      default:
        stop("differentiate expression kind");
        return id;
    }
  }
  Id<Expr> multary(BinOp op, const Type& ty, std::vector<Id<Expr>> args, std::vector<Id<Expr>> inv) {
    Expr out;
    out.kind = ExprKind::Multary;
    out.ty = ty;
    out.bin = op;
    out.args = std::move(args);
    out.inv_args = std::move(inv);
    return d_.exprs.add(std::move(out));
  }
  Id<Expr> binary(Id<Expr> x, BinOp op, Id<Expr> y, const Type& ty) {
    Expr out;
    out.kind = ExprKind::Binary;
    out.ty = ty;
    out.bin = op;
    out.a = x;
    out.b = y;
    return d_.exprs.add(std::move(out));
  }
  // differentiateMultary :1838-1923。
  Id<Expr> differentiate_multary(const Expr& e, const std::string& k) {
    if (e.bin == BinOp::Add) {   // (f + g - h)' = f' + g' - h'
      std::vector<Id<Expr>> args, inv;
      for (Id<Expr> x : e.args) args.push_back(differentiate(x, k));
      for (Id<Expr> x : e.inv_args) inv.push_back(differentiate(x, k));
      return multary(BinOp::Add, e.ty, std::move(args), std::move(inv));
    }
    if (e.inv_args.empty()) return multary(BinOp::Add, e.ty, product_rule(e.args, e.ty, k), {});
    // (f / g)' = (f'g - g'f) / g^2，f、g 是积（:1882-1910）
    const Id<Expr> diff_enumerator = multary(BinOp::Add, e.ty, product_rule(e.args, e.ty, k), {});
    const Id<Expr> diff_divisor = multary(BinOp::Add, e.ty, product_rule(e.inv_args, e.ty, k), {});
    const Id<Expr> divisor = multary(BinOp::Mul, e.ty, e.inv_args, {});
    std::vector<Id<Expr>> fg{diff_enumerator}, gf{diff_divisor};
    fg.insert(fg.end(), e.inv_args.begin(), e.inv_args.end());
    gf.insert(gf.end(), e.args.begin(), e.args.end());
    const Id<Expr> top = multary(BinOp::Add, e.ty, {multary(BinOp::Mul, e.ty, fg, {})}, {multary(BinOp::Mul, e.ty, gf, {})});
    return multary(BinOp::Mul, e.ty, {top}, {binary(divisor, BinOp::Pow, number(Type::real(), 2), e.ty)});
  }
  // differentiateMultaryMultiplicationArgs :1925-1948：第 i 项是把第 i 个因子换成导数的积，按原序。
  std::vector<Id<Expr>> product_rule(const std::vector<Id<Expr>>& args, const Type& ty, const std::string& k) {
    std::vector<Id<Expr>> diffs;
    for (Id<Expr> x : args) diffs.push_back(differentiate(x, k));
    std::vector<Id<Expr>> out;
    for (std::size_t i = 0; i < args.size(); ++i) {
      std::vector<Id<Expr>> factors = args;
      factors[i] = diffs[i];
      out.push_back(multary(BinOp::Mul, ty, std::move(factors), {}));
    }
    return out;
  }
  // differentiateBinary :1706-1836（幂没做）。
  Id<Expr> differentiate_binary(const Expr& e, const std::string& k) {
    if (e.bin == BinOp::Pow) {
      stop("differentiate power");
      return {};
    }
    const Id<Expr> d1 = differentiate(e.a, k), d2 = differentiate(e.b, k);
    switch (e.bin) {
      case BinOp::Add: return multary(BinOp::Add, e.ty, {d1, d2}, {});
      case BinOp::Sub: return multary(BinOp::Add, e.ty, {d1}, {d2});
      case BinOp::Mul: return multary(BinOp::Add, e.ty, {binary(e.a, BinOp::Mul, d2, e.ty), binary(d1, BinOp::Mul, e.b, e.ty)}, {});
      case BinOp::Div: {
        const Id<Expr> top = multary(BinOp::Add, e.ty, {binary(e.a, BinOp::Mul, d2, e.ty)}, {binary(d1, BinOp::Mul, e.b, e.ty)});
        return multary(BinOp::Mul, e.ty, {top}, {binary(e.b, BinOp::Pow, number(Type::real(), 2), e.ty)});
      }
      case BinOp::Pow: break;
    }
    YMC_UNREACHABLE("BinOp");
  }

  // ---- 表达式工具（NFExpression.mo）

  // makeZero / makeOne :4836-4883（REAL(2.0) 也借这里造）。
  Id<Expr> number(const Type& ty, int v) {
    Expr e;
    e.ty = ty;
    if (ty.kind == TypeKind::Real) {
      e.kind = ExprKind::RealLit;
      e.rval = v;
    } else if (ty.kind == TypeKind::Integer) {
      e.kind = ExprKind::IntLit;
      e.ival = v;
    } else {
      stop("zero or one of a non-numeric type");
      e.kind = ExprKind::IntLit;
    }
    return d_.exprs.add(std::move(e));
  }
  // isZero :4504-4516
  bool is_zero(Id<Expr> id) const {
    const Expr& e = d_.exprs[id];
    switch (e.kind) {
      case ExprKind::IntLit: return e.ival == 0;
      case ExprKind::RealLit: return e.rval == 0.0;
      case ExprKind::Cast:
      case ExprKind::Unary: return is_zero(e.a);
      default: return false;
    }
  }
  // negate :5035-5046
  Id<Expr> negate(Id<Expr> id) {
    Expr e = d_.exprs[id];
    switch (e.kind) {
      case ExprKind::IntLit: e.ival = -e.ival; return d_.exprs.add(std::move(e));
      case ExprKind::RealLit: e.rval = -e.rval; return d_.exprs.add(std::move(e));
      case ExprKind::Cast: e.a = negate(e.a); return d_.exprs.add(std::move(e));
      case ExprKind::Unary: return e.a;
      default: {
        Expr out;
        out.kind = ExprKind::Unary;
        out.ty = e.ty;
        out.a = id;
        return d_.exprs.add(std::move(out));
      }
    }
  }
  // logicNegate :5048-5058
  Id<Expr> logic_negate(Id<Expr> id) {
    Expr e = d_.exprs[id];
    if (e.kind == ExprKind::BoolLit) {
      e.bval = !e.bval;
      return d_.exprs.add(std::move(e));
    }
    if (e.kind == ExprKind::LogicalNot) return e.a;
    Expr out;
    out.kind = ExprKind::LogicalNot;
    out.ty = e.ty;
    out.a = id;
    return d_.exprs.add(std::move(out));
  }
  // SimplifyExp.simplifyDump。
  Id<Expr> simplify_exp(Id<Expr> id) {
    if (r_.unsupported) return id;
    std::string why;
    const Id<Expr> out = simplify(d_.exprs, in_, id, why);
    if (!why.empty()) stop(why);
    return out;
  }

  // Expression.map（NFExpression.mo:2651-2818）：先子式后本节点，子式变了就造新节点，再把本节点交给 f。只收 lower 放行的节点。
  template <class F>
  Id<Expr> map_exp(Id<Expr> id, const F& f) {
    if (!id.is_valid() || r_.unsupported) return id;
    Expr e = d_.exprs[id];
    switch (e.kind) {
      case ExprKind::IntLit:
      case ExprKind::RealLit:
      case ExprKind::BoolLit:
      case ExprKind::StringLit:
      case ExprKind::EnumLit:
      case ExprKind::Cref:   // 标量 cref 没有下标可走
      case ExprKind::Wild:
        return f(id);
      case ExprKind::Binary:
      case ExprKind::Relation:
      case ExprKind::Logical:
      case ExprKind::Unary:
      case ExprKind::LogicalNot:
      case ExprKind::Cast:
      case ExprKind::IfExp:
      case ExprKind::Range:
      case ExprKind::Multary:
      case ExprKind::Array:
      case ExprKind::Tuple:
      case ExprKind::Call: {
        bool changed = false;
        const auto child = [&](Id<Expr>& x) {
          if (!x.is_valid()) return;
          const Id<Expr> y = map_exp(x, f);
          changed = changed || y != x;
          x = y;
        };
        child(e.a);
        child(e.b);
        child(e.c);
        for (Id<Expr>& x : e.args) child(x);
        for (Id<Expr>& x : e.inv_args) child(x);
        if (r_.unsupported) return id;
        return f(changed ? d_.exprs.add(std::move(e)) : id);
      }
      default:
        stop("expression kind");
        return id;
    }
  }
  // applySimpleExp（NBReplacements.mo:187-218）：cref 在表里就换成替换式（不再往里走）。
  Id<Expr> replace(Id<Expr> id, const std::unordered_map<std::string, Id<Expr>>& repl) {
    if (repl.empty()) return id;
    return map_exp(id, [&](Id<Expr> x) {
      const Expr& e = d_.exprs[x];
      if (e.kind != ExprKind::Cref) return x;
      const auto it = repl.find(key(e.cref));
      return it == repl.end() ? x : it->second;
    });
  }

  // ---- Equation.map(eq, funcExp)（NBEquation.mo:1389-1485）：每个式子走 Expression.map。when 的赋值左边也走，reinit 的状态不走（只给
  // funcCrefOpt）；if 方程走条件和体里的方程；算法走 Statement.mapExp（NFStatement.mo:559-634：for 先体后范围，assert 连级别也走）。

  template <class F>
  void map_equation(Id<Eqn> id, const F& f) {
    Eqn q = d_.eqn_arena[id];
    switch (q.kind) {
      case EqnKind::Scalar:
        q.lhs = map_exp(q.lhs, f);
        q.rhs = map_exp(q.rhs, f);
        break;
      case EqnKind::When:
        for (WhenBranch& br : q.when) {
          br.condition = map_exp(br.condition, f);
          for (WhenStmt& st : br.stmts) {
            if (st.kind != WhenStmtKind::Reinit) st.a = map_exp(st.a, f);
            st.b = map_exp(st.b, f);
          }
        }
        break;
      case EqnKind::If:
        for (IfBranch& br : q.if_branches) {
          br.condition = map_exp(br.condition, f);
          map_equation(br.eqn, f);
        }
        break;
      case EqnKind::Algorithm:
        map_statements(q.alg, f);
        break;
    }
    if (r_.unsupported) return;
    d_.eqn_arena[id] = std::move(q);
  }
  template <class F>
  void map_statements(std::vector<Stmt>& stmts, const F& f) {
    for (Stmt& st : stmts) {
      switch (st.kind) {
        case StmtKind::For:
          map_statements(st.body, f);
          st.a = map_exp(st.a, f);
          break;
        case StmtKind::If:
        case StmtKind::When:
          for (StmtBranch& br : st.branches) {
            br.condition = map_exp(br.condition, f);
            map_statements(br.body, f);
          }
          break;
        case StmtKind::While:
          st.a = map_exp(st.a, f);
          map_statements(st.body, f);
          break;
        case StmtKind::Assign:
        case StmtKind::Assert:
        case StmtKind::Terminate:
        case StmtKind::NoRetCall:
          st.a = map_exp(st.a, f);
          st.b = map_exp(st.b, f);
          st.c = map_exp(st.c, f);
          break;
        case StmtKind::Break:
          break;
      }
    }
  }
  // EqData.mapExp 走的方程（NBEquation.mo:4387-4400）：simulation、continuous、clocked、discretes、initials、auxiliaries，不碰 removed。
  // simulation 和 continuous / discretes 是同一批指针，omc 映射两遍；替换式里不再有要换的 cref（见 apply_simple），第二遍不变，这里一遍。
  std::vector<Id<Eqn>> mapped_equations() const {
    std::vector<Id<Eqn>> out;
    std::unordered_set<std::uint32_t> seen;
    const EqnData& ed = d_.eqns;
    for (const EqnSet* set : {&ed.simulation, &ed.continuous, &ed.discretes, &ed.initials})
      for (Id<Eqn> id : set->items)
        if (!deleted_.count(id.raw) && seen.insert(id.raw).second) out.push_back(id);
    return out;
  }

  // ---- checkReplacements :286-316：所有方程里 pre(x) 的 x（filterPre :331-348，只认实参恰是一个 cref 的 pre）不能换。不能换的替换改成一条
  // `x = 替换式` 的方程（$RES_SIM_n，按类型进连续 / 离散），x 留在原桶里。

  void check_replacements() {
    std::unordered_set<std::string> exceptions;
    for (Id<Eqn> id : mapped_equations()) {
      map_equation(id, [&](Id<Expr> x) {
        const Expr& e = d_.exprs[x];
        if (e.kind == ExprKind::Call && e.call_name.size() == 1 && in_.str(e.call_name[0]) == "pre" && e.args.size() == 1 &&
            d_.exprs[e.args[0]].kind == ExprKind::Cref)
          exceptions.insert(key(d_.exprs[e.args[0]].cref));
        return x;
      });
    }
    std::vector<std::string> kept;
    std::unordered_map<std::string, Id<Expr>> repl;
    for (const std::string& k : repl_keys_) {
      if (!exceptions.count(k)) {
        kept.push_back(k);
        repl.emplace(k, repl_.at(k));
        continue;
      }
      // Equation.makeAssignment(fromCref(cref), exp, uniqueIndex, "SIM", …)；属性 lowerEquationAttributes(类型)：Real 连续，其余离散。
      const Var& v = d_.var_arena[var_of(k)];
      Expr lhs;
      lhs.kind = ExprKind::Cref;
      lhs.ty = v.ty;
      lhs.cref = v.name;
      Eqn q;
      q.lhs = d_.exprs.add(std::move(lhs));
      q.rhs = repl_.at(k);
      q.name.parts.push_back(ExprCrefPart{in_.intern("$RES_SIM_" + std::to_string(d_.eqns.unique_index++)), {}});
      q.time = v.ty.kind == TypeKind::Real ? EqnTime::Continuous : EqnTime::Discrete;
      aux_.insert(aux_.begin(), d_.eqn_arena.add(std::move(q)));   // auxEquations 头插
    }
    repl_keys_ = std::move(kept);
    repl_ = std::move(repl);
  }

  // ---- Replacements.applySimple（NBReplacements.mo:143-185）：表空就什么都不做。式子换完不化简（Simplify 2 才化简）。

  void apply_simple() {
    if (repl_.empty()) return;
    for (Id<Eqn> id : mapped_equations()) {
      map_equation(id, [&](Id<Expr> x) {
        const Expr& e = d_.exprs[x];
        if (e.kind != ExprKind::Cref) return x;
        const auto it = repl_.find(key(e.cref));
        return it == repl_.end() ? x : it->second;
      });
      if (r_.unsupported) return;
    }
    // applySimpleVar：全部变量（varData.variables）的绑定；aliasVars 这时还是空的。
    for (const auto& [name, v] : vars_) {
      Var& var = d_.var_arena[v];
      if (var.binding.is_valid()) var.binding = replace(var.binding, repl_);
    }
    // 被换掉的变量，绑定改成替换式（Binding.update NFBinding.mo:721-746，没有绑定的造一个）。
    for (const std::string& k : repl_keys_) d_.var_arena[var_of(k)].binding = repl_.at(k);
  }

  // ---- aliasDefault 的收尾 :241-278

  void finish() {
    EqnData& ed = d_.eqns;
    // EquationPointers.compress：simulation（newEquations）、equations、continuous、discretes 删掉简单方程，余下的保序。
    for (EqnSet* set : {&ed.simulation, &ed.equations, &ed.continuous, &ed.discretes}) {
      std::vector<Id<Eqn>> keep;
      for (Id<Eqn> id : set->items)
        if (!deleted_.count(id.raw)) keep.push_back(id);
      set->items = std::move(keep);
    }
    std::vector<Id<Var>> alias_vars;
    for (const std::string& k : repl_keys_) alias_vars.push_back(var_of(k));
    // removeList（返回值都接了，所以压紧，不留空位）：variables、unknowns、algebraics、states、discretes、clocks、initials。
    // top_level_inputs 不在表里：被换掉的顶层 input 留在 Top Level Input 桶里，也进 Alias 桶（实跑 LalB_input）。
    VarData& vd = d_.vars;
    const std::unordered_set<std::uint32_t> gone = [&] {
      std::unordered_set<std::uint32_t> s;
      for (Id<Var> v : alias_vars) s.insert(v.raw);
      return s;
    }();
    for (VarSet* set : {&vd.unknowns, &vd.algebraics, &vd.states, &vd.discretes}) {
      std::vector<Id<Var>> keep;
      for (Id<Var> v : set->items)
        if (!gone.count(v.raw)) keep.push_back(v);
      set->items = std::move(keep);
    }
    // List.splitOnTrue（保序）：先分出非平凡的（替换式不是 cref / -cref / not cref，而且含时变量），再分出常量 / 参数的。
    std::vector<Id<Var>> non_trivial, constant, trivial;
    for (Id<Var> v : alias_vars) {
      const Id<Expr> b = d_.var_arena[v].binding;
      if (!trivial_cref(b) && time_dependent(b)) non_trivial.push_back(v);
      else if (!time_dependent(b)) constant.push_back(v);
      else trivial.push_back(v);
    }
    const Symbol start = in_.intern("start"), fixed = in_.intern("fixed");
    for (Id<Var> v : constant) {
      Var& var = d_.var_arena[v];
      var.kind = VarKind::Parameter;                          // setVarKind(PARAMETER(NONE()))
      set_attr(var, start, var.binding, /*overwrite=*/true);  // setBindingAsStart：覆盖原来的 start
      Expr t;                                                 // setFixed(b = true, overwrite = false)：原来有 fixed 就不动（实跑 LalB_fixedfalse）
      t.kind = ExprKind::BoolLit;
      t.ty = Type::boolean();
      t.bval = true;
      set_attr(var, fixed, d_.exprs.add(std::move(t)), /*overwrite=*/false);
      add_var(vd.parameters, v);
    }
    for (Id<Var> v : trivial) add_var(vd.alias_vars, v);
    for (Id<Var> v : non_trivial) add_var(vd.non_trivial_alias, v);
    // 非平凡别名的绑定方程（generateBindingEquation(var, uniqueIndex, false)，$RES_BND_n，右边再化简一遍）只进 removed，不进 equations（:275-277）。
    for (Id<Var> v : non_trivial) {
      const Var& var = d_.var_arena[v];
      Eqn q;
      Expr lhs;
      lhs.kind = ExprKind::Cref;
      lhs.ty = var.ty;
      lhs.cref = var.name;
      q.lhs = d_.exprs.add(std::move(lhs));
      q.rhs = simplify_exp(d_.var_arena[v].binding);
      if (r_.unsupported) return;
      q.name.parts.push_back(ExprCrefPart{in_.intern("$RES_BND_" + std::to_string(ed.unique_index++)), {}});
      const VarKind kind = d_.var_arena[v].kind;   // BVariable.isContinuous(var, init = false)
      q.time = kind == VarKind::Discrete || kind == VarKind::Parameter || kind == VarKind::Constant ? EqnTime::Discrete : EqnTime::Continuous;
      ed.removed.items.push_back(d_.eqn_arena.add(std::move(q)));
    }
    // EqData.addUntypedList(eqData, auxEquations, newName = false)：equations 按表序（auxEquations 是头插的，所以是造的反序）；typeList 再头插一次，
    // 各桶回到造的序。
    for (Id<Eqn> q : aux_) ed.equations.items.push_back(q);
    for (auto it = aux_.rbegin(); it != aux_.rend(); ++it) {
      (d_.eqn_arena[*it].time == EqnTime::Continuous ? ed.continuous : ed.discretes).items.push_back(*it);
      ed.simulation.items.push_back(*it);
    }
  }

  // Expression.isTrivialCref :400-410：CREF、UNARY(CREF)、LUNARY(CREF)。
  bool trivial_cref(Id<Expr> id) const {
    const Expr& e = d_.exprs[id];
    if (e.kind == ExprKind::Cref) return true;
    return (e.kind == ExprKind::Unary || e.kind == ExprKind::LogicalNot) && d_.exprs[e.a].kind == ExprKind::Cref;
  }
  // checkExpMap(binding, isTimeDependent)：式子里有一个 cref 的变量种类不是 PARAMETER / CONSTANT / ITERATOR / START 就算（time 也算，
  // VariableKind.isTimeDependent NFBackendExtension.mo:279-291）。
  bool time_dependent(Id<Expr> id) const {
    if (!id.is_valid()) return false;
    const Expr& e = d_.exprs[id];
    if (e.kind == ExprKind::Cref) {
      const Id<Var> v = var_of(key(e.cref));
      if (!v.is_valid()) return true;
      const VarKind k = d_.var_arena[v].kind;
      return k != VarKind::Parameter && k != VarKind::Constant && k != VarKind::Iterator;
    }
    if (time_dependent(e.a) || time_dependent(e.b) || time_dependent(e.c)) return true;
    for (Id<Expr> x : e.args)
      if (time_dependent(x)) return true;
    for (Id<Expr> x : e.inv_args)
      if (time_dependent(x)) return true;
    return false;
  }
  // VariableAttributes.setStartAttribute / setFixed（NFBackendExtension.mo:532-626）：overwrite 为假时已有的不动。
  static void set_attr(Var& v, Symbol name, Id<Expr> value, bool overwrite) {
    for (FlatVarAttr& a : v.attrs) {
      if (a.name != name) continue;
      if (overwrite) a.value = value;
      return;
    }
    v.attrs.push_back(FlatVarAttr{name, value});
  }
  // VariablePointers.add：已在就原地更新，否则接在尾巴上。
  static void add_var(VarSet& set, Id<Var> v) {
    if (std::find(set.items.begin(), set.items.end(), v) == set.items.end()) set.items.push_back(v);
  }

  BackendDAE& d_;
  Interner& in_;
  ModuleResult r_;
  std::unordered_map<std::string, Id<Var>> vars_;   // 点分名 → 变量
  std::vector<AliasSet> sets_;                       // Pointer<AliasSet> 的池：下标就是指针
  std::vector<std::string> keys_;                    // Map<cref, SetPtr> 的键，插入序
  std::unordered_map<std::string, int> set_of_;      // 键 → 集合下标
  std::unordered_set<std::uint32_t> deleted_;        // 被 foldRemovePtr 删掉的简单方程
  std::vector<std::string> repl_keys_;               // 替换表的键，插入序（UnorderedMap.keyList）
  std::unordered_map<std::string, Id<Expr>> repl_;
  std::vector<Id<Eqn>> aux_;                         // checkReplacements 的 auxEquations（表序，头插）
};

}  // namespace

ModuleResult alias(BackendDAE& dae, Interner& in) { return AliasModule(dae, in).run(); }

}  // namespace ymc::bdae
