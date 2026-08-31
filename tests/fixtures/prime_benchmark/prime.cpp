
// Version A: Most naive - check every number from 2 to n-1
bool is_prime_vA(int number) {
	if (number <= 1) return false;
	for (int i = 2; i < number; i++) {
		if (number % i == 0) return false;
	}
	return true;
}

// Version B: Slightly better - check from 2 to n/2
bool is_prime_vB(int number) {
	if (number <= 1) return false;
	if (number == 2) return true;
	for (int i = 2; i <= number / 2; i++) {
		if (number % i == 0) return false;
	}
	return true;
}

// Version C: Good - only check up to sqrt(number)
bool is_prime_vC(int number) {
	if (number <= 1) return false;
	if (number == 2) return true;
	if (number % 2 == 0) return false;
	for (int i = 3; i * i <= number; i += 2) {
		if (number % i == 0) return false;
	}
	return true;
}

// Version D: Most optimized - sqrt + skip evens + small number handling
bool is_prime_vD(int number) {
	if (number <= 1) return false;
	if (number <= 3) return true;
	if (number % 2 == 0 || number % 3 == 0) return false;
	for (int i = 5; i * i <= number; i += 6) {
		if (number % i == 0 || number % (i + 2) == 0) return false;
	}
	return true;
}
