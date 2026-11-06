#ifndef GUFO_MODELS_FISH_DRAMA3_HOST_MEMORY_HPP_
#define GUFO_MODELS_FISH_DRAMA3_HOST_MEMORY_HPP_

namespace gufo::models::fish_drama3 {

/// Asks the OS to drop this process's resident pages that it can reload on
/// demand, such as file-backed safetensors mapping pages whose contents are
/// already on the device. Windows keeps every touched page of a read-only
/// mapping in the working set until physical memory runs out; there this
/// trims the working set. Elsewhere the kernel reclaims such pages on its
/// own, so this is a no-op.
void TrimResidentHostPages() noexcept;

}  // namespace gufo::models::fish_drama3

#endif  // GUFO_MODELS_FISH_DRAMA3_HOST_MEMORY_HPP_
