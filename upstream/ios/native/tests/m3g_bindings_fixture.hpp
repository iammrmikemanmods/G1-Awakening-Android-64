#pragma once

// Generated only from the frozen original execution fixture.
// SHA256: d6d39b71189bdd1ed6eb53fff342d516f2fabcf4f5d04d534fd2921f6a33300a
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace g1_binding_test {
struct NameCase { std::string_view hex; bool transform_suffix; std::uint32_t expected; };
inline constexpr std::array<NameCase,34> names{{
    {"",false,0U},
    {"",true,432179325U},
    {"636172",false,847635835U},
    {"636172",true,1012044474U},
    {"726f626f74",false,590208487U},
    {"726f626f74",true,838542512U},
    {"726f626f43414d",false,418881596U},
    {"726f626f43414d",true,32417421U},
    {"7665686943414d",false,363220926U},
    {"7665686943414d",true,749685101U},
    {"6361725472616e73666f726d",false,1012044474U},
    {"6361725472616e73666f726d",true,175292595U},
    {"726f626f745472616e73666f726d",false,838542512U},
    {"726f626f745472616e73666f726d",true,991697477U},
    {"726f626f43414d5472616e73666f726d",false,32417421U},
    {"726f626f43414d5472616e73666f726d",true,696205898U},
    {"7665686943414d5472616e73666f726d",false,749685101U},
    {"7665686943414d5472616e73666f726d",true,85896624U},
    {"63616d5f6175746f5f74616b65686974",false,60006714U},
    {"63616d5f6175746f5f74616b65686974",true,723795191U},
    {"63616d5f6175746f5f636172",false,857178138U},
    {"63616d5f6175746f5f636172",true,102968441U},
    {"63616d5f64656370745f7365747570",false,379293131U},
    {"63616d5f64656370745f7365747570",true,909353412U},
    {"526f6f744e6f6465",false,421721094U},
    {"526f6f744e6f6465",true,97887255U},
    {"61",false,1013904320U},
    {"61",true,385300327U},
    {"6162",false,285817485U},
    {"6162",true,272118792U},
    {"414243",false,333871425U},
    {"414243",true,169462786U},
    {"ff",false,1013904222U},
    {"ff",true,385300229U},
}};
struct TreeRow { std::size_t index; std::uint32_t user_id; std::vector<std::uint32_t> children; };
struct TreeCase { std::string_view name; std::vector<TreeRow> rows; std::size_t root; std::uint32_t key; std::optional<std::size_t> expected; };
inline const std::array<TreeCase,6> trees{{
    {"self_before_nested_duplicate",{{1,42U,{2U}},{2,42U,{}}},1,42U,std::size_t{1}},
    {"first_sibling_depth_first",{{1,1U,{2U,4U}},{2,2U,{3U}},{3,42U,{}},{4,42U,{}}},1,42U,std::size_t{3}},
    {"second_sibling_after_nonmatch",{{1,1U,{2U,4U}},{2,2U,{3U}},{3,3U,{}},{4,42U,{}}},1,42U,std::size_t{4}},
    {"missing",{{1,1U,{2U}},{2,2U,{}}},1,42U,std::nullopt},
    {"scoped_subtree",{{1,1U,{2U,3U}},{2,42U,{}},{3,42U,{}}},3,42U,std::size_t{3}},
    {"resource490_duplicate_shape",{{332,421721094U,{331U}},{331,1012044474U,{330U}},{330,1012044474U,{}}},332,1012044474U,std::size_t{331}},
}};
} // namespace g1_binding_test
