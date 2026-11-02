#include <gufo/ext/registry.hpp>
#include <gufo/ext/tts.hpp>
#include <string>
#include <string_view>

#include "config.hpp"

namespace gufo::models::fish_drama3 {

bool ProbeFishDrama3(std::string_view model_root) {
  return LooksLikeFishDrama3(std::string(model_root));
}

bool ResolveFishDrama3Artifacts(std::string_view model_root,
                                std::string* error) {
  if (!LooksLikeFishDrama3(std::string(model_root))) {
    if (error != nullptr) {
      *error = "unsupported Fish Drama-3 model in " + std::string(model_root);
    }
    return false;
  }
  return true;
}

}  // namespace gufo::models::fish_drama3

void fish_drama3_register(gufo::ext::Registry& registry) {
  using namespace gufo::models::fish_drama3;
  registry.AddTtsProvider({
      .id = "fish-drama3-tts",
      .probe = &ProbeFishDrama3,
      .resolve = &ResolveFishDrama3Artifacts,
      .create = nullptr,
  });
}
