#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <phosg/Strings.hh>
#include <string>

using namespace std;


string escape_quotes(const string& s) {
  string ret;
  for (size_t x = 0; x < s.size(); x++) {
    char ch = s[x];
    if (ch == '\"') {
      ret += "\\\"";
    } else if (ch < 0x20 || ch > 0x7E) {
      ret += string_printf("\\x%02X", ch);
    } else {
      ret += ch;
    }
  }
  return ret;
}

string first_file_that_exists(const vector<string>& names) {

  for (const auto& it : names) {
    struct stat st;
    if (stat(it.c_str(), &st) == 0) {
      return it;
    }
  }

  return "";
}
