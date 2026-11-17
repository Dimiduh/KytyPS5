#include "graphics/host_gpu/filteredRangeSet.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <map>
#include <utility>
#include <vector>

namespace {

using Libs::Graphics::FilteredRangeSet;
using Libs::Graphics::RangeSet;
using Ranges = std::vector<std::pair<uint64_t, uint64_t>>;

void Check(bool value, const char *message) {
  if (!value) {
    std::fprintf(stderr, "FilteredRangeSetTests: failed: %s\n", message);
    std::abort();
  }
}

struct Random {
  uint64_t state;
  uint64_t Next() {
    state ^= state << 13u;
    state ^= state >> 7u;
    state ^= state << 17u;
    return state;
  }
  uint64_t Below(uint64_t bound) { return Next() % bound; }
};

uint64_t RandomSize(Random &random) {
  switch (random.Below(4)) {
  case 0: return 1 + random.Below(256);                     // a few dwords
  case 1: return 1 + random.Below(uint64_t{1} << 20u);      // up to a fine-filter range
  case 2: return 1 + random.Below(uint64_t{1} << 28u);      // coarse granules
  default: return uint64_t{1} << (12u + random.Below(30)); // up to 2 TiB: saturates
  }
}

// Every answer equals the plain set's, through adds, subtracts, rebuilds and clears.
void TestMatchesRangeSet() {
  Random random{0x1234567887654321ull};
  FilteredRangeSet filtered;
  RangeSet plain;
  constexpr uint64_t span = uint64_t{1} << 40u;
  for (uint32_t step = 0; step < 200000; ++step) {
    const auto address = 0x10000000ull + random.Below(span);
    const auto size = RandomSize(random);
    const auto op = random.Below(16);
    if (op < 3) {
      filtered.Add(address, size);
      plain.Add(address, size);
    } else if (op < 7) {
      filtered.Subtract(address, size);
      plain.Subtract(address, size);
    } else if (op == 7 && random.Below(64) == 0) {
      filtered.Clear();
      plain.Clear();
    } else {
      Check(filtered.Intersects(address, size) == plain.Intersects(address, size),
            "Intersects disagreed with the plain set");
      Check(filtered.Contains(address, size) == plain.Contains(address, size),
            "Contains disagreed with the plain set");
      std::vector<std::pair<uint64_t, uint64_t>> a;
      std::vector<std::pair<uint64_t, uint64_t>> b;
      filtered.ForEachInRange(address, size, [&](uint64_t x, uint64_t y) { a.emplace_back(x, y); });
      plain.ForEachInRange(address, size, [&](uint64_t x, uint64_t y) { b.emplace_back(x, y); });
      Check(a == b, "ForEachInRange disagreed with the plain set");
    }
    Check(filtered.Empty() == plain.Empty(), "Empty disagreed with the plain set");
  }
}

// The set as a tree of one node per range: what RangeSet's blocks have to answer like.
class TreeRangeSet {
public:
  bool Add(uint64_t address, uint64_t size) {
    const auto end = address + size;
    auto it = m_ranges.lower_bound(address);
    if (it != m_ranges.begin() && std::prev(it)->second >= address) {
      it = std::prev(it);
    }
    if (it == m_ranges.end() || it->first > address) {
      it = m_ranges.emplace_hint(it, address, end);
    } else {
      if (it->second >= end) {
        return false;
      }
      it->second = end;
    }
    auto next = std::next(it);
    while (next != m_ranges.end() && next->first <= it->second) {
      it->second = std::max(it->second, next->second);
      next = m_ranges.erase(next);
    }
    return true;
  }
  void Subtract(uint64_t address, uint64_t size) {
    const auto end = address + size;
    auto it = m_ranges.lower_bound(address);
    if (it != m_ranges.begin() && std::prev(it)->second > address) {
      it = std::prev(it);
    }
    while (it != m_ranges.end() && it->first < end) {
      const auto begin = it->first;
      const auto last = it->second;
      it = m_ranges.erase(it);
      if (begin < address) {
        m_ranges.emplace(begin, address);
      }
      if (last > end) {
        m_ranges.emplace(end, last);
        break;
      }
    }
  }
  [[nodiscard]] bool Intersects(uint64_t address, uint64_t size) const {
    const auto end = address + size;
    auto it = m_ranges.lower_bound(address);
    if (it != m_ranges.begin() && std::prev(it)->second > address) {
      return true;
    }
    return it != m_ranges.end() && it->first < end;
  }
  [[nodiscard]] bool Contains(uint64_t address, uint64_t size) const {
    auto it = m_ranges.upper_bound(address);
    if (it == m_ranges.begin()) {
      return false;
    }
    --it;
    return it->first <= address && it->second >= address + size;
  }
  [[nodiscard]] Ranges All() const { return {m_ranges.begin(), m_ranges.end()}; }
  [[nodiscard]] Ranges InRange(uint64_t address, uint64_t size) const {
    Ranges result;
    const auto end = address + size;
    auto it = m_ranges.upper_bound(address);
    if (it != m_ranges.begin()) {
      --it;
    }
    for (; it != m_ranges.end() && it->first < end; ++it) {
      if (std::max(address, it->first) < std::min(end, it->second)) {
        result.emplace_back(std::max(address, it->first), std::min(end, it->second));
      }
    }
    return result;
  }
  void Clear() { m_ranges.clear(); }

private:
  std::map<uint64_t, uint64_t> m_ranges;
};

Ranges All(const RangeSet &set) {
  Ranges result;
  set.ForEach([&](uint64_t begin, uint64_t end) { result.emplace_back(begin, end); });
  return result;
}

// Every answer of the blocks equals the tree's: with a few ranges and with thousands (many
// blocks, split and joined), through ranges that swallow hundreds of others and cuts across
// blocks.
void TestBlocksMatchTree(uint64_t seed, uint64_t span, uint64_t max_size, uint32_t steps,
                         uint64_t wide_one_in) {
  Random random{seed};
  RangeSet set;
  TreeRangeSet tree;
  size_t most = 0;
  for (uint32_t step = 0; step < steps; ++step) {
    auto address = 0x10000000ull + random.Below(span);
    auto size = 1 + random.Below(max_size);
    const auto op = random.Below(32);
    if (op == 0 && random.Below(wide_one_in) == 0) {
      size = 1 + random.Below(span / 8); // swallows or cuts many
    }
    if (op < 14) {
      // (More adds than cuts, so that the set grows; one in four next to a range of the set.)
      if (random.Below(4) == 0 && !set.Empty()) {
        const auto all = tree.InRange(address, span);
        if (!all.empty()) {
          address = all.front().second;
        }
      }
      Check(set.Add(address, size) == tree.Add(address, size), "Add's answer disagreed with the tree");
    } else if (op < 22) {
      set.Subtract(address, size);
      tree.Subtract(address, size);
    } else if (op == 22 && random.Below(2000) == 0) {
      set.Clear();
      tree.Clear();
    } else {
      Check(set.Intersects(address, size) == tree.Intersects(address, size),
            "Intersects disagreed with the tree");
      Check(set.Contains(address, size) == tree.Contains(address, size),
            "Contains disagreed with the tree");
      Ranges found;
      set.ForEachInRange(address, size, [&](uint64_t x, uint64_t y) { found.emplace_back(x, y); });
      Check(found == tree.InRange(address, size), "ForEachInRange disagreed with the tree");
    }
    if (step % 64 == 0) {
      const auto all = All(set);
      most = std::max(most, all.size());
      Check(all == tree.All(), "the ranges disagreed with the tree");
      Check(set.Empty() == all.empty(), "Empty disagreed with the ranges");
    }
  }
  Check(All(set) == tree.All(), "the ranges disagreed with the tree at the end");
  Check(most >= 2, "the set never held ranges");
  std::printf("FilteredRangeSetTests: blocks seed %llu: up to %zu ranges\n",
              static_cast<unsigned long long>(seed), most);
}

// A sweep with a cursor gives what the plain queries give, and the union of two sets is the set
// both were added to.
void TestSweepAndUnion() {
  Random random{0x9e3779b97f4a7c15ull};
  RangeSet a;
  RangeSet b;
  RangeSet both;
  constexpr uint64_t span = uint64_t{1} << 26u;
  for (uint32_t i = 0; i < 6000; ++i) {
    const auto address = 0x10000000ull + random.Below(span);
    const auto size = 1 + random.Below(4096);
    (random.Below(2) == 0 ? a : b).Add(address, size);
    both.Add(address, size);
    if (random.Below(8) == 0) {
      const auto cut = 0x10000000ull + random.Below(span);
      a.Subtract(cut, 20000);
      b.Subtract(cut, 20000);
      both.Subtract(cut, 20000);
    }
  }
  Ranges merged;
  a.ForEachUnion(b, [&](uint64_t x, uint64_t y) { merged.emplace_back(x, y); });
  Check(merged == All(both), "ForEachUnion is not the set both were added to");
  Ranges swept;
  Ranges plain;
  RangeSet::Cursor cursor;
  for (uint64_t at = 0x10000000ull; at < 0x10000000ull + span; at += 1024) {
    a.ForEachInRange(at, 1024, cursor, [&](uint64_t x, uint64_t y) { swept.emplace_back(x, y); });
    a.ForEachInRange(at, 1024, [&](uint64_t x, uint64_t y) { plain.emplace_back(x, y); });
  }
  Check(swept == plain && !swept.empty(), "a sweep with a cursor disagreed with the plain queries");
  RangeSet empty;
  const auto before = All(a);
  a.Swap(empty);
  Check(a.Empty() && All(empty) == before, "Swap did not exchange the sets");
  Ranges of_one;
  empty.ForEachUnion(a, [&](uint64_t x, uint64_t y) { of_one.emplace_back(x, y); });
  Check(of_one == before, "the union with an empty set is not the set");
}

// Small dirty ranges among many disjoint queries: queries next to them stay negative, queries
// touching a byte of them are positive, and a subtracted range stops intersecting.
void TestNearbyQueries() {
  FilteredRangeSet set;
  set.Add(0x500000100ull, 4);
  set.Add(0x600000000ull, uint64_t{1} << 30u); // coarse granules
  Check(set.Intersects(0x500000100ull, 1) && set.Intersects(0x5000000f0ull, 0x11),
        "a query touching a small range missed it");
  Check(!set.Intersects(0x500000104ull, 0x80) && !set.Intersects(0x5000000c0ull, 0x40),
        "a query next to a small range intersected it");
  Check(set.Intersects(0x63fffff80ull, 0x80) && !set.Intersects(0x640000000ull, 0x80),
        "a query at the end of a large range was misjudged");
  set.Subtract(0x500000100ull, 4);
  Check(!set.Intersects(0x500000100ull, 1), "a subtracted range still intersected");
  set.Subtract(0x600000000ull, uint64_t{1} << 30u);
  Check(set.Empty() && !set.Intersects(0x600000000ull, 1), "an emptied set still intersected");
}

} // namespace

int main() {
  TestMatchesRangeSet();
  TestNearbyQueries();
  // A few ranges; thousands of small ones (dozens of blocks); wide ones that keep merging.
  TestBlocksMatchTree(0x1111111111111111ull, uint64_t{1} << 16u, 64, 300000, 1);
  TestBlocksMatchTree(0x2222222222222222ull, uint64_t{1} << 26u, 512, 600000, 400);
  TestBlocksMatchTree(0x3333333333333333ull, uint64_t{1} << 30u, uint64_t{1} << 16u, 300000, 8);
  TestSweepAndUnion();
  std::printf("FilteredRangeSetTests: ok\n");
  return 0;
}
