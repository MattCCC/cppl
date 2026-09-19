// Content digests back every semantic identity in the compiler, so they must
// be correct and reproducible, not merely consistent with themselves.

#include "cppl/source/digest.hpp"
#include "cppl/testing/test.hpp"

#include <string>

namespace {

std::string digest_of(std::string_view text) {
    return cppl::source::hash_bytes(text).to_hex();
}

}  // namespace

CPPL_TEST(digest_matches_the_published_sha256_vectors) {
    CPPL_CHECK_EQ(digest_of(""),
                  std::string("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
    CPPL_CHECK_EQ(digest_of("abc"),
                  std::string("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    CPPL_CHECK_EQ(digest_of("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
                  std::string("248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"));
}

CPPL_TEST(digest_spans_more_than_one_block) {
    std::string long_message(1000, 'a');
    CPPL_CHECK_EQ(digest_of(long_message).size(), std::size_t{64});
    CPPL_CHECK_EQ(digest_of(long_message), digest_of(std::string(1000, 'a')));
}

CPPL_TEST(field_encoding_separates_differently_split_content) {
    cppl::source::Hasher first;
    first.update_field("ab");
    first.update_field("c");

    cppl::source::Hasher second;
    second.update_field("a");
    second.update_field("bc");

    CPPL_CHECK(!(first.finish() == second.finish()));
}

CPPL_TEST(the_same_content_always_digests_the_same) {
    cppl::source::Hasher first;
    first.update_u64(7);
    first.update_field("law");

    cppl::source::Hasher second;
    second.update_u64(7);
    second.update_field("law");

    CPPL_CHECK(first.finish() == second.finish());
}
