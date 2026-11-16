#include "graphics/host_gpu/renderer/pendingLabels.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {

using Libs::Graphics::PendingLabels;

void Check(bool value, const char *message) {
  if (!value) {
    std::fprintf(stderr, "PendingLabelsTests: failed: %s\n", message);
    std::abort();
  }
}

auto Equals(uint64_t expected) {
  return [expected](uint64_t value) { return value == expected; };
}

void TestElideAndStore() {
  PendingLabels labels;
  Check(!labels.HasElided() && labels.ElidedTick(0, UINT64_MAX) == 0,
        "empty list elided");
  const auto id = labels.Add(0x1000, 8, 5);
  labels.SetTick(id, 7);
  Check(!labels.Elide(0x1000, 8, Equals(4)),
        "elided for a value the label does not store");
  Check(!labels.HasElided(), "a refused wait counted as elided");
  Check(!labels.Elide(0x2000, 8, Equals(5)), "elided a wait on other memory");
  Check(labels.Elide(0x1000, 8, Equals(5)), "a satisfied wait was not elided");
  Check(labels.HasElided(), "the elided label is not reported");
  Check(labels.ElidedTick(0x1004, 1) == 7,
        "an access inside the label missed its tick");
  Check(labels.ElidedTick(0x1008, 8) == 0,
        "an access after the label found it");
  Check(labels.ElidedTick(0x0ff8, 8) == 0,
        "an access before the label found it");
  Check(labels.Elide(0x1000, 8, Equals(5)),
        "a second wait on the label was not elided");
  labels.Stored(id);
  Check(!labels.HasElided() && labels.ElidedTick(0x1000, 8) == 0 &&
            labels.Size() == 0,
        "a stored label stays elided");
}

// A read is told without the list's lock that no elided label can overlap it:
// by the pages the elided labels are on, counted as they come and go.
void TestElidedFilter() {
  PendingLabels labels;
  Check(!labels.MayOverlapElided(0x5ffc, 8), "an empty list may overlap a read");
  // One label across a page boundary, one elsewhere that stays elided.
  const auto id = labels.Add(0x5ffc, 8, 5);
  labels.SetTick(id, 1);
  const auto other = labels.Add(0xa000, 4, 1);
  labels.SetTick(other, 2);
  Check(!labels.MayOverlapElided(0x5ffc, 8),
        "a label nothing has elided a wait on counts");
  Check(labels.Elide(0x5ffc, 8, Equals(5)) && labels.Elide(0xa000, 4, Equals(1)),
        "the waits were not elided");
  Check(labels.MayOverlapElided(0x5000, 4) && labels.MayOverlapElided(0x6ff0, 16) &&
            labels.MayOverlapElided(0x5ffc, 8) && labels.MayOverlapElided(0xa800, 1),
        "a read on a page of an elided label is not sent to the list");
  Check(labels.MayOverlapElided(0x100000, 0x100000),
        "a read of many pages is not sent to the list");
  Check(!labels.MayOverlapElided(0x900000, 64) && !labels.MayOverlapElided(0x5ffc, 0),
        "a read on another page, or of nothing, is sent to the list");
  uint64_t away = 0;
  labels.PatchElided(0x900000, 8, &away);
  uint64_t at = 0;
  labels.PatchElided(0x5ffc, 8, &at);
  Check(away == 0 && at == 5, "the overlay changed with the filter");
  Check(labels.ElidedTick(0x900000, 8) == 0 && labels.ElidedTick(0x6000, 2) == 1,
        "the tick of an elided label changed with the filter");
  // The pages are given up when the label is stored, either way.
  labels.Stored(id);
  Check(!labels.MayOverlapElided(0x5ffc, 8) && labels.MayOverlapElided(0xa000, 4),
        "a stored label still counts, or took the other label's page with it");
  labels.StoreCompleted(0xa000, 4, 2, [](const PendingLabels::Label &) {});
  Check(!labels.MayOverlapElided(0xa000, 4) && !labels.HasElided(),
        "a label stored by a settling access still counts");
}

void TestPartialAndNewest() {
  PendingLabels labels;
  const auto low = labels.Add(0x1000, 8, 0x0000000200000001ull);
  labels.SetTick(low, 3);
  // The upper half of a 64-bit label, read as a 32-bit wait.
  Check(labels.Elide(0x1004, 4, Equals(2)),
        "a wait on the upper half was not elided");
  Check(!labels.Elide(0x1004, 8, Equals(2)),
        "a wait crossing the label's end was elided");
  // A later label of the same address wins: it is stored last.
  const auto high = labels.Add(0x1000, 4, 9);
  labels.SetTick(high, 4);
  Check(!labels.Elide(0x1000, 4, Equals(1)),
        "an overwritten value satisfied the wait");
  Check(labels.Elide(0x1000, 4, Equals(9)),
        "the newest value did not satisfy the wait");
  Check(labels.ElidedTick(0x1000, 4) == 4,
        "the elided tick is not the newest label's");
  labels.Stored(low);
  labels.Stored(high);
  Check(!labels.HasElided() && labels.Size() == 0,
        "labels left after both were stored");
}

void TestStoreCompleted() {
  PendingLabels labels;
  const auto first = labels.Add(0x1000, 4, 1);
  const auto second = labels.Add(0x1000, 4, 2);
  const auto later = labels.Add(0x1000, 4, 3);
  labels.SetTick(first, 5);
  labels.SetTick(second, 6);
  labels.SetTick(later, 9);
  Check(labels.Elide(0x1000, 4, Equals(3)),
        "the newest label did not satisfy the wait");
  std::vector<uint64_t> stored;
  labels.StoreCompleted(0x1000, 4, 6, [&](const PendingLabels::Label &label) {
    stored.push_back(label.value);
  });
  Check(stored == std::vector<uint64_t>{1, 2},
        "completed labels not stored in order");
  Check(!labels.ShouldStore(first) && !labels.ShouldStore(second),
        "the regular store would write a stored label again");
  Check(labels.ShouldStore(later) && labels.HasElided(),
        "a label of an incomplete submission was stored");
  labels.StoreCompleted(0x1000, 4, 9, [&](const PendingLabels::Label &label) {
    stored.push_back(label.value);
  });
  Check(stored == std::vector<uint64_t>{1, 2, 3} && !labels.HasElided(),
        "the elided label was not stored once completed");
  labels.Stored(first);
  labels.Stored(second);
  labels.Stored(later);
  Check(labels.Size() == 0 && labels.ShouldStore(first),
        "stored labels left in the list");
}

void TestPatchElided() {
  PendingLabels labels;
  const auto first = labels.Add(0x1004, 4, 0x11111111u);
  const auto second = labels.Add(0x1008, 8, 0x3333333322222222ull);
  const auto other = labels.Add(0x1010, 4, 0x44444444u);
  labels.SetTick(first, 1);
  labels.SetTick(second, 2);
  labels.SetTick(other, 3);
  uint32_t words[5] = {0, 0, 0, 0, 0};
  labels.PatchElided(0x1000, sizeof(words), words);
  Check(words[1] == 0 && words[2] == 0,
        "a label whose wait was not elided was overlaid");
  Check(labels.Elide(0x1004, 4, Equals(0x11111111u)) &&
            labels.Elide(0x100c, 4, Equals(0x33333333u)),
        "satisfied waits were not elided");
  labels.PatchElided(0x1000, sizeof(words), words);
  Check(words[0] == 0 && words[1] == 0x11111111u && words[2] == 0x22222222u &&
            words[3] == 0x33333333u && words[4] == 0,
        "elided labels were not overlaid exactly onto their bytes");
  uint8_t byte = 0;
  labels.PatchElided(0x100b, 1, &byte);
  Check(byte == 0x22, "a read inside a label got the wrong byte");
}

void TestUnknownValue() {
  PendingLabels labels;
  // A timestamp: its value is known only when its submission ends.
  const auto id = labels.Add(0x1000, 8, 5, false);
  labels.SetTick(id, 2);
  Check(!labels.Elide(0x1000, 8, Equals(5)) && !labels.HasElided(),
        "a wait was elided on a label whose value is not known yet");
  labels.SetValue(id, 9);
  Check(labels.Elide(0x1000, 8, Equals(9)),
        "a wait on a label with its final value was not elided");
  uint64_t stored = 0;
  labels.StoreCompleted(
      0x1000, 8, 2,
      [&stored](const PendingLabels::Label &label) { stored = label.value; });
  Check(stored == 9, "a settling access stored the provisional value");
}

} // namespace

void TestStoredValues() {
  PendingLabels labels;
  Check(!labels.StoredSince(0x1000, 4, 0, 0),
        "a value nobody stored counted as the GPU's");
  // The GPU stored 7 at 0x1000 in epoch 5.
  labels.NoteStoredValue(0x1000, 4, 7, 5);
  Check(labels.StoredSince(0x1000, 4, 7, 5) &&
            labels.StoredSince(0x1000, 4, 7, 3),
        "a GPU store made in or after the wait's epoch was not recognized");
  Check(!labels.StoredSince(0x1000, 4, 7, 6),
        "a GPU store made before the wait's submission counted as racing with "
        "it");
  Check(!labels.StoredSince(0x1000, 8, 7, 0),
        "a wait of another width matched the store");
  Check(!labels.StoredSince(0x1004, 4, 7, 0),
        "a wait at another address matched the store");
  // Another value: the bytes were written since, and stay unknown even if 7
  // comes back.
  Check(!labels.StoredSince(0x1000, 4, 9, 0),
        "another value counted as the GPU's store");
  Check(!labels.StoredSince(0x1000, 4, 7, 0),
        "the record survived a different value being observed");
  // A later GPU store replaces the record.
  labels.NoteStoredValue(0x1000, 4, 7, 5);
  labels.NoteStoredValue(0x1000, 4, 8, 6);
  Check(labels.StoredSince(0x1000, 4, 8, 6),
        "a later GPU store did not replace the earlier one");
  Check(!labels.StoredSince(0x1000, 4, 7, 0),
        "the earlier GPU store's value still matched");
  // Unmapped memory forgets its stores.
  labels.NoteStoredValue(0x2000, 8, 1, 2);
  labels.ForgetStored(0x2000, 0x1000);
  Check(!labels.StoredSince(0x2000, 8, 1, 0),
        "an unmapped label kept its store");
}

void TestStoredValuesBounded() {
  PendingLabels labels;
  // Titles store labels at ever new addresses (rings that move through
  // memory): the records must not grow with them.
  constexpr uint64_t count = 4 * PendingLabels::STORED_CAPACITY;
  for (uint64_t i = 0; i < count; i++) {
    labels.NoteStoredValue(0x10000000 + i * 8, 4, i, 1);
  }
  Check(labels.StoredRecords() <= PendingLabels::STORED_CAPACITY,
        "label store records grew with the addresses stored");
  Check(labels.StoredSince(0x10000000 + (count - 1) * 8, 4, count - 1, 1),
        "the latest label store was not recorded");
  // A record displaced by another address answers "not the GPU's store",
  // never a wrong match.
  uint64_t recognized = 0;
  for (uint64_t i = 0; i < count; i++) {
    recognized += labels.StoredSince(0x10000000 + i * 8, 4, i, 1) ? 1 : 0;
    Check(!labels.StoredSince(0x10000000 + i * 8, 4, i + 1, 0),
          "a displaced record matched another value");
  }
  Check(recognized <= PendingLabels::STORED_CAPACITY,
        "more stores recognized than records kept");
}

int main() {
  TestElideAndStore();
  TestElidedFilter();
  TestPartialAndNewest();
  TestStoreCompleted();
  TestPatchElided();
  TestUnknownValue();
  TestStoredValues();
  TestStoredValuesBounded();
  std::printf("PendingLabelsTests: ok\n");
  return 0;
}
