from pathlib import Path
import re
import unittest
BASE=Path(__file__).resolve().parents[1]
H=(BASE/"firmware/cottage/include/cottage_network_budget.hpp").read_text(encoding="utf-8")
S=(BASE/"firmware/cottage/src/cottage_network_budget.cpp").read_text(encoding="utf-8")
G=(BASE/"firmware/cottage/src/cottage_geocode.cpp").read_text(encoding="utf-8")
class NetworkBudgetDiagnosticTests(unittest.TestCase):
 def test_geocode_admits_the_observed_internal_heap_range(self):
    match=re.search(r"NetworkBudgetLease lease\(8000,\s*(\d+)\s*\*\s*1024,\s*(\d+)\s*\*\s*1024\)",G)
    self.assertIsNotNone(match)
    total, contiguous=(int(n)*1024 for n in match.groups())
    # On the test controller the available heap can be about 55 KiB
    # after starting the search task, with at least 32 KiB contiguous.
    self.assertLessEqual(total,55*1024)
    self.assertGreaterEqual(contiguous,16*1024)
    self.assertLessEqual(contiguous,32*1024)
 def test_geocode_distinguishes_busy_gate_from_low_heap(self):
    self.assertIn("lease.diagnostic().failure==NetworkBudgetFailure::MutexBusy",G)
 def test_finished_job_does_not_park_an_internal_stack(self):
    worker=G.split("void worker_main(void*)",1)[1].split("} // namespace",1)[0]
    self.assertIn("vTaskDelete(nullptr)",worker)
    self.assertNotIn("ulTaskNotifyTake",worker)

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
