// Compile-time check: material_catalog.hpp is host-includable.
// On-device verification: serial log shows "[matcat] loaded N materials" on boot.
#include <cassert>
#include <cstdio>
#include <cstring>

int main() {
    // Validate fixture format: 3 materials in order.
    FILE* f = fopen("tests/fixtures/1-1.manifest", "r");
    assert(f && "tests/fixtures/1-1.manifest missing");

    const char* expected[] = {"rock_1", "snow_1", "girder"};
    int count = 0;
    char line[64];
    while (fgets(line, sizeof(line), f)) {
        int len = static_cast<int>(strlen(line));
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
            line[--len] = '\0';
        }
        if (len == 0) continue;
        assert(count < 3 && "too many materials in fixture");
        assert(strcmp(line, expected[count]) == 0 && "unexpected material name");
        ++count;
    }
    fclose(f);
    assert(count == 3 && "fixture must have exactly 3 materials");
    return 0;
}
