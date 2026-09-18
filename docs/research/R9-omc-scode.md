# R9：OMC 的 SCode 数据模型、Absyn→SCode 翻译规则、SCode 文本 dump

> 源码根 `C:\dev\OpenModelica\OMCompiler\Compiler\`（下文 `FrontEnd/` 即该目录下），基线提交 `3ceb0c9d06`（本会话 `git log -1` 确认）。行号均在本会话 `cat -n`/`awk` 实读；探针用安装版 `omc` 1.25.0，文件在 scratchpad `probe-r9\`（probe.mo / probe.mos / probe_out.txt / mini*.mos）。

## 结论摘要

1. SCode 是"元素扁平列表 + 按种类分桶"的 Absyn 镜像：`Program = list<Element>`，Element 五种（CLASS/COMPONENT/EXTENDS/IMPORT/DEFINEUNIT）；表达式、TypeSpec、Path、Import、InnerOuter、Direction、Subscript 全部**直接复用 Absyn 类型**（SCode.mo:55-57, :532, :547, :575, :599-600）。
2. 归一化只做六件事：组件列表拆成单个 COMPONENT（AbsynToSCode.mo:1194-1228）；public/protected 段折进每个元素的 `Prefixes.visibility`（:600-612）；classParts 分成 元素/方程/初始方程/算法/初始算法/约束/external 桶（:448-456）；`x.y = z` 展成 `x(y = z)`（:1790-1794）；组导入 `A.{x, y=z}` 拆成单条 IMPORT（:1265-1291）；`assert/terminate/reinit` 从 NORETCALL 提升为专用节点（:1595-1619, :844-865）。
3. 所有布尔前缀变两值 uniontype（SCode.mo:468-517）；final/redeclare/replaceable/inner-outer 集中在 `Prefixes`（:527-535）；**组件级 final 不进 Mod**，只进 Prefixes（AbsynToSCode.mo:1201, :1208）。
4. constrainedby → `REPLACEABLE(SOME(CONSTRAINCLASS(path, mod, comment)))`（:1211, :1357-1364）；带 constrainedby 的元素其字符串注释落在 ConstrainClass.comment（探针 mini4：SCode dump 里 "M comment" 消失，Absyn dump 仍在）。
5. 修饰三态 `MOD/REDECL/NOMOD`；空修饰规范化为 NOMOD（:1714）；子修饰只有 NAMEMOD、无下标修饰（SCode.mo:153-160）；空子修饰丢弃、注解里保留（:1737, :985）；`Absyn.EqMod` 只有 EQMOD/NOMOD（Absyn.mo:588-595），`:=` 与 `=` 在 Absyn/SCode 都无区分。
6. 方程 EQ_IF 把 elseif 摊成并行列表（:1557-1562），语句 ALG_IF 却保留 elseif 列表（:798-804）；多迭代器 for 展成嵌套、第一个迭代器最外层（:1587-1590, :811-814）；for 无范围 → `range = NONE`（:1651）；elsewhen 是 `list<tuple<Exp, list<…>>>`。
7. **陷阱：一个类里多个 `equation` 段在 SCode 中段顺序反转**（:635-637；探针里第二段的 `p.v = 0` 跑到最前），算法段不反转（:691）。初始/普通只靠所在列表区分，节点无 initial 标记。
8. 注释：`COMMENT(Option<ANNOTATION(Mod)>, Option<String>)`，字符串反转义（:1474）；类的多条 annotation 合并成一条（:1500）；extends / class-extends / constrainedby / 注解 的 Mod 只有 dummyInfo（:1176, :492, :1361, :985）。
9. external → `EXTERNALDECL(funcName, lang, output_, args, annotation_)` 五字段直搬（:925-929），函数 restriction 据此变 FR_EXTERNAL_FUNCTION（:285-288）；误放在函数类上的 Library/Include 注解被搬进 ExternalDecl 并警告（:514-561）。
10. 没有任何"合并"：同名 extends、重复 import 各自一个元素；元素顺序 = 源码顺序（:604, :966, :1229）。
11. dump：无 `-d` 标志；用脚本 `list(X, exportKind=OpenModelica.Scripting.ExportKind.SCode)`（安装版 1.25.0 里写 `ExportKind.SCode` 会报 "Class list not found"，必须全限定）。输出是可再解析的 Modelica 文本，且顺序重排为 元素→initial equation→initial algorithm→equation→algorithm（SCodeDumpTpl.tpl:198-214）。
12. 对 ymc：S2 = "每元素一节点 + 五个分桶 + Prefixes/Attributes 两个小结构体"，表达式层引用 S1 的 Absyn 节点而不复制；顺序保真、`:=`/段序等 OMC 丢掉的信息 ymc 可以选择保留（见 §6）。

## SCode 数据模型（FrontEnd/SCode.mo，共 656 行）

别名 `Ident = Absyn.Ident; Path = Absyn.Path; Subscript = Absyn.Subscript`(:55-57)；`type Program = list<Element>`(:163)。**本版本没有独立的 EEquation**（任务卡里的旧名字）：方程只有一个 `uniontype Equation`(:266-343)。下表字段按定义顺序，行号 = record 定义区间。

| uniontype（行） | 构造子（字段）行 |
|---|---|
| Restriction (:60-101) | R_CLASS :61 / R_OPTIMIZATION :62 / R_MODEL :63 / R_RECORD(`Boolean isOperator`) :64-66 / R_BLOCK :67 / R_CONNECTOR(`Boolean isExpandable`) :68-70 / R_OPERATOR :71 / R_TYPE :72 / R_PACKAGE :73 / R_FUNCTION(`FunctionRestriction functionRestriction`) :74-76 / R_ENUMERATION :78 / R_PREDEFINED_INTEGER,REAL,STRING,BOOLEAN,ENUMERATION :81-85 / R_PREDEFINED_CLOCK :87 / R_METARECORD(`Absyn.Path name; Integer index; Boolean singleton; Boolean moved; list<String> typeVars`) :90-96 / R_UNIONTYPE(`list<String> typeVars`) :98-100 |
| FunctionRestriction (:106-118) | FR_NORMAL_FUNCTION(`Absyn.FunctionPurity purity`) :107-109 / FR_EXTERNAL_FUNCTION(`purity`) :110-112 / FR_OPERATOR_FUNCTION :114 / FR_RECORD_CONSTRUCTOR :115 / FR_PARALLEL_FUNCTION :116 / FR_KERNEL_FUNCTION :117。注释(:103-104)："Same as Absyn.FunctionRestriction except this contains FR_EXTERNAL_FUNCTION and FR_RECORD_CONSTRUCTOR." |
| Mod (:121-150) | MOD(`Final finalPrefix; Each eachPrefix; list<SubMod> subModLst; Option<Absyn.Exp> binding; Option<String> comment; SourceInfo info`) :123-130 / REDECL(`Final finalPrefix; Each eachPrefix; Element element`) :132-136 / BREAK_COMPONENT(`SourceInfo info`) :138-140 / BREAK_CONNECT(`Absyn.ComponentRef lhs, rhs; SourceInfo info`) :142-146 / NOMOD :148 |
| SubMod (:153-160) | **只有** NAMEMOD(`Ident ident; Mod mod`) :156-159，无下标修饰。注释(:153-155)："Modifications using qualified names (such as in `x.y = z') are normalized (to `x(y = z)')." |
| Enum (:167-172) | ENUM(`Ident literal; Comment comment`) :168-171 |
| ClassDef (:175-228) | PARTS(`list<Element> elementLst; list<Equation> normalEquationLst, initialEquationLst; list<AlgorithmSection> normalAlgorithmLst, initialAlgorithmLst; list<ConstraintSection> constraintLst; list<Absyn.NamedArg> clsattrs; Option<ExternalDecl> externalDecl`) :193-202 / CLASS_EXTENDS(`Mod modifications; ClassDef composition`) :204-207（无 baseClassName，名字在外层 CLASS.name）/ DERIVED(`Absyn.TypeSpec typeSpec; Mod modifications; Attributes attributes`) :209-213 / ENUMERATION(`list<Enum> enumLst`，空 = `enumeration(:)`) :215-217 / OVERLOAD(`list<Absyn.Path> pathLst`) :219-221 / PDER(`Absyn.Path functionPath; list<Ident> derivedVariables`) :223-226 |
| Comment / Annotation / ExternalDecl | COMMENT(`Option<Annotation> annotation_; Option<String> comment`) :234-237，`constant noComment = COMMENT(NONE(),NONE())` :241 / ANNOTATION(`Mod modification`) :247-249 / EXTERNALDECL(`Option<Ident> funcName; Option<String> lang; Option<Absyn.ComponentRef> output_; list<Absyn.Exp> args; Option<Annotation> annotation_`) :255-261 |
| Equation (:266-343)，每个末尾都有 `Comment comment; SourceInfo info` | EQ_IF(`list<Absyn.Exp> condition; list<list<Equation>> thenBranch; list<Equation> elseBranch`) :270-276 / EQ_EQUALS(`Absyn.Exp expLeft, expRight`) :278-283 / EQ_PDE(`expLeft, expRight; Absyn.ComponentRef domain`) :285-291 / EQ_CONNECT(`Absyn.ComponentRef crefLeft, crefRight`) :293-298 / EQ_FOR(`Ident index; Option<Absyn.Exp> range; list<Equation> eEquationLst`) :300-306 / EQ_WHEN(`Absyn.Exp condition; list<Equation> eEquationLst; list<tuple<Absyn.Exp, list<Equation>>> elseBranches`) :308-314 / EQ_ASSERT(`Absyn.Exp condition, message, level`) :316-322 / EQ_TERMINATE(`Absyn.Exp message`) :324-328 / EQ_REINIT(`Absyn.Exp cref, expReinit`) :330-335 / EQ_NORETCALL(`Absyn.Exp exp`) :337-341。文件注释(:267-269)说 elseif 表示为嵌套 if，**已过时**（见 T7） |
| AlgorithmSection / ConstraintSection | ALGORITHM(`list<Statement> statements`) :350-352（无 initial 标记，靠 PARTS 的两个列表区分）/ CONSTRAINTS(`list<Absyn.Exp> constraints`) :357-359 |
| Statement (:362-464)，每个末尾都有 `Comment comment; SourceInfo info` | ALG_ASSIGN(`Absyn.Exp assignComponent, value`) :363-368 / ALG_IF(`Absyn.Exp boolExpr; list<Statement> trueBranch; list<tuple<Absyn.Exp, list<Statement>>> elseIfBranch; list<Statement> elseBranch`) :370-377 / ALG_FOR(`Ident index; Option<Absyn.Exp> range; list<Statement> forBody`) :379-385 / ALG_PARFOR(同) :387-393 / ALG_WHILE(`boolExpr; whileBody`) :395-400 / ALG_WHEN_A(`list<tuple<Absyn.Exp, list<Statement>>> branches`，第一项即 when) :402-406 / ALG_ASSERT(`condition, message, level`) :408-414 / ALG_TERMINATE(`message`) :416-420 / ALG_REINIT(`cref, newValue`) :422-427 / ALG_NORETCALL(`exp`) :429-433 / ALG_RETURN :435-438 / ALG_BREAK :440-443 / ALG_FAILURE(`list<Statement> stmts`) :446-450 / ALG_TRY(`body, elseBody`) :452-457 / ALG_CONTINUE :459-462 |
| 两值前缀 (:468-524) | Visibility{PUBLIC :469, PROTECTED :470} / Redeclare{REDECLARE :475, NOT_REDECLARE :476} / Replaceable{REPLACEABLE(`Option<ConstrainClass> cc`) :489-491, NOT_REPLACEABLE :492} / Final{FINAL :497, NOT_FINAL :498} / Each{EACH :503, NOT_EACH :504} / Encapsulated :509-510 / Partial :515-516 / ConnectorType{POTENTIAL :521, FLOW :522, STREAM :523} |
| ConstrainClass (:479-485) | CONSTRAINCLASS(`Absyn.Path constrainingClass; Mod modifier; Comment comment`) :480-484 |
| Prefixes (:527-535) | PREFIXES(`Visibility visibility; Redeclare redeclarePrefix; Final finalPrefix; Absyn.InnerOuter innerOuter; Replaceable replaceablePrefix`) :528-534 |
| Element (:538-590) | IMPORT(`Absyn.Import imp; Visibility visibility; SourceInfo info`) :546-550 / EXTENDS(`Path baseClassPath; Visibility visibility; Mod modifications; Option<Annotation> ann; SourceInfo info`) :552-558 / CLASS(`Ident name; Prefixes prefixes; Encapsulated encapsulatedPrefix; Partial partialPrefix; Restriction restriction; ClassDef classDef; Comment cmt; SourceInfo info`) :560-569 / COMPONENT(`Ident name; Prefixes prefixes; Attributes attributes; Absyn.TypeSpec typeSpec; Mod modifications; Comment comment; Option<Absyn.Exp> condition; SourceInfo info`) :571-580 / DEFINEUNIT(`Ident name; Visibility visibility; Option<String> exp; Option<Real> weight; SourceInfo info`) :582-588 |
| Attributes / Parallelism / Variability / Initial (:593-625) | ATTR(`Absyn.ArrayDim arrayDims; ConnectorType connectorType; Parallelism parallelism; Variability variability; Absyn.Direction direction; Absyn.IsField isField`) :594-601 / Parallelism{PARGLOBAL :606, PARLOCAL :607, NON_PARALLEL :608} / Variability{VAR :613, DISCRETE :614, PARAM :615, CONST :616} / Initial{INITIAL :623, NON_INITIAL :624}（仅 Inst 传参，不在树里） |

常量：`defaultPrefixes`(:628-634)、`defaultProtectedPrefixes`(:636-642)、`defaultVarAttr/ParamAttr/ConstAttr/InputAttr/OutputAttr`(:644-653)。

## 翻译规则逐条（FrontEnd/AbsynToSCode.mo，共 1867 行）

函数索引（定义行）：translateAbsyn2SCode :73 / translateClass :114 / translateClass2 :121 / translateRestriction :269 / containsExternalFuncDecl :326 / translateAttributes :340 / translateConnectorType :370 / translateClassdef :390 / translateAlternativeExternalAnnotation :514 / translateEnumlist :563 / translateClassdefElements :587 / translateClassdefEquations :622 / …Initialequations :648 / …Algorithms :674 / …Constraints :707 / …Initialalgorithms :738 / translateClassdefAlgorithmitems :765 / translateClassdefAlgorithmItem :772 / translateAlgBranches :893 / translateClassdefExternaldecls :908 / translateEitemlist :939 / translateAnnotation :970 / translateElement :1006 / translateElementspec :1080 / translateImports :1245 / translateGroupImport :1271 / translateRedeclarekeywords :1329 / translateConstrainClass :1344 / translateParallelism :1370 / translateVariability :1382 / translateEquations :1395 / translateCommentWithLineInfoChanges :1417 / getInfoAnnotationOrDefault :1428 / translateComment :1459 / translateCommentList :1479 / translateEquation :1534 / translateEqBranch :1631 / translateIterator :1643 / translateMod :1686 / translateArgs :1719 / translateSub :1773 / translateEach :1820 / checkTypeSpec :1830。

### T1 入口与顶层类
- `translateAbsyn2SCode`(:73-112)：先 `MetaUtil.createMetaClassesInProgram`(:93，MetaModelica uniontype 处理)，再 `sp = list(translateClass(c) for c in inClasses);`(:105)。顺序 = Absyn.PROGRAM.classes 顺序。副作用：重置 System 的 inner/outer、expandable、overconstrained、stream 四个全局标志(:96-102)。
- `translateClass2`(:121-179)：顶层类的 Prefixes 固定为 `PREFIXES(PUBLIC(), NOT_REDECLARE(), sFin, NOT_INNER_OUTER(), NOT_REPLACEABLE())`(:154-159)，注释 "here we set only final as is a top level class!"(:154)。`sFin/sEnc/sPar` 由 `SCodeUtil.boolFinal/boolEncapsulated/boolPartial` 把 Absyn 的 Boolean 转成两值 uniontype(:148-150)。info = Absyn.CLASS.info(:165)。

### T2 Restriction（:269-324）
- `R_FUNCTION(FR_NORMAL_FUNCTION(purity))` → 若 `containsExternalFuncDecl(d)` 则 `FR_EXTERNAL_FUNCTION(purity)` 否则 `FR_NORMAL_FUNCTION(purity)`(:285-288)；`containsExternalFuncDecl` = PARTS/CLASS_EXTENDS 的 classParts 里 `List.any(parts,AbsynUtil.isExternalPart)`(:334-335)。**FR_RECORD_CONSTRUCTOR 在此文件从不产生**。
- `R_RECORD → R_RECORD(false)`，`R_OPERATOR_RECORD → R_RECORD(true)`(:297-298)；`R_CONNECTOR → R_CONNECTOR(false)`，`R_EXP_CONNECTOR → R_CONNECTOR(true)` 且 `System.setHasExpandableConnectors(true)`(:302-303)。其余 1:1(:290-316)。

### T3 类体 ClassDef（translateClassdef :390-512）
- **短类 DERIVED**(:429-437)：`checkTypeSpec(t, info)`；`mod = translateMod(SOME(Absyn.CLASSMOD(a,Absyn.NOMOD())), NOT_FINAL(), NOT_EACH(), NONE(), info)`(:433)；`scodeAttr = translateAttributes(attr, {})`(:434，Absyn.DERIVED.attributes 里的 flow/stream/变量性/方向/数组维 进 DERIVED.attributes）；注释 `translateComment(cmt)`(:435) → `SCode.DERIVED(t,mod,scodeAttr)`。
- **PARTS**(:439-459)：元素、方程、初始方程、算法、初始算法、约束各自**独立扫一遍** classParts(:448-454)；类注解列表 `translateCommentList(ann, cmtString)`(:455)；external 段 `translateClassdefExternaldecls(parts)`(:456) 再经 `translateAlternativeExternalAnnotation`(:457)。typeVars 仅 MetaModelica(:442-449)。
- **枚举**：`ENUMLITERALS` → `SCode.ENUMERATION(translateEnumlist(lst))`(:461-467)；`ENUM_COLON` → `SCode.ENUMERATION({})`(:469-474)。`translateEnumlist`(:563-585)：`Absyn.ENUMLITERAL(id, cmtOpt)` → `SCode.ENUM(id, translateComment(cmtOpt))`(:578-583)——**枚举项的注释/注解保留**（函数头注释 "Comments are lost"(:565) 已过时）。
- **OVERLOAD** → `SCode.OVERLOAD(pathLst)`(:476-481)。
- **class extends**(:483-497)：parts 与 PARTS 同法翻译(:486-491)；`mod = translateMod(SOME(Absyn.CLASSMOD(cmod,Absyn.NOMOD())), NOT_FINAL(), NOT_EACH(), NONE(), AbsynUtil.dummyInfo)`(:492) → `SCode.CLASS_EXTENDS(mod, SCode.PARTS(els,eqs,initeqs,als,initals,cos,{},decl))`(:497)。注意 clsattrs 固定 `{}`，且修饰的 info 是 dummyInfo。
- **der 类** → `SCode.PDER(path,vars)`(:499-504)。
- 失败分支报 INTERNAL_ERROR "AbsynToSCode.translateClassdef failed"(:506-510)。

### T4 元素收集与顺序
- `translateClassdefElements`(:587-620)：只看 `Absyn.PUBLIC`/`Absyn.PROTECTED` 两种 classPart，分别以 `SCode.PUBLIC()`/`SCode.PROTECTED()` 调 `translateEitemlist`(:602/:610)；`listAppend(es_1, els)`(:604/:612) → **元素保持源码顺序**；其它 part 跳过(:616-617)。
- `translateEitemlist`(:939-967)：只处理 `Absyn.ELEMENTITEM(element = e)`(:957-962)，其它（如 LEXER_COMMENT）丢弃(:963)；用 `List.append_reverse` + `listReverseInPlace` 保序(:961,:966)。
- **方程段顺序反转**：`translateClassdefEquations`(:622-646) 做 `eql_1 = translateEquations(eql,false); eqs = translateClassdefEquations(rest); eqs_1 = listAppend(eqs, eql_1);`(:635-637)——**后面的 equation 段排在前面**（段内顺序不变）；初始方程同(:661-663)。算法段则 `SCode.ALGORITHM(al_1) :: als`(:691) 保持源码顺序；约束段同(:722)。（探针 §4 已实证。）
- `translateClassdefAlgorithmitems`(:765-770)：`list(... for stmt guard AbsynUtil.isAlgorithmItem(stmt) in inStatements)` 过滤掉非语句项。
- `translateEquations`(:1395-1415)：只收 `Absyn.EQUATIONITEM()`(:1413 guard)，`EQUATIONITEMCOMMENT` 丢弃；每条先 `translateCommentWithLineInfoChanges(eq.comment, eq.info)`(:1410)。`inIsInitial` 只透传，不进入 SCode 节点——初始/普通由所在列表区分。

### T5 元素：translateElement(:1006-1041) → translateElementspec(:1080-1243)
- `Absyn.ELEMENT(constrainClass=cc, finalPrefix=f, innerOuter=io, redeclareKeywords=repl, specification=s, info)` → `translateElementspec(cc,f,io,repl,vis,s,info)`(:1029-1033)；`Absyn.DEFINEUNIT(name,args,info)` → `SCode.DEFINEUNIT(name,vis,exp,weight,info)`，exp 取 NAMEDARG "exp" 的 STRING，weight 取 "weight" 的 REAL(:1035-1039, :1043-1078)。
- **局部类**(:1154-1171)：`re_1 = translateRestriction(cl, re)`；`(de_1,cmt) = translateClassdef(de,i,re_1)`；`(_, redecl) = translateRedeclarekeywords(repl)`(:1159)；**replaceable 来自 `Absyn.CLASSDEF.replaceable_` 布尔**(:1154 `replaceable_ = rp`)，`sRep = if rp then REPLACEABLE(scc) else NOT_REPLACEABLE()`(:1163)，`scc = translateConstrainClass(cc)`(:1162) → `SCode.CLASS(n, PREFIXES(vis,sRed,sFin,io,sRep), sEnc, sPar, re_1, de_1, cmt, i)`(:1166-1169)。info 用**类自身的 `i`**（Absyn.CLASS.info），不是元素 info。`R_OPERATOR` 类走 `translateOperatorDef`(:1136-1151)。
- **extends**(:1173-1186)：`mod = translateMod(SOME(Absyn.CLASSMOD(args,Absyn.NOMOD())), NOT_FINAL(), NOT_EACH(), NONE(), AbsynUtil.dummyInfo)`(:1176/:1183) → `SCode.EXTENDS(path,vis,mod,ann,info)`；有注解时 `ann = translateAnnotation(absann)`(:1184)。**无任何同名 extends 合并**——每个 extends 子句一个 EXTENDS 元素。
- **组件列表拆分**(:1190-1230)：`for comp in inElementSpec4.components loop`(:1194)，每个 `Absyn.COMPONENTITEM(component=Absyn.COMPONENT(name=n, arrayDim=d, modification=m), comment, condition=cond)`(:1195) 各生成一个 `SCode.COMPONENT(n, prefixes, SCode.ATTR(tot_dim,ct,prl1,var1,di,isf), t, mod, cmt, cond, info)`(:1226)。要点：
  - 数组维 `tot_dim := listAppend(d, ad)`(:1205)：**组件名后的维在前，类型后的维在后**（`Real[3] x[2]` → `{2,3}`）。
  - `mod := translateMod(m, NOT_FINAL(), NOT_EACH(), NONE(), info)`(:1201)——组件级 `final` **不进 Mod**，进 `Prefixes.finalPrefix`(:1208,:1213)。
  - `prefixes := SCode.PREFIXES(vis,sRed,sFin,io,sRep)`(:1213)：vis 来自所在 public/protected 段；sRed/repl_1 来自 `translateRedeclarekeywords(repl)`(:1206)；io 直接放 Absyn.InnerOuter；`sRep := if repl_1 then REPLACEABLE(scc) else NOT_REPLACEABLE()`(:1211)。**constrainedby → `Prefixes.replaceablePrefix = REPLACEABLE(SOME(CONSTRAINCLASS(path, mod, cmt)))`**。
  - 条件组件 `cond` 原样进 `COMPONENT.condition`(:1195→:1226)。
  - 注释 `(cmt,info) := translateCommentWithLineInfoChanges(comment,info)`(:1207)；info 为**元素级 info**（同一声明的多个组件共享），除非 `__OpenModelica_FileInfo` 注解覆盖。
  - `flow/stream` → `translateConnectorType(fl, st)`(:1212, 定义 :370-388：两者同为 true 报 INTERNAL_ERROR)；`setHasInnerOuterDefinitionsHandler(io)`/`setHasStreamConnectorsHandler(st)` 设全局标志(:1199-1200)。
  - 方向 `INPUT_OUTPUT`（OM 扩展）拆成 `$in_<n>` 输入 + `<n>` 输出两个组件(:1219-1225)；标准 Modelica 不会触发。
  - 结果 `listReverseInPlace` 回源码顺序(:1229)。空组件列表 → `{}`(:1188)。
- **import**(:1231-1236) → `translateImports`(:1245-1269)：全限定路径 `.A.B` 去掉 FULLYQUALIFIED 包装(:1258-1263)；**组导入 `A.{x, y=z}` 拆成多个 IMPORT**(:1265-1266)：`GROUP_IMPORT_NAME(name)` → `SCode.IMPORT(Absyn.QUAL_IMPORT(prefix.name),vis,info)`(:1284-1287)，`GROUP_IMPORT_RENAME(rename,name)` → `SCode.IMPORT(Absyn.NAMED_IMPORT(rename,prefix.name),vis,info)`(:1288-1291)；其它形式 `Absyn.Import` **原样保留**在 `SCode.IMPORT.imp`(:1267)。
- `translateRedeclarekeywords`(:1329-1342)：`REDECLARE→(replaceable=false,redeclare=true)`，`REPLACEABLE→(true,false)`，`REDECLARE_REPLACEABLE→(true,true)`，NONE→(false,false)。
- `translateConstrainClass`(:1344-1368)：`Absyn.CONSTRAINCLASS(elementSpec=Absyn.EXTENDS(path, elementArg), comment)` → `SCode.CONSTRAINCLASS(cc_path, translateMod(...dummyInfo), translateComment(cmt))`(:1357-1364)。
- `translateAttributes`(:340-368)：`Absyn.ATTR(f, s, p, v, dir, fi, adim)` → `SCode.ATTR(listAppend(extraADim, adim), ct, sp, sv, dir, fi)`(:359-366)；`translateVariability`(:1382-1393)/`translateParallelism`(:1370-1380) 1:1。

### T6 修饰 Mod / SubMod
- `translateMod(inMod, finalPrefix, eachPrefix, comment, info, keepEmpty=false)`(:1686-1717)：`Absyn.CLASSMOD(elementArgLst, eqMod)` 取出 args 与 eqmod(:1701-1704)；`subs := translateArgs(args, keepEmpty)`(:1706)；绑定 `Absyn.EQMOD() then SOME(eqmod.exp)`(:1708-1711)——见 §5 关于 `:=` 的说明；**规范化：`({}, NONE(), NOT_FINAL(), NOT_EACH()) → SCode.NOMOD()`**(:1714)，否则 `SCode.MOD(finalPrefix, eachPrefix, subs, binding, comment, info)`(:1715)。所以 `final x;`（无绑定）也会是 `MOD(FINAL(),…,{},NONE())`，不是 NOMOD。
- `translateArgs`(:1719-1771)：
  - `Absyn.MODIFICATION` → `smod := translateMod(arg.modification, boolFinal(arg.finalPrefix), translateEach(arg.eachPrefix), arg.comment, arg.info)`(:1734-1735)；**空修饰被丢弃**（`if not SCodeUtil.isEmptyMod(smod) or keepEmpty`(:1737)），仅注解翻译时 keepEmpty=true(:985，注释 "Keep empty modifiers since they might have meaning in annotations, e.g. annotation(Dialog())")；然后 `translateSub(arg.path, smod, arg.info)`(:1738)。`each`/`final` 记在这一层 MOD 的 eachPrefix/finalPrefix。
  - `Absyn.REDECLARATION` → `elem::{} := translateElementspec(arg.constrainClass, arg.finalPrefix, NOT_INNER_OUTER(), SOME(arg.redeclareKeywords), SCode.PUBLIC(), arg.elementSpec, arg.info)`(:1746-1748，必须恰好一个元素) → `SCode.NAMEMOD(AbsynUtil.elementSpecName(arg.elementSpec), SCode.REDECL(boolFinal(arg.finalPrefix), translateEach(arg.eachPrefix), elem))`(:1750-1754)。redeclare 修饰里的元素 visibility 固定 PUBLIC。
  - `Absyn.ELEMENTARGCOMMENT` 丢弃(:1758)；`INHERITANCEBREAK`（`break x`）→ `NAMEMOD(name, BREAK_COMPONENT(info))`(:1760-1762)，`break connect(a,b)` → `NAMEMOD("", BREAK_CONNECT(cr1,cr2,info))`(:1764-1765)。
  - 顺序保持（`listReverse` :1770）。
- `translateSub`(:1773-1796)：`Absyn.IDENT(i)` → `NAMEMOD(i, mod)`(:1789)；`Absyn.QUALIFIED(i, path)` → `NAMEMOD(i, MOD(NOT_FINAL(), NOT_EACH(), {translateSub(path,mod)}, NONE(), NONE(), info))`(:1790-1794)——即 **`x.y = z` 规范化为 `x(y = z)`**，外层合成的 MOD 不带 final/each。
- `translateEach`(:1820-1828) 1:1。

### T7 方程（translateEquation :1534-1629）
- EQ_IF(:1554-1562)：`conditions := ifExp :: elseif 条件列表`，`thenBranch = body :: elseif 体列表`，else 单独 → **elseif 摊平成并行列表**，不是 SCode.mo:267-269 注释说的"嵌套 if"。
- EQ_WHEN_E(:1564-1571) → `EQ_WHEN(whenExp, body, [(elsewhen 条件, 体)…])`。
- EQ_EQUALS/EQ_PDE/EQ_CONNECT 1:1(:1573-1580)。
- EQ_FOR(:1582-1592)：**多迭代器 → 嵌套 EQ_FOR**，`for i in listReverse(iterators)` 使第一个迭代器在最外层(:1587-1590)；`translateIterator`(:1643-1657)：`Absyn.ITERATOR(name, guardExp, range)`，range 为 Option（隐式范围 → NONE），guard 非空报 INTERNAL_ERROR "For loops with guards not yet implemented"(:1653-1656)。
- **assert/terminate/reinit 识别**：仅匹配 `functionName = Absyn.CREF_IDENT(name = "assert")` 且位置参数恰为 2 个(:1595-1597，level 补常量 `ASSERTION_LEVEL_ERROR` = `.AssertionLevel.error`，定义 :69-70)、3 个(:1600-1602)、或 2 个 + 具名 `level=`(:1605-1608)；`terminate` 1 个参数(:1611-1613)；`reinit` 2 个参数(:1616-1619)。**其它一切**（含参数个数不对、限定名 `.assert`、`Modelica.xxx(...)`）→ `EQ_NORETCALL(Absyn.CALL(functionName, functionArgs, {}), …)`(:1624-1626)，注释："…wrong number of arguments is also turned into a noretcall, since it's preferable to handle the error during instantation"(:1621-1623)。

### T8 语句（translateClassdefAlgorithmItem :772-891）
- 先 `Absyn.ALGORITHMITEM(algorithm_, comment, info)` 拆出(:782)，`translateCommentWithLineInfoChanges`(:783)。
- ALG_ASSIGN(:794-796)：`assignComponent`/`value` 两个 Absyn.Exp **原样**——tuple 左值就是 `Absyn.TUPLE`，无特殊处理。
- ALG_IF(:798-804)：**保留 elseif 列表**（`translateAlgBranches` :893-906），与 EQ_IF 不同。
- ALG_FOR/ALG_PARFOR(:806-828)：多迭代器 → 嵌套，同 T7。ALG_WHILE(:830-834)。
- ALG_WHEN_A(:836-841)：`branches := translateAlgBranches((boolExpr, whenBody) :: elseWhenAlgorithmBranch)` → when 本体是 branches 第一项。
- assert/terminate/reinit(:844-865) 与 T7 同规则；其它 `ALG_NORETCALL` → `SCode.ALG_NORETCALL(Absyn.CALL(functionCall, functionArgs, {}))`(:867-871)。
- ALG_FAILURE(:873-877)/ALG_TRY(:879-884) MetaModelica；ALG_RETURN/BREAK/CONTINUE(:886-888)。

### T9 external 段
- `translateClassdefExternaldecls`(:908-937)：取**第一个** `Absyn.EXTERNAL(externalDecl = Absyn.EXTERNALDECL(fn_name, lang, output_, args, aann))` → `SCode.EXTERNALDECL(fn_name, lang, output_, args, translateAnnotationOpt(aann))`(:925-929)，五个字段直接搬运。
- `translateAlternativeExternalAnnotation`(:514-561)：external 无注解而函数类注解里有 `Library/Include/LibraryDirectory/SourceDirectory/License`（白名单 :528-540）→ 过滤后复制进 ExternalDecl.annotation_(:547-554)，语言标准 ≥3.3 时报 `MISPLACED_EXTERNAL_ANNOTATION` 警告(:551-553)。

### T10 注释、注解、SourceInfo
- `translateComment`(:1459-1477)：NONE → `SCode.noComment`(:1470)；`Absyn.COMMENT(absann, ostr)` → `SCode.COMMENT(translateAnnotationOpt(absann), Util.applyOption(ostr, System.unescapedString))`(:1471-1475)——**字符串注释被反转义**。
- `translateAnnotation`(:970-991)：`elementArgs = {}` → NONE(:980)；否则 `translateMod(SOME(CLASSMOD(args,NOMOD())), NOT_FINAL(), NOT_EACH(), NONE(), AbsynUtil.dummyInfo, keepEmpty=true)`(:985)，空则 NONE 否则 `SOME(SCode.ANNOTATION(m))`(:988)。**注解修饰的 info 是 dummyInfo。**
- `translateCommentList`(:1479-1505)：类体上的多条 `annotation(...)` 用 `AbsynUtil.mergeAnnotationsList` 合并成一条(:1498-1503)——这是本文件中唯一的"合并"。
- `translateCommentWithLineInfoChanges`(:1417-1426) → `getInfoAnnotationOrDefault`(:1428-1457)：注解 `__OpenModelica_FileInfo=("file",line)` 可覆盖 SourceInfo(:1453-1454)。
- SourceInfo 携带位置：CLASS.info、COMPONENT.info、EXTENDS.info、IMPORT.info、DEFINEUNIT.info、每条 Equation/Statement 的 info、MOD.info（组件修饰用元素 info；extends/class-extends/constrainedby/注解 用 dummyInfo）。

## dump 格式与探针原文

### 入口：只有脚本 API `list(..., exportKind=ExportKind.SCode)`，没有 `-d` 标志
- `grep -i scode Util/Flags.mo` 只命中 `SCODE_INST`("newInst", Flags.mo:300) 与 `DO_SCODE_DEP`("scodeDep", :380)，**没有 dumpSCode 之类的调试标志**。
- 脚本 API（FrontEnd/ModelicaBuiltin.mo）：`type ExportKind = enumeration(Absyn, SCode, MetaModelicaInterface, Internal)`(:2492)；`function list "Lists the contents of the given class, or all loaded classes."`(:2494)，参数 `input ExportKind exportKind = ExportKind.Absyn;`(:2498)。
- 实现 `CevalScript.listClass`(Script/CevalScript.mo:3088)：`AllLoadedClasses` → `SCodeDump.programStr(SymbolTable.getSCode())`(:3118)；指定类 → `cl := FBuiltin.getElementWithPathCheckBuiltin(scodeP, className)`(:3134) 后 `SCodeDump.unparseElementStr(cl)`(:3138)。`MetaModelicaInterface` 用 `OPTIONS(true,false,true,true,true,true,true,true,true)`(:3099-3100) 剥掉算法段/protected/注释/external。
- `SCodeDump.programStr(prog, options=defaultOptions)`(FrontEnd/SCodeDump.mo:85-91) = `Tpl.tplString2(SCodeDumpTpl.dumpProgram, …)`；`unparseElementStr`(:216-223) = `SCodeDumpTpl.dumpElement`。`OPTIONS` 九个字段(:55-65)：stripAlgorithmSections / stripProtectedImports / stripProtectedClasses / stripProtectedComponents / stripMetaRecords / stripGraphicalAnnotations / stripStringComments / stripExternalDecl / stripOutputBindings；`defaultOptions = OPTIONS(false,false,false,false,true,true,false,false,false)`(:68)。另有 `saveTotalModel`/`getTotalModel` 也走 `SCodeDump.programStr`(Script/CevalScriptBackend.mo:7847, :7885)。
- 拿法：`omc probe.mos`，其中 `print(list(ProbeR9, exportKind=ExportKind.SCode))`（见下方探针）。

### 格式要点（Template/SCodeDumpTpl.tpl）——输出是"可再解析的 Modelica 文本"，不是树形 dump
- 元素序列 `dumpElements3`(:55-85)：每元素一行 `<el>;`；可见性切换时插入独立一行 `public`/`protected`（`dumpSectionVisibility` :127-135）；PARTS 类前后加空行(:95-100)。
- 类 `dumpClass`(:164-182)：`<redeclare><final><inner/outer><replaceable><encapsulated><partial><restriction> <name>"cmt"` + 体 + `end <name> constrainedby …`（header :184-193，footer :240-259）；类注解放在 `end` 前一行 ` annotation(...);`(:247-253)。class extends 头为 `… extends <name><mod>`(:187-190)。DERIVED → ` = <attrs><typespec><mod>"cmt"<ann><constrainedby>`(:221-225,:243)；ENUMERATION → ` = enumeration(a "cmt", b)`，空表 → `enumeration(:)`(:226-231)；PDER → ` = der(f, x)`(:232-234)。
- **PARTS 体的打印顺序 `dumpClassDef`(:198-214)：元素 → initial equation → initial algorithm → equation → algorithm → external**；所有 normalEquationLst 合并在一个 `equation` 标签下(`dumpEquations` :316-323)，多个算法段共用一个 `algorithm` 标签(:456-464)。
- 组件 `dumpComponent`(:273-293)：`<redeclare><each><final><inner|outer><replaceable><parglobal|parlocal><discrete|parameter|constant><input|output><flow|stream><type> <name><dims><mod> if <cond> constrainedby … "cmt" annotation(...)`(:292；前缀顺序 `dumpPrefixes` :622-631，属性顺序 `dumpAttributes` :819-828)。类与组件的 **`protected` 不作为元素前缀打印**（`dumpPrefixes` :622-631 不含 visibility；`dumpVisibility` :633-637 只被 import :141 / extends :158 / defineunit :299 调用），而是靠段标签。
- 修饰 `dumpModifier`(:739-747)：`(<sub>, <sub>) = <binding>`；子修饰 `<each><final><ident><mod>`(:784-791)；REDECL 子修饰直接打印元素 `redeclare [each] …`(:772-778)；NOMOD/BREAK_* 无 case → 空串。
- 方程 `dumpEquation`(:325-361)：`assert(c, m)` 当 level 为 `AssertionLevel.error`（限定或非限定）时**省略第三参**(`dumpAssertionLevel` :447-454)；EQ_IF 用 `elseif` 重建(:363-401)；EQ_FOR 无 range → `for i loop`(:415-422)；EQ_WHEN/elsewhen(:425-445)；EQ_NORETCALL 直接打印表达式(:356-359)。**EQ_PDE 无 case → errorMsg**(:360)。
- 语句 `dumpStatement`(:474-516)：ALG_IF **总是打印 `else`**（即使 elseBranch 空，:531-532）；ALG_ASSERT/TERMINATE/REINIT 不打印 comment(:486-497)。
- 注解 `dumpAnnotationSubModifier`(:793-817)：`Config.showAnnotations()` 为 false 时 `choices/Documentation/Dialog/Diagram/Icon/Line/Placement/preferredView/conversion/defaultComponentName/revisionId/uses` 子修饰被**省略**(:800-813)，所以默认 dump 看不到 Documentation。字符串注释重新转义 `System.escapedString(cmt,false)`(:919)。external → `external "C" y = foo(x) annotation(...);`(:887-901)。

### 探针：probe-r9/probe.mo → `omc probe.mos`（`list(ProbeR9, exportKind=OpenModelica.Scripting.ExportKind.SCode)`）
源码里故意写的形态 → dump 结果（原文见下，完整输出在 `probe-r9/probe_out.txt`）：
- `Real a, b(start = 1, fixed = true) "b comment";` → 两行 `Real a;` / `Real b(start = 1, fixed = true) "b comment";`；`Real c[2], d[3] annotation(HideResult = true);` → 注解只在 d 上。
- `Real[2] m2[3];` → `Real m2[3, 2];`（组件维在前）。`Real q(y.start = 5);` → `Real q(y(start = 5));`；`b.start = 3` → `b(start = 3)`。
- `import Modelica.Constants.{pi, e2 = e};` → `import Modelica.Constants.pi;` + `import e2 = Modelica.Constants.e;`。
- `assert(a > -3, "named level", level = AssertionLevel.warning)` → 具名变位置参；`assert(a > -2, "a too small")` 不补打 level。
- `for i in 1:2, j in 1:3 loop` → 两层嵌套；`for k2 loop` 保持无范围。
- 第一段 `equation … connect(p, rb.p);` 之后又写了 `equation p.v = 0;` → dump 中 `p.v = 0;` 排在 `a = sin(time);` **之前**（段序反转实证）；`initial equation` 被提到 `equation` 前（dump 顺序）。
- `extends Base(k = 2) annotation(Documentation(info = "ext"))` 与 Comp 的 `annotation(Documentation(...), Icon(...))` → 都不打印（Documentation/Icon 在省略名单）。
- `replaceable model M = Base constrainedby Base(k = 3) "M comment";` → `"M comment"` 消失（进了 ConstrainClass.comment）。
```
package ProbeR9 "probe for SCode dump"
  import Modelica.Math.sin;
  import SI = Modelica.Units.SI;
  import Modelica.Constants.pi;
  import e2 = Modelica.Constants.e;
  import Modelica.Math.*;
  connector Pin … end Pin;                 （原样：Real v; flow Real i;）
  type Angle = Real(unit = "rad", displayUnit = "deg") "angle";
  type Color = enumeration(red "R comment", green, blue) "colors";
  partial model Base … end Base;
  model Comp
    extends Base(k = 2);
    Real a;
    Real b(start = 1, fixed = true) "b comment";
    Real c[2];
    Real d[3] annotation(HideResult = true);
    Real m2[3, 2];
    replaceable model M = Base constrainedby Base(k = 3);
    replaceable Base rb constrainedby Base(k = 4);
    Boolean useX = true;
    Real x if useX;
    final parameter Integer n = 3;
    inner Real g = 9.81;
    Color col = Color.red;
    Real q(y(start = 5));
    final Real fq;
    Real sub(start = 1) = 2;
  protected
    Real z;
    discrete Real w;
  initial equation
    z = 0;
  equation
    p.v = 0;
    a = sin(time);
    b = a + k;
    assert(a > -2, "a too small");
    assert(b > -2, "b too small", AssertionLevel.warning);
    assert(a > -3, "named level", AssertionLevel.warning);
    Modelica.Utilities.Streams.print("hi");
    when a > 0.5 then
      reinit(g, 0);
    elsewhen a < -0.5 then
      w = 1;
    end when;
    if useX then
      x = 1;
    elseif n > 2 then
      x = 2;
    else
      x = 3;
    end if;
    for i in 1:2 loop
      for j in 1:3 loop
        m2[i, j] = 0;
      end for;
    end for;
    for k2 loop
      d[k2] = 0;
    end for;
    connect(p, rb.p);
  algorithm
    (a, b) := two(1.0);
    while a < 0 loop
      a := a + 1;
    end while;
    when b > 1 then
      w := 2;
    elsewhen b < 0 then
      w := 3;
    end when;
    if a > 1 then … elseif a > 0 then … else … end if;   （原样三行体，略）
    assert(a > 0, "alg assert");
    terminate("done");
    reinit(g, 1);
    Modelica.Utilities.Streams.print("alg");
  end Comp;
  model Sys
    Comp comp(redeclare model M = Base, a(start = 0), b(start = 3), each c = {1, 2}, final n = 4);
    Comp comp2(redeclare replaceable model M = Base(k = 5) constrainedby Base, redeclare final Base rb(k = 6));
  end Sys;
  package Lib  replaceable model Ext … end Ext; end Lib;
  package P2
    extends Lib;
    model extends Ext(k = 7)
      Real q2;
    end Ext;
  end P2;
  function f
    input Real x;
    output Real y;
    external "C" y = foo(x) annotation(Library = "bar");
  end f;
  function f2 … external "C"; end f2;     function two … end two;
  function df = der(f, x);
end ProbeR9
```
mini4（`loadString`）另证：`replaceable model M2 = Base "M2 comment";`（无 constrainedby）的注释保留；`Real a, b "ab comment" annotation(x=1);` → `Real a;` + `Real b "ab comment" annotation(x = 1);`。

## 未找到或待验证

- **修饰里的 `:=`**：`Absyn.EqMod` 只有 `NOMOD` / `EQMOD(exp, info)`（Absyn.mo:588-595），SCode.MOD.binding 因此无法区分 `=`/`:=`；OMC 解析器对 `:=` 修饰的处理未查（OMParser 不在本次范围）。
- `AbsynUtil.mergeAnnotationsList`（AbsynUtil.mo:4078-4084）的合并语义（同名子修饰谁覆盖谁）未读；`AbsynUtil.isExternalPart` 未读。
- 行号来自源码 HEAD（3ceb0c9d06），探针跑的是安装版 1.25.0 二进制：两者 AbsynToSCode.mo 是否有差异未比对；工作区版 omc 未跑。
- 未探针：`defineunit`、`EQ_PDE`（dump 模板无 case，SCodeDumpTpl.tpl:360）、`__OpenModelica_FileInfo` 覆盖 info（AbsynToSCode.mo:1453-1454）、`break` 修饰（:1760-1765）、`enumeration(:)`、`class extends` 上的 `annotation`。
- 安装版里非限定 `ExportKind.SCode` 失败而 `OpenModelica.Scripting.ExportKind.SCode` 成功的原因未查（脚本环境枚举查找问题，与 SCode 无关）。
- `translateElementAddinfo`（:1659-1683）在本文件内无调用点，其它文件未 grep。
- OM/MetaModelica 专有、ymc 无需实现的构造：`R_METARECORD/R_UNIONTYPE`、`FR_PARALLEL/KERNEL_FUNCTION`、`Parallelism`、`IsField`、`ALG_PARFOR/ALG_FAILURE/ALG_TRY`、`BREAK_COMPONENT/BREAK_CONNECT`、`INPUT_OUTPUT` 拆 `$in_`、typeVars/`makeTypeVarElement`（:1798-1818）、`R_PREDEFINED_CLOCK`、`R_OPTIMIZATION`/`ConstraintSection`/`clsattrs`。

## 对 ymc S2 数据结构的建议

原则：名字对齐 OMC 便于对照 dump；表达式/TypeSpec/Path/Import/Subscript **引用 S1 的 absyn 节点**（`const Exp*` 或 arena 索引）而不复制；布尔前缀用 `bool`/`enum class` 而不是两值 variant；砍掉上面列的 OM/MetaModelica 扩展。
```cpp
// src/scode/scode.h 草图（注释里是对应的 OMC 名字）
namespace ymc::scode {
using absyn::Exp; using absyn::Path; using absyn::TypeSpec; using absyn::ComponentRef; using absyn::Import; using absyn::Subscript;
enum class Visibility { Public, Protected };                      // SCode.Visibility
enum class Variability { Var, Discrete, Param, Const };            // SCode.Variability
enum class ConnectorType { Potential, Flow, Stream };              // SCode.ConnectorType
enum class Direction { Bidir, Input, Output };                     // Absyn.Direction（无 INPUT_OUTPUT）
enum class InnerOuter { None, Inner, Outer, InnerOuter };          // Absyn.InnerOuter
struct Element; struct Mod;
struct SubMod { std::string ident; std::unique_ptr<Mod> mod; };    // SCode.NAMEMOD（唯一形态；x.y=z 已展成嵌套）
struct Mod {                                                       // SCode.Mod：NOMOD / MOD / REDECL
  struct Plain  { bool isFinal=false, isEach=false; std::vector<SubMod> subs; const Exp* binding=nullptr;
                  std::optional<std::string> comment; SourceInfo info; };
  struct Redecl { bool isFinal=false, isEach=false; std::unique_ptr<Element> element; };
  std::variant<std::monostate, Plain, Redecl> v;                   // monostate == NOMOD（空修饰规范化）
};
struct Annotation { Mod modification; };                           // SCode.ANNOTATION
struct Comment { std::optional<Annotation> ann; std::optional<std::string> str; };   // SCode.COMMENT
struct ConstrainClass { Path path; Mod mod; Comment cmt; };        // SCode.CONSTRAINCLASS
struct Prefixes { Visibility vis; bool isRedeclare=false, isFinal=false; InnerOuter io; bool isReplaceable=false;
                  std::optional<ConstrainClass> cc; };             // SCode.PREFIXES + Replaceable.cc
struct Attributes { std::vector<const Subscript*> dims; ConnectorType ct; Variability var; Direction dir; };  // SCode.ATTR
enum class Restriction { Class, Model, Record, OperatorRecord, Block, Connector, ExpandableConnector, Operator, Type, Package,
                         Function, ExternalFunction, OperatorFunction, Enumeration, PredefInteger, PredefReal, PredefString,
                         PredefBoolean, PredefEnum };              // SCode.Restriction + FunctionRestriction 摊平；purity 另存 bool
struct Equation; struct Statement;
struct EqIf      { std::vector<const Exp*> conds; std::vector<std::vector<Equation>> thens; std::vector<Equation> else_; };  // EQ_IF（摊平 elseif）
struct EqWhen    { const Exp* cond; std::vector<Equation> body; std::vector<std::pair<const Exp*, std::vector<Equation>>> elsewhens; };
struct EqFor     { std::string index; const Exp* range; std::vector<Equation> body; };   // range==nullptr 即 NONE
struct EqEquals  { const Exp* lhs; const Exp* rhs; };
struct EqConnect { const ComponentRef* lhs; const ComponentRef* rhs; };
struct EqAssert  { const Exp* cond; const Exp* msg; const Exp* level; };                // level 缺省时指向共享的 AssertionLevel.error 节点
struct EqTerminate { const Exp* msg; };  struct EqReinit { const Exp* cref; const Exp* value; };  struct EqNoRetCall { const Exp* call; };
struct Equation { std::variant<EqIf, EqEquals, EqConnect, EqFor, EqWhen, EqAssert, EqTerminate, EqReinit, EqNoRetCall> v;
                  Comment cmt; SourceInfo info; };
// Statement 同构：Assign{lhs,rhs} If{cond,then,elseifs,else} For While When{branches} Assert Terminate Reinit NoRetCall Return Break
struct AlgorithmSection { std::vector<Statement> stmts; };         // SCode.ALGORITHM
struct ExternalDecl { std::optional<std::string> funcName, lang; const ComponentRef* output=nullptr;
                      std::vector<const Exp*> args; std::optional<Annotation> ann; };   // SCode.EXTERNALDECL
struct Parts { std::vector<Element> elements; std::vector<Equation> equations, initialEquations;
               std::vector<AlgorithmSection> algorithms, initialAlgorithms; std::optional<ExternalDecl> external; };  // SCode.PARTS
struct Derived { const TypeSpec* type; Mod mod; Attributes attr; };                     // SCode.DERIVED
struct EnumItem { std::string literal; Comment cmt; };  struct Enumeration { std::vector<EnumItem> items; };  // 空 == enumeration(:)
struct ClassExtends { Mod mod; Parts parts; };  struct Pder { Path fn; std::vector<std::string> vars; };
struct Class { std::string name; Prefixes prefixes; bool isEncapsulated=false, isPartial=false; Restriction restriction; bool isImpure=false;
               std::variant<Parts, ClassExtends, Derived, Enumeration, Pder> def; Comment cmt; SourceInfo info; };   // SCode.CLASS
struct Component { std::string name; Prefixes prefixes; Attributes attr; const TypeSpec* type; Mod mod; Comment cmt;
                   const Exp* condition=nullptr; SourceInfo info; };                                                  // SCode.COMPONENT
struct Extends { Path base; Visibility vis; Mod mod; std::optional<Annotation> ann; SourceInfo info; };               // SCode.EXTENDS
struct ImportEl { const Import* imp; Visibility vis; SourceInfo info; };                                             // SCode.IMPORT
struct Element { std::variant<Class, Component, Extends, ImportEl> v; };
using Program = std::vector<Element>;
}
```
可以比 OMC 做得更好的四点（都不影响与 OMC dump 对照）：(a) 方程段按源码顺序拼接，不要复刻 :635-637 的反转（对照工具按"段合并后集合相等"比较即可）；(b) extends/constrainedby 的 Mod 记真实 SourceInfo 而非 dummyInfo；(c) `Mod::Plain` 加 `bool isAssign`（`:=`）以便报错；(d) ymc 的 `scode-dump` 直接照 SCodeDumpTpl.tpl 的文本格式输出（同样的元素顺序、`protected` 段标签、`assert` 省略 error level、Documentation 等注解省略），这样 `tools/scode_compare.py` 可以和 `list(..., exportKind=OpenModelica.Scripting.ExportKind.SCode)` 逐行 diff。
