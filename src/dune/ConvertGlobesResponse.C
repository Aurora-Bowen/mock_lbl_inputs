// ConvertGlobesResponse.C
//
// Convert the DUNE TDR far-detector response written in GLoBES AEDL text
// (lbl_config/dune/tdr2017/globes_config/) into a ROOT file whose layout does
// not depend on GLoBES:
//
//   smearing/<energy_name>          TH2D  x = true energy (sampling bins), y = reconstructed energy (analysis bins)
//   efficiency/<FHC|RHC>/<channel>  TH1D  post-smearing efficiency list of the channel, reconstructed energy
//   xsec/<CC|NC>/<flavour>          TGraph x = E [GeV] (10^log10E), y = sigma/E as tabulated
//   meta/...                        TNamed provenance and global settings
//
// The AEDL files are read as plain text; no GLoBES code is used. The parser
// follows `include` statements from the top-level .glb file and handles the
// AEDL subset that appears in these files ($globals, CONSTANTS, %lists,
// nuflux/cross/energy/channel/rule/sys blocks, @energy matrices). Anything it
// does not recognise is printed as WARN and counted.
//
// It also writes channels.tsv (one row per channel) and rules.tsv (one row per
// (rule, channel) term), and prints the consistency checks (lines starting
// with CHECK).
//
// Run through scripts/dune/convert_response.sh, or:
//   root -l -b -q -e '.L src/dune/ConvertGlobesResponse.C+' \
//     -e 'ConvertGlobesResponse("lbl_config/dune/tdr2017/globes_config","DUNE_GLoBES.glb",
//          "lbl_config/dune/tdr2017/SHA256SUMS.txt","out.root","channels.tsv","rules.tsv","<git sha>")'

#include <TFile.h>
#include <TDirectory.h>
#include <TH1D.h>
#include <TH2D.h>
#include <TGraph.h>
#include <TNamed.h>
#include <TSystem.h>
#include <TSystemDirectory.h>
#include <TList.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace cgr {

struct Tok {
  std::string s;
  int kind;  // 0 word, 1 quoted string, 2 symbol
  std::string file;
  int line;
};

struct Value {
  std::vector<Tok> toks;
  std::string file;
  int line = 0;
  std::string Text() const {
    std::string t;
    for (const auto &k : toks) {
      if (!t.empty()) t += " ";
      t += (k.kind == 1 ? "\"" + k.s + "\"" : k.s);
    }
    return t;
  }
};

struct Block {
  std::string type, name, file;
  int line = 0;
  std::vector<std::string> keys;  // in order of appearance
  std::map<std::string, Value> attr;
};

struct Setting {
  std::string name, file;
  int line;
  Value val;
};

std::string gBase;
std::vector<std::string> gRead;  // files read, in order
std::set<std::string> gReadSet;
int gWarn = 0, gFail = 0;
std::map<std::string, std::vector<std::string>> gConstUse;  // constant -> "file:line (context)"

std::string Loc(const std::string &f, int l) { return f + ":" + std::to_string(l); }

void Warn(const std::string &m) { ++gWarn; printf("WARN %s\n", m.c_str()); }

void Check(bool ok, const std::string &name, const std::string &detail) {
  if (!ok) ++gFail;
  printf("CHECK %-4s %s  %s\n", ok ? "PASS" : "FAIL", name.c_str(), detail.c_str());
}

std::string Norm(std::string p) {
  while (p.compare(0, 2, "./") == 0) p = p.substr(2);
  return p;
}

bool Slurp(const std::string &rel, std::string &out) {
  std::ifstream in(gBase + "/" + rel, std::ios::binary);
  if (!in) return false;
  std::stringstream ss;
  ss << in.rdbuf();
  out = ss.str();
  return true;
}

bool IsWordChar(char c) { return std::isalnum((unsigned char)c) || std::strchr("_.+-#%$", c) != nullptr; }

// Tokenise one file and expand `include "file"` recursively.
void Tokenize(const std::string &relIn, std::vector<Tok> &out, int depth) {
  std::string rel = Norm(relIn);
  if (depth > 20) { Warn("include depth > 20 at " + rel); return; }
  std::string c;
  if (!Slurp(rel, c)) { Check(false, "file_readable", rel); return; }
  if (!gReadSet.count(rel)) { gReadSet.insert(rel); gRead.push_back(rel); }
  std::vector<Tok> loc;
  int line = 1;
  size_t i = 0, n = c.size();
  bool bol = true;
  while (i < n) {
    char ch = c[i];
    if (bol && ch == '%' && i + 1 < n && c[i + 1] == '!') {  // "%!GLoBES" magic header
      while (i < n && c[i] != '\n') ++i;
      continue;
    }
    bol = false;
    if (ch == '\n') { ++line; ++i; bol = true; continue; }
    if (std::isspace((unsigned char)ch)) { ++i; continue; }
    if (ch == '/' && i + 1 < n && c[i + 1] == '*') {
      i += 2;
      while (i + 1 < n && !(c[i] == '*' && c[i + 1] == '/')) { if (c[i] == '\n') ++line; ++i; }
      i += 2;
      continue;
    }
    if (ch == '/' && i + 1 < n && c[i + 1] == '/') { while (i < n && c[i] != '\n') ++i; continue; }
    if (ch == '"') {
      size_t j = c.find('"', i + 1);
      if (j == std::string::npos) { Warn("unterminated string at " + Loc(rel, line)); break; }
      loc.push_back({c.substr(i + 1, j - i - 1), 1, rel, line});
      i = j + 1;
      continue;
    }
    if (IsWordChar(ch)) {
      size_t j = i;
      while (j < n && IsWordChar(c[j])) ++j;
      loc.push_back({c.substr(i, j - i), 0, rel, line});
      i = j;
      continue;
    }
    loc.push_back({std::string(1, ch), 2, rel, line});
    ++i;
  }
  for (size_t k = 0; k < loc.size(); ++k) {
    if (loc[k].kind == 0 && loc[k].s == "include" && k + 1 < loc.size() && loc[k + 1].kind == 1) {
      printf("INCLUDE %s -> %s\n", Loc(rel, loc[k].line).c_str(), Norm(loc[k + 1].s).c_str());
      Tokenize(loc[k + 1].s, out, depth + 1);
      ++k;
    } else {
      out.push_back(loc[k]);
    }
  }
}

// ----------------------------------------------------------------- model
std::map<std::string, std::vector<Setting>> gGlobals;  // "$name" -> occurrences
std::map<std::string, Setting> gConst;                 // NAME -> definition
std::vector<std::string> gConstOrder;
std::map<std::string, Setting> gLists;                 // "%name" -> definition
std::vector<std::string> gListOrder;
std::vector<Block> gBlocks;

bool IsSym(const std::vector<Tok> &t, size_t i, const char *s) { return i < t.size() && t[i].kind == 2 && t[i].s == s; }

// value: tokens up to the end of a top-level statement ({...} or a single token or name(...))
size_t ReadSimpleValue(const std::vector<Tok> &t, size_t i, Value &v) {
  v.file = i < t.size() ? t[i].file : "";
  v.line = i < t.size() ? t[i].line : 0;
  if (IsSym(t, i, "{")) {
    int d = 0;
    for (; i < t.size(); ++i) {
      v.toks.push_back(t[i]);
      if (IsSym(t, i, "{")) ++d;
      if (IsSym(t, i, "}") && --d == 0) return i + 1;
    }
    return i;
  }
  if (i < t.size()) v.toks.push_back(t[i++]);
  if (IsSym(t, i, "(")) {  // function call such as samplingbincenter()
    for (; i < t.size(); ++i) {
      v.toks.push_back(t[i]);
      if (IsSym(t, i, ")")) return i + 1;
    }
  }
  return i;
}

void Parse(const std::vector<Tok> &t) {
  size_t i = 0;
  while (i < t.size()) {
    const Tok &a = t[i];
    if (a.kind == 0 && a.s[0] == '$' && IsSym(t, i + 1, "=")) {
      Setting s{a.s, a.file, a.line, Value()};
      i = ReadSimpleValue(t, i + 2, s.val);
      gGlobals[a.s].push_back(s);
      continue;
    }
    if (a.kind == 0 && a.s[0] == '%' && IsSym(t, i + 1, "=")) {
      Setting s{a.s, a.file, a.line, Value()};
      i = ReadSimpleValue(t, i + 2, s.val);
      if (gLists.count(a.s)) Warn("list " + a.s + " redefined at " + Loc(a.file, a.line));
      else gListOrder.push_back(a.s);
      gLists[a.s] = s;
      continue;
    }
    if (a.kind == 0 && IsSym(t, i + 1, "(") && i + 4 < t.size() && t[i + 2].kind == 0 && t[i + 2].s[0] == '#' &&
        IsSym(t, i + 3, ")") && IsSym(t, i + 4, "<")) {
      Block b;
      b.type = a.s; b.name = t[i + 2].s; b.file = a.file; b.line = a.line;
      i += 5;
      std::string key;
      int depth = 0;
      while (i < t.size()) {
        if (depth == 0 && IsSym(t, i, ">")) { ++i; break; }
        if (depth == 0 && IsSym(t, i, "@") && i + 2 < t.size() && t[i + 1].kind == 0 && IsSym(t, i + 2, "=")) {
          key = "@" + t[i + 1].s;
          if (b.attr.count(key)) Warn("attribute " + key + " repeated in " + b.name + " at " + Loc(t[i].file, t[i].line));
          else b.keys.push_back(key);
          b.attr[key] = Value();
          b.attr[key].file = t[i].file;
          b.attr[key].line = t[i].line;
          i += 3;
          continue;
        }
        if (IsSym(t, i, "{") || IsSym(t, i, "(")) ++depth;
        if (IsSym(t, i, "}") || IsSym(t, i, ")")) --depth;
        if (key.empty()) Warn("token '" + t[i].s + "' outside an attribute in " + b.name + " at " + Loc(t[i].file, t[i].line));
        else b.attr[key].toks.push_back(t[i]);
        ++i;
      }
      gBlocks.push_back(b);
      continue;
    }
    if (a.kind == 0 && IsSym(t, i + 1, "=") && i + 2 < t.size() && t[i + 2].kind == 0) {  // CONSTANT = number
      Setting s{a.s, a.file, a.line, Value()};
      s.val.toks.push_back(t[i + 2]);
      s.val.file = a.file; s.val.line = a.line;
      if (gConst.count(a.s)) Warn("constant " + a.s + " redefined at " + Loc(a.file, a.line));
      else gConstOrder.push_back(a.s);
      gConst[a.s] = s;
      i += 3;
      continue;
    }
    Warn("unparsed token '" + a.s + "' at " + Loc(a.file, a.line));
    ++i;
  }
}

bool ToDouble(const std::string &s, double &x) {
  if (s.empty()) return false;
  char *e = nullptr;
  x = std::strtod(s.c_str(), &e);
  return e && *e == '\0';
}

// number, or a constant from definitions.inc (usage is recorded)
bool Number(const Tok &k, double &x, const std::string &context) {
  if (ToDouble(k.s, x)) return true;
  auto it = gConst.find(k.s);
  if (it != gConst.end() && !it->second.val.toks.empty() && ToDouble(it->second.val.toks[0].s, x)) return true;
  return false;
}

// {a, b, c} -> numbers
bool NumberList(const Value &v, std::vector<double> &out, const std::string &context) {
  out.clear();
  if (v.toks.size() < 2 || v.toks.front().s != "{" || v.toks.back().s != "}") return false;
  bool expectNum = true;
  for (size_t i = 1; i + 1 < v.toks.size(); ++i) {
    const Tok &k = v.toks[i];
    if (expectNum) {
      double x;
      if (k.kind != 0 || !Number(k, x, context)) return false;
      out.push_back(x);
    } else if (!(k.kind == 2 && k.s == ",")) {
      return false;
    }
    expectNum = !expectNum;
  }
  return !expectNum || out.empty();
}

// split a token list at top-level ':' (outside {} and ())
std::vector<std::vector<Tok>> SplitColon(const std::vector<Tok> &v) {
  std::vector<std::vector<Tok>> r(1);
  int d = 0;
  for (const auto &k : v) {
    if (k.kind == 2 && (k.s == "{" || k.s == "(")) ++d;
    if (k.kind == 2 && (k.s == "}" || k.s == ")")) --d;
    if (d == 0 && k.kind == 2 && k.s == ":") { r.emplace_back(); continue; }
    r.back().push_back(k);
  }
  return r;
}

std::string Join(const std::vector<Tok> &v) {
  std::string s;
  for (const auto &k : v) s += k.s;
  return s;
}

std::string Fmt(double x) {
  char b[64];
  snprintf(b, sizeof b, "%.17g", x);
  return b;
}

struct Matrix {
  int nrows = 0;
  std::vector<int> klo, khi;
  std::vector<std::vector<double>> vals;
  bool terminated = false;
  bool ok = true;
  std::string err;
};

Matrix ParseMatrix(const Value &v) {
  Matrix m;
  const auto &t = v.toks;
  size_t i = 0;
  while (i < t.size()) {
    if (IsSym(t, i, ";")) { m.terminated = true; ++i; break; }
    if (!IsSym(t, i, "{")) { m.ok = false; m.err = "expected '{' at " + Loc(t[i].file, t[i].line) + " got '" + t[i].s + "'"; return m; }
    ++i;
    std::vector<double> row;
    bool expectNum = true;
    while (i < t.size() && !IsSym(t, i, "}")) {
      if (expectNum) {
        double x;
        if (!ToDouble(t[i].s, x)) { m.ok = false; m.err = "bad number '" + t[i].s + "' at " + Loc(t[i].file, t[i].line); return m; }
        row.push_back(x);
      } else if (!IsSym(t, i, ",")) {
        m.ok = false; m.err = "expected ',' at " + Loc(t[i].file, t[i].line); return m;
      }
      expectNum = !expectNum;
      ++i;
    }
    ++i;  // '}'
    if (row.size() < 2) { m.ok = false; m.err = "row with fewer than 2 numbers at row " + std::to_string(m.nrows); return m; }
    m.klo.push_back((int)row[0]);
    m.khi.push_back((int)row[1]);
    if (row[0] != (int)row[0] || row[1] != (int)row[1]) { m.ok = false; m.err = "non-integer k index in row " + std::to_string(m.nrows); }
    m.vals.emplace_back(row.begin() + 2, row.end());
    ++m.nrows;
    if (IsSym(t, i, ":")) { ++i; continue; }
    if (IsSym(t, i, ";")) continue;
    if (i < t.size()) { m.ok = false; m.err = "expected ':' or ';' after row " + std::to_string(m.nrows - 1) + " at " + Loc(t[i].file, t[i].line); return m; }
  }
  if (i < t.size()) { m.ok = false; m.err = "tokens after ';'"; }
  return m;
}

std::vector<double> EdgesFrom(double lo, double hi, const std::vector<double> &widths, int nfixed, std::string &how) {
  std::vector<double> e;
  if (!widths.empty()) {
    e.push_back(lo);
    double x = lo;
    for (double w : widths) { x += w; e.push_back(x); }
  } else if (nfixed > 0) {
    for (int k = 0; k <= nfixed; ++k) e.push_back(lo + (hi - lo) * k / nfixed);
    how += " (uniform)";
  }
  return e;
}

std::string Globalsrc(const std::string &k) {
  auto it = gGlobals.find(k);
  if (it == gGlobals.end()) return "";
  std::string s;
  for (const auto &x : it->second) s += (s.empty() ? "" : ", ") + Loc(x.file, x.line);
  return s;
}

const Setting *Global(const std::string &k) {
  auto it = gGlobals.find(k);
  return it == gGlobals.end() ? nullptr : &it->second.back();
}

double GlobalNumber(const std::string &k, bool &found) {
  found = false;
  const Setting *s = Global(k);
  double x = 0;
  if (s && s->val.toks.size() == 1 && Number(s->val.toks[0], x, k)) found = true;
  return x;
}

const Block *FindBlock(const std::string &type, const std::string &name) {
  const Block *r = nullptr;
  for (const auto &b : gBlocks)
    if (b.type == type && b.name == name && (!r || !b.keys.empty())) r = &b;  // prefer the non-empty definition
  return r;
}

std::string Strip(const std::string &s) { return (!s.empty() && (s[0] == '#' || s[0] == '%' || s[0] == '$')) ? s.substr(1) : s; }

void Mkdir(TFile *f, const std::string &path) {
  TDirectory *d = f;
  std::stringstream ss(path);
  std::string part;
  while (std::getline(ss, part, '/')) {
    TDirectory *s = d->GetDirectory(part.c_str());
    d = s ? s : d->mkdir(part.c_str());
  }
}

void WriteNamed(TFile *f, const std::string &dir, const std::string &name, const std::string &title) {
  Mkdir(f, dir);
  f->cd(dir.c_str());
  TNamed n(name.c_str(), title.c_str());
  n.Write();
}

}  // namespace cgr

int ConvertGlobesResponse(const char *globesDir, const char *topFile, const char *shaFile, const char *outRoot,
                          const char *channelsTsv, const char *rulesTsv, const char *gitCommit,
                          const char *converterPath = "src/dune/ConvertGlobesResponse.C",
                          const char *shaPrefix = "globes_config/") {
  using namespace cgr;
  gBase = globesDir;
  printf("ConvertGlobesResponse: base=%s top=%s\n", globesDir, topFile);

  // ------------------------------------------------------------- Step 1: parse
  std::vector<Tok> toks;
  Tokenize(topFile, toks, 0);
  Parse(toks);
  // where each constant is used: every word token equal to a constant name, except its definition
  {
    std::string key, blk;
    for (size_t i = 0; i < toks.size(); ++i) {
      const Tok &k = toks[i];
      if (k.kind == 0 && (k.s[0] == '$' || k.s[0] == '%') && IsSym(toks, i + 1, "=")) key = k.s;
      if (k.kind == 0 && k.s[0] == '#' && IsSym(toks, i + 1, ")")) blk = k.s;
      if (IsSym(toks, i, "@") && i + 2 < toks.size() && IsSym(toks, i + 2, "=")) key = blk + " @" + toks[i + 1].s;
      if (k.kind != 0 || !gConst.count(k.s)) continue;
      if (IsSym(toks, i + 1, "=")) continue;  // the definition itself
      gConstUse[k.s].push_back(Loc(k.file, k.line) + " (" + key + ")");
    }
  }
  printf("files read (%zu):\n", gRead.size());
  for (auto &f : gRead) printf("  %s\n", f.c_str());

  printf("\n== global settings\n");
  for (auto &g : gGlobals)
    for (auto &s : g.second) {
      std::string txt = s.val.Text();
      if (txt.size() > 140) txt = txt.substr(0, 140) + " ... (" + std::to_string(s.val.toks.size()) + " tokens)";
      printf("GLOBAL %s = %s  [%s]\n", g.first.c_str(), txt.c_str(), Loc(s.file, s.line).c_str());
    }
  printf("\n== constants\n");
  for (auto &c : gConstOrder) printf("CONST %s = %s  [%s]\n", c.c_str(), gConst[c].val.Text().c_str(), Loc(gConst[c].file, gConst[c].line).c_str());
  printf("\n== lists\n");
  for (auto &l : gListOrder) {
    std::vector<double> v;
    bool num = NumberList(gLists[l].val, v, l);
    printf("LIST %s  [%s]  %s\n", l.c_str(), Loc(gLists[l].file, gLists[l].line).c_str(),
           num ? (std::to_string(v.size()) + " numbers").c_str() : gLists[l].val.Text().c_str());
  }
  printf("\n== blocks\n");
  std::map<std::string, int> nType;
  for (auto &b : gBlocks) {
    ++nType[b.type];
    printf("BLOCK %s(%s) [%s] keys:", b.type.c_str(), b.name.c_str(), Loc(b.file, b.line).c_str());
    for (auto &k : b.keys) printf(" %s", k.c_str());
    printf("\n");
  }
  for (auto &x : nType) printf("  %d %s blocks\n", x.second, x.first.c_str());

  // flux definitions
  printf("\n== flux definitions\n");
  std::map<std::string, std::string> fluxMode;
  for (auto &b : gBlocks) {
    if (b.type != "nuflux") continue;
    std::string mode = "UNKNOWN";
    // The AEDL nuflux block has no polarity keyword; the only FHC/RHC marker inside
    // the definition is its identifier (#flux_FHC / #flux_RHC).
    std::string nm = Strip(b.name);
    if (nm.size() >= 4 && nm.compare(nm.size() - 4, 4, "_FHC") == 0) mode = "FHC";
    if (nm.size() >= 4 && nm.compare(nm.size() - 4, 4, "_RHC") == 0) mode = "RHC";
    fluxMode[b.name] = mode;
    printf("FLUX %s [%s] beam_mode=%s (from the definition's identifier)\n", b.name.c_str(), Loc(b.file, b.line).c_str(), mode.c_str());
    for (auto &k : b.keys) {
      const Value &v = b.attr.at(k);
      double x;
      std::string res;
      if (v.toks.size() == 1 && v.toks[0].kind == 0 && !ToDouble(v.toks[0].s, x) && Number(v.toks[0], x, b.name + " " + k))
        res = " = " + Fmt(x);
      printf("  %s = %s%s  [%s]\n", k.c_str(), v.Text().c_str(), res.c_str(), Loc(v.file, v.line).c_str());
    }
  }
  printf("\n== cross-section definitions\n");
  for (auto &b : gBlocks)
    if (b.type == "cross")
      for (auto &k : b.keys) printf("CROSS %s %s = %s [%s]\n", b.name.c_str(), k.c_str(), b.attr.at(k).Text().c_str(), Loc(b.attr.at(k).file, b.attr.at(k).line).c_str());
  printf("\n== sys definitions\n");
  for (auto &b : gBlocks)
    if (b.type == "sys")
      for (auto &k : b.keys) {
        double x = 0;
        const Value &v = b.attr.at(k);
        bool ok = v.toks.size() == 1 && Number(v.toks[0], x, b.name + " " + k);
        printf("SYS %s %s = %s%s [%s]\n", b.name.c_str(), k.c_str(), v.Text().c_str(), ok ? (" = " + Fmt(x)).c_str() : "", Loc(v.file, v.line).c_str());
      }

  // energy definitions
  printf("\n== energy definitions\n");
  std::vector<std::string> energyOrder;
  std::map<std::string, const Block *> energyDef;
  for (auto &b : gBlocks) {
    if (b.type != "energy") continue;
    printf("ENERGY %s [%s] %s\n", b.name.c_str(), Loc(b.file, b.line).c_str(), b.keys.empty() ? "(empty body)" : "(has @energy)");
    if (!b.keys.empty()) {
      if (energyDef.count(b.name)) Warn("second non-empty energy definition " + b.name);
      else { energyOrder.push_back(b.name); energyDef[b.name] = &b; }
    }
  }

  // ------------------------------------------------------------- binning
  printf("\n== binning\n");
  bool f1, f2, f3;
  double emin = GlobalNumber("$emin", f1), emax = GlobalNumber("$emax", f2);
  int nbinsDecl = (int)GlobalNumber("$bins", f3);
  std::vector<double> binsize;
  std::string recoHow, trueHow;
  if (Global("$binsize")) {
    NumberList(Global("$binsize")->val, binsize, "$binsize");
    recoHow = "$emin, $binsize (" + Globalsrc("$emin") + ", " + Globalsrc("$binsize") + ")";
  } else {
    recoHow = "$emin, $emax, $bins (" + Globalsrc("$emin") + ", " + Globalsrc("$emax") + ", " + Globalsrc("$bins") + ")";
  }
  std::vector<double> recoEdges = EdgesFrom(emin, emax, binsize, f3 ? nbinsDecl : 0, recoHow);
  int nReco = (int)recoEdges.size() - 1;
  printf("reco bins: %d from %s\n", nReco, recoHow.c_str());
  if (f3) Check(nbinsDecl == nReco, "bins_vs_binsize", "$bins=" + std::to_string(nbinsDecl) + " edges give " + std::to_string(nReco));
  else printf("  $bins not set; number of bins = number of $binsize entries = %d\n", nReco);
  Check(f2 && std::fabs(recoEdges.back() - emax) < 1e-9, "binsize_sum_equals_emax",
        "emin+sum(binsize)=" + Fmt(recoEdges.back()) + " $emax=" + Fmt(emax));

  double smin = GlobalNumber("$sampling_min", f1), smax = GlobalNumber("$sampling_max", f2);
  int nsampDecl = (int)GlobalNumber("$sampling_points", f3);
  std::vector<double> sstep;
  if (Global("$sampling_stepsize")) {
    NumberList(Global("$sampling_stepsize")->val, sstep, "$sampling_stepsize");
    trueHow = "$sampling_min, $sampling_stepsize (" + Globalsrc("$sampling_min") + ", " + Globalsrc("$sampling_stepsize") + ")";
  } else {
    trueHow = "$sampling_min, $sampling_max, $sampling_points";
  }
  std::vector<double> trueEdges = EdgesFrom(smin, smax, sstep, f3 ? nsampDecl : 0, trueHow);
  int nTrue = (int)trueEdges.size() - 1;
  printf("true sampling bins: %d from %s\n", nTrue, trueHow.c_str());
  Check(f2 && std::fabs(trueEdges.back() - smax) < 1e-9, "stepsize_sum_equals_sampling_max",
        "sampling_min+sum(stepsize)=" + Fmt(trueEdges.back()) + " $sampling_max=" + Fmt(smax));
  printf("reco edges:");
  for (double e : recoEdges) printf(" %g", e);
  printf("\ntrue edges:");
  for (double e : trueEdges) printf(" %g", e);
  printf("\n");

  // ------------------------------------------------------------- channels
  printf("\n== channels\n");
  struct Chan {
    std::string name, flux, mode, pol, ini, fin, xsec, energy, effList, effFile, src;
  };
  std::vector<Chan> chans;
  std::set<std::string> usedLists, usedEnergy;
  for (auto &b : gBlocks) {
    if (b.type != "channel") continue;
    Chan c;
    c.name = b.name;
    c.src = Loc(b.file, b.line);
    auto it = b.attr.find("@channel");
    if (it == b.attr.end()) { Check(false, "channel_has_@channel", b.name); continue; }
    auto parts = SplitColon(it->second.toks);
    if (parts.size() != 6) { Check(false, "channel_six_fields", b.name + " has " + std::to_string(parts.size())); continue; }
    c.flux = Join(parts[0]); c.pol = Join(parts[1]); c.ini = Join(parts[2]);
    c.fin = Join(parts[3]); c.xsec = Join(parts[4]); c.energy = Join(parts[5]);
    c.mode = fluxMode.count(c.flux) ? fluxMode[c.flux] : "UNDEFINED_FLUX";
    for (auto &k : b.keys) {
      if (k == "@channel" || k == "@post_smearing_efficiencies") continue;
      Warn("channel " + b.name + " has extra attribute " + k + " = " + b.attr.at(k).Text());
    }
    auto pe = b.attr.find("@post_smearing_efficiencies");
    if (pe != b.attr.end()) {
      const auto &v = pe->second.toks;
      if (v.size() == 4 && v[0].s == "copy" && v[1].s == "(" && v[3].s == ")") {
        c.effList = v[2].s;
        c.effFile = gLists.count(c.effList) ? gLists[c.effList].file : "UNDEFINED";
        usedLists.insert(c.effList);
      } else {
        c.effList = "inline:" + pe->second.Text();
        c.effFile = pe->second.file;
      }
    } else {
      c.effList = "none";
      c.effFile = "none";
    }
    usedEnergy.insert(c.energy);
    Check(fluxMode.count(c.flux) > 0, "channel_flux_defined", c.name + " -> " + c.flux);
    Check(FindBlock("cross", c.xsec) != nullptr, "channel_xsec_defined", c.name + " -> " + c.xsec);
    Check(energyDef.count(c.energy) > 0, "channel_energy_defined", c.name + " -> " + c.energy);
    Check(c.effList == "none" || c.effFile != "UNDEFINED", "channel_efflist_defined", c.name + " -> " + c.effList);
    chans.push_back(c);
    printf("CHANNEL %s [%s] %s:%s:%s:%s:%s:%s mode=%s post_eff=%s (%s)\n", c.name.c_str(), c.src.c_str(), c.flux.c_str(), c.pol.c_str(),
           c.ini.c_str(), c.fin.c_str(), c.xsec.c_str(), c.energy.c_str(), c.mode.c_str(), c.effList.c_str(), c.effFile.c_str());
  }
  printf("pre-smearing efficiencies / backgrounds: none found in any channel (checked attribute names above; extra attributes are WARNed)\n");

  {
    std::ofstream o(channelsTsv);
    o << "channel\tflux_name\tbeam_mode\tpolarity\tinitial_flavour\tfinal_flavour\txsec_name\tenergy_name\tpost_eff_list\tpost_eff_file\tsource\n";
    for (auto &c : chans)
      o << Strip(c.name) << "\t" << Strip(c.flux) << "\t" << c.mode << "\t" << c.pol << "\t" << c.ini << "\t" << c.fin << "\t"
        << Strip(c.xsec) << "\t" << Strip(c.energy) << "\t" << Strip(c.effList) << "\t" << c.effFile << "\t" << c.src << "\n";
  }

  // ------------------------------------------------------------- rules
  printf("\n== rules\n");
  std::set<std::string> chanInRule;
  {
    std::ofstream o(rulesTsv);
    o << "rule\trole\tcoefficient\tchannel\tenergy_window\tsource\n";
    for (auto &b : gBlocks) {
      if (b.type != "rule") continue;
      std::string win = "none";
      if (b.attr.count("@energy_window")) {
        auto p = SplitColon(b.attr.at("@energy_window").toks);
        win = "";
        for (size_t k = 0; k < p.size(); ++k) win += (k ? ":" : "") + Join(p[k]);
      }
      printf("RULE %s [%s]\n", b.name.c_str(), Loc(b.file, b.line).c_str());
      for (auto &k : b.keys) printf("  %s = %s [%s]\n", k.c_str(), b.attr.at(k).Text().c_str(), Loc(b.attr.at(k).file, b.attr.at(k).line).c_str());
      for (const char *role : {"@signal", "@background"}) {
        if (!b.attr.count(role)) continue;
        const Value &v = b.attr.at(role);
        auto terms = SplitColon(v.toks);
        std::string errKey = std::string(role) == "@signal" ? "@sys_on_multiex_errors_sig" : "@sys_on_multiex_errors_bg";
        size_t nerr = b.attr.count(errKey) ? SplitColon(b.attr.at(errKey).toks).size() : 0;
        Check(nerr == 0 || nerr == terms.size(), "rule_errors_match_terms",
              Strip(b.name) + " " + role + ": " + std::to_string(terms.size()) + " terms, " + std::to_string(nerr) + " entries in " + errKey);
        for (auto &term : terms) {
          // coef @ #channel
          if (term.size() != 3 || term[1].s != "@") { Check(false, "rule_term_format", Strip(b.name) + " " + Join(term)); continue; }
          chanInRule.insert(term[2].s);
          bool def = false;
          for (auto &c : chans) def |= c.name == term[2].s;
          Check(def, "rule_channel_defined", Strip(b.name) + " -> " + term[2].s);
          o << Strip(b.name) << "\t" << (std::string(role) == "@signal" ? "signal" : "background") << "\t" << term[0].s << "\t"
            << Strip(term[2].s) << "\t" << win << "\t" << Loc(term[0].file, term[0].line) << "\n";
        }
      }
    }
  }
  for (auto &c : chans) Check(chanInRule.count(c.name) > 0, "channel_used_by_a_rule", c.name);

  // ------------------------------------------------------------- unreferenced
  printf("\n== unreferenced definitions and files\n");
  for (auto &l : gListOrder)
    if (!usedLists.count(l)) printf("UNUSED_LIST %s defined at %s (no channel uses it)\n", l.c_str(), Loc(gLists[l].file, gLists[l].line).c_str());
  for (auto &e : energyOrder)
    if (!usedEnergy.count(e)) printf("UNUSED_ENERGY %s (no channel uses it)\n", e.c_str());
  std::map<std::string, int> energyUse;
  for (auto &c : chans) ++energyUse[c.energy];
  for (auto &e : energyOrder) printf("ENERGY_USE %s used by %d channel(s)\n", e.c_str(), energyUse[e]);
  std::map<std::string, std::string> fileRef;  // files named by @flux_file / @cross_file
  for (auto &b : gBlocks)
    for (const char *k : {"@flux_file", "@cross_file"})
      if (b.attr.count(k) && !b.attr.at(k).toks.empty()) fileRef[Norm(b.attr.at(k).toks[0].s)] = std::string(k) + " of " + b.name;
  for (auto &r : fileRef) printf("REFERENCED %s by %s\n", r.first.c_str(), r.second.c_str());
  for (const char *sub : {"smr", "eff", "xsec", "flux", "."}) {
    TSystemDirectory d(sub, (gBase + "/" + sub).c_str());
    TList *fl = d.GetListOfFiles();
    if (!fl) continue;
    std::vector<std::string> names;
    for (TObject *o : *fl) {
      std::string n = o->GetName();
      if (n == "." || n == ".." || ((TSystemDirectory *)o)->IsDirectory()) continue;
      names.push_back(std::string(sub) == "." ? n : std::string(sub) + "/" + n);
    }
    delete fl;
    std::sort(names.begin(), names.end());
    for (auto &n : names)
      if (!gReadSet.count(n) && !fileRef.count(n)) printf("NOT_REFERENCED %s (neither included from %s nor named by @flux_file/@cross_file)\n", n.c_str(), topFile);
  }
  printf("flux files named by @flux_file are not read: flux is not converted into the response file\n");

  // ------------------------------------------------------------- Step 2: checks
  printf("\n== Step 2: smearing matrices\n");
  std::map<std::string, Matrix> mats;
  for (auto &e : energyOrder) {
    const Block *b = energyDef[e];
    if (!b->attr.count("@energy")) { Check(false, "energy_has_@energy", e); continue; }
    for (auto &k : b->keys)
      if (k != "@energy") Warn("energy " + e + " has attribute " + k);
    Matrix m = ParseMatrix(b->attr.at("@energy"));
    if (!m.ok) { Check(false, "matrix_parse", e + ": " + m.err); continue; }
    Check(m.terminated, "matrix_terminated_by_semicolon", e);
    Check(m.nrows == nReco, "matrix_rows_eq_bins", e + ": rows=" + std::to_string(m.nrows) + " bins=" + std::to_string(nReco));
    int badLen = 0, badK = 0;
    for (int r = 0; r < m.nrows; ++r) {
      if ((int)m.vals[r].size() != m.khi[r] - m.klo[r] + 1) {
        ++badLen;
        printf("  %s row %d: %zu values, k_low=%d k_high=%d\n", e.c_str(), r, m.vals[r].size(), m.klo[r], m.khi[r]);
      }
      if (m.klo[r] < 0 || m.khi[r] >= nTrue || m.klo[r] > m.khi[r]) {
        ++badK;
        printf("  %s row %d: k_low=%d k_high=%d, sampling points=%d\n", e.c_str(), r, m.klo[r], m.khi[r], nTrue);
      }
    }
    Check(badLen == 0, "matrix_row_lengths", e + ": rows with n_values != k_high-k_low+1: " + std::to_string(badLen));
    Check(badK == 0, "matrix_k_range", e + ": rows with k outside [0," + std::to_string(nTrue - 1) + "]: " + std::to_string(badK));
    std::vector<double> col(nTrue, 0.0);
    for (int r = 0; r < m.nrows; ++r)
      for (size_t j = 0; j < m.vals[r].size(); ++j) {
        int k = m.klo[r] + (int)j;
        if (k >= 0 && k < nTrue) col[k] += m.vals[r][j];
      }
    int nOne = 0, nZero = 0, nOther = 0;
    double mn = 1e300, mx = -1e300;
    for (double s : col) {
      if (std::fabs(s - 1) <= 1e-5) ++nOne;
      else if (s == 0) ++nZero;
      else { ++nOther; mn = std::min(mn, s); mx = std::max(mx, s); }
    }
    int klo = *std::min_element(m.klo.begin(), m.klo.end()), khi = *std::max_element(m.khi.begin(), m.khi.end());
    double vmin = 1e300, vmax = -1e300;
    long nneg = 0, nval = 0;
    for (auto &r : m.vals)
      for (double v : r) { vmin = std::min(vmin, v); vmax = std::max(vmax, v); nneg += v < 0; ++nval; }
    printf("MATRIX %s file=%s rows=%d k_low_min=%d k_high_max=%d values=%ld min=%s max=%s negative=%ld colsum: ==1(1e-5)=%d ==0=%d other=%d",
           e.c_str(), b->file.c_str(), m.nrows, klo, khi, nval, Fmt(vmin).c_str(), Fmt(vmax).c_str(), nneg, nOne, nZero, nOther);
    if (nOther) printf(" other_min=%s other_max=%s", Fmt(mn).c_str(), Fmt(mx).c_str());
    printf("\n");
    mats[e] = m;
  }

  printf("\n== Step 2: post-smearing efficiency lists\n");
  std::map<std::string, std::vector<double>> effs;
  for (auto &l : gListOrder) {
    std::vector<double> v;
    if (!NumberList(gLists[l].val, v, l)) { printf("LIST %s is not a number list (%s), skipped\n", l.c_str(), gLists[l].val.Text().substr(0, 60).c_str()); continue; }
    if (!usedLists.count(l)) { printf("EFF(unused) %s n=%zu\n", l.c_str(), v.size()); continue; }
    double mn = *std::min_element(v.begin(), v.end()), mx = *std::max_element(v.begin(), v.end());
    int out = 0;
    for (double x : v) out += (x < 0 || x > 1);
    Check((int)v.size() == nReco, "eff_length_eq_bins", l + ": n=" + std::to_string(v.size()));
    Check(out == 0, "eff_in_0_1", l + ": outside [0,1]: " + std::to_string(out));
    printf("EFF %s file=%s n=%zu min=%s max=%s outside01=%d\n", l.c_str(), gLists[l].file.c_str(), v.size(), Fmt(mn).c_str(), Fmt(mx).c_str(), out);
    effs[l] = v;
  }
  // efficiency files not reached by include: inspect as text for the report
  {
    TSystemDirectory d("eff", (gBase + "/eff").c_str());
    TList *fl = d.GetListOfFiles();
    std::vector<std::string> names;
    if (fl) {
      for (TObject *o : *fl) names.push_back(o->GetName());
      delete fl;
    }
    std::sort(names.begin(), names.end());
    for (auto &n : names) {
      std::string rel = "eff/" + n;
      if (gReadSet.count(rel) || n == "." || n == "..") continue;
      std::string c;
      if (!Slurp(rel, c)) continue;
      size_t a = c.find('{'), z = c.find('}');
      std::string lname = c.substr(0, c.find('='));
      lname.erase(std::remove_if(lname.begin(), lname.end(), ::isspace), lname.end());
      int cnt = 0;
      double mn = 1e300, mx = -1e300;
      if (a != std::string::npos && z != std::string::npos) {
        std::stringstream ss(c.substr(a + 1, z - a - 1));
        std::string item;
        while (std::getline(ss, item, ',')) {
          double x;
          item.erase(std::remove_if(item.begin(), item.end(), ::isspace), item.end());
          if (ToDouble(item, x)) { ++cnt; mn = std::min(mn, x); mx = std::max(mx, x); }
        }
      }
      printf("EFF(not referenced) %s defines %s n=%d min=%s max=%s\n", rel.c_str(), lname.c_str(), cnt, Fmt(mn).c_str(), Fmt(mx).c_str());
    }
  }

  printf("\n== Step 2: cross-section tables\n");
  const char *flav[6] = {"nue", "numu", "nutau", "nuebar", "numubar", "nutaubar"};
  struct XS { std::string name, file; std::vector<double> l; std::vector<std::vector<double>> y; };
  std::vector<XS> xss;
  for (auto &b : gBlocks) {
    if (b.type != "cross" || !b.attr.count("@cross_file")) continue;
    XS x;
    x.name = Strip(b.name);
    x.file = Norm(b.attr.at("@cross_file").toks[0].s);
    std::string c;
    if (!Slurp(x.file, c)) { Check(false, "xsec_readable", x.file); continue; }
    if (!gReadSet.count(x.file)) { gReadSet.insert(x.file); gRead.push_back(x.file); }
    std::stringstream ss(c);
    std::string ln;
    std::set<size_t> ncol;
    int lineNo = 0;
    while (std::getline(ss, ln)) {
      ++lineNo;
      std::stringstream ls(ln);
      std::vector<double> row;
      std::string w;
      bool bad = false;
      while (ls >> w) { double v; if (ToDouble(w, v)) row.push_back(v); else bad = true; }
      if (row.empty() && !bad) continue;
      if (bad) { Check(false, "xsec_numeric", Loc(x.file, lineNo)); continue; }
      ncol.insert(row.size());
      x.l.push_back(row[0]);
      x.y.emplace_back(row.begin() + 1, row.end());
    }
    double smin = 1e300, smax = -1e300;
    for (size_t k = 1; k < x.l.size(); ++k) { smin = std::min(smin, x.l[k] - x.l[k - 1]); smax = std::max(smax, x.l[k] - x.l[k - 1]); }
    std::string cols;
    for (auto n : ncol) cols += std::to_string(n) + " ";
    Check(ncol.size() == 1 && *ncol.begin() == 7, "xsec_7_columns", x.file + ": column counts " + cols);
    printf("XSEC %s file=%s rows=%zu log10E_first=%s log10E_last=%s step_min=%s step_max=%s columns=%s\n", x.name.c_str(), x.file.c_str(),
           x.l.size(), Fmt(x.l.front()).c_str(), Fmt(x.l.back()).c_str(), Fmt(smin).c_str(), Fmt(smax).c_str(), cols.c_str());
    for (int f = 0; f < 6; ++f) {
      double mx = 0;
      for (auto &r : x.y) if ((int)r.size() > f) mx = std::max(mx, std::fabs(r[f]));
      printf("  column %d (%s by GLoBES manual order): max|y|=%s\n", f + 2, flav[f], Fmt(mx).c_str());
    }
    xss.push_back(x);
  }

  // ------------------------------------------------------------- Step 3: write ROOT
  printf("\n== Step 3: writing %s\n", outRoot);
  TFile *f = TFile::Open(outRoot, "RECREATE");
  if (!f || f->IsZombie()) { printf("ERROR cannot open %s\n", outRoot); return 2; }
  int nObj = 0;
  for (auto &e : energyOrder) {
    if (!mats.count(e)) continue;
    Matrix &m = mats[e];
    if (m.nrows != nReco) { printf("SKIP smearing/%s: rows != bins\n", Strip(e).c_str()); continue; }
    Mkdir(f, "smearing");
    f->cd("smearing");
    std::string title = "GLoBES energy(" + e + ") from " + energyDef[e]->file +
                        ": x = true neutrino energy [GeV] (sampling bins), y = reconstructed energy [GeV] (analysis bins), "
                        "content = matrix element as in the file (bins outside k_low..k_high = 0);E_{true} [GeV];E_{reco} [GeV]";
    TH2D h(Strip(e).c_str(), title.c_str(), nTrue, trueEdges.data(), nReco, recoEdges.data());
    for (int r = 0; r < m.nrows; ++r)
      for (size_t j = 0; j < m.vals[r].size(); ++j) {
        int k = m.klo[r] + (int)j;
        if (k >= 0 && k < nTrue) h.SetBinContent(k + 1, r + 1, m.vals[r][j]);
      }
    h.SetEntries(0);
    h.Write();
    ++nObj;
  }
  for (auto &c : chans) {
    if (!effs.count(c.effList)) { printf("SKIP efficiency/%s/%s: list %s not valid\n", c.mode.c_str(), Strip(c.name).c_str(), c.effList.c_str()); continue; }
    auto &v = effs[c.effList];
    if ((int)v.size() != nReco) { printf("SKIP efficiency %s: length\n", c.name.c_str()); continue; }
    std::string dir = "efficiency/" + c.mode;
    Mkdir(f, dir);
    f->cd(dir.c_str());
    std::string title = "post-smearing efficiency " + c.effList + " (" + c.effFile + ") of channel(" + c.name + ");E_{reco} [GeV];efficiency";
    TH1D h(Strip(c.name).c_str(), title.c_str(), nReco, recoEdges.data());
    for (int k = 0; k < nReco; ++k) h.SetBinContent(k + 1, v[k]);
    h.SetEntries(0);
    h.Write();
    ++nObj;
  }
  const char *xtitle = "GLoBES table, GENIE 2.12.10 + DUNE weights, 1e-38 cm^2/GeV, per-nucleon vs per-nucleus convention not established; reference only";
  for (auto &x : xss) {
    std::string dir = "xsec/" + x.name;
    Mkdir(f, dir);
    f->cd(dir.c_str());
    for (int fl = 0; fl < 6; ++fl) {
      TGraph g((int)x.l.size());
      for (size_t k = 0; k < x.l.size(); ++k) g.SetPoint((int)k, std::pow(10.0, x.l[k]), x.y[k][fl]);
      g.SetName(flav[fl]);
      g.TNamed::SetTitle(xtitle);  // TGraph::SetTitle would split at the ";" into axis titles
      g.Write(flav[fl]);
      ++nObj;
    }
  }

  // meta
  WriteNamed(f, "meta", "source", "arXiv:2103.04797v2 ancillary files, unmodified copy in lbl_config/dune/tdr2017/globes_config");
  WriteNamed(f, "meta", "converter", converterPath);
  WriteNamed(f, "meta", "git_commit", gitCommit);
  if (Global("$version")) WriteNamed(f, "meta", "globes_version", Global("$version")->val.toks[0].s + "  [" + Globalsrc("$version") + "]");
  WriteNamed(f, "meta", "reco_binning", "from " + recoHow);
  WriteNamed(f, "meta", "true_binning", "from " + trueHow);
  {
    std::string s;
    for (double e : recoEdges) s += Fmt(e) + " ";
    WriteNamed(f, "meta", "reco_edges", s);
    s.clear();
    for (double e : trueEdges) s += Fmt(e) + " ";
    WriteNamed(f, "meta", "true_edges", s);
  }
  // sha256 of every input file read, from SHA256SUMS.txt
  {
    std::map<std::string, std::string> sums;
    std::ifstream in(shaFile);
    std::string h, p;
    while (in >> h >> p) sums[p] = h;
    std::string all;
    int missing = 0;
    std::vector<std::string> files(gRead.begin(), gRead.end());
    std::sort(files.begin(), files.end());
    for (auto &r : files) {
      std::string key = shaPrefix + r;
      if (!sums.count(key)) { ++missing; printf("SHA256 missing for %s\n", key.c_str()); }
      all += (sums.count(key) ? sums[key] : std::string("MISSING")) + "  " + key + "\n";
    }
    Check(missing == 0, "sha256_listed_for_all_inputs", std::to_string(files.size()) + " files, missing " + std::to_string(missing));
    WriteNamed(f, "meta", "sha256", all);
  }
  for (auto &g : gGlobals)
    for (size_t k = 0; k < g.second.size(); ++k) {
      auto &s = g.second[k];
      std::string nm = Strip(g.first) + (g.second.size() > 1 ? "_" + std::to_string(k) : "");
      WriteNamed(f, "meta/globals", nm, s.val.Text() + "  [" + Loc(s.file, s.line) + "]");
    }
  for (auto &c : gConstOrder) {
    std::string uses;
    for (auto &u : gConstUse[c]) uses += (uses.empty() ? "" : "; ") + u;
    WriteNamed(f, "meta/constants", c, gConst[c].val.Text() + "  [defined " + Loc(gConst[c].file, gConst[c].line) + "; used " + (uses.empty() ? "nowhere" : uses) + "]");
  }
  for (auto &b : gBlocks) {
    if (b.type != "nuflux" && b.type != "rule" && b.type != "sys" && b.type != "cross") continue;
    std::string t = b.type + "(" + b.name + ") [" + Loc(b.file, b.line) + "]";
    for (auto &k : b.keys) t += "\n" + k + " = " + b.attr.at(k).Text() + "  [" + Loc(b.attr.at(k).file, b.attr.at(k).line) + "]";
    WriteNamed(f, "meta/" + b.type, Strip(b.name), t);
  }
  f->Close();
  delete f;

  printf("\nconstants usage:\n");
  for (auto &c : gConstOrder) {
    printf("  %s:", c.c_str());
    if (gConstUse[c].empty()) printf(" not used");
    for (auto &u : gConstUse[c]) printf(" %s;", u.c_str());
    printf("\n");
  }
  printf("\nSUMMARY objects(histograms/graphs)=%d channels=%zu energies=%zu lists_used=%zu checks_failed=%d warnings=%d\n", nObj, chans.size(),
         energyOrder.size(), usedLists.size(), gFail, gWarn);
  return gFail ? 1 : 0;
}
