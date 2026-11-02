// 差分驱动：解释 gen_ops.py 写的操作脚本，容器用 src/bdae/omc_unordered.h，日志格式与 mkhsh.py 生成的 Hsh.mo 一致，由 diff.py 逐行比。
//
// 比 Hsh.mo 多一种寄存器：c = Set<cref 文本>，走真实的 cref_hash（文本先解析成 ExprCref），chk.py 用它预测分区序和 Full 邻接行。
// omc 那边没有这种寄存器：ComponentRef 编译不进脚本，cref 的对拍靠 L2 的 NB 实跑输出。
//
// 寄存器和 Hsh.mo 一样是引用语义（shared_ptr，一开始全指向同一个对象，对应 arrayCreate），su 之后结果和入参之一是同一个对象，这样
// 脚本才能看出 union 改了谁。
//
// 用法：
//   cmake --build <构建目录> -j 3 --target omc_hash_drv      # tests/CMakeLists.txt 里的目标，EXCLUDE_FROM_ALL
//   <构建目录>/tests/omc_hash_drv ops.txt
#include <cstdint>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "bdae/omc_unordered.h"

using namespace ymc::bdae;

namespace {

constexpr std::size_t kRegisters = 4096;   // chk.py 的一行方程可能要用很多临时集合

std::vector<std::string> split(const std::string& s, char ch) {
  std::vector<std::string> out;
  std::string cur;
  for (char c : s) {
    if (c == ch) {
      out.push_back(cur);
      cur.clear();
    } else {
      cur.push_back(c);
    }
  }
  if (!cur.empty()) out.push_back(cur);
  return out;
}

std::string str(const std::string& s) { return s; }
std::string str(OmcInt i) { return std::to_string(i); }

template <class C>
std::string join(const C& c) {
  std::string out;
  bool first = true;
  for (const auto& x : c) {
    if (!first) out += ',';
    first = false;
    out += str(x);
  }
  return out;
}

struct TextHash {
  OmcInt operator()(const std::string& s) const { return string_hash_djb2(s); }
};

// cref 文本（a.b[1,2].c，下标逗号分隔、无空格）→ 真实 ExprCref → cref_hash。下标文本放表里，Id<Expr> 的 raw 当下标用，不用造表达式。
ymc::Interner& interner() {
  static ymc::Interner in;
  return in;
}
OmcInt cref_text_hash(const std::string& s) {
  ymc::ExprCref c;
  std::vector<std::string> subs;
  std::size_t i = 0;
  while (i < s.size()) {
    std::string name;
    while (i < s.size() && s[i] != '.' && s[i] != '[') name.push_back(s[i++]);
    ymc::ExprCrefPart part;
    part.name = interner().intern(name);
    if (i < s.size() && s[i] == '[') {
      ++i;
      std::string cur;
      int depth = 0;
      const auto flush = [&] {
        subs.push_back(cur);
        part.subscripts.push_back(ymc::Id<ymc::Expr>::from_index(static_cast<std::uint32_t>(subs.size() - 1)));
        cur.clear();
      };
      while (i < s.size() && !(s[i] == ']' && depth == 0)) {
        if (s[i] == '[' || s[i] == '(') ++depth;
        if (s[i] == ']' || s[i] == ')') --depth;
        if (s[i] == ',' && depth == 0) {
          flush();
        } else {
          cur.push_back(s[i]);
        }
        ++i;
      }
      ++i;
      flush();
    }
    if (i < s.size() && s[i] == '.') ++i;
    c.parts.push_back(std::move(part));
  }
  return cref_hash(c, interner(), [&](ymc::Id<ymc::Expr> id) { return std::string_view(subs[id.raw]); });
}
struct CrefTextHash {
  OmcInt operator()(const std::string& s) const { return cref_text_hash(s); }
};

std::string parse(const std::string& t, std::string*) { return t; }
OmcInt parse(const std::string& t, OmcInt*) { return std::stoll(t); }

std::string app0(const std::string& k) { return k + "z"; }
std::string app1(const std::string& k) { return k.size() > 2 ? k.substr(0, 2) : k; }
bool even(const std::string& k) { return k.size() % 2 == 0; }
OmcInt app0(OmcInt k) { return k * 2; }
OmcInt app1(OmcInt k) { return int_mod(k, 7); }
bool even(OmcInt k) { return int_mod(k, 2) == 0; }

template <class T, class H>
struct SetOps {
  using Set = UnorderedSet<T, H>;
  std::vector<std::shared_ptr<Set>> R;
  char P, p;

  SetOps(char upper, char lower) : R(kRegisters), P(upper), p(lower) {
    auto init = std::make_shared<Set>();
    for (auto& r : R) r = init;
  }
  T key(const std::string& t) { return parse(t, static_cast<T*>(nullptr)); }
  void dump(int r) {
    Set& s = *R[r];
    std::cout << P << r << " n=" << s.size() << " b=" << s.bucket_count() << " L=" << join(s.to_list()) << " A=" << join(s.to_array()) << "\n";
  }
  bool op(const std::string& o, const std::vector<std::string>& t) {
    if (o[0] != p) return false;
    const std::string q = o.substr(1);
    const auto I = [&](std::size_t i) { return std::stoi(t[i]); };
    if (q == "n") {
      const int a = I(1);
      R[I(0)] = a < 0 ? std::make_shared<Set>() : std::make_shared<Set>(H{}, std::equal_to<T>{}, a);   // a < 0 走默认桶数
    } else if (q == "fl") {
      std::vector<T> ks;
      for (std::size_t i = 1; i < t.size(); ++i) ks.push_back(key(t[i]));
      R[I(0)] = std::make_shared<Set>(Set::from_list(ks));
    } else if (q == "a") {
      R[I(0)]->add(key(t[1]));
    } else if (q == "an") {
      R[I(0)]->add_new(key(t[1]));
    } else if (q == "au") {
      std::cout << "au " << (R[I(0)]->add_unique(key(t[1])) ? "ok" : "fail") << "\n";
    } else if (q == "r") {
      std::cout << "r " << (R[I(0)]->remove(key(t[1])) ? "true" : "false") << "\n";
    } else if (q == "c") {
      std::cout << "c " << (R[I(0)]->contains(key(t[1])) ? "true" : "false") << "\n";
    } else if (q == "u") {
      auto a = R[I(1)], b = R[I(2)];
      Set& x = Set::union_sets(*a, *b);
      R[I(0)] = &x == a.get() ? a : b;
    } else if (q == "ul") {
      std::vector<Set> v;
      for (std::size_t i = 1; i < t.size(); ++i) v.push_back(std::move(*R[I(i)]));
      R[I(0)] = std::make_shared<Set>(Set::union_list(std::move(v)));
    } else if (q == "d") {
      R[I(0)] = std::make_shared<Set>(Set::difference(*R[I(1)], *R[I(2)]));
    } else if (q == "sd") {
      R[I(0)] = std::make_shared<Set>(Set::sym_difference(*R[I(1)], *R[I(2)]));
    } else if (q == "fof") {
      R[I(0)] = std::make_shared<Set>(R[I(1)]->filter_on_false([](const T& k) { return even(k); }));
    } else if (q == "sot") {
      auto pr = R[I(2)]->split_on_true([](const T& k) { return even(k); });
      R[I(0)] = std::make_shared<Set>(std::move(pr.first));
      R[I(1)] = std::make_shared<Set>(std::move(pr.second));
    } else if (q == "ap") {
      if (I(1) == 0) {
        R[I(0)]->apply([](const T& k) { return app0(k); });
      } else {
        R[I(0)]->apply([](const T& k) { return app1(k); });
      }
    } else if (q == "uq") {
      std::vector<T> ks;
      for (const std::string& x : t) ks.push_back(key(x));
      std::cout << "uq " << join(Set::unique_list(ks)) << "\n";
    } else if (q == "dump") {
      dump(I(0));
    } else {
      return false;
    }
    return true;
  }
};

template <class K, class H>
struct MapOps {
  using Map = UnorderedMap<K, OmcInt, H>;
  std::vector<std::shared_ptr<Map>> R;
  char P, p;

  MapOps(char upper, char lower) : R(kRegisters), P(upper), p(lower) {
    auto init = std::make_shared<Map>();
    for (auto& r : R) r = init;
  }
  K key(const std::string& t) { return parse(t, static_cast<K*>(nullptr)); }
  void dump(int r) {
    Map& m = *R[r];
    std::cout << P << r << " n=" << m.size() << " b=" << m.bucket_count() << " K=" << join(m.keys()) << " V=" << join(m.values()) << "\n";
  }
  bool op(const std::string& o, const std::vector<std::string>& t) {
    if (o[0] != p) return false;
    const std::string q = o.substr(1);
    const auto I = [&](std::size_t i) { return std::stoi(t[i]); };
    if (q == "n") {
      const int a = I(1);
      R[I(0)] = a < 0 ? std::make_shared<Map>() : std::make_shared<Map>(H{}, std::equal_to<K>{}, a);
    } else if (q == "fl") {
      std::vector<K> ks;
      std::vector<OmcInt> vs;
      for (std::size_t i = 1; i + 1 < t.size(); i += 2) {
        ks.push_back(key(t[i]));
        vs.push_back(std::stoll(t[i + 1]));
      }
      R[I(0)] = std::make_shared<Map>(Map::from_lists(ks, vs));
    } else if (q == "a") {
      if (R[I(0)]->bucket_count() == 0) {   // OMC 在这里 fail（loadFactor 除零），C++ 容器是断言，所以驱动先拦下，日志与 Hsh.mo 的 FAILED 行对齐
        std::cout << "FAILED " << o << " " << t[0] << " " << t[1] << " " << t[2] << "\n";
      } else {
        R[I(0)]->add(key(t[1]), std::stoll(t[2]));
      }
    } else if (q == "an") {
      R[I(0)]->add_new(key(t[1]), std::stoll(t[2]));
    } else if (q == "au") {
      std::cout << "au " << (R[I(0)]->add_unique(key(t[1]), std::stoll(t[2])) ? "ok" : "fail") << "\n";
    } else if (q == "up") {
      const OmcInt d = std::stoll(t[2]);
      std::cout << "up " << R[I(0)]->add_update(key(t[1]), [&](OmcInt* old) { return old != nullptr ? *old + d : d; }) << "\n";
    } else if (q == "r") {
      std::cout << "r " << (R[I(0)]->remove(key(t[1])) ? "true" : "false") << "\n";
    } else if (q == "g") {
      const OmcInt* v = R[I(0)]->get(key(t[1]));
      std::cout << "g " << (v != nullptr ? *v : OmcInt{-999}) << "\n";
    } else if (q == "c") {
      std::cout << "c " << (R[I(0)]->contains(key(t[1])) ? "true" : "false") << "\n";
    } else if (q == "m") {
      R[I(0)] = std::make_shared<Map>(Map::merge(*R[I(1)], *R[I(2)]));
    } else if (q == "sub") {
      std::vector<K> ks;
      for (std::size_t i = 2; i < t.size(); ++i) ks.push_back(key(t[i]));
      R[I(0)] = std::make_shared<Map>(R[I(1)]->sub_map(ks));
    } else if (q == "cp") {
      R[I(0)] = std::make_shared<Map>(R[I(1)]->copy());
    } else if (q == "dump") {
      dump(I(0));
    } else {
      return false;
    }
    return true;
  }
};

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cerr << "usage: omc_hash_drv <ops.txt>\n";
    return 2;
  }
  std::ifstream f(argv[1], std::ios::binary);
  if (!f) {
    std::cerr << "cannot open " << argv[1] << "\n";
    return 2;
  }
  std::stringstream ss;
  ss << f.rdbuf();
  SetOps<std::string, TextHash> S('S', 's');
  SetOps<std::string, CrefTextHash> C('C', 'c');
  SetOps<OmcInt, IdHash> I('I', 'i');
  MapOps<std::string, TextHash> M('M', 'm');
  MapOps<OmcInt, IdHash> N('N', 'n');
  for (const std::string& line : split(ss.str(), '\n')) {
    std::vector<std::string> t = split(line, ' ');
    if (t.empty()) continue;
    const std::string op = t[0];
    t.erase(t.begin());
    if (op == "#") {
      std::cout << line << "\n";
    } else if (op == "hcr") {
      std::cout << "hcr " << cref_text_hash(t[0]) << "\n";
    } else if (op == "hd") {
      std::cout << "hd " << string_hash_djb2(t[0]) << "\n";
    } else if (op == "hc") {
      std::cout << "hc " << string_hash_djb2_continue(t[0], std::stoll(t[1]) * 4294967296LL + std::stoll(t[2])) << "\n";
    } else if (op == "hm") {
      std::cout << "hm " << int_mod(std::stoll(t[0]) * 65536 + std::stoll(t[1]), std::stoll(t[2])) << "\n";
    } else if (op == "hp") {
      std::cout << "hp " << next_prime(std::stoll(t[0])) << "\n";
    } else if (S.op(op, t) || C.op(op, t) || I.op(op, t) || M.op(op, t) || N.op(op, t)) {
    } else {
      std::cout << "UNKNOWN " << op << "\n";
    }
  }
  return 0;
}
