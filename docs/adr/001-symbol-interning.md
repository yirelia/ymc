# ADR-001 符号表用 intern + deque 存原文

- 日期：2026-09-14
- 状态：已接受

## 背景
编译器里标识符/cref 会被反复比较、哈希、当 map key。若直接用 `std::string`，每次操作代价随
名字长度线性增长，且 IR 里到处存字符串很费内存。需要一个"名字 ↔ 整数"的双向表。

## 决定
- 用 `Interner`：`intern(text)` 把原文拷进内部、分配连续 `uint32_t` id，返回 4 字节 `Symbol`；
  `str(sym)` 取回 `string_view`。之后所有比较/哈希都是整数操作。
- 原文存在 `std::deque<std::string>`（不是 `std::vector<std::string>`）；索引用
  `unordered_map<string_view,uint32_t>`，key 指向 deque 里的那份拷贝。
- `Interner` 禁拷贝。

## 依据
- 设计动机：OMC 后端 `NBVariable.mo:1949` 的 `UnorderedMap<ComponentRef,Integer>` 等到处是
  "名字→索引"。（方案 §3.4 第 4 条：标识符全 intern 成 Symbol。）
- **deque vs vector 实验**（任务卡 T-S0-01 要求实测）：不变式 I-7 要求 `str()` 返回的
  `string_view` 在 Interner 存活期内永远有效。`std::vector<std::string>` 扩容会整体搬家，
  加上 `std::string` 的小字符串优化（SSO，短串存在对象内部），指向旧地址的 view 会悬垂；
  实测 10 万次插入后早期 view 读到垃圾/崩溃。`std::deque` 分块存储，push_back 不搬动已有
  元素，view 稳定。测试 `views_stay_valid_after_many_inserts` 用 100001 次插入固化这条。
- 禁拷贝：拷贝后新对象 `index_` 的 view 仍指向旧对象 `storage_`，必然悬垂；禁掉比修好便宜。

## 后果
- 得到：整数化的名字比较/哈希；原文只存一份；view 生命周期简单（同 Interner）。
- 放弃：不支持删除符号（只增不减）；不支持多线程并发 intern（暂不需要）。
- 何时重看：若将来需要跨 Interner 合并符号表，或需要并发，再评估。
