// BackendDAE 文本（ADR-008 D3）：版式逐字照 NB 自己的 dump，这样同一份文本既能和 omc -d=optdaedump 比字节，也是以后 .bdae 的正文。
#pragma once
#include <string>
#include <string_view>

#include "bdae/bdae.h"

namespace ymc::bdae {

// Expression.toString（NFExpression.mo:1894-1987 v1.25.0）。和压平用的 dump_expr 是两套版式：这边不打 cast、负 Integer 字面量也加括号、
// `*` 两侧优先级相同，而且认识 Multary。
[[nodiscard]] std::string exp_string(const ExprArena& a, const Interner& in, Id<Expr> e);

// 整段 `BackendDAE: (<stage>)`：BackendDAE.toString（NBackendDAE.mo:148-171）在分区为空时的那一支，pre 模块之后每段都走它（applyModules :374）。
// 字节范围 = omc 这一段从头线第一个 `#` 到下一段头线之前（tools/structcompare.py stage_section_texts）。
[[nodiscard]] std::string dump_stage(const BackendDAE& dae, const Interner& in, std::string_view stage);
[[nodiscard]] inline std::string dump_after_lowering(const BackendDAE& dae, const Interner& in) {
  return dump_stage(dae, in, "After Lowering");
}

}  // namespace ymc::bdae
