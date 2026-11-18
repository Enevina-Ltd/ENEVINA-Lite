from pathlib import Path
import unittest
BASE=Path(__file__).resolve().parents[1]
H=(BASE/"firmware/cottage/include/cottage_network_budget.hpp").read_text(encoding="utf-8")
S=(BASE/"firmware/cottage/src/cottage_network_budget.cpp").read_text(encoding="utf-8")
class NetworkBudgetDiagnosticTests(unittest.TestCase):
 def test_reason_capture_under_gate(self):
    self.assertIn("NetworkBudgetFailure::MutexBusy",S)
    self.assertIn("NetworkBudgetFailure::LowMemory",S)
    self.assertIn("if(detail)*detail=NetworkBudgetDiagnostic{};",S)
    self.assertIn("detail->free_internal=static_cast<uint32_t>(free);",S)
    self.assertIn("detail->largest_internal=static_cast<uint32_t>(largest);",S)
    self.assertLess(S.index("detail->free_internal="),S.index("xSemaphoreGive(gate);"))
    self.assertIn("detail->failure=NetworkBudgetFailure::LowMemory",S)
 def test_raii_lease_exposes_exact_cause(self):
    self.assertIn("NetworkBudgetDiagnostic* diagnostic=nullptr",H)
    self.assertIn("NetworkBudgetDiagnostic diagnostic_{};",H)
    self.assertIn("&diagnostic_",H)
    self.assertIn("diagnostic() const { return diagnostic_; }",H)
    self.assertIn("~NetworkBudgetLease() { if(held_) network_budget_release(); }",H)
if __name__=="__main__":unittest.main()
