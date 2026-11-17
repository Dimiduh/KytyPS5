#ifndef EMULATOR_GRAPHICS_SHADER_GAMESHADERSEEDS_H_
#define EMULATOR_GRAPHICS_SHADER_GAMESHADERSEEDS_H_

#include <cstdint>
#include <filesystem>
#include <string>

namespace Libs::Graphics {

// A game's shaders, read out of its own files into a seed file (shader.h, ShaderImportSeeds) for
// the precompile pass. What is read: the shader libraries of Unreal Engine 5 IoStore containers
// (*.utoc with *.ucas), wherever they lie under the game's folder or archive.
//
// The libraries keep their shaders in groups packed with Oodle. Unpacking is the work of a
// program of its own, ooz_decode (tools/ooz-decode, GPL-3.0-or-later): it is started with a file
// of packed groups and writes a file of raw ones. None of its code is part of the emulator.
struct GameShaderSeeds {
	enum class State {
		// The game's files hold no shader library.
		None,
		// The seed file is there, and it was made from these files or by someone else (a file
		// with no note of its sources next to it is never replaced).
		Kept,
		Written,
		// Nothing was written; an older seed file stays as it is.
		Failed,
	};
	State    state     = State::None;
	uint32_t libraries = 0;
	// Distinct shader binaries in the seed file.
	uint64_t shaders   = 0;
	uint64_t bytes     = 0;
	// Library entries without an AGC shader binary.
	uint64_t no_shader = 0;
	// Failed: why.
	std::string reason;
};

// Writes `seeds` from the files under `game` unless it is there and up to date: next to the seed
// file, "<seeds>.source" names the containers it was made from and their sizes.
// decoder: the ooz_decode program; without it only libraries with unpacked groups are read.
GameShaderSeeds ShaderWriteGameSeeds(const std::filesystem::path& game,
                                     const std::filesystem::path& seeds,
                                     const std::filesystem::path& decoder);
// ooz_decode next to the emulator's executable (whether or not it is there).
std::filesystem::path ShaderSeedDecoderPath();

} // namespace Libs::Graphics

#endif /* EMULATOR_GRAPHICS_SHADER_GAMESHADERSEEDS_H_ */
