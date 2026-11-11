#include <phosg/Filesystem.hh>
#include <phosg/Strings.hh>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <functional>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

using namespace std;
using namespace phosg;

static constexpr size_t BLK = 1024;
static uint8_t g_pad = 0;

static uint32_t u32be(const string& d, size_t o) {
  return ((uint8_t)d[o] << 24) | ((uint8_t)d[o + 1] << 16) | ((uint8_t)d[o + 2] << 8) | (uint8_t)d[o + 3];
}

static int32_t i32be(const string& d, size_t o) {
  return (int32_t)u32be(d, o);
}

static string esc(const string& b) {
  string out = "\"";
  for (unsigned char c : b) {
    if (c == 0x22) {
      out += "\\\"";
    } else if (c == 0x5c) {
      out += "\\\\";
    } else if (c >= 0x20 && c < 0x7f) {
      out += (char)c;
    } else {
      out += format("\\x{:02x}", c);
    }
  }
  out += '"';
  return out;
}

static string unesc(const string& s) {
  if (s.size() < 2 || s.front() != '"' || s.back() != '"') {
    throw runtime_error("bad string escape");
  }
  string b;
  size_t i = 1;
  size_t end = s.size() - 1;
  while (i < end) {
    if (s[i] == '\\') {
      if (i + 1 >= end) {
        throw runtime_error("truncated escape");
      }
      if (s[i + 1] == 'x') {
        if (i + 3 >= end) {
          throw runtime_error("truncated hex escape");
        }
        b += (char)stoul(s.substr(i + 2, 2), nullptr, 16);
        i += 4;
      } else {
        b += s[i + 1];
        i += 2;
      }
    } else {
      b += s[i];
      i += 1;
    }
  }
  return b;
}

static string padsfx(const string& p) {
  string expect(p.size(), (char)g_pad);
  if (p == expect) {
    return "";
  }
  string h;
  for (unsigned char c : p) {
    h += format("{:02x}", c);
  }
  return " pad=" + h;
}

static bool is_srcl_strn_line(const string& line, string* out_quoted = nullptr) {
  size_t i = 0;
  while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) {
    i++;
  }
  static const char prefix[] = "srcl Strn ";
  if (line.compare(i, sizeof(prefix) - 1, prefix) != 0) {
    return false;
  }
  size_t q = i + sizeof(prefix) - 1;
  if (q >= line.size() || line[q] != '"' || line.back() != '"') {
    return false;
  }
  if (out_quoted) {
    *out_quoted = line.substr(q);
  }
  return true;
}

static vector<string> srcfold(const vector<string>& sub, const string& ind) {
  vector<string> out;
  size_t i = 0;
  while (i < sub.size()) {
    size_t j = i;
    while (j < sub.size() && is_srcl_strn_line(sub[j])) {
      j++;
    }
    if (j > i) {
      vector<string> lines;
      bool ok = true;
      for (size_t k = i; k < j; k++) {
        string quoted;
        is_srcl_strn_line(sub[k], &quoted);
        string raw = unesc(quoted);
        if (raw.find('\r') != string::npos || raw.find('\n') != string::npos) {
          ok = false;
        }
        // Python bytes.strip() removes ASCII whitespace (0x09-0x0d, 0x20)
        size_t a = 0, b = raw.size();
        auto is_ws = [](unsigned char c) {
          return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
        };
        while (a < b && is_ws((unsigned char)raw[a])) {
          a++;
        }
        while (b > a && is_ws((unsigned char)raw[b - 1])) {
          b--;
        }
        if (raw.substr(a, b - a) == "#endsource") {
          ok = false;
        }
        lines.push_back(std::move(raw));
      }
      if (ok) {
        out.push_back(ind + "#source");
        for (auto& l : lines) {
          out.push_back(std::move(l));
        }
        out.push_back("#endsource");
      } else {
        out.insert(out.end(), sub.begin() + (ptrdiff_t)i, sub.begin() + (ptrdiff_t)j);
      }
      i = j;
    } else {
      out.push_back(sub[i]);
      i++;
    }
  }
  return out;
}

static size_t recs(const string& d, size_t o, size_t end, const string& ind, vector<string>& L) {
  while (o < end) {
    string k = d.substr(o, 4);
    string t = d.substr(o + 4, 4);
    if (k == "Padn" && t == "Padn") {
      return o;
    }
    if (t == "Int ") {
      L.push_back(ind + k + " Int " + to_string(i32be(d, o + 8)));
      o += 12;
    } else if (t == "Strn") {
      uint32_t n = u32be(d, o + 8);
      size_t padded = (n + 3) & ~3u;
      string pad = d.substr(o + 12 + n, padded - n);
      L.push_back(ind + k + " Strn " + esc(d.substr(o + 12, n)) + padsfx(pad));
      o += 12 + padded;
    } else if (t == "Rect") {
      L.push_back(ind + k + " Rect " + to_string(i32be(d, o + 8)) + " " + to_string(i32be(d, o + 12)) +
          " " + to_string(i32be(d, o + 16)) + " " + to_string(i32be(d, o + 20)));
      o += 24;
    } else if (t == "Grup") {
      uint32_t sz = u32be(d, o + 8);
      uint32_t gid = u32be(d, o + 12);
      L.push_back(ind + k + " Grup " + to_string(gid) + " {");
      vector<string> sub;
      o = recs(d, o + 16, o + 12 + sz, ind + "  ", sub);
      vector<string> folded = srcfold(sub, ind + "  ");
      L.insert(L.end(), folded.begin(), folded.end());
      if (d.substr(o, 8) != "EndGInt " || u32be(d, o + 8) != gid) {
        throw runtime_error(format("bad EndG @{:#x}", o));
      }
      o += 12;
      L.push_back(ind + "}");
    } else {
      throw runtime_error(format("unknown record at {:#x}", o));
    }
  }
  return o;
}

static uint8_t guesspad(const string& d, size_t o, size_t end) {
  map<uint8_t, int> counts;
  vector<uint8_t> order;
  for (size_t i = o; i + 4 <= end; i++) {
    if (d[i] == 'S' && d[i + 1] == 't' && d[i + 2] == 'r' && d[i + 3] == 'n') {
      size_t p = i + 4;
      if (p + 4 > end) {
        continue;
      }
      uint32_t n = u32be(d, p);
      uint32_t padlen = (4u - (n % 4u)) % 4u;
      if (padlen > 0 && p + 4 + n + padlen <= end) {
        uint8_t ch = (uint8_t)d[p + 4 + n];
        if (!counts.count(ch)) {
          order.push_back(ch);
        }
        counts[ch]++;
      }
    }
  }
  if (order.empty()) {
    return 0;
  }
  uint8_t best = order[0];
  int bestn = counts[best];
  for (size_t i = 1; i < order.size(); i++) {
    if (counts[order[i]] > bestn) {
      best = order[i];
      bestn = counts[best];
    }
  }
  return best;
}

static string safe_name(const string& n) {
  string out;
  for (unsigned char c : n) {
    if (c == '/' || c == ':' || c == 0) {
      out += '_';
    } else {
      out += (char)c;
    }
  }
  if (out.empty()) {
    out = "_";
  }
  return out;
}

static string to_lower(const string& s) {
  string o = s;
  for (char& c : o) {
    if (c >= 'A' && c <= 'Z') {
      c = (char)(c - 'A' + 'a');
    }
  }
  return o;
}

static string join_path(const string& a, const string& b) {
  if (a.empty()) {
    return b;
  }
  if (b.empty()) {
    return a;
  }
  return a + "/" + b;
}

struct Block {
  string tag;
  uint32_t id = 0;
  string name;
  bool has_name = false;
  uint32_t cont = 0;
  vector<string> text;
};

static void extract(const string& path, const string& outdir) {
  string d = load_file(path);
  if (d.size() < 12 || d.substr(0, 4) != "RbBF") {
    throw runtime_error("not RbBF");
  }
  uint32_t ver = u32be(d, 4);
  if (ver != 1) {
    throw runtime_error(format(
        "RBBF version {} not supported (RB 3.5.2 writes version 1)", ver));
  }
  uint32_t npairs = u32be(d, 8);
  vector<pair<uint32_t, uint32_t>> pairs;
  for (uint32_t i = 0; i < npairs; i++) {
    size_t off = 12 + 8 * i;
    pairs.emplace_back(u32be(d, off), u32be(d, off + 4));
  }
  uint32_t fa = u32be(d, 12 + 8 * npairs);
  uint32_t fb = u32be(d, 16 + 8 * npairs);
  size_t o = 20 + 8 * npairs;

  vector<Block> blocks;
  while (o + 4 <= d.size() && d.substr(o, 4) != "EOF!") {
    if (o + 32 > d.size()) {
      throw runtime_error("truncated block header");
    }
    string m = d.substr(o, 4);
    string tag = d.substr(o + 4, 4);
    uint32_t bid = u32be(d, o + 8);
    uint32_t res = u32be(d, o + 12);
    uint32_t sz = u32be(d, o + 16);
    uint32_t fl = u32be(d, o + 20);
    uint32_t s0 = u32be(d, o + 24);
    uint32_t s1 = u32be(d, o + 28);
    if (sz <= 32) {
      throw runtime_error("bad block size");
    }
    if (o + sz > d.size()) {
      throw runtime_error("block exceeds file");
    }

    vector<string> L;
    L.push_back(format(
        "#block {} {} id=0x{:08x} reserved={} flags={} stamp=0x{:08x},0x{:08x}",
        esc(m), esc(tag), bid, res, fl, s0, s1));

    if (m == "Blok" && fl == 0) {
      g_pad = guesspad(d, o + 32, o + sz);
      vector<string> sub;
      size_t pe = recs(d, o + 32, o + sz, "", sub);
      if (g_pad != 0) {
        L[0] += format(" strpad=0x{:02x}", g_pad);
      }
      vector<string> folded = srcfold(sub, "");
      L.insert(L.end(), folded.begin(), folded.end());
      uint32_t np = u32be(d, pe + 8);
      if (d.substr(pe, 8) != "PadnPadn" || pe + 12 + np != o + sz ||
          d.substr(pe + 12, np) != string(np, '*')) {
        throw runtime_error("bad Padn");
      }
      size_t rounded = ((pe - o + 12 + BLK - 1) / BLK) * BLK;
      if (rounded != sz) {
        L[0] += format(" size={}", sz);
      }
    } else {
      string raw = d.substr(o + 32, sz - 32);
      string hex;
      for (unsigned char c : raw) {
        hex += format("{:02x}", c);
      }
      L.push_back("#raw " + hex);
    }

    Block b;
    b.tag = tag;
    b.id = bid;
    for (const auto& x : L) {
      if (x.compare(0, 10, "Name Strn ") == 0 && !b.has_name) {
        b.name = unesc(x.substr(10));
        b.has_name = true;
      }
      if (x.compare(0, 9, "Cont Int ") == 0) {
        b.cont = (uint32_t)stoll(x.substr(9));
        break;
      }
    }
    b.text = std::move(L);
    blocks.push_back(std::move(b));
    o += sz;
  }
  if (o + 4 > d.size() || d.substr(o, 4) != "EOF!") {
    throw runtime_error("missing EOF!");
  }
  string trail = d.substr(o + 4);

  map<uint32_t, size_t> byid;
  for (size_t i = 0; i < blocks.size(); i++) {
    byid[blocks[i].id] = i;
  }

  function<string(size_t, vector<uint32_t>)> dirof = [&](size_t bi, vector<uint32_t> seen) -> string {
    const Block& b = blocks[bi];
    uint32_t c = b.cont;
    if (c) {
      auto it = byid.find(c);
      if (it != byid.end() && blocks[it->second].tag == "pFol") {
        bool in_seen = false;
        for (uint32_t s : seen) {
          if (s == c) {
            in_seen = true;
            break;
          }
        }
        if (!in_seen) {
          seen.push_back(c);
          const Block& p = blocks[it->second];
          string pname = p.has_name ? p.name : format("{:08x}", c);
          return join_path(dirof(it->second, seen), safe_name(pname));
        }
      }
    }
    return "";
  };

  filesystem::create_directories(outdir);
  set<string> used;
  vector<string> order;

  for (size_t bi = 0; bi < blocks.size(); bi++) {
    const Block& b = blocks[bi];
    string dd = dirof(bi, {});
    string fn;
    if (b.tag == "pFol") {
      string pname = b.has_name ? b.name : format("{:08x}", b.id);
      dd = join_path(dd, safe_name(pname));
      fn = "_folder.rb";
    } else if (b.tag == "Proj") {
      fn = "_project.rb";
    } else {
      string base = b.has_name ? b.name : (b.tag + format("_{:08x}", b.id));
      fn = safe_name(base) + ".rb";
    }
    string rel = join_path(dd, fn);
    string base_path;
    string ext;
    {
      size_t dot = rel.rfind('.');
      size_t slash = rel.rfind('/');
      if (dot != string::npos && (slash == string::npos || dot > slash)) {
        base_path = rel.substr(0, dot);
        ext = rel.substr(dot);
      } else {
        base_path = rel;
      }
    }
    int k = 2;
    while (used.count(to_lower(rel))) {
      rel = base_path + "." + to_string(k) + ext;
      k++;
    }
    used.insert(to_lower(rel));
    order.push_back(rel);
    if (!dd.empty()) {
      filesystem::create_directories(join_path(outdir, dd));
    }
    string body;
    for (size_t i = 0; i < b.text.size(); i++) {
      if (i) {
        body += '\n';
      }
      body += b.text[i];
    }
    body += '\n';
    save_file(join_path(outdir, rel), body);
  }

  vector<string> M;
  M.push_back(format("#rbbf version={} fieldA={} fieldB={}", ver, fa, fb));
  for (auto& p : pairs) {
    M.push_back(format("#pair {} {}", p.first, p.second));
  }
  if (!trail.empty()) {
    string hex;
    for (unsigned char c : trail) {
      hex += format("{:02x}", c);
    }
    M.push_back("#trailer " + hex);
  }
  for (auto& r : order) {
    M.push_back("#order " + esc(r));
  }
  string man;
  for (size_t i = 0; i < M.size(); i++) {
    if (i) {
      man += '\n';
    }
    man += M[i];
  }
  man += '\n';
  save_file(join_path(outdir, "project.rbmanifest"), man);
}

int main(int argc, char** argv) {
  try {
    if (argc == 2 && (!strcmp(argv[1], "--help") || !strcmp(argv[1], "-h"))) {
      fprintf(stderr, "Usage: rbproject_dasm [--help] project.rbp out_dir\n");
      fprintf(stderr, "Extracts an RBBF v1 REALbasic project to a text tree.\n");
      return 0;
    }
    if (argc != 3) {
      fprintf(stderr, "Usage: rbproject_dasm [--help] project.rbp out_dir\n");
      return 1;
    }
    extract(argv[1], argv[2]);
    return 0;
  } catch (const exception& e) {
    fprintf(stderr, "rbproject_dasm: %s\n", e.what());
    return 1;
  }
}
