#include "predictfun/lifecycle/journal.hpp"

#include <cstdlib>
#include <iostream>

int main(int argc, char **argv) {
  if (argc != 2) {
    std::cerr << "usage: predictfun_recovery_example JOURNAL_PATH\n";
    return EXIT_FAILURE;
  }
  auto opened = predictfun::lifecycle::PersistentOrderTracker::open(argv[1]);
  if (!opened) {
    std::cerr << "journal recovery failed (code "
              << static_cast<int>(opened.error().code) << ")\n";
    return EXIT_FAILURE;
  }
  const auto tracker = std::move(opened.value());
  std::size_t terminal = 0U;
  std::size_t reconcile = 0U;
  for (const auto &order : tracker.snapshot()) {
    terminal += order.terminal() ? 1U : 0U;
    reconcile += order.reconciliation_required ? 1U : 0U;
  }
  std::cout << "orders=" << tracker.size() << " terminal=" << terminal
            << " reconciliation_required=" << reconcile
            << " recovered_records=" << tracker.recovered_records()
            << " repaired_torn_tail="
            << (tracker.ignored_truncated_tail() ? "yes" : "no") << '\n';
  std::cout << "Nonterminal orders remain quarantined until the host applies "
               "a complete authenticated REST order snapshot.\n";
  return EXIT_SUCCESS;
}
