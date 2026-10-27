#include <phosg/Filesystem.hh>
#include <phosg/Strings.hh>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace std;
using namespace phosg;

static constexpr size_t BLK = 1024;

static void append_u32be(string& out, uint32_t v) {
  out.push_back((char)((v >> 24) & 0xff));
  out.push_back((char)((v >> 16) & 0xff));
  out.push_back((char)((v >> 8) & 0xff));
  out.push_back((char)(v & 0xff));
}

static void append_i32be(string& out, int32_t v) {
  append_u32be(out, (uint32_t)v);
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

static string from_hex(const string& hex) {
  if (hex.size() % 2 != 0) {
    throw runtime_error("odd hex length");
  }
  string out;
  out.reserve(hex.size() / 2);
  for (size_t i = 0; i < hex.size(); i += 2) {
    out += (char)stoul(hex.substr(i, 2), nullptr, 16);
  }
  return out;
}

static string strip_line(const string& ln) {
  size_t a = 0, b = ln.size();
  while (a < b && (ln[a] == ' ' || ln[a] == '\t')) {
    a++;
  }
  while (b > a && (ln[b - 1] == ' ' || ln[b - 1] == '\t')) {
    b--;
  }
  return ln.substr(a, b - a);
}

static pair<string, size_t> parse(
    const vector<string>& lines,
    size_t i,
    bool end_brace,
    uint8_t pb,
    const string& rel) {
  string out;
  while (i < lines.size()) {
    const string& ln = lines[i];
    string s = strip_line(ln);
    if (s == "}" && end_brace) {
      return {std::move(out), i + 1};
    }
    if (s == "#source") {
      i++;
      while (i < lines.size() && lines[i] != "#endsource") {
        const string& b = lines[i];
        out += "srcl";
        out += "Strn";
        append_u32be(out, (uint32_t)b.size());
        out += b;
        size_t pad = (4 - (b.size() % 4)) % 4;
        out.append(pad, (char)pb);
        i++;
      }
      if (i >= lines.size()) {
        throw runtime_error(format("{}:{}: missing #endsource", rel, i));
      }
      i++;
      continue;
    }
    if (s.empty()) {
      i++;
      continue;
    }
    if (s.size() < 9) {
      throw runtime_error(format("{}:{}: malformed line", rel, i + 1));
    }
    string k = s.substr(0, 4);
    string t = s.size() >= 9 ? s.substr(5, 4) : string();
    string rest = s.size() > 10 ? s.substr(10) : string();

    if (s.size() >= 8 && s.compare(5, 3, "Int") == 0) {
      int32_t v = (int32_t)stoll(s.substr(9));
      out += k;
      out += "Int ";
      append_i32be(out, v);
      i++;
    } else if (t == "Strn") {
      string pp;
      bool have_pp = false;
      if (rest.find(" pad=") != string::npos && !rest.empty() && rest.back() != '"') {
        size_t pos = rest.rfind(" pad=");
        pp = from_hex(rest.substr(pos + 5));
        rest = rest.substr(0, pos);
        have_pp = true;
      }
      string b = unesc(rest);
      out += k;
      out += "Strn";
      append_u32be(out, (uint32_t)b.size());
      out += b;
      if (have_pp) {
        out += pp;
      } else {
        size_t pad = (4 - (b.size() % 4)) % 4;
        out.append(pad, (char)pb);
      }
      i++;
    } else if (t == "Rect") {
      int32_t a, b, c, d;
      size_t p = 0;
      auto next_int = [&](int32_t& v) {
        while (p < rest.size() && rest[p] == ' ') {
          p++;
        }
        size_t start = p;
        if (p < rest.size() && (rest[p] == '-' || rest[p] == '+')) {
          p++;
        }
        while (p < rest.size() && rest[p] >= '0' && rest[p] <= '9') {
          p++;
        }
        if (start == p) {
          throw runtime_error(format("{}:{}: malformed Rect", rel, i + 1));
        }
        v = (int32_t)stoll(rest.substr(start, p - start));
      };
      next_int(a);
      next_int(b);
      next_int(c);
      next_int(d);
      out += k;
      out += "Rect";
      append_i32be(out, a);
      append_i32be(out, b);
      append_i32be(out, c);
      append_i32be(out, d);
      i++;
    } else if (t == "Grup") {
      size_t sp = 0;
      while (sp < rest.size() && rest[sp] == ' ') {
        sp++;
      }
      size_t ep = sp;
      while (ep < rest.size() && rest[ep] != ' ') {
        ep++;
      }
      if (sp == ep) {
        throw runtime_error(format("{}:{}: malformed Grup", rel, i + 1));
      }
      uint32_t gid = (uint32_t)stoul(rest.substr(sp, ep - sp));
      auto [body, ni] = parse(lines, i + 1, true, pb, rel);
      out += k;
      out += "Grup";
      append_u32be(out, 4 + (uint32_t)body.size());
      append_u32be(out, gid);
      out += body;
      out += "EndG";
      out += "Int ";
      append_u32be(out, gid);
      i = ni;
    } else {
      throw runtime_error(format("{}:{}: unknown type or malformed line", rel, i + 1));
    }
  }
  if (end_brace) {
    throw runtime_error(format("{}:{}: missing }}", rel, i));
  }
  return {std::move(out), i};
}

static vector<string> split_lines(const string& data) {
  vector<string> lines;
  size_t start = 0;
  while (start <= data.size()) {
    size_t nl = data.find('\n', start);
    if (nl == string::npos) {
      lines.push_back(data.substr(start));
      break;
    }
    lines.push_back(data.substr(start, nl - start));
    start = nl + 1;
  }
  return lines;
}

static void build(const string& indir, const string& path) {
  string man_path = (filesystem::path(indir) / "project.rbmanifest").string();
  string man_data = load_file(man_path);
  vector<string> M = split_lines(man_data);
  if (!M.empty() && M.back().empty()) {
    // allow trailing newline; blank last line after split of "...\n" is empty
  }

  if (M.empty() || M[0].compare(0, 6, "#rbbf ") != 0) {
    throw runtime_error(man_path + ":1: missing #rbbf header");
  }

  // Parse #rbbf version=... fieldA=... fieldB=...
  map<string, string> h;
  {
    // Use simple split on spaces then key=value
    string hdr = M[0];
    size_t pos = 6;
    while (pos < hdr.size()) {
      while (pos < hdr.size() && hdr[pos] == ' ') {
        pos++;
      }
      if (pos >= hdr.size()) {
        break;
      }
      size_t eq = hdr.find('=', pos);
      if (eq == string::npos) {
        throw runtime_error(man_path + ":1: bad #rbbf header");
      }
      string key = hdr.substr(pos, eq - pos);
      size_t vend = eq + 1;
      while (vend < hdr.size() && hdr[vend] != ' ') {
        vend++;
      }
      h[key] = hdr.substr(eq + 1, vend - eq - 1);
      pos = vend;
    }
  }

  vector<pair<uint32_t, uint32_t>> pairs;
  vector<string> order;
  string trail;
  for (size_t mi = 1; mi < M.size(); mi++) {
    const string& x = M[mi];
    if (x.empty()) {
      continue;
    }
    if (x.compare(0, 6, "#pair ") == 0) {
      size_t p = 6;
      while (p < x.size() && x[p] == ' ') {
        p++;
      }
      size_t mid = x.find(' ', p);
      if (mid == string::npos) {
        throw runtime_error(format("{}:{}: bad #pair", man_path, mi + 1));
      }
      uint32_t a = (uint32_t)stoul(x.substr(p, mid - p));
      uint32_t b = (uint32_t)stoul(x.substr(mid + 1));
      pairs.emplace_back(a, b);
    } else if (x.compare(0, 7, "#order ") == 0) {
      order.push_back(unesc(x.substr(7)));
    } else if (x.compare(0, 9, "#trailer ") == 0) {
      trail = from_hex(x.substr(9));
    }
  }

  string out;
  out += "RbBF";
  append_u32be(out, (uint32_t)stoul(h.at("version")));
  append_u32be(out, (uint32_t)pairs.size());
  for (auto& pr : pairs) {
    append_u32be(out, pr.first);
    append_u32be(out, pr.second);
  }
  append_u32be(out, (uint32_t)stoul(h.at("fieldA")));
  append_u32be(out, (uint32_t)stoul(h.at("fieldB")));

  for (const string& rel : order) {
    string fpath = (filesystem::path(indir) / rel).string();
    string fdata = load_file(fpath);
    vector<string> L = split_lines(fdata);
    if (!L.empty() && L.back().empty()) {
      L.pop_back();
    }
    if (L.empty()) {
      throw runtime_error(rel + ":1: empty block file");
    }

    // Parse #block "m" "tag" id=... reserved=... flags=... stamp=...,... [strpad=...] [size=...]
    // Match oracle: hd=L[0].split(); m=unesc(hd[1]); tag=unesc(hd[2]); kv=dict(...)
    vector<string> hd;
    {
      const string& line = L[0];
      size_t p = 0;
      while (p < line.size()) {
        while (p < line.size() && line[p] == ' ') {
          p++;
        }
        if (p >= line.size()) {
          break;
        }
        if (line[p] == '"') {
          size_t q = p + 1;
          while (q < line.size()) {
            if (line[q] == '\\' && q + 1 < line.size()) {
              q += 2;
              continue;
            }
            if (line[q] == '"') {
              q++;
              break;
            }
            q++;
          }
          hd.push_back(line.substr(p, q - p));
          p = q;
        } else {
          size_t q = p;
          while (q < line.size() && line[q] != ' ') {
            q++;
          }
          hd.push_back(line.substr(p, q - p));
          p = q;
        }
      }
    }
    if (hd.size() < 3 || hd[0] != "#block") {
      throw runtime_error(format("{}:1: missing #block header", rel));
    }
    string m = unesc(hd[1]);
    string tag = unesc(hd[2]);
    map<string, string> kv;
    for (size_t hi = 3; hi < hd.size(); hi++) {
      size_t eq = hd[hi].find('=');
      if (eq == string::npos) {
        throw runtime_error(format("{}:1: bad #block field", rel));
      }
      kv[hd[hi].substr(0, eq)] = hd[hi].substr(eq + 1);
    }

    auto need = [&](const char* key) -> const string& {
      auto it = kv.find(key);
      if (it == kv.end()) {
        throw runtime_error(format("{}:1: missing {}", rel, key));
      }
      return it->second;
    };

    string stamp = need("stamp");
    size_t comma = stamp.find(',');
    if (comma == string::npos) {
      throw runtime_error(format("{}:1: bad stamp", rel));
    }
    uint32_t s0 = (uint32_t)stoul(stamp.substr(0, comma), nullptr, 16);
    uint32_t s1 = (uint32_t)stoul(stamp.substr(comma + 1), nullptr, 16);
    uint32_t bid = (uint32_t)stoul(need("id"), nullptr, 0);
    uint32_t res = (uint32_t)stoul(need("reserved"));
    uint32_t fl = (uint32_t)stoul(need("flags"));

    string body;
    uint32_t sz = 0;
    if (L.size() >= 2 && L[1].compare(0, 5, "#raw ") == 0) {
      body = from_hex(L[1].substr(5));
      sz = 32 + (uint32_t)body.size();
    } else {
      uint8_t pb = 0;
      auto spit = kv.find("strpad");
      if (spit != kv.end()) {
        pb = (uint8_t)stoul(spit->second, nullptr, 0);
      }
      auto [parsed, _] = parse(L, 1, false, pb, rel);
      body = std::move(parsed);
      auto sit = kv.find("size");
      if (sit != kv.end()) {
        sz = (uint32_t)stoul(sit->second);
      } else {
        sz = (uint32_t)(((32 + body.size() + 12 + BLK - 1) / BLK) * BLK);
      }
      int64_t np64 = (int64_t)sz - 32 - (int64_t)body.size() - 12;
      if (np64 < 0) {
        throw runtime_error(format("{}:1: size too small", rel));
      }
      uint32_t np = (uint32_t)np64;
      body += "Padn";
      body += "Padn";
      append_u32be(body, np);
      body.append(np, '*');
    }

    out += m;
    out += tag;
    append_u32be(out, bid);
    append_u32be(out, res);
    append_u32be(out, sz);
    append_u32be(out, fl);
    append_u32be(out, s0);
    append_u32be(out, s1);
    out += body;
  }

  out += "EOF!";
  out += trail;
  save_file(path, out);
}

int main(int argc, char** argv) {
  try {
    if (argc == 2 && (!strcmp(argv[1], "--help") || !strcmp(argv[1], "-h"))) {
      fprintf(stderr, "Usage: rbproject_build [--help] in_dir out.rbp\n");
      fprintf(stderr, "Builds an RBBF v1 REALbasic project from a text tree.\n");
      return 0;
    }
    if (argc != 3) {
      fprintf(stderr, "Usage: rbproject_build [--help] in_dir out.rbp\n");
      return 1;
    }
    build(argv[1], argv[2]);
    return 0;
  } catch (const exception& e) {
    fprintf(stderr, "rbproject_build: %s\n", e.what());
    return 1;
  }
}
