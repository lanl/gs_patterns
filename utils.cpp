#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include <stdexcept>

#include "utils.h"

namespace gs_patterns
{
namespace gs_patterns_core
{

static inline int popcount(uint64_t x) {
    int c;

    for (c = 0; x != 0; x >>= 1)
        if (x & 1)
            c++;
    return c;
}

//string tools
int startswith(const char* a, const char* b) {
    if (strncmp(b, a, strlen(b)) == 0)
        return 1;
    return 0;
}

int endswith(const char* a, const char* b) {
    int idx = strlen(a);
    int preidx = strlen(b);

    if (preidx >= idx)
        return 0;
    if (strncmp(b, &a[idx - preidx], preidx) == 0)
        return 1;
    return 0;
}

//https://stackoverflow.com/questions/779875/what-function-is-to-replace-a-substring-from-a-string-in-c
const char* str_replace(const char* orig, const char* rep, const char* with) {
    char* result; // the return string
    char* ins;    // the next insert point
    char* tmp;    // varies
    int len_rep;  // length of rep (the string to remove)
    int len_with; // length of with (the string to replace rep with)
    int len_front; // distance between rep and end of last rep
    int count;    // number of replacements

    // sanity checks and initialization
    if (!orig)
        return NULL;

    if (!rep)
        return orig;

    len_rep = strlen(rep);
    if (len_rep == 0)
        return NULL; // empty rep causes infinite loop during count
    if (!with)
        with = "";
    len_with = strlen(with);

    // count the number of replacements needed
    ins = (char*)orig;
    for (count = 0; (tmp = strstr(ins, rep)); ++count) {
        ins = tmp + len_rep;
    }

    tmp = result = (char*)malloc(strlen(orig) + (len_with - len_rep) * count + 1);

    if (!result)
        return NULL;

    // first time through the loop, all the variable are set correctly
    // from here on,
    //    tmp points to the end of the result string
    //    ins points to the next occurrence of rep in orig
    //    orig points to the remainder of orig after "end of rep"
    while (count--) {
        ins = (char*)strstr(orig, rep);
        len_front = ins - orig;
        tmp = strncpy(tmp, orig, len_front) + len_front;
        tmp = strcpy(tmp, with) + len_with;
        orig += len_front + len_rep; // move to next "end of rep"
    }
    strcpy(tmp, orig);
    return result;
}

char* get_str(char* line, char* bparse, char* aparse) {

    char* sline;

    sline = (char*)str_replace(line, bparse, "");
    sline = (char*)str_replace(sline, aparse, "");

    return sline;
}

int cnt_str(char* line, char c) {

    int cnt = 0;
    for (int i = 0; line[i] != '\0'; i++) {
        if (line[i] == c)
            cnt++;
    }

    return cnt;
}

gzFile open_trace_file(const std::string & trace_file_name)
{
    gzFile fp;

    fp = gzopen(trace_file_name.c_str(), "hrb");
    if (NULL == fp) {
        throw std::runtime_error("Could not open " + trace_file_name + "!");
    }
    return fp;
}

void close_trace_file (gzFile & fp)
{
    gzclose(fp);
}

} // gs_patterns_core

} // gs_patterns