#pragma once

#include "graphics/shader/recompiler/frontend/cfg/ShaderCFG.h"
#include "graphics/shader/recompiler/frontend/decode/ShaderDecoder.h"
#include "graphics/shader/recompiler/ir/ShaderIR.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Libs::Graphics::ShaderRecompiler::Frontend {

struct EmbeddedFetchLoad {
	uint32_t              pc         = 0;
	int                   attrib_id  = -1;
	uint32_t              components = 0;
};

struct EmbeddedFetchPlan {
	std::vector<EmbeddedFetchLoad> loads;
	int32_t                        vertex_offset_sgpr   = -1;
	int32_t                        instance_offset_sgpr = -1;
};

struct TranslateOptions {
	ShaderType                    stage               = ShaderType::Unknown;
	uint32_t                      wave_size           = 64;
	uint64_t                      shader_hash         = 0;
	uint32_t                      user_data_base      = 0;
	uint32_t                      user_data_count     = 64;
	ShaderStageInputInfo          input_info;
	const EmbeddedFetchPlan*      embedded_fetch = nullptr;
};

IR::Program TranslateProgram(const Decoder::Program& decoded, const CFG::Graph& cfg,
                             const TranslateOptions& options);

// Whether wave64 compute programs the lane-local proof admits are translated one lane per
// invocation (default: on; KYTY_LANE_LOCAL=0 turns it off). Set before anything is translated.
void               SetLaneLocalTranslation(bool enabled);
[[nodiscard]] bool LaneLocalTranslation();

// A wave64 program whose results in each lane depend on no lane outside its 32-lane half
// (LaneLocal.cpp): on a host with 32-wide subgroups it runs one GCN lane per invocation, each
// subgroup a half wave, instead of two lanes per invocation. `reason`: why not.
bool IsWaveLaneLocal(const Decoder::Program& program, std::string* reason = nullptr);

// What makes a wave64 program lane-local (LaneLocal.cpp), for a 64-thread group whose two halves
// cooperate (`cooperative`): the lane reads the wave runs through workgroup memory and the
// lane-mask branches the wave decides (by pc). The translator does not emit such exchanges yet:
// it takes programs IsWaveLaneLocal proves.
struct LaneLocalProof {
	std::vector<uint32_t> exchange_reads;
	std::vector<uint32_t> wave_branches;
	[[nodiscard]] bool    Cooperative() const {
		return !exchange_reads.empty() || !wave_branches.empty();
	}
};
bool ProveWaveLaneLocal(const Decoder::Program& program, bool cooperative, LaneLocalProof& proof,
                        std::string* reason = nullptr);

} // namespace Libs::Graphics::ShaderRecompiler::Frontend
