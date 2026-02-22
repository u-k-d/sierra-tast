#include "sre/runtime.hpp"

#include <filesystem>
#include <iostream>
#include <vector>

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cerr << "usage: sre_planlint <plan.json> [diagnostics.jsonl] [summary.txt]\n";
    return 1;
  }

  const std::filesystem::path plan_path = argv[1];
  const std::filesystem::path jsonl_path =
      argc >= 3 ? std::filesystem::path(argv[2]) : std::filesystem::path("artifacts/diagnostics.ast_checks.jsonl");
  const std::filesystem::path summary_path =
      argc >= 4 ? std::filesystem::path(argv[3]) : std::filesystem::path("artifacts/diagnostics.ast_checks.txt");

  try {
    sre::Engine engine(std::filesystem::current_path());
    return engine.ValidatePlanFile(plan_path, jsonl_path, summary_path);
  } catch (const std::exception& ex) {
    std::cerr << "error: " << ex.what() << "\n";
    return 3;
  }
}

