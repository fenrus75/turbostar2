#include <iostream>
#include <chrono>

// Version A: Most naive - check every number from 2 to n-1
extern bool is_prime_vA(int number);
extern bool is_prime_vB(int number);
extern bool is_prime_vC(int number);
extern bool is_prime_vD(int number);

int main(int argc, char **argv)
{
	std::cout << "Testing prime functions...\n";

	// Test all versions with some known primes and composites
	int test_numbers[] = {0, 1, 2, 3, 4, 5, 7, 9, 10, 11, 13, 15, 17, 19, 21, 23, 29, 31, 97, 100, 101};
	int num_tests = sizeof(test_numbers) / sizeof(test_numbers[0]);

	for (int version = 0; version < 4; version++) {
		bool (*is_prime)(int) = nullptr;
		const char* name = nullptr;
		switch (version) {
			case 0: is_prime = is_prime_vA; name = "A (naive)"; break;
			case 1: is_prime = is_prime_vB; name = "B (n/2)"; break;
			case 2: is_prime = is_prime_vC; name = "C (sqrt)"; break;
			case 3: is_prime = is_prime_vD; name = "D (6k±1)"; break;
		}

		std::cout << "\nVersion " << name << ":\n";
		for (int i = 0; i < num_tests; i++) {
			int n = test_numbers[i];
			bool result = is_prime(n);
			std::cout << "  is_prime(" << n << ") = " << (result ? "true" : "false") << "\n";
		}
	}

	// Performance benchmark: time each version for numbers 1 to 225000
	std::cout << "\n\n=== Performance Benchmark (1 to 25000) ===\n";

	auto benchmark = [&](bool (*is_prime)(int), const char* name, int max_num) {
		auto start = std::chrono::high_resolution_clock::now();
		int prime_count = 0;
		for (int x = 1; x < 25 ; x++) 
		for (int n = 1; n <= max_num; n++) {
			if (is_prime(n)) prime_count++;
		}
		auto end = std::chrono::high_resolution_clock::now();
		auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
		std::cout << name << ": " << duration.count() << " us (found " << prime_count << " primes)\n";
		return duration.count();
	};

	int max_number = 25000;
	benchmark(is_prime_vA, "Version A (naive)", max_number);
	benchmark(is_prime_vB, "Version B (n/2)", max_number);
	benchmark(is_prime_vC, "Version C (sqrt)", max_number);
	benchmark(is_prime_vD, "Version D (6k±1)", max_number);

	return 0;
}