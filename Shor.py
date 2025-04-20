import numpy as np
from qiskit import QuantumCircuit, QuantumRegister, ClassicalRegister
from qiskit_aer import AerSimulator
from fractions import Fraction
from math import gcd, pi

def is_prime(n):
    if n <= 1:
        return False
    if n <= 3:
        return True
    if n % 2 == 0:
        return False
    i = 3
    while i * i <= n:
        if n % i == 0:
            return False
        i += 2
    return True

def inverse_qft(circuit, qubits):
    
    n = len(qubits)
    for i in range(n // 2):
        circuit.swap(qubits[i], qubits[n - i - 1])
    for j in range(n):
        for k in range(j):
            circuit.cp(-pi / (2 ** (j - k)), qubits[k], qubits[j])
        circuit.h(qubits[j])

def quantum_phase_estimation(a, N, qpe_size=8):
    phase_register = QuantumRegister(qpe_size, 'phase')
    target_qubits = len(bin(N)) - 2
    target_register = QuantumRegister(target_qubits, 'target')
    classical_register = ClassicalRegister(qpe_size, 'classical')
    
    circuit = QuantumCircuit(phase_register, target_register, classical_register)
    
    circuit.x(target_register[0])
    
    circuit.h(phase_register)
    
    for i in range(qpe_size):
        circuit.cx(phase_register[i], target_register[0])
    
    inverse_qft(circuit, list(range(qpe_size)))
    
    circuit.measure(phase_register, classical_register)
    
    return circuit

def continued_fraction_expansion(numerator, denominator, max_depth=100):
 
    result = []
    for _ in range(max_depth):
        if denominator == 0:
            break
        q = numerator // denominator
        result.append(q)
        numerator, denominator = denominator, numerator - q * denominator
    return result

def find_period(a, N, qpe_size=8):

    circuit = quantum_phase_estimation(a, N, qpe_size)
    simulator = AerSimulator()
    job = simulator.run(circuit, shots=1000)
    result = job.result()
    counts = result.get_counts()
    
    most_common = max(counts.items(), key=lambda x: x[1])[0]
    phase = int(most_common, 2) / (2 ** qpe_size)
    
    fraction = Fraction(phase).limit_denominator(N)
    
    if fraction.denominator % 2 == 0:
        return fraction.denominator
    else:
        return 2 * fraction.denominator

def shor_algorithm(N):
    if N % 2 == 0:
        return 2, N // 2

    if is_prime(N):
        print(f"{N} is prime; no non-trivial factors exist.")
        return None

    for a in range(2, N):
        if gcd(a, N) != 1:
            factor = gcd(a, N)
            return factor, N // factor
        
        r = find_period(a, N)
        if r % 2 == 0:
            factor1 = gcd(pow(a, r // 2) + 1, N)
            factor2 = gcd(pow(a, r // 2) - 1, N)
            if factor1 not in (1, N):
                return factor1, N // factor1
            if factor2 not in (1, N):
                return factor2, N // factor2
    return None

def main():
    N = int(input("Enter an integer: "))
    print(f"Factoring {N} using Shor's algorithm (toy implementation)...")
    factors = shor_algorithm(N)
    if factors:
        factor1, factor2 = factors
        print(f"Factors of {N}: {factor1} and {factor2}")
        assert factor1 * factor2 == N, "Factorization verification failed!"
        print("Factorization verified successfully!")
    else:
        print("Failed to factorize the number using the algorithm.")

if __name__ == "__main__":
    main()
