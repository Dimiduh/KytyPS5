#include "graphics/shader/gameShaderSeeds.h"

#include "common/common.h"
#include "common/file.h"
#include "common/logging/log.h"
#include "common/stringUtils.h"

#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_set>
#include <utility>
#include <vector>
#include <xxhash.h>

#if KYTY_PLATFORM == KYTY_PLATFORM_WINDOWS
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <spawn.h>
#include <sys/wait.h>
extern char** environ; // NOLINT
#endif

namespace Libs::Graphics {

namespace {

// EIoChunkType (Unreal Engine 5), the last byte of a chunk id.
constexpr uint8_t ChunkShaderLibrary = 8;
constexpr uint8_t ChunkShaderCode    = 9;
// EIoContainerFlags.
constexpr uint8_t ContainerEncrypted = 2;
// Sizes past these are taken for damaged files (ooz_decode has the same limit for a group).
constexpr uint64_t MaxToc     = 1ull << 30u;
constexpr uint64_t MaxLibrary = 1ull << 30u;
constexpr uint32_t MaxGroup   = 1u << 30u;
// Raw bytes of groups handed to the decoder in one run.
constexpr uint64_t BatchRaw = 128ull << 20u;
constexpr int      MaxDepth = 12;
// The start of an AGC shader binary: "1234", header version 0x18.
constexpr std::array<uint8_t, 8> ShaderSignature = {'1', '2', '3', '4', 0x18, 0, 0, 0};

uint32_t U32(const uint8_t* at) {
	uint32_t value = 0;
	std::memcpy(&value, at, sizeof(value));
	return value;
}

uint64_t U64(const uint8_t* at) {
	uint64_t value = 0;
	std::memcpy(&value, at, sizeof(value));
	return value;
}

bool ReadAt(Common::File& file, uint64_t offset, uint8_t* data, uint64_t size) {
	if (!file.Seek(offset)) {
		return false;
	}
	while (size != 0) {
		const auto part = static_cast<uint32_t>(std::min<uint64_t>(size, 64u << 20u));
		uint32_t   got  = 0;
		file.Read(data, part, &got);
		if (got == 0 || got > part) {
			return false;
		}
		data += got;
		size -= got;
	}
	return true;
}

bool ReadWhole(const std::filesystem::path& path, uint64_t limit, std::vector<uint8_t>& out) {
	Common::File file;
	if (!file.Open(path, Common::File::Mode::Read)) {
		return false;
	}
	const auto size = file.Size();
	if (size > limit) {
		return false;
	}
	out.resize(size);
	return size == 0 || ReadAt(file, 0, out.data(), size);
}

std::filesystem::path WithSuffix(const std::filesystem::path& path, const char* suffix) {
	auto result = path;
	result += suffix;
	return result;
}

void Remove(const std::filesystem::path& path) {
	std::error_code ignored;
	std::filesystem::remove(path, ignored);
}

// One container: its table of contents and the first file of its data.
struct Container {
	std::filesystem::path toc;
	std::filesystem::path cas;
	// The path under the game, with forward slashes: what the order and the source note go by.
	std::string name;
	uint64_t    toc_size = 0;
	uint64_t    cas_size = 0;
};

bool EndsWithNoCase(std::string_view text, std::string_view end) {
	return text.size() >= end.size() && Common::EqualNoCase(text.substr(text.size() - end.size()), end);
}

void FindContainers(const std::filesystem::path& folder, const std::string& prefix, int depth,
                    std::vector<Container>& found) {
	for (const auto& entry: Common::File::GetDirEntries(folder)) {
		if (entry.name == "." || entry.name == "..") {
			continue;
		}
		const auto path = folder / Common::PathFromUtf8(entry.name);
		if (!entry.is_file) {
			if (depth < MaxDepth) {
				FindContainers(path, prefix + entry.name + "/", depth + 1, found);
			}
			continue;
		}
		if (!EndsWithNoCase(entry.name, ".utoc")) {
			continue;
		}
		Container container;
		container.toc = path;
		container.cas = path;
		container.cas.replace_extension(".ucas");
		container.name     = prefix + entry.name;
		container.toc_size = Common::File::Size(container.toc);
		container.cas_size = Common::File::Size(container.cas);
		found.push_back(std::move(container));
	}
}

// What a table of contents says of the container's shader chunks.
struct Toc {
	using Id = std::array<uint8_t, 12>;
	struct Chunk {
		uint64_t offset = 0;
		uint64_t length = 0;
	};
	struct Block {
		uint64_t offset = 0;
		uint32_t packed = 0;
		uint32_t raw    = 0;
		uint8_t  method = 0;
	};
	uint32_t block_size     = 0;
	// 0: one data file.
	uint64_t partition_size = 0;
	uint8_t  flags          = 0;
	// Shader libraries in the table's order, and shader groups by id.
	std::vector<Chunk>  libraries;
	std::map<Id, Chunk> groups;
	std::vector<Block>  blocks;
};

uint64_t Bytes(const uint8_t* at, int count, bool big_endian) {
	uint64_t value = 0;
	for (int i = 0; i < count; i++) {
		value |= static_cast<uint64_t>(at[i]) << (8u * static_cast<unsigned>(big_endian ? count - 1 - i : i));
	}
	return value;
}

// FIoStoreTocHeader and the tables after it. nullptr, or what is wrong with the file.
const char* ParseToc(const std::vector<uint8_t>& toc, Toc& out) {
	constexpr uint64_t HeaderSize = 144;
	if (toc.size() < HeaderSize || std::memcmp(toc.data(), "-==--==--==--==-", 16) != 0) {
		return "a .utoc file is no IoStore table of contents";
	}
	const uint8_t  version     = toc[16];
	const uint64_t header_size = U32(&toc[20]);
	const uint64_t entries     = U32(&toc[24]);
	const uint64_t blocks      = U32(&toc[28]);
	const uint64_t block_entry = U32(&toc[32]);
	out.block_size             = U32(&toc[44]);
	const uint32_t partitions  = U32(&toc[52]);
	out.flags                  = toc[80];
	const uint64_t seeds       = version >= 4 ? U32(&toc[84]) : 0;
	out.partition_size         = version >= 3 && partitions > 1 ? U64(&toc[88]) : 0;
	const uint64_t overflow    = version >= 5 ? U32(&toc[96]) : 0;
	uint64_t       pos         = header_size;
	const uint64_t ids         = pos;
	pos += entries * 12;
	const uint64_t ranges = pos;
	pos += entries * 10 + seeds * 4 + overflow * 4;
	const uint64_t block_table = pos;
	pos += blocks * block_entry;
	if (header_size < HeaderSize || block_entry != 12 || out.block_size == 0 || pos > toc.size()) {
		return "a .utoc file has a table of contents this build does not read";
	}
	for (uint64_t i = 0; i < entries; i++) {
		const uint8_t* id = &toc[ids + i * 12];
		if (id[11] != ChunkShaderLibrary && id[11] != ChunkShaderCode) {
			continue;
		}
		const uint8_t*   range = &toc[ranges + i * 10];
		const Toc::Chunk chunk {Bytes(range, 5, true), Bytes(range + 5, 5, true)};
		if (id[11] == ChunkShaderLibrary) {
			out.libraries.push_back(chunk);
		} else {
			Toc::Id key {};
			std::memcpy(key.data(), id, key.size());
			out.groups[key] = chunk;
		}
	}
	if (out.libraries.empty()) {
		return nullptr;
	}
	out.blocks.resize(blocks);
	for (uint64_t i = 0; i < blocks; i++) {
		const uint8_t* entry = &toc[block_table + i * 12];
		out.blocks[i] = {Bytes(entry, 5, false), static_cast<uint32_t>(Bytes(entry + 5, 3, false)),
		                 static_cast<uint32_t>(Bytes(entry + 8, 3, false)), entry[11]};
	}
	return nullptr;
}

// A container's data files: "<name>.ucas", and "<name>_s<n>.ucas" for its further partitions.
struct Cas {
	std::filesystem::path                      first;
	uint64_t                                   partition_size = 0;
	std::vector<std::unique_ptr<Common::File>> parts;

	bool Read(uint64_t offset, uint8_t* data, uint64_t size) {
		const uint64_t index = partition_size != 0 ? offset / partition_size : 0;
		const uint64_t at    = partition_size != 0 ? offset % partition_size : offset;
		if (index > 255) {
			return false;
		}
		if (parts.size() <= index) {
			parts.resize(index + 1);
		}
		if (parts[index] == nullptr) {
			auto path = first;
			if (index != 0) {
				path.replace_extension();
				path += "_s" + std::to_string(index);
				path += first.extension();
			}
			auto file = std::make_unique<Common::File>();
			if (!file->Open(path, Common::File::Mode::Read)) {
				return false;
			}
			parts[index] = std::move(file);
		}
		return ReadAt(*parts[index], at, data, size);
	}
};

const char* ReadChunk(const Toc& toc, Cas& cas, const Toc::Chunk& chunk, uint64_t limit,
                      std::vector<uint8_t>& out) {
	if (chunk.length == 0 || chunk.length > limit) {
		return "a shader chunk has an unlikely size";
	}
	out.resize(chunk.length);
	uint64_t done = 0;
	while (done < chunk.length) {
		const uint64_t at    = chunk.offset + done;
		const uint64_t index = at / toc.block_size;
		if (index >= toc.blocks.size()) {
			return "a shader chunk lies outside its container";
		}
		const auto& block = toc.blocks[index];
		if (block.method != 0 || block.packed != block.raw) {
			return "the game's containers are packed as a whole, which this build does not read";
		}
		const uint64_t within = at % toc.block_size;
		if (within >= block.raw) {
			return "a shader chunk lies outside its container";
		}
		const uint64_t part = std::min<uint64_t>(block.raw - within, chunk.length - done);
		if (!cas.Read(block.offset + within, out.data() + done, part)) {
			return "a container's data could not be read";
		}
		done += part;
	}
	return nullptr;
}

// A shader library (FIoStoreShaderCodeArchiveHeader): its groups and where each shader lies in
// its group's raw bytes.
struct Library {
	struct Group {
		Toc::Id  id {};
		uint32_t raw    = 0;
		uint32_t packed = 0;
	};
	struct Member {
		uint32_t group  = 0;
		uint32_t offset = 0;
		uint32_t index  = 0;
	};
	std::vector<Group> groups;
	// By group, then by offset; first[g] .. first[g + 1] are the members of group g.
	std::vector<Member>   members;
	std::vector<uint32_t> first;
};

const char* ParseLibrary(const std::vector<uint8_t>& data, Library& out) {
	uint64_t pos = 0;
	bool     ok  = data.size() >= 4;
	// A serialized array: its count, then that many items of a fixed size.
	const auto array = [&](uint64_t item, uint64_t& count) -> const uint8_t* {
		if (!ok || data.size() - pos < 4) {
			ok = false;
			return nullptr;
		}
		const auto n = static_cast<int32_t>(U32(&data[pos]));
		pos += 4;
		if (n < 0 || (data.size() - pos) / item < static_cast<uint64_t>(n)) {
			ok = false;
			return nullptr;
		}
		const uint8_t* at = data.data() + pos;
		pos += static_cast<uint64_t>(n) * item;
		count = static_cast<uint64_t>(n);
		return at;
	};
	if (ok && U32(data.data()) != 1) {
		return "a shader library has a version this build does not read";
	}
	pos = 4;
	uint64_t       maps = 0, hashes = 0, ids = 0, entries = 0, shaders = 0, groups = 0, indices = 0;
	(void)array(20, maps);                           // shader map hashes
	(void)array(20, hashes);                         // shader hashes
	const uint8_t* group_ids = array(12, ids);       // the groups' chunk ids
	(void)array(8, entries);                         // shader map entries
	const uint8_t* shader_at = array(8, shaders);    // frequency : 4, group : 30, offset in group : 30
	const uint8_t* group_at  = array(16, groups);    // indices offset, shaders, raw size, packed size
	(void)array(4, indices);                         // shader indices
	if (!ok || ids != groups) {
		return "a shader library is not laid out as this build reads it";
	}
	out.groups.resize(groups);
	for (uint64_t g = 0; g < groups; g++) {
		std::memcpy(out.groups[g].id.data(), group_ids + g * 12, 12);
		out.groups[g].raw    = U32(group_at + g * 16 + 8);
		out.groups[g].packed = U32(group_at + g * 16 + 12);
	}
	out.members.reserve(shaders);
	for (uint64_t i = 0; i < shaders; i++) {
		const uint64_t packed = U64(shader_at + i * 8);
		const auto     group  = static_cast<uint32_t>((packed >> 4u) & 0x3fffffffu);
		if (group < groups) {
			out.members.push_back({group, static_cast<uint32_t>(packed >> 34u), static_cast<uint32_t>(i)});
		}
	}
	std::sort(out.members.begin(), out.members.end(), [](const auto& a, const auto& b) {
		return a.group != b.group ? a.group < b.group : a.offset != b.offset ? a.offset < b.offset : a.index < b.index;
	});
	out.first.assign(groups + 1, 0);
	for (const auto& member: out.members) {
		out.first[member.group + 1]++;
	}
	for (uint64_t g = 0; g < groups; g++) {
		out.first[g + 1] += out.first[g];
	}
	return nullptr;
}

// The decoder's exit code; -1 when it did not start or did not end.
int RunDecoder(const std::filesystem::path& decoder, const std::filesystem::path& input,
               const std::filesystem::path& output) {
#if KYTY_PLATFORM == KYTY_PLATFORM_WINDOWS
	// (No path holds a quote, and none of the three ends with a backslash.)
	std::wstring command =
	    L"\"" + decoder.wstring() + L"\" \"" + input.wstring() + L"\" \"" + output.wstring() + L"\"";
	STARTUPINFOW startup {};
	startup.cb = sizeof(startup);
	PROCESS_INFORMATION process {};
	if (CreateProcessW(decoder.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
	                   nullptr, nullptr, &startup, &process) == 0) {
		return -1;
	}
	CloseHandle(process.hThread);
	DWORD      code  = 1;
	const bool ended = WaitForSingleObject(process.hProcess, 10u * 60u * 1000u) == WAIT_OBJECT_0 &&
	                   GetExitCodeProcess(process.hProcess, &code) != 0;
	if (!ended) {
		TerminateProcess(process.hProcess, 1);
	}
	CloseHandle(process.hProcess);
	return ended ? static_cast<int>(code) : -1;
#else
	std::string arguments[3] = {decoder.string(), input.string(), output.string()};
	char*       argv[4]      = {arguments[0].data(), arguments[1].data(), arguments[2].data(), nullptr};
	pid_t       child        = 0;
	if (posix_spawn(&child, arguments[0].c_str(), nullptr, nullptr, argv, environ) != 0) {
		return -1;
	}
	int status = 0;
	if (waitpid(child, &status, 0) != child || !WIFEXITED(status)) {
		return -1;
	}
	return WEXITSTATUS(status);
#endif
}

struct Key {
	uint64_t low  = 0;
	uint64_t high = 0;
	bool     operator==(const Key&) const = default;
};
struct KeyHash {
	size_t operator()(const Key& key) const { return static_cast<size_t>(key.low); }
};

// Writes the seed file: every distinct shader once, in the order the libraries give them.
class Writer {
public:
	Writer(std::filesystem::path seeds, std::filesystem::path decoder)
	    : m_seeds(std::move(seeds)), m_decoder(std::move(decoder)), m_temp(WithSuffix(m_seeds, ".tmp")),
	      m_input(WithSuffix(m_seeds, ".decode-in")), m_output(WithSuffix(m_seeds, ".decode-out")) {}
	~Writer() {
		m_file.close();
		m_packed.close();
		Remove(m_temp);
		Remove(m_input);
		Remove(m_output);
	}
	KYTY_CLASS_NO_COPY(Writer);

	const char* Add(const Toc& toc, Cas& cas, const Library& library) {
		if (!m_file.is_open()) {
			std::error_code ignored;
			std::filesystem::create_directories(m_seeds.parent_path(), ignored);
			m_file.open(m_temp, std::ios::binary | std::ios::trunc);
			if (!m_file.is_open()) {
				return "the seed file could not be written";
			}
			const uint32_t count = 0;
			m_file.write("KytyShaderSeeds1", 16);
			m_file.write(reinterpret_cast<const char*>(&count), sizeof(count));
			m_bytes = 16 + sizeof(count);
		}
		std::vector<uint8_t> data;
		for (uint32_t g = 0; g < library.groups.size(); g++) {
			const auto& group = library.groups[g];
			const auto  chunk = toc.groups.find(group.id);
			if (chunk == toc.groups.end() || chunk->second.length != group.packed || group.raw == 0 ||
			    group.raw > MaxGroup || group.packed > group.raw) {
				return "a shader library names a group its container does not hold";
			}
			if (const char* error = ReadChunk(toc, cas, chunk->second, MaxGroup, data)) {
				return error;
			}
			if (group.packed == group.raw) {
				m_pending.push_back({g, group.raw, std::move(data)});
				data = {};
			} else {
				if (!m_packed.is_open()) {
					std::error_code missing;
					if (m_decoder.empty() || !std::filesystem::exists(m_decoder, missing)) {
						return "the shader libraries are packed with Oodle, and the program that unpacks "
						       "them (ooz_decode) is not next to the emulator";
					}
					m_packed.open(m_input, std::ios::binary | std::ios::trunc);
					if (!m_packed.is_open()) {
						return "the decoder's input could not be written";
					}
					m_packed.write("OOZDEC1", 8);
				}
				const uint32_t sizes[2] = {group.packed, group.raw};
				m_packed.write(reinterpret_cast<const char*>(sizes), sizeof(sizes));
				m_packed.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
				m_pending.push_back({g, group.raw, {}});
			}
			m_pending_raw += group.raw;
			if (m_pending_raw >= BatchRaw) {
				if (const char* error = Flush(library)) {
					return error;
				}
			}
		}
		return Flush(library);
	}

	// Ends the file and puts it in place.
	const char* Finish() {
		if (!m_file.is_open() || m_shaders == 0) {
			return "the shader libraries hold no shader binary this build reads";
		}
		m_file.seekp(16);
		const auto count = static_cast<uint32_t>(m_shaders);
		m_file.write(reinterpret_cast<const char*>(&count), sizeof(count));
		m_file.close();
		if (m_file.fail()) {
			return "the seed file could not be written";
		}
		std::error_code error;
		std::filesystem::rename(m_temp, m_seeds, error);
		return error ? "the seed file could not be put in place" : nullptr;
	}

	[[nodiscard]] uint64_t Shaders() const { return m_shaders; }
	[[nodiscard]] uint64_t Bytes() const { return m_bytes; }
	[[nodiscard]] uint64_t NoShader() const { return m_no_shader; }

private:
	struct Pending {
		uint32_t             group = 0;
		uint32_t             raw   = 0;
		// The group's bytes when it is stored unpacked; a packed group's come from the decoder.
		std::vector<uint8_t> stored;
	};

	const char* Flush(const Library& library) {
		std::ifstream raw;
		if (m_packed.is_open()) {
			m_packed.close();
			if (m_packed.fail()) {
				return "the decoder's input could not be written";
			}
			const int code = RunDecoder(m_decoder, m_input, m_output);
			if (code != 0) {
				return code == 3    ? "a shader group does not unpack: not a stream the decoder knows"
				       : code == -1 ? "the decoder (ooz_decode) could not be run"
				                    : "the decoder (ooz_decode) failed";
			}
			raw.open(m_output, std::ios::binary);
		}
		std::vector<uint8_t> data;
		for (auto& pending: m_pending) {
			if (!pending.stored.empty()) {
				Cut(library, pending.group, pending.stored);
				continue;
			}
			data.resize(pending.raw);
			raw.read(reinterpret_cast<char*>(data.data()), pending.raw);
			if (raw.gcount() != static_cast<std::streamsize>(pending.raw)) {
				return "the decoder's output is shorter than the groups it was given";
			}
			Cut(library, pending.group, data);
		}
		raw.close();
		Remove(m_input);
		Remove(m_output);
		m_pending.clear();
		m_pending_raw = 0;
		return m_file.fail() ? "the seed file could not be written" : nullptr;
	}

	// The shaders of one group: each from its offset to the next one's.
	void Cut(const Library& library, uint32_t group, std::span<const uint8_t> raw) {
		const uint32_t end = library.first[group + 1];
		for (uint32_t m = library.first[group]; m < end; m++) {
			const uint64_t from = std::min<uint64_t>(library.members[m].offset, raw.size());
			const uint64_t to   = m + 1 < end ? std::min<uint64_t>(library.members[m + 1].offset, raw.size()) : raw.size();
			const auto     blob = to > from ? raw.subspan(from, to - from) : std::span<const uint8_t> {};
			const auto     at   = std::search(blob.begin(), blob.end(), ShaderSignature.begin(), ShaderSignature.end());
			const auto     left = static_cast<uint64_t>(blob.end() - at);
			if (left < 0x48) {
				m_no_shader++;
				continue;
			}
			const uint8_t* binary      = &*at;
			const uint64_t header_size = U32(binary + 0x40);
			const uint64_t code_size   = U32(binary + 0x44);
			if (header_size < 0x60 || header_size > left || code_size > left - header_size || code_size == 0 ||
			    code_size % 4 != 0) {
				m_no_shader++;
				continue;
			}
			const auto hash = XXH3_128bits(binary, header_size + code_size);
			if (!m_seen.insert({hash.low64, hash.high64}).second) {
				continue;
			}
			const uint32_t sizes[2] = {static_cast<uint32_t>(header_size), static_cast<uint32_t>(code_size)};
			m_file.write(reinterpret_cast<const char*>(sizes), sizeof(sizes));
			m_file.write(reinterpret_cast<const char*>(binary), static_cast<std::streamsize>(header_size + code_size));
			m_bytes += sizeof(sizes) + header_size + code_size;
			m_shaders++;
		}
	}

	std::filesystem::path           m_seeds;
	std::filesystem::path           m_decoder;
	std::filesystem::path           m_temp;
	std::filesystem::path           m_input;
	std::filesystem::path           m_output;
	std::ofstream                   m_file;
	std::ofstream                   m_packed;
	std::vector<Pending>            m_pending;
	uint64_t                        m_pending_raw = 0;
	std::unordered_set<Key, KeyHash> m_seen;
	uint64_t                        m_shaders   = 0;
	uint64_t                        m_bytes     = 0;
	uint64_t                        m_no_shader = 0;
};

} // namespace

std::filesystem::path ShaderSeedDecoderPath() {
	const char* base = SDL_GetBasePath();
	if (base == nullptr) {
		return {};
	}
	// SDL returns UTF-8; path(const char*) would read it in the Windows ANSI code page.
#if KYTY_PLATFORM == KYTY_PLATFORM_WINDOWS
	return Common::PathFromUtf8(base) / "ooz_decode.exe";
#else
	return Common::PathFromUtf8(base) / "ooz_decode";
#endif
}

GameShaderSeeds ShaderWriteGameSeeds(const std::filesystem::path& game, const std::filesystem::path& seeds,
                                     const std::filesystem::path& decoder) {
	GameShaderSeeds        result;
	std::vector<Container> containers;
	FindContainers(game, "", 0, containers);
	if (containers.empty()) {
		return result;
	}
	std::sort(containers.begin(), containers.end(), [](const auto& a, const auto& b) { return a.name < b.name; });
	std::string sources = "KytyShaderSeedsSource1\n";
	for (const auto& container: containers) {
		sources += container.name + "\t" + std::to_string(container.toc_size) + "\t" +
		           std::to_string(container.cas_size) + "\n";
	}
	const auto      note = WithSuffix(seeds, ".source");
	std::error_code error;
	if (std::filesystem::exists(seeds, error)) {
		std::ifstream file(note, std::ios::binary);
		const std::string old {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
		if (!file.is_open() || old == sources) {
			result.state = GameShaderSeeds::State::Kept;
			return result;
		}
	}
	const auto fail = [&](const char* reason) {
		result.state  = GameShaderSeeds::State::Failed;
		result.reason = reason;
		return result;
	};
	Writer               writer(seeds, decoder);
	std::vector<uint8_t> bytes;
	for (const auto& container: containers) {
		Toc toc;
		if (!ReadWhole(container.toc, MaxToc, bytes)) {
			return fail("a .utoc file could not be read");
		}
		if (const char* reason = ParseToc(bytes, toc)) {
			return fail(reason);
		}
		if (toc.libraries.empty()) {
			continue;
		}
		if ((toc.flags & ContainerEncrypted) != 0) {
			return fail("the game's containers are encrypted");
		}
		if (result.libraries == 0) {
			Log::WriteToConsoleAndLog("Shader seeds: reading the shader libraries of the game's files\n");
		}
		Cas cas {container.cas, toc.partition_size, {}};
		for (const auto& chunk: toc.libraries) {
			Library library;
			if (const char* reason = ReadChunk(toc, cas, chunk, MaxLibrary, bytes)) {
				return fail(reason);
			}
			if (const char* reason = ParseLibrary(bytes, library)) {
				return fail(reason);
			}
			if (const char* reason = writer.Add(toc, cas, library)) {
				return fail(reason);
			}
			result.libraries++;
		}
	}
	if (result.libraries == 0) {
		return result;
	}
	result.no_shader = writer.NoShader();
	if (const char* reason = writer.Finish()) {
		return fail(reason);
	}
	{
		std::ofstream file(note, std::ios::binary | std::ios::trunc);
		file.write(sources.data(), static_cast<std::streamsize>(sources.size()));
	}
	result.state   = GameShaderSeeds::State::Written;
	result.shaders = writer.Shaders();
	result.bytes   = writer.Bytes();
	return result;
}

} // namespace Libs::Graphics
