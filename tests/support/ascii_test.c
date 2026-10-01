#include "console_common/support/ascii.h"

#include <assert.h>
#include <stdio.h>

int main(void) {
    assert(cc_ascii_lower('A') == 'a');
    assert(cc_ascii_lower('z') == 'z');
    assert(cc_ascii_lower(0xc3) == 0xc3);
    assert(cc_ascii_equal_ignore_case("", ""));
    assert(cc_ascii_equal_ignore_case("Folder/File", "folder/file"));
    assert(!cc_ascii_equal_ignore_case("Folder", "Folder/File"));
    assert(!cc_ascii_equal_ignore_case("", "a"));
    assert(cc_ascii_equal_ignore_case("Caf\xc3\xa9", "caf\xc3\xa9"));
    assert(!cc_ascii_equal_ignore_case("\xc3\x89", "\xc3\xa9"));
    assert(cc_ascii_hash_ignore_case("HELLO") == UINT64_C(0xa430d84680aabd0b));
    assert(cc_ascii_hash_ignore_case("Folder/File") ==
           cc_ascii_hash_ignore_case("folder/file"));
    puts("ASCII case comparison and hashing passed.");
    return 0;
}
