// BackendDAE 文本（ADR-008 D3）：版式逐字照 NB 自己的 dump，这样同一份文本既能和 omc -d=optdaedump 比字节，也是以后 .bdae 的正文。
#pragma once
#include <string>

#include "bdae/bdae.h"

namespace ymc::bdae {

// Expression.toString（NFExpression.mo:1894-1987 v1.25.0）。和压平用的 dump_expr 是两套版式：这边不打 cast、负 Integer 字面量也加括号、
// `*` 两侧优先级相同，而且认识 Multary。
[[nodiscard]] std::string exp_string(const ExprArena& a, const Interner& in, Id<Expr> e);

// 整段 `BackendDAE: (After Lowering)`：BackendDAE.toString（NBackendDAE.mo:148-171）在分区为空时的那一支。
// 字节范围 = omc 这一段从头线第一个 `#` 到下一段头线之前（tools/structcompare.py after_lowering_text）。
[[nodiscard]] std::string dump_after_lowering(const BackendDAE& dae, const Interner& in);

}  // namespace ymc::bdae
