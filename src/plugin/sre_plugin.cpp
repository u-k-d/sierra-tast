#include "sre/runtime.hpp"

#include <filesystem>

extern "C" __declspec(dllexport) int SREPlanLintFile(
    const char* plan_path,
    const char* diagnostics_jsonl_path,
    const char* diagnostics_summary_path) {
  try {
    if (plan_path == nullptr || diagnostics_jsonl_path == nullptr || diagnostics_summary_path == nullptr) {
      return 3;
    }
    const std::filesystem::path repo_root = std::filesystem::current_path();
    sre::Engine engine(repo_root);
    return engine.ValidatePlanFile(plan_path, diagnostics_jsonl_path, diagnostics_summary_path, {});
  } catch (...) {
    return 4;
  }
}

#if defined(SRE_ENABLE_ACSIL)
#include "sierrachart.h"
SCDLLName("SRE Runtime")

SCSFExport scsf_SREPlanLintBridge(SCStudyInterfaceRef sc) {
  if (sc.SetDefaults) {
    sc.GraphName = "SRE Plan Lint Bridge";
    sc.StudyDescription = "Validates SRE JSON plans against layer contracts and emits diagnostics.";
    sc.AutoLoop = 0;
    sc.FreeDLL = 0;

    sc.Input[0].Name = "Plan JSON File";
    sc.Input[0].SetPathAndFileName("");
    sc.Input[1].Name = "Diagnostics JSONL";
    sc.Input[1].SetPathAndFileName("artifacts/diagnostics.ast_checks.jsonl");
    sc.Input[2].Name = "Diagnostics Summary";
    sc.Input[2].SetPathAndFileName("artifacts/diagnostics.ast_checks.txt");
    return;
  }

  const int rc = SREPlanLintFile(sc.Input[0].GetString(), sc.Input[1].GetString(), sc.Input[2].GetString());
  if (rc == 0) {
    sc.AddMessageToLog("SRE validation passed.", 0);
  } else {
    SCString msg;
    msg.Format("SRE validation failed with code %d.", rc);
    sc.AddMessageToLog(msg, 1);
  }
}
#endif

