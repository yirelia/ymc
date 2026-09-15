# T-S0-01 Symbol 与 Interner（符号表）

## 目标

一句话：把标识符字符串变成 4 字节整数 `Symbol`，之后所有比较、哈希、当 map 的 key 都是整数操作；需要原文时用 `str()` 拿回 `string_view`。

## 来源（方案 §3.6）

- 设计动机：OMC 后端里 cref → 索引的 map 到处都是（`NBVariable.mo:1949 VARIABLE_POINTERS` 里 `UnorderedMap<ComponentRef, Integer>`，`NBEquation.mo:4220 EquationPointers` 同样），每次查找都要哈希整个名字。我们把"名字 → 整数"这一步做一次（intern），后面全是整数。
- 这是基建，没有 Modelica 语义，不引用 MLS。
- ADR：`docs/adr/001-symbol-interning.md`（你写完这张卡后由你补一段"为什么用 deque"，30 行以内）。

## 输入 → 输出

- `intern(text)`：任意 `string_view`（可以是临时缓冲区）→ `Symbol`。
- `str(sym)`：`Symbol` → `string_view`，指向 Interner 内部拷贝。
- `lookup(text)`：只查不插。

## 接口（你在 `src/base/symbol.h` 里照着敲；`symbol.cpp` 写成员函数体）

```cpp
#pragma once
#include <cstddef>        // size_t
#include <cstdint>        // uint32_t
#include <deque>
#include <functional>     // std::hash
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace ymc {

struct Symbol {
  static constexpr std::uint32_t kInvalid = 0xFFFFFFFFu;
  std::uint32_t id = kInvalid;

  [[nodiscard]] bool is_valid() const noexcept { return id != kInvalid; }
  bool operator==(const Symbol&) const = default;
};

class Interner {
 public:
  Interner() = default;
  Interner(const Interner&) = delete;             // 见坑 #2
  Interner& operator=(const Interner&) = delete;

  [[nodiscard]] Symbol intern(std::string_view text);                    // 不存在则拷贝并分配下一个 id
  [[nodiscard]] std::string_view str(Symbol s) const;                    // 前置条件：s 由本 Interner 产生
  [[nodiscard]] std::optional<Symbol> lookup(std::string_view text) const;
  [[nodiscard]] std::size_t size() const noexcept;

 private:
  std::deque<std::string> storage_;                        // 第 i 个元素 = id 为 i 的符号原文（拥有字符）
  std::unordered_map<std::string_view, std::uint32_t> index_;   // 原文视图 → id；视图指向 storage_ 里的字符串
};

}  // namespace ymc

// 让 Symbol 能当 unordered_map / unordered_set 的 key
template <>
struct std::hash<ymc::Symbol> {
  std::size_t operator()(ymc::Symbol s) const noexcept { return std::hash<std::uint32_t>{}(s.id); }
};
```

## 不变量（测试 I-1 … I-9 逐条对应）

| # | 不变量 |
|---|---|
| I-1 | 默认构造的 `Symbol` 无效 |
| I-2 | 同一文本 ↔ 同一 `Symbol`；不同文本 ↔ 不同 `Symbol` |
| I-3 | id 从 0 起、按首次出现顺序连续；`size()` = 已分配 id 数 |
| I-4 | `intern` 必须拷贝输入；调用方的缓冲区销毁后 `str()` 仍正确 |
| I-5 | `lookup` 不插入 |
| I-6 | 空串是合法符号 |
| I-7 | `str()` 返回的视图在 Interner 生命周期内**永远**有效（十万次插入后仍有效） |
| I-8 | `Symbol` 可哈希 |
| I-9 | Interner 不可拷贝 |

复杂度：`intern`/`lookup` 平均 O(名字长度)，`str` O(1)。

## 不做

- 不做多线程安全；不做删除（符号只增不减）；不做大小写折叠；不预置关键字（词法器阶段再决定）。

## 分步实施（每步都能编译，每步都跑一次测试）

**第 0 步：让测试出现。** 创建空的 `src/base/symbol.h`（只写 `#pragma once`）和空的 `src/base/symbol.cpp`（写一行注释）。重新 configure：`cmake --preset gcc-debug`。此时 `symbol_test` 目标出现但编译失败——这是预期的，你要的是"看到它出现"。

**第 1 步：`Symbol`。** 在头里写 `struct Symbol` 和 `std::hash<ymc::Symbol>` 特化。写一个临时的空 `class Interner {};`。编译 `symbol_test`：I-1、I-9 之外的测试还编不过，没关系，先看 I-1 那个测试函数能否单独通过——可以把其它测试暂时注释掉（记得恢复）。
- 观察点：`Symbol` 是 4 字节——在 gdb 里 `print sizeof(ymc::Symbol)`。

**第 2 步：`Interner` 骨架。** 按接口写声明，`symbol.cpp` 里每个函数先写最笨的实现：`intern` 直接 `storage_.push_back(std::string(text))`，返回 `Symbol{size-1}`，先不查重。跑测试：I-2 会 FAIL（重复文本得到不同 id）——**故意的**，你要亲眼看到没有 `index_` 时的行为。
- 这一步要学：`std::string(text)` 从 `string_view` 构造 `string` 是显式的；`storage_.size()` 是 `size_t`，转 `uint32_t` 要 `static_cast<std::uint32_t>(...)`（否则 `-Wconversion` 类警告；本项目没开它，但养成习惯）。

**第 3 步：查重。** `intern` 先 `index_.find(text)`，命中直接返回；未命中才 push 到 `storage_`，然后把 **指向 `storage_.back()` 的 `string_view`** 作为 key 插进 `index_`。
- 关键一行：`index_.emplace(std::string_view(storage_.back()), id);` —— key 必须指向 `storage_` 里那份拷贝，**绝不能**是参数 `text`（参数指向调用方的缓冲区，函数返回就失效 → I-4 会读到垃圾）。
- 跑测试：I-2、I-3、I-4、I-6 应过；I-7 也应过——因为 deque。
- **实验（必做）**：把 `std::deque` 临时改成 `std::vector`，跑 I-7。多半 FAIL 或崩溃。改回来。这 5 分钟胜过读十遍文档。

**第 4 步：`lookup`、`str`、`size`。** `str` 要有前置条件检查：`YMC_ASSERT(s.is_valid() && s.id < storage_.size(), "Symbol not from this Interner")`（`#include "base/assert.h"`）。
- `std::optional<Symbol>`：找到返回 `Symbol{it->second}`，没找到返回 `std::nullopt`。

**第 5 步：全绿 + gdb 一遍。** `ctest --preset gcc-debug -R symbol`。然后用 gdb 在 `intern` 下断点，跑 I-3，`print storage_.size()`、`print index_.size()`，看它们怎么涨。

## 坑（提前告诉你，但你要自己踩一次第 1 个）

1. **`std::vector<std::string>` 存原文**：vector 扩容搬家 + `std::string` 的小字符串优化 → 指向字符串内部的 `string_view` 悬垂。见 `docs/cpp/00-basics-for-S0.md` §4–§5、§9。
2. **为什么 Interner 不可拷贝**：拷贝后新对象的 `index_` 里的 `string_view` 仍指向**旧对象**的 `storage_`。禁掉拷贝比修好它便宜。
3. **key 用参数 `text` 当 `string_view`**：见第 3 步。
4. **`size()` 返回 `storage_.size()` 而不是 `index_.size()`**：两者应相等，但 `storage_` 是真相来源；写个 `YMC_ASSERT` 保证相等是好习惯。
5. **`[[nodiscard]]` 触发的警告**：测试 I-7 里 `in.intern("sym_" + ...)` 故意没接返回值，编译时会出 `warning: ignoring return value ... declared with attribute 'nodiscard'`。这是留给你的第一个"读懂警告并处理"练习：把那行改成 `(void)in.intern(...)`（显式声明"我知道我在丢弃返回值"）。

## 验收

```
cmake --preset gcc-debug
cmake --build --preset gcc-debug
ctest --preset gcc-debug -R symbol      # 9 tests, 0 failed
```

## 调试手段（本任务新增）

无新增 flag。gdb 在 `ymc::Interner::intern` 下断点；`print storage_`（gdb 可能不会漂亮地打印 deque，用 `print storage_.size()` 和 `print storage_[0]`）。

## C++ 要点

`#pragma once`、struct 默认成员值、`= default/= delete`、`const` 成员函数、`string_view` vs `string`、`deque` 的稳定性保证、`unordered_map::find/emplace`、`std::hash` 特化、`std::optional`、`static_cast`、`[[nodiscard]]`。

## 完成定义

- [ ] `ctest -R symbol` 全绿
- [ ] 做过"vector 替换实验"并在 `docs/adr/001-symbol-interning.md` 写下观察结果
- [ ] 无编译警告
- [ ] 评审通过（我看：不变量是否都有断言、命名规范、注释是否写"为什么"）
