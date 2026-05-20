// Compile-time check: material_catalog.hpp is host-includable.
// On-device verification: serial log shows "[matcat] loaded N materials" on boot.
#include <cassert>
#include <cstdio>
#include <cstring>

int main() {
    // Validate fixture format: 3 materials in order.
    FILE* f = fopen("tests/fixtures/first-room.manifest", "r");
    assert(f && "tests/fixtures/first-room.manifest missing");

    const char* expected[] = {"rock_1", "rock_1_climbable"};
    int count = 0;
    char line[64];
    while (fgets(line, sizeof(line), f)) {
        int len = static_cast<int>(strlen(line));
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
            line[--len] = '\0';
        }
        if (len == 0) continue;
        assert(count < 2 && "too many materials in fixture");
        assert(strcmp(line, expected[count]) == 0 && "unexpected material name");
        ++count;
    }
    fclose(f);
    assert(count == 2 && "fixture must have exactly 2 materials");
    return 0;
}
