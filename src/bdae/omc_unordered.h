// OMC 哈希容器复刻（S5b-0，ADR-008 F6 / D4）——UnorderedSet、UnorderedMap、ComponentRef.hash 的逐位语义。
//
// 为什么要复刻：用户裁定块序与 OMC 逐位同序，而 NB 的邻接行、分区未知量 / 方程序直接来自哈希桶序。下面每条规则都是 OMC v1.25.0 的
// 原样行为（路径相对 OMCompiler/），包括看起来像笔误的地方（extract_from_lst 里的 size 落后一拍），不要"修"。
//
// 两种容器的序完全不同，别混：
//   Set：序 = 桶序。toList 是"逆桶序"（桶号大到小，同桶旧到新），toArray / fold / toString / union 的遍历是"正桶序"（桶号小到大，同桶新到旧）。
//   Map：keyList / valueList / toList / toArray 都是**插入序**，与哈希、桶数、rehash 无关（Compiler/Util/UnorderedMap.mo:429-439 →
//        Vector.toList）。Map 的桶只用来查找。
//
// 同桶的存储：OMC 的桶是 list，新元素头插（UnorderedSet.mo:822）。这里用 vector 尾插，**back 就是 OMC 的表头**，遍历 OMC 的桶 = 反向遍历 vector。
//
// 引用语义：OMC 的 Set / Map 里全是 Mutable，赋值共享同一个对象，union 就地改其中一个入参再返回它。这里禁掉隐式拷贝，省得改到副本上；
// 需要共享的地方（when 拆分的 discr_map，NBEquation.mo:3377-3397）由调用方用下标或指针表达。
#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string_view>
#include <utility>
#include <vector>

#include "base/assert.h"
#include "base/symbol.h"
#include "inst/expr.h"

namespace ymc::bdae {

// MetaModelica 的 Integer。Windows 64 位上是 long long（SimulationRuntime/c/openmodelica_types.h:54-70、:108），与 C long（32 位）不同宽，
// 下面的哈希就卡在这两个宽度之间。
using OmcInt = std::int64_t;

namespace omc_detail {

// labs((long)h)，long 是 32 位（Windows LLP64，安装版 omc 1.25.0 是这个构建）。结果落在 [0, 2^31]：h == 0x80000000 时 C 里是 labs(LONG_MIN) 溢出，
// 实测返回 +2147483648，不是 -2147483648（gcc 认定 labs 非负，转 long long 时零扩展）。所以 64 位算术取绝对值恰好对上。实测命令见单测 raw_hash_values。
[[nodiscard]] constexpr OmcInt labs32(std::uint32_t h) noexcept {
  const OmcInt s = h >= 0x80000000u ? static_cast<OmcInt>(h) - (OmcInt{1} << 32) : static_cast<OmcInt>(h);
  return s < 0 ? -s : s;
}

[[nodiscard]] constexpr std::uint32_t djb2_step(std::string_view s, std::uint32_t h) noexcept {
  for (const char c : s) h = h * 33u + static_cast<unsigned char>(c);   // ((hash << 5) + hash) + c，字节按无符号（djb2_hash 的 const unsigned char*）
  return h;
}

}  // namespace omc_detail

// stringHashDjb2（SimulationRuntime/c/meta/meta_modelica_builtin.c:156-163，djb2_hash :122-128）。
[[nodiscard]] constexpr OmcInt string_hash_djb2(std::string_view s) noexcept {
  return omc_detail::labs32(omc_detail::djb2_step(s, 5381u));
}

// stringHashDjb2Continue（meta_modelica_builtin.c:166-173）。接着上一步的结果算，上一步已经 labs 过，所以分段算 ≠ 拼接后算。
// 入参先截成 32 位（(unsigned long)hash）。
[[nodiscard]] constexpr OmcInt string_hash_djb2_continue(std::string_view s, OmcInt hash) noexcept {
  return omc_detail::labs32(omc_detail::djb2_step(s, static_cast<std::uint32_t>(hash)));
}

// intMod（FrontEnd/MetaModelicaBuiltin.mo:120-128 → modelica_integer_mod，SimulationRuntime/c/util/utility.h:140-144）：结果与 y 同号。
// 负哈希确实存在：Tearing.hash = -1（NBackEnd/Modules/3_Post/NBTearing.mo:82-86），StrongComponent.hash 的循环块就是它（NBStrongComponent.mo:292-306）。
[[nodiscard]] constexpr OmcInt int_mod(OmcInt x, OmcInt y) noexcept {
  const OmcInt r = x % y;
  return ((y > 0 && r < 0) || (y < 0 && r > 0)) ? r + y : r;
}

// Util.nextPrime（Compiler/Util/Util.mo:1242-1287）：不小于 n 的最小素数，n <= 2 时是 2。
[[nodiscard]] constexpr OmcInt next_prime(OmcInt n) noexcept {
  if (n <= 2) return 2;
  OmcInt p = n + int_mod(n + 1, 2);   // 偶数加一
  for (;; p += 2) {
    bool prime = true;
    OmcInt i = 3, q = p / 3;
    while (q >= i) {                  // i 走到 sqrt(p) 为止；只对正奇数成立
      if (p == q * i) { prime = false; break; }
      i += 2;
      q = p / i;
    }
    if (prime) return p;
  }
}

// ComponentRef.hash（Compiler/NFFrontEnd/NFComponentRef.mo:1448-1487）。
//   - **末段先**：NF 的 cref 从最后一段往前链（restCref 是前缀），所以 a.b.c 依次喂 c、b、a；ExprCref 的 parts 是首段在前，这里反着走。
//     `$DER.x` 就是 x 先、`$DER` 后。
//   - 段内：先名字，再逐个下标的 Subscript.toString 文本。段与段、下标与下标之间没有分隔符，每步都 labs。
//   - 空 cref（EMPTY）是 0；WILD 是 stringHashDjb2("_")，等价于名字为 `_` 的单段 cref。
// sub_text：Id<Expr> → Subscript.toString 的文本，由调用方给（这里不依赖表达式打印器）。
template <class SubText>
[[nodiscard]] OmcInt cref_hash(const ExprCref& c, const Interner& in, const SubText& sub_text) {
  OmcInt h = 0;
  bool first = true;
  for (auto it = c.parts.rbegin(); it != c.parts.rend(); ++it) {
    const std::string_view name = in.str(it->name);
    h = first ? string_hash_djb2(name) : string_hash_djb2_continue(name, h);
    first = false;
    for (const Id<Expr> sub : it->subscripts) h = string_hash_djb2_continue(sub_text(sub), h);
  }
  return h;
}

// ComponentRef.hashStrip（NFComponentRef.mo:1489-1509）：不看下标。VariablePointers.empty 在 scalarized = false 时用它（NBVariable.mo:1693-1697），
// --newBackend 关了 nfScalarize（NFInst.mo resetGlobalFlags），所以 NB 的名→下标表走这一支；那张表是 Map，序不受影响，只影响查找。
[[nodiscard]] inline OmcInt cref_hash_strip(const ExprCref& c, const Interner& in) {
  OmcInt h = 0;
  bool first = true;
  for (auto it = c.parts.rbegin(); it != c.parts.rend(); ++it) {
    const std::string_view name = in.str(it->name);
    h = first ? string_hash_djb2(name) : string_hash_djb2_continue(name, h);
    first = false;
  }
  return h;
}

// Util.id：整数键直接当哈希（NBPartitioning.mo:699 等）。
struct IdHash {
  template <class I>
  [[nodiscard]] constexpr OmcInt operator()(I key) const noexcept { return static_cast<OmcInt>(key); }
};

// stringHashDjb2 当哈希：文本键（Condition / TimeEvent / Call_Id / Mode / Expression.hash 都是先 toString 再走它）。
struct StringHash {
  [[nodiscard]] constexpr OmcInt operator()(std::string_view key) const noexcept { return string_hash_djb2(key); }
};

// UnorderedSet（Compiler/Util/UnorderedSet.mo）。Hash：const T& → OmcInt；可带状态（cref 键要带 Interner）。
template <class T, class Hash, class Eq = std::equal_to<T>>
class UnorderedSet {
 public:
  static constexpr OmcInt kDefaultBuckets = 13;   // new 的默认值（:73）

  explicit UnorderedSet(Hash hash = Hash{}, Eq eq = Eq{}, OmcInt bucket_count = kDefaultBuckets)
      : buckets_(static_cast<std::size_t>(bucket_count)), hash_(std::move(hash)), eq_(std::move(eq)) {}

  UnorderedSet(const UnorderedSet&) = delete;
  UnorderedSet& operator=(const UnorderedSet&) = delete;
  UnorderedSet(UnorderedSet&&) noexcept = default;
  UnorderedSet& operator=(UnorderedSet&&) noexcept = default;

  // fromList（:82-93）：桶数 = nextPrime(元素个数，含重复)，所以一元素的集合只有 2 个桶，邻接矩阵里到处是这种集合。
  [[nodiscard]] static UnorderedSet from_list(const std::vector<T>& elements, Hash hash = Hash{}, Eq eq = Eq{}) {
    UnorderedSet set(std::move(hash), std::move(eq), next_prime(static_cast<OmcInt>(elements.size())));
    for (const T& e : elements) set.add(e);
    return set;
  }

  // add（:108-122）：已有就不动。可能 rehash。
  void add(const T& key) {
    const Found f = find(key);
    if (!f.found) add_key(key, f.bucket);
  }
  // addNew（:124-137）：不查重，重复键会留两份。
  void add_new(const T& key) { add_key(key, bucket_of(key, bucket_count())); }
  // addUnique（:139-149）：OMC 里键已存在时 fail，这里返回 false，由调用方决定怎么报。
  [[nodiscard]] bool add_unique(const T& key) {
    const Found f = find(key);
    if (f.found) return false;
    add_key(key, f.bucket);
    return true;
  }
  // remove（:151-179）：只摘掉表头起第一个匹配，其余相对序不变（List.deleteMemberOnTrue，List.mo:4911-4943）。不 rehash、不缩桶。
  bool remove(const T& key) {
    std::vector<T>& b = buckets_[static_cast<std::size_t>(bucket_of(key, bucket_count()))];
    for (auto it = b.rbegin(); it != b.rend(); ++it) {
      if (eq_(*it, key)) {
        b.erase(std::next(it).base());
        --size_;
        return true;
      }
    }
    return false;
  }

  [[nodiscard]] bool contains(const T& key) const { return find(key).found; }
  [[nodiscard]] OmcInt size() const noexcept { return size_; }
  [[nodiscard]] bool empty() const noexcept { return size_ == 0; }
  [[nodiscard]] OmcInt bucket_count() const noexcept { return static_cast<OmcInt>(buckets_.size()); }

  // toList（:250-260）：边走边头插，结果是正桶序倒过来。[0] 是 OMC 列表的表头。
  [[nodiscard]] std::vector<T> to_list() const {
    std::vector<T> out = to_array();
    std::reverse(out.begin(), out.end());
    return out;
  }
  // toArray（:262-278）：正桶序。fold（:280-297）、toString（:561-573）、union / difference 的遍历都是这个序。
  [[nodiscard]] std::vector<T> to_array() const {
    std::vector<T> out;
    out.reserve(static_cast<std::size_t>(size_));
    for (const std::vector<T>& b : buckets_) {
      for (auto it = b.rbegin(); it != b.rend(); ++it) out.push_back(*it);
    }
    return out;
  }

  // apply（:333-382）：逐键换新值。新桶数是 nextPrime(size)，**不乘 2**（rehash 才乘），重复的新键丢弃。
  template <class Fn>
  void apply(const Fn& fn) {
    const OmcInt n = next_prime(size_);
    std::vector<std::vector<T>> fresh(static_cast<std::size_t>(n));
    OmcInt count = 0;
    for (const T& k : to_array()) {
      T new_key = fn(k);
      std::vector<T>& b = fresh[static_cast<std::size_t>(bucket_of(new_key, n))];
      const bool duplicate = std::any_of(b.begin(), b.end(), [&](const T& nk) { return eq_(nk, new_key); });
      if (!duplicate) {
        b.push_back(std::move(new_key));
        ++count;
      }
    }
    buckets_ = std::move(fresh);
    size_ = count;
  }

  // union（:598-618）：把小的并进大的，就地改、返回被改的那个。**相等时并进第二个**（条件是 size1 > size2）。
  // 选谁决定桶数，也就决定序，所以 C++ 里不能换成"总往左边并"。
  static UnorderedSet& union_sets(UnorderedSet& set1, UnorderedSet& set2) {
    UnorderedSet& into = set1.size_ > set2.size_ ? set1 : set2;
    const UnorderedSet& from = set1.size_ > set2.size_ ? set2 : set1;
    for (const T& k : from.to_array()) into.add(k);
    return into;
  }

  // union_list（:620-639 + extractFromLst :827-855）。入参的头元素 = sets[0]。空表返回一个新的默认集合。
  // 入参被消耗（OMC 里调用方之后也不再用它们）。
  [[nodiscard]] static UnorderedSet union_list(std::vector<UnorderedSet> sets, Hash hash = Hash{}, Eq eq = Eq{}) {
    if (sets.empty()) return UnorderedSet(std::move(hash), std::move(eq));
    // extractFromLst(lst, intGt)。size 故意落后一拍：换人时记的是**被换下去的那个**的大小（:848，先读 single 再换），所以选出来的未必是最大的。
    std::size_t single = 0;
    OmcInt size = sets[0].size_;
    std::vector<std::size_t> rest;   // OMC 头插，下面反向走
    for (std::size_t i = 1; i < sets.size(); ++i) {
      if (sets[i].size_ > size) {
        size = sets[single].size_;
        rest.push_back(single);
        single = i;
      } else {
        rest.push_back(i);
      }
    }
    UnorderedSet* cur = &sets[single];
    for (auto it = rest.rbegin(); it != rest.rend(); ++it) cur = &union_sets(*cur, sets[*it]);
    return std::move(*cur);
  }

  // difference（:706-722）：set1 \ set2。先头插收集再 fromList，所以按正桶序倒过来加进新集合，桶数 nextPrime(结果个数)。
  [[nodiscard]] static UnorderedSet difference(const UnorderedSet& set1, const UnorderedSet& set2) {
    std::vector<T> acc;
    set1.collect_not_in(set2, acc);
    std::reverse(acc.begin(), acc.end());
    return from_list(acc, set1.hash_, set1.eq_);
  }
  // sym_difference（:724-747）：同上，先走 set1 再走 set2。
  [[nodiscard]] static UnorderedSet sym_difference(const UnorderedSet& set1, const UnorderedSet& set2) {
    std::vector<T> acc;
    set1.collect_not_in(set2, acc);
    set2.collect_not_in(set1, acc);
    std::reverse(acc.begin(), acc.end());
    return from_list(acc, set1.hash_, set1.eq_);
  }

  // filterOnFalse（:471-489）：新集合用默认 13 桶，按正桶序加。
  template <class Pred>
  [[nodiscard]] UnorderedSet filter_on_false(const Pred& fn) const {
    UnorderedSet out(hash_, eq_);
    for (const T& k : to_array()) {
      if (!fn(k)) out.add(k);
    }
    return out;
  }
  // splitOnTrue（:491-508）：两个新集合都是默认 13 桶。返回 (trueSet, falseSet)。
  template <class Pred>
  [[nodiscard]] std::pair<UnorderedSet, UnorderedSet> split_on_true(const Pred& fn) const {
    std::pair<UnorderedSet, UnorderedSet> out{UnorderedSet(hash_, eq_), UnorderedSet(hash_, eq_)};
    for (const T& k : to_array()) (fn(k) ? out.first : out.second).add(k);
    return out;
  }

  // unique_list（:589-596）：不到两个元素时原样返回，否则 toList(fromList(...))，序变成桶序。
  [[nodiscard]] static std::vector<T> unique_list(const std::vector<T>& list, Hash hash = Hash{}, Eq eq = Eq{}) {
    if (list.size() < 2) return list;
    return from_list(list, std::move(hash), std::move(eq)).to_list();
  }

 private:
  struct Found { bool found; OmcInt bucket; };

  [[nodiscard]] OmcInt bucket_of(const T& key, OmcInt count) const {
    YMC_ASSERT(count > 0, "UnorderedSet: zero buckets");   // OMC 这里是 intMod(x, 0)；空的 0 桶集合只在 Alias 里出现，不会有键进来
    return int_mod(hash_(key), count);
  }
  // find（:774-796）：从表头找，所以 add_new 造出的重复键命中的是新的那个。
  [[nodiscard]] Found find(const T& key) const {
    const OmcInt h = bucket_of(key, bucket_count());
    const std::vector<T>& b = buckets_[static_cast<std::size_t>(h)];
    for (auto it = b.rbegin(); it != b.rend(); ++it) {
      if (eq_(*it, key)) return {true, h};
    }
    return {false, h};
  }
  // addKey（:798-825）：**加之前**判 size / 桶数 > 1，所以 13 桶的集合第 15 个元素才触发 rehash。
  void add_key(const T& key, OmcInt hash) {
    OmcInt h = hash;
    if (size_ > bucket_count()) {
      rehash();
      h = bucket_of(key, bucket_count());
    }
    buckets_[static_cast<std::size_t>(h)].push_back(key);
    ++size_;
  }
  // rehash（:535-559）：新桶数 nextPrime(size * 2)，size 不含正要加的键。旧桶按正桶序走，每个键头插进新桶。
  void rehash() {
    const OmcInt n = next_prime(size_ * 2);
    std::vector<std::vector<T>> fresh(static_cast<std::size_t>(n));
    for (std::vector<T>& b : buckets_) {
      for (auto it = b.rbegin(); it != b.rend(); ++it) {
        fresh[static_cast<std::size_t>(bucket_of(*it, n))].push_back(std::move(*it));
      }
    }
    buckets_ = std::move(fresh);
  }
  void collect_not_in(const UnorderedSet& other, std::vector<T>& acc) const {
    for (const T& k : to_array()) {
      if (!other.contains(k)) acc.push_back(k);
    }
  }

  std::vector<std::vector<T>> buckets_;   // 每桶 back = OMC 的表头
  OmcInt size_ = 0;
  Hash hash_;
  Eq eq_;
};

// UnorderedMap（Compiler/Util/UnorderedMap.mo）。keys_ / values_ 是插入序的并排数组，桶里存下标。
template <class K, class V, class Hash, class Eq = std::equal_to<K>>
class UnorderedMap {
 public:
  static constexpr OmcInt kDefaultBuckets = 1;    // new 的默认值（:86），与 Set 的 13 不同

  explicit UnorderedMap(Hash hash = Hash{}, Eq eq = Eq{}, OmcInt bucket_count = kDefaultBuckets)
      : buckets_(static_cast<std::size_t>(bucket_count)), hash_(std::move(hash)), eq_(std::move(eq)) {}

  UnorderedMap(const UnorderedMap&) = delete;
  UnorderedMap& operator=(const UnorderedMap&) = delete;
  UnorderedMap(UnorderedMap&&) noexcept = default;
  UnorderedMap& operator=(UnorderedMap&&) noexcept = default;

  // fromLists（:98-126）：桶数 nextPrime(键数)，重复键走 add（更新值，不占新位置）。
  [[nodiscard]] static UnorderedMap from_lists(const std::vector<K>& keys, std::vector<V> values, Hash hash = Hash{}, Eq eq = Eq{}) {
    YMC_ASSERT(keys.size() == values.size(), "UnorderedMap::from_lists: size mismatch");
    UnorderedMap map(std::move(hash), std::move(eq), next_prime(static_cast<OmcInt>(keys.size())));
    for (std::size_t i = 0; i < keys.size(); ++i) map.add(keys[i], std::move(values[i]));
    return map;
  }
  // copy（:128-140）：OMC 的 Map 是引用语义，要分叉必须显式拷。
  [[nodiscard]] UnorderedMap copy() const {
    UnorderedMap out(hash_, eq_, bucket_count());
    out.buckets_ = buckets_;
    out.keys_ = keys_;
    out.values_ = values_;
    return out;
  }

  // add（:161-177）：键已存在只改值，不动它在 keyList 里的位置。
  void add(const K& key, V value) {
    const Found f = find(key);
    if (f.index >= 0) {
      values_[static_cast<std::size_t>(f.index)] = std::move(value);
    } else {
      add_entry(key, std::move(value), f.bucket);
    }
  }
  // addNew（:179-192）：不查重。
  void add_new(const K& key, V value) { add_entry(key, std::move(value), bucket_of(key, bucket_count())); }
  // addUnique（:194-206）：OMC 里键已存在时 fail，这里返回 false。
  [[nodiscard]] bool add_unique(const K& key, V value) {
    const Found f = find(key);
    if (f.index >= 0) return false;
    add_entry(key, std::move(value), f.bucket);
    return true;
  }
  // addUpdate（:248-278）：fn(旧值指针或 nullptr) 给新值。
  template <class Fn>
  V& add_update(const K& key, const Fn& fn) {
    const Found f = find(key);
    if (f.index >= 0) {
      V& slot = values_[static_cast<std::size_t>(f.index)];
      slot = fn(&slot);
      return slot;
    }
    add_entry(key, fn(static_cast<V*>(nullptr)), f.bucket);
    return values_.back();
  }
  // remove（:280-322）：从并排数组里抽掉，后面的元素左移，桶里大于它的下标都减一。O(N)。不 rehash、不缩桶。
  // 注意 VariablePointers.remove 不走这里：它写 -1 墓碑（NBVariable.mo:1786-1803），键还留在 keyList 里。
  bool remove(const K& key) {
    const Found f = find(key);
    if (f.index < 0) return false;
    std::vector<std::uint32_t>& b = buckets_[static_cast<std::size_t>(f.bucket)];
    b.erase(std::find(b.begin(), b.end(), static_cast<std::uint32_t>(f.index)));
    keys_.erase(keys_.begin() + f.index);
    values_.erase(values_.begin() + f.index);
    for (std::vector<std::uint32_t>& bucket : buckets_) {
      for (std::uint32_t& i : bucket) {
        if (static_cast<OmcInt>(i) > f.index) --i;
      }
    }
    return true;
  }

  // get（:333-343）：不存在返回 nullptr。指针在下一次改动 Map 之前有效。
  [[nodiscard]] const V* get(const K& key) const {
    const Found f = find(key);
    return f.index >= 0 ? &values_[static_cast<std::size_t>(f.index)] : nullptr;
  }
  [[nodiscard]] V* get(const K& key) {
    const Found f = find(key);
    return f.index >= 0 ? &values_[static_cast<std::size_t>(f.index)] : nullptr;
  }
  [[nodiscard]] bool contains(const K& key) const { return find(key).index >= 0; }
  [[nodiscard]] OmcInt size() const noexcept { return static_cast<OmcInt>(keys_.size()); }
  [[nodiscard]] bool empty() const noexcept { return keys_.empty(); }
  [[nodiscard]] OmcInt bucket_count() const noexcept { return static_cast<OmcInt>(buckets_.size()); }

  // keyList / valueList（:429-439）、toList（:423-427）、keyArray / valueArray：都是这两个数组，插入序。
  [[nodiscard]] const std::vector<K>& keys() const noexcept { return keys_; }
  [[nodiscard]] const std::vector<V>& values() const noexcept { return values_; }

  // merge（:573-599）：拷大的（相等时拷第二个），把小的按插入序 addUnique 进去。两边有相同的键时 OMC 报内部错误后继续，这里断言。
  [[nodiscard]] static UnorderedMap merge(const UnorderedMap& map1, const UnorderedMap& map2) {
    const bool first_bigger = map1.keys_.size() > map2.keys_.size();
    UnorderedMap result = (first_bigger ? map1 : map2).copy();
    const UnorderedMap& tmp = first_bigger ? map2 : map1;
    for (std::size_t i = 0; i < tmp.keys_.size(); ++i) {
      const bool added = result.add_unique(tmp.keys_[i], tmp.values_[i]);
      YMC_ASSERT(added, "UnorderedMap::merge: both maps contain the same key");
    }
    return result;
  }
  // subMap（:601-619）：桶数 nextPrime(列表长度)，序跟列表走。键不存在时 OMC 报内部错误，这里断言。
  [[nodiscard]] UnorderedMap sub_map(const std::vector<K>& list) const {
    UnorderedMap out(hash_, eq_, next_prime(static_cast<OmcInt>(list.size())));
    for (const K& k : list) {
      const V* v = get(k);
      YMC_ASSERT(v != nullptr, "UnorderedMap::sub_map: key does not exist");
      out.add(k, *v);
    }
    return out;
  }

 private:
  struct Found { OmcInt index; OmcInt bucket; };   // index 是 keys_ 的 0 基下标，-1 = 没有

  [[nodiscard]] OmcInt bucket_of(const K& key, OmcInt count) const { return int_mod(hash_(key), count); }
  // find（:770-795）：0 桶的 Map 可以查（Alias 用变量数当桶数，模型没变量时就是 0），bucket 记 0。
  [[nodiscard]] Found find(const K& key) const {
    if (buckets_.empty()) return {-1, 0};
    const OmcInt h = bucket_of(key, bucket_count());
    const std::vector<std::uint32_t>& b = buckets_[static_cast<std::size_t>(h)];
    for (auto it = b.rbegin(); it != b.rend(); ++it) {
      if (eq_(key, keys_[*it])) return {static_cast<OmcInt>(*it), h};
    }
    return {-1, h};
  }
  // addEntry（:797-819）：**先 push 再判**负载，和 Set 相反，rehash 时新键已在 keys_ 里。
  // 0 桶的 Map 不能加：OMC 里 loadFactor 除零 fail（实测，单测 map_zero_buckets），NB 没有这条路。
  void add_entry(const K& key, V value, OmcInt hash) {
    YMC_ASSERT(!buckets_.empty(), "UnorderedMap: add to a map with zero buckets");
    keys_.push_back(key);
    values_.push_back(std::move(value));
    if (size() > bucket_count()) {
      rehash();
    } else {
      buckets_[static_cast<std::size_t>(hash)].push_back(static_cast<std::uint32_t>(keys_.size() - 1));
    }
  }
  // rehash（:691-713）：新桶数 nextPrime(键数 * 2)，按 keys_ 序重灌。
  void rehash() {
    const OmcInt n = next_prime(size() * 2);
    buckets_.assign(static_cast<std::size_t>(n), {});
    for (std::size_t i = 0; i < keys_.size(); ++i) {
      buckets_[static_cast<std::size_t>(bucket_of(keys_[i], n))].push_back(static_cast<std::uint32_t>(i));
    }
  }

  std::vector<std::vector<std::uint32_t>> buckets_;   // 每桶 back = OMC 的表头
  std::vector<K> keys_;
  std::vector<V> values_;
  Hash hash_;
  Eq eq_;
};

}  // namespace ymc::bdae
