// ymc 命令行入口（基建，随阶段增长）。
//
// S1 起支持：
//   ymc <file.mo>                 等价 --dump-after=parse
//   ymc --dump-after=parse <f>    解析后反解析（unparse）打印到 stdout
//   ymc --check-syntax <f>        只检查语法，打印诊断；有错退出码非 0
//   ymc --dump-tokens <f>         打印 token 流（调试）
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

#include "absyn/absyn.h"
#include "absyn/unparse.h"
#include "base/diag.h"
#include "base/symbol.h"
#include "parse/lexer.h"
#include "parse/parser.h"

namespace {

void print_help() {
  std::puts(
      "ymc - self-made Modelica compiler (S1)\n"
      "usage:\n"
      "  ymc <file.mo>                 parse and unparse (= --dump-after=parse)\n"
      "  ymc --dump-after=parse <f>    print unparsed source\n"
      "  ymc --check-syntax <f>        syntax check only\n"
      "  ymc --dump-tokens <f>         print token stream\n"
      "  ymc --help | --version");
}

bool read_file(const char* path, std::string& out) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return false;
  std::ostringstream ss;
  ss << f.rdbuf();
  out = ss.str();
  return true;
}

// 返回退出码：0 = 无错误。
int run(const char* mode, const char* path) {
  std::string src;
  if (!read_file(path, src)) {
    std::fprintf(stderr, "Error: cannot open file: %s\n", path);
    return 2;
  }
  ymc::Interner interner;
  ymc::Diagnostics diags;
  ymc::Lexer lexer(src, /*file=*/0, interner, diags);
  std::vector<ymc::Token> toks = lexer.tokenize();

  if (std::strcmp(mode, "dump-tokens") == 0) {
    for (const ymc::Token& t : toks) {
      const char* name = ymc::token_kind_name(t.kind);
      std::string text = t.text.is_valid() ? std::string(interner.str(t.text)) : "";
      std::printf("%u:%u\t%s\t%s\n", t.loc.line, t.loc.col, name, text.c_str());
    }
    diags.print(stderr);
    return diags.has_errors() ? 1 : 0;
  }

  ymc::Absyn absyn(interner);
  ymc::Parser parser(toks, absyn, diags);
  parser.parse_program();

  diags.print(stderr);

  if (std::strcmp(mode, "check-syntax") == 0) {
    if (diags.has_errors()) {
      std::fprintf(stderr, "%zu error(s)\n", diags.error_count());
      return 1;
    }
    std::puts("OK");
    return 0;
  }
  // dump-after=parse
  std::string text = ymc::unparse(absyn);
  std::fputs(text.c_str(), stdout);
  return diags.has_errors() ? 1 : 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc >= 2 && std::strcmp(argv[1], "--version") == 0) {
    std::puts("ymc 0.1.0 (S1)");
    return 0;
  }
  if (argc >= 2 && std::strcmp(argv[1], "--help") == 0) {
    print_help();
    return 0;
  }
  // 带文件参数的模式
  const char* mode = "dump-after=parse";
  const char* path = nullptr;
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--dump-after=parse") == 0) mode = "dump-after=parse";
    else if (std::strcmp(argv[i], "--check-syntax") == 0) mode = "check-syntax";
    else if (std::strcmp(argv[i], "--dump-tokens") == 0) mode = "dump-tokens";
    else if (argv[i][0] != '-') path = argv[i];
  }
  if (!path) {
    print_help();
    return 0;
  }
  return run(mode, path);
}
