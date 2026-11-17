// ooz_decode: decodes Kraken, Mermaid, Selkie and Leviathan streams with the ooz decompressor
// (https://github.com/powzix/ooz, GPL-3.0-or-later). A program of its own: it shares no code with
// the emulator, which starts it and exchanges two files with it.
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
//   ooz_decode <input file> <output file>
//
// Input:  "OOZDEC1" and a zero byte, then one record per stream: its packed size and its raw size
//         (u32 each, little endian) and the packed bytes.
// Output: the raw bytes of every stream, one after the other in the order of the input.
// Exit code: 0 when every stream decoded to its raw size; otherwise nothing of the output is to be
// used (1 arguments, 2 input, 3 a stream that does not decode, 4 output).
//
// The decoder trusts its input (it is not hardened against damaged streams): give it a game's
// own files only.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

// ooz, kraken.cpp. The output buffer needs 64 spare bytes after dst_len: the decoder writes past
// the end.
int Kraken_Decompress(const unsigned char* src, size_t src_len, unsigned char* dst, size_t dst_len);

namespace {

constexpr char   kMagic[8]   = {'O', 'O', 'Z', 'D', 'E', 'C', '1', '\0'};
constexpr size_t kSafeSpace  = 64;
// A record larger than this is taken for a damaged file, not for a stream.
constexpr uint32_t kMaxSize  = 1u << 30;

struct File {
	FILE* f = nullptr;
	~File() {
		if (f != nullptr) {
			fclose(f);
		}
	}
};

} // namespace

int wmain(int argc, wchar_t** argv) {
	if (argc != 3) {
		fprintf(stderr, "usage: ooz_decode <input file> <output file>\n");
		return 1;
	}
	File in;
	in.f = _wfopen(argv[1], L"rb");
	if (in.f == nullptr) {
		fprintf(stderr, "ooz_decode: the input file cannot be opened\n");
		return 2;
	}
	char magic[sizeof(kMagic)] = {};
	if (fread(magic, 1, sizeof(magic), in.f) != sizeof(magic) || memcmp(magic, kMagic, sizeof(magic)) != 0) {
		fprintf(stderr, "ooz_decode: the input file is no list of streams\n");
		return 2;
	}
	File out;
	out.f = _wfopen(argv[2], L"wb");
	if (out.f == nullptr) {
		fprintf(stderr, "ooz_decode: the output file cannot be written\n");
		return 4;
	}
	std::vector<unsigned char> packed;
	std::vector<unsigned char> raw;
	uint64_t                   streams = 0;
	for (;;) {
		uint32_t     sizes[2] = {};
		const size_t got      = fread(sizes, 1, sizeof(sizes), in.f);
		if (got == 0) {
			break;
		}
		if (got != sizeof(sizes) || sizes[0] == 0 || sizes[0] > kMaxSize || sizes[1] == 0 || sizes[1] > kMaxSize) {
			fprintf(stderr, "ooz_decode: stream %llu has no valid sizes\n", static_cast<unsigned long long>(streams));
			return 2;
		}
		packed.resize(sizes[0]);
		raw.resize(sizes[1] + kSafeSpace);
		if (fread(packed.data(), 1, packed.size(), in.f) != packed.size()) {
			fprintf(stderr, "ooz_decode: stream %llu ends before its size\n", static_cast<unsigned long long>(streams));
			return 2;
		}
		const int decoded = Kraken_Decompress(packed.data(), packed.size(), raw.data(), sizes[1]);
		if (decoded < 0 || static_cast<uint32_t>(decoded) != sizes[1]) {
			fprintf(stderr, "ooz_decode: stream %llu does not decode (%d of %u bytes)\n",
			        static_cast<unsigned long long>(streams), decoded, sizes[1]);
			return 3;
		}
		if (fwrite(raw.data(), 1, sizes[1], out.f) != sizes[1]) {
			fprintf(stderr, "ooz_decode: the output file cannot be written\n");
			return 4;
		}
		streams++;
	}
	if (fflush(out.f) != 0) {
		fprintf(stderr, "ooz_decode: the output file cannot be written\n");
		return 4;
	}
	return 0;
}
