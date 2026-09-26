// SPEC: CLASS-010, CLASS-011, STDMODEL-015, REFINE-060
// `Cursor::emit` of fixtures/cross_feature/buffers.hpp on a cursor whose
// member is refined. The object's members are caller storage the function owes
// their refinement for at return, and no disjointness of the object and the
// vector it pushes to is assumed from their types, so after `push_back` the
// member's value is not known and its refinement is not proven. The accepted
// cursor's member is not refined.
#include <cstddef>
#include <vector>

type Position = std::size_t where (self <= 4096ul);

struct Cursor {
    Position at;

    verified std::size_t emit(std::vector<unsigned>& out, unsigned value) const
        ensures (result == out.size() && 1ul <= result)
    {
        out.push_back(value);
        return out.size();
    }
};
