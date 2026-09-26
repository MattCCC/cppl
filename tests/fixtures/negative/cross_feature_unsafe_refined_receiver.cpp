// SPEC: CLASS-010, UNSAFE-005, REFINE-060
// `Cursor::setting` of fixtures/cross_feature/buffers.cpp on a cursor whose
// member is refined. An unsafe block may reach the object, so after it the
// member's value is not known, and the refinement owed for it at return is not
// proven. An unsafe block never re-establishes what it may have broken. The
// accepted cursor's member is not refined.
#include <cstddef>

type Position = std::size_t where (self <= 4096ul);

unsafe std::size_t read_setting();

struct Cursor {
    Position at;

    verified std::size_t setting() const
        ensures (result <= 4096ul)
    {
        std::size_t value = 0ul;
        unsafe {
            value = read_setting();
        }
        if (value > 4096ul) {
            return 4096ul;
        }
        return value;
    }
};

unsafe std::size_t read_setting() {
    return 64ul;
}
