// 类型（S3 实例化/压平/后端共用）——对标 OMC NFFrontEnd/NFType.mo + NFDimension.mo。
//
// 设计（ADR-006 D1、研究底稿 R14 §类型表示）：
//   - `Type` = enum `TypeKind` + 载荷，值语义（挂在每个 Expr 节点上，像 OMC 的 NFExpression 持 NFType）。
//   - **数组保持扁平**：一个元素类型 + `std::vector<Dimension>`，绝不 array-of-array（对标 NFType.ARRAY，
//     `liftArrayLeft` 在左边加维度）。压平打印、下标运算、标量化全靠这个不变量（R14）。
//   - `TypeKind` 用整数序，便于 `Integer→Real` 之类隐式提升用"kind 比较"快判（R14）。
//
// v1 竖切片（ADR-006 D6）只用标量：Real/Integer/Boolean/String/Enumeration + Unknown。
// Array/Complex/Clock/Tuple/Function 的 kind 先占位（enum 稳定），载荷与逻辑等到对应竖切片再长肉。
#pragma once
#include <cstdint>
#include <vector>

#include "base/arena.h"
#include "base/symbol.h"

namespace ymc {

struct Expr;  // 前置声明：Dimension 的 EXP 维度持 Id<Expr>（见下）；expr.h 反过来 include 本文件

// 维度变率（对标 NFDimension 里维度大小的 variability）：常量维度可直接标量化，参数维度要先求值。
enum class DimVariability : std::uint8_t { Constant, Parameter, Discrete, Continuous };

// ---- 维度（对标 NFDimension.mo 的 Dimension）----
// v1 无数组，这里给出完整形态但暂不参与实例化；数组竖切片时再接 typing/scalarize。
enum class DimKind : std::uint8_t {
  Integer,    // 已知整数大小：INTEGER(size, var)
  Exp,        // 大小是表达式（参数/符号）：EXP(exp, var)
  Boolean,    // Boolean 下标（大小 2）
  Enum,       // 枚举下标（大小 = 字面量数）
  Unknown,    // `:` 未知维度（函数形参、外部）
};

struct Dimension {
  DimKind kind = DimKind::Unknown;
  std::int64_t size = 0;             // Integer
  Id<Expr> exp;                      // Exp：大小表达式；Enum：枚举类型的某个引用
  DimVariability var = DimVariability::Constant;
  bool is_processing = false;        // 检测循环维度（对标 NFDimension 的 processing 标志）
};

// ---- 类型（对标 NFType.mo 的 Type）----
enum class TypeKind : std::uint8_t {
  // 顺序即隐式提升的偏序里的一段：Integer < Real（R14）。
  Boolean,
  Integer,
  Real,
  String,
  Enumeration,   // 载荷 enum_path + enum_literals
  // —— 以下 v1 不用，占位保持 enum 稳定 ——
  Clock,
  Array,         // 载荷 array_elem(kind 折进本 struct) + dims；扁平
  Tuple,         // 载荷 tuple_types（多输出函数、元组赋值）
  Complex,       // record / 用户 class 实例；载荷 complex_cls
  Function,      // 函数类型（函数竖切片）
  Polymorphic,   // 类型参数（MetaModelica 风，NF 内部）
  Unknown,       // 尚未定型
};

struct Type {
  TypeKind kind = TypeKind::Unknown;

  // Enumeration：类型的全限定名（点分）与字面量名，按声明序（R13：打印成 `enumeration(a, b, c)`）。
  std::vector<Symbol> enum_path;
  std::vector<Symbol> enum_literals;

  // Array（扁平）：元素类型的 kind + 其自身载荷放在 array_elem 里；dims 是各维。
  //   不变量：array_elem->kind != Array（扁平）。v1 恒空。
  std::vector<Dimension> dims;             // 非空 ⟺ kind == Array（或后续被 lift 的类型）
  // array_elem 用 Id 化会更省；v1 无数组，先留一个可选的元素类型指针位（数组竖切片时再定）。

  // Complex：实例节点（inst 层的 INode）；用无类型 Id 避免本文件依赖 inode.h，接线时再定强类型别名。
  std::uint32_t complex_node = 0xFFFFFFFFu;

  [[nodiscard]] bool is_scalar() const noexcept { return dims.empty(); }
  [[nodiscard]] bool is_numeric() const noexcept {
    return kind == TypeKind::Integer || kind == TypeKind::Real;
  }

  // v1 常用标量类型的工厂（内建类型定型时用）。显式赋 kind（不用聚合初始化，避免
  // -Wmissing-field-initializers；本项目两套编译器 -Werror 零警告）。
  static Type of(TypeKind k) { Type t; t.kind = k; return t; }
  static Type boolean() { return of(TypeKind::Boolean); }
  static Type integer() { return of(TypeKind::Integer); }
  static Type real() { return of(TypeKind::Real); }
  static Type string() { return of(TypeKind::String); }
  static Type unknown() { return of(TypeKind::Unknown); }
};

// 隐式类型提升是否允许 from→to（R14：只做内建数值 Integer→Real；同类型总允许）。
// 用于二元运算与绑定匹配；实际插 CAST 节点在 typing 趟（expr.h 的 ExprKind::Cast）。
[[nodiscard]] inline bool type_promotable(TypeKind from, TypeKind to) noexcept {
  if (from == to) return true;
  return from == TypeKind::Integer && to == TypeKind::Real;
}

}  // namespace ymc
