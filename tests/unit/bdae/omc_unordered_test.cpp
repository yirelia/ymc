// OMC 哈希容器复刻（S5b-0）。期望值全部来自 omc 1.25.0 实跑，不是 ymc 的输出。来源分两种：
//
// [容器] v1.25.0 的 UnorderedSet.mo / UnorderedMap.mo / Vector.mo / Mutable.mo 原文（只给 Vector.new 加了 impure，挡住编译期常量求值），
//   连同 List / Util 里用到的函数原文，由安装版 omc 编译后跑在它自己的运行时上，所以哈希是真的 stringHashDjb2，桶数也能直接读。工具在 tools/omc_hash/：
//     cmake --build <构建目录> -j 3 --target omc_hash_drv
//     python -I tools/omc_hash/diff.py --work <工作目录> --drv <构建目录>/tests/omc_hash_drv --ut --hash
//   --ut 跑的就是下面 [容器] 用例的场景（gen_ops.py ut），日志在 <工作目录>/omc.ut.log；--hash 的日志在 omc.hash.log。
//   操作清单见 gen_ops.py 文件头，真容器怎么编译见 mkhsh.py 文件头。
//
// [后端] tests/probes/bdae/s5b0.probes 的模型：
//     python -I tools/omc_hash/chk.py --drv <构建目录>/tests/omc_hash_drv --run-dir <目录> --probes tests/probes/bdae/s5b0.probes
//   它对每个模型跑 `omc --newBackend -s --simCodeTarget=None -d=optdaedump,bltmatrixdump,bltdump model.mo`（输出存在 <目录>/<模型>/out.txt），
//   取 `[ODE] Differential-Algebraic: (Partitioning)` 段（Cluster.toPartition，NBPartitioning.mo:551-552）的未知量 / 方程序、
//   `FullAdjacencyMatrix` 的行（UnorderedSet.toList(occurences[i])，NBAdjacency.mo:878）、`(After Lowering)` 的 $RES_SIM_k（when 拆分）。
#include "ymc_test.h"

#include <string>
#include <utility>
#include <vector>

#include "bdae/omc_unordered.h"

using namespace ymc;
using namespace ymc::bdae;

namespace {

using SSet = UnorderedSet<std::string, StringHash>;
using SMap = UnorderedMap<std::string, OmcInt, StringHash>;

std::string join(const std::vector<std::string>& v) {
  std::string out;
  for (std::size_t i = 0; i < v.size(); ++i) {
    if (i != 0) out += ',';
    out += v[i];
  }
  return out;
}
std::string join(const std::vector<OmcInt>& v) {
  std::string out;
  for (std::size_t i = 0; i < v.size(); ++i) {
    if (i != 0) out += ',';
    out += std::to_string(v[i]);
  }
  return out;
}
std::vector<std::string> words(const char* text) {
  std::vector<std::string> out;
  std::string cur;
  for (const char* p = text; *p != '\0'; ++p) {
    if (*p == ' ') {
      out.push_back(cur);
      cur.clear();
    } else {
      cur.push_back(*p);
    }
  }
  if (!cur.empty()) out.push_back(cur);
  return out;
}

// cref 键：文本 → ExprCref → cref_hash。下标文本放进表里，Id<Expr> 的 raw 当下标用，这样不用真的造表达式。
ExprCref parse_cref(const std::string& s, Interner& in, std::vector<std::string>& subs) {
  ExprCref c;
  std::size_t i = 0;
  while (i < s.size()) {
    const std::size_t begin = i;
    while (i < s.size() && s[i] != '.' && s[i] != '[') ++i;
    ExprCrefPart part;
    part.name = in.intern(std::string_view(s).substr(begin, i - begin));
    if (i < s.size() && s[i] == '[') {
      ++i;
      while (s[i] != ']') {
        const std::size_t sb = i;
        while (s[i] != ',' && s[i] != ']') ++i;
        subs.push_back(s.substr(sb, i - sb));
        part.subscripts.push_back(Id<Expr>::from_index(static_cast<std::uint32_t>(subs.size() - 1)));
        if (s[i] == ',') ++i;
      }
      ++i;
    }
    if (i < s.size() && s[i] == '.') ++i;
    c.parts.push_back(std::move(part));
  }
  return c;
}
struct TextCrefHash {
  [[nodiscard]] OmcInt operator()(const std::string& s) const {
    Interner in;
    std::vector<std::string> subs;
    const ExprCref c = parse_cref(s, in, subs);
    return cref_hash(c, in, [&](Id<Expr> id) { return std::string_view(subs[id.raw]); });
  }
};
using CSet = UnorderedSet<std::string, TextCrefHash>;

std::string partition_order(const char* insertion) {
  CSet set;
  for (const std::string& k : words(insertion)) set.add(k);
  return join(set.to_list());
}

}  // namespace

// ---- 运行时函数：stringHashDjb2 / Continue / intMod / nextPrime ----

// [容器] 脚本行 `hd x` 等：print(intString(stringHashDjb2(s)))。Windows 的 long 是 32 位，超出的部分回绕后再 labs。
YMC_TEST(raw_hash_values) {
  YMC_CHECK_EQ(string_hash_djb2("x"), 177693);
  YMC_CHECK_EQ(string_hash_djb2("a"), 177670);
  YMC_CHECK_EQ(string_hash_djb2("_"), 177668);
  YMC_CHECK_EQ(string_hash_djb2("uu"), 5863887);
  YMC_CHECK_EQ(string_hash_djb2("12"), 5861576);
  YMC_CHECK_EQ(string_hash_djb2("abc"), 193485963);
  YMC_CHECK_EQ(string_hash_djb2("time"), 2090760340);
  YMC_CHECK_EQ(string_hash_djb2("$DER"), 2087843748);
  YMC_CHECK_EQ(string_hash_djb2("$RES_SIM_0"), 1652035926);
  YMC_CHECK_EQ(string_hash_djb2("$FUN_12"), 2140682348);
  YMC_CHECK_EQ(string_hash_djb2("abcdefghij"), 1215284260);
  YMC_CHECK_EQ(string_hash_djb2("ZZZZZZZZZZZZZZZZZZ"), 1878158375);
  // 字节按无符号算（UTF-8 的 é、中文、高位字节）
  YMC_CHECK_EQ(string_hash_djb2("\xc3\xa9"), 5866513);
  YMC_CHECK_EQ(string_hash_djb2("a\xc3\xa9"), 193489234);
  YMC_CHECK_EQ(string_hash_djb2("\xe4\xb8\xad\xe6\x96\x87"), 689673393);
  YMC_CHECK_EQ(string_hash_djb2("\xff\xfe\xfd"), 193663327);
}

// [容器] 这几个串的 djb2 恰好是 0x80000000，C 里是 labs(LONG_MIN)：实测 +2147483648。
YMC_TEST(labs_of_long_min_is_positive) {
  YMC_CHECK_EQ(string_hash_djb2("VBUetgn"), OmcInt{2147483648});
  YMC_CHECK_EQ(string_hash_djb2("VBUethM"), OmcInt{2147483648});
  YMC_CHECK_EQ(string_hash_djb2_continue("ethM", 193472914), OmcInt{2147483648});
}

// [容器] Continue 的入参先截成 32 位无符号（(unsigned long)hash）：脚本行 `hc <串> <hi> <lo>`，入参 = hi * 2^32 + lo。
YMC_TEST(continue_truncates_input_to_32_bits) {
  YMC_CHECK_EQ(string_hash_djb2_continue("B", 0), 66);
  YMC_CHECK_EQ(string_hash_djb2_continue("tYgjmU", (OmcInt{1} << 32) + 1), 1645573857);
  YMC_CHECK_EQ(string_hash_djb2_continue("hYgCfrL$spNxnyVm", OmcInt{1} << 32), 1556058246);
  YMC_CHECK_EQ(string_hash_djb2_continue("djAWtGSU6po8", (OmcInt{1} << 32) - 1), 737745931);
  YMC_CHECK_EQ(string_hash_djb2_continue("x", -2147483648LL), OmcInt{2147483528});
  YMC_CHECK_EQ(string_hash_djb2_continue("6HyS3", -2147483648LL), 2080721891);
}

// [容器] 脚本行 `hm`。结果与第二个数同号。
YMC_TEST(int_mod_sign_follows_divisor) {
  YMC_CHECK_EQ(int_mod(-1, 13), 12);
  YMC_CHECK_EQ(int_mod(-14, 13), 12);
  YMC_CHECK_EQ(int_mod(14, 13), 1);
  YMC_CHECK_EQ(int_mod(-13, 13), 0);
  YMC_CHECK_EQ(int_mod(-2147483648LL, 13), 2);
  YMC_CHECK_EQ(int_mod(5, -3), -1);
  YMC_CHECK_EQ(int_mod(-5, -3), -2);
  YMC_CHECK_EQ(int_mod(-5, 3), 1);
}

// [容器] 脚本行 `hp`：print(intString(Util.nextPrime(n)))。
YMC_TEST(next_prime_table) {
  YMC_CHECK_EQ(next_prime(-2), 2);
  YMC_CHECK_EQ(next_prime(0), 2);
  YMC_CHECK_EQ(next_prime(2), 2);
  YMC_CHECK_EQ(next_prime(3), 3);
  YMC_CHECK_EQ(next_prime(4), 5);
  YMC_CHECK_EQ(next_prime(9), 11);
  YMC_CHECK_EQ(next_prime(24), 29);
  YMC_CHECK_EQ(next_prime(28), 29);
  YMC_CHECK_EQ(next_prime(60), 61);
  YMC_CHECK_EQ(next_prime(124), 127);
  YMC_CHECK_EQ(next_prime(256), 257);
  YMC_CHECK_EQ(next_prime(1000), 1009);
  YMC_CHECK_EQ(next_prime(100000), 100003);
}

// ---- UnorderedSet ----

// [容器] new; add k0, k1, ...; 每步看桶数和序。第 15 个元素才 rehash：加之前 size(14) / 13 > 1。
YMC_TEST(set_rehash_thresholds) {
  SSet s;
  const std::vector<std::pair<int, OmcInt>> buckets = {{13, 13}, {14, 13}, {15, 29}, {29, 29}, {30, 29}, {31, 61}, {61, 61}, {62, 61},
                                                       {63, 127}, {127, 127}, {128, 127}, {129, 257}, {257, 257}, {258, 257}, {259, 521}};
  std::size_t next = 0;
  for (int i = 0; i < 259; ++i) {
    s.add("k" + std::to_string(i));
    if (next < buckets.size() && buckets[next].first == i + 1) {
      YMC_CHECK_EQ(s.bucket_count(), buckets[next].second);
      ++next;
    }
    if (i + 1 == 14) YMC_CHECK_EQ(join(s.to_list()), "k5,k4,k3,k2,k1,k0,k9,k13,k8,k12,k7,k11,k6,k10");
    if (i + 1 == 15) {
      YMC_CHECK_EQ(join(s.to_list()), "k14,k13,k12,k11,k10,k9,k8,k7,k6,k5,k4,k3,k2,k1,k0");
      YMC_CHECK_EQ(join(s.to_array()), "k0,k1,k2,k3,k4,k5,k6,k7,k8,k9,k10,k11,k12,k13,k14");
    }
    if (i + 1 == 30) {
      YMC_CHECK_EQ(join(s.to_list()),
                   "k16,k22,k15,k21,k14,k20,k13,k12,k11,k10,k9,k8,k7,k6,k5,k4,k3,k2,k1,k0,k29,k28,k27,k26,k19,k25,k18,k24,k17,k23");
    }
    if (i + 1 == 31) {
      YMC_CHECK_EQ(join(s.to_list()),
                   "k29,k9,k28,k8,k27,k7,k26,k6,k25,k5,k24,k4,k23,k3,k2,k22,k1,k21,k0,k20,k19,k18,k17,k16,k15,k30,k14,k13,k12,k11,k10");
    }
  }
  YMC_CHECK_EQ(next, buckets.size());
}

// [容器] toList 与 toArray 不是一个序：一个是另一个倒过来。
YMC_TEST(set_to_list_is_reverse_of_to_array) {
  SSet s;
  for (const std::string& k : words("alpha beta gamma delta x y z")) s.add(k);
  YMC_CHECK_EQ(s.bucket_count(), 13);
  YMC_CHECK_EQ(join(s.to_list()), "beta,gamma,z,y,x,alpha,delta");
  YMC_CHECK_EQ(join(s.to_array()), "delta,alpha,x,y,z,gamma,beta");
}

// [容器] fromList 的桶数按列表长度（含重复）算。
YMC_TEST(set_from_list_buckets) {
  SSet s0 = SSet::from_list({});
  YMC_CHECK_EQ(s0.bucket_count(), 2);
  SSet s1 = SSet::from_list(words("a"));
  YMC_CHECK_EQ(s1.bucket_count(), 2);
  SSet s2 = SSet::from_list(words("a b"));
  YMC_CHECK_EQ(join(s2.to_list()), "b,a");
  YMC_CHECK_EQ(s2.bucket_count(), 2);
  SSet s3 = SSet::from_list(words("a b c"));
  YMC_CHECK_EQ(join(s3.to_list()), "b,a,c");
  YMC_CHECK_EQ(s3.bucket_count(), 3);
  SSet s4 = SSet::from_list(words("a a a a a"));
  YMC_CHECK_EQ(s4.size(), 1);
  YMC_CHECK_EQ(s4.bucket_count(), 5);
}

// [容器] 大小相等时并进第二个；返回的是被改的那个对象，另一个不动。
YMC_TEST(set_union_tie_goes_to_second) {
  SSet p = SSet::from_list(words("p"));
  SSet q = SSet::from_list(words("q"));
  SSet& r = SSet::union_sets(p, q);
  YMC_CHECK(&r == &q);
  YMC_CHECK_EQ(join(q.to_list()), "p,q");
  YMC_CHECK_EQ(join(p.to_list()), "p");

  SSet a = SSet::from_list(words("p r"));
  SSet b = SSet::from_list(words("q"));
  SSet& r2 = SSet::union_sets(a, b);
  YMC_CHECK(&r2 == &a);
  YMC_CHECK_EQ(join(a.to_list()), "p,r,q");
  YMC_CHECK_EQ(a.bucket_count(), 2);
  YMC_CHECK_EQ(join(b.to_list()), "q");

  // 两个空集相等，结果是第二个：谁在右边决定桶数（字面量的空集 13 桶，fromList({}) 的 2 桶）。
  SSet e13;
  SSet e2 = SSet::from_list({});
  YMC_CHECK_EQ(SSet::union_sets(e13, e2).bucket_count(), 2);
  YMC_CHECK_EQ(SSet::union_sets(e2, e13).bucket_count(), 13);
}

// [容器] 九个单元素集合（Full 邻接里的实际形态）：sets 里 a 在表头。一路并进后面的，2 桶涨到 17 桶。
YMC_TEST(set_union_list_singletons) {
  std::vector<SSet> sets;
  for (const std::string& k : words("a b c d e f g h i")) sets.push_back(SSet::from_list({k}));
  SSet r = SSet::union_list(std::move(sets));
  YMC_CHECK_EQ(r.bucket_count(), 17);
  YMC_CHECK_EQ(join(r.to_list()), "i,h,g,f,e,d,c,b,a");
}

// [容器] extractFromLst 的 size 落后一拍：大小 1, 5, 3, 4。5 > 1 换人时 size 记的是 1（被换下去的），3 > 1 又换人，size 记 5，4 > 5 不换。
// 所以起点是大小 3 的集合而不是最大的那个；接着并进大小 4 的 {j,k,l,m}（5 桶，第 7 个元素时 rehash 到 13），再把 5 个和 1 个并进来。
YMC_TEST(set_union_list_lagging_size) {
  std::vector<SSet> sets;
  sets.push_back(SSet::from_list(words("a")));
  sets.push_back(SSet::from_list(words("b c d e f")));
  sets.push_back(SSet::from_list(words("g h i")));
  sets.push_back(SSet::from_list(words("j k l m")));
  SSet r = SSet::union_list(std::move(sets));
  YMC_CHECK_EQ(r.bucket_count(), 13);
  YMC_CHECK_EQ(join(r.to_list()), "a,m,l,k,j,i,h,g,f,e,d,c,b");
}

YMC_TEST(set_union_list_empty_is_default) {
  SSet r = SSet::union_list({});
  YMC_CHECK_EQ(r.bucket_count(), 13);
  YMC_CHECK(r.empty());
}

// [容器] difference / sym_difference：头插收集后 fromList，桶数按结果个数。
YMC_TEST(set_difference) {
  SSet a = SSet::from_list(words("a b c d e f"));
  SSet b = SSet::from_list(words("b d x"));
  SSet d = SSet::difference(a, b);
  YMC_CHECK_EQ(join(d.to_list()), "e,c,a,f");
  YMC_CHECK_EQ(d.bucket_count(), 5);
  SSet s = SSet::sym_difference(a, b);
  YMC_CHECK_EQ(join(s.to_list()), "e,x,c,a,f");
  YMC_CHECK_EQ(s.bucket_count(), 5);
}

// [容器] 删掉再加回来：回到同桶表头，同桶的序变了。这里 b、o 同桶。
YMC_TEST(set_remove_then_add) {
  SSet s;
  for (const std::string& k : words("b o c p")) s.add(k);
  YMC_CHECK_EQ(join(s.to_list()), "c,p,b,o");
  YMC_CHECK(s.remove("b"));
  s.add("b");
  YMC_CHECK_EQ(join(s.to_list()), "c,p,o,b");
  YMC_CHECK_EQ(join(s.to_array()), "b,o,p,c");
  YMC_CHECK(!s.remove("zz"));
  YMC_CHECK_EQ(s.size(), 4);
}

// [容器] addNew 不查重；remove 只摘一份，所以 contains 还是真（NBStrongComponent.mo:982-984 删了再 addNew 的那条路）。
YMC_TEST(set_add_new_keeps_duplicates) {
  SSet s;
  s.add("a");
  s.add_new("a");
  YMC_CHECK_EQ(join(s.to_list()), "a,a");
  YMC_CHECK_EQ(s.size(), 2);
  YMC_CHECK(s.remove("a"));
  YMC_CHECK_EQ(s.size(), 1);
  YMC_CHECK(s.contains("a"));
  YMC_CHECK(!s.add_unique("a"));
  YMC_CHECK(s.add_unique("b"));
}

// [容器] apply：新桶数 nextPrime(size)，不乘 2；重复的新键丢弃。
YMC_TEST(set_apply_buckets) {
  SSet s;
  for (const std::string& k : words("aa ab ac bd be")) s.add(k);
  s.apply([](const std::string& k) { return k + "z"; });
  YMC_CHECK_EQ(s.bucket_count(), 5);
  YMC_CHECK_EQ(join(s.to_list()), "acz,bez,aaz,abz,bdz");
  SSet t;
  for (const std::string& k : words("aax aay abx bdy")) t.add(k);
  t.apply([](const std::string& k) { return k.size() > 2 ? k.substr(0, 2) : k; });
  YMC_CHECK_EQ(t.size(), 3);
  YMC_CHECK_EQ(t.bucket_count(), 5);
  YMC_CHECK_EQ(join(t.to_list()), "bd,ab,aa");
}

// [容器] 谓词：长度为偶数。两边都是默认 13 桶。
YMC_TEST(set_filter_and_split) {
  const auto even = [](const std::string& k) { return k.size() % 2 == 0; };
  SSet s = SSet::from_list(words("a bb ccc dddd eeeee ffffff"));
  SSet f = s.filter_on_false(even);
  YMC_CHECK_EQ(f.bucket_count(), 13);
  YMC_CHECK_EQ(join(f.to_list()), "a,eeeee,ccc");
  auto [yes, no] = s.split_on_true(even);
  YMC_CHECK_EQ(join(yes.to_list()), "dddd,bb,ffffff");
  YMC_CHECK_EQ(join(no.to_list()), "a,eeeee,ccc");
}

YMC_TEST(set_unique_list) {
  YMC_CHECK_EQ(join(SSet::unique_list(words("z"))), "z");
  YMC_CHECK_EQ(join(SSet::unique_list(words("b a b c a"))), "c,b,a");
  using ISet = UnorderedSet<OmcInt, IdHash>;
  YMC_CHECK_EQ(join(ISet::unique_list({5, 3, 5, 9, 1})), "9,3,1,5");
  YMC_CHECK_EQ(join(ISet::unique_list({9})), "9");
}

// [容器] 整数键，负数按 intMod 落桶：-1 → 4，-6 → 4，4 → 4，9 → 4（5 桶），全同桶，序就是插入序。
YMC_TEST(set_negative_integer_keys) {
  UnorderedSet<OmcInt, IdHash> s(IdHash{}, {}, 5);
  for (OmcInt k : {OmcInt{-1}, OmcInt{-6}, OmcInt{4}, OmcInt{9}}) s.add(k);
  YMC_CHECK_EQ(join(s.to_list()), "-1,-6,4,9");
  YMC_CHECK_EQ(join(s.to_array()), "9,4,-6,-1");
}

// ---- UnorderedMap ----

// [容器] 默认 1 桶，push 之后判负载：2 个键就 rehash 到 5。keys 不受影响。
YMC_TEST(map_rehash_thresholds) {
  SMap m;
  const std::vector<std::pair<int, OmcInt>> buckets = {{1, 1}, {2, 5}, {3, 5}, {5, 5}, {6, 13}, {7, 13}, {13, 13}, {14, 29}, {15, 29}, {29, 29}, {30, 61}};
  std::size_t next = 0;
  for (int i = 0; i < 30; ++i) {
    m.add("k" + std::to_string(i), i + 1);
    if (next < buckets.size() && buckets[next].first == i + 1) {
      YMC_CHECK_EQ(m.bucket_count(), buckets[next].second);
      ++next;
    }
  }
  YMC_CHECK_EQ(next, buckets.size());
  for (std::size_t i = 0; i < m.keys().size(); ++i) {
    YMC_CHECK_EQ(m.keys()[i], "k" + std::to_string(i));
    YMC_CHECK_EQ(m.values()[i], static_cast<OmcInt>(i) + 1);
  }
}

// [容器] add 已有的键只改值；删掉再加排到最后。
YMC_TEST(map_update_remove_readd) {
  SMap m;
  m.add("c", 1);
  m.add("a", 2);
  m.add("b", 3);
  m.add("a", 9);
  YMC_CHECK_EQ(join(m.keys()), "c,a,b");
  YMC_CHECK_EQ(join(m.values()), "1,9,3");
  YMC_CHECK(m.remove("c"));
  YMC_CHECK_EQ(join(m.keys()), "a,b");
  m.add("c", 4);
  YMC_CHECK_EQ(join(m.keys()), "a,b,c");
  YMC_CHECK_EQ(join(m.values()), "9,3,4");
  YMC_CHECK_EQ(*m.get("b"), 3);
  YMC_CHECK(m.get("zz") == nullptr);
}

// [容器] 相等时拷第二个，把第一个接在后面；不等时拷大的。
YMC_TEST(map_merge) {
  SMap a;
  a.add("a", 1);
  a.add("b", 2);
  SMap b;
  b.add("c", 3);
  b.add("d", 4);
  SMap m = SMap::merge(a, b);
  YMC_CHECK_EQ(join(m.keys()), "c,d,a,b");
  YMC_CHECK_EQ(join(m.values()), "3,4,1,2");
  YMC_CHECK_EQ(m.bucket_count(), 5);
  SMap c;
  c.add("c", 3);
  SMap n = SMap::merge(a, c);
  YMC_CHECK_EQ(join(n.keys()), "a,b,c");
}

YMC_TEST(map_sub_map_and_from_lists) {
  SMap a;
  a.add("a", 1);
  a.add("b", 2);
  SMap s = a.sub_map(words("b a"));
  YMC_CHECK_EQ(join(s.keys()), "b,a");
  YMC_CHECK_EQ(join(s.values()), "2,1");
  YMC_CHECK_EQ(s.bucket_count(), 2);
  SMap f = SMap::from_lists(words("x y x"), {1, 2, 3});
  YMC_CHECK_EQ(join(f.keys()), "x,y");
  YMC_CHECK_EQ(join(f.values()), "3,2");
  YMC_CHECK_EQ(f.bucket_count(), 3);
}

// [容器] addNew 留两份；查到新的那份；remove 摘新的那份。
YMC_TEST(map_add_new_duplicates) {
  SMap m;
  m.add("a", 1);
  m.add_new("a", 2);
  YMC_CHECK_EQ(join(m.keys()), "a,a");
  YMC_CHECK_EQ(*m.get("a"), 2);
  YMC_CHECK(m.remove("a"));
  YMC_CHECK_EQ(join(m.values()), "1");
  YMC_CHECK_EQ(*m.get("a"), 1);
  YMC_CHECK(!m.add_unique("a", 5));
}

YMC_TEST(map_add_update) {
  SMap m;
  const auto bump = [](OmcInt d) { return [d](OmcInt* old) { return old != nullptr ? *old + d : d; }; };
  YMC_CHECK_EQ(m.add_update("a", bump(5)), 5);
  YMC_CHECK_EQ(m.add_update("b", bump(1)), 1);
  YMC_CHECK_EQ(m.add_update("a", bump(7)), 12);
  YMC_CHECK_EQ(join(m.keys()), "a,b");
  YMC_CHECK_EQ(join(m.values()), "12,1");
}

// [容器] 0 桶的 Map 能查（NBAlias.mo:431 / :451 的桶数是变量数），加键时 OMC fail，这里是断言，不测。
YMC_TEST(map_zero_buckets) {
  SMap m(StringHash{}, {}, 0);
  YMC_CHECK(!m.contains("a"));
  YMC_CHECK(m.get("a") == nullptr);
  YMC_CHECK_EQ(m.bucket_count(), 0);
}

// ---- ComponentRef.hash：对 NB 的实跑输出 ----

// [后端] HshN15：15 个名字，跨 13→29 的 rehash。插入序 = (Events) 段的 Algebraic Variables。
YMC_TEST(cref_partition_order_across_rehash) {
  YMC_CHECK_EQ(partition_order("cz_0 oF_N_1 s_2 jc6_3 g47m4_4 z8Bl_m_5 oWPcfoENj5_6 gQN_7 zr_8 rKKA_9 hkomyVBJ_10 oJ4Bv6_11 j45Ttev_bST3b_12 qo_f_13 yC6G_14"),
               "gQN_7,hkomyVBJ_10,j45Ttev_bST3b_12,zr_8,jc6_3,g47m4_4,z8Bl_m_5,s_2,rKKA_9,oF_N_1,oJ4Bv6_11,oWPcfoENj5_6,cz_0,qo_f_13,yC6G_14");
}

// [后端] HshD2：a.b 与 ab、b.a 与 ba 拼起来同字节。hash 末段先，所以 a.b 的输入是 b、a，和 ba 的区别只在段间那次 labs；这组名字都没溢出，
// a.b 与 ba 同哈希（同桶），靠插入序分先后。
YMC_TEST(cref_dotted_names_hash_last_segment_first) {
  YMC_CHECK_EQ(partition_order("a.b a.a b.b b.a ab ba aa bb"), "a.a,aa,b.b,bb,a.b,ba,b.a,ab");
}

// [后端] HshD3：$DER 前缀在 hash 里排最后（ComponentRef.append，NBVariable.mo:943）。插入序 = Algebraic + Derivative。
YMC_TEST(cref_der_prefix_hashes_last) {
  YMC_CHECK_EQ(partition_order("alg1 alg2 $DER.s1.x $DER.s1.y $DER.s2.x $DER.s2.y $DER.x"), "$DER.x,$DER.s1.x,$DER.s1.y,$DER.s2.x,$DER.s2.y,alg2,alg1");
}

// [后端] HshD2 的方程名：$RES_SIM_k 是单段 cref。
YMC_TEST(cref_equation_names) {
  YMC_CHECK_EQ(partition_order("$RES_SIM_7 $RES_SIM_6 $RES_SIM_5 $RES_SIM_4 $RES_SIM_3 $RES_SIM_2 $RES_SIM_1 $RES_SIM_0"),
               "$RES_SIM_0,$RES_SIM_1,$RES_SIM_2,$RES_SIM_3,$RES_SIM_4,$RES_SIM_5,$RES_SIM_6,$RES_SIM_7");
}

// [后端] HshS3，FullAdjacencyMatrix 的行：下标逐个 Continue。
//   $RES_SIM_13：w[1, 1] = x1[2] * x1[1] + time  →  {x1[1], x1[2], w[1, 1]}
//   $RES_SIM_0： x[12] = w[1, 2] * x1[2] * x[1] * x[11] * x12 + time  →  {x[11], x[1], w[1, 2], x1[2], x12, x[12]}
// 集合的造法照 collectDependencies（NBAdjacency.mo:1674、:1723-1742）：每个 cref 一个 fromList，MULTARY 头插后 union_list，time 不是未知量给空的 fromList。
YMC_TEST(cref_subscripts_full_adjacency_rows) {
  const auto product = [](const char* args) {
    std::vector<CSet> sets;
    for (const std::string& a : words(args)) sets.insert(sets.begin(), CSet::from_list({a}));
    return CSet::union_list(std::move(sets));
  };
  const auto row = [&](const char* lhs, const char* args) {
    CSet occ1 = CSet::from_list({std::string(lhs)});
    std::vector<CSet> terms;
    terms.push_back(CSet::from_list({}));   // time 是第二项，头插后在表头
    terms.push_back(product(args));
    CSet occ2 = CSet::union_list(std::move(terms));
    return join(CSet::union_sets(occ1, occ2).to_list());
  };
  YMC_CHECK_EQ(row("w[1,1]", "x1[2] x1[1]"), "x1[1],x1[2],w[1,1]");
  YMC_CHECK_EQ(row("x[12]", "w[1,2] x1[2] x[1] x[11] x12"), "x[11],x[1],w[1,2],x1[2],x12,x[12]");
}

// [后端] when 拆分（NBEquation.mo:3173-3174）。这里说的是 split 的返回序：bodies[k] 就是 $RES_SIM_k（这个 when 是模型最后一条方程）。
// dump 的打印序是它再反一次（typeList 头插，NBEquation.mo:4661），所以 HshW1 打出来恰好是源序，别拿打印序对这里。tools/omc_hash/chk.py 的 check_when 两种序都校验。
// HshW1：d5, d1, d4, d2, d3 依次赋值，语句表头插所以 keyList 进的是 d3, d2, d4, d1, d5：插入序，和桶序（d5, d4, d3, d2, d1）无关。
YMC_TEST(when_split_discretes_are_insertion_order) {
  SMap discr;
  for (const std::string& k : words("d3 d2 d4 d1 d5")) discr.add(k, 1);
  YMC_CHECK_EQ(join(discr.keys()), "d3,d2,d4,d1,d5");
  CSet bucket_order;
  for (const std::string& k : words("d3 d2 d4 d1 d5")) bucket_order.add(k);
  YMC_CHECK(join(bucket_order.to_list()) != join(discr.keys()));
}

// HshW2：reinit 的状态走 UnorderedSet.toList，是桶序，与语句序无关。
YMC_TEST(when_split_states_are_bucket_order) {
  CSet states;
  for (const std::string& k : words("st6 st3 st8 st1 st5 st4 st7 st2 st9")) states.add(k);
  YMC_CHECK_EQ(join(states.to_list()), "st9,st8,st7,st6,st5,st4,st3,st2,st1");
}

YMC_TEST_MAIN
