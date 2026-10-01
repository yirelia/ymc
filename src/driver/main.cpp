// ymc 命令行入口（基建，随阶段增长）。
//
// S1：
//   ymc <file.mo>...               等价 --dump-after=parse
//   ymc --dump-after=parse <f>...  解析后反解析（unparse）打印到 stdout
//   ymc --check-syntax <f>...      只检查语法，打印诊断；有错退出码非 0
//   ymc --dump-tokens <f>          打印 token 流（调试）
//   ymc --log=parse ...            解析器主要规则入口打印 "[parse] rule @line:col"（调试）
//   ymc --std=3.2 ...              语言版本（目前只影响 pure/impure 是否为保留字）
//   ymc --files-from=<list> ...    从文件读路径列表（每行一个）——单进程批量解析，用于基准测试
// S2：
//   ymc --load-model=P [--version=V] [--modelica-path=a;b] [--dump-class-names]
//       按 MODELICAPATH 装载库 P（及其 uses 依赖）；--dump-class-names 打印
//       "# top: <已装载顶层>" 一行，然后 P 与其全部子孙类的全限定名（先序，含 protected），
//       与 OMC getClassNames(P, recursive=true, qualified=true, showProtected=true) 同序。
//   ymc --dump-after=scode [--class=Q.N] (<file.mo> | --load-model=P ...)
//       Absyn → SCode 后按 OMC list(X, exportKind=SCode) 的格式打印；--class 只打印该类（全限定名）。
#include <cstdio>
#include <cstdlib>
#include <cstring>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "absyn/absyn.h"
#include "absyn/unparse.h"
#include "base/diag.h"
#include "base/symbol.h"
#include "parse/lexer.h"
#include "parse/parser.h"
#include "flat/flat_dump.h"
#include "flat/flatten.h"
#include "inst/inst.h"
#include "scode/absyn_to_scode.h"
#include "scode/loader.h"
#include "scode/scode_dump.h"

namespace {

struct Options {
  const char* mode = "dump-after=flat";   // S3：默认压平（同 omc X.mo）；--dump-after=parse 回到 S1 反解析
  double std_version = 3.6;
  bool trace_parse = false;
  std::vector<std::string> paths;
  // S2
  std::string load_model;
  std::string version;
  std::string modelica_path;   // 空 = 默认根列表
  bool dump_class_names = false;
  std::string dump_class;      // --class=Q.N（配合 --dump-after=scode）
};

void print_help() {
  std::puts(
      "ymc - self-made Modelica compiler (S2)\n"
      "usage:\n"
      "  ymc <file.mo>...               instantiate + flatten last class (= omc X.mo)\n"
      "  ymc --dump-after=parse <f>...  parse and unparse (S1)\n"
      "  ymc --dump-after=flat <f>...   instantiate + flatten (S3, default)\n"
      "  ymc --check-syntax <f>...      syntax check only\n"
      "  ymc --dump-tokens <f>          print token stream\n"
      "  ymc --log=parse                trace parser rule entries to stderr\n"
      "  ymc --std=<ver>                language version (default 3.6)\n"
      "  ymc --files-from=<list>        read file paths (one per line)\n"
      "  ymc --load-model=P [--version=V] [--modelica-path=a;b] [--dump-class-names]\n"
      "                                 load library P via MODELICAPATH (S2)\n"
      "  ymc --help | --version");
}

bool read_file(const std::string& path, std::string& out) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return false;
  std::ostringstream ss;
  ss << f.rdbuf();
  out = ss.str();
  return true;
}

// 在 SCode program 里按全限定名 "A.B.C" 找类元素；找不到返回 invalid。
ymc::Id<ymc::SElement> find_scode_class(const ymc::SCode& s, const std::string& qname) {
  const std::vector<ymc::Id<ymc::SElement>>* level = &s.program;
  ymc::Id<ymc::SElement> found;
  std::size_t b = 0;
  while (b <= qname.size()) {
    std::size_t e = qname.find('.', b);
    if (e == std::string::npos) e = qname.size();
    std::string_view part(qname.data() + b, e - b);
    found = ymc::Id<ymc::SElement>{};
    for (ymc::Id<ymc::SElement> id : *level) {
      const ymc::SElement& el = s.elems[id];
      if (el.kind == ymc::SElementKind::Class && s.absyn.interner.str(el.name) == part) {
        found = id;
        break;
      }
    }
    if (!found.is_valid()) return found;
    level = &s.elems[found].def.elements;
    b = e + 1;
  }
  return found;
}

// Absyn → SCode → 文本；--class 给定时只打印那个类。返回退出码。
int dump_scode(ymc::Absyn& absyn, const std::string& dump_class) {
  ymc::SCode scode(absyn);
  ymc::translate_program(scode, absyn.top);
  std::string text;
  if (dump_class.empty()) {
    text = ymc::dump_scode_program(scode);
  } else {
    ymc::Id<ymc::SElement> cls = find_scode_class(scode, dump_class);
    if (!cls.is_valid()) {
      std::fprintf(stderr, "Error: class not found: %s\n", dump_class.c_str());
      return 1;
    }
    text = ymc::dump_scode_element(scode, cls);
  }
  std::fputs(text.c_str(), stdout);
  return 0;
}

// S3：实例化 + 压平 + dump（默认 `ymc X.mo`，对齐 `omc X.mo`：压平文件里最后一个顶层类）。
// v1 未实现的构造 → 退出码 66 + `ymc: not implemented: <原因>`（运行器据此记 SKIP）。
int dump_flat_pipeline(ymc::Absyn& absyn, ymc::Diagnostics& diags, ymc::Interner& interner) {
  ymc::SCode scode(absyn);
  ymc::translate_program(scode, absyn.top);
  if (scode.program.empty()) {
    std::fprintf(stderr, "ymc: no class to instantiate\n");
    return 1;
  }
  ymc::Inst inst(interner, absyn, scode);
  ymc::Instantiator instr(inst, diags);
  instr.make_top_node();
  ymc::Symbol root_name = scode.elems[scode.program.back()].name;   // 最后一个顶层类（同 omc）
  ymc::NodeId root = instr.instantiate_root(root_name);
  // v1 未实现的构造先于错误判断（内建枚举/extends 内建等在实例化阶段就标 unsupported）。
  if (instr.unsupported()) {
    std::fprintf(stderr, "ymc: not implemented: %s\n", instr.unsupported_reason().c_str());
    return 66;
  }
  diags.print(stderr, "");
  if (!root.is_valid() || inst.nodes[root].is_empty() || diags.has_errors()) return 1;
  ymc::FlattenResult fr = ymc::flatten(inst, interner, root);
  if (fr.unsupported) {
    std::fprintf(stderr, "ymc: not implemented: %s\n", fr.unsupported_reason.c_str());
    return 66;
  }
  std::string text = ymc::dump_flat(fr.model, interner);
  std::fputs(text.c_str(), stdout);
  return diags.has_errors() ? 1 : 0;
}

// 处理一个文件；返回退出码：0 = 无错误。
int run_one(const Options& opt, const std::string& path) {
  std::string src;
  if (!read_file(path, src)) {
    std::fprintf(stderr, "Error: cannot open file: %s\n", path.c_str());
    return 2;
  }
  ymc::Interner interner;
  ymc::Diagnostics diags;
  ymc::Lexer lexer(src, /*file=*/0, interner, diags, /*pure_impure_keywords=*/opt.std_version >= 3.3);
  std::vector<ymc::Token> toks = lexer.tokenize();

  if (std::strcmp(opt.mode, "dump-tokens") == 0) {
    for (const ymc::Token& t : toks) {
      const char* name = ymc::token_kind_name(t.kind);
      std::string text = t.text.is_valid() ? std::string(interner.str(t.text)) : "";
      std::printf("%u:%u\t%s\t%s\n", t.loc.line, t.loc.col, name, text.c_str());
    }
    diags.print(stderr, path.c_str());
    return diags.has_errors() ? 1 : 0;
  }

  ymc::Absyn absyn(interner);
  ymc::Parser parser(toks, absyn, diags);
  parser.set_trace(opt.trace_parse);
  parser.parse_program();

  diags.print(stderr, path.c_str());

  if (std::strcmp(opt.mode, "check-syntax") == 0) {
    if (diags.has_errors()) {
      std::fprintf(stderr, "%zu error(s)\n", diags.error_count());
      return 1;
    }
    if (opt.paths.size() == 1) std::puts("OK");
    return 0;
  }
  if (std::strcmp(opt.mode, "dump-after=scode") == 0) {
    int rc = dump_scode(absyn, opt.dump_class);
    return rc != 0 ? rc : (diags.has_errors() ? 1 : 0);
  }
  if (std::strcmp(opt.mode, "dump-after=flat") == 0) {
    if (diags.has_errors()) return 1;   // 解析出错先退（不进实例化）
    return dump_flat_pipeline(absyn, diags, interner);
  }
  std::string text = ymc::unparse(absyn);
  std::fputs(text.c_str(), stdout);
  return diags.has_errors() ? 1 : 0;
}

// S2：装载库并（可选）打印类名。
int run_load_model(const Options& opt) {
  ymc::Interner interner;
  ymc::Diagnostics diags;
  ymc::Absyn absyn(interner);
  ymc::LoaderOptions lopt;
  lopt.roots = opt.modelica_path.empty() ? ymc::Loader::default_roots()
                                         : ymc::Loader::split_modelica_path(opt.modelica_path);
  lopt.pure_impure_keywords = opt.std_version >= 3.3;
  ymc::Loader loader(absyn, diags, lopt);
  ymc::Id<ymc::ClassDef> root = loader.load_library(opt.load_model, opt.version);
  diags.print(stderr, "");
  if (!root.is_valid()) {
    std::fprintf(stderr, "Failed to load %s\n", opt.load_model.c_str());
    return 1;
  }
  if (std::strcmp(opt.mode, "dump-after=scode") == 0) {
    int rc = dump_scode(absyn, opt.dump_class);
    return rc != 0 ? rc : (diags.has_errors() ? 1 : 0);
  }
  if (opt.dump_class_names) {
    // OMC getClassNames() 的顶层顺序是"装载顺序的逆序"（每装一个库就前插；探针 2026-09-16：
    // loadModel(Buildings) → {ModelicaServices, Complex, Modelica, Buildings}），这里照此输出
    std::string top = "# top:";
    const std::vector<ymc::LoadedLibrary>& ls = loader.loaded();
    for (std::size_t i = ls.size(); i-- > 0;) top += " " + ls[i].name;
    std::puts(top.c_str());
    std::string root_name(interner.str(absyn.classes[root].name));
    std::puts(root_name.c_str());  // OMC 的递归列表第一个是 root 自身
    std::vector<std::string> names;
    ymc::collect_class_names(absyn, root, root_name + ".", /*include_protected=*/true, names);
    for (const std::string& n : names) std::puts(n.c_str());
  } else {
    std::fprintf(stderr, "loaded %zu librar%s: ", loader.loaded().size(),
                 loader.loaded().size() == 1 ? "y" : "ies");
    for (const ymc::LoadedLibrary& l : loader.loaded())
      std::fprintf(stderr, "%s%s ", l.name.c_str(),
                   l.has_version ? (" " + l.version.raw).c_str() : "");
    std::fputc('\n', stderr);
  }
  return diags.has_errors() ? 1 : 0;
}

}  // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
  // Windows 文本模式会把 '\n' 翻成 '\r\n'，与 OMC 的 LF 输出逐字节不同（对拍失败）。
  // 把 stdout 设为二进制，让 dump 的 '\n' 原样落地。ponytail: 只此一处，别处不需要。
  _setmode(_fileno(stdout), _O_BINARY);
#endif
  if (argc >= 2 && std::strcmp(argv[1], "--version") == 0) {
    std::puts("ymc 0.2.0 (S2)");
    return 0;
  }
  if (argc >= 2 && std::strcmp(argv[1], "--help") == 0) {
    print_help();
    return 0;
  }
  Options opt;
  for (int i = 1; i < argc; ++i) {
    const char* a = argv[i];
    if (std::strcmp(a, "--dump-after=parse") == 0) opt.mode = "dump-after=parse";
    else if (std::strcmp(a, "--dump-after=flat") == 0) opt.mode = "dump-after=flat";
    else if (std::strcmp(a, "--dump-after=scode") == 0) opt.mode = "dump-after=scode";
    else if (std::strncmp(a, "--class=", 8) == 0) opt.dump_class = a + 8;
    else if (std::strcmp(a, "--check-syntax") == 0) opt.mode = "check-syntax";
    else if (std::strcmp(a, "--dump-tokens") == 0) opt.mode = "dump-tokens";
    else if (std::strcmp(a, "--log=parse") == 0) opt.trace_parse = true;
    else if (std::strcmp(a, "--dump-class-names") == 0) opt.dump_class_names = true;
    else if (std::strncmp(a, "--std=", 6) == 0) opt.std_version = std::atof(a + 6);
    else if (std::strncmp(a, "--load-model=", 13) == 0) opt.load_model = a + 13;
    else if (std::strncmp(a, "--version=", 10) == 0) opt.version = a + 10;
    else if (std::strncmp(a, "--modelica-path=", 16) == 0) opt.modelica_path = a + 16;
    else if (std::strncmp(a, "--files-from=", 13) == 0) {
      std::ifstream lst(a + 13);
      if (!lst) {
        std::fprintf(stderr, "Error: cannot open list file: %s\n", a + 13);
        return 2;
      }
      std::string line;
      while (std::getline(lst, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
        if (!line.empty()) opt.paths.push_back(line);
      }
    } else if (a[0] != '-' && a[0] != '+') opt.paths.push_back(a);
    else if (std::strcmp(a, "-d=newInst") == 0) {
      // omc 的新前端开关：ymc 只有新前端语义，无操作。
    } else if (a[0] == '+' || std::strncmp(a, "-i=", 3) == 0 || std::strncmp(a, "-d=", 3) == 0 ||
               std::strncmp(a, "-g=", 3) == 0 || std::strcmp(a, "--newBackend") == 0 ||
               std::strcmp(a, "--unitChecking") == 0 || std::strncmp(a, "--allowNonStandardModelica=", 27) == 0) {
      // testsuite `// cflags:` 透传来的 omc 选项（tests/omc/cflags_map.json）：ymc 未实现 → 按未实现契约退 66（SKIP），
      // 不当用法错误（此前退 2 被记成 FAIL：EncapsulatingInst1 / Prefix3 的 `-i=`）。
      std::fprintf(stdout, "ymc: not implemented: option %s\n", a);
      return 66;
    } else {
      std::fprintf(stderr, "Error: unknown option: %s\n", a);
      return 2;
    }
  }
  if (!opt.load_model.empty()) return run_load_model(opt);
  if (opt.paths.empty()) {
    print_help();
    return 0;
  }
  int worst = 0;
  std::size_t failed = 0;
  for (const std::string& p : opt.paths) {
    int rc = run_one(opt, p);
    if (rc != 0) ++failed;
    if (rc > worst) worst = rc;
  }
  if (opt.paths.size() > 1 && std::strcmp(opt.mode, "check-syntax") == 0)
    std::fprintf(stderr, "%zu file(s), %zu with errors\n", opt.paths.size(), failed);
  return worst;
}
