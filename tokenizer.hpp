#ifndef GUFO_MODELS_FISH_DRAMA3_TOKENIZER_HPP_
#define GUFO_MODELS_FISH_DRAMA3_TOKENIZER_HPP_

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace gufo::models::fish_drama3 {

class Drama3Tokenizer {
public:
  using TokenId = std::uint32_t;
  static constexpr TokenId kInvalidTokenId = 0xFFFFFFFFU;

  explicit Drama3Tokenizer(const std::filesystem::path& model_dir);

  [[nodiscard]] std::vector<std::int32_t> Encode(std::string_view input) const;

private:
  struct PairHash {
    std::size_t operator()(
        const std::pair<TokenId, TokenId>& p) const noexcept {
      return (static_cast<std::size_t>(p.first) << 32) |
             static_cast<std::size_t>(p.second);
    }
  };
  struct MergeRule {
    std::uint32_t rank;
    TokenId token;
  };

  [[nodiscard]] std::vector<TokenId> BpeMergeChunk(
      std::string_view chunk) const;

  std::string pattern_;
  std::unordered_map<std::string, TokenId> specials_;
  std::unordered_map<std::string, TokenId> token_to_id_;
  std::unordered_map<std::pair<TokenId, TokenId>, MergeRule, PairHash>
      merge_ranks_;
  std::array<TokenId, 256> byte_tokens_{};
};

}  // namespace gufo::models::fish_drama3

#endif  // GUFO_MODELS_FISH_DRAMA3_TOKENIZER_HPP_
