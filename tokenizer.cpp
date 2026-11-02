#include "tokenizer.hpp"

#include <gufo/ext/json.hpp>
#include <unicode/normalizer2.h>
#include <unicode/regex.h>
#include <unicode/unistr.h>

#include <cmath>
#include <fstream>
#include <limits>
#include <queue>
#include <sstream>
#include <stdexcept>

namespace gufo::models::fish_drama3 {
namespace {

namespace json = gufo::json;
constexpr std::uint32_t kMaxTokenId = 262144;

json::Value ReadJsonFile(const std::filesystem::path& path) {
  std::ifstream input(path);
  if (!input.is_open()) {
    throw std::runtime_error("Cannot open " + path.string());
  }
  std::stringstream buffer;
  buffer << input.rdbuf();
  return json::parse(buffer.str());
}

std::string ByteToGpt2Utf8(std::uint8_t b) {
  static const auto b2u_map = []() {
    std::array<std::string, 256> table;
    for (int i = '!'; i <= '~'; ++i)
      table[i] = std::string(1, static_cast<char>(i));
    for (int i = 161; i <= 172; ++i) {
      char buf[3] = {static_cast<char>(0xC0 | (i >> 6)),
                     static_cast<char>(0x80 | (i & 0x3F)), 0};
      table[i] = buf;
    }
    for (int i = 174; i <= 255; ++i) {
      char buf[3] = {static_cast<char>(0xC0 | (i >> 6)),
                     static_cast<char>(0x80 | (i & 0x3F)), 0};
      table[i] = buf;
    }
    int n = 0;
    for (int i = 0; i < 256; ++i) {
      if ((i < '!' || i > '~') && (i < 161 || i > 172) &&
          (i < 174 || i > 255)) {
        const int cp = 256 + n;
        char buf[3] = {static_cast<char>(0xC0 | (cp >> 6)),
                       static_cast<char>(0x80 | (cp & 0x3F)), 0};
        table[i] = buf;
        ++n;
      }
    }
    return table;
  }();
  return b2u_map[b];
}

}  // namespace

Drama3Tokenizer::Drama3Tokenizer(const std::filesystem::path& model_dir) {
  const auto config = ReadJsonFile(model_dir / "tokenizer.json");
  const auto* model = config.find("model");
  const auto* vocab = model ? model->find("vocab") : nullptr;
  const auto* merges = model ? model->find("merges") : nullptr;
  const auto* added = config.find("added_tokens");
  const auto* pre = config.find("pre_tokenizer");
  const auto* sequence = pre ? pre->find("pretokenizers") : nullptr;
  if (!vocab || !vocab->is_object() || !merges || !merges->is_array() ||
      !added || !added->is_array() || !sequence || !sequence->is_array() ||
      sequence->size() != 2) {
    throw std::runtime_error("Invalid Drama 3 tokenizer.json");
  }

  const auto& split = sequence->items()[0];
  const auto require_false = [](const json::Value& object, const char* key) {
    const auto* value = object.find(key);
    if (!value || !value->is_bool() || value->as_bool()) {
      throw std::runtime_error(
          std::string("Unsupported Drama 3 tokenizer setting: ") + key);
    }
  };
  const auto* regex = split.find("pattern");
  if (split.member_str("type") != "Split" || !regex ||
      sequence->items()[1].member_str("type") != "ByteLevel") {
    throw std::runtime_error("Unsupported Drama 3 pre-tokenizer");
  }
  if (model->member_str("type") != "BPE" ||
      split.member_str("behavior") != "Isolated") {
    throw std::runtime_error("Unsupported Drama 3 BPE configuration");
  }
  require_false(split, "invert");
  require_false(sequence->items()[1], "add_prefix_space");
  require_false(sequence->items()[1], "use_regex");
  require_false(*model, "byte_fallback");
  require_false(*model, "ignore_merges");

  pattern_ = regex->member_str("Regex");
  if (pattern_.empty()) {
    throw std::runtime_error("Missing Drama 3 tokenizer regex");
  }
  if (const auto* normalizer = config.find("normalizer");
      !normalizer || normalizer->member_str("type") != "NFC") {
    throw std::runtime_error("Unsupported Drama 3 tokenizer normalizer");
  }

  const auto token_id = [](const json::Value* value) {
    if (!value || !value->is_number() || !std::isfinite(value->as_double()) ||
        value->as_double() < 0 || value->as_double() >= kMaxTokenId ||
        value->as_double() != std::floor(value->as_double())) {
      throw std::runtime_error("Invalid Drama 3 token ID");
    }
    return static_cast<TokenId>(value->as_double());
  };

  token_to_id_.reserve(vocab->members().size() + 256);
  for (const auto& [word, id] : vocab->members()) {
    const TokenId tid = token_id(&id);
    if (word.empty()) {
      throw std::runtime_error("Invalid Drama 3 tokenizer entry");
    }
    token_to_id_[word] = tid;
  }
  for (const auto& entry : added->items()) {
    for (const char* key : {"single_word", "lstrip", "rstrip", "normalized"}) {
      require_false(entry, key);
    }
    const auto id = token_id(entry.find("id"));
    const auto word = entry.member_str("content");
    if (word.starts_with("<|semantic:")) {
      continue;
    }
    if (word.empty()) {
      throw std::runtime_error("Invalid Drama 3 added token entry");
    }
    token_to_id_[word] = id;
    specials_.emplace(word, id);
  }

  for (std::size_t b = 0; b < 256; ++b) {
    const std::string gpt2_utf8 = ByteToGpt2Utf8(static_cast<std::uint8_t>(b));
    const auto it = token_to_id_.find(gpt2_utf8);
    byte_tokens_[b] = (it != token_to_id_.end()) ? it->second : kInvalidTokenId;
  }

  merge_ranks_.reserve(merges->size());
  for (std::uint32_t rank = 0; rank < merges->size(); ++rank) {
    const auto& merge = merges->items()[rank];
    std::string part1;
    std::string part2;
    if (merge.is_string()) {
      const auto& s = merge.str();
      const auto pos = s.find(' ');
      if (pos == std::string::npos) {
        throw std::runtime_error("Invalid Drama 3 tokenizer merge");
      }
      part1 = s.substr(0, pos);
      part2 = s.substr(pos + 1);
    } else if (merge.is_array() && merge.size() == 2 &&
               merge.items()[0].is_string() && merge.items()[1].is_string()) {
      part1 = merge.items()[0].str();
      part2 = merge.items()[1].str();
    } else {
      throw std::runtime_error("Invalid Drama 3 tokenizer merge");
    }
    const auto it1 = token_to_id_.find(part1);
    const auto it2 = token_to_id_.find(part2);
    if (it1 != token_to_id_.end() && it2 != token_to_id_.end()) {
      const auto merged = token_to_id_.find(part1 + part2);
      merge_ranks_[{it1->second, it2->second}] = {
          .rank = rank,
          .token = (merged != token_to_id_.end()) ? merged->second
                                                  : kInvalidTokenId,
      };
    }
  }
}

std::vector<Drama3Tokenizer::TokenId> Drama3Tokenizer::BpeMergeChunk(
    std::string_view chunk) const {
  if (chunk.empty()) {
    return {};
  }
  std::vector<TokenId> word_tokens;
  word_tokens.reserve(chunk.size());
  for (const unsigned char c : chunk) {
    const TokenId tid = byte_tokens_[c];
    if (tid == kInvalidTokenId) {
      throw std::runtime_error("Missing byte token in Drama 3 vocabulary");
    }
    word_tokens.push_back(tid);
  }
  if (word_tokens.size() <= 1) {
    return word_tokens;
  }

  constexpr std::size_t kNoSymbol = std::numeric_limits<std::size_t>::max();
  const std::size_t count = word_tokens.size();
  std::vector<std::size_t> previous(count);
  std::vector<std::size_t> following(count);
  for (std::size_t i = 0; i < count; ++i) {
    previous[i] = i == 0 ? kNoSymbol : i - 1;
    following[i] = i + 1 < count ? i + 1 : kNoSymbol;
  }

  struct Candidate {
    std::uint32_t rank;
    std::size_t index;
    TokenId left;
    TokenId right;
  };
  const auto later = [](const Candidate& a, const Candidate& b) {
    return a.rank != b.rank ? a.rank > b.rank : a.index > b.index;
  };
  std::priority_queue<Candidate, std::vector<Candidate>, decltype(later)>
      candidates(later);

  const auto offer = [&](std::size_t index, std::size_t right) {
    const auto merge =
        merge_ranks_.find({word_tokens[index], word_tokens[right]});
    if (merge != merge_ranks_.end()) {
      candidates.push({
          .rank = merge->second.rank,
          .index = index,
          .left = word_tokens[index],
          .right = word_tokens[right],
      });
    }
  };
  for (std::size_t i = 0; i + 1 < count; ++i) {
    offer(i, i + 1);
  }

  while (!candidates.empty()) {
    const Candidate best = candidates.top();
    candidates.pop();
    const std::size_t right = following[best.index];
    if (word_tokens[best.index] != best.left || right == kNoSymbol ||
        word_tokens[right] != best.right) {
      continue;
    }
    const auto merge = merge_ranks_.find({best.left, best.right});
    if (merge == merge_ranks_.end() || merge->second.token == kInvalidTokenId) {
      break;
    }
    word_tokens[best.index] = merge->second.token;
    const std::size_t after = following[right];
    following[best.index] = after;
    if (after != kNoSymbol) {
      previous[after] = best.index;
    }
    word_tokens[right] = kInvalidTokenId;
    if (previous[best.index] != kNoSymbol) {
      offer(previous[best.index], best.index);
    }
    if (after != kNoSymbol) {
      offer(best.index, after);
    }
  }

  std::vector<TokenId> merged;
  merged.reserve(count);
  for (std::size_t i = 0; i != kNoSymbol; i = following[i]) {
    merged.push_back(word_tokens[i]);
  }
  return merged;
}

std::vector<std::int32_t> Drama3Tokenizer::Encode(
    std::string_view input) const {
  std::vector<std::int32_t> result;
  UErrorCode status = U_ZERO_ERROR;
  const auto pattern =
      std::unique_ptr<icu::RegexPattern>(icu::RegexPattern::compile(
          icu::UnicodeString::fromUTF8(pattern_), 0, status));
  if (U_FAILURE(status) || !pattern) {
    throw std::runtime_error("Invalid Drama 3 tokenizer regex");
  }

  while (!input.empty()) {
    std::size_t end = input.size();
    std::size_t length = 0;
    TokenId token = 0;
    for (const auto& [special, id] : specials_) {
      const auto offset = input.find(special);
      if (offset < end || (offset == end && special.size() > length)) {
        end = offset;
        length = special.size();
        token = id;
      }
    }

    const auto original = icu::UnicodeString::fromUTF8(
        icu::StringPiece(input.data(), static_cast<int>(end)));
    icu::UnicodeString text;
    const auto* normalizer = icu::Normalizer2::getNFCInstance(status);
    if (U_FAILURE(status)) {
      throw std::runtime_error("NFC initialization failed");
    }
    normalizer->normalize(original, text, status);
    const auto matcher =
        std::unique_ptr<icu::RegexMatcher>(pattern->matcher(text, status));
    if (U_FAILURE(status) || !matcher) {
      throw std::runtime_error("Drama 3 Unicode regex matching failed");
    }

    int consumed = 0;
    while (matcher->find(status)) {
      if (matcher->start(status) != consumed) {
        throw std::runtime_error("Drama 3 tokenizer left unmatched text");
      }
      std::string piece;
      matcher->group(status).toUTF8String(piece);
      const auto ids = BpeMergeChunk(piece);
      for (const auto id : ids) {
        result.push_back(static_cast<std::int32_t>(id));
      }
      consumed = matcher->end(status);
    }
    if (U_FAILURE(status) || consumed != text.length()) {
      throw std::runtime_error("Drama 3 tokenization failed");
    }
    if (!length) {
      break;
    }
    result.push_back(static_cast<std::int32_t>(token));
    input.remove_prefix(end + length);
  }
  return result;
}

}  // namespace gufo::models::fish_drama3
