#include "console_common/support/host.h"
#include "console_common/support/process.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
    static const char *const values[] = {"",       "two words", "quote\"inside",
                                         "ends\\", "\\\"both",  "caf\xc3\xa9"};
    if (argc > 1 && !strcmp(argv[1], "--child")) {
        assert(argc == 8);
        for (size_t index = 0; index < 6; ++index)
            assert(!strcmp(argv[index + 2], values[index]));
        FILE *file = cc_host_fopen("child-output.txt", "wb");
        assert(file && fputs("okay", file) >= 0 && fclose(file) == 0);
        return 19;
    }
    assert(argc == 2);
    char *program = cc_host_executable_path();
    assert(program);
    char *arguments[9] = {program, "--child"};
    for (size_t index = 0; index < 6; ++index)
        arguments[index + 2] = (char *)values[index];
    assert(cc_process_run(program, argv[1], arguments, false) == 19);
    assert(cc_process_run(program, argv[1], arguments, true) == 19);
    assert(cc_process_run(program, "missing-working-directory", arguments, false) !=
           19);
    assert(cc_process_run("missing-executable", NULL, arguments, false) != 19);
    free(program);
    return 0;
}
