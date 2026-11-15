#ifndef EMULATOR_INCLUDE_EMULATOR_GRAPHICS_SHADER_RECOMPILER_SPIRVDRIVERSIMPLIFY_H_
#define EMULATOR_INCLUDE_EMULATOR_GRAPHICS_SHADER_RECOMPILER_SPIRVDRIVERSIMPLIFY_H_

#include <cstddef>
#include <cstdint>
#include <vector>

namespace Libs::Graphics::ShaderRecompiler::Spirv {

struct DriverSimplifyStats {
	uint32_t speculated_branches = 0;
	uint32_t clamped_loads       = 0;
	uint32_t forwarded_operands  = 0;
	uint32_t removed             = 0;
	// Values replaced by an equal value: a local identity (KYTY_NO_SPV_PEEPHOLE) or a dominating
	// duplicate (KYTY_NO_SPV_CSE).
	uint32_t folded       = 0;
	uint32_t deduplicated = 0;
	// Blocks whose instructions were moved next to their users (KYTY_NO_SPV_SINK).
	uint32_t sunk_blocks = 0;
};

bool SimplifyForDriver(std::vector<uint32_t>& words, DriverSimplifyStats* stats = nullptr);

class SimplifiedStore;
// The store SimplifyForDriverStored uses (null: none); the caller keeps it alive while it is set.
void SetSimplifiedStore(SimplifiedStore* store);
// SimplifyForDriver through the store: a module simplified before (in this or an earlier session)
// takes the stored words, a new one is simplified and stored.
bool SimplifyForDriverStored(std::vector<uint32_t>& words);
// While one lives, the calling thread's translations neither read nor fill the store: for modules
// no session is known to need again (a precompile of shaders the game has not used). The store
// is read into memory by every session, so what goes in must earn its place (the modules of all
// of Saros' compute shaders are 1.4 GB).
class SimplifiedStoreBypass {
public:
	SimplifiedStoreBypass();
	~SimplifiedStoreBypass();
	SimplifiedStoreBypass(const SimplifiedStoreBypass&)            = delete;
	SimplifiedStoreBypass& operator=(const SimplifiedStoreBypass&) = delete;

private:
	bool m_previous;
};

uint32_t DriverSimplifyMinWords();
void     SetDriverSimplifyMinWords(uint32_t words);

// Whether an emitted module of `words` words goes through SimplifyForDriver: modules of at least
// `min_words` words, unless the profile turns the pass off (--no-shader-simplification).
constexpr bool SimplifiesForDriver(bool enabled, size_t words, uint32_t min_words) {
	return enabled && words >= min_words;
}

} // namespace Libs::Graphics::ShaderRecompiler::Spirv

#endif /* EMULATOR_INCLUDE_EMULATOR_GRAPHICS_SHADER_RECOMPILER_SPIRVDRIVERSIMPLIFY_H_ */
