from pathlib import Path
import unittest

HEADER=(Path(__file__).resolve().parents[1]/
        "firmware/main/met_gzip.hpp").read_text(encoding="utf-8")

class InflaterStack(unittest.TestCase):
    def test_no_hidden_stack_decompressor(self):
        code=HEADER.split("inline bool met_inflate_psram(",1)[1]
        self.assertNotIn("tinfl_decompress_mem_to_mem(",code)
        self.assertIn("tinfl_decompress(",code)
        self.assertIn("sizeof(tinfl_decompressor)",code)
        self.assertIn("MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT",code)
        self.assertIn("if(!state)return false;",code)
        self.assertIn("heap_caps_free(state);",code)
    def test_reject_incomplete_and_broken_streams(self):
        self.assertIn("status!=TINFL_STATUS_DONE",HEADER)
        self.assertIn("source_bytes!=compressed_size",HEADER)
        self.assertIn("produced!=isize",HEADER)
        self.assertIn("mz_crc32(",HEADER)
        self.assertIn("TINFL_FLAG_PARSE_ZLIB_HEADER",HEADER)
        self.assertNotIn("MALLOC_CAP_INTERNAL",HEADER)

if __name__=="__main__":unittest.main()
