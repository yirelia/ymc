// Absyn 上的小工具（S2 装载器用）：按全限定路径找类、把文件里的类插进包围类。
//
// 依据：MLS §13.4.1（目录的子条目是包围类的元素；两个子条目不得同名）、
//       §13.4.3（within 指向的包围类必须存在且不能是短类定义）。
#pragma once
#include <string>
#include <vector>

#include "absyn/absyn.h"

namespace ymc {

// 在 parent 的元素里按名字找嵌套类（只看 ElementKind::ClassDef）；找不到返回 invalid。
[[nodiscard]] Id<ClassDef> find_nested_class(const Absyn& a, Id<ClassDef> parent, Symbol name);

// 按全限定路径找类：第一段在 Absyn::top 里找，其余段逐层进嵌套类。找不到返回 invalid。
[[nodiscard]] Id<ClassDef> find_class(const Absyn& a, const std::vector<Symbol>& path);

enum class InsertResult { Ok, ParentIsShortClass, DuplicateName };

// 把 cls 追加为 parent 的元素（MLS §13.4.1）。parent 是短类定义 → ParentIsShortClass（§13.4.3）；
// parent 已有同名嵌套类 → DuplicateName（§13.4.1）。
InsertResult insert_class(Absyn& a, Id<ClassDef> parent, Id<ClassDef> cls, bool is_protected = false);

// "A.B.C" 形式的名字（调试/诊断用）。
[[nodiscard]] std::string qualified_name(const Absyn& a, const std::vector<Symbol>& path);

// 两个书写表达式结构相同（同 kind、同字面量 / 运算符 / 名字，子式递归相同；对标 AbsynUtil.expEqual）。括号（1 元 Tuple）
// 不看穿、Real 字面量按拼写比。
[[nodiscard]] bool exp_equal(const Absyn& a, Id<Exp> x, Id<Exp> y);

}  // namespace ymc
