// 库装载器（S2）：按 MODELICAPATH 找库、把目录/文件层次装成一棵 Absyn（方案 §5 S2）。
//
// 依据（MLS 3.6，页码见 docs 引用的 R10）：
//   §13.3   MODELICAPATH 是有序根列表；顶层名找不到才搜；在某根找到 A 后不回溯其它根
//   §13.4.1 目录 = 结构化实体：必含 package.mo（定义同名类）；子目录/子文件是该类的元素；
//           package.order 存在时类与常量按其顺序加入，不匹配 → 顺序 tool specific、可警告
//   §13.4.2 X.mo 只定义一个类且名为 X；从 MODELICAPATH 加载多类文件是 error；扩展名 .mo
//   §13.4.3 非顶层文件以 within <包围类> 开头，该类必须存在且非短类定义
//   §18.8.3 四种存储形式：IDENT.mo | IDENT VERSION.mo | IDENT/ | IDENT VERSION/
//   §18.8.2 uses(Lib(version="x")) 声明依赖；工具据此选版本（§13.3 "may"）
// OMC 对齐（默认根目录、多版本选择、错误路径、package.order、uses）：锚点见 loader.cpp。
#pragma once
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "absyn/absyn.h"
#include "base/diag.h"
#include "scode/version.h"

namespace ymc {

struct LoaderOptions {
  std::vector<std::filesystem::path> roots;  // MODELICAPATH 有序根列表
  bool follow_uses = true;                   // 装载后递归装载顶层类 uses 注解里的库
  bool pure_impure_keywords = true;          // 词法器的 --std 开关（S1）
};

struct LoadedLibrary {
  std::string name;
  bool has_version = false;
  Version version;
  std::filesystem::path path;   // 库根（目录或 .mo 文件）
  Id<ClassDef> cls;             // 顶层类
};

// 顶层类注解里的版本元数据（MLS §18.8.2）：version / conversion(noneFromVersion, from(version=…)) / uses(...)。
// 版本选择与 uses 递归装载都靠它（Interactive.mo:7511-7548）。
struct LibraryMeta {
  std::string version;                      // annotation(version = "4.0.0")
  std::vector<std::string> none_from;       // conversion(noneFromVersion = "3.2.3", ...)
  std::vector<std::string> convert_from;    // conversion(from(version = "2.1" | {"2.1", "2.2"}, script = ...))
  std::vector<std::pair<std::string, std::string>> uses;  // 仅 uses(Lib(version = "x")) → (Lib, x)
  std::vector<std::string> uses_missing_version;           // version 不是字符串：警告后按 default 装载
};

// 从已解析的类提取注解元数据。
[[nodiscard]] LibraryMeta library_meta(const Absyn& a, Id<ClassDef> cls);
// 只为读元数据解析一个库根文件（package.mo / X.mo）到临时树，不污染主树；读不到/无类 → 全空。
[[nodiscard]] LibraryMeta read_library_meta(const std::filesystem::path& root_file, Interner& interner,
                                            bool pure_impure_keywords = true);

class Loader {
 public:
  Loader(Absyn& absyn, Diagnostics& diags, LoaderOptions opt)
      : a_(absyn), diags_(diags), opt_(std::move(opt)) {}

  // 按名字（可选版本，空 = 默认）从 roots 找库并装载进 Absyn::top；已装载则直接返回。
  // 失败返回 invalid 并在 diags 报错。
  Id<ClassDef> load_library(std::string_view name, std::string_view requested_version = "");

  // 显式装载一个 .mo 文件或库目录（等价 OMC loadFile 的最简形式）；返回其顶层类。
  Id<ClassDef> load_path(const std::filesystem::path& path);

  [[nodiscard]] const std::vector<LoadedLibrary>& loaded() const noexcept { return loaded_; }

  // 默认根列表：OPENMODELICALIBRARY → MODELICAPATH（ymc 有意超集）→ OMC 用户库目录。
  // Windows 根列表都用 ';' 分隔（settingsimpl.c:226-258；MLS §13.3）。
  static std::vector<std::filesystem::path> default_roots();
  static std::vector<std::filesystem::path> split_modelica_path(std::string_view s);

 private:
  struct Candidate {
    LibraryEntry entry;
    std::filesystem::path path;
    Version version;      // 目录名里的版本；没有则取注解 version
    LibraryMeta meta;     // 根文件注解（版本选择用）
  };
  void scan_root(const std::filesystem::path& root, std::string_view name,
                 std::vector<Candidate>& out);
  // 版本选择（复刻 OMC 两层机制的结果，规则与锚点见 loader.cpp；决策见 ADR-005）：
  // 请求 V：0 = 版本相同（带后缀的请求要求后缀前缀匹配）；1 = 候选的 conversion(noneFromVersion) 含 V；
  // 都没有 → V 是已知发行版或该库没有任何 conversion 历史时，做"前 j 级相等且 ≥ V 取最高"的逐级放宽；
  // 再没有 → 无版本信息的条目；否则 nullptr。V 为空 → 最高版本（同版本取枚举靠后者）。
  const Candidate* choose_version(const std::vector<Candidate>& cands,
                                  std::string_view requested_version) const;
  Id<ClassDef> load_library_impl(std::string_view name, std::string_view requested_version,
                                 bool force_load, std::string_view request_origin);

  // 解析一个文件得到 stored-definition（共享 Absyn/Interner）；失败时 classes 为空。
  StoredDefinition parse_file(const std::filesystem::path& file, std::uint32_t file_id);
  // 装载目录形式的结构化实体（§13.4.1）：parent 无效 = 顶层库
  Id<ClassDef> load_dir(const std::filesystem::path& dir, const std::string& expected_name,
                        Id<ClassDef> parent, const std::vector<Symbol>& parent_path);
  // 装载单文件实体（§13.4.2）
  Id<ClassDef> load_file_entity(const std::filesystem::path& file, const std::string& expected_name,
                                Id<ClassDef> parent, const std::vector<Symbol>& parent_path,
                                bool expect_parts = false);
  // 把子文件的类插进 parent（§13.4.1 同名冲突 / §13.4.3 包围类不能是短类）；within 校验在 load_file_entity 里
  bool place_into(Id<ClassDef> cls, Id<ClassDef> parent, const std::vector<Symbol>& parent_path);
  std::vector<std::string> read_package_order(const std::filesystem::path& dir) const;
  void follow_uses(Id<ClassDef> top);

  Absyn& a_;
  Diagnostics& diags_;
  LoaderOptions opt_;
  std::vector<LoadedLibrary> loaded_;
  std::vector<std::string> file_names_;   // file_id → 路径（诊断用）
};

// 递归枚举类名（S2 验收，对齐 OMC getClassNames(recursive=true, qualified=true)）：
// 先序：root 的每个子类（按元素顺序），每个子类紧跟其子孙。探针（2026-09-16，omc 1.25.0）：
// OMC 的结果把 root 自身放在第一个，这里不含 root，由调用方先打印 root。
void collect_class_names(const Absyn& a, Id<ClassDef> root, const std::string& prefix,
                         bool include_protected, std::vector<std::string>& out);

}  // namespace ymc
