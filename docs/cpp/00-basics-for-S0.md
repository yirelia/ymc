# C++ 基础速通 —— 只讲 S0 两张任务卡需要的

读法：不用一次读完。先看 §1–§3 就可以开始 T-S0-01；写到哪里卡住，回来查对应小节。每一节结尾有"你会怎么用"。

---

## 1. 一个 C++ 程序是怎么拼起来的

- **翻译单元（translation unit）**：一个 `.cpp` 文件 + 它 `#include` 进来的所有头文件 = 编译器一次处理的整体。每个 `.cpp` 单独编译成 `.o`，最后链接器把所有 `.o` 拼成 exe。
- **头文件 `.h`**：放"声明"（这个东西长什么样）；`.cpp` 放"定义"（它怎么干活）。头文件第一行永远写 `#pragma once`，防止同一头被包含两次。
- **一次定义规则（ODR）**：一个函数/变量在整个程序里只能定义一次。所以普通函数的函数体写在 `.cpp`；写在 `.h` 里会被多个 `.cpp` 包含 → 链接时报 `multiple definition of ...`。例外：`inline` 函数、类内直接写的成员函数、**模板**——这些必须在头文件里（见 §7）。
- **namespace**：本项目所有代码放在 `namespace ymc { ... }` 里，避免和标准库、测试框架撞名。

你会怎么用：`symbol.h` 放 `struct Symbol` 和 `class Interner` 的声明；`symbol.cpp` 放 `Interner` 各成员函数的函数体；`arena.h` 是模板，全部写在头里。

## 2. 值、引用、指针、const

```cpp
int a = 1;
int& r = a;        // 引用：a 的别名，改 r 就是改 a。引用必须初始化，不能"指向别的"
const int& cr = a; // 常量引用：只能读。函数参数传大对象用 const T&，避免拷贝
int* p = &a;       // 指针：存地址。本项目 IR 里禁止用指针互相引用（用 Id）
```

- **`const` 成员函数**：`size_t size() const;` 末尾的 `const` 表示"这个函数不改对象"。通过 `const Interner&` 只能调 const 成员函数。规则：能加 const 的都加。
- **传参三选一**：小东西（int、`Symbol`、`Id<T>`、`string_view`）**按值**传；大东西只读 **`const T&`**；要把东西"交给"函数去存 → **按值 + `std::move`**（§6）。

你会怎么用：`std::string_view str(Symbol s) const;`——`s` 按值（4 字节），函数 `const`。

## 3. struct / class、构造函数、`= default` / `= delete`

```cpp
struct Symbol {                        // struct：默认 public，用来装"纯数据 + 小函数"
  uint32_t id = kInvalid;              // 成员默认值：默认构造出来就是无效
  static constexpr uint32_t kInvalid = 0xFFFFFFFFu;   // 类级常量
  bool is_valid() const { return id != kInvalid; }
  bool operator==(const Symbol&) const = default;     // C++20：让编译器生成逐成员比较
};

class Interner {                       // class：默认 private，用来装"有不变量要保护的对象"
 public:
  Interner() = default;                // 默认构造
  Interner(const Interner&) = delete;  // 禁止拷贝（为什么？见 T-S0-01 坑 #2）
  Interner& operator=(const Interner&) = delete;
 private:
  // 数据成员，名字末尾加下划线
};
```

- `= default`：叫编译器生成；`= delete`：禁止这个操作，谁用谁编译错——这是 C++ 表达"设计意图"的方式。
- `explicit`：单参数构造函数前加它，防止 `Symbol s = 5;` 这种隐式转换。`Id<T>` 会用到。

## 4. `std::vector` —— 你要理解它什么时候"搬家"

`std::vector<T>` 是连续内存数组。`push_back` 时容量不够就 **分配一块更大的内存，把所有元素搬过去，释放旧的**。搬家之后，**所有指向旧元素的指针、引用、`string_view` 全部失效**（悬垂）。

```cpp
std::vector<int> v;
v.push_back(1);
int& first = v[0];      // 引用第 0 个
for (int i = 0; i < 1000; ++i) v.push_back(i);   // 某次 push_back 搬家了
first = 5;              // 未定义行为：first 指向已释放的内存。可能"看起来正常"，也可能崩
```

这就是方案里"持有 Id，不持有引用跨越 `add`"那条规则的来源，也是 T-S0-01 坑 #1 的根源。`reserve(n)` 预留容量能减少搬家，但不能当作保证。

`size()` 返回 `size_t`（无符号）；和 `int` 比较会有符号/无符号警告——用 `std::uint32_t` 或 `size_t` 做下标。

## 5. `std::string` vs `std::string_view`

- `std::string`：**拥有**一段字符（自己分配内存、自己释放）。
- `std::string_view`：**指向**别人的字符 + 长度，不拥有。零拷贝、便宜，但**被指向的东西没了它就悬垂**。
- 小字符串优化（SSO）：短字符串（libstdc++ 是 ≤15 字节）直接存在 `std::string` 对象内部。所以 **`std::string` 对象一搬家，指向它内部的 `string_view` 就废了**——这就是"vector<string> 存符号"必错的原因。

规则：函数**参数**用 `string_view`（谁调都方便）；要**保存**就必须拷成 `std::string` 放进稳定的容器。

## 6. 移动语义，只需要知道这么多

`std::move(x)` 不移动任何东西，它只是把 `x` 标成"可以被掏空"。接收方（比如 `vector::push_back(T&&)`）会把 `x` 内部的资源（堆内存指针）偷走，`x` 变成空壳。之后不要再用 `x`。

```cpp
Id<T> Arena<T>::add(T value) {        // 按值接收：调用方 add(std::move(p)) 时零拷贝
  items_.push_back(std::move(value));  // 再把 value 移进 vector；不加 std::move 就是拷贝（unique_ptr 会编译错）
  ...
}
```

`std::unique_ptr` 只能移动不能拷贝——arena_test 的 A-8 就是靠它检验你有没有写 `std::move`。

## 7. 模板：10 行够用

```cpp
template <class T>          // "对任意类型 T"
struct Id {
  uint32_t raw;
};
Id<int> a;  Id<double> b;   // 编译器为每个用到的 T 生成一份独立的类型；Id<int> 和 Id<double> 毫无关系
```

- 模板的定义必须**整个放在头文件**（编译器看到 `Id<Expr>` 时要能看见完整代码才能生成）。
- `Id<Expr>` 和 `Id<Var>` 是不同类型——这正是我们要的"强类型 Id"：把 `Id<Var>` 传给要 `Id<Expr>` 的函数直接编译错，而裸 `uint32_t` 不会。这叫 phantom type（幽灵类型），`T` 本身在结构里根本没用到。
- 模板成员函数写在类外要重复 `template <class T>` 前缀：`template <class T> Id<T> Arena<T>::add(T v) { ... }`。初学建议直接写在类内。

## 8. `std::unordered_map` 和自定义哈希

```cpp
std::unordered_map<std::string_view, uint32_t> index_;   // key → value
auto it = index_.find(text);      // 找不到返回 end()
if (it != index_.end()) return it->second;
index_.emplace(key, value);       // 插入
```

- 用自己的类型当 key，需要告诉标准库怎么哈希：特化 `std::hash`：

```cpp
template <>
struct std::hash<ymc::Symbol> {
  size_t operator()(ymc::Symbol s) const noexcept { return std::hash<uint32_t>{}(s.id); }
};
```

模板类型（`Id<T>`）用"偏特化"：`template <class T> struct std::hash<ymc::Id<T>> { ... };`。

- key 是 `string_view` 时，map 只存"视图"，**字符本体必须在别处稳定存放**（T-S0-01 的核心设计点）。

## 9. `std::deque`：为什么它能解决稳定性问题

`std::deque<T>` 是分块数组：`push_back` **不搬已有元素**（标准保证：push_back 后指向已有元素的引用仍然有效）。所以 `std::deque<std::string>` 里每个 `string` 对象一旦放进去地址就不变，指向它内部的 `string_view` 一直有效。代价是随机访问比 vector 慢一点点，符号表不在乎。

## 10. 固定宽度整数、`[[nodiscard]]`、`noexcept`、`static_assert`

- `<cstdint>` 的 `std::uint32_t`：明确 32 位无符号。Id 用它：40 亿个节点够用，且比 `size_t` 省一半内存。
- `[[nodiscard]]`：调用者丢弃返回值时编译器警告。`intern()`、`add()` 都要加——忘了接返回值就是 bug。
- `noexcept`：承诺不抛异常，标准库据此选更快的路径。只加在确实不会抛的小函数上（`is_valid`、`size`）。
- `static_assert(cond, "msg")`：编译期断言。

## 11. 读懂 GCC 的报错

- `error: 'Symbol' was not declared in this scope` → 少 `#include` 或少 `namespace`/`using`。
- `error: passing 'const Interner' as 'this' argument discards qualifiers` → 你在 const 对象上调了非 const 成员函数，给函数加 `const`。
- `error: use of deleted function 'Interner::Interner(const Interner&)'` → 有地方拷贝了 Interner（按值传参了？改成 `Interner&`）。
- `undefined reference to 'ymc::Interner::intern(...)'` → 链接错：声明了没定义，或 `.cpp` 没进 CMake（本项目 `src/base/*.cpp` 自动收集，重新 configure 一次）。
- `error: no match for 'operator=='` → 缺 `operator==`（`= default` 那行）。
- `warning: comparison of integer expressions of different signedness` → `int` 和 `size_t` 比了，改类型。
- 模板报错很长：**只看第一个 `error:`**，后面多半是连锁反应。

## 12. gdb 最小命令集（TDM-GCC 自带 `gdb`）

```
cd build/gcc-debug
gdb tests/symbol_test.exe
(gdb) break ymc::Interner::intern        # 在函数入口下断点（先 tab 补全看名字）
(gdb) run                                # 跑到断点
(gdb) next   / n                         # 单步（不进函数）
(gdb) step   / s                         # 单步（进函数）
(gdb) print text                         # 看变量；print storage_.size()
(gdb) bt                                 # 调用栈
(gdb) finish                             # 跑完当前函数回到调用处
(gdb) continue / c
(gdb) quit
```

YMC_ASSERT 失败会 `abort()`，gdb 会停在那里，`bt` 就能看到是谁调的。

### 12b. 更省力的选择：Visual Studio 2022 调试器

`cmake --preset msvc` 之后打开 `build\msvc\ymc.sln`：
- 右键 `symbol_test` → 设为启动项目；在代码行左侧点一下打断点；**F5** 启动，**F10** 单步、**F11** 进函数、**Shift+F11** 跳出。
- "局部变量"窗口直接显示 `storage_`（deque）和 `index_`（unordered_map）的内容——gdb 在 TDM-GCC 上做不到这么漂亮。
- `YMC_ASSERT` 失败时 VS 会弹出对话框，点"重试"就停在断言处，"调用堆栈"窗口能看到是谁调的。
- ASan：`cmake --preset msvc-asan` 构建出的可执行文件，越界/悬垂访问会直接报出 `heap-buffer-overflow` 之类，带源码行号。T-S0-01 坑 #1 的实验用它跑，现象更清楚。

## 13. CMake 三步心智模型

1. **configure**：`cmake --preset gcc-debug` —— 读 `CMakeLists.txt`，生成 `build/gcc-debug/` 下的 Makefile。加了新 `.cpp` 或新测试文件要重新跑一次。
2. **build**：`cmake --build --preset gcc-debug` —— 调 `mingw32-make` 真正编译。
3. **test**：`ctest --preset gcc-debug` —— 跑所有 `add_test` 登记的可执行文件；`-R symbol` 只跑名字匹配的；`--output-on-failure` 失败时显示输出。

单独跑一个测试看完整输出：`build\gcc-debug\tests\symbol_test.exe`。
