import sys

sys.path.append("wrapper-python")
import pation
import os
import time

input_pdf = "125-65-PB.pdf"
output_pdf = "test_cpy.pdf"

print("Compressing PDF with Python wrapper...")
start_time = time.time()
success = pation.compress(
    input_path=input_pdf, output_path=output_pdf, dpi=150, quality=20
)
end_time = time.time()

if success:
    print(f"Success! Saved to {output_pdf}")
    print(f"Time taken: {end_time - start_time:.2f} seconds")
else:
    print("Failed to compress PDF")
