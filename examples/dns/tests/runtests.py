import subprocess
import os

good_dir = "good_packets"
bad_dir = "bad_packets"

dns_parser_exe = "../dns_parser"

def run_test(file_path):
    # Run the DNS parser on a given file and return True if it passed, False if it failed.
    try:
        result = subprocess.run([dns_parser_exe, file_path],
                                capture_output=True, text=True, check=False)
        output = result.stdout
        if "Packet Passed" in output:
            return True, output
        else:
            return False, output
    except Exception as e:
        return False, str(e)

def test_directory(directory, should_pass):
    # Test all files in a directory. should_pass indicates expected result.
    files = [os.path.join(directory, f) for f in os.listdir(directory) if os.path.isfile(os.path.join(directory, f))]
    for f in files:
        passed, output = run_test(f)
        if passed == should_pass:
            if not should_pass:
                print(f"[PASS] (packet rejected) {f}")
            else:
                print(f"[PASS] (packet accepted) {f}")
        else:
            print(f"[FAIL] {f}")
            print("Output:")
            print(output)
            print("="*40)

def main():
    print("Testing good packets...")
    test_directory(good_dir, should_pass=True)
    print("\nTesting bad packets...")
    test_directory(bad_dir, should_pass=False)


if __name__ == "__main__":
    main()
